/*
 * T-Head TH1520 video/vision system-register apertures
 *
 * Public vendor U-Boot configures the four VISYS dividers and four VOSYS
 * registers below before it attempts any video initialization. Vendor Linux
 * independently describes both 4 KiB syscon apertures and the VOSYS gate
 * bits.  This models only that software-visible configuration state.  It
 * deliberately does not create clock outputs, alter video devices, or claim
 * physical reset values or gate effects.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_video_sysreg.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

typedef struct TH1520VideoSysRegInfo {
    hwaddr offset;
    unsigned int index;
    uint32_t writable_mask;
} TH1520VideoSysRegInfo;

static const TH1520VideoSysRegInfo th1520_visys_regs[] = {
    { TH1520_VISYS_ISP0_CLK_CFG,       0, TH1520_VISYS_CLK_DIV_MASK },
    { TH1520_VISYS_ISP1_CLK_CFG,       1, TH1520_VISYS_CLK_DIV_MASK },
    { TH1520_VISYS_ISP_RY_CLK_CFG,     2, TH1520_VISYS_CLK_DIV_MASK },
    { TH1520_VISYS_MIPI_CSI0_PIXELCLK, 3, TH1520_VISYS_CLK_DIV_MASK },
};

static const TH1520VideoSysRegInfo th1520_vosys_regs[] = {
    { TH1520_VOSYS_GPU_RST_CFG,  3, TH1520_VOSYS_GPU_RST_CFG_MASK },
    { TH1520_VOSYS_CLK_GATE,     0, TH1520_VOSYS_CLK_GATE_MASK },
    { TH1520_VOSYS_CLK_GATE1,    1, TH1520_VOSYS_CLK_GATE1_MASK },
    { TH1520_VOSYS_DPU_CCLK_CFG, 2, TH1520_VOSYS_DPU_CCLK_MASK },
};

static const TH1520VideoSysRegInfo *
th1520_video_sysreg_find(const TH1520VideoSysRegState *s, hwaddr offset)
{
    const TH1520VideoSysRegInfo *regs;
    size_t count;

    if (s->vosys) {
        regs = th1520_vosys_regs;
        count = ARRAY_SIZE(th1520_vosys_regs);
    } else {
        regs = th1520_visys_regs;
        count = ARRAY_SIZE(th1520_visys_regs);
    }

    for (size_t i = 0; i < count; i++) {
        if (regs[i].offset == offset) {
            return &regs[i];
        }
    }

    return NULL;
}

static uint64_t th1520_video_sysreg_read(void *opaque, hwaddr offset,
                                          unsigned int size)
{
    TH1520VideoSysRegState *s = opaque;
    const TH1520VideoSysRegInfo *reg = th1520_video_sysreg_find(s, offset);

    if (reg) {
        return s->regs[reg->index];
    }

    qemu_log_mask(LOG_UNIMP,
                  "%s: unimplemented %s register at 0x%03" HWADDR_PRIx
                  "\n", TYPE_TH1520_VIDEO_SYSREG,
                  s->vosys ? "VOSYS" : "VISYS", offset);
    return 0;
}

static void th1520_video_sysreg_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned int size)
{
    TH1520VideoSysRegState *s = opaque;
    const TH1520VideoSysRegInfo *reg = th1520_video_sysreg_find(s, offset);

    if (reg) {
        s->regs[reg->index] = value & reg->writable_mask;
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "%s: unimplemented %s register at 0x%03" HWADDR_PRIx
                  "\n", TYPE_TH1520_VIDEO_SYSREG,
                  s->vosys ? "VOSYS" : "VISYS", offset);
}

static const MemoryRegionOps th1520_video_sysreg_ops = {
    .read = th1520_video_sysreg_read,
    .write = th1520_video_sysreg_write,
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

static void th1520_video_sysreg_reset(DeviceState *dev)
{
    TH1520VideoSysRegState *s = TH1520_VIDEO_SYSREG(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static const VMStateDescription vmstate_th1520_video_sysreg = {
    .name = TYPE_TH1520_VIDEO_SYSREG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, TH1520VideoSysRegState,
                             TH1520_VIDEO_SYSREG_REG_COUNT),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_video_sysreg_init(Object *obj)
{
    TH1520VideoSysRegState *s = TH1520_VIDEO_SYSREG(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_video_sysreg_ops, s,
                          TYPE_TH1520_VIDEO_SYSREG,
                          TH1520_VIDEO_SYSREG_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static const Property th1520_video_sysreg_props[] = {
    DEFINE_PROP_BOOL("vosys", TH1520VideoSysRegState, vosys, false),
};

static void th1520_video_sysreg_class_init(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 video/vision system registers";
    dc->vmsd = &vmstate_th1520_video_sysreg;
    device_class_set_legacy_reset(dc, th1520_video_sysreg_reset);
    device_class_set_props(dc, th1520_video_sysreg_props);
}

static const TypeInfo th1520_video_sysreg_info = {
    .name = TYPE_TH1520_VIDEO_SYSREG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520VideoSysRegState),
    .instance_init = th1520_video_sysreg_init,
    .class_init = th1520_video_sysreg_class_init,
};

static void th1520_video_sysreg_register_types(void)
{
    type_register_static(&th1520_video_sysreg_info);
}

type_init(th1520_video_sysreg_register_types)
