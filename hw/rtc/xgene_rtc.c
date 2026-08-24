/*
 * Synopsys DesignWare APB RTC as used by APM X-Gene and TH1520
 *
 * The counter, match, load, control and interrupt registers follow the
 * DesignWare APB RTC programming interface exposed by the Linux
 * apm,xgene-rtc driver.  TH1520 additionally implements the optional
 * prescaler: its vendor driver programs a 32.768 kHz input with CPSR=32768.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/rtc/xgene_rtc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/rtc.h"
#include "system/system.h"

static uint32_t xgene_rtc_divisor(XGeneRTCState *s)
{
    if ((s->ccr & XGENE_RTC_CCR_PSCLR_EN) && s->cpsr) {
        return s->cpsr;
    }

    return 1;
}

static uint64_t xgene_rtc_input_cycles(XGeneRTCState *s, int64_t now)
{
    int64_t elapsed = now - s->base_ns;

    if (!(s->ccr & XGENE_RTC_CCR_EN) || elapsed <= 0) {
        return 0;
    }

    return muldiv64(elapsed, clock_get_hz(s->clock),
                    NANOSECONDS_PER_SECOND);
}

static uint32_t xgene_rtc_advance_count(XGeneRTCState *s, uint32_t start,
                                         uint64_t ticks)
{
    uint64_t distance;
    uint64_t period;

    if (!(s->ccr & XGENE_RTC_CCR_WEN)) {
        return start + ticks;
    }

    distance = (uint32_t)(s->cmr - start);
    if (!distance) {
        distance = UINT64_C(1) << 32;
    }
    if (ticks < distance) {
        return start + ticks;
    }

    /* Wrap to zero on the clock which reaches CMR. */
    ticks -= distance;
    period = s->cmr ? s->cmr : 1;
    return ticks % period;
}

static uint64_t xgene_rtc_elapsed_ticks(XGeneRTCState *s, int64_t now)
{
    uint64_t cycles = xgene_rtc_input_cycles(s, now);

    if (cycles > UINT64_MAX - s->prescaler_count) {
        cycles = UINT64_MAX;
    } else {
        cycles += s->prescaler_count;
    }

    return cycles / xgene_rtc_divisor(s);
}

static uint32_t xgene_rtc_get_count(XGeneRTCState *s)
{
    int64_t now = qemu_clock_get_ns(rtc_clock);

    return xgene_rtc_advance_count(s, s->counter_base,
                                   xgene_rtc_elapsed_ticks(s, now));
}

static uint32_t xgene_rtc_get_prescaler(XGeneRTCState *s)
{
    int64_t now = qemu_clock_get_ns(rtc_clock);
    uint64_t cycles = xgene_rtc_input_cycles(s, now);

    if (cycles > UINT64_MAX - s->prescaler_count) {
        cycles = UINT64_MAX;
    } else {
        cycles += s->prescaler_count;
    }

    return cycles % xgene_rtc_divisor(s);
}

static void xgene_rtc_sync(XGeneRTCState *s)
{
    int64_t now = qemu_clock_get_ns(rtc_clock);
    uint64_t cycles = xgene_rtc_input_cycles(s, now);
    uint32_t divisor = xgene_rtc_divisor(s);
    uint64_t total;

    if (cycles > UINT64_MAX - s->prescaler_count) {
        total = UINT64_MAX;
    } else {
        total = cycles + s->prescaler_count;
    }

    s->counter_base = xgene_rtc_advance_count(s, s->counter_base,
                                               total / divisor);
    s->prescaler_count = total % divisor;
    s->base_ns = now;
}

static uint32_t xgene_rtc_stat(XGeneRTCState *s)
{
    return (s->ccr & XGENE_RTC_CCR_MASK) ? 0 : s->rstat;
}

static void xgene_rtc_update_irq(XGeneRTCState *s)
{
    qemu_set_irq(s->irq, !!xgene_rtc_stat(s));
}

static uint64_t xgene_rtc_ticks_to_ns(XGeneRTCState *s, uint64_t ticks)
{
    uint32_t divisor = xgene_rtc_divisor(s);
    uint32_t hz = clock_get_hz(s->clock);
    uint64_t cycles;
    uint64_t ns;

    if (!hz || ticks > UINT64_MAX / divisor) {
        return UINT64_MAX;
    }

    cycles = ticks * divisor;
    if (cycles > s->prescaler_count) {
        cycles -= s->prescaler_count;
    } else {
        cycles = 1;
    }
    ns = muldiv64_round_up(cycles, NANOSECONDS_PER_SECOND, hz);
    return MAX(ns, UINT64_C(1));
}

static int64_t xgene_rtc_deadline(XGeneRTCState *s, uint64_t ticks)
{
    int64_t now = qemu_clock_get_ns(rtc_clock);
    uint64_t delay = xgene_rtc_ticks_to_ns(s, ticks);

    if (delay >= (uint64_t)(INT64_MAX - now)) {
        return INT64_MAX;
    }

    return now + delay;
}

static void xgene_rtc_schedule_alarm(XGeneRTCState *s)
{
    uint32_t count;
    uint64_t ticks;

    timer_del(s->alarm_timer);
    if (!(s->ccr & XGENE_RTC_CCR_EN) || !clock_get_hz(s->clock)) {
        return;
    }

    count = xgene_rtc_get_count(s);
    ticks = (uint32_t)(s->cmr - count);
    if (!ticks) {
        ticks = UINT64_C(1) << 32;
    }
    timer_mod(s->alarm_timer, xgene_rtc_deadline(s, ticks));
}

static void xgene_rtc_schedule_load(XGeneRTCState *s)
{
    timer_del(s->load_timer);
    if (!s->load_pending || !(s->ccr & XGENE_RTC_CCR_EN) ||
        !clock_get_hz(s->clock)) {
        return;
    }

    timer_mod(s->load_timer, xgene_rtc_deadline(s, 1));
}

static void xgene_rtc_alarm(void *opaque)
{
    XGeneRTCState *s = opaque;

    xgene_rtc_sync(s);
    if (s->ccr & XGENE_RTC_CCR_IE) {
        s->rstat = XGENE_RTC_INTERRUPT;
    }
    xgene_rtc_update_irq(s);
    xgene_rtc_schedule_alarm(s);
}

static void xgene_rtc_load(void *opaque)
{
    XGeneRTCState *s = opaque;

    s->counter_base = s->clr;
    s->prescaler_count = 0;
    s->base_ns = qemu_clock_get_ns(rtc_clock);
    s->load_pending = false;

    if (s->counter_base == s->cmr) {
        if (s->ccr & XGENE_RTC_CCR_IE) {
            s->rstat = XGENE_RTC_INTERRUPT;
        }
        if (s->ccr & XGENE_RTC_CCR_WEN) {
            s->counter_base = 0;
        }
    }

    xgene_rtc_update_irq(s);
    xgene_rtc_schedule_alarm(s);
}

static uint64_t xgene_rtc_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    XGeneRTCState *s = opaque;
    uint32_t value;

    switch (offset) {
    case XGENE_RTC_CCVR:
        return xgene_rtc_get_count(s);
    case XGENE_RTC_CMR:
        return s->cmr;
    case XGENE_RTC_CLR:
        return s->clr;
    case XGENE_RTC_CCR:
        return s->ccr;
    case XGENE_RTC_STAT:
        return xgene_rtc_stat(s);
    case XGENE_RTC_RSTAT:
        return s->rstat;
    case XGENE_RTC_EOI:
        value = s->rstat;
        s->rstat = 0;
        xgene_rtc_update_irq(s);
        return value;
    case XGENE_RTC_VER:
        return s->component_version;
    case XGENE_RTC_CPSR:
        return s->cpsr;
    case XGENE_RTC_CPCVR:
        return xgene_rtc_get_prescaler(s);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from reserved register 0x%03"
                      HWADDR_PRIx "\n", TYPE_XGENE_RTC, offset);
        return 0;
    }
}

static void xgene_rtc_write_control(XGeneRTCState *s, uint32_t value)
{
    uint32_t old = s->ccr;

    xgene_rtc_sync(s);
    s->ccr = value & XGENE_RTC_CCR_VALID;
    if ((old ^ s->ccr) & (XGENE_RTC_CCR_EN |
                          XGENE_RTC_CCR_PSCLR_EN)) {
        s->prescaler_count = 0;
    }
    xgene_rtc_update_irq(s);
    xgene_rtc_schedule_load(s);
    xgene_rtc_schedule_alarm(s);
}

static void xgene_rtc_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned int size)
{
    XGeneRTCState *s = opaque;
    uint32_t value32 = value;

    switch (offset) {
    case XGENE_RTC_CMR:
        xgene_rtc_sync(s);
        s->cmr = value32;
        xgene_rtc_schedule_alarm(s);
        break;
    case XGENE_RTC_CLR:
        s->clr = value32;
        s->load_pending = true;
        xgene_rtc_schedule_load(s);
        break;
    case XGENE_RTC_CCR:
        xgene_rtc_write_control(s, value32);
        break;
    case XGENE_RTC_CPSR:
        xgene_rtc_sync(s);
        s->cpsr = value32;
        s->prescaler_count = 0;
        xgene_rtc_schedule_load(s);
        xgene_rtc_schedule_alarm(s);
        break;
    case XGENE_RTC_CCVR:
    case XGENE_RTC_STAT:
    case XGENE_RTC_RSTAT:
    case XGENE_RTC_EOI:
    case XGENE_RTC_VER:
    case XGENE_RTC_CPCVR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register 0x%03"
                      HWADDR_PRIx "\n", TYPE_XGENE_RTC, offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to reserved register 0x%03"
                      HWADDR_PRIx "\n", TYPE_XGENE_RTC, offset);
        break;
    }
}

static bool xgene_rtc_access_valid(void *opaque, hwaddr offset,
                                    unsigned int size, bool is_write,
                                    MemTxAttrs attrs)
{
    return size == 4 && !(offset & 3);
}

static const MemoryRegionOps xgene_rtc_ops = {
    .read = xgene_rtc_read,
    .write = xgene_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = xgene_rtc_access_valid,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xgene_rtc_clock_update(void *opaque, ClockEvent event)
{
    XGeneRTCState *s = opaque;

    if (!s->alarm_timer) {
        return;
    }

    if (event == ClockPreUpdate) {
        xgene_rtc_sync(s);
        return;
    }

    s->base_ns = qemu_clock_get_ns(rtc_clock);
    xgene_rtc_schedule_load(s);
    xgene_rtc_schedule_alarm(s);
}

static void xgene_rtc_reset(DeviceState *dev)
{
    XGeneRTCState *s = XGENE_RTC(dev);

    if (s->alarm_timer) {
        xgene_rtc_sync(s);
        timer_del(s->alarm_timer);
        timer_del(s->load_timer);
    }

    /* The battery-backed count is retained across a system reset. */
    s->cmr = 0;
    s->clr = 0;
    s->ccr = 0;
    s->rstat = 0;
    s->cpsr = s->prescaler_reset;
    s->prescaler_count = 0;
    s->load_pending = false;
    s->base_ns = qemu_clock_get_ns(rtc_clock);
    xgene_rtc_update_irq(s);
}

static int xgene_rtc_pre_save(void *opaque)
{
    XGeneRTCState *s = opaque;
    int64_t delta = 0;

    if (rtc_clock == QEMU_CLOCK_REALTIME) {
        delta = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) -
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }

    s->base_ns_vmstate = s->base_ns - delta;
    s->alarm_deadline_vmstate = timer_pending(s->alarm_timer) ?
        (int64_t)timer_expire_time_ns(s->alarm_timer) - delta : -1;
    s->load_deadline_vmstate = timer_pending(s->load_timer) ?
        (int64_t)timer_expire_time_ns(s->load_timer) - delta : -1;
    return 0;
}

static int xgene_rtc_post_load(void *opaque, int version_id)
{
    XGeneRTCState *s = opaque;
    int64_t delta = 0;
    int64_t alarm_deadline;
    int64_t load_deadline;
    bool load_applied = false;

    if ((s->ccr & ~XGENE_RTC_CCR_VALID) ||
        (s->rstat & ~XGENE_RTC_INTERRUPT) ||
        s->prescaler_count >= xgene_rtc_divisor(s)) {
        return -EINVAL;
    }

    if (rtc_clock == QEMU_CLOCK_REALTIME) {
        delta = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) -
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
    s->base_ns = s->base_ns_vmstate + delta;
    alarm_deadline = s->alarm_deadline_vmstate < 0 ? -1 :
                     s->alarm_deadline_vmstate + delta;
    load_deadline = s->load_deadline_vmstate < 0 ? -1 :
                    s->load_deadline_vmstate + delta;

    timer_del(s->alarm_timer);
    timer_del(s->load_timer);
    if (s->load_pending && load_deadline >= 0) {
        if (load_deadline <= qemu_clock_get_ns(rtc_clock)) {
            xgene_rtc_load(s);
            load_applied = true;
        } else {
            timer_mod(s->load_timer, load_deadline);
        }
    }
    xgene_rtc_update_irq(s);
    if (!load_applied && alarm_deadline >= 0 &&
        (s->ccr & XGENE_RTC_CCR_EN) && clock_get_hz(s->clock)) {
        if (alarm_deadline <= qemu_clock_get_ns(rtc_clock)) {
            xgene_rtc_alarm(s);
        } else {
            timer_mod(s->alarm_timer, alarm_deadline);
        }
    } else if (!load_applied) {
        xgene_rtc_schedule_alarm(s);
    }
    return 0;
}

static const VMStateDescription vmstate_xgene_rtc = {
    .name = TYPE_XGENE_RTC,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = xgene_rtc_pre_save,
    .post_load = xgene_rtc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(clock, XGeneRTCState),
        VMSTATE_UINT32(counter_base, XGeneRTCState),
        VMSTATE_UINT32(prescaler_count, XGeneRTCState),
        VMSTATE_UINT32(cmr, XGeneRTCState),
        VMSTATE_UINT32(clr, XGeneRTCState),
        VMSTATE_UINT32(ccr, XGeneRTCState),
        VMSTATE_UINT32(rstat, XGeneRTCState),
        VMSTATE_UINT32(cpsr, XGeneRTCState),
        VMSTATE_BOOL(load_pending, XGeneRTCState),
        VMSTATE_INT64(base_ns_vmstate, XGeneRTCState),
        VMSTATE_INT64(alarm_deadline_vmstate, XGeneRTCState),
        VMSTATE_INT64(load_deadline_vmstate, XGeneRTCState),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property xgene_rtc_properties[] = {
    DEFINE_PROP_UINT32("component-version", XGeneRTCState,
                       component_version, 0),
    DEFINE_PROP_UINT32("prescaler-reset", XGeneRTCState,
                       prescaler_reset, 0),
};

static void xgene_rtc_realize(DeviceState *dev, Error **errp)
{
    XGeneRTCState *s = XGENE_RTC(dev);
    struct tm tm;

    if (!clock_has_source(s->clock)) {
        error_setg(errp, "%s: clock input must be connected",
                   TYPE_XGENE_RTC);
        return;
    }
    if (!clock_get_hz(s->clock)) {
        error_setg(errp, "%s: clock input frequency must be non-zero",
                   TYPE_XGENE_RTC);
        return;
    }

    s->alarm_timer = timer_new_ns(rtc_clock, xgene_rtc_alarm, s);
    s->load_timer = timer_new_ns(rtc_clock, xgene_rtc_load, s);
    qemu_get_timedate(&tm, 0);
    s->counter_base = mktimegm(&tm);
    s->base_ns = qemu_clock_get_ns(rtc_clock);
}

static void xgene_rtc_init(Object *obj)
{
    XGeneRTCState *s = XGENE_RTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &xgene_rtc_ops, s,
                          TYPE_XGENE_RTC, XGENE_RTC_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->clock = qdev_init_clock_in(DEVICE(obj), "rtc",
                                  xgene_rtc_clock_update, s,
                                  ClockPreUpdate | ClockUpdate);
}

static void xgene_rtc_finalize(Object *obj)
{
    XGeneRTCState *s = XGENE_RTC(obj);

    if (s->alarm_timer) {
        timer_free(s->alarm_timer);
        timer_free(s->load_timer);
    }
}

static void xgene_rtc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xgene_rtc_realize;
    device_class_set_legacy_reset(dc, xgene_rtc_reset);
    device_class_set_props(dc, xgene_rtc_properties);
    dc->vmsd = &vmstate_xgene_rtc;
}

static const TypeInfo xgene_rtc_info = {
    .name = TYPE_XGENE_RTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XGeneRTCState),
    .instance_init = xgene_rtc_init,
    .instance_finalize = xgene_rtc_finalize,
    .class_init = xgene_rtc_class_init,
};

static void xgene_rtc_register_types(void)
{
    type_register_static(&xgene_rtc_info);
}

type_init(xgene_rtc_register_types)
