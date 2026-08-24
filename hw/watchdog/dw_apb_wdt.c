/*
 * Synopsys DesignWare APB watchdog
 *
 * The fixed-TOP implementation has a sticky enable bit and two response
 * modes.  In reset mode the first timeout requests the configured QEMU
 * watchdog action.  In interrupt mode the first timeout asserts the IRQ and
 * starts another TOP period; an uncleared interrupt at the next timeout
 * requests the watchdog action.  Reading EOI clears the interrupt without
 * restarting the counter, while a valid CRR kick does both.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/watchdog/dw_apb_wdt.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/watchdog.h"

static uint32_t dw_apb_wdt_top_count(unsigned int top)
{
    return UINT32_C(1) << (16 + (top & 0xf));
}

static void dw_apb_wdt_update_irq(DWAPBWDTState *s)
{
    qemu_set_irq(s->irq, !!(s->stat & DW_APB_WDT_STAT_INTERRUPT));
}

static void dw_apb_wdt_reload(DWAPBWDTState *s, uint32_t count, bool run)
{
    ptimer_transaction_begin(s->timer);
    ptimer_set_count(s->timer, count);
    if (run) {
        ptimer_run(s->timer, 1);
    }
    ptimer_transaction_commit(s->timer);
}

static void dw_apb_wdt_clear_interrupt(DWAPBWDTState *s)
{
    s->stat &= ~DW_APB_WDT_STAT_INTERRUPT;
    dw_apb_wdt_update_irq(s);
}

static void dw_apb_wdt_reset_state(DWAPBWDTState *s, bool in_transaction)
{
    s->cr = DW_APB_WDT_CR_RMOD;
    s->torr = 0;
    s->stat = 0;

    if (!in_transaction) {
        ptimer_transaction_begin(s->timer);
    }
    ptimer_stop(s->timer);
    ptimer_set_count(s->timer, s->counter_reset_value);
    ptimer_set_period_from_clock(s->timer, s->pclk, 1);
    if (!in_transaction) {
        ptimer_transaction_commit(s->timer);
    }
    dw_apb_wdt_update_irq(s);
}

static void dw_apb_wdt_reset(DeviceState *dev)
{
    dw_apb_wdt_reset_state(DW_APB_WDT(dev), false);
}

static void dw_apb_wdt_expire(void *opaque)
{
    DWAPBWDTState *s = opaque;

    if ((s->cr & DW_APB_WDT_CR_RMOD) &&
        !(s->stat & DW_APB_WDT_STAT_INTERRUPT)) {
        s->stat |= DW_APB_WDT_STAT_INTERRUPT;
        dw_apb_wdt_update_irq(s);
        ptimer_set_count(s->timer,
                         dw_apb_wdt_top_count(s->torr & 0xf));
        ptimer_run(s->timer, 1);
        return;
    }

    qemu_log_mask(CPU_LOG_RESET, "DesignWare APB watchdog expired\n");

    /*
     * watchdog_perform_action() may release the BQL.  A physical watchdog
     * reset includes the watchdog itself, so restore local reset state first
     * for actions which notify or reset the virtual machine.
     */
    switch (get_watchdog_action()) {
    case WATCHDOG_ACTION_DEBUG:
    case WATCHDOG_ACTION_NONE:
    case WATCHDOG_ACTION_PAUSE:
        break;
    default:
        dw_apb_wdt_reset_state(s, true);
        break;
    }
    watchdog_perform_action();
}

static uint64_t dw_apb_wdt_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    DWAPBWDTState *s = opaque;

    switch (offset) {
    case DW_APB_WDT_CR:
        return s->cr;
    case DW_APB_WDT_TORR:
        return s->torr;
    case DW_APB_WDT_CCVR:
        return ptimer_get_count(s->timer);
    case DW_APB_WDT_CRR:
        return 0;
    case DW_APB_WDT_STAT:
        return s->stat;
    case DW_APB_WDT_EOI: {
        uint32_t value = s->stat & DW_APB_WDT_STAT_INTERRUPT;

        dw_apb_wdt_clear_interrupt(s);
        return value;
    }
    case DW_APB_WDT_COMP_PARAM_5:
        return s->component_param_5;
    case DW_APB_WDT_COMP_PARAM_4:
        return s->component_param_4;
    case DW_APB_WDT_COMP_PARAM_3:
        return s->component_param_3;
    case DW_APB_WDT_COMP_PARAM_2:
        return s->component_param_2;
    case DW_APB_WDT_COMP_PARAM_1:
        return s->component_param_1;
    case DW_APB_WDT_COMP_VERSION:
        return s->component_version;
    case DW_APB_WDT_COMP_TYPE:
        return s->component_type;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from reserved register 0x%03"
                      HWADDR_PRIx "\n", TYPE_DW_APB_WDT, offset);
        return 0;
    }
}

static void dw_apb_wdt_write_control(DWAPBWDTState *s, uint32_t value)
{
    uint32_t old = s->cr;

    value &= DW_APB_WDT_CR_VALID;
    if (old & DW_APB_WDT_CR_ENABLE) {
        value |= DW_APB_WDT_CR_ENABLE;
    }
    s->cr = value;

    if (!(old & DW_APB_WDT_CR_ENABLE) &&
        (value & DW_APB_WDT_CR_ENABLE)) {
        dw_apb_wdt_reload(s,
                          dw_apb_wdt_top_count(extract32(s->torr, 4, 4)),
                          true);
    }
}

static void dw_apb_wdt_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned int size)
{
    DWAPBWDTState *s = opaque;
    uint32_t value32 = value;

    switch (offset) {
    case DW_APB_WDT_CR:
        dw_apb_wdt_write_control(s, value32);
        break;
    case DW_APB_WDT_TORR:
        s->torr = value32 & DW_APB_WDT_TORR_VALID;
        break;
    case DW_APB_WDT_CRR:
        if ((value32 & 0xff) == DW_APB_WDT_CRR_RESTART) {
            dw_apb_wdt_clear_interrupt(s);
            dw_apb_wdt_reload(s,
                              dw_apb_wdt_top_count(s->torr & 0xf),
                              s->cr & DW_APB_WDT_CR_ENABLE);
        }
        break;
    case DW_APB_WDT_CCVR:
    case DW_APB_WDT_STAT:
    case DW_APB_WDT_EOI:
    case DW_APB_WDT_COMP_PARAM_5:
    case DW_APB_WDT_COMP_PARAM_4:
    case DW_APB_WDT_COMP_PARAM_3:
    case DW_APB_WDT_COMP_PARAM_2:
    case DW_APB_WDT_COMP_PARAM_1:
    case DW_APB_WDT_COMP_VERSION:
    case DW_APB_WDT_COMP_TYPE:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register 0x%03"
                      HWADDR_PRIx "\n", TYPE_DW_APB_WDT, offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to reserved register 0x%03"
                      HWADDR_PRIx "\n", TYPE_DW_APB_WDT, offset);
        break;
    }
}

static bool dw_apb_wdt_access_valid(void *opaque, hwaddr offset,
                                     unsigned int size, bool is_write,
                                     MemTxAttrs attrs)
{
    return size == 4 && !(offset & 3);
}

static const MemoryRegionOps dw_apb_wdt_ops = {
    .read = dw_apb_wdt_read,
    .write = dw_apb_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = dw_apb_wdt_access_valid,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void dw_apb_wdt_clock_update(void *opaque, ClockEvent event)
{
    DWAPBWDTState *s = opaque;

    if (!s->timer) {
        return;
    }
    ptimer_transaction_begin(s->timer);
    ptimer_set_period_from_clock(s->timer, s->pclk, 1);
    ptimer_transaction_commit(s->timer);
}

static void dw_apb_wdt_reset_input(void *opaque, int n, int level)
{
    if (level) {
        dw_apb_wdt_reset(DEVICE(opaque));
    }
}

static int dw_apb_wdt_post_load(void *opaque, int version_id)
{
    DWAPBWDTState *s = opaque;

    if ((s->cr & ~DW_APB_WDT_CR_VALID) ||
        (s->torr & ~DW_APB_WDT_TORR_VALID) ||
        (s->stat & ~DW_APB_WDT_STAT_INTERRUPT) ||
        (s->stat && (!(s->cr & DW_APB_WDT_CR_ENABLE) ||
                     !(s->cr & DW_APB_WDT_CR_RMOD)))) {
        return -EINVAL;
    }
    dw_apb_wdt_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_dw_apb_wdt = {
    .name = TYPE_DW_APB_WDT,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = dw_apb_wdt_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pclk, DWAPBWDTState),
        VMSTATE_PTIMER(timer, DWAPBWDTState),
        VMSTATE_UINT32(cr, DWAPBWDTState),
        VMSTATE_UINT32(torr, DWAPBWDTState),
        VMSTATE_UINT32(stat, DWAPBWDTState),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property dw_apb_wdt_properties[] = {
    DEFINE_PROP_UINT32("component-param1", DWAPBWDTState,
                       component_param_1, DW_APB_WDT_PARAM_1_USE_FIX_TOP),
    DEFINE_PROP_UINT32("component-param2", DWAPBWDTState,
                       component_param_2, 0),
    DEFINE_PROP_UINT32("component-param3", DWAPBWDTState,
                       component_param_3, 0),
    DEFINE_PROP_UINT32("component-param4", DWAPBWDTState,
                       component_param_4, 0),
    DEFINE_PROP_UINT32("component-param5", DWAPBWDTState,
                       component_param_5, 0),
    DEFINE_PROP_UINT32("component-version", DWAPBWDTState,
                       component_version, 0),
    DEFINE_PROP_UINT32("component-type", DWAPBWDTState,
                       component_type, DW_APB_WDT_COMP_TYPE_VALUE),
    DEFINE_PROP_UINT32("counter-reset-value", DWAPBWDTState,
                       counter_reset_value, 0),
};

static void dw_apb_wdt_init(Object *obj)
{
    DWAPBWDTState *s = DW_APB_WDT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &dw_apb_wdt_ops, s,
                          TYPE_DW_APB_WDT, DW_APB_WDT_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->pclk = qdev_init_clock_in(DEVICE(s), "pclk",
                                 dw_apb_wdt_clock_update, s, ClockUpdate);
    qdev_init_gpio_in_named(DEVICE(s), dw_apb_wdt_reset_input, "reset", 1);
}

static void dw_apb_wdt_realize(DeviceState *dev, Error **errp)
{
    DWAPBWDTState *s = DW_APB_WDT(dev);
    const uint8_t policies = PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                             PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                             PTIMER_POLICY_NO_COUNTER_ROUND_DOWN;

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, "%s: pclk must be connected", TYPE_DW_APB_WDT);
        return;
    }
    if (!(s->component_param_1 & DW_APB_WDT_PARAM_1_USE_FIX_TOP)) {
        error_setg(errp, "%s: only fixed TOP values are supported",
                   TYPE_DW_APB_WDT);
        return;
    }

    s->timer = ptimer_init(dw_apb_wdt_expire, s, policies);
    ptimer_transaction_begin(s->timer);
    ptimer_set_period_from_clock(s->timer, s->pclk, 1);
    ptimer_transaction_commit(s->timer);
}

static void dw_apb_wdt_finalize(Object *obj)
{
    DWAPBWDTState *s = DW_APB_WDT(obj);

    if (s->timer) {
        ptimer_free(s->timer);
    }
}

static void dw_apb_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Synopsys DesignWare APB watchdog";
    dc->realize = dw_apb_wdt_realize;
    dc->vmsd = &vmstate_dw_apb_wdt;
    device_class_set_legacy_reset(dc, dw_apb_wdt_reset);
    device_class_set_props(dc, dw_apb_wdt_properties);
    set_bit(DEVICE_CATEGORY_WATCHDOG, dc->categories);
}

static const TypeInfo dw_apb_wdt_info = {
    .name = TYPE_DW_APB_WDT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWAPBWDTState),
    .instance_init = dw_apb_wdt_init,
    .instance_finalize = dw_apb_wdt_finalize,
    .class_init = dw_apb_wdt_class_init,
};

static void dw_apb_wdt_register_types(void)
{
    type_register_static(&dw_apb_wdt_info);
}

type_init(dw_apb_wdt_register_types)
