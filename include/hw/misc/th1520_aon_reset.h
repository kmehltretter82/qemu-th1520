/*
 * T-Head TH1520 AON reset-generator register
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_AON_RESET_H
#define HW_MISC_TH1520_AON_RESET_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_AON_RESET "th1520-aon-reset"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520AONResetState, TH1520_AON_RESET)

#define TH1520_AON_RESET_MMIO_SIZE 0x4
#define TH1520_AON_RESET_AUDIO_RST_CFG_MASK 0x0000003f
#define TH1520_AON_RESET_AUDIO_RST_CFG_RESET 0x00000000

struct TH1520AONResetState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t audio_rst_cfg;
};

#endif /* HW_MISC_TH1520_AON_RESET_H */
