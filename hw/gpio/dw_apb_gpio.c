/*
 * Synopsys DesignWare APB GPIO
 *
 * This model provides one Port A, including software data/direction control,
 * external pin sampling and the combined level/edge interrupt controller.
 * Port B-D and optional hardware-control inputs are not implemented.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/gpio/dw_apb_gpio.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define GPIO_SWPORTA_DR       0x00
#define GPIO_SWPORTA_DDR      0x04
#define GPIO_SWPORTA_CTL      0x08
#define GPIO_INTEN            0x30
#define GPIO_INTMASK          0x34
#define GPIO_INTTYPE_LEVEL    0x38
#define GPIO_INT_POLARITY     0x3c
#define GPIO_INTSTATUS        0x40
#define GPIO_RAW_INTSTATUS    0x44
#define GPIO_PORTA_DEBOUNCE   0x48
#define GPIO_PORTA_EOI        0x4c
#define GPIO_EXT_PORTA        0x50
#define GPIO_LS_SYNC          0x60
#define GPIO_ID_CODE          0x64
#define GPIO_VER_ID_CODE      0x6c
#define GPIO_CONFIG_REG2      0x70
#define GPIO_CONFIG_REG1      0x74

static uint32_t dw_apb_gpio_raw_status(DWAPBGPIOState *s)
{
    uint32_t active_level;
    uint32_t raw;

    active_level = ~(s->pin_level ^ s->int_polarity);
    active_level &= ~s->inttype_level;
    raw = (s->edge_pending & s->inttype_level) | active_level;

    return raw & s->inten & s->pin_mask;
}

static uint32_t dw_apb_gpio_status(DWAPBGPIOState *s)
{
    return dw_apb_gpio_raw_status(s) & ~s->intmask;
}

static void dw_apb_gpio_update_irq(DWAPBGPIOState *s)
{
    qemu_set_irq(s->irq, dw_apb_gpio_status(s) != 0);
}

static void dw_apb_gpio_update_pins(DWAPBGPIOState *s, bool detect_edges,
                                    bool force_outputs)
{
    uint32_t internal_driven = s->swporta_ddr & s->pin_mask;
    uint32_t internal_level = s->swporta_dr & internal_driven;
    uint32_t conflict;
    uint32_t old_level = s->pin_level;
    uint32_t changed;
    uint32_t rising;
    uint32_t falling;

    conflict = internal_driven & s->external_driven &
               (internal_level ^ s->external_level);
    if (conflict) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: GPIO pins have multiple drivers: 0x%08" PRIx32
                      "\n", DEVICE(s)->canonical_path, conflict);
    }

    s->pin_level = (s->external_level & s->external_driven) |
                   (internal_level & ~s->external_driven);
    s->pin_level &= s->pin_mask;
    changed = old_level ^ s->pin_level;

    if (detect_edges) {
        rising = changed & s->pin_level & s->int_polarity;
        falling = changed & ~s->pin_level & ~s->int_polarity;
        s->edge_pending |= (rising | falling) & s->inttype_level;
        s->edge_pending &= s->pin_mask;
    }

    if (changed || force_outputs) {
        for (unsigned int i = 0; i < s->ngpios; i++) {
            if (force_outputs || (changed & BIT(i))) {
                qemu_set_irq(s->output[i], !!(s->pin_level & BIT(i)));
            }
        }
    }

    dw_apb_gpio_update_irq(s);
}

static uint64_t dw_apb_gpio_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    DWAPBGPIOState *s = opaque;

    switch (offset) {
    case GPIO_SWPORTA_DR:
        return s->swporta_dr;
    case GPIO_SWPORTA_DDR:
        return s->swporta_ddr;
    case GPIO_SWPORTA_CTL:
        return s->swporta_ctl;
    case GPIO_INTEN:
        return s->inten;
    case GPIO_INTMASK:
        return s->intmask;
    case GPIO_INTTYPE_LEVEL:
        return s->inttype_level;
    case GPIO_INT_POLARITY:
        return s->int_polarity;
    case GPIO_INTSTATUS:
        return dw_apb_gpio_status(s);
    case GPIO_RAW_INTSTATUS:
        return dw_apb_gpio_raw_status(s);
    case GPIO_PORTA_DEBOUNCE:
        return s->debounce;
    case GPIO_PORTA_EOI:
        return 0;
    case GPIO_EXT_PORTA:
        return s->pin_level;
    case GPIO_LS_SYNC:
        return s->ls_sync;
    case GPIO_ID_CODE:
        return s->component_id;
    case GPIO_VER_ID_CODE:
        return s->component_version;
    case GPIO_CONFIG_REG2:
        return s->config_reg2;
    case GPIO_CONFIG_REG1:
        return s->config_reg1;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_DW_APB_GPIO, offset);
        return 0;
    }
}

static void dw_apb_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned int size)
{
    DWAPBGPIOState *s = opaque;
    uint32_t value32 = value;

    switch (offset) {
    case GPIO_SWPORTA_DR:
        s->swporta_dr = value32 & s->pin_mask;
        dw_apb_gpio_update_pins(s, true, false);
        break;
    case GPIO_SWPORTA_DDR:
        s->swporta_ddr = value32 & s->pin_mask;
        dw_apb_gpio_update_pins(s, true, false);
        break;
    case GPIO_SWPORTA_CTL:
        /* Hardware-function inputs are not present in this one-port model. */
        s->swporta_ctl = value32 & s->pin_mask;
        break;
    case GPIO_INTEN:
        s->inten = value32 & s->pin_mask;
        dw_apb_gpio_update_irq(s);
        break;
    case GPIO_INTMASK:
        s->intmask = value32 & s->pin_mask;
        dw_apb_gpio_update_irq(s);
        break;
    case GPIO_INTTYPE_LEVEL:
        s->inttype_level = value32 & s->pin_mask;
        s->edge_pending &= s->inttype_level;
        dw_apb_gpio_update_irq(s);
        break;
    case GPIO_INT_POLARITY:
        s->int_polarity = value32 & s->pin_mask;
        dw_apb_gpio_update_irq(s);
        break;
    case GPIO_PORTA_DEBOUNCE:
        /* Preserve the programming contract; filter timing is not modeled. */
        s->debounce = value32 & s->pin_mask;
        break;
    case GPIO_PORTA_EOI:
        s->edge_pending &= ~(value32 & s->pin_mask);
        dw_apb_gpio_update_irq(s);
        break;
    case GPIO_LS_SYNC:
        s->ls_sync = value32 & 1;
        break;
    case GPIO_INTSTATUS:
    case GPIO_RAW_INTSTATUS:
    case GPIO_EXT_PORTA:
    case GPIO_ID_CODE:
    case GPIO_VER_ID_CODE:
    case GPIO_CONFIG_REG2:
    case GPIO_CONFIG_REG1:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register at 0x%03"
                      HWADDR_PRIx "\n", TYPE_DW_APB_GPIO, offset);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_DW_APB_GPIO, offset);
        break;
    }
}

static bool dw_apb_gpio_access_valid(void *opaque, hwaddr offset,
                                     unsigned int size, bool is_write,
                                     MemTxAttrs attrs)
{
    return size == 4 && !(offset & 3);
}

static const MemoryRegionOps dw_apb_gpio_ops = {
    .read = dw_apb_gpio_read,
    .write = dw_apb_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = dw_apb_gpio_access_valid,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void dw_apb_gpio_set_input(void *opaque, int line, int level)
{
    DWAPBGPIOState *s = opaque;

    g_assert(line >= 0 && line < s->ngpios);

    s->external_driven = deposit32(s->external_driven, line, 1, level >= 0);
    if (level >= 0) {
        s->external_level = deposit32(s->external_level, line, 1, level > 0);
    }
    dw_apb_gpio_update_pins(s, true, false);
}

static void dw_apb_gpio_reset(DeviceState *dev)
{
    DWAPBGPIOState *s = DW_APB_GPIO(dev);

    s->swporta_dr = s->reset_data & s->pin_mask;
    s->swporta_ddr = s->reset_direction & s->pin_mask;
    s->swporta_ctl = 0;
    s->inten = 0;
    s->intmask = 0;
    s->inttype_level = 0;
    s->int_polarity = 0;
    s->edge_pending = 0;
    s->debounce = 0;
    s->ls_sync = 0;
    dw_apb_gpio_update_pins(s, false, true);
}

static int dw_apb_gpio_post_load(void *opaque, int version_id)
{
    DWAPBGPIOState *s = opaque;

    dw_apb_gpio_update_pins(s, false, true);
    return 0;
}

static const VMStateDescription vmstate_dw_apb_gpio = {
    .name = TYPE_DW_APB_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = dw_apb_gpio_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(swporta_dr, DWAPBGPIOState),
        VMSTATE_UINT32(swporta_ddr, DWAPBGPIOState),
        VMSTATE_UINT32(swporta_ctl, DWAPBGPIOState),
        VMSTATE_UINT32(inten, DWAPBGPIOState),
        VMSTATE_UINT32(intmask, DWAPBGPIOState),
        VMSTATE_UINT32(inttype_level, DWAPBGPIOState),
        VMSTATE_UINT32(int_polarity, DWAPBGPIOState),
        VMSTATE_UINT32(edge_pending, DWAPBGPIOState),
        VMSTATE_UINT32(debounce, DWAPBGPIOState),
        VMSTATE_UINT32(ls_sync, DWAPBGPIOState),
        VMSTATE_UINT32(external_level, DWAPBGPIOState),
        VMSTATE_UINT32(external_driven, DWAPBGPIOState),
        VMSTATE_END_OF_LIST(),
    },
};

static void dw_apb_gpio_realize(DeviceState *dev, Error **errp)
{
    DWAPBGPIOState *s = DW_APB_GPIO(dev);

    if (!s->ngpios || s->ngpios > DW_APB_GPIO_MAX_PINS) {
        error_setg(errp, "ngpios must be between 1 and %u",
                   DW_APB_GPIO_MAX_PINS);
        return;
    }

    s->pin_mask = s->ngpios == 32 ? UINT32_MAX : BIT(s->ngpios) - 1;
    qdev_init_gpio_in_named(dev, dw_apb_gpio_set_input, "gpio-in",
                            s->ngpios);
    qdev_init_gpio_out_named(dev, s->output, "gpio-out", s->ngpios);
}

static void dw_apb_gpio_init(Object *obj)
{
    DWAPBGPIOState *s = DW_APB_GPIO(obj);

    memory_region_init_io(&s->mmio, obj, &dw_apb_gpio_ops, s,
                          TYPE_DW_APB_GPIO, DW_APB_GPIO_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const Property dw_apb_gpio_properties[] = {
    DEFINE_PROP_UINT8("ngpios", DWAPBGPIOState, ngpios,
                      DW_APB_GPIO_MAX_PINS),
    DEFINE_PROP_UINT32("reset-data", DWAPBGPIOState, reset_data, 0),
    DEFINE_PROP_UINT32("reset-direction", DWAPBGPIOState,
                       reset_direction, 0),
    DEFINE_PROP_UINT32("component-id", DWAPBGPIOState, component_id, 0),
    DEFINE_PROP_UINT32("component-version", DWAPBGPIOState,
                       component_version, 0),
    DEFINE_PROP_UINT32("component-parameters-1", DWAPBGPIOState,
                       config_reg1, 0),
    DEFINE_PROP_UINT32("component-parameters-2", DWAPBGPIOState,
                       config_reg2, 0),
};

static void dw_apb_gpio_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "Synopsys DesignWare APB GPIO";
    dc->realize = dw_apb_gpio_realize;
    dc->vmsd = &vmstate_dw_apb_gpio;
    device_class_set_legacy_reset(dc, dw_apb_gpio_reset);
    device_class_set_props(dc, dw_apb_gpio_properties);
}

static const TypeInfo dw_apb_gpio_info = {
    .name = TYPE_DW_APB_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWAPBGPIOState),
    .instance_init = dw_apb_gpio_init,
    .class_init = dw_apb_gpio_class_init,
};

static void dw_apb_gpio_register_types(void)
{
    type_register_static(&dw_apb_gpio_info);
}

type_init(dw_apb_gpio_register_types)
