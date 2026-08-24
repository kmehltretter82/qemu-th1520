/*
 * T-Head-specific CSRs.
 *
 * Copyright (c) 2024 VRULL GmbH
 * Copyright (c) 2025 Chao Liu <chao.liu.zevorn@gmail.com>
 *
 * For more information, see XuanTie-C908-UserManual_xrvm_20240530.pdf
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
#include "cpu.h"
#include "cpu_vendorid.h"
#include "exec/cputlb.h"
#include "target/riscv/tcg/csr.h"
#include "target/riscv/tcg/pmu.h"

/* Extended M-mode control registers of T-Head */
#define CSR_TH_MXSTATUS        0x7c0
#define CSR_TH_MHCR            0x7c1
#define CSR_TH_MCOR            0x7c2
#define CSR_TH_MCCR2           0x7c3
#define CSR_TH_MHINT           0x7c5
#define CSR_TH_MRVBR           0x7c7
#define CSR_TH_MCOUNTERWEN     0x7c9
#define CSR_TH_MCOUNTERINTEN   0x7ca
#define CSR_TH_MCOUNTEROF      0x7cb
#define CSR_TH_MHINT2          0x7cc
#define CSR_TH_MHINT3          0x7cd
#define CSR_TH_MHINT4          0x7ce
#define CSR_TH_MCINS           0x7d2
#define CSR_TH_MCINDEX         0x7d3
#define CSR_TH_MCDATA0         0x7d4
#define CSR_TH_MCDATA1         0x7d5
#define CSR_TH_MSMPR           0x7f3
#define CSR_TH_CPUID           0xfc0
#define CSR_TH_MAPBADDR        0xfc1

/* TH_MXSTATUS bits */
#define TH_MXSTATUS_UCME        BIT(16)
#define TH_MXSTATUS_CLINTEE     BIT(17)
#define TH_MXSTATUS_INSDE       BIT(19)
#define TH_MXSTATUS_MHRD        BIT(18)
/* Extended S-mode control registers of T-Head */
#define CSR_TH_SXSTATUS        0x5c0
#define CSR_TH_SHCR            0x5c1
#define CSR_TH_SCER2           0x5c2
#define CSR_TH_SCER            0x5c3
#define CSR_TH_SCOUNTERINTEN   0x5c4
#define CSR_TH_SCOUNTEROF      0x5c5
#define CSR_TH_SCYCLE          0x5e0
#define CSR_TH_SHPMCOUNTER3    0x5e3
#define CSR_TH_SHPMCOUNTER4    0x5e4
#define CSR_TH_SHPMCOUNTER5    0x5e5
#define CSR_TH_SHPMCOUNTER6    0x5e6
#define CSR_TH_SHPMCOUNTER7    0x5e7
#define CSR_TH_SHPMCOUNTER8    0x5e8
#define CSR_TH_SHPMCOUNTER9    0x5e9
#define CSR_TH_SHPMCOUNTER10   0x5ea
#define CSR_TH_SHPMCOUNTER11   0x5eb
#define CSR_TH_SHPMCOUNTER12   0x5ec
#define CSR_TH_SHPMCOUNTER13   0x5ed
#define CSR_TH_SHPMCOUNTER14   0x5ee
#define CSR_TH_SHPMCOUNTER15   0x5ef
#define CSR_TH_SHPMCOUNTER16   0x5f0
#define CSR_TH_SHPMCOUNTER17   0x5f1
#define CSR_TH_SHPMCOUNTER18   0x5f2
#define CSR_TH_SHPMCOUNTER19   0x5f3
#define CSR_TH_SHPMCOUNTER20   0x5f4
#define CSR_TH_SHPMCOUNTER21   0x5f5
#define CSR_TH_SHPMCOUNTER22   0x5f6
#define CSR_TH_SHPMCOUNTER23   0x5f7
#define CSR_TH_SHPMCOUNTER24   0x5f8
#define CSR_TH_SHPMCOUNTER25   0x5f9
#define CSR_TH_SHPMCOUNTER26   0x5fa
#define CSR_TH_SHPMCOUNTER27   0x5fb
#define CSR_TH_SHPMCOUNTER28   0x5fc
#define CSR_TH_SHPMCOUNTER29   0x5fd
#define CSR_TH_SHPMCOUNTER30   0x5fe
#define CSR_TH_SHPMCOUNTER31   0x5ff
#define CSR_TH_SMIR            0x9c0
#define CSR_TH_SMLO0           0x9c1
#define CSR_TH_SMEH            0x9c2
#define CSR_TH_SMCIR           0x9c3

/* Extended U-mode control registers of T-Head */
#define CSR_TH_FXCR            0x800

/* TH_SXSTATUS bits */
#define TH_SXSTATUS_UCME        BIT(16)
#define TH_SXSTATUS_MAEE        BIT(21)
#define TH_SXSTATUS_THEADISAEE  BIT(22)

static RISCVException mmode(CPURISCVState *env, int csrno)
{
    return RISCV_EXCP_NONE;
}

static RISCVException smode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException any(CPURISCVState *env, int csrno)
{
    return RISCV_EXCP_NONE;
}

static bool test_thead_mvendorid(RISCVCPU *cpu)
{
    return cpu->cfg.mvendorid == THEAD_VENDOR_ID;
}

static RISCVException read_th_mxstatus(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    /* This legacy generic T-Head CSR surface does not model MAEE. */
    *val = TH_MXSTATUS_UCME | THEAD_MXSTATUS_THEADISAEE;
    return RISCV_EXCP_NONE;
}

static RISCVException read_unimp_th_csr(CPURISCVState *env, int csrno,
                                        target_ulong *val)
{
    *val = 0;
    return RISCV_EXCP_NONE;
}

static RISCVException read_th_sxstatus(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    /* This legacy generic T-Head CSR surface does not model MAEE. */
    *val = TH_SXSTATUS_UCME | TH_SXSTATUS_THEADISAEE;
    return RISCV_EXCP_NONE;
}

/*
 * C910 values below are derived from openC910 RTL.  TH1520 integration and
 * silicon-stepping differences remain tracked in the BeagleV Ahead hardware
 * validation ledger.
 */
#define TH_C910_MXSTATUS_WRITABLE \
    (THEAD_MXSTATUS_THEADISAEE | THEAD_MXSTATUS_MAEE | \
     TH_MXSTATUS_INSDE | TH_MXSTATUS_MHRD | TH_MXSTATUS_CLINTEE | \
     TH_MXSTATUS_UCME | THEAD_MXSTATUS_MM | THEAD_MXSTATUS_PMDM | \
     THEAD_MXSTATUS_PMDS | THEAD_MXSTATUS_PMDU)
#define TH_C910_MXSTATUS_RESET \
    (THEAD_MXSTATUS_THEADISAEE | THEAD_MXSTATUS_MAEE | \
     TH_MXSTATUS_CLINTEE | TH_MXSTATUS_UCME | THEAD_MXSTATUS_MM)
#define TH_C910_SXSTATUS_WRITABLE \
    (THEAD_MXSTATUS_MM | THEAD_MXSTATUS_PMDS | THEAD_MXSTATUS_PMDU)

#define TH_C910_MHCR_FIXED       (BIT(8) | BIT(3))
#define TH_C910_MHCR_WRITABLE \
    (BIT(12) | BIT(7) | BIT(6) | BIT(5) | BIT(4) | \
     BIT(2) | BIT(1) | BIT(0))

#define TH_C910_MHINT_WRITABLE \
    (MAKE_64BIT_MASK(21, 4) | BIT(18) | MAKE_64BIT_MASK(16, 2) | \
     BIT(15) | MAKE_64BIT_MASK(13, 2) | MAKE_64BIT_MASK(8, 4) | \
     BIT(5) | BIT(3) | BIT(2))
#define TH_C910_MHINT_RESET      (BIT(17) | BIT(14))
#define TH_C910_MHINT2_WRITABLE \
    (MAKE_64BIT_MASK(32, 2) | MAKE_64BIT_MASK(26, 6) | \
     MAKE_64BIT_MASK(14, 9) | MAKE_64BIT_MASK(9, 5) | \
     MAKE_64BIT_MASK(0, 5))
#define TH_C910_MHINT3_WRITABLE  MAKE_64BIT_MASK(0, 30)
#define TH_C910_MHINT3_RESET     0x10404040
#define TH_C910_MCOUNTERWEN_WRITABLE \
    (BIT(0) | MAKE_64BIT_MASK(2, 30))

static const uint32_t th1520_c910_cpuid[] = {
    0x090c090d, 0x110c9000, 0x260c0001, 0x30530077,
    0x42080407, 0x50000003, 0x60000a53,
};

void riscv_thead_c910_csr_reset(CPURISCVState *env)
{
    env->th_mxstatus = TH_C910_MXSTATUS_RESET;
    env->th_mhcr = TH_C910_MHCR_FIXED;
    env->th_mcor = 0;
    env->th_mhint = TH_C910_MHINT_RESET;
    env->th_mhint2 = 0;
    env->th_mhint3 = TH_C910_MHINT3_RESET;
    env->th_mcounterwen = 0;
    env->th_mcounterinten = 0;
    env->th_mcounterof = 0;
    env->th_cpuid_index = 0;
}

static bool test_thead_c910(RISCVCPU *cpu)
{
    return object_dynamic_cast(OBJECT(cpu), TYPE_RISCV_CPU_THEAD_C910);
}

static RISCVException read_c910_mxstatus(CPURISCVState *env, int csrno,
                                         target_ulong *val)
{
    *val = env->th_mxstatus | ((target_ulong)env->priv << 30);
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_mxstatus(CPURISCVState *env, int csrno,
                                          target_ulong val, uintptr_t ra)
{
    uint64_t old = env->th_mxstatus;

    env->th_mxstatus = (old & ~TH_C910_MXSTATUS_WRITABLE) |
                       (val & TH_C910_MXSTATUS_WRITABLE);
    if ((old ^ env->th_mxstatus) & THEAD_MXSTATUS_MAEE) {
        tlb_flush(env_cpu(env));
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_sxstatus(CPURISCVState *env, int csrno,
                                         target_ulong *val)
{
    return read_c910_mxstatus(env, csrno, val);
}

static RISCVException write_c910_sxstatus(CPURISCVState *env, int csrno,
                                          target_ulong val, uintptr_t ra)
{
    env->th_mxstatus = (env->th_mxstatus & ~TH_C910_SXSTATUS_WRITABLE) |
                       (val & TH_C910_SXSTATUS_WRITABLE);
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_mhcr(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    *val = env->th_mhcr;
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_mhcr(CPURISCVState *env, int csrno,
                                      target_ulong val, uintptr_t ra)
{
    env->th_mhcr = TH_C910_MHCR_FIXED | (val & TH_C910_MHCR_WRITABLE);
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_mcor(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    *val = env->th_mcor;
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_mcor(CPURISCVState *env, int csrno,
                                      target_ulong val, uintptr_t ra)
{
    /* Cache and branch-predictor commands complete synchronously in QEMU. */
    env->th_mcor = val & 0x3;
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_mhint(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = env->th_mhint;
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_mhint(CPURISCVState *env, int csrno,
                                       target_ulong val, uintptr_t ra)
{
    env->th_mhint = val & TH_C910_MHINT_WRITABLE;
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_mhint2(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    *val = env->th_mhint2;
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_mhint2(CPURISCVState *env, int csrno,
                                        target_ulong val, uintptr_t ra)
{
    env->th_mhint2 = val & TH_C910_MHINT2_WRITABLE;
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_mhint3(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    *val = env->th_mhint3;
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_mhint3(CPURISCVState *env, int csrno,
                                        target_ulong val, uintptr_t ra)
{
    env->th_mhint3 = val & TH_C910_MHINT3_WRITABLE;
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_mrvbr(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = env->resetvec & MAKE_64BIT_MASK(1, 39);
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_mcounterwen(CPURISCVState *env, int csrno,
                                            target_ulong *val)
{
    *val = env->th_mcounterwen;
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_mcounterwen(CPURISCVState *env, int csrno,
                                             target_ulong val, uintptr_t ra)
{
    env->th_mcounterwen = val & TH_C910_MCOUNTERWEN_WRITABLE;
    return RISCV_EXCP_NONE;
}

static void c910_reschedule_cleared_overflows(CPURISCVState *env,
                                               uint32_t cleared)
{
    while (cleared) {
        uint32_t ctr_idx = ctz32(cleared);
        target_ulong value;

        cleared &= ~BIT(ctr_idx);
        if (riscv_pmu_read_ctr(env, &value, false, ctr_idx) ==
            RISCV_EXCP_NONE) {
            riscv_pmu_setup_timer(env, value, ctr_idx);
        }
    }
}

static RISCVException read_c910_mcounterinten(CPURISCVState *env, int csrno,
                                               target_ulong *val)
{
    *val = env->th_mcounterinten;
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_mcounterinten(CPURISCVState *env, int csrno,
                                                target_ulong val,
                                                uintptr_t ra)
{
    env->th_mcounterinten = val & TH_C910_MCOUNTERWEN_WRITABLE;
    riscv_pmu_thead_c9xx_update_irq(env);
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_mcounterof(CPURISCVState *env, int csrno,
                                            target_ulong *val)
{
    *val = env->th_mcounterof;
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_mcounterof(CPURISCVState *env, int csrno,
                                             target_ulong val, uintptr_t ra)
{
    uint32_t old = env->th_mcounterof;

    env->th_mcounterof = val & TH_C910_MCOUNTERWEN_WRITABLE;
    riscv_pmu_thead_c9xx_update_irq(env);
    c910_reschedule_cleared_overflows(env, old & ~env->th_mcounterof);
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_scounterinten(CPURISCVState *env, int csrno,
                                               target_ulong *val)
{
    *val = env->th_mcounterinten & env->th_mcounterwen;
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_scounterinten(CPURISCVState *env, int csrno,
                                                target_ulong val,
                                                uintptr_t ra)
{
    uint32_t mask = env->th_mcounterwen & TH_C910_MCOUNTERWEN_WRITABLE;

    env->th_mcounterinten = (env->th_mcounterinten & ~mask) | (val & mask);
    riscv_pmu_thead_c9xx_update_irq(env);
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_scounterof(CPURISCVState *env, int csrno,
                                            target_ulong *val)
{
    *val = env->th_mcounterof & env->th_mcounterwen;
    return RISCV_EXCP_NONE;
}

static RISCVException write_c910_scounterof(CPURISCVState *env, int csrno,
                                             target_ulong val, uintptr_t ra)
{
    uint32_t mask = env->th_mcounterwen & TH_C910_MCOUNTERWEN_WRITABLE;
    uint32_t old = env->th_mcounterof;

    env->th_mcounterof = (env->th_mcounterof & ~mask) | (val & mask);
    riscv_pmu_thead_c9xx_update_irq(env);
    c910_reschedule_cleared_overflows(env, old & ~env->th_mcounterof);
    return RISCV_EXCP_NONE;
}

static RISCVException read_c910_cpuid(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = th1520_c910_cpuid[env->th_cpuid_index];
    env->th_cpuid_index = (env->th_cpuid_index + 1) %
                          ARRAY_SIZE(th1520_c910_cpuid);
    return RISCV_EXCP_NONE;
}

const RISCVCSR th_csr_list[] = {
    {
        .csrno = CSR_TH_MXSTATUS,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mxstatus", mmode, read_th_mxstatus }
    },
    {
        .csrno = CSR_TH_MHCR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mhcr", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCOR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcor", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCCR2,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mccr2", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MHINT,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mhint", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MRVBR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mrvbr", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCOUNTERWEN,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcounterwen", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCOUNTERINTEN,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcounterinten", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCOUNTEROF,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcounterof", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCINS,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcins", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCINDEX,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcindex", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCDATA0,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcdata0", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCDATA1,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcdata1", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MSMPR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.msmpr", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_CPUID,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.cpuid", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MAPBADDR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mapbaddr", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SXSTATUS,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.sxstatus", smode, read_th_sxstatus }
    },
    {
        .csrno = CSR_TH_SHCR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shcr", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCER2,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.scer2", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCER,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.scer", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCOUNTERINTEN,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.scounterinten", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCOUNTEROF,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.scounterof", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCYCLE,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.scycle", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER3,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter3", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER4,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter4", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER5,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter5", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER6,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter6", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER7,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter7", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER8,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter8", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER9,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter9", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER10,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter10", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER11,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter11", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER12,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter12", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER13,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter13", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER14,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter14", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER15,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter15", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER16,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter16", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER17,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter17", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER18,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter18", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER19,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter19", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER20,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter20", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER21,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter21", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER22,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter22", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER23,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter23", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER24,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter24", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER25,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter25", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER26,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter26", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER27,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter27", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER28,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter28", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER29,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter29", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER30,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter30", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER31,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter31", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMIR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.smir", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMLO0,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.smlo0", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMEH,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.smeh", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMCIR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.smcir", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_FXCR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.fxcr", any, read_unimp_th_csr }
    },
    { }
};

const RISCVCSR th_c910_csr_list[] = {
    {
        .csrno = CSR_TH_MXSTATUS,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mxstatus", mmode, read_c910_mxstatus,
                     write_c910_mxstatus }
    },
    {
        .csrno = CSR_TH_MHCR,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mhcr", mmode, read_c910_mhcr,
                     write_c910_mhcr }
    },
    {
        .csrno = CSR_TH_MCOR,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mcor", mmode, read_c910_mcor,
                     write_c910_mcor }
    },
    {
        .csrno = CSR_TH_MCCR2,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mccr2", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MHINT,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mhint", mmode, read_c910_mhint,
                     write_c910_mhint }
    },
    {
        .csrno = CSR_TH_MRVBR,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mrvbr", mmode, read_c910_mrvbr }
    },
    {
        .csrno = CSR_TH_MCOUNTERWEN,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mcounterwen", mmode,
                     read_c910_mcounterwen, write_c910_mcounterwen }
    },
    {
        .csrno = CSR_TH_MCOUNTERINTEN,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mcounterinten", mmode,
                     read_c910_mcounterinten, write_c910_mcounterinten }
    },
    {
        .csrno = CSR_TH_MCOUNTEROF,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mcounterof", mmode,
                     read_c910_mcounterof, write_c910_mcounterof }
    },
    {
        .csrno = CSR_TH_MHINT2,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mhint2", mmode, read_c910_mhint2,
                     write_c910_mhint2 }
    },
    {
        .csrno = CSR_TH_MHINT3,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mhint3", mmode, read_c910_mhint3,
                     write_c910_mhint3 }
    },
    {
        .csrno = CSR_TH_MHINT4,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mhint4", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCINS,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mcins", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCINDEX,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mcindex", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCDATA0,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mcdata0", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCDATA1,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mcdata1", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MSMPR,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.msmpr", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_CPUID,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.cpuid", mmode, read_c910_cpuid }
    },
    {
        .csrno = CSR_TH_MAPBADDR,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.mapbaddr", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SXSTATUS,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.sxstatus", smode, read_c910_sxstatus,
                     write_c910_sxstatus }
    },
    {
        .csrno = CSR_TH_SHCR,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.shcr", smode, read_c910_mhcr }
    },
    {
        .csrno = CSR_TH_SCER2,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.scer2", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCER,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.scer", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCOUNTERINTEN,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.scounterinten", smode,
                     read_c910_scounterinten, write_c910_scounterinten }
    },
    {
        .csrno = CSR_TH_SCOUNTEROF,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.scounterof", smode,
                     read_c910_scounterof, write_c910_scounterof }
    },
    {
        .csrno = CSR_TH_SMIR,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.smir", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMLO0,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.smel", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMEH,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.smeh", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMCIR,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.smcir", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_FXCR,
        .insertion_test = test_thead_c910,
        .csr_ops = { "th.c910.fxcr", any, read_unimp_th_csr }
    },
    { }
};
