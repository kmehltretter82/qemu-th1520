/*
 * T-Head C900 Core-Local Interrupt Controller
 *
 * The C900 CLINT is similar to the original SiFive CLINT layout, but it has
 * no memory-mapped mtime register, accepts only 32-bit APB accesses, and adds
 * supervisor software-interrupt and time-compare banks.  These differences
 * are visible in the openC910 RTL and are also accounted for by OpenSBI's
 * thead,c900-clint compatibility handling.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/intc/thead_c900_clint.h"
#include "migration/vmstate.h"
#include "system/cpus.h"
#include "target/riscv/cpu.h"
#include "target/riscv/cpu_bits.h"

struct THeadC900CLINTTimerContext {
    THeadC900CLINTState *clint;
    uint32_t hart;
    bool supervisor;
};

static uint64_t thead_c900_clint_raw_time(THeadC900CLINTState *s)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                    s->timebase_freq, NANOSECONDS_PER_SECOND);
}

static uint64_t thead_c900_clint_time(void *opaque)
{
    THeadC900CLINTState *s = opaque;

    return thead_c900_clint_raw_time(s) + s->time_delta;
}

static void thead_c900_clint_update_timer(THeadC900CLINTState *s,
                                          uint32_t hart, bool supervisor)
{
    uint64_t now = thead_c900_clint_time(s);
    uint64_t compare = supervisor ? s->stimecmp[hart] : s->mtimecmp[hart];
    QEMUTimer *timer = supervisor ? s->stimers[hart] : s->mtimers[hart];
    qemu_irq irq = supervisor ? s->stimer_irqs[hart] : s->mtimer_irqs[hart];
    uint64_t now_ns;
    uint64_t ns_diff;

    if (compare <= now) {
        timer_del(timer);
        qemu_irq_raise(irq);
        return;
    }

    qemu_irq_lower(irq);
    ns_diff = muldiv64_round_up(compare - now, NANOSECONDS_PER_SECOND,
                               s->timebase_freq);
    now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer_mod(timer, ns_diff > INT64_MAX - now_ns ? INT64_MAX
                                                   : now_ns + ns_diff);
}

static void thead_c900_clint_timer_cb(void *opaque)
{
    THeadC900CLINTTimerContext *context = opaque;

    /* Recheck the architectural counter to avoid fractional-tick early IRQs. */
    thead_c900_clint_update_timer(context->clint, context->hart,
                                  context->supervisor);
}

static bool thead_c900_clint_decode(THeadC900CLINTState *s, hwaddr addr,
                                    uint32_t *hart, bool *supervisor,
                                    bool *timer, bool *high)
{
    hwaddr offset;
    uint32_t stride;

    *high = false;
    if (addr < THEAD_C900_CLINT_MSIP_BASE + (s->num_harts << 2)) {
        offset = addr - THEAD_C900_CLINT_MSIP_BASE;
        stride = 4;
        *supervisor = false;
        *timer = false;
    } else if (addr >= THEAD_C900_CLINT_MTIMECMP_BASE &&
               addr < THEAD_C900_CLINT_MTIMECMP_BASE +
                      (s->num_harts << 3)) {
        offset = addr - THEAD_C900_CLINT_MTIMECMP_BASE;
        stride = 8;
        *supervisor = false;
        *timer = true;
        *high = offset & 4;
    } else if (addr >= THEAD_C900_CLINT_SSIP_BASE &&
               addr < THEAD_C900_CLINT_SSIP_BASE + (s->num_harts << 2)) {
        offset = addr - THEAD_C900_CLINT_SSIP_BASE;
        stride = 4;
        *supervisor = true;
        *timer = false;
    } else if (addr >= THEAD_C900_CLINT_STIMECMP_BASE &&
               addr < THEAD_C900_CLINT_STIMECMP_BASE +
                      (s->num_harts << 3)) {
        offset = addr - THEAD_C900_CLINT_STIMECMP_BASE;
        stride = 8;
        *supervisor = true;
        *timer = true;
        *high = offset & 4;
    } else {
        return false;
    }

    *hart = offset / stride;
    return true;
}

static bool thead_c900_clint_access_valid(void *opaque, hwaddr addr,
                                          unsigned size, bool is_write,
                                          MemTxAttrs attrs)
{
    THeadC900CLINTState *s = opaque;
    privilege_mode_t mode = PRV_M;
    uint32_t hart;
    bool supervisor;
    bool timer;
    bool high;

    if (size != 4 || (addr & 3) ||
        !thead_c900_clint_decode(s, addr, &hart, &supervisor, &timer, &high)) {
        return false;
    }

    /* QTest and debugger transactions have no executing CPU. */
    if (current_cpu) {
        RISCVCPU *cpu = RISCV_CPU(current_cpu);
        bool virt;

        riscv_cpu_eff_priv(&cpu->env, &mode, &virt);
    }

    return mode == PRV_M || (supervisor && mode == PRV_S);
}

static uint64_t thead_c900_clint_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    THeadC900CLINTState *s = opaque;
    uint32_t hart;
    bool supervisor;
    bool timer;
    bool high;

    if (!thead_c900_clint_decode(s, addr, &hart, &supervisor, &timer, &high)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead-c900-clint: invalid read at 0x%04" HWADDR_PRIx
                      "\n", addr);
        return 0;
    }

    if (!timer) {
        return supervisor ? s->ssip[hart] : s->msip[hart];
    }

    return extract64(supervisor ? s->stimecmp[hart] : s->mtimecmp[hart],
                     high ? 32 : 0, 32);
}

static void thead_c900_clint_write(void *opaque, hwaddr addr, uint64_t value,
                                   unsigned size)
{
    THeadC900CLINTState *s = opaque;
    uint64_t *compare;
    uint32_t hart;
    bool supervisor;
    bool timer;
    bool high;

    if (!thead_c900_clint_decode(s, addr, &hart, &supervisor, &timer, &high)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead-c900-clint: invalid write at 0x%04" HWADDR_PRIx
                      "\n", addr);
        return;
    }

    if (!timer) {
        uint32_t *pending = supervisor ? s->ssip : s->msip;
        qemu_irq irq = supervisor ? s->ssip_irqs[hart] : s->msip_irqs[hart];

        pending[hart] = value & 1;
        qemu_set_irq(irq, pending[hart]);
        return;
    }

    compare = supervisor ? s->stimecmp : s->mtimecmp;
    if (high) {
        compare[hart] = deposit64(compare[hart], 32, 32, value);
    } else {
        compare[hart] = deposit64(compare[hart], 0, 32, value);
    }
    thead_c900_clint_update_timer(s, hart, supervisor);
}

static const MemoryRegionOps thead_c900_clint_ops = {
    .read = thead_c900_clint_read,
    .write = thead_c900_clint_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = thead_c900_clint_access_valid,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void thead_c900_clint_reset_enter(Object *obj, ResetType type)
{
    THeadC900CLINTState *s = THEAD_C900_CLINT(obj);

    for (uint32_t i = 0; i < s->num_harts; i++) {
        s->msip[i] = 0;
        s->ssip[i] = 0;
        s->mtimecmp[i] = UINT64_MAX;
        s->stimecmp[i] = UINT64_MAX;
        timer_del(s->mtimers[i]);
        timer_del(s->stimers[i]);
        qemu_irq_lower(s->msip_irqs[i]);
        qemu_irq_lower(s->ssip_irqs[i]);
        qemu_irq_lower(s->mtimer_irqs[i]);
        qemu_irq_lower(s->stimer_irqs[i]);
    }
}

static int thead_c900_clint_post_load(void *opaque, int version_id)
{
    THeadC900CLINTState *s = opaque;

    /* QEMU_CLOCK_VIRTUAL need not have the same origin on the destination. */
    s->time_delta = s->time_at_save - thead_c900_clint_raw_time(s);
    for (uint32_t i = 0; i < s->num_harts; i++) {
        qemu_set_irq(s->msip_irqs[i], s->msip[i]);
        qemu_set_irq(s->ssip_irqs[i], s->ssip[i]);
        thead_c900_clint_update_timer(s, i, false);
        thead_c900_clint_update_timer(s, i, true);
    }
    return 0;
}

static bool thead_c900_clint_legacy_needed(void *opaque)
{
    return false;
}

static int thead_c900_clint_legacy_pre_load(void *opaque)
{
    THeadC900CLINTState *s = opaque;

    for (uint32_t i = 0; i < s->num_harts; i++) {
        s->msip[i] = 0;
        s->ssip[i] = 0;
        /*
         * The old device had no supervisor timer state.  Do not turn a
         * software-written CPU MIP.STIP into a persistent C900 deadline that
         * an old guest may not know how to clear.
         */
        s->stimecmp[i] = UINT64_MAX;
        timer_del(s->stimers[i]);
    }
    return 0;
}

static int thead_c900_clint_legacy_post_load(void *opaque, int version_id)
{
    THeadC900CLINTState *s = opaque;

    /*
     * The old ACLINT stream saved an offset from QEMU_CLOCK_VIRTUAL rather
     * than the architectural counter.  Reconstruct the latter before using
     * the C900 post-load path.  Software-interrupt state lived in each CPU's
     * migrated MIP field because the old SWI device had no VMState section.
     */
    s->time_at_save = thead_c900_clint_raw_time(s) + s->time_delta;
    for (uint32_t i = 0; i < s->num_harts; i++) {
        CPUState *cs = cpu_by_arch_id(s->hartid_base + i);
        CPURISCVState *env;

        if (!cs) {
            return -EINVAL;
        }
        env = cpu_env(cs);
        s->msip[i] = !!(env->mip & MIP_MSIP);
        s->ssip[i] = !!(env->mip & MIP_SSIP);
    }
    return thead_c900_clint_post_load(s, 1);
}

/*
 * Early Ahead revisions used the generic ACLINT MTIMER.  Consume its exact
 * version-3 payload but never emit that obsolete section from a current
 * machine.  The loaded timer deadlines are rescheduled from mtimecmp by the
 * C900 post-load path.  That old board did not expose TIME through this
 * device; loading its state does not remove the current C900 TIME facility.
 */
static const VMStateDescription vmstate_thead_c900_clint_legacy_ahead = {
    .name = "riscv_mtimer",
    .version_id = 3,
    .minimum_version_id = 3,
    .pre_load = thead_c900_clint_legacy_pre_load,
    .post_load = thead_c900_clint_legacy_post_load,
    .needed = thead_c900_clint_legacy_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(time_delta, THeadC900CLINTState),
        VMSTATE_VARRAY_UINT32(mtimecmp, THeadC900CLINTState, num_harts, 0,
                              vmstate_info_uint64, uint64_t),
        VMSTATE_VARRAY_OF_POINTER_UINT32(mtimers, THeadC900CLINTState,
                                         num_harts, 0, vmstate_info_timer,
                                         QEMUTimer),
        VMSTATE_END_OF_LIST()
    },
};

static int thead_c900_clint_pre_save(void *opaque)
{
    THeadC900CLINTState *s = opaque;

    s->time_at_save = thead_c900_clint_time(s);
    return 0;
}

static const VMStateDescription vmstate_thead_c900_clint = {
    .name = TYPE_THEAD_C900_CLINT,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = thead_c900_clint_pre_save,
    .post_load = thead_c900_clint_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(time_at_save, THeadC900CLINTState),
        VMSTATE_VARRAY_UINT32(msip, THeadC900CLINTState, num_harts, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(ssip, THeadC900CLINTState, num_harts, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(mtimecmp, THeadC900CLINTState, num_harts, 0,
                              vmstate_info_uint64, uint64_t),
        VMSTATE_VARRAY_UINT32(stimecmp, THeadC900CLINTState, num_harts, 0,
                              vmstate_info_uint64, uint64_t),
        VMSTATE_END_OF_LIST()
    },
};

static void thead_c900_clint_realize(DeviceState *dev, Error **errp)
{
    THeadC900CLINTState *s = THEAD_C900_CLINT(dev);

    if (!s->num_harts || s->num_harts > 4095) {
        error_setg(errp, "C900 CLINT hart count must be between 1 and 4095");
        return;
    }
    if (!s->timebase_freq) {
        error_setg(errp, "C900 CLINT timebase frequency must be nonzero");
        return;
    }
    if (s->legacy_ahead_vmstate &&
        (s->hartid_base != 0 || s->num_harts != 4 ||
         s->timebase_freq != 3000000)) {
        error_setg(errp, "legacy Ahead MTIMER VMState requires 4 harts at "
                   "hart base 0 and a 3 MHz timebase");
        return;
    }

    s->msip = g_new0(uint32_t, s->num_harts);
    s->ssip = g_new0(uint32_t, s->num_harts);
    s->mtimecmp = g_new(uint64_t, s->num_harts);
    s->stimecmp = g_new(uint64_t, s->num_harts);
    s->mtimers = g_new0(QEMUTimer *, s->num_harts);
    s->stimers = g_new0(QEMUTimer *, s->num_harts);
    s->timer_contexts = g_new0(THeadC900CLINTTimerContext,
                               s->num_harts * 2);
    s->msip_irqs = g_new0(qemu_irq, s->num_harts);
    s->mtimer_irqs = g_new0(qemu_irq, s->num_harts);
    s->ssip_irqs = g_new0(qemu_irq, s->num_harts);
    s->stimer_irqs = g_new0(qemu_irq, s->num_harts);

    qdev_init_gpio_out_named(dev, s->msip_irqs, "msip", s->num_harts);
    qdev_init_gpio_out_named(dev, s->mtimer_irqs, "mtimer", s->num_harts);
    qdev_init_gpio_out_named(dev, s->ssip_irqs, "ssip", s->num_harts);
    qdev_init_gpio_out_named(dev, s->stimer_irqs, "stimer", s->num_harts);

    for (uint32_t i = 0; i < s->num_harts; i++) {
        CPUState *cs = cpu_by_arch_id(s->hartid_base + i);
        RISCVCPU *cpu;
        THeadC900CLINTTimerContext *mctx = &s->timer_contexts[i * 2];
        THeadC900CLINTTimerContext *sctx = &s->timer_contexts[i * 2 + 1];

        if (!cs) {
            error_setg(errp, "C900 CLINT cannot find hart %u",
                       s->hartid_base + i);
            return;
        }
        cpu = RISCV_CPU(cs);
        if (riscv_cpu_claim_interrupts(cpu, MIP_MSIP | MIP_MTIP |
                                           MIP_SSIP | MIP_STIP) < 0) {
            error_setg(errp, "C900 CLINT interrupt lines already claimed for "
                       "hart %u", s->hartid_base + i);
            return;
        }

        cpu->env.rdtime_fn = thead_c900_clint_time;
        cpu->env.rdtime_fn_arg = s;

        *mctx = (THeadC900CLINTTimerContext) {
            .clint = s,
            .hart = i,
            .supervisor = false,
        };
        *sctx = (THeadC900CLINTTimerContext) {
            .clint = s,
            .hart = i,
            .supervisor = true,
        };
        s->mtimers[i] = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     thead_c900_clint_timer_cb, mctx);
        s->stimers[i] = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     thead_c900_clint_timer_cb, sctx);
    }

    memory_region_init_io(&s->mmio, OBJECT(dev), &thead_c900_clint_ops, s,
                          TYPE_THEAD_C900_CLINT, THEAD_C900_CLINT_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    if (s->legacy_ahead_vmstate &&
        vmstate_register_with_alias_id(
            VMSTATE_IF(dev), 0,
            &vmstate_thead_c900_clint_legacy_ahead, s, -1, 0, errp) < 0) {
        return;
    }
}

static void thead_c900_clint_unrealize(DeviceState *dev)
{
    THeadC900CLINTState *s = THEAD_C900_CLINT(dev);

    if (s->legacy_ahead_vmstate) {
        vmstate_unregister(VMSTATE_IF(dev),
                           &vmstate_thead_c900_clint_legacy_ahead, s);
    }
}

static void thead_c900_clint_finalize(Object *obj)
{
    THeadC900CLINTState *s = THEAD_C900_CLINT(obj);

    for (uint32_t i = 0; i < s->num_harts; i++) {
        timer_free(s->mtimers ? s->mtimers[i] : NULL);
        timer_free(s->stimers ? s->stimers[i] : NULL);
    }
    g_free(s->msip);
    g_free(s->ssip);
    g_free(s->mtimecmp);
    g_free(s->stimecmp);
    g_free(s->mtimers);
    g_free(s->stimers);
    g_free(s->timer_contexts);
    g_free(s->msip_irqs);
    g_free(s->mtimer_irqs);
    g_free(s->ssip_irqs);
    g_free(s->stimer_irqs);
}

static const Property thead_c900_clint_properties[] = {
    DEFINE_PROP_UINT32("hartid-base", THeadC900CLINTState, hartid_base, 0),
    DEFINE_PROP_UINT32("num-harts", THeadC900CLINTState, num_harts, 1),
    DEFINE_PROP_UINT32("timebase-freq", THeadC900CLINTState,
                       timebase_freq, 0),
    DEFINE_PROP_BOOL("legacy-ahead-vmstate", THeadC900CLINTState,
                     legacy_ahead_vmstate, false),
};

static void thead_c900_clint_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->realize = thead_c900_clint_realize;
    dc->unrealize = thead_c900_clint_unrealize;
    dc->vmsd = &vmstate_thead_c900_clint;
    device_class_set_props(dc, thead_c900_clint_properties);
    rc->phases.enter = thead_c900_clint_reset_enter;
}

static const TypeInfo thead_c900_clint_info = {
    .name = TYPE_THEAD_C900_CLINT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(THeadC900CLINTState),
    .instance_finalize = thead_c900_clint_finalize,
    .class_init = thead_c900_clint_class_init,
};

static void thead_c900_clint_register_types(void)
{
    type_register_static(&thead_c900_clint_info);
}

type_init(thead_c900_clint_register_types)
