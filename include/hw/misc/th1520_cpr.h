/*
 * T-Head TH1520 application-domain clock and reset controllers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_CPR_H
#define HW_MISC_TH1520_CPR_H

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

struct TH1520APClockState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    QEMUTimer pll_lock_timer;
    uint32_t regs[TH1520_AP_CLOCK_REGS];
    uint32_t pll_pending;
    int64_t pll_deadline[TH1520_AP_PLL_COUNT];
    int64_t pll_remaining[TH1520_AP_PLL_COUNT];
};

struct TH1520APResetState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[TH1520_AP_RESET_REGS];
};

#endif /* HW_MISC_TH1520_CPR_H */
