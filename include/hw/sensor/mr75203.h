/*
 * Moortec MR75203 PVT controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SENSOR_MR75203_H
#define HW_SENSOR_MR75203_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_MR75203 "mr75203"
OBJECT_DECLARE_SIMPLE_TYPE(MR75203State, MR75203)

#define MR75203_WINDOW_COUNT       4
#define MR75203_MACRO_COUNT        3
#define MR75203_MAX_TS             31
#define MR75203_MAX_PD             31
#define MR75203_MAX_VM             31
#define MR75203_MAX_VM_CHANNELS    256
#define MR75203_SDIF_REG_COUNT     8

typedef enum MR75203WindowType {
    MR75203_WINDOW_COMMON,
    MR75203_WINDOW_TS,
    MR75203_WINDOW_PD,
    MR75203_WINDOW_VM,
} MR75203WindowType;

typedef struct MR75203Window {
    MemoryRegion iomem;
    MR75203State *parent;
    MR75203WindowType type;
} MR75203Window;

struct MR75203State {
    SysBusDevice parent_obj;

    MR75203Window window[MR75203_WINDOW_COUNT];
    Clock *clock;

    uint8_t ts_count;
    uint8_t pd_count;
    uint8_t vm_count;
    uint8_t vm_channels;
    uint32_t common_size;
    uint32_t ts_size;
    uint32_t pd_size;
    uint32_t vm_size;
    uint32_t component_id;
    uint32_t id_reset;
    uint32_t coeff_g;
    uint32_t coeff_h;
    int32_t coeff_j;
    uint32_t coeff_cal5;

    uint32_t id_number;
    uint32_t scratch;
    uint32_t reg_lock;
    bool sw_locked;

    uint32_t clk_synth[MR75203_MACRO_COUNT];
    uint32_t sdif_disable[MR75203_MACRO_COUNT];
    uint32_t sdif_w[MR75203_MACRO_COUNT];
    uint32_t sdif_ctrl[MR75203_MACRO_COUNT];
    uint32_t sample_ctrl[MR75203_MACRO_COUNT];
    uint16_t sample_count[MR75203_MACRO_COUNT];
    uint32_t sdif_rdata[MR75203_MACRO_COUNT];
    uint32_t ip_reg[MR75203_MACRO_COUNT][MR75203_SDIF_REG_COUNT];

    int32_t temperature[MR75203_MAX_TS];
    int32_t voltage[MR75203_MAX_VM_CHANNELS];
    uint16_t process_sample[MR75203_MAX_PD];
};

#endif /* HW_SENSOR_MR75203_H */
