/*
 * T-Head C900 Platform-Level Interrupt Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_THEAD_C900_PLIC_H
#define HW_INTC_THEAD_C900_PLIC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_THEAD_C900_PLIC "thead.c900-plic"
OBJECT_DECLARE_SIMPLE_TYPE(THeadC900PLICState, THEAD_C900_PLIC)

struct THeadC900PLICState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t hartid_base;
    uint32_t num_harts;
    uint32_t num_sources;
    uint32_t bitfield_words;
    uint32_t num_contexts;
    uint32_t num_enables;
    bool legacy_ahead_vmstate;

    uint32_t control;
    uint32_t *source_priority;
    uint32_t *threshold;
    uint32_t *pending;
    uint32_t *active;
    uint32_t *enable;
    uint32_t *source_level;
    uint32_t *edge_trigger;

    qemu_irq *m_external_irqs;
    qemu_irq *s_external_irqs;
};

#define THEAD_C900_PLIC_SIZE             0x01000000
#define THEAD_C900_PLIC_PRIORITY_BASE    0x000000
#define THEAD_C900_PLIC_PENDING_BASE     0x001000
#define THEAD_C900_PLIC_ENABLE_BASE      0x002000
#define THEAD_C900_PLIC_ENABLE_STRIDE    0x000080
#define THEAD_C900_PLIC_CONTROL          0x1ffffc
#define THEAD_C900_PLIC_CONTEXT_BASE     0x200000
#define THEAD_C900_PLIC_CONTEXT_STRIDE   0x001000
#define THEAD_C900_PLIC_MAX_PRIORITY     31

#endif /* HW_INTC_THEAD_C900_PLIC_H */
