/*
 * T-Head TH1520 DDR control configuration register
 *
 * Public vendor U-Boot's LPDDR4 SPL writes staged reset-release values to
 * DDR_SYSREG.DDR_CFG0 after programming the PLL.  Its generated register
 * description supplies a zero reset value, writable bits 0, 1 and 4..31,
 * and reserved bits 2..3.  This maps only that exact word.  It records
 * firmware-visible state but deliberately has no power-good, reset, clock,
 * controller, PHY, DFI, or DRAM effect.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_ddr_control.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static uint64_t th1520_ddr_control_read(void *opaque, hwaddr offset,
                                        unsigned int size)
{
    TH1520DDRControlState *s = opaque;

    switch (offset) {
    case TH1520_DDR_CFG0_OFFSET:
        return s->cfg0;
    case TH1520_DDR_CFG1_OFFSET:
        return s->cfg1;
    default:
        g_assert_not_reached();
    }
}

static void th1520_ddr_control_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned int size)
{
    TH1520DDRControlState *s = opaque;

    switch (offset) {
    case TH1520_DDR_CFG0_OFFSET:
        s->cfg0 = value & TH1520_DDR_CFG0_WRITABLE_MASK;
        break;
    case TH1520_DDR_CFG1_OFFSET:
        s->cfg1 = value & TH1520_DDR_CFG1_WRITABLE_MASK;
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps th1520_ddr_control_ops = {
    .read = th1520_ddr_control_read,
    .write = th1520_ddr_control_write,
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

static void th1520_ddr_control_reset(DeviceState *dev)
{
    TH1520DDRControlState *s = TH1520_DDR_CONTROL(dev);

    s->cfg0 = TH1520_DDR_CFG0_RESET;
    s->cfg1 = TH1520_DDR_CFG1_RESET;
}

static int th1520_ddr_control_post_load(void *opaque, int version_id)
{
    TH1520DDRControlState *s = opaque;

    if (version_id < 2) {
        s->cfg1 = TH1520_DDR_CFG1_RESET;
    }

    return 0;
}

static const VMStateDescription vmstate_th1520_ddr_control = {
    .name = TYPE_TH1520_DDR_CONTROL,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = th1520_ddr_control_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cfg0, TH1520DDRControlState),
        VMSTATE_UINT32_V(cfg1, TH1520DDRControlState, 2),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_ddr_control_init(Object *obj)
{
    TH1520DDRControlState *s = TH1520_DDR_CONTROL(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_ddr_control_ops, s,
                          TYPE_TH1520_DDR_CONTROL,
                          TH1520_DDR_CONTROL_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void th1520_ddr_control_class_init(ObjectClass *klass,
                                          const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 DDR control configuration state (partial)";
    dc->user_creatable = false;
    dc->vmsd = &vmstate_th1520_ddr_control;
    device_class_set_legacy_reset(dc, th1520_ddr_control_reset);
}

static const TypeInfo th1520_ddr_control_info = {
    .name = TYPE_TH1520_DDR_CONTROL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520DDRControlState),
    .instance_init = th1520_ddr_control_init,
    .class_init = th1520_ddr_control_class_init,
};

static void th1520_ddr_control_register_types(void)
{
    type_register_static(&th1520_ddr_control_info);
}

type_init(th1520_ddr_control_register_types)
