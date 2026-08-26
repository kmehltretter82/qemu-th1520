/*
 * BeagleV Ahead AP6203BM control/wake peer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_AP6203BM_H
#define HW_MISC_TH1520_AP6203BM_H

#include "hw/core/irq.h"
#include "hw/core/qdev.h"

#define TYPE_TH1520_AP6203BM "th1520-ap6203bm"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520AP6203BMState, TH1520_AP6203BM)

enum {
    TH1520_AP6203BM_WL_REG_ON,
    TH1520_AP6203BM_BT_REG_ON,
    TH1520_AP6203BM_BT_WAKE_HOST,
    TH1520_AP6203BM_CONTROL_COUNT,
};

enum {
    TH1520_AP6203BM_WL_HOST_WAKE,
    TH1520_AP6203BM_BT_HOST_WAKE,
    TH1520_AP6203BM_HOST_WAKE_COUNT,
};

struct TH1520AP6203BMState {
    DeviceState parent_obj;

    qemu_irq control_out[TH1520_AP6203BM_CONTROL_COUNT];
    qemu_irq host_wake_out[TH1520_AP6203BM_HOST_WAKE_COUNT];
    bool wl_reg_on;
    bool bt_reg_on;
    bool bt_wake_host;
    bool wl_host_wake;
    bool bt_host_wake;
};

#endif /* HW_MISC_TH1520_AP6203BM_H */
