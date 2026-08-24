/*
 * T-Head TH1520 USB3 DRD wrapper
 *
 * The wrapper contains one USB2/USB3 physical connector backed by QEMU's
 * host-only DWC3/xHCI model.  The documented digital control and PHY tuning
 * registers are retained exactly, while analog PHY effects, port-disable
 * overrides and device/OTG operation remain explicit validation work.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/usb/th1520_usb.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TH1520_USB_HOST_CTRL 0x44
#define TH1520_USB_HOST_TOPOLOGY_MASK 0x0000ff60
#define TH1520_USB_HOST_TOPOLOGY_RESET 0x00001100

typedef struct TH1520USBRegInfo {
    uint32_t reset;
    uint32_t write_mask;
} TH1520USBRegInfo;

static const TH1520USBRegInfo th1520_usb_reginfo[TH1520_USB_DRD_REGS] = {
    [0x00 / 4] = { 0x00000000, 0x00000000 }, /* USB_CLK_GATE_STS */
    [0x04 / 4] = { 0x00000000, 0x00000000 }, /* TRACE_STS0 */
    [0x08 / 4] = { 0x00000000, 0x00000000 }, /* TRACE_STS1 */
    [0x0c / 4] = { 0x00000000, 0x0000ffff }, /* USB_GPIO */
    [0x10 / 4] = { 0x00000040, 0x00000000 }, /* USB_DEBUG_STS0 */
    [0x14 / 4] = { 0x0003c400, 0x00000000 }, /* USB_DEBUG_STS1 */
    [0x18 / 4] = { 0x00000000, 0x00000000 }, /* USB_DEBUG_STS2 */
    [0x1c / 4] = { 0x00000020, 0x0000003f }, /* USBCTL_CLK_CTRL0 */
    [0x20 / 4] = { 0x00002a00, 0x1ff7ff7f }, /* USBPHY_CLK_CTRL1 */
    [0x24 / 4] = { 0x00095182, 0x1f1f77f3 }, /* USBPHY_TEST_CTRL0 */
    [0x28 / 4] = { 0x10303344, 0x3331f777 }, /* USBPHY_TEST_CTRL1 */
    [0x2c / 4] = { 0x01c1c0f0, 0x03f3f3ff }, /* USBPHY_TEST_CTRL2 */
    [0x30 / 4] = { 0x0000047f, 0x00000f7f }, /* USBPHY_TEST_CTRL3 */
    [0x34 / 4] = { 0x00000000, 0x00000001 }, /* REF_SSP_EN */
    [0x38 / 4] = { 0x00000000, 0x0000000f }, /* USB_HADDR_SEL */
    [0x3c / 4] = { 0x00000000, 0x000001ff }, /* USB_SYS */
    [0x40 / 4] = { 0x00000000, 0x00000000 }, /* USB_HOST_STATUS */
    [0x44 / 4] = { 0x00001101, 0x0333ff7f }, /* USB_HOST_CTRL */
    [0x48 / 4] = { 0x00000018, 0x0000003f }, /* USBPHY_HOST_CTRL */
    [0x4c / 4] = { 0x00000000, 0x00000000 }, /* USBPHY_HOST_STATUS */
    [0x50 / 4] = { 0x00000000, 0xffffffff }, /* USB_TEST_REG0 */
    [0x54 / 4] = { 0x00000000, 0xffffffff }, /* USB_TEST_REG1 */
    [0x58 / 4] = { 0xffffffff, 0xffffffff }, /* USB_TEST_REG2 */
    [0x5c / 4] = { 0xffffffff, 0xffffffff }, /* USB_TEST_REG3 */
};

static uint64_t th1520_usb_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    TH1520USBState *s = opaque;

    if (offset >= sizeof(s->drd_regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from reserved offset 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_USB, offset);
        return 0;
    }

    return s->drd_regs[offset / 4];
}

static void th1520_usb_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned int size)
{
    TH1520USBState *s = opaque;
    const TH1520USBRegInfo *info;
    uint32_t old;

    if (offset >= sizeof(s->drd_regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to reserved offset 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_USB, offset);
        return;
    }

    info = &th1520_usb_reginfo[offset / 4];
    if (!info->write_mask) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only offset 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_USB, offset);
        return;
    }

    old = s->drd_regs[offset / 4];
    s->drd_regs[offset / 4] = (old & ~info->write_mask) |
                              ((uint32_t)value & info->write_mask);

    if (offset == TH1520_USB_HOST_CTRL &&
        ((old ^ s->drd_regs[offset / 4]) &
         TH1520_USB_HOST_TOPOLOGY_MASK) &&
        (s->drd_regs[offset / 4] & TH1520_USB_HOST_TOPOLOGY_MASK) !=
        TH1520_USB_HOST_TOPOLOGY_RESET) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: dynamic xHCI port topology/disable override is "
                      "not implemented\n", TYPE_TH1520_USB);
    }
}

static const MemoryRegionOps th1520_usb_ops = {
    .read = th1520_usb_read,
    .write = th1520_usb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void th1520_usb_reset_input(void *opaque, int n, int level)
{
    TH1520USBState *s = opaque;
    bool old = s->reset_asserted[n];

    s->reset_asserted[n] = level;
    if (level && !old) {
        /*
         * The three active-low inputs reset distinct silicon domains.  The
         * reusable QEMU core does not expose those boundaries, so assertion
         * of any input conservatively resets the complete digital core.
         */
        device_cold_reset(DEVICE(&s->dwc3));
    }
}

static void th1520_usb_reset(DeviceState *dev)
{
    TH1520USBState *s = TH1520_USB(dev);

    for (size_t i = 0; i < ARRAY_SIZE(th1520_usb_reginfo); i++) {
        s->drd_regs[i] = th1520_usb_reginfo[i].reset;
    }
    memset(s->reset_asserted, 0, sizeof(s->reset_asserted));
}

static const VMStateDescription vmstate_th1520_usb = {
    .name = TYPE_TH1520_USB,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(drd_regs, TH1520USBState,
                             TH1520_USB_DRD_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_usb_realize(DeviceState *dev, Error **errp)
{
    TH1520USBState *s = TH1520_USB(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dwc3), errp)) {
        return;
    }

    sysbus_init_mmio(sbd, &s->drd_iomem);
    sysbus_init_mmio(sbd, &s->dwc3_alias);
    qdev_pass_gpios(DEVICE(&s->dwc3.sysbus_xhci), dev,
                    SYSBUS_DEVICE_GPIO_IRQ);
}

static void th1520_usb_init(Object *obj)
{
    TH1520USBState *s = TH1520_USB(obj);

    object_initialize_child(obj, "dwc3", &s->dwc3, TYPE_USB_DWC3);
    qdev_prop_set_uint32(DEVICE(&s->dwc3.sysbus_xhci), "p2", 1);
    qdev_prop_set_uint32(DEVICE(&s->dwc3.sysbus_xhci), "p3", 1);
    qdev_prop_set_uint32(DEVICE(&s->dwc3.sysbus_xhci), "intrs", 1);
    qdev_prop_set_uint32(DEVICE(&s->dwc3.sysbus_xhci), "slots", 2);

    memory_region_init_io(&s->drd_iomem, obj, &th1520_usb_ops, s,
                          TYPE_TH1520_USB ".drd",
                          TH1520_USB_DRD_MMIO_SIZE);
    memory_region_init_alias(&s->dwc3_alias, obj, TYPE_TH1520_USB ".dwc3",
                             &s->dwc3.iomem, 0, DWC3_SIZE);

    qdev_alias_all_properties(DEVICE(&s->dwc3), obj);
    qdev_alias_all_properties(DEVICE(&s->dwc3.sysbus_xhci), obj);
    object_property_add_alias(obj, "dma", OBJECT(&s->dwc3.sysbus_xhci),
                              "dma");
    qdev_init_gpio_in_named(DEVICE(s), th1520_usb_reset_input, "reset", 3);
}

static void th1520_usb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 USB3 DRD wrapper";
    dc->realize = th1520_usb_realize;
    dc->vmsd = &vmstate_th1520_usb;
    device_class_set_legacy_reset(dc, th1520_usb_reset);
}

static const TypeInfo th1520_usb_info = {
    .name = TYPE_TH1520_USB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520USBState),
    .instance_init = th1520_usb_init,
    .class_init = th1520_usb_class_init,
};

static void th1520_usb_register_types(void)
{
    type_register_static(&th1520_usb_info);
}

type_init(th1520_usb_register_types)
