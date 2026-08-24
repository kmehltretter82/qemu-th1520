/*
 * Synopsys DesignWare APB RTC as used by APM X-Gene and TH1520
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RTC_XGENE_RTC_H
#define HW_RTC_XGENE_RTC_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_XGENE_RTC "xgene-rtc"
OBJECT_DECLARE_SIMPLE_TYPE(XGeneRTCState, XGENE_RTC)

#define XGENE_RTC_MMIO_SIZE 0x1000

enum XGeneRTCRegister {
    XGENE_RTC_CCVR  = 0x00,
    XGENE_RTC_CMR   = 0x04,
    XGENE_RTC_CLR   = 0x08,
    XGENE_RTC_CCR   = 0x0c,
    XGENE_RTC_STAT  = 0x10,
    XGENE_RTC_RSTAT = 0x14,
    XGENE_RTC_EOI   = 0x18,
    XGENE_RTC_VER   = 0x1c,
    XGENE_RTC_CPSR  = 0x20,
    XGENE_RTC_CPCVR = 0x24,
};

#define XGENE_RTC_CCR_IE         BIT(0)
#define XGENE_RTC_CCR_MASK       BIT(1)
#define XGENE_RTC_CCR_EN         BIT(2)
#define XGENE_RTC_CCR_WEN        BIT(3)
#define XGENE_RTC_CCR_PSCLR_EN   BIT(4)
#define XGENE_RTC_CCR_VALID      0x1f

#define XGENE_RTC_INTERRUPT      BIT(0)

struct XGeneRTCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    Clock *clock;
    QEMUTimer *alarm_timer;
    QEMUTimer *load_timer;

    uint32_t counter_base;
    uint32_t prescaler_count;
    int64_t base_ns;

    uint32_t cmr;
    uint32_t clr;
    uint32_t ccr;
    uint32_t rstat;
    uint32_t cpsr;
    bool load_pending;

    uint32_t component_version;
    uint32_t prescaler_reset;

    int64_t base_ns_vmstate;
    int64_t alarm_deadline_vmstate;
    int64_t load_deadline_vmstate;
};

#endif /* HW_RTC_XGENE_RTC_H */
