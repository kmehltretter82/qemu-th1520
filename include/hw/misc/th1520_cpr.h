/*
 * T-Head TH1520 application-domain clock and reset controllers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_CPR_H
#define HW_MISC_TH1520_CPR_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_TH1520_AP_CLOCK "th1520-ap-clock"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520APClockState, TH1520_AP_CLOCK)

#define TYPE_TH1520_AP_RESET "th1520-ap-reset"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520APResetState, TH1520_AP_RESET)

#define TH1520_AP_CLOCK_MMIO_SIZE 0x1000
#define TH1520_AP_RESET_MMIO_SIZE 0x1000
#define TH1520_AP_CLOCK_REGS (TH1520_AP_CLOCK_MMIO_SIZE / sizeof(uint32_t))
#define TH1520_AP_RESET_REGS (TH1520_AP_RESET_MMIO_SIZE / sizeof(uint32_t))
#define TH1520_AP_PLL_COUNT 7

#define TH1520_AP_CLOCK_PWM_OUTPUT "pwm"
#define TH1520_AP_CLOCK_TIMER0_OUTPUT "timer0"
#define TH1520_AP_CLOCK_TIMER1_OUTPUT "timer1"
#define TH1520_AP_CLOCK_WDT0_OUTPUT "wdt0"
#define TH1520_AP_CLOCK_WDT1_OUTPUT "wdt1"
#define TH1520_AP_CLOCK_GMAC_AXI_OUTPUT "gmac-axi"

/*
 * Active-high leaf-clock enables backed by the mainline TH1520 AP clock
 * driver.  Only gates whose consumers currently exist in this machine are
 * exported; shared parent gates and clocks for absent devices remain
 * software-visible register state.
 */
enum {
    TH1520_AP_CLOCK_GATE_EMMC_SDIO,
    TH1520_AP_CLOCK_GATE_GMAC1,
    TH1520_AP_CLOCK_GATE_PADCTRL1,
    TH1520_AP_CLOCK_GATE_PADCTRL0,
    TH1520_AP_CLOCK_GATE_GMAC_AXI,
    TH1520_AP_CLOCK_GATE_GPIO3,
    TH1520_AP_CLOCK_GATE_GMAC0,
    TH1520_AP_CLOCK_GATE_PWM,
    TH1520_AP_CLOCK_GATE_SPI,
    TH1520_AP_CLOCK_GATE_UART0,
    TH1520_AP_CLOCK_GATE_UART1,
    TH1520_AP_CLOCK_GATE_UART2,
    TH1520_AP_CLOCK_GATE_UART3,
    TH1520_AP_CLOCK_GATE_UART4,
    TH1520_AP_CLOCK_GATE_UART5,
    TH1520_AP_CLOCK_GATE_GPIO0,
    TH1520_AP_CLOCK_GATE_GPIO1,
    TH1520_AP_CLOCK_GATE_GPIO2,
    TH1520_AP_CLOCK_GATE_I2C0,
    TH1520_AP_CLOCK_GATE_I2C1,
    TH1520_AP_CLOCK_GATE_I2C2,
    TH1520_AP_CLOCK_GATE_I2C3,
    TH1520_AP_CLOCK_GATE_I2C4,
    TH1520_AP_CLOCK_GATE_I2C5,
    TH1520_AP_CLOCK_GATE_DMA,
    TH1520_AP_CLOCK_GATE_MBOX0,
    TH1520_AP_CLOCK_GATE_MBOX1,
    TH1520_AP_CLOCK_GATE_MBOX2,
    TH1520_AP_CLOCK_GATE_MBOX3,
    TH1520_AP_CLOCK_GATE_WDT0,
    TH1520_AP_CLOCK_GATE_WDT1,
    TH1520_AP_CLOCK_GATE_TIMER0,
    TH1520_AP_CLOCK_GATE_TIMER1,
    TH1520_AP_CLOCK_GATE_COUNT,
};

/*
 * Software-visible reset outputs backed by exact Linux reset IDs.  An output
 * is asserted when any reset member represented by that QEMU device is active
 * low.  Other reset words remain register-only.
 */
enum {
    TH1520_AP_RESET_PWM,
    TH1520_AP_RESET_TIMER0_3,
    TH1520_AP_RESET_TIMER4_7,
    TH1520_AP_RESET_WDT0,
    TH1520_AP_RESET_WDT1,
    TH1520_AP_RESET_UART0,
    TH1520_AP_RESET_UART1,
    TH1520_AP_RESET_UART2,
    TH1520_AP_RESET_UART3,
    TH1520_AP_RESET_UART4,
    TH1520_AP_RESET_UART5,
    TH1520_AP_RESET_I2C0,
    TH1520_AP_RESET_I2C1,
    TH1520_AP_RESET_I2C2,
    TH1520_AP_RESET_I2C3,
    TH1520_AP_RESET_I2C4,
    TH1520_AP_RESET_I2C5,
    TH1520_AP_RESET_SPI0,
    TH1520_AP_RESET_GPIO0,
    TH1520_AP_RESET_GPIO1,
    TH1520_AP_RESET_GPIO2,
    TH1520_AP_RESET_GPIO3,
    TH1520_AP_RESET_PADCTRL0,
    TH1520_AP_RESET_PADCTRL1,
    TH1520_AP_RESET_DMAC0,
    TH1520_AP_RESET_GMAC0,
    TH1520_AP_RESET_GMAC1,
    TH1520_AP_RESET_GMAC_SHARED,
    TH1520_AP_RESET_OUTPUT_COUNT,
};

struct TH1520APClockState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    QEMUTimer pll_lock_timer;
    qemu_irq peripheral_clock_enable[TH1520_AP_CLOCK_GATE_COUNT];
    Clock *pwm_clock;
    Clock *timer_clock[2];
    Clock *wdt_clock[2];
    Clock *gmac_axi_clock;
    uint32_t regs[TH1520_AP_CLOCK_REGS];
    uint32_t pll_pending;
    int64_t pll_deadline[TH1520_AP_PLL_COUNT];
    int64_t pll_remaining[TH1520_AP_PLL_COUNT];
    bool clock_enabled[TH1520_AP_CLOCK_GATE_COUNT];
};

struct TH1520APResetState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq peripheral_reset[TH1520_AP_RESET_OUTPUT_COUNT];
    uint32_t regs[TH1520_AP_RESET_REGS];
    bool reset_asserted[TH1520_AP_RESET_OUTPUT_COUNT];
};

#endif /* HW_MISC_TH1520_CPR_H */
