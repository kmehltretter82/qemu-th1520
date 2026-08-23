/*
 * Synopsys DesignWare APB timer
 *
 * This models the software-visible four-counter component used by TH1520.
 * Cascade wiring, per-counter synthesized clocks, and physical PWM outputs
 * are integration options and are deliberately not inferred here.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/timer/dw_apb_timer.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define DW_APB_TIMER_CHANNEL_STRIDE       0x14
#define DW_APB_TIMER_LOAD_COUNT           0x00
#define DW_APB_TIMER_CURRENT_VALUE        0x04
#define DW_APB_TIMER_CONTROL              0x08
#define DW_APB_TIMER_EOI                  0x0c
#define DW_APB_TIMER_INT_STATUS           0x10

#define DW_APB_TIMERS_INT_STATUS          0xa0
#define DW_APB_TIMERS_EOI                 0xa4
#define DW_APB_TIMERS_RAW_INT_STATUS      0xa8
#define DW_APB_TIMERS_COMP_VERSION        0xac
#define DW_APB_TIMER_LOAD_COUNT2_BASE     0xb0
#define DW_APB_TIMER_PROTECTION_BASE      0xd0

#define DW_APB_TIMER_CONTROL_ENABLE       BIT(0)
#define DW_APB_TIMER_CONTROL_MODE         BIT(1)
#define DW_APB_TIMER_CONTROL_INT_MASK     BIT(2)
#define DW_APB_TIMER_CONTROL_PWM          BIT(3)
#define DW_APB_TIMER_CONTROL_VALID        0x0f
#define DW_APB_TIMER_PROTECTION_VALID     0x07

#define DW_APB_TIMER_CURRENT_RESET        0x80000000

static bool dw_apb_timer_masked_status(DWAPBTimerState *s,
                                       unsigned int channel)
{
    return s->raw_intr[channel] &&
           !(s->control[channel] & DW_APB_TIMER_CONTROL_INT_MASK);
}

static uint32_t dw_apb_timers_status(DWAPBTimerState *s, bool masked)
{
    uint32_t status = 0;

    for (unsigned int i = 0; i < DW_APB_TIMER_CHANNELS; i++) {
        if (masked ? dw_apb_timer_masked_status(s, i) : s->raw_intr[i]) {
            status |= BIT(i);
        }
    }
    return status;
}

static void dw_apb_timer_update_irq(DWAPBTimerState *s,
                                     unsigned int channel)
{
    qemu_set_irq(s->irq[channel], dw_apb_timer_masked_status(s, channel));
}

static void dw_apb_timer_update_irqs(DWAPBTimerState *s)
{
    for (unsigned int i = 0; i < DW_APB_TIMER_CHANNELS; i++) {
        dw_apb_timer_update_irq(s, i);
    }
}

static void dw_apb_timer_clear_irq(DWAPBTimerState *s,
                                    unsigned int channel)
{
    s->raw_intr[channel] = 0;
    dw_apb_timer_update_irq(s, channel);
}

static void dw_apb_timer_write_load(DWAPBTimerState *s,
                                     unsigned int channel, uint32_t value)
{
    ptimer_state *timer = s->timer[channel];
    uint32_t limit;

    s->load_count[channel] = value;
    limit = s->control[channel] & DW_APB_TIMER_CONTROL_MODE ?
            value : UINT32_MAX;

    ptimer_transaction_begin(timer);
    ptimer_set_limit(timer, limit, 0);
    ptimer_set_count(timer, value);
    ptimer_transaction_commit(timer);
}

static void dw_apb_timer_write_control(DWAPBTimerState *s,
                                        unsigned int channel,
                                        uint32_t value)
{
    ptimer_state *timer = s->timer[channel];
    uint32_t old = s->control[channel];

    value &= DW_APB_TIMER_CONTROL_VALID;
    ptimer_transaction_begin(timer);

    if ((old & DW_APB_TIMER_CONTROL_ENABLE) &&
        !(value & DW_APB_TIMER_CONTROL_ENABLE)) {
        ptimer_stop(timer);
    }

    if ((old ^ value) & DW_APB_TIMER_CONTROL_MODE) {
        uint32_t limit = value & DW_APB_TIMER_CONTROL_MODE ?
                         s->load_count[channel] : UINT32_MAX;

        ptimer_set_limit(timer, limit, 0);
    }

    s->control[channel] = value;
    if (value & DW_APB_TIMER_CONTROL_ENABLE) {
        ptimer_run(timer, 0);
    }
    ptimer_transaction_commit(timer);

    if ((value & DW_APB_TIMER_CONTROL_PWM) &&
        !(old & DW_APB_TIMER_CONTROL_PWM)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: timer %u PWM output is not connected\n",
                      TYPE_DW_APB_TIMER, channel);
    }
    dw_apb_timer_update_irq(s, channel);
}

static uint64_t dw_apb_timer_read(void *opaque, hwaddr offset,
                                  unsigned int size)
{
    DWAPBTimerState *s = opaque;

    if (offset < DW_APB_TIMER_CHANNELS * DW_APB_TIMER_CHANNEL_STRIDE) {
        unsigned int channel = offset / DW_APB_TIMER_CHANNEL_STRIDE;

        switch (offset % DW_APB_TIMER_CHANNEL_STRIDE) {
        case DW_APB_TIMER_LOAD_COUNT:
            return s->load_count[channel];
        case DW_APB_TIMER_CURRENT_VALUE:
            return ptimer_get_count(s->timer[channel]);
        case DW_APB_TIMER_CONTROL:
            return s->control[channel];
        case DW_APB_TIMER_EOI:
            dw_apb_timer_clear_irq(s, channel);
            return 0;
        case DW_APB_TIMER_INT_STATUS:
            return dw_apb_timer_masked_status(s, channel);
        default:
            g_assert_not_reached();
        }
    }

    switch (offset) {
    case DW_APB_TIMERS_INT_STATUS:
        return dw_apb_timers_status(s, true);
    case DW_APB_TIMERS_EOI:
        for (unsigned int i = 0; i < DW_APB_TIMER_CHANNELS; i++) {
            s->raw_intr[i] = 0;
        }
        dw_apb_timer_update_irqs(s);
        return 0;
    case DW_APB_TIMERS_RAW_INT_STATUS:
        return dw_apb_timers_status(s, false);
    case DW_APB_TIMERS_COMP_VERSION:
        return s->component_version;
    case DW_APB_TIMER_LOAD_COUNT2_BASE ...
         DW_APB_TIMER_LOAD_COUNT2_BASE + 4 * (DW_APB_TIMER_CHANNELS - 1):
        return s->load_count2[(offset - DW_APB_TIMER_LOAD_COUNT2_BASE) / 4];
    case DW_APB_TIMER_PROTECTION_BASE ...
         DW_APB_TIMER_PROTECTION_BASE + 4 * (DW_APB_TIMER_CHANNELS - 1):
        return s->protection[(offset - DW_APB_TIMER_PROTECTION_BASE) / 4];
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from reserved register 0x%03"
                      HWADDR_PRIx "\n", TYPE_DW_APB_TIMER, offset);
        return 0;
    }
}

static void dw_apb_timer_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned int size)
{
    DWAPBTimerState *s = opaque;
    uint32_t value32 = value;

    if (offset < DW_APB_TIMER_CHANNELS * DW_APB_TIMER_CHANNEL_STRIDE) {
        unsigned int channel = offset / DW_APB_TIMER_CHANNEL_STRIDE;

        switch (offset % DW_APB_TIMER_CHANNEL_STRIDE) {
        case DW_APB_TIMER_LOAD_COUNT:
            dw_apb_timer_write_load(s, channel, value32);
            return;
        case DW_APB_TIMER_CONTROL:
            dw_apb_timer_write_control(s, channel, value32);
            return;
        case DW_APB_TIMER_CURRENT_VALUE:
        case DW_APB_TIMER_EOI:
        case DW_APB_TIMER_INT_STATUS:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to read-only timer %u register 0x%02"
                          HWADDR_PRIx "\n", TYPE_DW_APB_TIMER, channel,
                          offset % DW_APB_TIMER_CHANNEL_STRIDE);
            return;
        default:
            g_assert_not_reached();
        }
    }

    switch (offset) {
    case DW_APB_TIMER_LOAD_COUNT2_BASE ...
         DW_APB_TIMER_LOAD_COUNT2_BASE + 4 * (DW_APB_TIMER_CHANNELS - 1):
        s->load_count2[(offset - DW_APB_TIMER_LOAD_COUNT2_BASE) / 4] =
            value32;
        break;
    case DW_APB_TIMER_PROTECTION_BASE ...
         DW_APB_TIMER_PROTECTION_BASE + 4 * (DW_APB_TIMER_CHANNELS - 1):
        s->protection[(offset - DW_APB_TIMER_PROTECTION_BASE) / 4] =
            value32 & DW_APB_TIMER_PROTECTION_VALID;
        break;
    case DW_APB_TIMERS_INT_STATUS:
    case DW_APB_TIMERS_EOI:
    case DW_APB_TIMERS_RAW_INT_STATUS:
    case DW_APB_TIMERS_COMP_VERSION:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register 0x%03"
                      HWADDR_PRIx "\n", TYPE_DW_APB_TIMER, offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to reserved register 0x%03"
                      HWADDR_PRIx "\n", TYPE_DW_APB_TIMER, offset);
        break;
    }
}

static bool dw_apb_timer_access_valid(void *opaque, hwaddr offset,
                                      unsigned int size, bool is_write,
                                      MemTxAttrs attrs)
{
    return size == 4 && !(offset & 3);
}

static const MemoryRegionOps dw_apb_timer_ops = {
    .read = dw_apb_timer_read,
    .write = dw_apb_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = dw_apb_timer_access_valid,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void dw_apb_timer_tick(void *opaque)
{
    DWAPBTimerContext *context = opaque;
    DWAPBTimerState *s = context->parent;

    s->raw_intr[context->index] = 1;
    dw_apb_timer_update_irq(s, context->index);
}

static void dw_apb_timer_clk_update(void *opaque, ClockEvent event)
{
    DWAPBTimerState *s = opaque;

    for (unsigned int i = 0; i < DW_APB_TIMER_CHANNELS; i++) {
        if (!s->timer[i]) {
            continue;
        }
        ptimer_transaction_begin(s->timer[i]);
        ptimer_set_period_from_clock(s->timer[i], s->timer_clk, 1);
        ptimer_transaction_commit(s->timer[i]);
    }
}

static void dw_apb_timer_reset(DeviceState *dev)
{
    DWAPBTimerState *s = DW_APB_TIMER(dev);

    for (unsigned int i = 0; i < DW_APB_TIMER_CHANNELS; i++) {
        s->load_count[i] = 0;
        s->control[i] = 0;
        s->raw_intr[i] = 0;
        s->load_count2[i] = 0;
        s->protection[i] = 2;

        ptimer_transaction_begin(s->timer[i]);
        ptimer_stop(s->timer[i]);
        ptimer_set_limit(s->timer[i], UINT32_MAX, 0);
        ptimer_set_count(s->timer[i], DW_APB_TIMER_CURRENT_RESET);
        ptimer_set_period_from_clock(s->timer[i], s->timer_clk, 1);
        ptimer_transaction_commit(s->timer[i]);
    }
    dw_apb_timer_update_irqs(s);
}

static int dw_apb_timer_post_load(void *opaque, int version_id)
{
    DWAPBTimerState *s = opaque;

    for (unsigned int i = 0; i < DW_APB_TIMER_CHANNELS; i++) {
        if ((s->control[i] & ~DW_APB_TIMER_CONTROL_VALID) ||
            s->raw_intr[i] > 1 ||
            (s->protection[i] & ~DW_APB_TIMER_PROTECTION_VALID)) {
            return -EINVAL;
        }
    }
    dw_apb_timer_update_irqs(s);
    return 0;
}

static const VMStateDescription vmstate_dw_apb_timer = {
    .name = TYPE_DW_APB_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = dw_apb_timer_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PTIMER_ARRAY(timer, DWAPBTimerState, DW_APB_TIMER_CHANNELS),
        VMSTATE_CLOCK(timer_clk, DWAPBTimerState),
        VMSTATE_UINT32_ARRAY(load_count, DWAPBTimerState,
                             DW_APB_TIMER_CHANNELS),
        VMSTATE_UINT32_ARRAY(control, DWAPBTimerState,
                             DW_APB_TIMER_CHANNELS),
        VMSTATE_UINT32_ARRAY(raw_intr, DWAPBTimerState,
                             DW_APB_TIMER_CHANNELS),
        VMSTATE_UINT32_ARRAY(load_count2, DWAPBTimerState,
                             DW_APB_TIMER_CHANNELS),
        VMSTATE_UINT32_ARRAY(protection, DWAPBTimerState,
                             DW_APB_TIMER_CHANNELS),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property dw_apb_timer_properties[] = {
    DEFINE_PROP_UINT32("component-version", DWAPBTimerState,
                       component_version, 0),
};

static void dw_apb_timer_init(Object *obj)
{
    DWAPBTimerState *s = DW_APB_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &dw_apb_timer_ops, s,
                          TYPE_DW_APB_TIMER, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    for (unsigned int i = 0; i < DW_APB_TIMER_CHANNELS; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
        s->context[i].parent = s;
        s->context[i].index = i;
    }
    s->timer_clk = qdev_init_clock_in(DEVICE(s), "timer",
                                      dw_apb_timer_clk_update, s,
                                      ClockUpdate);
}

static void dw_apb_timer_realize(DeviceState *dev, Error **errp)
{
    DWAPBTimerState *s = DW_APB_TIMER(dev);
    const uint8_t policies = PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |
                             PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                             PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                             PTIMER_POLICY_NO_COUNTER_ROUND_DOWN;

    if (!clock_has_source(s->timer_clk)) {
        error_setg(errp, "%s: timer clock must be connected",
                   TYPE_DW_APB_TIMER);
        return;
    }

    for (unsigned int i = 0; i < DW_APB_TIMER_CHANNELS; i++) {
        s->timer[i] = ptimer_init(dw_apb_timer_tick, &s->context[i],
                                  policies);
        ptimer_transaction_begin(s->timer[i]);
        ptimer_set_period_from_clock(s->timer[i], s->timer_clk, 1);
        ptimer_transaction_commit(s->timer[i]);
    }
}

static void dw_apb_timer_finalize(Object *obj)
{
    DWAPBTimerState *s = DW_APB_TIMER(obj);

    for (unsigned int i = 0; i < DW_APB_TIMER_CHANNELS; i++) {
        if (s->timer[i]) {
            ptimer_free(s->timer[i]);
        }
    }
}

static void dw_apb_timer_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Synopsys DesignWare APB timer";
    dc->realize = dw_apb_timer_realize;
    dc->vmsd = &vmstate_dw_apb_timer;
    device_class_set_legacy_reset(dc, dw_apb_timer_reset);
    device_class_set_props(dc, dw_apb_timer_properties);
}

static const TypeInfo dw_apb_timer_info = {
    .name = TYPE_DW_APB_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWAPBTimerState),
    .instance_init = dw_apb_timer_init,
    .instance_finalize = dw_apb_timer_finalize,
    .class_init = dw_apb_timer_class_init,
};

static void dw_apb_timer_register_types(void)
{
    type_register_static(&dw_apb_timer_info);
}

type_init(dw_apb_timer_register_types)
