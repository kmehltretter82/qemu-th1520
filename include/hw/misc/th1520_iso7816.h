/*
 * T-Head TH1520 ISO7816 compatibility configuration register
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_ISO7816_H
#define HW_MISC_TH1520_ISO7816_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_ISO7816_CONFIG "th1520-iso7816-config"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520ISO7816ConfigState, TH1520_ISO7816_CONFIG)

#define TH1520_ISO7816_CONFIG_MMIO_SIZE 0x4
#define TH1520_ISO7816_CONFIG_MIE       0x00000080

struct TH1520ISO7816ConfigState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t config;
};

#endif /* HW_MISC_TH1520_ISO7816_H */
