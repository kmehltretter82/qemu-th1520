/*
 * T-Head TH1520 TEE miscellaneous-system clock-control register
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_TEE_MISCSYS_CLOCK_H
#define HW_MISC_TH1520_TEE_MISCSYS_CLOCK_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_TEE_MISCSYS_CLOCK "th1520-tee-miscsys-clock"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520TEEMiscSysClockState,
                           TH1520_TEE_MISCSYS_CLOCK)

#define TH1520_TEE_MISCSYS_CLOCK_MMIO_SIZE 0x4
#define TH1520_TEE_MISCSYS_CLOCK_ENABLE_MASK 0x000007ff
#define TH1520_TEE_MISCSYS_CLOCK_RESET 0x000007ff

struct TH1520TEEMiscSysClockState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t clock_ctrl;
};

#endif /* HW_MISC_TH1520_TEE_MISCSYS_CLOCK_H */
