/*
 * T-Head TH1520 application-domain clock and reset controllers
 *
 * The two blocks have separate physical apertures and device-tree bindings.
 * This model implements the REE register banks visible through those
 * apertures.  Clock programming changes the guest-visible clock tree exposed
 * by Linux; it deliberately does not scale TCG execution throughput.
 *
 * PLL lock is deterministic virtual time.  The public system manual quotes a
 * maximum lock time of about 21.25 us for the default configurations.  The
 * exact distribution, calibration state machine, invalid configurations and
 * voltage/frequency coupling still require physical differential tests.
 *
 * Leaf gate bits for currently modeled AP peripherals are exported as
 * active-high GPIO levels.  The PWM, timer and watchdog leaves additionally
 * drive QEMU Clock outputs at their established 125 MHz rate, so those timed
 * engines can pause while gated.  Effects on untimed engines and bus accesses
 * remain hardware-validation work rather than being inferred here.
 *
 * Reset registers preserve the silicon defaults and active-low programming
 * convention.  Mainline-described resets for currently modeled AP children
 * drive QEMU reset outputs.  An asserted output immediately resets its
 * consumer's modeled state; this is intentionally not a claim about silicon
 * pulse width, bus behavior while reset is held, retention, or the many
 * remaining reset domains.  In particular, the silicon default releases only
 * C910 core 0, whereas the current direct-boot machine deliberately starts
 * all four harts.  That boot/reset discrepancy is tracked in the
 * hardware-validation ledger.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/misc/th1520_cpr.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TH1520_PLL_STS 0x080
#define TH1520_PLL_VCO_RST BIT(29)
#define TH1520_PLL_LOCK_TIME_NS 21250

#define TH1520_PLL_CFG0_MASK 0x077fff3f
#define TH1520_PLL_CFG1_MASK 0x7bffffff
#define TH1520_PLL_CFG2_MASK 0x3ffff000
#define TH1520_PLL_CFG3_MASK 0xfffff700
#define TH1520_AP_TIMED_GATE_HZ 125000000
/* stmmaceth: the GMAC AXI/DMA clock the pinned Linux RIWT conversion assumes. */
#define TH1520_AP_GMAC_AXI_HZ 500000000

typedef struct TH1520RegInfo {
    uint16_t offset;
    uint32_t reset;
    uint32_t write_mask;
} TH1520RegInfo;

#define PLL_CFG0(base, value) \
    { (base) + 0x0, (value), TH1520_PLL_CFG0_MASK }
#define PLL_CFG1(base, value) \
    { (base) + 0x4, (value), TH1520_PLL_CFG1_MASK }
#define PLL_CFG2(base, value) \
    { (base) + 0x8, (value), TH1520_PLL_CFG2_MASK }
#define PLL_CFG3(base, value) \
    { (base) + 0xc, (value), TH1520_PLL_CFG3_MASK }

static const TH1520RegInfo th1520_ap_clock_reginfo[] = {
    PLL_CFG0(0x000, 0x02507d01),
    PLL_CFG1(0x000, 0x03000000),
    PLL_CFG2(0x000, 0x02000000),
    PLL_CFG3(0x000, 0x07fff400),
    PLL_CFG0(0x010, 0x02507d01),
    PLL_CFG1(0x010, 0x03000000),
    PLL_CFG2(0x010, 0x02000000),
    PLL_CFG3(0x010, 0x07fff400),
    PLL_CFG0(0x020, 0x01307d01),
    PLL_CFG1(0x020, 0x03000000),
    PLL_CFG2(0x020, 0x02000000),
    PLL_CFG3(0x020, 0x07fff400),
    PLL_CFG0(0x030, 0x01306301),
    PLL_CFG1(0x030, 0x03000000),
    PLL_CFG2(0x030, 0x02000000),
    PLL_CFG3(0x030, 0x07fff400),
    PLL_CFG0(0x040, 0x01206301),
    PLL_CFG1(0x040, 0x03000000),
    PLL_CFG2(0x040, 0x02000000),
    PLL_CFG3(0x040, 0x07fff400),
    PLL_CFG0(0x050, 0x01206301),
    PLL_CFG1(0x050, 0x03000000),
    PLL_CFG2(0x050, 0x02000000),
    PLL_CFG3(0x050, 0x07fff400),
    PLL_CFG0(0x060, 0x01306301),
    PLL_CFG1(0x060, 0x63000000),
    PLL_CFG2(0x060, 0x02000000),
    PLL_CFG3(0x060, 0x07fff500),
    { TH1520_PLL_STS, 0x00000000, 0x00000000 },
    { 0x100, 0x000009f0, 0x00000ff3 },
    { 0x104, 0x30303030, 0x3f3f3f3f },
    { 0x120, 0x000000d4, 0x00000037 },
    { 0x130, 0x00000018, 0x0000000f },
    { 0x134, 0x000001b2, 0x00000197 },
    { 0x138, 0x00000112, 0x0000013f },
    { 0x140, 0x00000258, 0x0000027f },
    { 0x150, 0x00001f28, 0x00001f7f },
    { 0x1b4, 0x0000002a, 0x0000003f },
    { 0x1b8, 0x0000002a, 0x0000003f },
    { 0x1bc, 0x0000002a, 0x0000003f },
    { 0x1c0, 0x0000002a, 0x0000003f },
    { 0x1c4, 0x00000034, 0x000000bf },
    { 0x1c8, 0x0000000b, 0x0000007f },
    { 0x1d0, 0x00330016, 0x003f003f },
    { 0x1d8, 0x00000016, 0x0000003f },
    { 0x1dc, 0x00000033, 0x0000003f },
    { 0x1e0, 0x0000b312, 0x0000ffff },
    { 0x1e4, 0x00000032, 0x0000003f },
    { 0x1e8, 0x00000102, 0x000001ff },
    { 0x1ec, 0x00000102, 0x000001ff },
    { 0x1f0, 0x00000002, 0x00000003 },
    { 0x204, 0x55ffffff, 0x55ffffff },
    { 0x208, 0x000007ff, 0x000007ff },
    { 0x20c, 0x0000001e, 0x0000001e },
    { 0x210, 0x00000000, 0x00000001 },
    { 0x220, 0x00000000, 0x00000007 },
};

static const uint32_t th1520_pll_lock_mask[TH1520_AP_PLL_COUNT] = {
    BIT(1), BIT(4), BIT(3), BIT(7), BIT(8), BIT(9), BIT(10),
};

typedef struct TH1520ClockGateInfo {
    uint16_t offset;
    uint32_t mask;
} TH1520ClockGateInfo;

/* Leaf gates and bit positions are from clk-th1520-ap.c. */
static const TH1520ClockGateInfo
th1520_ap_clock_gate_info[TH1520_AP_CLOCK_GATE_COUNT] = {
    [TH1520_AP_CLOCK_GATE_EMMC_SDIO] = { 0x204, BIT(30) },
    [TH1520_AP_CLOCK_GATE_GMAC1] = { 0x204, BIT(26) },
    [TH1520_AP_CLOCK_GATE_PADCTRL1] = { 0x204, BIT(24) },
    [TH1520_AP_CLOCK_GATE_PADCTRL0] = { 0x204, BIT(22) },
    [TH1520_AP_CLOCK_GATE_GMAC_AXI] = { 0x204, BIT(21) },
    [TH1520_AP_CLOCK_GATE_GPIO3] = { 0x204, BIT(20) },
    [TH1520_AP_CLOCK_GATE_GMAC0] = { 0x204, BIT(19) },
    [TH1520_AP_CLOCK_GATE_PWM] = { 0x204, BIT(18) },
    [TH1520_AP_CLOCK_GATE_SPI] = { 0x204, BIT(15) },
    [TH1520_AP_CLOCK_GATE_UART0] = { 0x204, BIT(14) },
    [TH1520_AP_CLOCK_GATE_UART1] = { 0x204, BIT(13) },
    [TH1520_AP_CLOCK_GATE_UART2] = { 0x204, BIT(12) },
    [TH1520_AP_CLOCK_GATE_UART3] = { 0x204, BIT(11) },
    [TH1520_AP_CLOCK_GATE_UART4] = { 0x204, BIT(10) },
    [TH1520_AP_CLOCK_GATE_UART5] = { 0x204, BIT(9) },
    [TH1520_AP_CLOCK_GATE_GPIO0] = { 0x204, BIT(8) },
    [TH1520_AP_CLOCK_GATE_GPIO1] = { 0x204, BIT(7) },
    [TH1520_AP_CLOCK_GATE_GPIO2] = { 0x204, BIT(6) },
    [TH1520_AP_CLOCK_GATE_I2C0] = { 0x204, BIT(5) },
    [TH1520_AP_CLOCK_GATE_I2C1] = { 0x204, BIT(4) },
    [TH1520_AP_CLOCK_GATE_I2C2] = { 0x204, BIT(3) },
    [TH1520_AP_CLOCK_GATE_I2C3] = { 0x204, BIT(2) },
    [TH1520_AP_CLOCK_GATE_I2C4] = { 0x204, BIT(1) },
    [TH1520_AP_CLOCK_GATE_I2C5] = { 0x204, BIT(0) },
    [TH1520_AP_CLOCK_GATE_DMA] = { 0x208, BIT(8) },
    [TH1520_AP_CLOCK_GATE_MBOX0] = { 0x208, BIT(7) },
    [TH1520_AP_CLOCK_GATE_MBOX1] = { 0x208, BIT(6) },
    [TH1520_AP_CLOCK_GATE_MBOX2] = { 0x208, BIT(5) },
    [TH1520_AP_CLOCK_GATE_MBOX3] = { 0x208, BIT(4) },
    [TH1520_AP_CLOCK_GATE_WDT0] = { 0x208, BIT(3) },
    [TH1520_AP_CLOCK_GATE_WDT1] = { 0x208, BIT(2) },
    [TH1520_AP_CLOCK_GATE_TIMER0] = { 0x208, BIT(1) },
    [TH1520_AP_CLOCK_GATE_TIMER1] = { 0x208, BIT(0) },
};

static const TH1520RegInfo *th1520_reginfo_find(const TH1520RegInfo *info,
                                                size_t count,
                                                hwaddr offset)
{
    for (size_t i = 0; i < count; i++) {
        if (info[i].offset == offset) {
            return &info[i];
        }
    }

    return NULL;
}

static Clock *th1520_ap_clock_timed_output(TH1520APClockState *s,
                                           unsigned int output)
{
    if (output == TH1520_AP_CLOCK_GATE_PWM) {
        return s->pwm_clock;
    }
    if (output >= TH1520_AP_CLOCK_GATE_WDT0 &&
        output <= TH1520_AP_CLOCK_GATE_WDT1) {
        return s->wdt_clock[output - TH1520_AP_CLOCK_GATE_WDT0];
    }
    if (output >= TH1520_AP_CLOCK_GATE_TIMER0 &&
        output <= TH1520_AP_CLOCK_GATE_TIMER1) {
        return s->timer_clock[output - TH1520_AP_CLOCK_GATE_TIMER0];
    }
    if (output == TH1520_AP_CLOCK_GATE_GMAC_AXI) {
        return s->gmac_axi_clock;
    }
    return NULL;
}

static unsigned th1520_ap_clock_output_hz(unsigned int output)
{
    return output == TH1520_AP_CLOCK_GATE_GMAC_AXI ? TH1520_AP_GMAC_AXI_HZ :
                                                     TH1520_AP_TIMED_GATE_HZ;
}

static void th1520_ap_clock_update_gate(TH1520APClockState *s,
                                        unsigned int output, bool force,
                                        bool update_clock)
{
    const TH1520ClockGateInfo *info = &th1520_ap_clock_gate_info[output];
    bool enabled = s->regs[info->offset / 4] & info->mask;
    Clock *clock = th1520_ap_clock_timed_output(s, output);

    if (force || s->clock_enabled[output] != enabled) {
        s->clock_enabled[output] = enabled;
        qemu_set_irq(s->peripheral_clock_enable[output], enabled);
    }
    if (clock && update_clock) {
        clock_update_hz(clock,
                        enabled ? th1520_ap_clock_output_hz(output) : 0);
    }
}

static void th1520_ap_clock_update_gates(TH1520APClockState *s,
                                         hwaddr offset, bool force,
                                         bool update_clocks)
{
    for (unsigned int i = 0; i < TH1520_AP_CLOCK_GATE_COUNT; i++) {
        if (force || th1520_ap_clock_gate_info[i].offset == offset) {
            th1520_ap_clock_update_gate(s, i, force, update_clocks);
        }
    }
}

static void th1520_ap_clock_schedule_next(TH1520APClockState *s)
{
    int64_t next = INT64_MAX;

    timer_del(&s->pll_lock_timer);
    for (unsigned int i = 0; i < TH1520_AP_PLL_COUNT; i++) {
        if (s->pll_pending & BIT(i)) {
            next = MIN(next, s->pll_deadline[i]);
        }
    }

    if (next != INT64_MAX) {
        timer_mod_ns(&s->pll_lock_timer, next);
    }
}

static void th1520_ap_clock_cancel_pll(TH1520APClockState *s,
                                       unsigned int pll)
{
    s->pll_pending &= ~BIT(pll);
    s->regs[TH1520_PLL_STS / 4] &= ~th1520_pll_lock_mask[pll];
    th1520_ap_clock_schedule_next(s);
}

static void th1520_ap_clock_restart_pll(TH1520APClockState *s,
                                        unsigned int pll)
{
    s->regs[TH1520_PLL_STS / 4] &= ~th1520_pll_lock_mask[pll];
    s->pll_pending |= BIT(pll);
    s->pll_deadline[pll] = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                           TH1520_PLL_LOCK_TIME_NS;
    th1520_ap_clock_schedule_next(s);
}

static void th1520_ap_clock_lock(void *opaque)
{
    TH1520APClockState *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    for (unsigned int i = 0; i < TH1520_AP_PLL_COUNT; i++) {
        if ((s->pll_pending & BIT(i)) && s->pll_deadline[i] <= now) {
            s->pll_pending &= ~BIT(i);
            s->regs[TH1520_PLL_STS / 4] |= th1520_pll_lock_mask[i];
        }
    }

    th1520_ap_clock_schedule_next(s);
}

static uint64_t th1520_ap_clock_read(void *opaque, hwaddr offset,
                                     unsigned int size)
{
    TH1520APClockState *s = opaque;
    const TH1520RegInfo *info =
        th1520_reginfo_find(th1520_ap_clock_reginfo,
                            ARRAY_SIZE(th1520_ap_clock_reginfo), offset);

    if (!info) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_AP_CLOCK, offset);
        return 0;
    }

    /*
     * A polling guest can advance QEMU_CLOCK_VIRTUAL past the lock deadline
     * before the I/O thread dispatches the timer callback.  Materialize every
     * expired deadline here as well, or a due lock can remain invisible until
     * after the guest's polling timeout.
     */
    if (offset == TH1520_PLL_STS && timer_pending(&s->pll_lock_timer) &&
        timer_expired(&s->pll_lock_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL))) {
        th1520_ap_clock_lock(s);
    }

    return s->regs[offset / 4];
}

static void th1520_ap_clock_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned int size)
{
    TH1520APClockState *s = opaque;
    const TH1520RegInfo *info =
        th1520_reginfo_find(th1520_ap_clock_reginfo,
                            ARRAY_SIZE(th1520_ap_clock_reginfo), offset);
    uint32_t old;
    uint32_t next;
    unsigned int pll;
    unsigned int pll_reg;

    if (!info) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_AP_CLOCK, offset);
        return;
    }

    if (!info->write_mask) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register at 0x%03"
                      HWADDR_PRIx "\n", TYPE_TH1520_AP_CLOCK, offset);
        return;
    }

    old = s->regs[offset / 4];
    next = (old & ~info->write_mask) | ((uint32_t)value & info->write_mask);

    /* PLL calibration pulse is write-one/self-clearing on the hardware. */
    if (offset <= 0x06c && (offset & 0xf) == 0xc) {
        next &= ~BIT(9);
    }

    s->regs[offset / 4] = next;
    if (old != next) {
        th1520_ap_clock_update_gates(s, offset, false, true);
    }
    if (old == next || offset > 0x064) {
        return;
    }

    pll = offset / 0x10;
    pll_reg = offset & 0xf;
    if (pll_reg != 0 && pll_reg != 4) {
        return;
    }

    if (s->regs[(pll * 0x10 + 4) / 4] & TH1520_PLL_VCO_RST) {
        th1520_ap_clock_cancel_pll(s, pll);
    } else {
        th1520_ap_clock_restart_pll(s, pll);
    }
}

static const MemoryRegionOps th1520_ap_clock_ops = {
    .read = th1520_ap_clock_read,
    .write = th1520_ap_clock_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void th1520_ap_clock_reset(DeviceState *dev)
{
    TH1520APClockState *s = TH1520_AP_CLOCK(dev);

    timer_del(&s->pll_lock_timer);
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->pll_deadline, 0, sizeof(s->pll_deadline));
    memset(s->pll_remaining, 0, sizeof(s->pll_remaining));
    s->pll_pending = 0;

    for (size_t i = 0; i < ARRAY_SIZE(th1520_ap_clock_reginfo); i++) {
        const TH1520RegInfo *info = &th1520_ap_clock_reginfo[i];

        s->regs[info->offset / 4] = info->reset;
    }

    for (unsigned int pll = 0; pll < TH1520_AP_PLL_COUNT; pll++) {
        if (!(s->regs[(pll * 0x10 + 4) / 4] & TH1520_PLL_VCO_RST)) {
            th1520_ap_clock_restart_pll(s, pll);
        }
    }
    th1520_ap_clock_update_gates(s, 0, true, true);
}

static int th1520_ap_clock_pre_save(void *opaque)
{
    TH1520APClockState *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    for (unsigned int i = 0; i < TH1520_AP_PLL_COUNT; i++) {
        s->pll_remaining[i] = s->pll_pending & BIT(i) ?
                              MAX(s->pll_deadline[i] - now, 0) : 0;
    }

    return 0;
}

static int th1520_ap_clock_post_load(void *opaque, int version_id)
{
    TH1520APClockState *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    for (unsigned int i = 0; i < TH1520_AP_PLL_COUNT; i++) {
        if (s->pll_pending & BIT(i)) {
            s->pll_deadline[i] = now + s->pll_remaining[i];
        }
    }

    th1520_ap_clock_schedule_next(s);
    /* Raw levels and output periods are derived entirely from the registers. */
    th1520_ap_clock_update_gates(s, 0, true, true);
    return 0;
}

static const VMStateDescription vmstate_th1520_ap_clock = {
    .name = TYPE_TH1520_AP_CLOCK,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = th1520_ap_clock_pre_save,
    .post_load = th1520_ap_clock_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, TH1520APClockState,
                             TH1520_AP_CLOCK_REGS),
        VMSTATE_UINT32(pll_pending, TH1520APClockState),
        VMSTATE_INT64_ARRAY(pll_remaining, TH1520APClockState,
                            TH1520_AP_PLL_COUNT),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_ap_clock_init(Object *obj)
{
    TH1520APClockState *s = TH1520_AP_CLOCK(obj);

    timer_init_ns(&s->pll_lock_timer, QEMU_CLOCK_VIRTUAL,
                  th1520_ap_clock_lock, s);
    memory_region_init_io(&s->iomem, obj, &th1520_ap_clock_ops, s,
                          TYPE_TH1520_AP_CLOCK,
                          TH1520_AP_CLOCK_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    qdev_init_gpio_out_named(DEVICE(s), s->peripheral_clock_enable,
                             "peripheral-clock-enable",
                             TH1520_AP_CLOCK_GATE_COUNT);
    s->pwm_clock = qdev_init_clock_out(DEVICE(s),
                                       TH1520_AP_CLOCK_PWM_OUTPUT);
    s->timer_clock[0] = qdev_init_clock_out(
        DEVICE(s), TH1520_AP_CLOCK_TIMER0_OUTPUT);
    s->timer_clock[1] = qdev_init_clock_out(
        DEVICE(s), TH1520_AP_CLOCK_TIMER1_OUTPUT);
    s->wdt_clock[0] = qdev_init_clock_out(
        DEVICE(s), TH1520_AP_CLOCK_WDT0_OUTPUT);
    s->wdt_clock[1] = qdev_init_clock_out(
        DEVICE(s), TH1520_AP_CLOCK_WDT1_OUTPUT);
    s->gmac_axi_clock = qdev_init_clock_out(
        DEVICE(s), TH1520_AP_CLOCK_GMAC_AXI_OUTPUT);
}

static void th1520_ap_clock_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 application clock controller";
    dc->vmsd = &vmstate_th1520_ap_clock;
    device_class_set_legacy_reset(dc, th1520_ap_clock_reset);
}

#define RESET_REG(offset, reset, mask) { (offset), (reset), (mask) }

static const TH1520RegInfo th1520_ap_reset_reginfo[] = {
    RESET_REG(0x000, 0x1, 0x1),
    RESET_REG(0x004, 0x3, 0x1f),
    RESET_REG(0x00c, 0x3, 0x3),
    RESET_REG(0x010, 0x3, 0x3),
    RESET_REG(0x014, 0x1, 0x1),
    RESET_REG(0x018, 0x1, 0x1),
    RESET_REG(0x01c, 0x1, 0x1),
    RESET_REG(0x020, 0x1, 0x1),
    RESET_REG(0x024, 0x1, 0x1),
    RESET_REG(0x028, 0x1, 0x1),
    RESET_REG(0x02c, 0x1, 0x1),
    RESET_REG(0x030, 0x1, 0x1),
    RESET_REG(0x034, 0x1, 0x1),
    RESET_REG(0x038, 0x1, 0x1),
    RESET_REG(0x03c, 0x3, 0x3),
    RESET_REG(0x040, 0x3, 0x3),
    RESET_REG(0x044, 0x1, 0x1),
    RESET_REG(0x048, 0x1, 0x1),
    RESET_REG(0x04c, 0x1, 0x1),
    RESET_REG(0x068, 0xf, 0xf),
    RESET_REG(0x070, 0x3, 0x3),
    RESET_REG(0x074, 0x3, 0x3),
    RESET_REG(0x078, 0x3, 0x3),
    RESET_REG(0x07c, 0x3, 0x3),
    RESET_REG(0x080, 0x3, 0x3),
    RESET_REG(0x084, 0x3, 0x3),
    RESET_REG(0x08c, 0x3, 0x3),
    RESET_REG(0x090, 0x3, 0x3),
    RESET_REG(0x094, 0x3, 0x3),
    RESET_REG(0x098, 0x3, 0x3),
    RESET_REG(0x09c, 0x3, 0x3),
    RESET_REG(0x0a0, 0x3, 0x3),
    RESET_REG(0x0a4, 0x3, 0x3),
    RESET_REG(0x0a8, 0x3, 0x3),
    RESET_REG(0x0ac, 0x3, 0x3),
    RESET_REG(0x0b0, 0x3, 0x3),
    RESET_REG(0x0b4, 0x3, 0x3),
    RESET_REG(0x0b8, 0x3, 0x3),
    RESET_REG(0x0c0, 0x3, 0x3),
    RESET_REG(0x0c4, 0x1, 0x1),
    RESET_REG(0x0cc, 0x2, 0x2),
    RESET_REG(0x0d4, 0x1, 0x1),
    RESET_REG(0x0d8, 0x3, 0x3),
    RESET_REG(0x0dc, 0x1, 0x1),
    RESET_REG(0x0e4, 0x1, 0x1),
    RESET_REG(0x0f8, 0x3, 0x3),
    RESET_REG(0x0fc, 0x1, 0x1),
    RESET_REG(0x128, 0x3, 0x3),
    RESET_REG(0x12c, 0x1, 0x1),
    RESET_REG(0x138, 0x1, 0x1),
    RESET_REG(0x148, 0x3, 0x3),
    RESET_REG(0x14c, 0x3, 0x3),
    RESET_REG(0x178, 0x1, 0x1),
    RESET_REG(0x188, 0x1, 0x1),
    RESET_REG(0x18c, 0x1, 0x1),
    RESET_REG(0x1a8, 0x3, 0x3),
    RESET_REG(0x1ac, 0x1, 0x1),
    RESET_REG(0x1b0, 0x0, 0x1),
    RESET_REG(0x1dc, 0x3, 0x3),
    RESET_REG(0x1ec, 0x1, 0x1),
    RESET_REG(0x1f8, 0x1, 0x1),
    RESET_REG(0x204, 0xf, 0xf),
    RESET_REG(0x208, 0x3, 0x3),
    RESET_REG(0x20c, 0x1, 0x1),
    RESET_REG(0x210, 0x3, 0x3),
    RESET_REG(0x214, 0x1, 0x1),
    RESET_REG(0x218, 0x1, 0x1),
    RESET_REG(0x220, 0x8, 0xf),
};

typedef struct TH1520ResetOutputInfo {
    uint16_t offset;
    uint32_t mask;
} TH1520ResetOutputInfo;

/*
 * Reset membership comes from reset-th1520.c.  Treat any active-low member
 * represented by a QEMU device as a whole-device reset until hardware
 * establishes the individual APB/core/AXI effects (ledger item RST-001).
 */
static const TH1520ResetOutputInfo
th1520_ap_reset_output_info[TH1520_AP_RESET_OUTPUT_COUNT] = {
    [TH1520_AP_RESET_WDT0] = { 0x034, 0x1 },
    [TH1520_AP_RESET_WDT1] = { 0x038, 0x1 },
    [TH1520_AP_RESET_PWM] = { 0x0c0, 0x3 },
    [TH1520_AP_RESET_TIMER0_3] = { 0x03c, 0x3 },
    [TH1520_AP_RESET_TIMER4_7] = { 0x040, 0x3 },
    [TH1520_AP_RESET_UART0] = { 0x070, 0x3 },
    [TH1520_AP_RESET_UART1] = { 0x074, 0x3 },
    [TH1520_AP_RESET_UART2] = { 0x078, 0x3 },
    [TH1520_AP_RESET_UART3] = { 0x07c, 0x3 },
    [TH1520_AP_RESET_UART4] = { 0x080, 0x3 },
    [TH1520_AP_RESET_UART5] = { 0x084, 0x3 },
    [TH1520_AP_RESET_I2C0] = { 0x098, 0x3 },
    [TH1520_AP_RESET_I2C1] = { 0x09c, 0x3 },
    [TH1520_AP_RESET_I2C2] = { 0x0a0, 0x3 },
    [TH1520_AP_RESET_I2C3] = { 0x0a4, 0x3 },
    [TH1520_AP_RESET_I2C4] = { 0x0a8, 0x3 },
    [TH1520_AP_RESET_I2C5] = { 0x0ac, 0x3 },
    [TH1520_AP_RESET_SPI0] = { 0x094, 0x3 },
    [TH1520_AP_RESET_GPIO0] = { 0x0b0, 0x3 },
    [TH1520_AP_RESET_GPIO1] = { 0x0b4, 0x3 },
    [TH1520_AP_RESET_GPIO2] = { 0x0b8, 0x3 },
    [TH1520_AP_RESET_GPIO3] = { 0x1a8, 0x3 },
    [TH1520_AP_RESET_PADCTRL0] = { 0x0c4, 0x1 },
    [TH1520_AP_RESET_PADCTRL1] = { 0x20c, 0x1 },
    [TH1520_AP_RESET_DMAC0] = { 0x14c, 0x3 },
    [TH1520_AP_RESET_GMAC0] = { 0x068, 0xf },
    [TH1520_AP_RESET_GMAC1] = { 0x204, 0xf },
    [TH1520_AP_RESET_GMAC_SHARED] = { 0x208, 0x3 },
};

static void th1520_ap_reset_update_output(TH1520APResetState *s,
                                          unsigned int output, bool force)
{
    const TH1520ResetOutputInfo *info =
        &th1520_ap_reset_output_info[output];
    bool asserted = (s->regs[info->offset / 4] & info->mask) != info->mask;

    if (force || s->reset_asserted[output] != asserted) {
        s->reset_asserted[output] = asserted;
        qemu_set_irq(s->peripheral_reset[output], asserted);
    }
}

static void th1520_ap_reset_update_outputs(TH1520APResetState *s,
                                            hwaddr offset, bool force)
{
    for (unsigned int i = 0; i < TH1520_AP_RESET_OUTPUT_COUNT; i++) {
        if (force || th1520_ap_reset_output_info[i].offset == offset) {
            th1520_ap_reset_update_output(s, i, force);
        }
    }
}

static uint64_t th1520_ap_reset_read(void *opaque, hwaddr offset,
                                     unsigned int size)
{
    TH1520APResetState *s = opaque;
    const TH1520RegInfo *info =
        th1520_reginfo_find(th1520_ap_reset_reginfo,
                            ARRAY_SIZE(th1520_ap_reset_reginfo), offset);

    if (!info) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_AP_RESET, offset);
        return 0;
    }

    return s->regs[offset / 4];
}

static void th1520_ap_reset_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned int size)
{
    TH1520APResetState *s = opaque;
    const TH1520RegInfo *info =
        th1520_reginfo_find(th1520_ap_reset_reginfo,
                            ARRAY_SIZE(th1520_ap_reset_reginfo), offset);
    uint32_t old;

    if (!info) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_AP_RESET, offset);
        return;
    }

    old = s->regs[offset / 4];
    s->regs[offset / 4] = (old & ~info->write_mask) |
                          ((uint32_t)value & info->write_mask);
    if (old != s->regs[offset / 4]) {
        th1520_ap_reset_update_outputs(s, offset, false);
    }
}

static const MemoryRegionOps th1520_ap_reset_ops = {
    .read = th1520_ap_reset_read,
    .write = th1520_ap_reset_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void th1520_ap_reset_reset(DeviceState *dev)
{
    TH1520APResetState *s = TH1520_AP_RESET(dev);

    memset(s->regs, 0, sizeof(s->regs));
    for (size_t i = 0; i < ARRAY_SIZE(th1520_ap_reset_reginfo); i++) {
        const TH1520RegInfo *info = &th1520_ap_reset_reginfo[i];

        s->regs[info->offset / 4] = info->reset;
    }
    th1520_ap_reset_update_outputs(s, 0, true);
}

static int th1520_ap_reset_post_load(void *opaque, int version_id)
{
    TH1520APResetState *s = opaque;

    th1520_ap_reset_update_outputs(s, 0, true);
    return 0;
}

static const VMStateDescription vmstate_th1520_ap_reset = {
    .name = TYPE_TH1520_AP_RESET,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = th1520_ap_reset_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, TH1520APResetState,
                             TH1520_AP_RESET_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_ap_reset_init(Object *obj)
{
    TH1520APResetState *s = TH1520_AP_RESET(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_ap_reset_ops, s,
                          TYPE_TH1520_AP_RESET,
                          TH1520_AP_RESET_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    qdev_init_gpio_out_named(DEVICE(s), s->peripheral_reset,
                             "peripheral-reset",
                             TH1520_AP_RESET_OUTPUT_COUNT);
}

static void th1520_ap_reset_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 application reset controller";
    dc->vmsd = &vmstate_th1520_ap_reset;
    device_class_set_legacy_reset(dc, th1520_ap_reset_reset);
}

static const TypeInfo th1520_cpr_types[] = {
    {
        .name = TYPE_TH1520_AP_CLOCK,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(TH1520APClockState),
        .instance_init = th1520_ap_clock_init,
        .class_init = th1520_ap_clock_class_init,
    }, {
        .name = TYPE_TH1520_AP_RESET,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(TH1520APResetState),
        .instance_init = th1520_ap_reset_init,
        .class_init = th1520_ap_reset_class_init,
    },
};

DEFINE_TYPES(th1520_cpr_types)
