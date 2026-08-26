/*
 * T-Head TH1520 TEE DSP-system reset register
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_TEE_DSP_RESET_H
#define HW_MISC_TH1520_TEE_DSP_RESET_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_TEE_DSP_RESET "th1520-tee-dsp-reset"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520TEEDSPResetState, TH1520_TEE_DSP_RESET)

#define TH1520_TEE_DSP_RESET_MMIO_SIZE 0x4
#define TH1520_TEE_DSP_RESET_SW_RST_MASK 0x7d11770f
#define TH1520_TEE_DSP_RESET_SW_RST_RESET 0x7d11000f

struct TH1520TEEDSPResetState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t sw_rst;
};

#endif /* HW_MISC_TH1520_TEE_DSP_RESET_H */
