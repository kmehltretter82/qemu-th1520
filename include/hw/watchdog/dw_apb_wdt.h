/*
 * Synopsys DesignWare APB watchdog
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_WATCHDOG_DW_APB_WDT_H
#define HW_WATCHDOG_DW_APB_WDT_H

#include "hw/core/clock.h"
#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"

#define TYPE_DW_APB_WDT "dw-apb-wdt"
OBJECT_DECLARE_SIMPLE_TYPE(DWAPBWDTState, DW_APB_WDT)

#define DW_APB_WDT_MMIO_SIZE 0x1000

enum DWAPBWDTRegister {
    DW_APB_WDT_CR           = 0x000,
    DW_APB_WDT_TORR         = 0x004,
    DW_APB_WDT_CCVR         = 0x008,
    DW_APB_WDT_CRR          = 0x00c,
    DW_APB_WDT_STAT         = 0x010,
    DW_APB_WDT_EOI          = 0x014,
    DW_APB_WDT_COMP_PARAM_5 = 0x0e4,
    DW_APB_WDT_COMP_PARAM_4 = 0x0e8,
    DW_APB_WDT_COMP_PARAM_3 = 0x0ec,
    DW_APB_WDT_COMP_PARAM_2 = 0x0f0,
    DW_APB_WDT_COMP_PARAM_1 = 0x0f4,
    DW_APB_WDT_COMP_VERSION = 0x0f8,
    DW_APB_WDT_COMP_TYPE    = 0x0fc,
};

#define DW_APB_WDT_CR_ENABLE       BIT(0)
#define DW_APB_WDT_CR_RMOD         BIT(1)
#define DW_APB_WDT_CR_RPL_MASK     0x1c
#define DW_APB_WDT_CR_VALID        0x1f

#define DW_APB_WDT_TORR_TOP_MASK       0x0f
#define DW_APB_WDT_TORR_TOP_INIT_MASK  0xf0
#define DW_APB_WDT_TORR_VALID          0xff

#define DW_APB_WDT_CRR_RESTART     0x76
#define DW_APB_WDT_STAT_INTERRUPT  BIT(0)

#define DW_APB_WDT_PARAM_1_USE_FIX_TOP BIT(6)
#define DW_APB_WDT_COMP_TYPE_VALUE     0x44570120

struct DWAPBWDTState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    Clock *pclk;
    ptimer_state *timer;

    uint32_t cr;
    uint32_t torr;
    uint32_t stat;

    uint32_t component_param_1;
    uint32_t component_param_2;
    uint32_t component_param_3;
    uint32_t component_param_4;
    uint32_t component_param_5;
    uint32_t component_version;
    uint32_t component_type;
    uint32_t counter_reset_value;
};

#endif /* HW_WATCHDOG_DW_APB_WDT_H */
