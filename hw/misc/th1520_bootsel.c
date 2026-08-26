/*
 * T-Head TH1520 BOOT_SEL register
 *
 * The public TH1520 System User Manual describes BOOT_SEL at system-control
 * offset 0x10. Bits 3:0 reflect the boot straps and are read-only; bit 4 is
 * a write-one-to-set lock which is already set after reset. Model only this
 * exact word, not the surrounding system-control register block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/misc/th1520_bootsel.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static uint64_t th1520_bootsel_read(void *opaque, hwaddr offset,
                                    unsigned int size)
{
    TH1520BootSelState *s = opaque;

    return (s->boot_sel & TH1520_BOOTSEL_SELECT_MASK) |
           TH1520_BOOTSEL_UPDATE;
}

static void th1520_bootsel_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned int size)
{
    /*
     * BOOT_SEL[3:0] is read-only. BOOT_SEL_UPDT is reset-set and W1S, so any
     * write leaves the only modeled post-reset state unchanged.
     */
}

static const MemoryRegionOps th1520_bootsel_ops = {
    .read = th1520_bootsel_read,
    .write = th1520_bootsel_write,
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

static const VMStateDescription vmstate_th1520_bootsel = {
    .name = TYPE_TH1520_BOOTSEL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(boot_sel, TH1520BootSelState),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property th1520_bootsel_properties[] = {
    DEFINE_PROP_UINT8("boot-sel", TH1520BootSelState, boot_sel,
                      TH1520_BOOTSEL_EMMC),
};

static void th1520_bootsel_init(Object *obj)
{
    TH1520BootSelState *s = TH1520_BOOTSEL(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_bootsel_ops, s,
                          TYPE_TH1520_BOOTSEL,
                          TH1520_BOOTSEL_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void th1520_bootsel_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 BOOT_SEL register";
    dc->vmsd = &vmstate_th1520_bootsel;
    device_class_set_props(dc, th1520_bootsel_properties);
}

static const TypeInfo th1520_bootsel_info = {
    .name = TYPE_TH1520_BOOTSEL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520BootSelState),
    .instance_init = th1520_bootsel_init,
    .class_init = th1520_bootsel_class_init,
};

static void th1520_bootsel_register_types(void)
{
    type_register_static(&th1520_bootsel_info);
}

type_init(th1520_bootsel_register_types)
