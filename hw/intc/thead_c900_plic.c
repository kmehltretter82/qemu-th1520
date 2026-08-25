/*
 * T-Head C900 Platform-Level Interrupt Controller
 *
 * The C900 PLIC uses the standard PLIC register geometry, but its public RTL
 * exposes behavior that is not present in QEMU's SiFive PLIC model: a
 * machine-mode control register delegates the complete aperture to S-mode,
 * pending words are writable, trigger type is supplied per source, and an
 * asserted level source is sampled again on completion.  Arbitration is
 * shared by the M and S contexts of each hart, with an M enable taking
 * precedence when a source is enabled in both contexts.
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/intc/thead_c900_plic.h"
#include "hw/pci/msi.h"
#include "migration/vmstate.h"
#include "system/cpus.h"
#include "target/riscv/cpu.h"
#include "target/riscv/cpu_bits.h"

typedef enum THeadC900PLICRegister {
    C900_PLIC_REG_INVALID,
    C900_PLIC_REG_PRIORITY,
    C900_PLIC_REG_PENDING,
    C900_PLIC_REG_ENABLE,
    C900_PLIC_REG_CONTROL,
    C900_PLIC_REG_THRESHOLD,
    C900_PLIC_REG_CLAIM,
} THeadC900PLICRegister;

typedef struct THeadC900PLICDecode {
    THeadC900PLICRegister reg;
    uint32_t index;
    uint32_t context;
} THeadC900PLICDecode;

typedef struct THeadC900PLICCandidate {
    uint32_t irq;
    bool machine;
} THeadC900PLICCandidate;

static uint32_t atomic_set_masked(uint32_t *ptr, uint32_t mask,
                                  uint32_t value)
{
    uint32_t old;
    uint32_t new;
    uint32_t observed = qatomic_read(ptr);

    do {
        old = observed;
        new = (old & ~mask) | (value & mask);
        observed = qatomic_cmpxchg(ptr, old, new);
    } while (observed != old);

    return old;
}

static bool thead_c900_plic_test_bit(const uint32_t *bitmap, uint32_t bit)
{
    return qatomic_read(&bitmap[bit >> 5]) & (1U << (bit & 31));
}

static void thead_c900_plic_set_bit(uint32_t *bitmap, uint32_t bit,
                                    bool level)
{
    uint32_t mask = 1U << (bit & 31);

    atomic_set_masked(&bitmap[bit >> 5], mask, level ? mask : 0);
}

static uint32_t thead_c900_plic_word_mask(THeadC900PLICState *s,
                                          uint32_t word)
{
    uint32_t remainder;

    if (word + 1 != s->bitfield_words) {
        return UINT32_MAX;
    }

    remainder = s->num_sources & 31;
    return remainder ? (1U << remainder) - 1 : UINT32_MAX;
}

static bool thead_c900_plic_decode(THeadC900PLICState *s, hwaddr addr,
                                   THeadC900PLICDecode *decode)
{
    hwaddr offset;
    uint32_t word;

    *decode = (THeadC900PLICDecode) {
        .reg = C900_PLIC_REG_INVALID,
    };

    if (addr - THEAD_C900_PLIC_PRIORITY_BASE < (s->num_sources << 2)) {
        decode->reg = C900_PLIC_REG_PRIORITY;
        decode->index = (addr - THEAD_C900_PLIC_PRIORITY_BASE) >> 2;
        return true;
    }

    if (addr >= THEAD_C900_PLIC_PENDING_BASE &&
        addr - THEAD_C900_PLIC_PENDING_BASE < (s->bitfield_words << 2)) {
        decode->reg = C900_PLIC_REG_PENDING;
        decode->index = (addr - THEAD_C900_PLIC_PENDING_BASE) >> 2;
        return true;
    }

    if (addr >= THEAD_C900_PLIC_ENABLE_BASE &&
        addr - THEAD_C900_PLIC_ENABLE_BASE <
            s->num_contexts * THEAD_C900_PLIC_ENABLE_STRIDE) {
        offset = addr - THEAD_C900_PLIC_ENABLE_BASE;
        decode->context = offset / THEAD_C900_PLIC_ENABLE_STRIDE;
        word = (offset % THEAD_C900_PLIC_ENABLE_STRIDE) >> 2;
        if (word < s->bitfield_words) {
            decode->reg = C900_PLIC_REG_ENABLE;
            decode->index = word;
            return true;
        }
        return false;
    }

    if (addr == THEAD_C900_PLIC_CONTROL) {
        decode->reg = C900_PLIC_REG_CONTROL;
        return true;
    }

    if (addr >= THEAD_C900_PLIC_CONTEXT_BASE &&
        addr - THEAD_C900_PLIC_CONTEXT_BASE <
            s->num_contexts * THEAD_C900_PLIC_CONTEXT_STRIDE) {
        offset = addr - THEAD_C900_PLIC_CONTEXT_BASE;
        decode->context = offset / THEAD_C900_PLIC_CONTEXT_STRIDE;
        switch (offset % THEAD_C900_PLIC_CONTEXT_STRIDE) {
        case 0:
            decode->reg = C900_PLIC_REG_THRESHOLD;
            return true;
        case 4:
            decode->reg = C900_PLIC_REG_CLAIM;
            return true;
        default:
            return false;
        }
    }

    return false;
}

static bool thead_c900_plic_context_is_supervisor(uint32_t context)
{
    return context & 1;
}

static uint32_t thead_c900_plic_context(uint32_t hart, bool supervisor)
{
    return hart * 2 + supervisor;
}

static bool thead_c900_plic_access_valid(void *opaque, hwaddr addr,
                                         unsigned size, bool is_write,
                                         MemTxAttrs attrs)
{
    THeadC900PLICState *s = opaque;
    THeadC900PLICDecode decode;
    privilege_mode_t mode = PRV_M;

    if (size != 4 || (addr & 3) ||
        !thead_c900_plic_decode(s, addr, &decode)) {
        return false;
    }

    /* QTest and debugger transactions have no executing CPU. */
    if (current_cpu) {
        RISCVCPU *cpu = RISCV_CPU(current_cpu);
        bool virt;

        riscv_cpu_eff_priv(&cpu->env, &mode, &virt);
    }

    if (mode == PRV_M) {
        return true;
    }
    if (mode != PRV_S || decode.reg == C900_PLIC_REG_CONTROL) {
        return false;
    }

    /*
     * Before delegation, the RTL permits S-mode to use only S-context enable,
     * threshold, and claim/complete registers.  Control bit zero promotes an
     * S transaction for the rest of the aperture, which is the operation
     * performed by OpenSBI for thead,c900-plic.
     */
    if (s->control & 1) {
        return true;
    }

    return (decode.reg == C900_PLIC_REG_ENABLE ||
            decode.reg == C900_PLIC_REG_THRESHOLD ||
            decode.reg == C900_PLIC_REG_CLAIM) &&
           thead_c900_plic_context_is_supervisor(decode.context);
}

static THeadC900PLICCandidate
thead_c900_plic_find_candidate(THeadC900PLICState *s, uint32_t hart)
{
    THeadC900PLICCandidate candidate = { 0 };
    uint32_t machine_context = thead_c900_plic_context(hart, false);
    uint32_t supervisor_context = thead_c900_plic_context(hart, true);
    uint32_t best = 0;

    for (uint32_t irq = 1; irq < s->num_sources; irq++) {
        uint32_t word = irq >> 5;
        uint32_t mask = 1U << (irq & 31);
        uint32_t priority;
        uint32_t score;
        bool machine;
        bool supervisor;

        if (!(qatomic_read(&s->pending[word]) & mask) ||
            (qatomic_read(&s->active[word]) & mask)) {
            continue;
        }

        priority = s->source_priority[irq];
        if (!priority) {
            continue;
        }

        machine = s->enable[machine_context * s->bitfield_words + word] &
                  mask;
        supervisor = s->enable[supervisor_context * s->bitfield_words +
                               word] & mask;
        if (!machine && !supervisor) {
            continue;
        }

        /* The RTL arbitrates {M-enable, priority}; M therefore wins ties. */
        score = priority | (machine ? 1U << 5 : 0);
        if (score > best) {
            best = score;
            candidate.irq = irq;
            candidate.machine = machine;
        }
    }

    return candidate;
}

static void thead_c900_plic_update(THeadC900PLICState *s)
{
    for (uint32_t hart = 0; hart < s->num_harts; hart++) {
        THeadC900PLICCandidate candidate =
            thead_c900_plic_find_candidate(s, hart);
        bool machine = false;
        bool supervisor = false;

        if (candidate.irq) {
            uint32_t context =
                thead_c900_plic_context(hart, !candidate.machine);
            bool above_threshold =
                s->source_priority[candidate.irq] > s->threshold[context];

            machine = candidate.machine && above_threshold;
            supervisor = !candidate.machine && above_threshold;
        }

        qemu_set_irq(s->m_external_irqs[hart], machine);
        qemu_set_irq(s->s_external_irqs[hart], supervisor);
    }
}

static uint32_t thead_c900_plic_claim(THeadC900PLICState *s,
                                      uint32_t context)
{
    uint32_t hart = context >> 1;
    bool supervisor = thead_c900_plic_context_is_supervisor(context);
    THeadC900PLICCandidate candidate =
        thead_c900_plic_find_candidate(s, hart);

    /* The RTL claim register follows arbitration, not the threshold output. */
    if (!candidate.irq || candidate.machine == supervisor) {
        return 0;
    }

    thead_c900_plic_set_bit(s->pending, candidate.irq, false);
    thead_c900_plic_set_bit(s->active, candidate.irq, true);
    thead_c900_plic_update(s);
    return candidate.irq;
}

static void thead_c900_plic_complete(THeadC900PLICState *s,
                                     uint32_t context, uint32_t irq)
{
    uint32_t word;
    uint32_t mask;

    if (!irq || irq >= s->num_sources) {
        return;
    }

    word = irq >> 5;
    mask = 1U << (irq & 31);

    /* C900 accepts completion only while that context enables the source. */
    if (!(s->enable[context * s->bitfield_words + word] & mask)) {
        return;
    }

    thead_c900_plic_set_bit(s->active, irq, false);
    if (!thead_c900_plic_test_bit(s->edge_trigger, irq) &&
        thead_c900_plic_test_bit(s->source_level, irq)) {
        thead_c900_plic_set_bit(s->pending, irq, true);
    }
    thead_c900_plic_update(s);
}

static uint64_t thead_c900_plic_read(void *opaque, hwaddr addr,
                                     unsigned size)
{
    THeadC900PLICState *s = opaque;
    THeadC900PLICDecode decode;

    if (!thead_c900_plic_decode(s, addr, &decode)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead-c900-plic: invalid read at 0x%08" HWADDR_PRIx
                      "\n", addr);
        return 0;
    }

    switch (decode.reg) {
    case C900_PLIC_REG_PRIORITY:
        return s->source_priority[decode.index];
    case C900_PLIC_REG_PENDING:
        return qatomic_read(&s->pending[decode.index]) &
               thead_c900_plic_word_mask(s, decode.index) &
               (decode.index ? UINT32_MAX : ~1U);
    case C900_PLIC_REG_ENABLE:
        return s->enable[decode.context * s->bitfield_words + decode.index] &
               thead_c900_plic_word_mask(s, decode.index) &
               (decode.index ? UINT32_MAX : ~1U);
    case C900_PLIC_REG_CONTROL:
        return s->control;
    case C900_PLIC_REG_THRESHOLD:
        return s->threshold[decode.context];
    case C900_PLIC_REG_CLAIM:
        return thead_c900_plic_claim(s, decode.context);
    default:
        g_assert_not_reached();
    }
}

static void thead_c900_plic_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned size)
{
    THeadC900PLICState *s = opaque;
    THeadC900PLICDecode decode;
    uint32_t word_mask;

    if (!thead_c900_plic_decode(s, addr, &decode)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead-c900-plic: invalid write at 0x%08" HWADDR_PRIx
                      "\n", addr);
        return;
    }

    switch (decode.reg) {
    case C900_PLIC_REG_PRIORITY:
        if (decode.index) {
            s->source_priority[decode.index] =
                value & THEAD_C900_PLIC_MAX_PRIORITY;
            thead_c900_plic_update(s);
        }
        break;
    case C900_PLIC_REG_PENDING:
        word_mask = thead_c900_plic_word_mask(s, decode.index);
        if (!decode.index) {
            word_mask &= ~1U;
        }
        qatomic_set(&s->pending[decode.index], (uint32_t)value & word_mask);
        thead_c900_plic_update(s);
        break;
    case C900_PLIC_REG_ENABLE:
        word_mask = thead_c900_plic_word_mask(s, decode.index);
        if (!decode.index) {
            word_mask &= ~1U;
        }
        s->enable[decode.context * s->bitfield_words + decode.index] =
            value & word_mask;
        thead_c900_plic_update(s);
        break;
    case C900_PLIC_REG_CONTROL:
        s->control = value & 1;
        break;
    case C900_PLIC_REG_THRESHOLD:
        s->threshold[decode.context] =
            value & THEAD_C900_PLIC_MAX_PRIORITY;
        thead_c900_plic_update(s);
        break;
    case C900_PLIC_REG_CLAIM:
        thead_c900_plic_complete(s, decode.context, value);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps thead_c900_plic_ops = {
    .read = thead_c900_plic_read,
    .write = thead_c900_plic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = thead_c900_plic_access_valid,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void thead_c900_plic_source(void *opaque, int irq, int level)
{
    THeadC900PLICState *s = opaque;
    uint32_t word;
    uint32_t mask;
    uint32_t old;

    if (!irq || irq >= s->num_sources) {
        return;
    }

    word = irq >> 5;
    mask = 1U << (irq & 31);
    old = atomic_set_masked(&s->source_level[word], mask,
                            level ? mask : 0);

    /* Both edge and level inputs latch only the low-to-high transition. */
    if (level && !(old & mask) &&
        !thead_c900_plic_test_bit(s->active, irq)) {
        thead_c900_plic_set_bit(s->pending, irq, true);
    }
    thead_c900_plic_update(s);
}

static void thead_c900_plic_edge_trigger(void *opaque, int irq, int level)
{
    THeadC900PLICState *s = opaque;

    if (!irq || irq >= s->num_sources) {
        return;
    }

    thead_c900_plic_set_bit(s->edge_trigger, irq, level);
}

static void thead_c900_plic_reset_enter(Object *obj, ResetType type)
{
    THeadC900PLICState *s = THEAD_C900_PLIC(obj);

    s->control = 0;
    memset(s->source_priority, 0,
           sizeof(*s->source_priority) * s->num_sources);
    memset(s->threshold, 0, sizeof(*s->threshold) * s->num_contexts);
    memset(s->pending, 0, sizeof(*s->pending) * s->bitfield_words);
    memset(s->active, 0, sizeof(*s->active) * s->bitfield_words);
    memset(s->enable, 0, sizeof(*s->enable) * s->num_enables);

    for (uint32_t hart = 0; hart < s->num_harts; hart++) {
        qemu_irq_lower(s->m_external_irqs[hart]);
        qemu_irq_lower(s->s_external_irqs[hart]);
    }
}

static void thead_c900_plic_reset_exit(Object *obj, ResetType type)
{
    THeadC900PLICState *s = THEAD_C900_PLIC(obj);

    /* A line held high through reset is sampled as a fresh assertion. */
    for (uint32_t irq = 1; irq < s->num_sources; irq++) {
        if (thead_c900_plic_test_bit(s->source_level, irq)) {
            thead_c900_plic_set_bit(s->pending, irq, true);
        }
    }
    thead_c900_plic_update(s);
}

static int thead_c900_plic_post_load(void *opaque, int version_id)
{
    THeadC900PLICState *s = opaque;

    s->control &= 1;
    s->source_priority[0] = 0;
    s->pending[0] &= ~1U;
    s->active[0] &= ~1U;
    s->source_level[0] &= ~1U;
    s->edge_trigger[0] &= ~1U;

    for (uint32_t irq = 1; irq < s->num_sources; irq++) {
        s->source_priority[irq] &= THEAD_C900_PLIC_MAX_PRIORITY;
    }
    for (uint32_t context = 0; context < s->num_contexts; context++) {
        s->threshold[context] &= THEAD_C900_PLIC_MAX_PRIORITY;
        s->enable[context * s->bitfield_words] &= ~1U;
    }
    for (uint32_t word = 0; word < s->bitfield_words; word++) {
        uint32_t mask = thead_c900_plic_word_mask(s, word);

        s->pending[word] &= mask;
        s->active[word] &= mask;
        s->source_level[word] &= mask;
        s->edge_trigger[word] &= mask;
        for (uint32_t context = 0; context < s->num_contexts; context++) {
            s->enable[context * s->bitfield_words + word] &= mask;
        }
    }

    thead_c900_plic_update(s);
    return 0;
}

static bool thead_c900_plic_legacy_needed(void *opaque)
{
    return false;
}

static int thead_c900_plic_legacy_pre_load(void *opaque)
{
    THeadC900PLICState *s = opaque;

    /*
     * The generic PLIC accepted S-mode accesses across its aperture.  Treat
     * that as delegated operation, and default state which did not exist in
     * its migration stream to inactive level-triggered inputs.  This follows
     * the current Ahead wiring contract; the old model's edge-like input
     * latch cannot be recovered without saved line and trigger state.
     */
    s->control = 1;
    memset(s->source_level, 0,
           sizeof(*s->source_level) * s->bitfield_words);
    memset(s->edge_trigger, 0,
           sizeof(*s->edge_trigger) * s->bitfield_words);
    return 0;
}

/*
 * The first Ahead machine used QEMU's generic SiFive PLIC.  Its arrays have
 * direct C900 counterparts, but its section name and payload were replaced
 * when the dedicated controller was introduced.  Keep this description
 * load-only: current streams must continue to use thead.c900-plic.
 */
static const VMStateDescription vmstate_thead_c900_plic_legacy_ahead = {
    .name = "riscv_sifive_plic",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_load = thead_c900_plic_legacy_pre_load,
    .post_load = thead_c900_plic_post_load,
    .needed = thead_c900_plic_legacy_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_VARRAY_UINT32(source_priority, THeadC900PLICState,
                              num_sources, 0, vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(threshold, THeadC900PLICState, num_contexts, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(pending, THeadC900PLICState, bitfield_words, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(active, THeadC900PLICState, bitfield_words, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(enable, THeadC900PLICState, num_enables, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_thead_c900_plic = {
    .name = TYPE_THEAD_C900_PLIC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = thead_c900_plic_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(control, THeadC900PLICState),
        VMSTATE_VARRAY_UINT32(source_priority, THeadC900PLICState,
                              num_sources, 0, vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(threshold, THeadC900PLICState, num_contexts, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(pending, THeadC900PLICState, bitfield_words, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(active, THeadC900PLICState, bitfield_words, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(enable, THeadC900PLICState, num_enables, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(source_level, THeadC900PLICState,
                              bitfield_words, 0, vmstate_info_uint32,
                              uint32_t),
        VMSTATE_VARRAY_UINT32(edge_trigger, THeadC900PLICState,
                              bitfield_words, 0, vmstate_info_uint32,
                              uint32_t),
        VMSTATE_END_OF_LIST()
    },
};

static void thead_c900_plic_realize(DeviceState *dev, Error **errp)
{
    THeadC900PLICState *s = THEAD_C900_PLIC(dev);

    if (!s->num_harts || s->num_harts > 32) {
        error_setg(errp, "C900 PLIC hart count must be between 1 and 32");
        return;
    }
    if (s->num_sources < 2 || s->num_sources > 1024) {
        error_setg(errp,
                   "C900 PLIC source count including source zero must be "
                   "between 2 and 1024");
        return;
    }

    s->bitfield_words = (s->num_sources + 31) >> 5;
    s->num_contexts = s->num_harts * 2;
    s->num_enables = s->num_contexts * s->bitfield_words;
    s->source_priority = g_new0(uint32_t, s->num_sources);
    s->threshold = g_new0(uint32_t, s->num_contexts);
    s->pending = g_new0(uint32_t, s->bitfield_words);
    s->active = g_new0(uint32_t, s->bitfield_words);
    s->enable = g_new0(uint32_t, s->num_enables);
    s->source_level = g_new0(uint32_t, s->bitfield_words);
    s->edge_trigger = g_new0(uint32_t, s->bitfield_words);
    s->m_external_irqs = g_new0(qemu_irq, s->num_harts);
    s->s_external_irqs = g_new0(qemu_irq, s->num_harts);

    if (s->legacy_ahead_vmstate &&
        (s->hartid_base != 0 || s->num_harts != 4 ||
         s->num_sources != 241 || s->num_contexts != 8 ||
         s->bitfield_words != 8 || s->num_enables != 64)) {
        error_setg(errp, "legacy Ahead PLIC VMState requires 4 harts and "
                   "241 sources at hart base 0");
        return;
    }

    qdev_init_gpio_in_named(dev, thead_c900_plic_source, "source",
                            s->num_sources);
    qdev_init_gpio_in_named(dev, thead_c900_plic_edge_trigger,
                            "edge-trigger", s->num_sources);
    qdev_init_gpio_out_named(dev, s->m_external_irqs, "mext",
                             s->num_harts);
    qdev_init_gpio_out_named(dev, s->s_external_irqs, "sext",
                             s->num_harts);

    for (uint32_t hart = 0; hart < s->num_harts; hart++) {
        CPUState *cs = cpu_by_arch_id(s->hartid_base + hart);
        RISCVCPU *cpu;

        if (!cs) {
            error_setg(errp, "C900 PLIC cannot find hart %u",
                       s->hartid_base + hart);
            return;
        }
        cpu = RISCV_CPU(cs);
        if (riscv_cpu_claim_interrupts(cpu, MIP_SEIP) < 0) {
            error_setg(errp, "C900 PLIC SEIP line already claimed for hart %u",
                       s->hartid_base + hart);
            return;
        }
    }

    memory_region_init_io(&s->mmio, OBJECT(dev), &thead_c900_plic_ops, s,
                          TYPE_THEAD_C900_PLIC, THEAD_C900_PLIC_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    msi_nonbroken = true;

    if (s->legacy_ahead_vmstate &&
        vmstate_register_with_alias_id(
            VMSTATE_IF(dev), 0,
            &vmstate_thead_c900_plic_legacy_ahead, s, -1, 0, errp) < 0) {
        return;
    }
}

static void thead_c900_plic_unrealize(DeviceState *dev)
{
    THeadC900PLICState *s = THEAD_C900_PLIC(dev);

    if (s->legacy_ahead_vmstate) {
        vmstate_unregister(VMSTATE_IF(dev),
                           &vmstate_thead_c900_plic_legacy_ahead, s);
    }
}

static void thead_c900_plic_finalize(Object *obj)
{
    THeadC900PLICState *s = THEAD_C900_PLIC(obj);

    g_free(s->source_priority);
    g_free(s->threshold);
    g_free(s->pending);
    g_free(s->active);
    g_free(s->enable);
    g_free(s->source_level);
    g_free(s->edge_trigger);
    g_free(s->m_external_irqs);
    g_free(s->s_external_irqs);
}

static const Property thead_c900_plic_properties[] = {
    DEFINE_PROP_UINT32("hartid-base", THeadC900PLICState, hartid_base, 0),
    DEFINE_PROP_UINT32("num-harts", THeadC900PLICState, num_harts, 1),
    DEFINE_PROP_UINT32("num-sources", THeadC900PLICState, num_sources, 2),
    DEFINE_PROP_BOOL("legacy-ahead-vmstate", THeadC900PLICState,
                     legacy_ahead_vmstate, false),
};

static void thead_c900_plic_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->realize = thead_c900_plic_realize;
    dc->unrealize = thead_c900_plic_unrealize;
    dc->vmsd = &vmstate_thead_c900_plic;
    device_class_set_props(dc, thead_c900_plic_properties);
    rc->phases.enter = thead_c900_plic_reset_enter;
    rc->phases.exit = thead_c900_plic_reset_exit;
}

static const TypeInfo thead_c900_plic_info = {
    .name = TYPE_THEAD_C900_PLIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(THeadC900PLICState),
    .instance_finalize = thead_c900_plic_finalize,
    .class_init = thead_c900_plic_class_init,
};

static void thead_c900_plic_register_types(void)
{
    type_register_static(&thead_c900_plic_info);
}

type_init(thead_c900_plic_register_types)
