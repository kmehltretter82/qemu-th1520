/*
 * T-Head TH1520 TEE miscellaneous-system clock-control register
 *
 * The TH1520 System User Manual describes the 11 low bits of
 * MISCSYS_TEE_CLK_CTRL_TEE as independent read/write clock gates, reset to
 * one. Vendor U-Boot clears the TEE DMA gate at this exact address during
 * board_late_init(). Model only the word: it is not a TEE aperture, DMA
 * controller, or functional clock tree.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_tee_miscsys_clock.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static uint64_t th1520_tee_miscsys_clock_read(void *opaque, hwaddr offset,
                                               unsigned int size)
{
    TH1520TEEMiscSysClockState *s = opaque;

    return s->clock_ctrl;
}

static void th1520_tee_miscsys_clock_write(void *opaque, hwaddr offset,
                                           uint64_t value, unsigned int size)
{
    TH1520TEEMiscSysClockState *s = opaque;

    s->clock_ctrl = value & TH1520_TEE_MISCSYS_CLOCK_ENABLE_MASK;
}

static const MemoryRegionOps th1520_tee_miscsys_clock_ops = {
    .read = th1520_tee_miscsys_clock_read,
    .write = th1520_tee_miscsys_clock_write,
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

static void th1520_tee_miscsys_clock_reset(DeviceState *dev)
{
    TH1520TEEMiscSysClockState *s = TH1520_TEE_MISCSYS_CLOCK(dev);

    s->clock_ctrl = TH1520_TEE_MISCSYS_CLOCK_RESET;
}

static const VMStateDescription vmstate_th1520_tee_miscsys_clock = {
    .name = TYPE_TH1520_TEE_MISCSYS_CLOCK,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(clock_ctrl, TH1520TEEMiscSysClockState),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_tee_miscsys_clock_init(Object *obj)
{
    TH1520TEEMiscSysClockState *s = TH1520_TEE_MISCSYS_CLOCK(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_tee_miscsys_clock_ops, s,
                          TYPE_TH1520_TEE_MISCSYS_CLOCK,
                          TH1520_TEE_MISCSYS_CLOCK_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void th1520_tee_miscsys_clock_class_init(ObjectClass *klass,
                                                 const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 TEE miscellaneous-system clock-control register";
    dc->vmsd = &vmstate_th1520_tee_miscsys_clock;
    device_class_set_legacy_reset(dc, th1520_tee_miscsys_clock_reset);
}

static const TypeInfo th1520_tee_miscsys_clock_info = {
    .name = TYPE_TH1520_TEE_MISCSYS_CLOCK,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520TEEMiscSysClockState),
    .instance_init = th1520_tee_miscsys_clock_init,
    .class_init = th1520_tee_miscsys_clock_class_init,
};

static void th1520_tee_miscsys_clock_register_types(void)
{
    type_register_static(&th1520_tee_miscsys_clock_info);
}

type_init(th1520_tee_miscsys_clock_register_types)
