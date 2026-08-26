/*
 * T-Head TH1520 TEE VOSYS DPU_RST_CFG_TEE register
 *
 * The TH1520 System User Manual describes the three low bits of
 * DPU_RST_CFG_TEE as active-low, read/write DPU reset controls, reset to
 * zero. Vendor SPL writes 0x7 to this exact word while configuring VOSYS.
 * Model only the word: this is not a TEE VOSYS aperture and does not infer
 * reset effects, clocks, IOPMP behavior, or DPU devices.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_tee_vosys_dpu_reset.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static uint64_t th1520_tee_vosys_dpu_reset_read(void *opaque, hwaddr offset,
                                                 unsigned int size)
{
    TH1520TEEVOSYSDPUResetState *s = opaque;

    return s->dpu_rst_cfg;
}

static void th1520_tee_vosys_dpu_reset_write(void *opaque, hwaddr offset,
                                              uint64_t value,
                                              unsigned int size)
{
    TH1520TEEVOSYSDPUResetState *s = opaque;

    s->dpu_rst_cfg = value & TH1520_TEE_VOSYS_DPU_RESET_MASK;
}

static const MemoryRegionOps th1520_tee_vosys_dpu_reset_ops = {
    .read = th1520_tee_vosys_dpu_reset_read,
    .write = th1520_tee_vosys_dpu_reset_write,
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

static void th1520_tee_vosys_dpu_reset_reset(DeviceState *dev)
{
    TH1520TEEVOSYSDPUResetState *s = TH1520_TEE_VOSYS_DPU_RESET(dev);

    s->dpu_rst_cfg = TH1520_TEE_VOSYS_DPU_RESET_VALUE;
}

static const VMStateDescription vmstate_th1520_tee_vosys_dpu_reset = {
    .name = TYPE_TH1520_TEE_VOSYS_DPU_RESET,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(dpu_rst_cfg, TH1520TEEVOSYSDPUResetState),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_tee_vosys_dpu_reset_init(Object *obj)
{
    TH1520TEEVOSYSDPUResetState *s = TH1520_TEE_VOSYS_DPU_RESET(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_tee_vosys_dpu_reset_ops, s,
                          TYPE_TH1520_TEE_VOSYS_DPU_RESET,
                          TH1520_TEE_VOSYS_DPU_RESET_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void th1520_tee_vosys_dpu_reset_class_init(ObjectClass *klass,
                                                   const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 TEE VOSYS DPU reset register";
    dc->vmsd = &vmstate_th1520_tee_vosys_dpu_reset;
    device_class_set_legacy_reset(dc, th1520_tee_vosys_dpu_reset_reset);
}

static const TypeInfo th1520_tee_vosys_dpu_reset_info = {
    .name = TYPE_TH1520_TEE_VOSYS_DPU_RESET,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520TEEVOSYSDPUResetState),
    .instance_init = th1520_tee_vosys_dpu_reset_init,
    .class_init = th1520_tee_vosys_dpu_reset_class_init,
};

static void th1520_tee_vosys_dpu_reset_register_types(void)
{
    type_register_static(&th1520_tee_vosys_dpu_reset_info);
}

type_init(th1520_tee_vosys_dpu_reset_register_types)
