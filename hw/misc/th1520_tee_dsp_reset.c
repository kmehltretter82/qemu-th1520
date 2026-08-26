/*
 * T-Head TH1520 TEE DSP-system DSPSYS_SW_RST register
 *
 * The TH1520 System User Manual describes the documented bits of
 * DSPSYS_SW_RST_TEE as active-low, read/write reset controls, with reset
 * value 0x7d11000f.  Vendor SPL writes all ones to this exact word while
 * configuring the DSP subsystem.  Model only the word: this is not a DSP
 * system-register aperture and does not infer reset effects, clocks, IOPMP
 * behavior, or DSP cores.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_tee_dsp_reset.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static uint64_t th1520_tee_dsp_reset_read(void *opaque, hwaddr offset,
                                           unsigned int size)
{
    TH1520TEEDSPResetState *s = opaque;

    return s->sw_rst;
}

static void th1520_tee_dsp_reset_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned int size)
{
    TH1520TEEDSPResetState *s = opaque;

    s->sw_rst = value & TH1520_TEE_DSP_RESET_SW_RST_MASK;
}

static const MemoryRegionOps th1520_tee_dsp_reset_ops = {
    .read = th1520_tee_dsp_reset_read,
    .write = th1520_tee_dsp_reset_write,
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

static void th1520_tee_dsp_reset_reset(DeviceState *dev)
{
    TH1520TEEDSPResetState *s = TH1520_TEE_DSP_RESET(dev);

    s->sw_rst = TH1520_TEE_DSP_RESET_SW_RST_RESET;
}

static const VMStateDescription vmstate_th1520_tee_dsp_reset = {
    .name = TYPE_TH1520_TEE_DSP_RESET,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(sw_rst, TH1520TEEDSPResetState),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_tee_dsp_reset_init(Object *obj)
{
    TH1520TEEDSPResetState *s = TH1520_TEE_DSP_RESET(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_tee_dsp_reset_ops, s,
                          TYPE_TH1520_TEE_DSP_RESET,
                          TH1520_TEE_DSP_RESET_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void th1520_tee_dsp_reset_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 TEE DSP-system reset register";
    dc->vmsd = &vmstate_th1520_tee_dsp_reset;
    device_class_set_legacy_reset(dc, th1520_tee_dsp_reset_reset);
}

static const TypeInfo th1520_tee_dsp_reset_info = {
    .name = TYPE_TH1520_TEE_DSP_RESET,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520TEEDSPResetState),
    .instance_init = th1520_tee_dsp_reset_init,
    .class_init = th1520_tee_dsp_reset_class_init,
};

static void th1520_tee_dsp_reset_register_types(void)
{
    type_register_static(&th1520_tee_dsp_reset_info);
}

type_init(th1520_tee_dsp_reset_register_types)
