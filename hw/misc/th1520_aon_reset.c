/*
 * T-Head TH1520 AON reset-generator AUDIO_RST_CFG register
 *
 * The TH1520 System User Manual describes the six low bits of AUDIO_RST_CFG
 * as active-low, read/write AUDIOSYS reset controls, reset to zero.  Vendor
 * SPL writes 0x37 to this exact word before clock and DDR setup.  Model only
 * the word: this is not an AON reset-generator aperture and does not infer
 * the reset effects of any audio device.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_aon_reset.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static uint64_t th1520_aon_reset_read(void *opaque, hwaddr offset,
                                      unsigned int size)
{
    TH1520AONResetState *s = opaque;

    return s->audio_rst_cfg;
}

static void th1520_aon_reset_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned int size)
{
    TH1520AONResetState *s = opaque;

    s->audio_rst_cfg = value & TH1520_AON_RESET_AUDIO_RST_CFG_MASK;
}

static const MemoryRegionOps th1520_aon_reset_ops = {
    .read = th1520_aon_reset_read,
    .write = th1520_aon_reset_write,
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

static void th1520_aon_reset_reset(DeviceState *dev)
{
    TH1520AONResetState *s = TH1520_AON_RESET(dev);

    s->audio_rst_cfg = TH1520_AON_RESET_AUDIO_RST_CFG_RESET;
}

static const VMStateDescription vmstate_th1520_aon_reset = {
    .name = TYPE_TH1520_AON_RESET,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(audio_rst_cfg, TH1520AONResetState),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_aon_reset_init(Object *obj)
{
    TH1520AONResetState *s = TH1520_AON_RESET(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_aon_reset_ops, s,
                          TYPE_TH1520_AON_RESET,
                          TH1520_AON_RESET_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void th1520_aon_reset_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 AON AUDIO_RST_CFG register";
    dc->vmsd = &vmstate_th1520_aon_reset;
    device_class_set_legacy_reset(dc, th1520_aon_reset_reset);
}

static const TypeInfo th1520_aon_reset_info = {
    .name = TYPE_TH1520_AON_RESET,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520AONResetState),
    .instance_init = th1520_aon_reset_init,
    .class_init = th1520_aon_reset_class_init,
};

static void th1520_aon_reset_register_types(void)
{
    type_register_static(&th1520_aon_reset_info);
}

type_init(th1520_aon_reset_register_types)
