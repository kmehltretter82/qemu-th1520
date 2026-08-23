/*
 * Synopsys DesignWare APB SSI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SSI_DW_APB_SSI_H
#define HW_SSI_DW_APB_SSI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo32.h"
#include "qom/object.h"

#define TYPE_DW_APB_SSI "dw-apb-ssi"
OBJECT_DECLARE_SIMPLE_TYPE(DWAPBSSIState, DW_APB_SSI)

#define DW_APB_SSI_MAX_CS 16

/*
 * QEMU interface:
 *  + sysbus MMIO region 0: the 4 KiB component register bank
 *  + sysbus IRQ 0: combined interrupt output
 *  + GPIO outputs "cs" 0..num-cs-1: active-low native chip selects
 *  + SSI bus "spi": peripherals can be attached by a board model
 */
struct DWAPBSSIState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq cs[DW_APB_SSI_MAX_CS];
    SSIBus *spi;
    Fifo32 tx_fifo;
    Fifo32 rx_fifo;

    uint32_t ctrlr0;
    uint32_t ctrlr1;
    uint32_t ssienr;
    uint32_t mwcr;
    uint32_t ser;
    uint32_t baudr;
    uint32_t txftlr;
    uint32_t rxftlr;
    uint32_t imr;
    uint32_t dmacr;
    uint32_t dmatdlr;
    uint32_t dmardlr;
    uint32_t rx_sample_dly;
    uint32_t spi_ctrlr0;
    uint32_t tx_overflow;
    uint32_t rx_underflow;
    uint32_t rx_overflow;
    uint32_t mst_error;
    uint32_t auto_rx_remaining;

    uint32_t component_id;
    uint32_t component_version;
    uint16_t fifo_depth;
    uint8_t num_cs;
};

#endif /* HW_SSI_DW_APB_SSI_H */
