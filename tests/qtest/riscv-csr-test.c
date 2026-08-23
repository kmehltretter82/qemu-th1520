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

#define CSR_MVENDORID       0xf11
#define CSR_MISELECT        0x350
#define CSR_SATP            0x180
#define CSR_PMPADDR0        0x3b0
#define CSR_TH_SXSTATUS     0x5c0
#define CSR_TH_MXSTATUS     0x7c0
#define CSR_TH_MHCR         0x7c1
#define CSR_TH_MCOR         0x7c2
#define CSR_TH_MHINT        0x7c5
#define CSR_TH_MRVBR        0x7c7
#define CSR_TH_MCOUNTERWEN  0x7c9
#define CSR_TH_MHINT2       0x7cc
#define CSR_TH_MHINT3       0x7cd
#define CSR_TH_CPUID        0xfc0

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

    for (size_t i = 0; i < ARRAY_SIZE(cpuid); i++) {
        g_assert_cmphex(get_csr(qts, CSR_TH_CPUID), ==, cpuid[i]);
    }
    g_assert_cmphex(get_csr(qts, CSR_TH_CPUID), ==, cpuid[0]);

    set_csr(qts, CSR_PMPADDR0, UINT64_MAX);
    g_assert_cmphex(get_csr(qts, CSR_PMPADDR0), ==,
                    (1ULL << 38) - 1);
    set_csr(qts, CSR_SATP, (8ULL << 60) | ((1ULL << 60) - 1));
    g_assert_cmphex(get_csr(qts, CSR_SATP), ==, satp_expected);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine("virt")) {
        qtest_add_func("/cpu/csr", run_test_csr);
    }
    if (qtest_has_machine("beaglev-ahead")) {
        qtest_add_func("/cpu/thead-c910-csr", run_test_thead_c910_csrs);
    }

    return g_test_run();
}
