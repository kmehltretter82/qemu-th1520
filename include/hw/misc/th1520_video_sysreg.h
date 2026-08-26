/*
 * T-Head TH1520 video/vision system-register apertures
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_VIDEO_SYSREG_H
#define HW_MISC_TH1520_VIDEO_SYSREG_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_VIDEO_SYSREG "th1520-video-sysreg"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520VideoSysRegState, TH1520_VIDEO_SYSREG)

#define TH1520_VIDEO_SYSREG_MMIO_SIZE  0x1000
#define TH1520_VIDEO_SYSREG_REG_COUNT  4

/* Vendor U-Boot's documented VISYS divider configuration registers. */
#define TH1520_VISYS_ISP0_CLK_CFG       0x024
#define TH1520_VISYS_ISP1_CLK_CFG       0x028
#define TH1520_VISYS_ISP_RY_CLK_CFG     0x02c
#define TH1520_VISYS_MIPI_CSI0_PIXELCLK 0x030
#define TH1520_VISYS_CLK_DIV_MASK       0x0000001f

/* Vendor U-Boot's documented VOSYS configuration/reset registers. */
#define TH1520_VOSYS_GPU_RST_CFG        0x000
#define TH1520_VOSYS_CLK_GATE           0x050
#define TH1520_VOSYS_CLK_GATE1          0x054
#define TH1520_VOSYS_DPU_CCLK_CFG       0x064
#define TH1520_VOSYS_GPU_RST_CFG_MASK   0x00000003
#define TH1520_VOSYS_CLK_GATE_MASK      0xfbfffff9
#define TH1520_VOSYS_CLK_GATE1_MASK     0x00000001
#define TH1520_VOSYS_DPU_CCLK_MASK      0x0000001f

struct TH1520VideoSysRegState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    bool vosys;
    uint32_t regs[TH1520_VIDEO_SYSREG_REG_COUNT];
};

#endif /* HW_MISC_TH1520_VIDEO_SYSREG_H */
