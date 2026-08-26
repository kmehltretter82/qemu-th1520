/*
 * QTest testcase for RISC-V CSRs
 *
 * Copyright (c) 2024 Syntacore.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"
#include "migration/riscv64/c910-fxcr-legacy-migration-guest.h"
#include "migration/riscv64/c910-fxcr-migration-guest.h"
#include "migration/riscv64/c910-inhibited-pmu-guest.h"
#include "migration/riscv64/c910-pending-pmu-guest.h"
#include "migration/riscv64/c910-pmu-guest.h"
#include "migration/riscv64/riscv-fixed-pmu-guest.h"
#include "migration/riscv64/riscv-inhibited-pmu-guest.h"
#include "migration/riscv64/riscv-pending-pmu-guest.h"
#include "migration/riscv64/riscv-pmu-guest.h"

#define CSR_MVENDORID       0xf11
#define CSR_MARCHID         0xf12
#define CSR_MIMPID          0xf13
#define CSR_FFLAGS          0x001
#define CSR_FRM             0x002
#define CSR_FCSR            0x003
#define CSR_MISELECT        0x350
#define CSR_MSTATUS         0x300
#define CSR_MISA            0x301
#define CSR_MIDELEG         0x303
#define CSR_MIE             0x304
#define CSR_MCOUNTINHIBIT   0x320
#define CSR_MIP             0x344
#define CSR_MHPMEVENT3      0x323
#define CSR_SIE             0x104
#define CSR_SIP             0x144
#define CSR_SATP            0x180
#define CSR_VSTART          0x008
#define CSR_VXSAT           0x009
#define CSR_VXRM            0x00a
#define CSR_VL              0xc20
#define CSR_VTYPE           0xc21
#define CSR_VLENB           0xc22
#define CSR_TH_SXSTATUS     0x5c0
#define CSR_TH_MXSTATUS     0x7c0
#define CSR_TH_MHCR         0x7c1
#define CSR_TH_MCOR         0x7c2
#define CSR_TH_MHINT        0x7c5
#define CSR_TH_MRVBR        0x7c7
#define CSR_TH_MCOUNTERWEN  0x7c9
#define CSR_TH_MCOUNTERINTEN 0x7ca
#define CSR_TH_MCOUNTEROF   0x7cb
#define CSR_TH_MHINT2       0x7cc
#define CSR_TH_MHINT3       0x7cd
#define CSR_TH_SCOUNTERINTEN 0x5c4
#define CSR_TH_SCOUNTEROF   0x5c5
#define CSR_TH_FXCR         0x800
#define CSR_TH_CPUID        0xfc0

#define MSTATUS_FS_INITIAL  (UINT64_C(1) << 13)
#define MSTATUS_FS_MASK     (UINT64_C(3) << 13)

#define PMU_TEST_ARM_OFFSET  8
#define PMU_TEST_GO_OFFSET   16
#define PMU_TEST_MIGRATED_OFFSET 56
#define PMU_TEST_READY       1
#define PMU_TEST_ARMED       2
#define PMU_TEST_PASS        3
#define LCOFI_BIT             (1ULL << 13)

#define FXCR_TEST_GO_OFFSET      8
#define FXCR_TEST_RESET_OFFSET  16
#define FXCR_TEST_READY          1
#define FXCR_TEST_MIGRATION_PASS 2
#define FXCR_TEST_RESET_PASS     3
#define FXCR_DQNAN               (UINT64_C(1) << 23)
#define FXCR_FE                  (UINT64_C(1) << 5)
#define FFLAGS_NV                (UINT64_C(1) << 4)
#define FFLAGS_NX                (UINT64_C(1) << 0)
#define FXCR_FRM_SHIFT           24

#define C910_TEST_HARTS                  4
#define C910_CSR_VMSTATE_NAME            "cpu/thead-c910-csr"
#define C910_CSR_VMSTATE_V3_PAYLOAD_SIZE 64
#define C910_CSR_VMSTATE_V2_PAYLOAD_SIZE 61
#define C910_CSR_VMSTATE_V1_PAYLOAD_SIZE 53
#define C910_VM_SECTION_FULL_MARKER       0x04
#define C910_VM_SUBSECTION_MARKER        0x05
#define RISCV_CPU_VMSTATE_NAME            "cpu"
#define RISCV_CPU_VMSTATE_V12             12
#define RISCV_CPU_VMSTATE_V11             11
#define C910_PMU_OVF_BIT                 (UINT64_C(1) << 17)

typedef struct PMUMigrationTest {
    const char *machine_args;
    const uint8_t *guest;
    size_t guest_size;
    uint64_t load_addr;
    uint64_t status_addr;
    bool mark_migrated;
} PMUMigrationTest;

static const PMUMigrationTest riscv_pmu_migration = {
    .machine_args = "-machine virt "
                    "-cpu rv64,pmu-mask=0x8,sscofpmf=true,smcntrpmf=true "
                    "-smp 1 -bios none",
    .guest = riscv_pmu_guest_bin,
    .guest_size = sizeof(riscv_pmu_guest_bin),
    .load_addr = 0x80000000,
    .status_addr = 0x80200000,
};

static const PMUMigrationTest riscv_fixed_pmu_migration = {
    .machine_args = "-machine virt "
                    "-cpu rv64,pmu-mask=0,smcntrpmf=true "
                    "-smp 1 -bios none",
    .guest = riscv_fixed_pmu_guest_bin,
    .guest_size = sizeof(riscv_fixed_pmu_guest_bin),
    .load_addr = 0x80000000,
    .status_addr = 0x80200000,
    .mark_migrated = true,
};

static const PMUMigrationTest riscv_inhibited_pmu_migration = {
    .machine_args = "-machine virt "
                    "-cpu rv64,pmu-mask=0x8 -smp 1 -bios none",
    .guest = riscv_inhibited_pmu_guest_bin,
    .guest_size = sizeof(riscv_inhibited_pmu_guest_bin),
    .load_addr = 0x80000000,
    .status_addr = 0x80200000,
};

static const PMUMigrationTest riscv_pending_pmu_migration = {
    .machine_args = "-machine virt "
                    "-cpu rv64,pmu-mask=0x8,sscofpmf=true "
                    "-smp 1 -bios none",
    .guest = riscv_pending_pmu_guest_bin,
    .guest_size = sizeof(riscv_pending_pmu_guest_bin),
    .load_addr = 0x80000000,
    .status_addr = 0x80200000,
};

static const PMUMigrationTest c910_pmu_migration = {
    .machine_args = "-machine beaglev-ahead -bios none",
    .guest = c910_pmu_guest_bin,
    .guest_size = sizeof(c910_pmu_guest_bin),
    .load_addr = 0,
    .status_addr = 0x200000,
};

static const PMUMigrationTest c910_inhibited_pmu_migration = {
    .machine_args = "-machine beaglev-ahead -bios none",
    .guest = c910_inhibited_pmu_guest_bin,
    .guest_size = sizeof(c910_inhibited_pmu_guest_bin),
    .load_addr = 0,
    .status_addr = 0x200000,
};

static const PMUMigrationTest c910_pending_pmu_migration = {
    .machine_args = "-machine beaglev-ahead -bios none",
    .guest = c910_pending_pmu_guest_bin,
    .guest_size = sizeof(c910_pending_pmu_guest_bin),
    .load_addr = 0,
    .status_addr = 0x200000,
};

static uint64_t get_csr_hart(QTestState *qts, uint32_t hart, uint32_t csrno)
{
    uint64_t val = 0;

    g_assert_cmpint(qtest_csr_call(qts, "get_csr", hart, csrno, &val), ==,
                    0);
    return val;
}

static void set_csr_hart(QTestState *qts, uint32_t hart, uint32_t csrno,
                         uint64_t val)
{
    g_assert_cmpint(qtest_csr_call(qts, "set_csr", hart, csrno, &val), ==,
                    0);
}

static uint64_t get_csr(QTestState *qts, uint32_t csrno)
{
    return get_csr_hart(qts, 0, csrno);
}

static void set_csr(QTestState *qts, uint32_t csrno, uint64_t val)
{
    set_csr_hart(qts, 0, csrno, val);
}

static uint64_t raise_fp_exception(QTestState *qts, uint64_t flags)
{
    uint64_t val = flags;

    g_assert_cmpint(qtest_csr_call(qts, "raise_fp_exception", 0, 0, &val),
                    ==, 0);
    return val;
}

static void assert_csr_mask(QTestState *qts, uint32_t csrno, uint64_t mask,
                            uint64_t expected)
{
    g_assert_cmphex(get_csr(qts, csrno) & mask, ==, expected);
}

static void wait_for_migration_complete(QTestState *qts)
{
    int64_t deadline = g_get_monotonic_time() + 30 * G_USEC_PER_SEC;

    while (g_get_monotonic_time() < deadline) {
        QDict *result = qtest_qmp_assert_success_ref(
            qts, "{ 'execute': 'query-migrate' }");
        const char *status = qdict_get_str(result, "status");

        if (!strcmp(status, "completed")) {
            qobject_unref(result);
            return;
        }
        g_assert_cmpstr(status, !=, "failed");
        qobject_unref(result);
        g_usleep(10000);
    }
    g_error("migration did not complete within 30 seconds");
}

static uint64_t wait_for_guest_status_change(QTestState *qts,
                                             uint64_t status_addr,
                                             uint64_t old_status)
{
    int64_t deadline = g_get_monotonic_time() + 30 * G_USEC_PER_SEC;
    uint64_t status = old_status;

    while (g_get_monotonic_time() < deadline) {
        status = qtest_readq(qts, status_addr);
        if (status != old_status) {
            return status;
        }
        g_usleep(1000);
    }
    g_error("guest status did not change from 0x%" PRIx64, old_status);
}

static bool vmstate_subsection_header_at(const uint8_t *data, gsize size,
                                         gsize offset, const char *name,
                                         uint32_t version)
{
    gsize name_len = strlen(name);
    gsize header_size = 2 + name_len + sizeof(uint32_t);

    if (name_len > UINT8_MAX || offset > size ||
        header_size > size - offset) {
        return false;
    }

    return data[offset] == C910_VM_SUBSECTION_MARKER &&
           data[offset + 1] == name_len &&
           !memcmp(data + offset + 2, name, name_len) &&
           ldl_be_p(data + offset + 2 + name_len) == version;
}

static bool vmstate_full_header_at(const uint8_t *data, gsize size,
                                   gsize offset, const char *name,
                                   uint32_t instance_id, uint32_t version)
{
    gsize name_len = strlen(name);
    gsize instance_offset = offset + 1 + sizeof(uint32_t) + 1 + name_len;
    gsize version_offset = instance_offset + sizeof(uint32_t);
    gsize header_size = 1 + sizeof(uint32_t) + 1 + name_len +
                        2 * sizeof(uint32_t);

    if (name_len > UINT8_MAX || offset > size ||
        header_size > size - offset) {
        return false;
    }

    return data[offset] == C910_VM_SECTION_FULL_MARKER &&
           data[offset + 1 + sizeof(uint32_t)] == name_len &&
           !memcmp(data + offset + 1 + sizeof(uint32_t) + 1,
                   name, name_len) &&
           ldl_be_p(data + instance_offset) == instance_id &&
           ldl_be_p(data + version_offset) == version;
}

/*
 * Turn the current C910 CSR subsection into the exact historical wire shape.
 * The fixed layouts come from the historical v1/v2 descriptors and the
 * current v3 descriptor.  C910 CSR version 1 predates the parent RISC-V CPU
 * VMState version 12, whose payload is unchanged from version 11, so lower
 * those four parent section headers as well.  This is valid only for the
 * ordinary sequential file stream used here: VMState subsections have no
 * payload length or checksum.  VMDESC is suppressed so the file contains no
 * stale current-version JSON.
 */
static void downgrade_c910_csr_subsections(const char *path,
                                           uint32_t target_version)
{
    const gsize name_len = strlen(C910_CSR_VMSTATE_NAME);
    const gsize header_size = 2 + name_len + sizeof(uint32_t);
    g_autofree char *contents = NULL;
    g_autoptr(GByteArray) stream = NULL;
    g_autoptr(GArray) offsets = g_array_new(false, false, sizeof(gsize));
    g_autoptr(GError) error = NULL;
    gsize original_size;
    gsize size;
    guint target_count = 0;
    guint current_count = 0;

    g_assert_true(target_version == 1 || target_version == 2);
    g_assert_true(g_file_get_contents(path, &contents, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(size, >=, 8);
    g_assert_cmpuint(size, <=, G_MAXUINT);
    g_assert_cmpmem(contents, 4, "QEVM", 4);
    g_assert_cmpuint(ldl_be_p(contents + 4), ==, 3);

    stream = g_byte_array_new_take(
        (guint8 *)g_steal_pointer(&contents), size);
    original_size = stream->len;

    for (uint32_t hart = 0; hart < C910_TEST_HARTS; hart++) {
        guint header_count = 0;

        for (gsize offset = 0; offset < stream->len; offset++) {
            gsize version_offset = offset + 1 + sizeof(uint32_t) + 1 +
                                   strlen(RISCV_CPU_VMSTATE_NAME) +
                                   sizeof(uint32_t);

            if (!vmstate_full_header_at(stream->data, stream->len, offset,
                                        RISCV_CPU_VMSTATE_NAME, hart,
                                        RISCV_CPU_VMSTATE_V12)) {
                continue;
            }
            header_count++;
            if (target_version == 1) {
                stl_be_p(stream->data + version_offset,
                         RISCV_CPU_VMSTATE_V11);
            }
        }
        g_assert_cmpuint(header_count, ==, 1);
    }

    for (gsize offset = 0; offset + header_size <= stream->len; offset++) {
        if (!vmstate_subsection_header_at(stream->data, stream->len, offset,
                                          C910_CSR_VMSTATE_NAME, 3)) {
            continue;
        }
        g_array_append_val(offsets, offset);
    }
    g_assert_cmpuint(offsets->len, ==, C910_TEST_HARTS);

    /* Work backwards so byte removal cannot invalidate earlier offsets. */
    for (guint i = offsets->len; i > 0; i--) {
        gsize offset = g_array_index(offsets, gsize, i - 1);
        gsize version_offset = offset + 2 + name_len;
        gsize payload = offset + header_size;

        stl_be_p(stream->data + version_offset, target_version);
        g_byte_array_remove_range(stream,
                                  payload + C910_CSR_VMSTATE_V2_PAYLOAD_SIZE,
                                  C910_CSR_VMSTATE_V3_PAYLOAD_SIZE -
                                  C910_CSR_VMSTATE_V2_PAYLOAD_SIZE);
        if (target_version == 1) {
            /* Retain cpuid_index while deleting the two v2 u32 fields. */
            g_byte_array_remove_range(stream, payload + 52, 8);
        }
    }

    g_assert_cmpuint(stream->len, ==,
        original_size - C910_TEST_HARTS *
        (C910_CSR_VMSTATE_V3_PAYLOAD_SIZE -
         (target_version == 1 ? C910_CSR_VMSTATE_V1_PAYLOAD_SIZE :
                                C910_CSR_VMSTATE_V2_PAYLOAD_SIZE)));

    for (gsize offset = 0; offset + header_size <= stream->len; offset++) {
        target_count += vmstate_subsection_header_at(
            stream->data, stream->len, offset, C910_CSR_VMSTATE_NAME,
            target_version);
        current_count += vmstate_subsection_header_at(
            stream->data, stream->len, offset, C910_CSR_VMSTATE_NAME, 3);
    }
    g_assert_cmpuint(target_count, ==, C910_TEST_HARTS);
    g_assert_cmpuint(current_count, ==, 0);

    for (uint32_t hart = 0; hart < C910_TEST_HARTS; hart++) {
        guint expected_count = 0;
        guint unexpected_count = 0;
        uint32_t expected_version = target_version == 1 ?
                                    RISCV_CPU_VMSTATE_V11 :
                                    RISCV_CPU_VMSTATE_V12;
        uint32_t unexpected_version = target_version == 1 ?
                                      RISCV_CPU_VMSTATE_V12 :
                                      RISCV_CPU_VMSTATE_V11;

        for (gsize offset = 0; offset < stream->len; offset++) {
            expected_count += vmstate_full_header_at(
                stream->data, stream->len, offset,
                RISCV_CPU_VMSTATE_NAME, hart, expected_version);
            unexpected_count += vmstate_full_header_at(
                stream->data, stream->len, offset,
                RISCV_CPU_VMSTATE_NAME, hart, unexpected_version);
        }
        g_assert_cmpuint(expected_count, ==, 1);
        g_assert_cmpuint(unexpected_count, ==, 0);
    }
    g_assert_cmpuint(ldl_be_p(stream->data + 4), ==, 3);

    g_assert_true(g_file_set_contents(path, (const char *)stream->data,
                                      stream->len, &error));
    g_assert_no_error(error);
}

#define CSR_SEED           0x015

#define SEED_OPST_MASK     (UINT64_C(0x3) << 30)
#define SEED_OPST_ES16     (UINT64_C(0x2) << 30)
#define SEED_OPST_DEAD     (UINT64_C(0x3) << 30)

static void run_test_csr(void)
{
    uint64_t res;
    uint64_t val = 0;

    QTestState *qts = qtest_init("-machine virt -cpu veyron-v1");

    res = qtest_csr_call(qts, "get_csr", 0, CSR_MVENDORID, &val);

    g_assert_cmpint(res, ==, 0);
    g_assert_cmpint(val, ==, 0x61f);

    val = 0xff;
    res = qtest_csr_call(qts, "set_csr", 0, CSR_MISELECT, &val);

    g_assert_cmpint(res, ==, 0);

    val = 0;
    res = qtest_csr_call(qts, "get_csr", 0, CSR_MISELECT, &val);

    g_assert_cmpint(res, ==, 0);
    g_assert_cmpint(val, ==, 0xff);

    qtest_quit(qts);
}

static void run_test_thead_c910_csrs(void)
{
    static const uint32_t cpuid[] = {
        0x090c090d, 0x110c9000, 0x260c0001, 0x30530077,
        0x42080407, 0x50000003, 0x60000a53,
    };
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");
    uint64_t mxstatus_writable = (1ULL << 22) | (1ULL << 21) |
                                 (1ULL << 19) | (1ULL << 18) |
                                 (1ULL << 17) | (1ULL << 16) |
                                 (1ULL << 15) | (1ULL << 13) |
                                 (1ULL << 11) | (1ULL << 10);
    uint64_t mhcr_expected = (1ULL << 12) | (1ULL << 8) | (1ULL << 7) |
                             (1ULL << 6) | (1ULL << 5) | (1ULL << 4) |
                             (1ULL << 3) | (1ULL << 2) | (1ULL << 1) |
                             (1ULL << 0);
    uint64_t satp_expected = (8ULL << 60) | (0xffffULL << 44) |
                             ((1ULL << 28) - 1);

    g_assert_cmphex(get_csr(qts, CSR_MVENDORID), ==, 0x5b7);
    g_assert_cmphex(get_csr(qts, CSR_MARCHID), ==, 0);
    g_assert_cmphex(get_csr(qts, CSR_MIMPID), ==, 0);
    g_assert_cmphex(get_csr(qts, CSR_MISA) & (1ULL << 21), ==,
                    1ULL << 21);
    g_assert_cmphex(get_csr(qts, CSR_TH_MXSTATUS), ==, 0xc0638000);
    g_assert_cmphex(get_csr(qts, CSR_TH_SXSTATUS), ==, 0xc0638000);
    g_assert_cmphex(get_csr(qts, CSR_TH_MHCR), ==, 0x108);
    g_assert_cmphex(get_csr(qts, CSR_TH_MHINT), ==, 0x24000);
    g_assert_cmphex(get_csr(qts, CSR_TH_MHINT2), ==, 0);
    g_assert_cmphex(get_csr(qts, CSR_TH_MHINT3), ==, 0x10404040);
    g_assert_cmphex(get_csr(qts, CSR_TH_MRVBR), ==, 0xffffd00000ULL);
    set_csr(qts, CSR_MSTATUS,
            get_csr(qts, CSR_MSTATUS) | MSTATUS_FS_INITIAL);
    g_assert_cmphex(get_csr(qts, CSR_TH_FXCR), ==, 0);

    /* FXCR aliases frm/fflags and owns DQNaN plus aggregate FE. */
    set_csr(qts, CSR_TH_FXCR, UINT64_MAX);
    g_assert_cmphex(get_csr(qts, CSR_TH_FXCR), ==, 0x0780003f);
    g_assert_cmphex(get_csr(qts, CSR_FRM), ==, 7);
    g_assert_cmphex(get_csr(qts, CSR_FFLAGS), ==, 0x1f);
    g_assert_cmphex(get_csr(qts, CSR_FCSR), ==, 0xff);
    set_csr(qts, CSR_FCSR, 0x45);
    g_assert_cmphex(get_csr(qts, CSR_TH_FXCR), ==, 0x02800025);
    set_csr(qts, CSR_TH_FXCR, 0);
    g_assert_cmphex(get_csr(qts, CSR_FCSR), ==, 0);

    set_csr(qts, CSR_TH_MXSTATUS, UINT64_MAX);
    g_assert_cmphex(get_csr(qts, CSR_TH_MXSTATUS), ==,
                    0xc0000000 | mxstatus_writable);
    set_csr(qts, CSR_TH_MHCR, UINT64_MAX);
    g_assert_cmphex(get_csr(qts, CSR_TH_MHCR), ==, mhcr_expected);
    set_csr(qts, CSR_TH_MCOR, UINT64_MAX);
    g_assert_cmphex(get_csr(qts, CSR_TH_MCOR), ==, 3);
    set_csr(qts, CSR_TH_MCOUNTERWEN, UINT64_MAX);
    g_assert_cmphex(get_csr(qts, CSR_TH_MCOUNTERWEN), ==, 0xfffffffd);

    /* C9xx selectors are six-bit WARL values, limited to raw event 42. */
    set_csr(qts, CSR_MHPMEVENT3, 42);
    g_assert_cmphex(get_csr(qts, CSR_MHPMEVENT3), ==, 42);
    set_csr(qts, CSR_MHPMEVENT3, 43);
    g_assert_cmphex(get_csr(qts, CSR_MHPMEVENT3), ==, 0);
    /* The qtest accelerator does not realize programmable PMU counters. */
    set_csr(qts, CSR_MCOUNTINHIBIT, UINT64_MAX);
    g_assert_cmphex(get_csr(qts, CSR_MCOUNTINHIBIT) & 5, ==, 5);
    set_csr(qts, CSR_MCOUNTINHIBIT, 0);

    /* Machine access owns all bits except TIME; supervisor sees WEN aliases. */
    set_csr(qts, CSR_TH_MCOUNTERINTEN, UINT64_MAX);
    g_assert_cmphex(get_csr(qts, CSR_TH_MCOUNTERINTEN), ==, 0xfffffffd);
    g_assert_cmphex(get_csr(qts, CSR_TH_SCOUNTERINTEN), ==, 0xfffffffd);
    set_csr(qts, CSR_TH_MCOUNTERWEN, (1 << 0) | (1 << 3));
    set_csr(qts, CSR_TH_SCOUNTERINTEN, 1 << 3);
    g_assert_cmphex(get_csr(qts, CSR_TH_SCOUNTERINTEN), ==, 1 << 3);
    g_assert_cmphex(get_csr(qts, CSR_TH_MCOUNTERINTEN), ==, 0xfffffffc);

    set_csr(qts, CSR_TH_MCOUNTEROF, (1 << 0) | (1 << 1) |
                                      (1 << 3) | (1 << 4));
    g_assert_cmphex(get_csr(qts, CSR_TH_MCOUNTEROF), ==, 0x19);
    g_assert_cmphex(get_csr(qts, CSR_MIP) & (1 << 17), ==, 1 << 17);
    set_csr(qts, CSR_MIP, 0);
    g_assert_cmphex(get_csr(qts, CSR_MIP) & (1 << 17), ==, 1 << 17);
    g_assert_cmphex(get_csr(qts, CSR_TH_SCOUNTEROF), ==, 0x9);
    set_csr(qts, CSR_TH_SCOUNTEROF, 1 << 3);
    g_assert_cmphex(get_csr(qts, CSR_TH_SCOUNTEROF), ==, 1 << 3);
    g_assert_cmphex(get_csr(qts, CSR_TH_MCOUNTEROF), ==, 0x18);
    set_csr(qts, CSR_TH_MCOUNTEROF, 0);
    g_assert_cmphex(get_csr(qts, CSR_MIP) & (1 << 17), ==, 0);

    set_csr(qts, CSR_MIDELEG, 1 << 17);
    g_assert_cmphex(get_csr(qts, CSR_MIDELEG) & (1 << 17), ==, 1 << 17);

    for (size_t i = 0; i < ARRAY_SIZE(cpuid); i++) {
        g_assert_cmphex(get_csr(qts, CSR_TH_CPUID), ==, cpuid[i]);
    }
    g_assert_cmphex(get_csr(qts, CSR_TH_CPUID), ==, cpuid[0]);

    set_csr(qts, CSR_SATP, (8ULL << 60) | ((1ULL << 60) - 1));
    g_assert_cmphex(get_csr(qts, CSR_SATP), ==, satp_expected);

    /* XTheadVector uses T-Head's status position and has no vcsr CSR. */
    set_csr(qts, CSR_MSTATUS, (1ULL << 13) | (1ULL << 23));
    g_assert_cmphex(get_csr(qts, CSR_MSTATUS) & (3ULL << 23), ==,
                    1ULL << 23);
    g_assert_cmphex(get_csr(qts, CSR_VLENB), ==, 16);
    g_assert_cmphex(get_csr(qts, CSR_VL), ==, 0);
    g_assert_cmphex(get_csr(qts, CSR_VTYPE), ==, 1ULL << 63);
    set_csr(qts, CSR_VSTART, 7);
    g_assert_cmphex(get_csr(qts, CSR_VSTART), ==, 7);
    set_csr(qts, CSR_VXRM, 2);
    g_assert_cmphex(get_csr(qts, CSR_VXRM), ==, 2);
    set_csr(qts, CSR_VXSAT, 1);
    g_assert_cmphex(get_csr(qts, CSR_VXSAT), ==, 1);

    set_csr(qts, CSR_TH_FXCR, UINT64_MAX);
    qtest_system_reset(qts);
    g_assert_cmphex(get_csr(qts, CSR_MSTATUS) & MSTATUS_FS_MASK, ==, 0);
    set_csr(qts, CSR_MSTATUS,
            get_csr(qts, CSR_MSTATUS) | MSTATUS_FS_INITIAL);
    g_assert_cmphex(get_csr(qts, CSR_TH_FXCR), ==, 0);

    qtest_quit(qts);
}

static void run_test_sscofpmf_interrupt_csrs(void)
{
    QTestState *qts;

    qts = qtest_init("-machine virt -cpu veyron-v1");
    set_csr(qts, CSR_MIDELEG, 0);
    set_csr(qts, CSR_MIE, LCOFI_BIT);
    set_csr(qts, CSR_MIP, LCOFI_BIT);
    assert_csr_mask(qts, CSR_MIE, LCOFI_BIT, LCOFI_BIT);
    assert_csr_mask(qts, CSR_MIP, LCOFI_BIT, LCOFI_BIT);

    set_csr(qts, CSR_MIDELEG, LCOFI_BIT);
    assert_csr_mask(qts, CSR_MIDELEG, LCOFI_BIT, LCOFI_BIT);
    assert_csr_mask(qts, CSR_SIE, LCOFI_BIT, LCOFI_BIT);
    assert_csr_mask(qts, CSR_SIP, LCOFI_BIT, LCOFI_BIT);

    set_csr(qts, CSR_SIE, 0);
    set_csr(qts, CSR_SIP, 0);
    assert_csr_mask(qts, CSR_MIE, LCOFI_BIT, 0);
    assert_csr_mask(qts, CSR_MIP, LCOFI_BIT, 0);
    qtest_quit(qts);

    qts = qtest_init("-machine virt -cpu sifive-u54");
    set_csr(qts, CSR_MIDELEG, LCOFI_BIT);
    set_csr(qts, CSR_MIE, LCOFI_BIT);
    set_csr(qts, CSR_MIP, LCOFI_BIT);
    set_csr(qts, CSR_SIE, LCOFI_BIT);
    set_csr(qts, CSR_SIP, LCOFI_BIT);
    assert_csr_mask(qts, CSR_MIDELEG, LCOFI_BIT, 0);
    assert_csr_mask(qts, CSR_MIE, LCOFI_BIT, 0);
    assert_csr_mask(qts, CSR_MIP, LCOFI_BIT, 0);
    assert_csr_mask(qts, CSR_SIE, LCOFI_BIT, 0);
    assert_csr_mask(qts, CSR_SIP, LCOFI_BIT, 0);
    qtest_quit(qts);
}

static void run_test_pmu_migration(const void *opaque)
{
    const PMUMigrationTest *test = opaque;
    g_autofree char *args = NULL;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    g_autofree char *dst_args = NULL;
    QTestState *src;
    QTestState *dst;
    uint64_t status;
    int fd;

    args = g_strdup_printf(
        "%s -accel tcg,thread=single -icount shift=0 -S",
        test->machine_args);

    fd = g_file_open_tmp("riscv-pmu-migration-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    dst_args = g_strdup_printf("%s -incoming defer", args);

    src = qtest_init(args);
    dst = qtest_init(dst_args);
    qtest_memwrite(src, test->load_addr, test->guest, test->guest_size);
    qtest_qmp_assert_success(src, "{ 'execute': 'cont' }");

    status = wait_for_guest_status_change(src, test->status_addr, 0);
    g_assert_cmphex(status, ==, PMU_TEST_READY);
    qtest_writeq(src, test->status_addr + PMU_TEST_ARM_OFFSET, 1);
    status = wait_for_guest_status_change(src, test->status_addr,
                                          PMU_TEST_READY);
    g_assert_cmphex(status, ==, PMU_TEST_ARMED);
    qtest_qmp_assert_success(src, "{ 'execute': 'stop' }");

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readq(dst, test->status_addr), ==, PMU_TEST_ARMED);
    if (test->mark_migrated) {
        qtest_writeq(dst, test->status_addr + PMU_TEST_MIGRATED_OFFSET, 1);
    }
    qtest_writeq(dst, test->status_addr + PMU_TEST_GO_OFFSET, 1);
    qtest_qmp_assert_success(dst, "{ 'execute': 'cont' }");
    status = wait_for_guest_status_change(dst, test->status_addr,
                                          PMU_TEST_ARMED);
    g_assert_cmphex(status, ==, PMU_TEST_PASS);

    qtest_quit(src);
    qtest_quit(dst);
    unlink(path);
}

static void run_test_thead_c910_fxcr_migration_reset(void)
{
    const uint64_t status_addr = 0x200000;
    const uint64_t source_fxcr = FXCR_DQNAN | FFLAGS_NX;
    const uint64_t migrated_fxcr = source_fxcr | FXCR_FE;
    const uint64_t reset_fxcr = FXCR_FE | FFLAGS_NV;
    const char *args = "-machine beaglev-ahead -bios none "
                       "-accel tcg,thread=single -icount shift=0 -S";
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    g_autofree char *dst_args = NULL;
    QTestState *src;
    QTestState *dst;
    uint64_t status;
    int fd;

    fd = g_file_open_tmp("riscv-c910-fxcr-migration-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(close(fd), ==, 0);
    uri = g_strdup_printf("file:%s", path);
    dst_args = g_strdup_printf("%s -incoming defer", args);

    src = qtest_init(args);
    dst = qtest_init(dst_args);

    /*
     * The incoming CPU cannot run before migration.  Leave one local,
     * non-migrated SoftFloat event for the C910 post-load hook to discard.
     */
    g_assert_cmphex(raise_fp_exception(dst, FFLAGS_NV), ==, FFLAGS_NV);

    qtest_memwrite(src, 0, c910_fxcr_migration_guest_bin,
                   sizeof(c910_fxcr_migration_guest_bin));
    qtest_qmp_assert_success(src, "{ 'execute': 'cont' }");
    status = wait_for_guest_status_change(src, status_addr, 0);
    g_assert_cmphex(status, ==, FXCR_TEST_READY);
    qtest_qmp_assert_success(src, "{ 'execute': 'stop' }");
    g_assert_cmphex(get_csr(src, CSR_TH_FXCR), ==, source_fxcr);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readq(dst, status_addr), ==, FXCR_TEST_READY);
    /* Do not read FXCR here: that would re-arm exception event tracking. */
    qtest_writeq(dst, status_addr + FXCR_TEST_GO_OFFSET, 1);
    qtest_qmp_assert_success(dst, "{ 'execute': 'cont' }");
    status = wait_for_guest_status_change(dst, status_addr, FXCR_TEST_READY);
    g_assert_cmphex(status, ==, FXCR_TEST_MIGRATION_PASS);

    qtest_qmp_assert_success(dst, "{ 'execute': 'stop' }");
    g_assert_cmphex(get_csr(dst, CSR_TH_FXCR), ==, migrated_fxcr);

    /*
     * QEMU system reset preserves RAM, so a cookie selects the reset phase.
     * Keep the CPU stopped until after reset to make the transition exact.
     */
    qtest_writeq(dst, status_addr + FXCR_TEST_RESET_OFFSET, 1);
    qtest_system_reset(dst);
    qtest_qmp_assert_success(dst, "{ 'execute': 'cont' }");
    status = wait_for_guest_status_change(dst, status_addr,
                                          FXCR_TEST_MIGRATION_PASS);
    g_assert_cmphex(status, ==, FXCR_TEST_RESET_PASS);

    qtest_qmp_assert_success(dst, "{ 'execute': 'stop' }");
    g_assert_cmphex(get_csr(dst, CSR_TH_FXCR), ==, reset_fxcr);

    qtest_quit(src);
    qtest_quit(dst);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void run_test_thead_c910_legacy_migration(const void *opaque)
{
    static const uint32_t migrated_csrs[] = {
        CSR_TH_MXSTATUS, CSR_TH_MHCR, CSR_TH_MCOR, CSR_TH_MHINT,
        CSR_TH_MHINT2, CSR_TH_MHINT3, CSR_TH_MCOUNTERWEN,
    };
    static const uint32_t cpuid[] = {
        0x090c090d, 0x110c9000, 0x260c0001, 0x30530077,
        0x42080407, 0x50000003, 0x60000a53,
    };
    static const uint8_t mxstatus_bits[C910_TEST_HARTS] = {
        10, 11, 13, 15,
    };
    static const uint8_t mhcr_bits[C910_TEST_HARTS] = {
        0, 1, 2, 4,
    };
    static const uint8_t mhint_bits[C910_TEST_HARTS] = {
        2, 3, 5, 8,
    };
    static const uint8_t source_frm[C910_TEST_HARTS] = {
        3, 4, 2, 1,
    };
    const uint32_t version = *(const uint32_t *)opaque;
    const uint64_t status_addr = 0x200000;
    const uint64_t source_custom_fxcr = FXCR_DQNAN | FXCR_FE | FFLAGS_NV;
    const uint64_t poison_fxcr = FXCR_DQNAN | FXCR_FE | FFLAGS_NV;
    const char *args = "-machine beaglev-ahead,suppress-vmdesc=on "
                       "-bios none -accel tcg,thread=single "
                       "-icount shift=0 -S";
    uint64_t expected[C910_TEST_HARTS][ARRAY_SIZE(migrated_csrs)];
    uint64_t expected_inten[C910_TEST_HARTS];
    uint64_t expected_of[C910_TEST_HARTS];
    uint64_t expected_frm[C910_TEST_HARTS];
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    g_autofree char *dst_args = NULL;
    QTestState *src;
    QTestState *dst;
    uint64_t status;
    int fd;

    g_assert_true(version == 1 || version == 2);
    fd = g_file_open_tmp("riscv-c910-legacy-migration-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(close(fd), ==, 0);
    uri = g_strdup_printf("file:%s", path);
    dst_args = g_strdup_printf("%s -incoming defer", args);

    src = qtest_init(args);
    dst = qtest_init(dst_args);
    qtest_memwrite(src, 0, c910_fxcr_legacy_migration_guest_bin,
                   sizeof(c910_fxcr_legacy_migration_guest_bin));
    qtest_qmp_assert_success(src, "{ 'execute': 'cont' }");
    status = wait_for_guest_status_change(src, status_addr, 0);
    g_assert_cmphex(status, ==, FXCR_TEST_READY);
    qtest_qmp_assert_success(src, "{ 'execute': 'stop' }");

    for (uint32_t hart = 0; hart < C910_TEST_HARTS; hart++) {
        uint64_t counter_bit = UINT64_C(1) << (hart + 3);
        uint64_t source_inten = counter_bit;
        uint64_t source_of = version == 2 ? counter_bit :
                              UINT64_C(1) << (hart + 10);
        uint64_t poison_counter = UINT64_C(1) << (hart + 20);

        set_csr_hart(src, hart, CSR_TH_MXSTATUS,
                     UINT64_C(1) << mxstatus_bits[hart]);
        set_csr_hart(src, hart, CSR_TH_MHCR,
                     UINT64_C(1) << mhcr_bits[hart]);
        set_csr_hart(src, hart, CSR_TH_MCOR, hart);
        set_csr_hart(src, hart, CSR_TH_MHINT,
                     UINT64_C(1) << mhint_bits[hart]);
        set_csr_hart(src, hart, CSR_TH_MHINT2, UINT64_C(1) << hart);
        set_csr_hart(src, hart, CSR_TH_MHINT3,
                     UINT64_C(1) << (hart + 4));
        set_csr_hart(src, hart, CSR_TH_MCOUNTERWEN, counter_bit);
        set_csr_hart(src, hart, CSR_TH_MCOUNTERINTEN, source_inten);
        set_csr_hart(src, hart, CSR_TH_MCOUNTEROF, source_of);

        for (size_t i = 0; i < ARRAY_SIZE(migrated_csrs); i++) {
            expected[hart][i] = get_csr_hart(src, hart, migrated_csrs[i]);
        }
        expected_inten[hart] = get_csr_hart(src, hart,
                                            CSR_TH_MCOUNTERINTEN);
        expected_of[hart] = get_csr_hart(src, hart, CSR_TH_MCOUNTEROF);
        for (uint32_t i = 0; i < hart; i++) {
            get_csr_hart(src, hart, CSR_TH_CPUID);
        }

        expected_frm[hart] = source_frm[hart];
        set_csr_hart(src, hart, CSR_MSTATUS,
                     get_csr_hart(src, hart, CSR_MSTATUS) |
                     MSTATUS_FS_INITIAL);
        set_csr_hart(src, hart, CSR_TH_FXCR,
                     (expected_frm[hart] << FXCR_FRM_SHIFT) |
                     source_custom_fxcr);

        /* Poison every legacy default on the incoming CPU. */
        set_csr_hart(dst, hart, CSR_MSTATUS,
                     get_csr_hart(dst, hart, CSR_MSTATUS) |
                     MSTATUS_FS_INITIAL);
        set_csr_hart(dst, hart, CSR_TH_FXCR, poison_fxcr);
        set_csr_hart(dst, hart, CSR_TH_MCOUNTERINTEN, poison_counter);
        set_csr_hart(dst, hart, CSR_TH_MCOUNTEROF, poison_counter);
    }

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    downgrade_c910_csr_subsections(path, version);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readq(dst, status_addr), ==, FXCR_TEST_READY);
    for (uint32_t hart = 0; hart < C910_TEST_HARTS; hart++) {
        uint64_t expected_pending = version == 2 ? C910_PMU_OVF_BIT : 0;

        for (size_t i = 0; i < ARRAY_SIZE(migrated_csrs); i++) {
            g_assert_cmphex(get_csr_hart(dst, hart, migrated_csrs[i]), ==,
                            expected[hart][i]);
        }
        g_assert_cmphex(get_csr_hart(dst, hart, CSR_TH_CPUID), ==,
                        cpuid[hart]);
        g_assert_cmphex(get_csr_hart(dst, hart, CSR_TH_MCOUNTERINTEN), ==,
                        version == 2 ? expected_inten[hart] : 0);
        g_assert_cmphex(get_csr_hart(dst, hart, CSR_TH_MCOUNTEROF), ==,
                        version == 2 ? expected_of[hart] : 0);
        g_assert_cmphex(get_csr_hart(dst, hart, CSR_MIP) &
                        C910_PMU_OVF_BIT, ==, expected_pending);

        /* Hart 0 must not access FXCR before the derived-state guest runs. */
        if (hart != 0) {
            g_assert_cmphex(get_csr_hart(dst, hart, CSR_TH_FXCR), ==,
                            expected_frm[hart] << FXCR_FRM_SHIFT);
            g_assert_cmphex(get_csr_hart(dst, hart, CSR_FCSR), ==,
                            expected_frm[hart] << 5);
        }
    }

    qtest_writeq(dst, status_addr + FXCR_TEST_GO_OFFSET, 1);
    qtest_qmp_assert_success(dst, "{ 'execute': 'cont' }");
    status = wait_for_guest_status_change(dst, status_addr, FXCR_TEST_READY);
    g_assert_cmphex(status, ==, FXCR_TEST_MIGRATION_PASS);
    qtest_qmp_assert_success(dst, "{ 'execute': 'stop' }");
    g_assert_cmphex(get_csr_hart(dst, 0, CSR_TH_FXCR), ==,
                    (expected_frm[0] << FXCR_FRM_SHIFT) |
                    FXCR_FE | FFLAGS_NX);
    g_assert_cmphex(get_csr_hart(dst, 0, CSR_FCSR), ==,
                    (expected_frm[0] << 5) | FFLAGS_NX);

    qtest_quit(src);
    qtest_quit(dst);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void run_test_seed_csr(void)
{
    uint64_t val = 0;
    uint64_t opst;
    QTestState *qts;

    qts = qtest_init("-machine virt -cpu tt-ascalon");

    qtest_csr_call(qts, "get_csr", 0, CSR_SEED, &val);

    opst = val & SEED_OPST_MASK;
    g_assert_true(opst == SEED_OPST_ES16 ||
                  opst == SEED_OPST_DEAD);

    g_assert_cmphex(val >> 32, ==, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    static const uint32_t c910_legacy_version_1 = 1;
    static const uint32_t c910_legacy_version_2 = 2;

    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine("virt")) {
        qtest_add_func("/cpu/csr", run_test_csr);
        qtest_add_func("/cpu/csr/seed", run_test_seed_csr);
        qtest_add_func("/cpu/sscofpmf-interrupt-csrs",
                       run_test_sscofpmf_interrupt_csrs);
        qtest_add_data_func("/cpu/fixed-pmu-migration",
                            &riscv_fixed_pmu_migration,
                            run_test_pmu_migration);
        qtest_add_data_func("/cpu/inhibited-pmu-migration",
                            &riscv_inhibited_pmu_migration,
                            run_test_pmu_migration);
        qtest_add_data_func("/cpu/pending-pmu-migration",
                            &riscv_pending_pmu_migration,
                            run_test_pmu_migration);
        qtest_add_data_func("/cpu/pmu-migration", &riscv_pmu_migration,
                            run_test_pmu_migration);
    }
    if (qtest_has_machine("beaglev-ahead")) {
        qtest_add_func("/cpu/thead-c910-csr", run_test_thead_c910_csrs);
        qtest_add_func("/cpu/thead-c910-fxcr-migration-reset",
                       run_test_thead_c910_fxcr_migration_reset);
        qtest_add_data_func("/cpu/thead-c910-legacy-migration/v1",
                            &c910_legacy_version_1,
                            run_test_thead_c910_legacy_migration);
        qtest_add_data_func("/cpu/thead-c910-legacy-migration/v2",
                            &c910_legacy_version_2,
                            run_test_thead_c910_legacy_migration);
        qtest_add_data_func("/cpu/thead-c910-pmu-migration",
                            &c910_pmu_migration, run_test_pmu_migration);
        qtest_add_data_func("/cpu/thead-c910-inhibited-pmu-migration",
                            &c910_inhibited_pmu_migration,
                            run_test_pmu_migration);
        qtest_add_data_func("/cpu/thead-c910-pending-pmu-migration",
                            &c910_pending_pmu_migration,
                            run_test_pmu_migration);
    }

    return g_test_run();
}
