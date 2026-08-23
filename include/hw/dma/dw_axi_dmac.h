/*
 * Synopsys DesignWare AXI DMA Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DMA_DW_AXI_DMAC_H
#define HW_DMA_DW_AXI_DMAC_H

#include "hw/core/sysbus.h"

#define TYPE_DW_AXI_DMAC "dw-axi-dmac"
OBJECT_DECLARE_SIMPLE_TYPE(DWAxiDMACState, DW_AXI_DMAC)

#define DW_AXI_DMAC_MAX_CHANNELS 8

typedef struct DWAxiDMACChannel {
    uint64_t sar;
    uint64_t dar;
    uint64_t block_ts;
    uint64_t ctl;
    uint64_t cfg;
    uint64_t llp;
    uint64_t status;
    uint64_t sw_hs_src;
    uint64_t sw_hs_dst;
    uint64_t axi_id;
    uint64_t axi_qos;
    uint64_t sstat_addr;
    uint64_t dstat_addr;
    uint32_t int_status_enable;
    uint32_t int_status;
    uint32_t int_signal_enable;
    bool enabled;
    bool suspended;
} DWAxiDMACChannel;

struct DWAxiDMACState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint64_t id;
    uint64_t component_version;
    uint64_t cfg;
    uint64_t low_power_cfg;
    uint32_t common_int_status_enable;
    uint32_t common_int_status;
    uint32_t common_int_signal_enable;

    uint32_t num_channels;
    uint32_t block_size;
    uint32_t data_width;
    DWAxiDMACChannel channel[DW_AXI_DMAC_MAX_CHANNELS];
};

#endif /* HW_DMA_DW_AXI_DMAC_H */
