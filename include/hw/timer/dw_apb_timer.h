/*
 * Synopsys DesignWare APB timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_DW_APB_TIMER_H
#define HW_TIMER_DW_APB_TIMER_H

#include "hw/core/clock.h"
#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_DW_APB_TIMER "dw-apb-timer"
OBJECT_DECLARE_SIMPLE_TYPE(DWAPBTimerState, DW_APB_TIMER)

#define DW_APB_TIMER_CHANNELS 4

typedef struct DWAPBTimerContext {
    DWAPBTimerState *parent;
    unsigned int index;
} DWAPBTimerContext;

/*
 * QEMU interface:
 *  + Clock input "timer": common counter clock
 *  + sysbus MMIO region 0: the component register bank
 *  + sysbus IRQ 0..3: timer interrupt outputs
 */
struct DWAPBTimerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq[DW_APB_TIMER_CHANNELS];
    Clock *timer_clk;
    ptimer_state *timer[DW_APB_TIMER_CHANNELS];
    DWAPBTimerContext context[DW_APB_TIMER_CHANNELS];

    uint32_t load_count[DW_APB_TIMER_CHANNELS];
    uint32_t control[DW_APB_TIMER_CHANNELS];
    uint32_t raw_intr[DW_APB_TIMER_CHANNELS];
    uint32_t load_count2[DW_APB_TIMER_CHANNELS];
    uint32_t protection[DW_APB_TIMER_CHANNELS];

    uint32_t component_version;
};

#endif /* HW_TIMER_DW_APB_TIMER_H */
