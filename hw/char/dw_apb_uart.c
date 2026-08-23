/*
 * Synopsys DesignWare APB UART
 *
 * The core's standard registers use QEMU's 16550 implementation.  This
 * wrapper supplies the DesignWare APB register layout and behavior which is
 * visible to firmware and operating-system drivers.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/char/dw_apb_uart.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"

#define DW_UART_APERTURE             0x100

#define DW_UART_RBR_THR_DLL          0x00
#define DW_UART_IER_DLH              0x04
#define DW_UART_IIR_FCR              0x08
#define DW_UART_LCR                  0x0c
#define DW_UART_LSR                  0x14
#define DW_UART_MSR                  0x18
#define DW_UART_SCR                  0x1c
#define DW_UART_USR                  0x7c
#define DW_UART_TFL                  0x80
#define DW_UART_RFL                  0x84
#define DW_UART_SRR                  0x88
#define DW_UART_DLF                  0xc0
#define DW_UART_CPR                  0xf4
#define DW_UART_UCV                  0xf8
#define DW_UART_CTR                  0xfc

#define DW_UART_LCR_DLAB             BIT(7)
#define DW_UART_FCR_ENABLE           BIT(0)
#define DW_UART_FCR_RFR              BIT(1)
#define DW_UART_FCR_XFR              BIT(2)

#define DW_UART_LSR_DR               BIT(0)
#define DW_UART_LSR_THRE             BIT(5)
#define DW_UART_LSR_TEMT             BIT(6)

#define DW_UART_IIR_NO_INT           BIT(0)
#define DW_UART_IIR_BUSY             0x07

#define DW_UART_USR_BUSY             BIT(0)
#define DW_UART_USR_TFNF             BIT(1)
#define DW_UART_USR_TFE              BIT(2)
#define DW_UART_USR_RFNE             BIT(3)
#define DW_UART_USR_RFF              BIT(4)

#define DW_UART_SRR_UR               BIT(0)
#define DW_UART_SRR_RFR              BIT(1)
#define DW_UART_SRR_XFR              BIT(2)

#define DW_UART_CTR_DEFAULT          0x44570110

static void dw_apb_uart_update_irq(DWAPBUARTState *s)
{
    qemu_set_irq(s->irq, s->serial_irq_level || s->busy_irq);
}

static void dw_apb_uart_serial_irq(void *opaque, int n, int level)
{
    DWAPBUARTState *s = opaque;

    s->serial_irq_level = level;
    dw_apb_uart_update_irq(s);
}

static bool dw_apb_uart_busy(DWAPBUARTState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    return !(s->serial.lsr & DW_UART_LSR_TEMT) || now < s->busy_until;
}

static uint32_t dw_apb_uart_fifo_level(Fifo8 *fifo, bool fifo_enabled,
                                       bool holding_register_full)
{
    if (fifo_enabled) {
        return fifo8_num_used(fifo);
    }

    return holding_register_full;
}

static uint32_t dw_apb_uart_usr(DWAPBUARTState *s)
{
    bool fifo_enabled = s->serial.fcr & DW_UART_FCR_ENABLE;
    bool rx_not_empty = s->serial.lsr & DW_UART_LSR_DR;
    bool tx_empty = s->serial.lsr & DW_UART_LSR_TEMT;
    bool tx_not_full;
    bool rx_full;
    uint32_t value = 0;

    if (fifo_enabled) {
        tx_not_full = !fifo8_is_full(&s->serial.xmit_fifo);
        rx_full = fifo8_is_full(&s->serial.recv_fifo);
    } else {
        tx_not_full = s->serial.lsr & DW_UART_LSR_THRE;
        rx_full = rx_not_empty;
    }

    if (dw_apb_uart_busy(s)) {
        value |= DW_UART_USR_BUSY;
    }
    if (tx_not_full) {
        value |= DW_UART_USR_TFNF;
    }
    if (tx_empty) {
        value |= DW_UART_USR_TFE;
    }
    if (rx_not_empty) {
        value |= DW_UART_USR_RFNE;
    }
    if (rx_full) {
        value |= DW_UART_USR_RFF;
    }

    return value;
}

static void dw_apb_uart_busy_detect(DWAPBUARTState *s)
{
    if (!s->uart_16550_compatible) {
        s->busy_irq = true;
        dw_apb_uart_update_irq(s);
    }
}

static bool dw_apb_uart_write_blocked(DWAPBUARTState *s, hwaddr addr)
{
    if (s->uart_16550_compatible || !dw_apb_uart_busy(s)) {
        return false;
    }

    if (addr == DW_UART_LCR ||
        ((addr == DW_UART_RBR_THR_DLL || addr == DW_UART_IER_DLH) &&
         (s->serial.lcr & DW_UART_LCR_DLAB))) {
        dw_apb_uart_busy_detect(s);
        return true;
    }

    return false;
}

static uint64_t dw_apb_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    DWAPBUARTState *s = opaque;
    bool fifo_enabled = s->serial.fcr & DW_UART_FCR_ENABLE;

    if (addr <= DW_UART_SCR) {
        uint64_t value;

        if (addr == DW_UART_IIR_FCR && s->busy_irq) {
            return DW_UART_IIR_BUSY | (s->serial.iir & 0xf0);
        }
        value = serial_io_ops.read(&s->serial, addr >> 2, 1);
        if (addr == DW_UART_LSR && dw_apb_uart_busy(s)) {
            /* The transmit shift register is not empty while BUSY is set. */
            value &= ~DW_UART_LSR_TEMT;
        }
        return value;
    }

    switch (addr) {
    case DW_UART_USR: {
        uint32_t value = dw_apb_uart_usr(s);

        s->busy_irq = false;
        dw_apb_uart_update_irq(s);
        return value;
    }
    case DW_UART_TFL:
        if (!s->fifo_stat) {
            return 0;
        }
        return dw_apb_uart_fifo_level(&s->serial.xmit_fifo, fifo_enabled,
                                      !(s->serial.lsr & DW_UART_LSR_THRE));
    case DW_UART_RFL:
        if (!s->fifo_stat) {
            return 0;
        }
        return dw_apb_uart_fifo_level(&s->serial.recv_fifo, fifo_enabled,
                                      s->serial.lsr & DW_UART_LSR_DR);
    case DW_UART_DLF:
        return s->dlf;
    case DW_UART_CPR:
        return s->component_parameters;
    case DW_UART_UCV:
        return s->component_version;
    case DW_UART_CTR:
        return s->component_type;
    default:
        return 0;
    }
}

static void dw_apb_uart_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    DWAPBUARTState *s = opaque;
    uint32_t value32 = value;

    if (addr <= DW_UART_SCR) {
        if (dw_apb_uart_write_blocked(s, addr)) {
            return;
        }
        serial_io_ops.write(&s->serial, addr >> 2, value32 & 0xff, 1);
        if (addr == DW_UART_RBR_THR_DLL &&
            !(s->serial.lcr & DW_UART_LCR_DLAB)) {
            int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

            /*
             * The inherited serial core hands each character to the backend
             * immediately, so it has no transmit queue whose latency can be
             * accumulated here.  Keep the shift register busy for one frame.
             */
            s->busy_until = now + s->serial.char_transmit_time;
        }
        return;
    }

    switch (addr) {
    case DW_UART_SRR:
        if (value32 & DW_UART_SRR_UR) {
            device_cold_reset(DEVICE(s));
            return;
        }
        if (value32 & (DW_UART_SRR_RFR | DW_UART_SRR_XFR)) {
            uint8_t fcr = s->serial.fcr;

            if (value32 & DW_UART_SRR_RFR) {
                fcr |= DW_UART_FCR_RFR;
            }
            if (value32 & DW_UART_SRR_XFR) {
                fcr |= DW_UART_FCR_XFR;
            }
            serial_io_ops.write(&s->serial, DW_UART_IIR_FCR >> 2, fcr, 1);
        }
        break;
    case DW_UART_DLF:
        if (s->dlf_width) {
            s->dlf = value32 & MAKE_64BIT_MASK(0, s->dlf_width);
            serial_set_divisor_fraction(&s->serial, s->dlf, s->dlf_width);
        }
        break;
    default:
        break;
    }
}

static bool dw_apb_uart_access_valid(void *opaque, hwaddr addr,
                                     unsigned size, bool is_write,
                                     MemTxAttrs attrs)
{
    return size == 4 && !(addr & 3);
}

static const MemoryRegionOps dw_apb_uart_ops = {
    .read = dw_apb_uart_read,
    .write = dw_apb_uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = dw_apb_uart_access_valid,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void dw_apb_uart_reset(DeviceState *dev)
{
    DWAPBUARTState *s = DW_APB_UART(dev);

    device_cold_reset(DEVICE(&s->serial));
    serial_io_ops.write(&s->serial, DW_UART_IIR_FCR >> 2, 0, 1);

    s->dlf = 0;
    s->busy_until = 0;
    s->serial.divisor_fraction = 0;
    s->serial.divisor_fraction_bits = s->dlf_width;
    s->serial_irq_level = false;
    s->busy_irq = false;
    dw_apb_uart_update_irq(s);
}

static int dw_apb_uart_post_load(void *opaque, int version_id)
{
    DWAPBUARTState *s = opaque;

    serial_set_divisor_fraction(&s->serial, s->dlf, s->dlf_width);
    s->serial_irq_level = !(s->serial.iir & DW_UART_IIR_NO_INT);
    dw_apb_uart_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_dw_apb_uart = {
    .name = "dw-apb-uart",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = dw_apb_uart_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(serial, DWAPBUARTState, 0, vmstate_serial,
                       SerialState),
        VMSTATE_UINT32(dlf, DWAPBUARTState),
        VMSTATE_INT64(busy_until, DWAPBUARTState),
        VMSTATE_BOOL(busy_irq, DWAPBUARTState),
        VMSTATE_END_OF_LIST()
    },
};

static void dw_apb_uart_realize(DeviceState *dev, Error **errp)
{
    DWAPBUARTState *s = DW_APB_UART(dev);
    uint32_t encoded_fifo_size;

    if (s->dlf_width > 16) {
        error_setg(errp, "dlf-width must be between 0 and 16");
        return;
    }

    encoded_fifo_size = extract32(s->component_parameters, 16, 8) * 16;
    if (encoded_fifo_size && encoded_fifo_size != s->serial.fifo_size) {
        error_setg(errp, "component-parameters FIFO size %u does not match "
                   "fifo-size %u", encoded_fifo_size, s->serial.fifo_size);
        return;
    }

    s->serial_irq_sink = qemu_allocate_irq(dw_apb_uart_serial_irq, s, 0);
    s->serial.irq = s->serial_irq_sink;
    if (!qdev_realize(DEVICE(&s->serial), NULL, errp)) {
        qemu_free_irq(s->serial_irq_sink);
        s->serial_irq_sink = NULL;
        return;
    }

    serial_set_divisor_fraction(&s->serial, s->dlf, s->dlf_width);
}

static void dw_apb_uart_unrealize(DeviceState *dev)
{
    DWAPBUARTState *s = DW_APB_UART(dev);

    qemu_free_irq(s->serial_irq_sink);
    s->serial_irq_sink = NULL;
}

static void dw_apb_uart_init(Object *obj)
{
    DWAPBUARTState *s = DW_APB_UART(obj);

    object_initialize_child(obj, "serial", &s->serial, TYPE_SERIAL);
    qdev_alias_all_properties(DEVICE(&s->serial), obj);

    memory_region_init_io(&s->mmio, obj, &dw_apb_uart_ops, s,
                          TYPE_DW_APB_UART, DW_UART_APERTURE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const Property dw_apb_uart_properties[] = {
    DEFINE_PROP_UINT8("dlf-width", DWAPBUARTState, dlf_width, 0),
    DEFINE_PROP_UINT32("component-parameters", DWAPBUARTState,
                       component_parameters, 0),
    DEFINE_PROP_UINT32("component-version", DWAPBUARTState,
                       component_version, 0),
    DEFINE_PROP_UINT32("component-type", DWAPBUARTState, component_type,
                       DW_UART_CTR_DEFAULT),
    DEFINE_PROP_BOOL("uart-16550-compatible", DWAPBUARTState,
                     uart_16550_compatible, false),
    DEFINE_PROP_BOOL("fifo-stat", DWAPBUARTState, fifo_stat, false),
};

static void dw_apb_uart_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = dw_apb_uart_realize;
    dc->unrealize = dw_apb_uart_unrealize;
    dc->vmsd = &vmstate_dw_apb_uart;
    device_class_set_legacy_reset(dc, dw_apb_uart_reset);
    device_class_set_props(dc, dw_apb_uart_properties);
}

static const TypeInfo dw_apb_uart_info = {
    .name = TYPE_DW_APB_UART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWAPBUARTState),
    .instance_init = dw_apb_uart_init,
    .class_init = dw_apb_uart_class_init,
};

static void dw_apb_uart_register_types(void)
{
    type_register_static(&dw_apb_uart_info);
}

type_init(dw_apb_uart_register_types)
