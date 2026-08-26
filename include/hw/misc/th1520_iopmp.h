/*
 * T-Head TH1520 I/O physical memory protection configuration window
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_IOPMP_H
#define HW_MISC_TH1520_IOPMP_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_IOPMP "th1520-iopmp"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520IOPMPState, TH1520_IOPMP)

#define TH1520_IOPMP_MMIO_SIZE             0x1000
#define TH1520_IOPMP_REGION_COUNT          16

#define TH1520_IOPMP_MISC_CTRL             0x004
#define TH1520_IOPMP_DUMMY_ADDR            0x008
#define TH1520_IOPMP_PAGE_LOCK0            0x040
#define TH1520_IOPMP_ATTR_CFG0             0x080
#define TH1520_IOPMP_DEFAULT_ATTR_CFG      0x0c0
#define TH1520_IOPMP_REGION0_SADDR         0x280
#define TH1520_IOPMP_REGION0_EADDR         0x284

#define TH1520_IOPMP_PAGE_LOCK_REGIONS     0x0000ffff
#define TH1520_IOPMP_PAGE_LOCK_DEFAULT_CFG 0x00010000
#define TH1520_IOPMP_PAGE_LOCK_BYPASS_EN   0x00020000
#define TH1520_IOPMP_PAGE_LOCK_DUMMY_ADDR  0x00040000
#define TH1520_IOPMP_PAGE_LOCK_MASK        0x0007ffff
#define TH1520_IOPMP_CTRL_BYPASS           0xff000000

struct TH1520IOPMPState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t misc_ctrl;
    uint32_t dummy_addr;
    uint32_t page_lock0;
    uint32_t attr_cfg[TH1520_IOPMP_REGION_COUNT];
    uint32_t default_attr_cfg;
    uint32_t region_start[TH1520_IOPMP_REGION_COUNT];
    uint32_t region_end[TH1520_IOPMP_REGION_COUNT];
};

#endif /* HW_MISC_TH1520_IOPMP_H */
