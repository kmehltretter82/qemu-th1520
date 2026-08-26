/*
 * T-Head TH1520 DDR PLL configuration registers
 *
 * Public vendor U-Boot's LPDDR4 SPL configures the three words at
 * DDR_SYSREG_BADDR + 0x8, + 0xc, and + 0x18 before it begins DDR training.
 * This device deliberately maps only those words.  The generated source
 * supplies their reset values and writable fields, but it provides no
 * physical lock timing.  Releasing CFG1's reset bit therefore changes the
 * lock status immediately as a deterministic firmware compatibility
 * convention; this is not a model of the DDR PLL, clock tree, controller,
 * PHY, or DRAM training.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_ddr_pll.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static uint64_t th1520_ddr_pll_cfg0_read(void *opaque, hwaddr offset,
                                         unsigned int size)
{
    TH1520DDRPLLState *s = opaque;

    return s->pll_cfg0;
}

static void th1520_ddr_pll_cfg0_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned int size)
{
    TH1520DDRPLLState *s = opaque;

    s->pll_cfg0 = value & TH1520_DDR_PLL_CFG0_WRITABLE_MASK;
}

static uint64_t th1520_ddr_pll_cfg1_read(void *opaque, hwaddr offset,
                                         unsigned int size)
{
    TH1520DDRPLLState *s = opaque;

    return s->pll_cfg1;
}

static void th1520_ddr_pll_cfg1_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned int size)
{
    TH1520DDRPLLState *s = opaque;

    s->pll_cfg1 = value & TH1520_DDR_PLL_CFG1_WRITABLE_MASK;

    /* See the source-backed compatibility convention in the file header. */
    s->pll_lock = !(s->pll_cfg1 & TH1520_DDR_PLL_CFG1_RESET_BIT);
}

static uint64_t th1520_ddr_pll_sts_read(void *opaque, hwaddr offset,
                                        unsigned int size)
{
    TH1520DDRPLLState *s = opaque;

    return (s->pll_lock ? TH1520_DDR_PLL_STS_LOCK : 0) |
           (s->core_clock_cg ? TH1520_DDR_PLL_STS_CORE_CLOCK_CG : 0);
}

static void th1520_ddr_pll_sts_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned int size)
{
    TH1520DDRPLLState *s = opaque;

    s->core_clock_cg = value & TH1520_DDR_PLL_STS_CORE_CLOCK_CG;
}

static const MemoryRegionOps th1520_ddr_pll_cfg0_ops = {
    .read = th1520_ddr_pll_cfg0_read,
    .write = th1520_ddr_pll_cfg0_write,
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

static const MemoryRegionOps th1520_ddr_pll_cfg1_ops = {
    .read = th1520_ddr_pll_cfg1_read,
    .write = th1520_ddr_pll_cfg1_write,
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

static const MemoryRegionOps th1520_ddr_pll_sts_ops = {
    .read = th1520_ddr_pll_sts_read,
    .write = th1520_ddr_pll_sts_write,
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

static void th1520_ddr_pll_reset(DeviceState *dev)
{
    TH1520DDRPLLState *s = TH1520_DDR_PLL(dev);

    s->pll_cfg0 = TH1520_DDR_PLL_CFG0_RESET;
    s->pll_cfg1 = TH1520_DDR_PLL_CFG1_RESET;
    s->pll_lock = false;
    s->core_clock_cg = false;
}

static const VMStateDescription vmstate_th1520_ddr_pll = {
    .name = TYPE_TH1520_DDR_PLL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(pll_cfg0, TH1520DDRPLLState),
        VMSTATE_UINT32(pll_cfg1, TH1520DDRPLLState),
        VMSTATE_BOOL(pll_lock, TH1520DDRPLLState),
        VMSTATE_BOOL(core_clock_cg, TH1520DDRPLLState),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_ddr_pll_init(Object *obj)
{
    TH1520DDRPLLState *s = TH1520_DDR_PLL(obj);

    memory_region_init_io(&s->cfg0_iomem, obj, &th1520_ddr_pll_cfg0_ops, s,
                          TYPE_TH1520_DDR_PLL ".cfg0",
                          TH1520_DDR_PLL_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->cfg0_iomem);
    memory_region_init_io(&s->cfg1_iomem, obj, &th1520_ddr_pll_cfg1_ops, s,
                          TYPE_TH1520_DDR_PLL ".cfg1",
                          TH1520_DDR_PLL_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->cfg1_iomem);
    memory_region_init_io(&s->sts_iomem, obj, &th1520_ddr_pll_sts_ops, s,
                          TYPE_TH1520_DDR_PLL ".sts",
                          TH1520_DDR_PLL_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->sts_iomem);
}

static void th1520_ddr_pll_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 DDR PLL configuration state (partial)";
    dc->user_creatable = false;
    dc->vmsd = &vmstate_th1520_ddr_pll;
    device_class_set_legacy_reset(dc, th1520_ddr_pll_reset);
}

static const TypeInfo th1520_ddr_pll_info = {
    .name = TYPE_TH1520_DDR_PLL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520DDRPLLState),
    .instance_init = th1520_ddr_pll_init,
    .class_init = th1520_ddr_pll_class_init,
};

static void th1520_ddr_pll_register_types(void)
{
    type_register_static(&th1520_ddr_pll_info);
}

type_init(th1520_ddr_pll_register_types)
