/*
 * T-Head TH1520 miscellaneous subsystem clock/reset registers
 *
 * This implements the public REE register aperture used by the eMMC, SDIO
 * and USB clock/reset drivers.  Clock bits are software-visible state only;
 * they do not yet stop child devices or alter virtual transfer timing.
 * USB reset bits are exported independently so that their physical effects
 * can be refined after board measurements without changing this ABI.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_miscsys.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TH1520_MISCSYS_USB_SWRST 0x014

typedef struct TH1520MiscSysRegInfo {
    uint16_t offset;
    uint32_t reset;
    uint32_t write_mask;
} TH1520MiscSysRegInfo;

static const TH1520MiscSysRegInfo th1520_miscsys_reginfo[] = {
    { 0x000, 0x00000003, 0x00000003 }, /* EMMC_SWRST */
    { 0x008, 0x00000003, 0x00000003 }, /* MISCSYS_AXI_SWRST */
    { 0x00c, 0x00000001, 0x00000001 }, /* SDIO0_SWRST */
    { 0x010, 0x00000001, 0x00000001 }, /* SDIO1_SWRST */
    { 0x014, 0x00000001, 0x00000007 }, /* USB3_DRD_SWRST */
    { 0x100, 0x00000001, 0x00000001 }, /* MISCSYS_BUS_CLK_CTRL */
    { 0x104, 0x0000000f, 0x0000000f }, /* MISCSYS_USB_CLK_CTRL */
    { 0x108, 0x00000001, 0x00000001 }, /* MISCSYS_EMMC_CLK_CTRL */
    { 0x10c, 0x00000001, 0x00000001 }, /* MISCSYS_SDIO0_CLK_CTRL */
    { 0x110, 0x00000001, 0x00000001 }, /* MISCSYS_SDIO1_CLK_CTRL */
};

static const TH1520MiscSysRegInfo *
th1520_miscsys_reginfo_find(hwaddr offset)
{
    for (size_t i = 0; i < ARRAY_SIZE(th1520_miscsys_reginfo); i++) {
        if (th1520_miscsys_reginfo[i].offset == offset) {
            return &th1520_miscsys_reginfo[i];
        }
    }

    return NULL;
}

static void th1520_miscsys_update_usb_reset(TH1520MiscSysState *s,
                                             unsigned int output,
                                             bool force)
{
    bool asserted = !(s->regs[TH1520_MISCSYS_USB_SWRST / 4] & BIT(output));

    if (force || s->usb_reset_asserted[output] != asserted) {
        s->usb_reset_asserted[output] = asserted;
        qemu_set_irq(s->usb_reset[output], asserted);
    }
}

static void th1520_miscsys_update_usb_resets(TH1520MiscSysState *s,
                                              bool force)
{
    for (unsigned int i = 0; i < TH1520_MISCSYS_USB_RESET_COUNT; i++) {
        th1520_miscsys_update_usb_reset(s, i, force);
    }
}

static uint64_t th1520_miscsys_read(void *opaque, hwaddr offset,
                                    unsigned int size)
{
    TH1520MiscSysState *s = opaque;

    if (!th1520_miscsys_reginfo_find(offset)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_MISCSYS, offset);
        return 0;
    }

    return s->regs[offset / 4];
}

static void th1520_miscsys_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned int size)
{
    TH1520MiscSysState *s = opaque;
    const TH1520MiscSysRegInfo *info = th1520_miscsys_reginfo_find(offset);
    uint32_t old;

    if (!info) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_MISCSYS, offset);
        return;
    }

    old = s->regs[offset / 4];
    s->regs[offset / 4] = (old & ~info->write_mask) |
                          ((uint32_t)value & info->write_mask);
    if (offset == TH1520_MISCSYS_USB_SWRST &&
        old != s->regs[offset / 4]) {
        th1520_miscsys_update_usb_resets(s, false);
    }
}

static const MemoryRegionOps th1520_miscsys_ops = {
    .read = th1520_miscsys_read,
    .write = th1520_miscsys_write,
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

static void th1520_miscsys_reset(DeviceState *dev)
{
    TH1520MiscSysState *s = TH1520_MISCSYS(dev);

    memset(s->regs, 0, sizeof(s->regs));
    for (size_t i = 0; i < ARRAY_SIZE(th1520_miscsys_reginfo); i++) {
        const TH1520MiscSysRegInfo *info = &th1520_miscsys_reginfo[i];

        s->regs[info->offset / 4] = info->reset;
    }
    th1520_miscsys_update_usb_resets(s, true);
}

static int th1520_miscsys_post_load(void *opaque, int version_id)
{
    TH1520MiscSysState *s = opaque;

    th1520_miscsys_update_usb_resets(s, true);
    return 0;
}

static const VMStateDescription vmstate_th1520_miscsys = {
    .name = TYPE_TH1520_MISCSYS,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = th1520_miscsys_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, TH1520MiscSysState,
                             TH1520_MISCSYS_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_miscsys_init(Object *obj)
{
    TH1520MiscSysState *s = TH1520_MISCSYS(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_miscsys_ops, s,
                          TYPE_TH1520_MISCSYS,
                          TH1520_MISCSYS_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    qdev_init_gpio_out_named(DEVICE(s), s->usb_reset, "usb-reset",
                             TH1520_MISCSYS_USB_RESET_COUNT);
}

static void th1520_miscsys_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 miscellaneous clock/reset controller";
    dc->vmsd = &vmstate_th1520_miscsys;
    device_class_set_legacy_reset(dc, th1520_miscsys_reset);
}

static const TypeInfo th1520_miscsys_info = {
    .name = TYPE_TH1520_MISCSYS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520MiscSysState),
    .instance_init = th1520_miscsys_init,
    .class_init = th1520_miscsys_class_init,
};

static void th1520_miscsys_register_types(void)
{
    type_register_static(&th1520_miscsys_info);
}

type_init(th1520_miscsys_register_types)
