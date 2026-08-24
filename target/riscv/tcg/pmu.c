/*
 * RISC-V PMU file.
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "pmu.h"
#include "exec/icount.h"
#include "system/device_tree.h"

#define RISCV_TIMEBASE_FREQ 1000000000 /* 1Ghz */

static bool riscv_pmu_counter_valid(RISCVCPU *cpu, uint32_t ctr_idx)
{
    if (ctr_idx < 3 || ctr_idx >= RV_MAX_MHPMCOUNTERS ||
        !(cpu->pmu_avail_ctrs & BIT(ctr_idx))) {
        return false;
    } else {
        return true;
    }
}

static bool riscv_pmu_counter_present(RISCVCPU *cpu, uint32_t ctr_idx)
{
    if (cpu->cfg.thead_c9xx_pmu && (ctr_idx == 0 || ctr_idx == 2)) {
        return true;
    }

    return riscv_pmu_counter_valid(cpu, ctr_idx);
}

static bool riscv_pmu_counter_enabled(RISCVCPU *cpu, uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;

    if (riscv_pmu_counter_present(cpu, ctr_idx) &&
        !get_field(env->mcountinhibit, BIT(ctr_idx))) {
        return true;
    } else {
        return false;
    }
}

static bool riscv_pmu_counter_filtered(CPURISCVState *env, uint64_t cfg)
{
    bool virt_on = env->virt_enabled;

    return (env->priv == PRV_M && (cfg & MHPMEVENT_BIT_MINH)) ||
           (env->priv == PRV_S && virt_on &&
            (cfg & MHPMEVENT_BIT_VSINH)) ||
           (env->priv == PRV_U && virt_on &&
            (cfg & MHPMEVENT_BIT_VUINH)) ||
           (env->priv == PRV_S && !virt_on &&
            (cfg & MHPMEVENT_BIT_SINH)) ||
           (env->priv == PRV_U && !virt_on &&
            (cfg & MHPMEVENT_BIT_UINH));
}

/*
 * Information needed to update counters:
 *  new_priv, new_virt: To correctly save starting snapshot for the newly
 *                      started mode. Look at array being indexed with newprv.
 *  old_priv, old_virt: To correctly select previous snapshot for old priv
 *                      and compute delta. Also to select correct counter
 *                      to inc. Look at arrays being indexed with env->priv.
 *
 *  To avoid the complexity of calling this function, we assume that
 *  env->priv and env->virt_enabled contain old priv and old virt and
 *  new priv and new virt values are passed in as arguments.
 */
static void riscv_pmu_icount_update_priv(CPURISCVState *env,
                                         privilege_mode_t newpriv,
                                         bool new_virt)
{
    uint64_t *snapshot_prev, *snapshot_new;
    uint64_t current_icount;
    uint64_t *counter_arr;
    uint64_t delta;

    if (icount_enabled()) {
        current_icount = icount_get_raw();
    } else {
        current_icount = cpu_get_host_ticks();
    }

    if (env->virt_enabled) {
        g_assert(env->priv <= PRV_S);
        counter_arr = env->pmu_fixed_ctrs[1].counter_virt;
        snapshot_prev = env->pmu_fixed_ctrs[1].counter_virt_prev;
    } else {
        counter_arr = env->pmu_fixed_ctrs[1].counter;
        snapshot_prev = env->pmu_fixed_ctrs[1].counter_prev;
    }

    if (new_virt) {
        g_assert(newpriv <= PRV_S);
        snapshot_new = env->pmu_fixed_ctrs[1].counter_virt_prev;
    } else {
        snapshot_new = env->pmu_fixed_ctrs[1].counter_prev;
    }

     /*
      * new_priv can be same as env->priv. So we need to calculate
      * delta first before updating snapshot_new[new_priv].
      */
    delta = current_icount - snapshot_prev[env->priv];
    snapshot_new[newpriv] = current_icount;

    counter_arr[env->priv] += delta;
}

static void riscv_pmu_cycle_update_priv(CPURISCVState *env,
                                        privilege_mode_t newpriv,
                                        bool new_virt)
{
    uint64_t *snapshot_prev, *snapshot_new;
    uint64_t current_ticks;
    uint64_t *counter_arr;
    uint64_t delta;

    if (icount_enabled()) {
        current_ticks = icount_get();
    } else {
        current_ticks = cpu_get_host_ticks();
    }

    if (env->virt_enabled) {
        g_assert(env->priv <= PRV_S);
        counter_arr = env->pmu_fixed_ctrs[0].counter_virt;
        snapshot_prev = env->pmu_fixed_ctrs[0].counter_virt_prev;
    } else {
        counter_arr = env->pmu_fixed_ctrs[0].counter;
        snapshot_prev = env->pmu_fixed_ctrs[0].counter_prev;
    }

    if (new_virt) {
        g_assert(newpriv <= PRV_S);
        snapshot_new = env->pmu_fixed_ctrs[0].counter_virt_prev;
    } else {
        snapshot_new = env->pmu_fixed_ctrs[0].counter_prev;
    }

    delta = current_ticks - snapshot_prev[env->priv];
    snapshot_new[newpriv] = current_ticks;

    counter_arr[env->priv] += delta;
}

void riscv_pmu_update_fixed_ctrs(CPURISCVState *env,
                                 privilege_mode_t newpriv,
                                 bool new_virt)
{
    riscv_pmu_cycle_update_priv(env, newpriv, new_virt);
    riscv_pmu_icount_update_priv(env, newpriv, new_virt);
}

void riscv_pmu_decr_instret(CPURISCVState *env)
{
    if (!icount_enabled() ||
        (env->mcountinhibit & COUNTEREN_IR) ||
        riscv_pmu_counter_filtered(env, env->minstretcfg)) {
        return;
    }

    /*
     * minstret is derived from icount, which includes the current
     * instruction.  Move the baseline forward to exclude an instruction
     * that raises an exception and therefore does not retire.
     */
    env->pmu_ctrs[2].mhpmcounter_prev++;
}

static uint32_t riscv_pmu_event_counter_mask(RISCVCPU *cpu,
                                             uint32_t event_idx)
{
    if (!cpu->pmu_event_ctr_map) {
        return 0;
    }

    return GPOINTER_TO_UINT(g_hash_table_lookup(cpu->pmu_event_ctr_map,
                                                GUINT_TO_POINTER(event_idx)));
}

void riscv_pmu_thead_c9xx_update_irq(CPURISCVState *env)
{
    bool pending = env->th_mcounterinten & env->th_mcounterof;

    riscv_cpu_update_mip(env, MIP_THEAD_C9XX_PMU_OVF,
                         BOOL_TO_MASK(pending));
}

static void riscv_pmu_set_overflow(RISCVCPU *cpu, uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;

    if (cpu->cfg.thead_c9xx_pmu) {
        env->th_mcounterof |= BIT(ctr_idx);
        riscv_pmu_thead_c9xx_update_irq(env);
    } else if (!(env->mhpmevent_val[ctr_idx] & MHPMEVENT_BIT_OF)) {
        env->mhpmevent_val[ctr_idx] |= MHPMEVENT_BIT_OF;
        riscv_cpu_update_mip(env, MIP_LCOFIP, BOOL_TO_MASK(1));
    }
}

static bool riscv_pmu_thead_c9xx_priv_disabled(CPURISCVState *env)
{
    switch (env->priv) {
    case PRV_M:
        return env->th_mxstatus & THEAD_MXSTATUS_PMDM;
    case PRV_S:
        return env->th_mxstatus & THEAD_MXSTATUS_PMDS;
    case PRV_U:
        return env->th_mxstatus & THEAD_MXSTATUS_PMDU;
    default:
        return false;
    }
}

int riscv_pmu_incr_ctr(RISCVCPU *cpu, uint32_t event_idx)
{
    uint32_t ctr_mask;
    CPURISCVState *env = &cpu->env;
    bool updated = false;

    if (!cpu->cfg.pmu_mask) {
        return 0;
    }
    ctr_mask = riscv_pmu_event_counter_mask(cpu, event_idx);
    if (!ctr_mask) {
        return -1;
    }

    while (ctr_mask) {
        uint32_t ctr_idx = ctz32(ctr_mask);
        PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];

        ctr_mask &= ~BIT(ctr_idx);
        if (!riscv_pmu_counter_enabled(cpu, ctr_idx)) {
            continue;
        }

        /* Privilege mode filtering */
        if ((cpu->cfg.thead_c9xx_pmu &&
             riscv_pmu_thead_c9xx_priv_disabled(env)) ||
            (!cpu->cfg.thead_c9xx_pmu &&
             riscv_pmu_counter_filtered(env,
                                        env->mhpmevent_val[ctr_idx]))) {
            continue;
        }

        if (counter->mhpmcounter_val == UINT64_MAX) {
            counter->mhpmcounter_val = 0;
            riscv_pmu_set_overflow(cpu, ctr_idx);
        } else {
            counter->mhpmcounter_val++;
        }
        updated = true;
    }

    return updated ? 0 : -1;
}

bool riscv_pmu_ctr_monitor_instructions(CPURISCVState *env,
                                        uint32_t target_ctr)
{
    RISCVCPU *cpu;
    uint32_t event_idx;
    uint32_t ctr_mask;

    /* Fixed instret counter */
    if (target_ctr == 2) {
        return true;
    }

    cpu = env_archcpu(env);
    if (!cpu->pmu_event_ctr_map) {
        return false;
    }

    event_idx = cpu->cfg.thead_c9xx_pmu ?
                THEAD_C9XX_PMU_EVENT_INSTRUCTIONS :
                RISCV_PMU_EVENT_HW_INSTRUCTIONS;
    ctr_mask = riscv_pmu_event_counter_mask(cpu, event_idx);

    return ctr_mask & BIT(target_ctr);
}

bool riscv_pmu_ctr_monitor_cycles(CPURISCVState *env, uint32_t target_ctr)
{
    RISCVCPU *cpu;
    uint32_t event_idx;
    uint32_t ctr_mask;

    /* Fixed mcycle counter */
    if (target_ctr == 0) {
        return true;
    }

    cpu = env_archcpu(env);
    if (!cpu->pmu_event_ctr_map || cpu->cfg.thead_c9xx_pmu) {
        return false;
    }

    event_idx = RISCV_PMU_EVENT_HW_CPU_CYCLES;
    ctr_mask = riscv_pmu_event_counter_mask(cpu, event_idx);

    return ctr_mask & BIT(target_ctr);
}

uint64_t riscv_pmu_ctr_get_fixed_counters_val(CPURISCVState *env,
                                               int counter_idx)
{
    int inst = riscv_pmu_ctr_monitor_instructions(env, counter_idx);
    uint64_t *counter_arr_virt = env->pmu_fixed_ctrs[inst].counter_virt;
    uint64_t *counter_arr = env->pmu_fixed_ctrs[inst].counter;
    uint64_t curr_val = 0;
    uint64_t cfg_val = 0;

    if (counter_idx == 0) {
        cfg_val = env->mcyclecfg;
    } else if (counter_idx == 2) {
        cfg_val = env->minstretcfg;
    } else {
        cfg_val = env->mhpmevent_val[counter_idx];
        cfg_val &= MHPMEVENT_FILTER_MASK;
    }

    if (riscv_cpu_cfg(env)->thead_c9xx_pmu) {
        cfg_val = 0;
        cfg_val |= env->th_mxstatus & THEAD_MXSTATUS_PMDM ?
                   MCYCLECFG_BIT_MINH : 0;
        cfg_val |= env->th_mxstatus & THEAD_MXSTATUS_PMDS ?
                   MCYCLECFG_BIT_SINH | MCYCLECFG_BIT_VSINH : 0;
        cfg_val |= env->th_mxstatus & THEAD_MXSTATUS_PMDU ?
                   MCYCLECFG_BIT_UINH | MCYCLECFG_BIT_VUINH : 0;
    }

    if (!cfg_val) {
        if (icount_enabled()) {
            curr_val = inst ? icount_get_raw() : icount_get();
        } else {
            curr_val = cpu_get_host_ticks();
        }

        return curr_val;
    }

    /* Update counter before reading. */
    riscv_pmu_update_fixed_ctrs(env, env->priv, env->virt_enabled);

    if (!(cfg_val & MCYCLECFG_BIT_MINH)) {
        curr_val += counter_arr[PRV_M];
    }

    if (!(cfg_val & MCYCLECFG_BIT_SINH)) {
        curr_val += counter_arr[PRV_S];
    }

    if (!(cfg_val & MCYCLECFG_BIT_UINH)) {
        curr_val += counter_arr[PRV_U];
    }

    if (!(cfg_val & MCYCLECFG_BIT_VSINH)) {
        curr_val += counter_arr_virt[PRV_S];
    }

    if (!(cfg_val & MCYCLECFG_BIT_VUINH)) {
        curr_val += counter_arr_virt[PRV_U];
    }

    return curr_val;
}

static void pmu_remove_counter_from_event_map(RISCVCPU *cpu,
                                              uint32_t ctr_idx)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, cpu->pmu_event_ctr_map);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        uint32_t ctr_mask = GPOINTER_TO_UINT(value) & ~BIT(ctr_idx);

        if (ctr_mask) {
            g_hash_table_iter_replace(&iter, GUINT_TO_POINTER(ctr_mask));
        } else {
            g_hash_table_iter_remove(&iter);
        }
    }
}

static int64_t pmu_icount_ticks_to_ns(int64_t value)
{
    int64_t ret = 0;

    if (icount_enabled()) {
        ret = icount_to_ns(value);
    } else {
        ret = (NANOSECONDS_PER_SECOND / RISCV_TIMEBASE_FREQ) * value;
    }

    return ret;
}

int riscv_pmu_update_event_map(CPURISCVState *env, uint64_t value,
                               uint32_t ctr_idx)
{
    uint32_t ctr_mask;
    uint32_t event_idx;
    RISCVCPU *cpu = env_archcpu(env);

    if (!riscv_pmu_counter_valid(cpu, ctr_idx) || !cpu->pmu_event_ctr_map) {
        return -1;
    }

    pmu_remove_counter_from_event_map(cpu, ctr_idx);

    /*
     * Expected mhpmevent value is zero for reset case. Remove the current
     * mapping.
     */
    if (!(value & MHPMEVENT_IDX_MASK)) {
        return 0;
    }

    event_idx = value & MHPMEVENT_IDX_MASK;
    if (cpu->cfg.thead_c9xx_pmu) {
        if (event_idx > THEAD_C9XX_PMU_EVENT_MAX) {
            return -1;
        }
    } else {
        switch (event_idx) {
        case RISCV_PMU_EVENT_HW_CPU_CYCLES:
        case RISCV_PMU_EVENT_HW_INSTRUCTIONS:
        case RISCV_PMU_EVENT_CACHE_DTLB_READ_MISS:
        case RISCV_PMU_EVENT_CACHE_DTLB_WRITE_MISS:
        case RISCV_PMU_EVENT_CACHE_ITLB_PREFETCH_MISS:
            break;
        default:
            /* We don't support any raw events right now */
            return -1;
        }
    }

    ctr_mask = riscv_pmu_event_counter_mask(cpu, event_idx);

    g_hash_table_insert(cpu->pmu_event_ctr_map, GUINT_TO_POINTER(event_idx),
                        GUINT_TO_POINTER(ctr_mask | BIT(ctr_idx)));

    return 0;
}

static bool riscv_pmu_overflow_pending(RISCVCPU *cpu, uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;

    if (cpu->cfg.thead_c9xx_pmu) {
        return !!(env->th_mcounterof & BIT(ctr_idx));
    }

    return !!(env->mhpmevent_val[ctr_idx] & MHPMEVENT_BIT_OF);
}

static void pmu_timer_trigger_counter(RISCVCPU *cpu, uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;
    PMUCTRState *counter;
    int64_t irq_trigger_at;
    target_ulong curr_ctr_low, curr_ctr_high;
    uint64_t curr_ctr_val;
    uint64_t ctr_val;

    if (!riscv_pmu_counter_enabled(cpu, ctr_idx) ||
        (!riscv_pmu_ctr_monitor_cycles(env, ctr_idx) &&
         !riscv_pmu_ctr_monitor_instructions(env, ctr_idx))) {
        return;
    }

    /* Generate interrupt only if OF bit is clear */
    if (riscv_pmu_overflow_pending(cpu, ctr_idx)) {
        return;
    }

    counter = &env->pmu_ctrs[ctr_idx];
    if (counter->irq_overflow_left > 0) {
        int64_t curr_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        uint64_t delay = MIN(counter->irq_overflow_left,
                             (uint64_t)(INT64_MAX - curr_ns));

        irq_trigger_at = curr_ns + delay;
        timer_mod_anticipate_ns(cpu->pmu_timer, irq_trigger_at);
        counter->irq_overflow_left -= delay;
        return;
    }

    riscv_pmu_read_ctr(env, &curr_ctr_low, false, ctr_idx);
    curr_ctr_val = curr_ctr_low;
    ctr_val = counter->mhpmcounter_val;
    if (riscv_cpu_mxl(env) == MXL_RV32) {
        riscv_pmu_read_ctr(env, &curr_ctr_high, true, ctr_idx);
        curr_ctr_val |= (uint64_t)curr_ctr_high << 32;
    }

    /*
     * We can not accommodate for inhibited modes when setting up timer. Check
     * if the counter has actually overflowed or not by comparing current
     * counter value (accommodated for inhibited modes) with software written
     * counter value.
     */
    if (curr_ctr_val >= ctr_val) {
        riscv_pmu_setup_timer(env, curr_ctr_val, ctr_idx);
        return;
    }

    riscv_pmu_set_overflow(cpu, ctr_idx);
}

/* Timer callback for instret and cycle counter overflow */
void riscv_pmu_timer_cb(void *priv)
{
    RISCVCPU *cpu = priv;

    for (uint32_t ctr_idx = 0; ctr_idx < RV_MAX_MHPMCOUNTERS; ctr_idx++) {
        if (riscv_pmu_counter_present(cpu, ctr_idx)) {
            pmu_timer_trigger_counter(cpu, ctr_idx);
        }
    }
}

int riscv_pmu_setup_timer(CPURISCVState *env, uint64_t value, uint32_t ctr_idx)
{
    uint64_t overflow_delta, overflow_left = 0;
    int64_t curr_ns, overflow_at, overflow_ns;
    RISCVCPU *cpu = env_archcpu(env);
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];

    if (!cpu->pmu_timer || !riscv_pmu_counter_enabled(cpu, ctr_idx) ||
        (!cpu->cfg.ext_sscofpmf && !cpu->cfg.thead_c9xx_pmu) ||
        riscv_pmu_overflow_pending(cpu, ctr_idx)) {
        return -1;
    }

    if (value) {
        overflow_delta = UINT64_MAX - value + 1;
    } else {
        overflow_delta = UINT64_MAX;
    }

    /*
     * QEMU supports only int64_t timers while RISC-V counters are uint64_t.
     * Compute the leftover and save it so that it can be reprogrammed again
     * when timer expires.
     */
    if (overflow_delta > INT64_MAX) {
        overflow_left = overflow_delta - INT64_MAX;
        overflow_delta = INT64_MAX;
    }

    if (riscv_pmu_ctr_monitor_cycles(env, ctr_idx) ||
        riscv_pmu_ctr_monitor_instructions(env, ctr_idx)) {
        overflow_ns = pmu_icount_ticks_to_ns((int64_t)overflow_delta);
        overflow_left = pmu_icount_ticks_to_ns(
            MIN(overflow_left, (uint64_t)INT64_MAX));
    } else {
        return -1;
    }
    curr_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (overflow_ns < 0 || curr_ns > INT64_MAX - overflow_ns) {
        if (overflow_ns > 0) {
            overflow_left += overflow_ns - (INT64_MAX - curr_ns);
        }
        overflow_at = INT64_MAX;
    } else {
        overflow_at = curr_ns + overflow_ns;
    }
    counter->irq_overflow_left = overflow_left;
    timer_mod_anticipate_ns(cpu->pmu_timer, overflow_at);

    return 0;
}

static bool riscv_pmu_migration_counter_enabled(RISCVCPU *cpu,
                                                 uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;

    return (ctr_idx == 0 || ctr_idx == 2 ||
            riscv_pmu_counter_valid(cpu, ctr_idx)) &&
           !(env->mcountinhibit & BIT(ctr_idx));
}

void riscv_pmu_prepare_save(CPURISCVState *env)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint32_t counters = cpu->pmu_avail_ctrs | COUNTEREN_CY | COUNTEREN_IR;

    while (counters) {
        uint32_t ctr_idx = ctz32(counters);
        PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
        uint64_t current;

        counters &= ~BIT(ctr_idx);
        if (!riscv_pmu_migration_counter_enabled(cpu, ctr_idx) ||
            (!riscv_pmu_ctr_monitor_cycles(env, ctr_idx) &&
             !riscv_pmu_ctr_monitor_instructions(env, ctr_idx))) {
            continue;
        }

        /* Materialize the derived value and preserve source continuation. */
        current = riscv_pmu_ctr_get_fixed_counters_val(env, ctr_idx);
        counter->mhpmcounter_val += current - counter->mhpmcounter_prev;
        counter->mhpmcounter_prev = current;
    }
}

static void riscv_pmu_rebase_fixed_counters(CPURISCVState *env)
{
    uint64_t cycles;
    uint64_t instructions;

    if (icount_enabled()) {
        cycles = icount_get();
        instructions = icount_get_raw();
    } else {
        cycles = cpu_get_host_ticks();
        instructions = cycles;
    }

    memset(env->pmu_fixed_ctrs, 0, sizeof(env->pmu_fixed_ctrs));
    if (env->virt_enabled) {
        g_assert(env->priv <= PRV_S);
        env->pmu_fixed_ctrs[0].counter_virt_prev[env->priv] = cycles;
        env->pmu_fixed_ctrs[1].counter_virt_prev[env->priv] = instructions;
    } else {
        env->pmu_fixed_ctrs[0].counter_prev[env->priv] = cycles;
        env->pmu_fixed_ctrs[1].counter_prev[env->priv] = instructions;
    }
}

int riscv_pmu_post_load(CPURISCVState *env)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint32_t counters = cpu->pmu_avail_ctrs | COUNTEREN_CY | COUNTEREN_IR;

    if (!cpu->pmu_event_ctr_map) {
        return 0;
    }

    /* Reconstruct PMU state derived from the architectural CSR values. */
    g_hash_table_remove_all(cpu->pmu_event_ctr_map);
    for (uint32_t ctr_idx = 3; ctr_idx < RV_MAX_MHPMCOUNTERS; ctr_idx++) {
        if (!(cpu->pmu_avail_ctrs & BIT(ctr_idx))) {
            continue;
        }
        /* Unsupported raw selectors remain architectural state, not a map. */
        riscv_pmu_update_event_map(env, env->mhpmevent_val[ctr_idx], ctr_idx);
    }

    riscv_pmu_rebase_fixed_counters(env);
    if (cpu->pmu_timer) {
        timer_del(cpu->pmu_timer);
    }

    while (counters) {
        uint32_t ctr_idx = ctz32(counters);
        PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];

        counters &= ~BIT(ctr_idx);
        counter->irq_overflow_left = 0;
        if (!riscv_pmu_migration_counter_enabled(cpu, ctr_idx) ||
            (!riscv_pmu_ctr_monitor_cycles(env, ctr_idx) &&
             !riscv_pmu_ctr_monitor_instructions(env, ctr_idx))) {
            continue;
        }

        counter->mhpmcounter_prev =
            riscv_pmu_ctr_get_fixed_counters_val(env, ctr_idx);
        if (cpu->pmu_timer &&
            (ctr_idx > 2 || cpu->cfg.thead_c9xx_pmu) &&
            !riscv_pmu_overflow_pending(cpu, ctr_idx)) {
            riscv_pmu_setup_timer(env, counter->mhpmcounter_val, ctr_idx);
        }
    }

    return 0;
}

void riscv_pmu_init(RISCVCPU *cpu, Error **errp)
{
    if (cpu->cfg.pmu_mask & (COUNTEREN_CY | COUNTEREN_TM | COUNTEREN_IR)) {
        error_setg(errp, "\"pmu-mask\" contains invalid bits (0-2) set");
        return;
    }

    if (ctpop32(cpu->cfg.pmu_mask) > (RV_MAX_MHPMCOUNTERS - 3)) {
        error_setg(errp, "Number of counters exceeds maximum available");
        return;
    }

    cpu->pmu_event_ctr_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (!cpu->pmu_event_ctr_map) {
        error_setg(errp, "Unable to allocate PMU event hash table");
        return;
    }

    cpu->pmu_avail_ctrs = cpu->cfg.pmu_mask;
}
