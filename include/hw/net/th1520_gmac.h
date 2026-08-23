/*
 * T-Head TH1520 GMAC APB clock glue
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NET_TH1520_GMAC_H
#define HW_NET_TH1520_GMAC_H

#include "hw/core/sysbus.h"

#define TH1520_GMAC_APB_REG_SIZE 0x1000
#define TH1520_GMAC_APB_NR_REGS  9

#define TYPE_TH1520_GMAC_APB "th1520-gmac-apb"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520GMACAPBState, TH1520_GMAC_APB)

struct TH1520GMACAPBState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[TH1520_GMAC_APB_NR_REGS];
};

#endif /* HW_NET_TH1520_GMAC_H */
