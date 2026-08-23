/*
 * T-Head C900 Core-Local Interrupt Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_THEAD_C900_CLINT_H
#define HW_INTC_THEAD_C900_CLINT_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_THEAD_C900_CLINT "thead.c900-clint"
OBJECT_DECLARE_SIMPLE_TYPE(THeadC900CLINTState, THEAD_C900_CLINT)

typedef struct THeadC900CLINTTimerContext THeadC900CLINTTimerContext;

struct THeadC900CLINTState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t hartid_base;
    uint32_t num_harts;
    uint32_t timebase_freq;
    uint64_t time_delta;
    uint64_t time_at_save;

    uint32_t *msip;
    uint32_t *ssip;
    uint64_t *mtimecmp;
    uint64_t *stimecmp;
    QEMUTimer **mtimers;
    QEMUTimer **stimers;
    THeadC900CLINTTimerContext *timer_contexts;

    qemu_irq *msip_irqs;
    qemu_irq *mtimer_irqs;
    qemu_irq *ssip_irqs;
    qemu_irq *stimer_irqs;
};

#define THEAD_C900_CLINT_SIZE         0x10000
#define THEAD_C900_CLINT_MSIP_BASE    0x0000
#define THEAD_C900_CLINT_MTIMECMP_BASE 0x4000
#define THEAD_C900_CLINT_SSIP_BASE    0xc000
#define THEAD_C900_CLINT_STIMECMP_BASE 0xd000

#endif /* HW_INTC_THEAD_C900_CLINT_H */
