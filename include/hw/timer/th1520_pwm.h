/*
 * T-Head TH1520 PWM controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_TH1520_PWM_H
#define HW_TIMER_TH1520_PWM_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_TH1520_PWM "th1520-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520PWMState, TH1520_PWM)

#define TH1520_PWM_CHANNELS 6

typedef struct TH1520PWMChannel {
    TH1520PWMState *parent;
    QEMUTimer timer;

    uint32_t ctrl;
    uint32_t period;
    uint32_t fp;
    uint32_t active_ctrl;
    uint32_t active_period;
    uint32_t active_fp;
    int64_t remaining_ns;
    bool running;
    bool update_pending;
    bool edge_is_boundary;
    bool output_level;
    uint8_t index;
} TH1520PWMChannel;

struct TH1520PWMState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *pwm_clk;
    qemu_irq output[TH1520_PWM_CHANNELS];
    TH1520PWMChannel channel[TH1520_PWM_CHANNELS];
};

#endif /* HW_TIMER_TH1520_PWM_H */
