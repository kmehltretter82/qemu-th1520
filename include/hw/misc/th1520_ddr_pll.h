/*
 * T-Head TH1520 DDR PLL configuration registers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_DDR_PLL_H
#define HW_MISC_TH1520_DDR_PLL_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_DDR_PLL "th1520-ddr-pll"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520DDRPLLState, TH1520_DDR_PLL)

#define TH1520_DDR_PLL_MMIO_SIZE               0x4

/* Values and fields used by the public vendor SPL's LPDDR4 initialization. */
#define TH1520_DDR_PLL_CFG0_RESET              0x01408501
#define TH1520_DDR_PLL_CFG1_RESET              0x03000000
#define TH1520_DDR_PLL_CFG0_WRITABLE_MASK      0x077fff3f
#define TH1520_DDR_PLL_CFG1_WRITABLE_MASK      0xdbffffff
#define TH1520_DDR_PLL_CFG1_RESET_BIT          0x40000000
#define TH1520_DDR_PLL_STS_LOCK                0x00000001
#define TH1520_DDR_PLL_STS_CORE_CLOCK_CG       0x00010000

struct TH1520DDRPLLState {
    SysBusDevice parent_obj;

    MemoryRegion cfg0_iomem;
    MemoryRegion cfg1_iomem;
    MemoryRegion sts_iomem;
    uint32_t pll_cfg0;
    uint32_t pll_cfg1;
    bool pll_lock;
    bool core_clock_cg;
};

#endif /* HW_MISC_TH1520_DDR_PLL_H */
