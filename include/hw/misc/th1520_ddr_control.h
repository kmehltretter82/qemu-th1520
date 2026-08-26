/*
 * T-Head TH1520 DDR control configuration register
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_DDR_CONTROL_H
#define HW_MISC_TH1520_DDR_CONTROL_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_DDR_CONTROL "th1520-ddr-control"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520DDRControlState, TH1520_DDR_CONTROL)

#define TH1520_DDR_CONTROL_MMIO_SIZE      0x4

/* Generated vendor SPL fields for DDR_SYSREG.DDR_CFG0. */
#define TH1520_DDR_CFG0_RESET              0x00000000
#define TH1520_DDR_CFG0_WRITABLE_MASK      0xfffffff3

struct TH1520DDRControlState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t cfg0;
};

#endif /* HW_MISC_TH1520_DDR_CONTROL_H */
