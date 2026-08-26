/*
 * T-Head TH1520 ISO7816 compatibility configuration register
 *
 * Public vendor U-Boot clears CONFIG.MIE at ISO7816 base + 0x10 before
 * configuring PWM.  Vendor Linux independently names that field as the
 * global interrupt enable.  Map only that exact word: this is not a model of
 * the disabled 16 KiB ISO7816 controller, its card interface, or its IRQ.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_iso7816.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static uint64_t th1520_iso7816_config_read(void *opaque, hwaddr offset,
                                           unsigned int size)
{
    TH1520ISO7816ConfigState *s = opaque;

    return s->config;
}

static void th1520_iso7816_config_write(void *opaque, hwaddr offset,
                                        uint64_t value, unsigned int size)
{
    TH1520ISO7816ConfigState *s = opaque;

    s->config = value & TH1520_ISO7816_CONFIG_MIE;
}

static const MemoryRegionOps th1520_iso7816_config_ops = {
    .read = th1520_iso7816_config_read,
    .write = th1520_iso7816_config_write,
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

static void th1520_iso7816_config_reset(DeviceState *dev)
{
    TH1520ISO7816ConfigState *s = TH1520_ISO7816_CONFIG(dev);

    /* Hardware reset value is not public; zero is the QEMU convention. */
    s->config = 0;
}

static const VMStateDescription vmstate_th1520_iso7816_config = {
    .name = TYPE_TH1520_ISO7816_CONFIG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(config, TH1520ISO7816ConfigState),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_iso7816_config_init(Object *obj)
{
    TH1520ISO7816ConfigState *s = TH1520_ISO7816_CONFIG(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_iso7816_config_ops, s,
                          TYPE_TH1520_ISO7816_CONFIG,
                          TH1520_ISO7816_CONFIG_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void th1520_iso7816_config_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 ISO7816 CONFIG register";
    dc->vmsd = &vmstate_th1520_iso7816_config;
    device_class_set_legacy_reset(dc, th1520_iso7816_config_reset);
}

static const TypeInfo th1520_iso7816_config_info = {
    .name = TYPE_TH1520_ISO7816_CONFIG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520ISO7816ConfigState),
    .instance_init = th1520_iso7816_config_init,
    .class_init = th1520_iso7816_config_class_init,
};

static void th1520_iso7816_config_register_types(void)
{
    type_register_static(&th1520_iso7816_config_info);
}

type_init(th1520_iso7816_config_register_types)
