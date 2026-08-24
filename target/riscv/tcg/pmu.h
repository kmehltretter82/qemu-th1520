/*
 * RISC-V PMU header file.
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

#ifndef RISCV_PMU_H
#define RISCV_PMU_H

#include "cpu.h"
#include "qapi/error.h"

/* Raw event selector values implemented by the open C910 PMU. */
#define THEAD_C9XX_PMU_EVENT_ICACHE_ACCESS       1
#define THEAD_C9XX_PMU_EVENT_ICACHE_MISS         2
#define THEAD_C9XX_PMU_EVENT_ITLB_MISS           3
#define THEAD_C9XX_PMU_EVENT_DTLB_MISS           4
#define THEAD_C9XX_PMU_EVENT_INSTRUCTIONS        22
#define THEAD_C9XX_PMU_EVENT_MAX                 42

bool riscv_pmu_ctr_monitor_instructions(CPURISCVState *env,
                                        uint32_t target_ctr);
bool riscv_pmu_ctr_monitor_cycles(CPURISCVState *env,
                                  uint32_t target_ctr);
uint64_t riscv_pmu_ctr_get_fixed_counters_val(CPURISCVState *env,
                                               int counter_idx);
void riscv_pmu_timer_cb(void *priv);
void riscv_pmu_init(RISCVCPU *cpu, Error **errp);
int riscv_pmu_update_event_map(CPURISCVState *env, uint64_t value,
                               uint32_t ctr_idx);
int riscv_pmu_incr_ctr(RISCVCPU *cpu, uint32_t event_idx);
void riscv_pmu_generate_fdt_node(void *fdt, uint32_t cmask, char *pmu_name);
int riscv_pmu_setup_timer(CPURISCVState *env, uint64_t value,
                          uint32_t ctr_idx);
void riscv_pmu_thead_c9xx_update_irq(CPURISCVState *env);
void riscv_pmu_update_fixed_ctrs(CPURISCVState *env, privilege_mode_t newpriv,
                                 bool new_virt);
void riscv_pmu_decr_instret(CPURISCVState *env);
void riscv_pmu_prepare_save(CPURISCVState *env);
int riscv_pmu_post_load(CPURISCVState *env);
RISCVException riscv_pmu_read_ctr(CPURISCVState *env, target_ulong *val,
                                  bool upper_half, uint32_t ctr_idx);

#endif /* RISCV_PMU_H */
