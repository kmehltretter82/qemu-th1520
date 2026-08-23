/*
 * DesignWare I2C Module.
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DESIGNWARE_I2C_H
#define DESIGNWARE_I2C_H

#include "qemu/fifo8.h"
#include "hw/i2c/i2c.h"
#include "hw/core/irq.h"
#include "hw/core/register.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define DESIGNWARE_I2C_R_MAX (0x100 / 4)

#define DESIGNWARE_I2C_RX_FIFO_SIZE 16
#define DESIGNWARE_I2C_TX_FIFO_SIZE 16

typedef enum DesignWareI2CStatus {
    DW_I2C_STATUS_IDLE,
    DW_I2C_STATUS_SENDING_ADDRESS,
    DW_I2C_STATUS_SENDING,
    DW_I2C_STATUS_RECEIVING,
} DesignWareI2CStatus;

#define TYPE_DESIGNWARE_I2C "designware-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(DesignWareI2CState, DESIGNWARE_I2C)

/*
 * struct DesignWareI2CState - DesignWare I2C device state.
 * @bus: The underlying I2C Bus
 * @irq: Interrupt line fired on transaction events.
 * @rx_fifo: The FIFO buffer for receiving in FIFO mode.
 */
struct DesignWareI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    I2CBus      *bus;
    qemu_irq     irq;

    uint32_t regs[DESIGNWARE_I2C_R_MAX];
    RegisterInfo regs_info[DESIGNWARE_I2C_R_MAX];

    /* fifo8_num_used(rx_fifo) should always equal DW_IC_RXFLR */
    Fifo8    rx_fifo;

    DesignWareI2CStatus status;

    /* Synthesis parameters and reset values exposed through QOM. */
    uint32_t component_parameters;
    uint32_t component_version;
    uint32_t component_type;
    uint32_t intr_mask_reset;
    uint32_t intr_mask_valid;
    uint32_t fs_spklen_reset;
    uint32_t hs_spklen_reset;
    uint32_t scl_stuck_timeout_reset;
    uint32_t sda_stuck_timeout_reset;
    uint32_t ack_general_call_reset;
};

#endif /* DESIGNWARE_I2C_H */
