/*
 * BeagleV Ahead machine tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"

#define TH1520_CLINT_BASE          0xffdc000000ULL
#define C900_MSIP(hart)            (TH1520_CLINT_BASE + 0x0000 + 4 * (hart))
#define C900_MTIMECMP(hart)        (TH1520_CLINT_BASE + 0x4000 + 8 * (hart))
#define C900_SSIP(hart)            (TH1520_CLINT_BASE + 0xc000 + 4 * (hart))
#define C900_STIMECMP(hart)        (TH1520_CLINT_BASE + 0xd000 + 8 * (hart))

#define CSR_TIME                   0xc01

#define C910_HARTS                 4
#define C900_CLINT_QOM_PATH        "/machine/soc/clint"

typedef struct C900CLINTBank {
    const char *name;
    uint64_t base;
    uint32_t stride;
    bool timer;
} C900CLINTBank;

static const C900CLINTBank c900_clint_banks[] = {
    { "msip",   TH1520_CLINT_BASE + 0x0000, 4, false },
    { "mtimer", TH1520_CLINT_BASE + 0x4000, 8, true },
    { "ssip",   TH1520_CLINT_BASE + 0xc000, 4, false },
    { "stimer", TH1520_CLINT_BASE + 0xd000, 8, true },
};

static uint64_t get_csr(QTestState *qts, uint32_t hart, uint32_t csr)
{
    uint64_t value = 0;

    g_assert_cmpint(qtest_csr_call(qts, "get_csr", hart, csr, &value), ==, 0);
    return value;
}

static void write_compare(QTestState *qts, uint64_t addr, uint64_t value)
{
    /* C900 exposes the two halves as separate 32-bit APB registers. */
    qtest_writel(qts, addr + 4, value >> 32);
    qtest_writel(qts, addr, value);
}

static void assert_clint_reset_state(QTestState *qts)
{
    for (uint32_t hart = 0; hart < C910_HARTS; hart++) {
        g_assert_cmphex(qtest_readl(qts, C900_MSIP(hart)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, C900_SSIP(hart)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, C900_MTIMECMP(hart)), ==,
                        UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, C900_MTIMECMP(hart) + 4), ==,
                        UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, C900_STIMECMP(hart)), ==,
                        UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, C900_STIMECMP(hart) + 4), ==,
                        UINT32_MAX);
    }
}

static void assert_only_irq(QTestState *qts, uint32_t asserted)
{
    for (uint32_t hart = 0; hart < C910_HARTS; hart++) {
        g_assert_cmpint(qtest_get_irq(qts, hart), ==, hart == asserted);
    }
}

static void assert_no_irq(QTestState *qts)
{
    for (uint32_t hart = 0; hart < C910_HARTS; hart++) {
        g_assert_false(qtest_get_irq(qts, hart));
    }
}

static void test_c900_clint_reset(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_clint_reset_state(qts);
    qtest_quit(qts);
}

static void test_c900_clint_bank(const void *opaque)
{
    const C900CLINTBank *bank = opaque;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    /*
     * riscv_cpu_set_irq() is intentionally inactive with -accel qtest, so
     * observe the controller's wires rather than the CPU mip CSR here.
     */
    qtest_irq_intercept_out_named(qts, C900_CLINT_QOM_PATH, bank->name);
    assert_no_irq(qts);

    for (uint32_t hart = 0; hart < C910_HARTS; hart++) {
        uint64_t addr = bank->base + bank->stride * hart;

        if (bank->timer) {
            write_compare(qts, addr, 0);
            assert_only_irq(qts, hart);
            write_compare(qts, addr, UINT64_MAX);
        } else {
            qtest_writel(qts, addr, UINT32_MAX);
            g_assert_cmphex(qtest_readl(qts, addr), ==, 1);
            assert_only_irq(qts, hart);
            qtest_writel(qts, addr, 0);
        }
        assert_no_irq(qts);
    }

    if (bank->timer) {
        /* Three 3 MHz ticks take exactly one microsecond. */
        g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);
        g_assert_cmphex(get_csr(qts, 0, CSR_TIME), ==, 0);
        write_compare(qts, bank->base, 3);
        qtest_clock_step(qts, 999);
        g_assert_cmphex(get_csr(qts, 0, CSR_TIME), ==, 2);
        assert_no_irq(qts);
        qtest_clock_step(qts, 1);
        g_assert_cmphex(get_csr(qts, 0, CSR_TIME), ==, 3);
        assert_only_irq(qts, 0);
    } else {
        qtest_writel(qts, bank->base, 1);
        assert_only_irq(qts, 0);
    }

    qtest_system_reset(qts);
    assert_no_irq(qts);
    assert_clint_reset_state(qts);
    qtest_quit(qts);
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

static void test_c900_clint_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-clint-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_irq_intercept_out_named(dst, C900_CLINT_QOM_PATH, "mtimer");

    g_assert_cmpint(qtest_clock_set(src, 1000), ==, 1000);
    g_assert_cmphex(get_csr(src, 0, CSR_TIME), ==, 3);
    qtest_writel(src, C900_MSIP(1), 1);
    qtest_writel(src, C900_SSIP(2), 1);
    write_compare(src, C900_MTIMECMP(3), 6);
    write_compare(src, C900_STIMECMP(0), 0);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(get_csr(dst, 0, CSR_TIME), ==, 3);
    g_assert_cmphex(qtest_readl(dst, C900_MSIP(1)), ==, 1);
    g_assert_cmphex(qtest_readl(dst, C900_SSIP(2)), ==, 1);
    g_assert_cmphex(qtest_readl(dst, C900_MTIMECMP(3)), ==, 6);
    g_assert_cmphex(qtest_readl(dst, C900_MTIMECMP(3) + 4), ==, 0);
    g_assert_cmphex(qtest_readl(dst, C900_STIMECMP(0)), ==, 0);
    g_assert_cmphex(qtest_readl(dst, C900_STIMECMP(0) + 4), ==, 0);

    assert_no_irq(dst);
    qtest_clock_step(dst, 999);
    g_assert_cmphex(get_csr(dst, 0, CSR_TIME), ==, 5);
    assert_no_irq(dst);
    qtest_clock_step(dst, 1);
    g_assert_cmphex(get_csr(dst, 0, CSR_TIME), ==, 6);
    assert_only_irq(dst, 3);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine("beaglev-ahead")) {
        qtest_add_func("/beaglev-ahead/c900-clint/reset",
                       test_c900_clint_reset);
        qtest_add_data_func("/beaglev-ahead/c900-clint/msip",
                            &c900_clint_banks[0], test_c900_clint_bank);
        qtest_add_data_func("/beaglev-ahead/c900-clint/mtimer",
                            &c900_clint_banks[1], test_c900_clint_bank);
        qtest_add_data_func("/beaglev-ahead/c900-clint/ssip",
                            &c900_clint_banks[2], test_c900_clint_bank);
        qtest_add_data_func("/beaglev-ahead/c900-clint/stimer",
                            &c900_clint_banks[3], test_c900_clint_bank);
        qtest_add_func("/beaglev-ahead/c900-clint/migration",
                       test_c900_clint_migration);
    }

    return g_test_run();
}
