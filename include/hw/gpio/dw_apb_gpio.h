/*
 * Synopsys DesignWare APB GPIO
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_GPIO_DW_APB_GPIO_H
#define HW_GPIO_DW_APB_GPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_DW_APB_GPIO "dw-apb-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(DWAPBGPIOState, DW_APB_GPIO)

#define DW_APB_GPIO_MAX_PINS 32
#define DW_APB_GPIO_MMIO_SIZE 0x1000

struct DWAPBGPIOState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq output[DW_APB_GPIO_MAX_PINS];

    uint32_t swporta_dr;
    uint32_t swporta_ddr;
    uint32_t swporta_ctl;
    uint32_t inten;
    uint32_t intmask;
    uint32_t inttype_level;
    uint32_t int_polarity;
    uint32_t edge_pending;
    uint32_t debounce;
    uint32_t ls_sync;
    uint32_t external_level;
    uint32_t external_driven;
    uint32_t pin_level;

    uint32_t reset_data;
    uint32_t reset_direction;
    uint32_t component_id;
    uint32_t component_version;
    uint32_t config_reg1;
    uint32_t config_reg2;
    uint32_t pin_mask;
    uint8_t ngpios;
};

#endif /* HW_GPIO_DW_APB_GPIO_H */
