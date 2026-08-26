/*
 * T-Head TH1520 TEE VOSYS DPU_RST_CFG_TEE register
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_TEE_VOSYS_DPU_RESET_H
#define HW_MISC_TH1520_TEE_VOSYS_DPU_RESET_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_TEE_VOSYS_DPU_RESET "th1520-tee-vosys-dpu-reset"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520TEEVOSYSDPUResetState,
                           TH1520_TEE_VOSYS_DPU_RESET)

#define TH1520_TEE_VOSYS_DPU_RESET_MMIO_SIZE 0x4
#define TH1520_TEE_VOSYS_DPU_RESET_MASK      0x00000007
#define TH1520_TEE_VOSYS_DPU_RESET_VALUE     0x00000000

struct TH1520TEEVOSYSDPUResetState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t dpu_rst_cfg;
};

#endif /* HW_MISC_TH1520_TEE_VOSYS_DPU_RESET_H */
