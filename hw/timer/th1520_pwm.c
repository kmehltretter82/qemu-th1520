/*
 * T-Head TH1520 PWM controller
 *
 * Linux documents six PWM channels with a 0x20-byte stride.  It programs
 * period and falling-point values into a shadow configuration, then uses the
 * CFG_UPDATE strobe to make that configuration take effect at the start of a
 * later period.  START is likewise a strobe; the Linux driver issues it in a
 * separate transaction when it enables a previously inactive channel.
 *
 * The currently upstream Linux driver only uses continuous mode.  The
 * one-shot mode, inactive-output control, clock-rate changes, and physical
 * pin routing need hardware validation and are intentionally not inferred
 * here.  The TH1520 board model wires the documented AP reset pair to an
 * immediate model reset, but its hardware pulse/hold semantics remain
 * unproved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/timer/th1520_pwm.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"

#define TH1520_PWM_CHANNEL_STRIDE       0x20
#define TH1520_PWM_CTRL                 0x00
#define TH1520_PWM_PERIOD               0x08
#define TH1520_PWM_FP                   0x0c
#define TH1520_PWM_REG_SIZE             0x0b0

#define TH1520_PWM_CTRL_START           BIT(0)
#define TH1520_PWM_CTRL_CFG_UPDATE      BIT(2)
#define TH1520_PWM_CTRL_CONTINUOUS      BIT(5)
#define TH1520_PWM_CTRL_FPOUT           BIT(8)
#define TH1520_PWM_CTRL_STROBES         (TH1520_PWM_CTRL_START | \
                                         TH1520_PWM_CTRL_CFG_UPDATE)

static void th1520_pwm_set_output(TH1520PWMChannel *channel, bool level)
{
    TH1520PWMState *s = channel->parent;

    channel->output_level = level;
    qemu_set_irq(s->output[channel->index], level);
}

static uint64_t th1520_pwm_cycles_to_ns(TH1520PWMState *s,
                                         uint32_t cycles)
{
    uint64_t hz = clock_get_hz(s->pwm_clk);

    return ((uint64_t)cycles * NANOSECONDS_PER_SECOND + hz - 1) / hz;
}

static bool th1520_pwm_initial_level(const TH1520PWMChannel *channel)
{
    if (!(channel->active_ctrl & TH1520_PWM_CTRL_CONTINUOUS) ||
        !channel->active_period) {
        return false;
    }

    if (channel->active_ctrl & TH1520_PWM_CTRL_FPOUT) {
        return channel->active_fp != 0;
    }
    return channel->active_fp == 0;
}

static void th1520_pwm_schedule(TH1520PWMChannel *channel,
                                 uint32_t cycles, bool edge_is_boundary)
{
    TH1520PWMState *s = channel->parent;
    uint64_t delay = th1520_pwm_cycles_to_ns(s, cycles);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    /* A non-zero cycle count and a realized non-zero clock yield this too. */
    if (!delay) {
        delay = 1;
    }
    channel->edge_is_boundary = edge_is_boundary;
    timer_mod_ns(&channel->timer, now + delay);
}

static void th1520_pwm_latch(TH1520PWMChannel *channel)
{
    channel->active_ctrl = channel->ctrl;
    channel->active_period = channel->period;
    channel->active_fp = channel->fp;
    channel->update_pending = false;
}

static void th1520_pwm_begin_period(TH1520PWMChannel *channel)
{
    uint32_t edge;

    if (channel->update_pending) {
        th1520_pwm_latch(channel);
    }
    if (!(channel->active_ctrl & TH1520_PWM_CTRL_CONTINUOUS)) {
        if (channel->active_ctrl) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: one-shot PWM mode is not modeled\n",
                          TYPE_TH1520_PWM);
        }
        channel->running = false;
        timer_del(&channel->timer);
        th1520_pwm_set_output(channel, false);
        return;
    }
    if (!channel->active_period) {
        channel->running = false;
        timer_del(&channel->timer);
        th1520_pwm_set_output(channel, false);
        return;
    }

    th1520_pwm_set_output(channel, th1520_pwm_initial_level(channel));
    if (channel->active_fp > 0 &&
        channel->active_fp < channel->active_period) {
        edge = channel->active_fp;
        th1520_pwm_schedule(channel, edge, false);
    } else {
        th1520_pwm_schedule(channel, channel->active_period, true);
    }
}

static void th1520_pwm_timer(void *opaque)
{
    TH1520PWMChannel *channel = opaque;

    if (!channel->running) {
        return;
    }

    if (channel->edge_is_boundary) {
        th1520_pwm_begin_period(channel);
        return;
    }

    th1520_pwm_set_output(channel, !th1520_pwm_initial_level(channel));
    th1520_pwm_schedule(channel,
                         channel->active_period - channel->active_fp, true);
}

static void th1520_pwm_write_ctrl(TH1520PWMChannel *channel, uint32_t value)
{
    channel->ctrl = value & ~TH1520_PWM_CTRL_STROBES;
    if (value & TH1520_PWM_CTRL_CFG_UPDATE) {
        channel->update_pending = true;
    }
    if ((value & TH1520_PWM_CTRL_START) && !channel->running) {
        /* START begins with the configuration most recently written by SW. */
        th1520_pwm_latch(channel);
        channel->running = true;
        th1520_pwm_begin_period(channel);
    }
}

static uint64_t th1520_pwm_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    TH1520PWMState *s = opaque;
    unsigned int index = offset / TH1520_PWM_CHANNEL_STRIDE;
    TH1520PWMChannel *channel = &s->channel[index];

    switch (offset % TH1520_PWM_CHANNEL_STRIDE) {
    case TH1520_PWM_CTRL:
        return channel->ctrl;
    case TH1520_PWM_PERIOD:
        return channel->period;
    case TH1520_PWM_FP:
        return channel->fp;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from reserved register 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_PWM, offset);
        return 0;
    }
}

static void th1520_pwm_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned int size)
{
    TH1520PWMState *s = opaque;
    unsigned int index = offset / TH1520_PWM_CHANNEL_STRIDE;
    TH1520PWMChannel *channel = &s->channel[index];

    switch (offset % TH1520_PWM_CHANNEL_STRIDE) {
    case TH1520_PWM_CTRL:
        th1520_pwm_write_ctrl(channel, value);
        break;
    case TH1520_PWM_PERIOD:
        channel->period = value;
        break;
    case TH1520_PWM_FP:
        channel->fp = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to reserved register 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_PWM, offset);
        break;
    }
}

static bool th1520_pwm_access_valid(void *opaque, hwaddr offset,
                                     unsigned int size, bool is_write,
                                     MemTxAttrs attrs)
{
    return size == 4 && !(offset & 3);
}

static const MemoryRegionOps th1520_pwm_ops = {
    .read = th1520_pwm_read,
    .write = th1520_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = th1520_pwm_access_valid,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void th1520_pwm_reset(DeviceState *dev)
{
    TH1520PWMState *s = TH1520_PWM(dev);

    for (unsigned int i = 0; i < TH1520_PWM_CHANNELS; i++) {
        TH1520PWMChannel *channel = &s->channel[i];

        timer_del(&channel->timer);
        channel->ctrl = 0;
        channel->period = 0;
        channel->fp = 0;
        channel->active_ctrl = 0;
        channel->active_period = 0;
        channel->active_fp = 0;
        channel->running = false;
        channel->update_pending = false;
        channel->edge_is_boundary = false;
        th1520_pwm_set_output(channel, false);
    }
}

static void th1520_pwm_reset_input(void *opaque, int n, int level)
{
    if (level) {
        th1520_pwm_reset(DEVICE(opaque));
    }
}

static int th1520_pwm_post_load(void *opaque, int version_id)
{
    TH1520PWMState *s = opaque;

    if (!clock_get_hz(s->pwm_clk)) {
        return -EINVAL;
    }
    for (unsigned int i = 0; i < TH1520_PWM_CHANNELS; i++) {
        TH1520PWMChannel *channel = &s->channel[i];

        if (!channel->running) {
            timer_del(&channel->timer);
            th1520_pwm_set_output(channel, false);
            continue;
        }
        if (!(channel->active_ctrl & TH1520_PWM_CTRL_CONTINUOUS) ||
            !channel->active_period) {
            return -EINVAL;
        }
        th1520_pwm_set_output(channel, channel->output_level);
    }
    return 0;
}

static const VMStateDescription vmstate_th1520_pwm_channel = {
    .name = TYPE_TH1520_PWM "/channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER(timer, TH1520PWMChannel),
        VMSTATE_UINT32(ctrl, TH1520PWMChannel),
        VMSTATE_UINT32(period, TH1520PWMChannel),
        VMSTATE_UINT32(fp, TH1520PWMChannel),
        VMSTATE_UINT32(active_ctrl, TH1520PWMChannel),
        VMSTATE_UINT32(active_period, TH1520PWMChannel),
        VMSTATE_UINT32(active_fp, TH1520PWMChannel),
        VMSTATE_BOOL(running, TH1520PWMChannel),
        VMSTATE_BOOL(update_pending, TH1520PWMChannel),
        VMSTATE_BOOL(edge_is_boundary, TH1520PWMChannel),
        VMSTATE_BOOL(output_level, TH1520PWMChannel),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription vmstate_th1520_pwm = {
    .name = TYPE_TH1520_PWM,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = th1520_pwm_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pwm_clk, TH1520PWMState),
        VMSTATE_STRUCT_ARRAY(channel, TH1520PWMState, TH1520_PWM_CHANNELS,
                             0, vmstate_th1520_pwm_channel,
                             TH1520PWMChannel),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_pwm_realize(DeviceState *dev, Error **errp)
{
    TH1520PWMState *s = TH1520_PWM(dev);
    uint64_t hz;

    if (!clock_has_source(s->pwm_clk)) {
        error_setg(errp, "%s: PWM clock must be connected", TYPE_TH1520_PWM);
        return;
    }
    hz = clock_get_hz(s->pwm_clk);
    if (!hz || hz > NANOSECONDS_PER_SECOND) {
        error_setg(errp, "%s: PWM clock must be between 1 Hz and 1 GHz",
                   TYPE_TH1520_PWM);
        return;
    }
}

static void th1520_pwm_init(Object *obj)
{
    TH1520PWMState *s = TH1520_PWM(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_pwm_ops, s,
                          TYPE_TH1520_PWM, TH1520_PWM_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    s->pwm_clk = qdev_init_clock_in(DEVICE(s), "pwm", NULL, NULL, 0);
    qdev_init_gpio_in_named(DEVICE(s), th1520_pwm_reset_input, "reset", 1);
    qdev_init_gpio_out_named(DEVICE(s), s->output, "pwm",
                             TH1520_PWM_CHANNELS);
    for (unsigned int i = 0; i < TH1520_PWM_CHANNELS; i++) {
        TH1520PWMChannel *channel = &s->channel[i];

        channel->parent = s;
        channel->index = i;
        timer_init_ns(&channel->timer, QEMU_CLOCK_VIRTUAL,
                      th1520_pwm_timer, channel);
    }
}

static void th1520_pwm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 PWM controller";
    dc->realize = th1520_pwm_realize;
    dc->vmsd = &vmstate_th1520_pwm;
    device_class_set_legacy_reset(dc, th1520_pwm_reset);
}

static const TypeInfo th1520_pwm_info = {
    .name = TYPE_TH1520_PWM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520PWMState),
    .instance_init = th1520_pwm_init,
    .class_init = th1520_pwm_class_init,
};

static void th1520_pwm_register_types(void)
{
    type_register_static(&th1520_pwm_info);
}

type_init(th1520_pwm_register_types)
