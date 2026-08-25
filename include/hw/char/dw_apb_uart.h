/*
 * Synopsys DesignWare APB UART
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_DW_APB_UART_H
#define HW_CHAR_DW_APB_UART_H

#include "hw/char/serial.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_DW_APB_UART "dw-apb-uart"
OBJECT_DECLARE_SIMPLE_TYPE(DWAPBUARTState, DW_APB_UART)

struct DWAPBUARTState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    SerialState serial;
    qemu_irq irq;
    qemu_irq serial_irq_sink;

    uint32_t dlf;
    uint32_t component_parameters;
    uint32_t component_version;
    uint32_t component_type;
    int64_t busy_until;
    uint8_t dlf_width;
    bool serial_irq_level;
    bool busy_irq;
    bool uart_16550_compatible;
    bool fifo_stat;
    bool legacy_ahead_vmstate;
};

#endif /* HW_CHAR_DW_APB_UART_H */
