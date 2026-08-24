/*
 * Moortec MR75203 PVT controller
 *
 * The controller is synthesis-configurable.  This model exposes the identity,
 * common macro programming registers and sample paths used by Linux's
 * mr75203 driver.  Temperature and voltage inputs are deterministic QOM
 * properties expressed in milli-degrees Celsius and millivolts respectively.
 *
 * Alarm comparators, the controller timer and interrupt aggregation are not
 * implemented yet.  Those registers are intentionally not approximated by
 * this Linux-facing first model.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/sensor/mr75203.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define PVT_COMP_ID                 0x00
#define PVT_IP_CONFIG               0x04
#define PVT_ID_NUM                  0x08
#define PVT_TM_SCRATCH              0x0c
#define PVT_REG_LOCK                0x10
#define PVT_LOCK_STATUS             0x14

#define PVT_UNLOCK_VALUE            0x1acce551

#define CLK_SYNTH                   0x00
#define CLK_SYNTH_ENABLE            BIT(24)
#define CLK_SYNTH_MASK              0x01ffffff
#define SDIF_DISABLE                0x04
#define SDIF_STATUS                 0x08
#define SDIF_STATUS_BUSY            BIT(0)
#define SDIF_STATUS_LOCK            BIT(1)
#define SDIF_W                      0x0c
#define SDIF_PROG                   BIT(31)
#define SDIF_WRITE                  BIT(27)
#define SDIF_ADDR_SHIFT             24
#define SDIF_ADDR_LENGTH            3
#define SDIF_HALT                   0x10
#define SDIF_CTRL                   0x14
#define SAMPLE_CTRL                 0x20
#define SAMPLE_CTRL_CLEAR           0x24
#define SAMPLE_COUNT                0x28

#define SAMPLE_CTRL_DISABLE         BIT(0)
#define SAMPLE_CTRL_HOLD            BIT(1)
#define SAMPLE_CTRL_DISCARD         BIT(2)

#define MACRO_COMMON_SIZE           0x40
#define MACRO_STRIDE                0x40
#define MACRO_SDIF_RDATA            0x10
#define MACRO_SDIF_DONE             0x14
#define MACRO_SDIF_DATA             0x18

#define VM_COMMON_SIZE              0x200
#define VM_STRIDE                   0x200
#define VM_SDIF_DONE                0x34
#define VM_SDIF_DATA                0x40

#define IP_CTRL                     0
#define IP_CTRL_RESET_RELEASE       BIT(1)
#define IP_CTRL_RUN_CONTINUOUS      BIT(3)
#define IP_CTRL_AUTO                BIT(8)
#define IP_CTRL_VM_MODE             BIT(10)

#define TEMP_MIN_MC                 (-40000)
#define TEMP_MAX_MC                 125000
#define TEMP_RAW_MAX                0x0fff
#define VOLTAGE_RAW_MAX             0x3fff
#define HZ_PER_MHZ                  1000000

static unsigned int mr75203_macro_index(MR75203WindowType type)
{
    g_assert(type >= MR75203_WINDOW_TS && type <= MR75203_WINDOW_VM);
    return type - MR75203_WINDOW_TS;
}

static uint32_t mr75203_macro_count(const MR75203State *s,
                                    MR75203WindowType type)
{
    switch (type) {
    case MR75203_WINDOW_TS:
        return s->ts_count;
    case MR75203_WINDOW_PD:
        return s->pd_count;
    case MR75203_WINDOW_VM:
        return s->vm_count;
    default:
        g_assert_not_reached();
    }
}

static uint32_t mr75203_instance_mask(const MR75203State *s,
                                      MR75203WindowType type)
{
    unsigned int count = mr75203_macro_count(s, type);

    if (!count) {
        return 0;
    }
    return count == 32 ? UINT32_MAX : MAKE_64BIT_MASK(0, count);
}

static uint64_t mr75203_ip_frequency(const MR75203State *s,
                                     unsigned int macro)
{
    uint32_t value = s->clk_synth[macro];
    uint32_t low;
    uint32_t high;

    if (!(value & CLK_SYNTH_ENABLE)) {
        return 0;
    }

    low = extract32(value, 0, 8);
    high = extract32(value, 8, 8);
    return clock_get_hz(s->clock) / (low + high + 2);
}

static bool mr75203_sample_ready(const MR75203State *s,
                                 MR75203WindowType type,
                                 unsigned int instance)
{
    unsigned int macro = mr75203_macro_index(type);
    uint32_t ctrl = s->ip_reg[macro][IP_CTRL];

    if (!mr75203_ip_frequency(s, macro) ||
        (s->sdif_disable[macro] & BIT(instance)) ||
        (s->sample_ctrl[macro] & SAMPLE_CTRL_DISCARD)) {
        return false;
    }

    if (!(ctrl & IP_CTRL_RESET_RELEASE) ||
        !(ctrl & (IP_CTRL_RUN_CONTINUOUS | IP_CTRL_AUTO))) {
        return false;
    }

    return type != MR75203_WINDOW_VM || (ctrl & IP_CTRL_VM_MODE);
}

static int64_t mr75203_temperature_from_raw(const MR75203State *s,
                                             uint16_t raw)
{
    uint64_t ip_frequency = mr75203_ip_frequency(
        s, mr75203_macro_index(MR75203_WINDOW_TS));

    return (int64_t)s->coeff_g +
           (int64_t)s->coeff_h * raw / s->coeff_cal5 -
           s->coeff_h / 2 +
           (int64_t)s->coeff_j * (int64_t)ip_frequency / HZ_PER_MHZ;
}

static uint16_t mr75203_temperature_raw(const MR75203State *s,
                                        unsigned int channel)
{
    uint64_t ip_frequency = mr75203_ip_frequency(
        s, mr75203_macro_index(MR75203_WINDOW_TS));
    int64_t j_term = (int64_t)s->coeff_j * (int64_t)ip_frequency /
                     HZ_PER_MHZ;
    int64_t base = (int64_t)s->coeff_g - s->coeff_h / 2 + j_term;
    int64_t target = s->temperature[channel] - base;
    int64_t raw;
    int64_t best_error = INT64_MAX;
    uint16_t best = 0;

    raw = (target * s->coeff_cal5 + s->coeff_h / 2) / s->coeff_h;
    raw = MAX(0, MIN((int64_t)TEMP_RAW_MAX, raw));

    for (int candidate = MAX(0, (int)raw - 2);
         candidate <= MIN(TEMP_RAW_MAX, (int)raw + 2); candidate++) {
        int64_t error = llabs(mr75203_temperature_from_raw(s, candidate) -
                              s->temperature[channel]);

        if (error < best_error) {
            best_error = error;
            best = candidate;
        }
    }

    return best;
}

static int32_t mr75203_voltage_from_raw(uint16_t raw)
{
    return ((int64_t)90 * raw - 245805) / 1024;
}

static uint16_t mr75203_voltage_raw(const MR75203State *s,
                                    unsigned int channel)
{
    int64_t raw = ((int64_t)s->voltage[channel] * 1024 + 245805 + 45) / 90;
    int64_t best_error = INT64_MAX;
    uint16_t best = 0;

    raw = MAX(0, MIN((int64_t)VOLTAGE_RAW_MAX, raw));
    for (int candidate = MAX(0, (int)raw - 2);
         candidate <= MIN(VOLTAGE_RAW_MAX, (int)raw + 2); candidate++) {
        int64_t error = llabs(mr75203_voltage_from_raw(candidate) -
                              s->voltage[channel]);

        if (error < best_error) {
            best_error = error;
            best = candidate;
        }
    }

    return best;
}

static void mr75203_increment_sample_count(MR75203State *s,
                                           MR75203WindowType type)
{
    unsigned int macro = mr75203_macro_index(type);

    if (s->sample_ctrl[macro] & SAMPLE_CTRL_DISABLE) {
        return;
    }
    if (s->sample_count[macro] != UINT16_MAX ||
        !(s->sample_ctrl[macro] & SAMPLE_CTRL_HOLD)) {
        s->sample_count[macro]++;
    }
}

static uint64_t mr75203_common_read(MR75203State *s, hwaddr offset)
{
    switch (offset) {
    case PVT_COMP_ID:
        return s->component_id;
    case PVT_IP_CONFIG:
        return (uint32_t)s->vm_channels << 24 |
               (uint32_t)s->vm_count << 16 |
               (uint32_t)s->pd_count << 8 |
               s->ts_count;
    case PVT_ID_NUM:
        return s->id_number;
    case PVT_TM_SCRATCH:
        return s->scratch;
    case PVT_REG_LOCK:
        return s->reg_lock;
    case PVT_LOCK_STATUS:
        return s->sw_locked;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented common register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_MR75203, offset);
        return 0;
    }
}

static void mr75203_common_write(MR75203State *s, hwaddr offset,
                                  uint64_t value)
{
    switch (offset) {
    case PVT_ID_NUM:
        s->id_number = value;
        return;
    case PVT_TM_SCRATCH:
        s->scratch = value;
        return;
    case PVT_REG_LOCK:
        s->reg_lock = value;
        s->sw_locked = value != PVT_UNLOCK_VALUE;
        return;
    case PVT_COMP_ID:
    case PVT_IP_CONFIG:
    case PVT_LOCK_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only common register 0x%03"
                      HWADDR_PRIx "\n", TYPE_MR75203, offset);
        return;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented common register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_MR75203, offset);
    }
}

static uint64_t mr75203_macro_common_read(MR75203State *s,
                                          MR75203WindowType type,
                                          hwaddr offset)
{
    unsigned int macro = mr75203_macro_index(type);

    switch (offset) {
    case CLK_SYNTH:
        return s->clk_synth[macro];
    case SDIF_DISABLE:
        return s->sdif_disable[macro];
    case SDIF_STATUS:
        return (s->ip_reg[macro][IP_CTRL] & IP_CTRL_AUTO) ?
               SDIF_STATUS_LOCK : 0;
    case SDIF_W:
        return s->sdif_w[macro];
    case SDIF_HALT:
        return 0;
    case SDIF_CTRL:
        return s->sdif_ctrl[macro];
    case SAMPLE_CTRL:
        return s->sample_ctrl[macro];
    case SAMPLE_CTRL_CLEAR:
        return 0;
    case SAMPLE_COUNT:
        return s->sample_count[macro];
    default:
        return UINT64_MAX;
    }
}

static void mr75203_macro_common_write(MR75203State *s,
                                       MR75203WindowType type,
                                       hwaddr offset, uint64_t value)
{
    unsigned int macro = mr75203_macro_index(type);
    uint32_t instance_mask = mr75203_instance_mask(s, type);
    unsigned int address;

    if (s->sw_locked) {
        return;
    }

    switch (offset) {
    case CLK_SYNTH:
        s->clk_synth[macro] = value & CLK_SYNTH_MASK;
        return;
    case SDIF_DISABLE:
        s->sdif_disable[macro] = value & instance_mask;
        return;
    case SDIF_W:
        s->sdif_w[macro] = value & ~SDIF_PROG;
        if (!(value & SDIF_PROG)) {
            return;
        }
        address = extract32(value, SDIF_ADDR_SHIFT, SDIF_ADDR_LENGTH);
        if (value & SDIF_WRITE) {
            s->ip_reg[macro][address] = value & 0x00ffffff;
        } else if (!(s->ip_reg[macro][IP_CTRL] & IP_CTRL_AUTO)) {
            s->sdif_rdata[macro] = s->ip_reg[macro][address];
        }
        return;
    case SDIF_HALT:
        if (value & 1) {
            memset(s->ip_reg[macro], 0, sizeof(s->ip_reg[macro]));
            s->sdif_rdata[macro] = 0;
        }
        return;
    case SDIF_CTRL:
        s->sdif_ctrl[macro] = value & instance_mask;
        return;
    case SAMPLE_CTRL:
        s->sample_ctrl[macro] = value & 7;
        return;
    case SAMPLE_CTRL_CLEAR:
        if (value & 1) {
            s->sample_count[macro] = 0;
        }
        return;
    case SDIF_STATUS:
    case SAMPLE_COUNT:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only macro register 0x%03"
                      HWADDR_PRIx "\n", TYPE_MR75203, offset);
        return;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented macro register at 0x%03"
                      HWADDR_PRIx "\n", TYPE_MR75203, offset);
    }
}

static uint64_t mr75203_ts_pd_read(MR75203State *s,
                                   MR75203WindowType type, hwaddr offset)
{
    unsigned int macro = mr75203_macro_index(type);
    unsigned int instance;
    hwaddr reg;

    if (offset < MACRO_COMMON_SIZE) {
        uint64_t value = mr75203_macro_common_read(s, type, offset);

        if (value != UINT64_MAX) {
            return value;
        }
        goto unimplemented;
    }

    instance = (offset - MACRO_COMMON_SIZE) / MACRO_STRIDE;
    reg = (offset - MACRO_COMMON_SIZE) % MACRO_STRIDE;
    if (instance >= mr75203_macro_count(s, type)) {
        goto unimplemented;
    }

    switch (reg) {
    case MACRO_SDIF_RDATA:
        return s->sdif_rdata[macro];
    case MACRO_SDIF_DONE:
        return mr75203_sample_ready(s, type, instance);
    case MACRO_SDIF_DATA:
        if (!mr75203_sample_ready(s, type, instance)) {
            return 0;
        }
        mr75203_increment_sample_count(s, type);
        if (type == MR75203_WINDOW_TS) {
            return mr75203_temperature_raw(s, instance);
        }
        return s->process_sample[instance];
    default:
        break;
    }

unimplemented:
    qemu_log_mask(LOG_UNIMP,
                  "%s: unimplemented %s register at 0x%03" HWADDR_PRIx "\n",
                  TYPE_MR75203,
                  type == MR75203_WINDOW_TS ? "TS" : "PD", offset);
    return 0;
}

static void mr75203_ts_pd_write(MR75203State *s,
                                MR75203WindowType type, hwaddr offset,
                                uint64_t value)
{
    if (offset < MACRO_COMMON_SIZE) {
        mr75203_macro_common_write(s, type, offset, value);
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "%s: unimplemented %s register at 0x%03" HWADDR_PRIx "\n",
                  TYPE_MR75203,
                  type == MR75203_WINDOW_TS ? "TS" : "PD", offset);
}

static uint64_t mr75203_vm_read(MR75203State *s, hwaddr offset)
{
    unsigned int instance;
    unsigned int channel;
    hwaddr reg;

    if (offset < VM_COMMON_SIZE) {
        uint64_t value = mr75203_macro_common_read(
            s, MR75203_WINDOW_VM, offset);

        if (value != UINT64_MAX) {
            return value;
        }
        goto unimplemented;
    }

    instance = (offset - VM_COMMON_SIZE) / VM_STRIDE;
    reg = (offset - VM_COMMON_SIZE) % VM_STRIDE;
    if (instance >= s->vm_count) {
        goto unimplemented;
    }

    if (reg == VM_SDIF_DONE) {
        return mr75203_sample_ready(s, MR75203_WINDOW_VM, instance);
    }
    if (reg >= VM_SDIF_DATA &&
        reg < VM_SDIF_DATA + 4 * s->vm_channels) {
        channel = (reg - VM_SDIF_DATA) / 4;
        if (!mr75203_sample_ready(s, MR75203_WINDOW_VM, instance)) {
            return 0;
        }
        mr75203_increment_sample_count(s, MR75203_WINDOW_VM);
        return mr75203_voltage_raw(s, instance * s->vm_channels + channel);
    }

unimplemented:
    qemu_log_mask(LOG_UNIMP,
                  "%s: unimplemented VM register at 0x%03" HWADDR_PRIx "\n",
                  TYPE_MR75203, offset);
    return 0;
}

static void mr75203_vm_write(MR75203State *s, hwaddr offset, uint64_t value)
{
    if (offset < VM_COMMON_SIZE) {
        mr75203_macro_common_write(s, MR75203_WINDOW_VM, offset, value);
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "%s: unimplemented VM register at 0x%03" HWADDR_PRIx "\n",
                  TYPE_MR75203, offset);
}

static uint64_t mr75203_read(void *opaque, hwaddr offset, unsigned int size)
{
    MR75203Window *window = opaque;
    MR75203State *s = window->parent;

    switch (window->type) {
    case MR75203_WINDOW_COMMON:
        return mr75203_common_read(s, offset);
    case MR75203_WINDOW_TS:
    case MR75203_WINDOW_PD:
        return mr75203_ts_pd_read(s, window->type, offset);
    case MR75203_WINDOW_VM:
        return mr75203_vm_read(s, offset);
    default:
        g_assert_not_reached();
    }
}

static void mr75203_write(void *opaque, hwaddr offset, uint64_t value,
                          unsigned int size)
{
    MR75203Window *window = opaque;
    MR75203State *s = window->parent;

    switch (window->type) {
    case MR75203_WINDOW_COMMON:
        mr75203_common_write(s, offset, value);
        return;
    case MR75203_WINDOW_TS:
    case MR75203_WINDOW_PD:
        mr75203_ts_pd_write(s, window->type, offset, value);
        return;
    case MR75203_WINDOW_VM:
        mr75203_vm_write(s, offset, value);
        return;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps mr75203_ops = {
    .read = mr75203_read,
    .write = mr75203_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void mr75203_get_temperature(Object *obj, Visitor *v,
                                    const char *name, void *opaque,
                                    Error **errp)
{
    int32_t *temperature = opaque;
    int64_t value = *temperature;

    visit_type_int(v, name, &value, errp);
}

static void mr75203_set_temperature(Object *obj, Visitor *v,
                                    const char *name, void *opaque,
                                    Error **errp)
{
    int32_t *temperature = opaque;
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }
    if (value < TEMP_MIN_MC || value > TEMP_MAX_MC) {
        error_setg(errp, "%s must be between %d and %d milli-degrees C",
                   name, TEMP_MIN_MC, TEMP_MAX_MC);
        return;
    }
    *temperature = value;
}

static void mr75203_get_voltage(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    int32_t *voltage = opaque;
    int64_t value = *voltage;

    visit_type_int(v, name, &value, errp);
}

static void mr75203_set_voltage(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    int32_t *voltage = opaque;
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }
    if (value < mr75203_voltage_from_raw(0) ||
        value > mr75203_voltage_from_raw(VOLTAGE_RAW_MAX)) {
        error_setg(errp, "%s is outside the representable voltage range",
                   name);
        return;
    }
    *voltage = value;
}

static void mr75203_reset(DeviceState *dev)
{
    MR75203State *s = MR75203(dev);

    s->id_number = s->id_reset;
    s->scratch = 0;
    s->reg_lock = 0;
    s->sw_locked = false;
    memset(s->clk_synth, 0, sizeof(s->clk_synth));
    memset(s->sdif_disable, 0, sizeof(s->sdif_disable));
    memset(s->sdif_w, 0, sizeof(s->sdif_w));
    memset(s->sdif_ctrl, 0, sizeof(s->sdif_ctrl));
    memset(s->sample_ctrl, 0, sizeof(s->sample_ctrl));
    memset(s->sample_count, 0, sizeof(s->sample_count));
    memset(s->sdif_rdata, 0, sizeof(s->sdif_rdata));
    memset(s->ip_reg, 0, sizeof(s->ip_reg));
}

static const VMStateDescription vmstate_mr75203 = {
    .name = TYPE_MR75203,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(id_number, MR75203State),
        VMSTATE_UINT32(scratch, MR75203State),
        VMSTATE_UINT32(reg_lock, MR75203State),
        VMSTATE_BOOL(sw_locked, MR75203State),
        VMSTATE_UINT32_ARRAY(clk_synth, MR75203State,
                             MR75203_MACRO_COUNT),
        VMSTATE_UINT32_ARRAY(sdif_disable, MR75203State,
                             MR75203_MACRO_COUNT),
        VMSTATE_UINT32_ARRAY(sdif_w, MR75203State, MR75203_MACRO_COUNT),
        VMSTATE_UINT32_ARRAY(sdif_ctrl, MR75203State, MR75203_MACRO_COUNT),
        VMSTATE_UINT32_ARRAY(sample_ctrl, MR75203State,
                             MR75203_MACRO_COUNT),
        VMSTATE_UINT16_ARRAY(sample_count, MR75203State,
                             MR75203_MACRO_COUNT),
        VMSTATE_UINT32_ARRAY(sdif_rdata, MR75203State,
                             MR75203_MACRO_COUNT),
        VMSTATE_UINT32_2DARRAY(ip_reg, MR75203State, MR75203_MACRO_COUNT,
                               MR75203_SDIF_REG_COUNT),
        VMSTATE_INT32_ARRAY(temperature, MR75203State, MR75203_MAX_TS),
        VMSTATE_INT32_ARRAY(voltage, MR75203State,
                            MR75203_MAX_VM_CHANNELS),
        VMSTATE_UINT16_ARRAY(process_sample, MR75203State, MR75203_MAX_PD),
        VMSTATE_END_OF_LIST(),
    },
};

static void mr75203_init_window(MR75203State *s, MR75203WindowType type,
                                const char *name, uint32_t size)
{
    MR75203Window *window = &s->window[type];

    window->parent = s;
    window->type = type;
    memory_region_init_io(&window->iomem, OBJECT(s), &mr75203_ops, window,
                          name, size);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &window->iomem);
}

static void mr75203_realize(DeviceState *dev, Error **errp)
{
    MR75203State *s = MR75203(dev);
    Object *obj = OBJECT(dev);
    uint32_t total_vm_channels = s->vm_count * s->vm_channels;

    if (!clock_has_source(s->clock)) {
        error_setg(errp, "%s: clock input must be connected", TYPE_MR75203);
        return;
    }
    if (!s->coeff_h || !s->coeff_cal5) {
        error_setg(errp, "%s: temperature coefficients must be nonzero",
                   TYPE_MR75203);
        return;
    }
    if (s->ts_count > MR75203_MAX_TS ||
        s->pd_count > MR75203_MAX_PD ||
        s->vm_count > MR75203_MAX_VM) {
        error_setg(errp, "%s: configured macro count exceeds model maximum",
                   TYPE_MR75203);
        return;
    }
    if (total_vm_channels > MR75203_MAX_VM_CHANNELS) {
        error_setg(errp, "%s: %u voltage channels exceeds model maximum %u",
                   TYPE_MR75203, total_vm_channels,
                   MR75203_MAX_VM_CHANNELS);
        return;
    }
    if (s->common_size < 0x80 ||
        s->ts_size < MACRO_COMMON_SIZE + s->ts_count * MACRO_STRIDE ||
        s->pd_size < MACRO_COMMON_SIZE + s->pd_count * MACRO_STRIDE ||
        s->vm_size < VM_COMMON_SIZE + s->vm_count * VM_STRIDE) {
        error_setg(errp, "%s: configured aperture is too small for synthesis",
                   TYPE_MR75203);
        return;
    }

    mr75203_init_window(s, MR75203_WINDOW_COMMON, "mr75203.common",
                        s->common_size);
    mr75203_init_window(s, MR75203_WINDOW_TS, "mr75203.ts", s->ts_size);
    mr75203_init_window(s, MR75203_WINDOW_PD, "mr75203.pd", s->pd_size);
    mr75203_init_window(s, MR75203_WINDOW_VM, "mr75203.vm", s->vm_size);

    for (unsigned int i = 0; i < s->ts_count; i++) {
        object_property_add(obj, "temperature[*]", "int",
                            mr75203_get_temperature,
                            mr75203_set_temperature, NULL,
                            &s->temperature[i]);
    }
    for (unsigned int i = 0; i < total_vm_channels; i++) {
        object_property_add(obj, "voltage[*]", "int", mr75203_get_voltage,
                            mr75203_set_voltage, NULL, &s->voltage[i]);
    }
    for (unsigned int i = 0; i < s->pd_count; i++) {
        object_property_add_uint16_ptr(obj, "process-sample[*]",
                                       &s->process_sample[i],
                                       OBJ_PROP_FLAG_READWRITE);
    }
}

static const Property mr75203_properties[] = {
    DEFINE_PROP_UINT8("ts-count", MR75203State, ts_count, 0),
    DEFINE_PROP_UINT8("pd-count", MR75203State, pd_count, 0),
    DEFINE_PROP_UINT8("vm-count", MR75203State, vm_count, 0),
    DEFINE_PROP_UINT8("vm-channels", MR75203State, vm_channels, 0),
    DEFINE_PROP_UINT32("common-mmio-size", MR75203State, common_size, 0x80),
    DEFINE_PROP_UINT32("ts-mmio-size", MR75203State, ts_size, 0x40),
    DEFINE_PROP_UINT32("pd-mmio-size", MR75203State, pd_size, 0x40),
    DEFINE_PROP_UINT32("vm-mmio-size", MR75203State, vm_size, 0x200),
    DEFINE_PROP_UINT32("component-id", MR75203State, component_id, 0),
    DEFINE_PROP_UINT32("id-number", MR75203State, id_reset, 0),
    DEFINE_PROP_UINT32("ts-coeff-g", MR75203State, coeff_g, 60000),
    DEFINE_PROP_UINT32("ts-coeff-h", MR75203State, coeff_h, 200000),
    DEFINE_PROP_INT32("ts-coeff-j", MR75203State, coeff_j, -100),
    DEFINE_PROP_UINT32("ts-coeff-cal5", MR75203State, coeff_cal5, 4094),
};

static void mr75203_init(Object *obj)
{
    MR75203State *s = MR75203(obj);

    s->clock = qdev_init_clock_in(DEVICE(obj), "clock", NULL, NULL, 0);
    for (unsigned int i = 0; i < ARRAY_SIZE(s->temperature); i++) {
        s->temperature[i] = 25000;
    }
    for (unsigned int i = 0; i < ARRAY_SIZE(s->voltage); i++) {
        s->voltage[i] = 800;
    }
}

static void mr75203_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Moortec MR75203 PVT controller";
    dc->realize = mr75203_realize;
    dc->vmsd = &vmstate_mr75203;
    device_class_set_legacy_reset(dc, mr75203_reset);
    device_class_set_props(dc, mr75203_properties);
}

static const TypeInfo mr75203_info = {
    .name = TYPE_MR75203,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MR75203State),
    .instance_init = mr75203_init,
    .class_init = mr75203_class_init,
};

static void mr75203_register_types(void)
{
    type_register_static(&mr75203_info);
}

type_init(mr75203_register_types)
