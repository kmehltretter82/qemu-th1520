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
#include "libqtest.h"
#include "migration/riscv64/c910-pmu-guest.h"
#include "migration/riscv64/riscv-pmu-guest.h"

#define CSR_MVENDORID       0xf11
#define CSR_MARCHID         0xf12
#define CSR_MIMPID          0xf13
#define CSR_MISELECT        0x350
#define CSR_MSTATUS         0x300
#define CSR_MISA            0x301
#define CSR_MIDELEG         0x303
#define CSR_MCOUNTINHIBIT   0x320
#define CSR_MIP             0x344
#define CSR_MHPMEVENT3      0x323
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
#define CSR_TH_CPUID        0xfc0

#define PMU_TEST_ARM_OFFSET  8
#define PMU_TEST_GO_OFFSET   16
#define PMU_TEST_READY       1
#define PMU_TEST_ARMED       2
#define PMU_TEST_PASS        3

typedef struct PMUMigrationTest {
    const char *machine_args;
    const uint8_t *guest;
    size_t guest_size;
    uint64_t load_addr;
    uint64_t status_addr;
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

static const PMUMigrationTest c910_pmu_migration = {
    .machine_args = "-machine beaglev-ahead -bios none",
    .guest = c910_pmu_guest_bin,
    .guest_size = sizeof(c910_pmu_guest_bin),
    .load_addr = 0,
    .status_addr = 0x200000,
};

static uint64_t get_csr(QTestState *qts, uint32_t csrno)
{
    uint64_t val = 0;

    g_assert_cmpint(qtest_csr_call(qts, "get_csr", 0, csrno, &val), ==, 0);
    return val;
}

static void set_csr(QTestState *qts, uint32_t csrno, uint64_t val)
{
    g_assert_cmpint(qtest_csr_call(qts, "set_csr", 0, csrno, &val), ==, 0);
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
    qtest_writeq(dst, test->status_addr + PMU_TEST_GO_OFFSET, 1);
    qtest_qmp_assert_success(dst, "{ 'execute': 'cont' }");
    status = wait_for_guest_status_change(dst, test->status_addr,
                                          PMU_TEST_ARMED);
    g_assert_cmphex(status, ==, PMU_TEST_PASS);

    qtest_quit(src);
    qtest_quit(dst);
    unlink(path);
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
    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine("virt")) {
        qtest_add_func("/cpu/csr", run_test_csr);
        qtest_add_func("/cpu/csr/seed", run_test_seed_csr);
        qtest_add_data_func("/cpu/pmu-migration", &riscv_pmu_migration,
                            run_test_pmu_migration);
    }
    if (qtest_has_machine("beaglev-ahead")) {
        qtest_add_func("/cpu/thead-c910-csr", run_test_thead_c910_csrs);
        qtest_add_data_func("/cpu/thead-c910-pmu-migration",
                            &c910_pmu_migration, run_test_pmu_migration);
    }

    return g_test_run();
}
