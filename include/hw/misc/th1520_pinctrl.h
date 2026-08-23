/*
 * T-Head TH1520 pad controllers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_PINCTRL_H
#define HW_MISC_TH1520_PINCTRL_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_PADCTRL "th1520-padctrl"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520PadCtrlState, TH1520_PADCTRL)

#define TH1520_PADCTRL_MAX_MMIO_SIZE 0x2000
#define TH1520_PADCTRL_REG_END 0x420
#define TH1520_PADCTRL_REGS (TH1520_PADCTRL_REG_END / sizeof(uint32_t))

struct TH1520PadCtrlState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[TH1520_PADCTRL_REGS];
    uint8_t pad_group;
};

#endif /* HW_MISC_TH1520_PINCTRL_H */
