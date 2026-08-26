/*
 * T-Head TH1520 BOOT_SEL register
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_BOOTSEL_H
#define HW_MISC_TH1520_BOOTSEL_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_BOOTSEL "th1520-bootsel"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520BootSelState, TH1520_BOOTSEL)

#define TH1520_BOOTSEL_MMIO_SIZE       0x4
#define TH1520_BOOTSEL_SELECT_MASK     0x0f
#define TH1520_BOOTSEL_UPDATE          0x10
#define TH1520_BOOTSEL_EMMC            0x04

struct TH1520BootSelState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint8_t boot_sel;
};

#endif /* HW_MISC_TH1520_BOOTSEL_H */
