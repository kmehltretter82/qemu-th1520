/*
 * BeagleV Ahead machine tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"
#include "qobject/qdict.h"

#include <libfdt.h>
#include <poll.h>

#define TH1520_BROM_BASE           0xffffd00000ULL
#define TH1520_CLINT_BASE          0xffdc000000ULL
#define TH1520_PLIC_BASE           0xffd8000000ULL
#define TH1520_SRAM_BASE           0xffe0000000ULL
#define TH1520_UART0_BASE          0xffe7014000ULL
#define C900_MSIP(hart)            (TH1520_CLINT_BASE + 0x0000 + 4 * (hart))
#define C900_MTIMECMP(hart)        (TH1520_CLINT_BASE + 0x4000 + 8 * (hart))
#define C900_SSIP(hart)            (TH1520_CLINT_BASE + 0xc000 + 4 * (hart))
#define C900_STIMECMP(hart)        (TH1520_CLINT_BASE + 0xd000 + 8 * (hart))

#define C900_PLIC_PRIORITY(irq)    (TH1520_PLIC_BASE + 4 * (irq))
#define C900_PLIC_PENDING(word)    (TH1520_PLIC_BASE + 0x1000 + 4 * (word))
#define C900_PLIC_ENABLE(context, word) \
    (TH1520_PLIC_BASE + 0x2000 + 0x80 * (context) + 4 * (word))
#define C900_PLIC_CONTROL          (TH1520_PLIC_BASE + 0x1ffffc)
#define C900_PLIC_THRESHOLD(context) \
    (TH1520_PLIC_BASE + 0x200000 + 0x1000 * (context))
#define C900_PLIC_CLAIM(context)   (C900_PLIC_THRESHOLD(context) + 4)

#define DW_UART_RBR_THR_DLL        (TH1520_UART0_BASE + 0x00)
#define DW_UART_IER_DLH            (TH1520_UART0_BASE + 0x04)
#define DW_UART_IIR_FCR            (TH1520_UART0_BASE + 0x08)
#define DW_UART_LCR                (TH1520_UART0_BASE + 0x0c)
#define DW_UART_LSR                (TH1520_UART0_BASE + 0x14)
#define DW_UART_SCR                (TH1520_UART0_BASE + 0x1c)
#define DW_UART_USR                (TH1520_UART0_BASE + 0x7c)
#define DW_UART_TFL                (TH1520_UART0_BASE + 0x80)
#define DW_UART_RFL                (TH1520_UART0_BASE + 0x84)
#define DW_UART_SRR                (TH1520_UART0_BASE + 0x88)
#define DW_UART_DLF                (TH1520_UART0_BASE + 0xc0)
#define DW_UART_RE_EN              (TH1520_UART0_BASE + 0xb4)
#define DW_UART_CPR                (TH1520_UART0_BASE + 0xf4)
#define DW_UART_UCV                (TH1520_UART0_BASE + 0xf8)
#define DW_UART_CTR                (TH1520_UART0_BASE + 0xfc)

#define CSR_TIME                   0xc01
#define CSR_MSCRATCH               0x340
#define CSR_TH_MCOR                0x7c2
#define CSR_TH_MCOUNTERWEN         0x7c9
#define CSR_TH_CPUID               0xfc0

#define C910_HARTS                 4
#define C900_PLIC_CONTEXTS         (C910_HARTS * 2)
#define C900_PLIC_WORDS            8
#define C900_CLINT_QOM_PATH        "/machine/soc/clint"
#define C900_PLIC_QOM_PATH         "/machine/soc/plic"
#define DW_UART_QOM_PATH           "/machine/soc/uart0"

#define TH1520_UART0_IRQ           36

#define BROM_RESET_FDT_ADDR        (TH1520_BROM_BASE + 32)
#define BROM_FW_DYNAMIC_INFO       (TH1520_BROM_BASE + 40)
#define FW_DYNAMIC_MAGIC           0x4942534f
#define FW_DYNAMIC_VERSION         2

#define UART_IER_RDI               BIT(0)
#define UART_IER_THRI              BIT(1)
#define UART_IIR_NO_INT            BIT(0)
#define UART_IIR_THRI              0x02
#define UART_IIR_RDI               0x04
#define UART_IIR_BUSY              0x07
#define UART_FCR_ENABLE            BIT(0)
#define UART_LCR_DLAB              BIT(7)
#define UART_LSR_DR                BIT(0)
#define UART_LSR_THRE              BIT(5)
#define UART_LSR_TEMT              BIT(6)
#define UART_USR_BUSY              BIT(0)
#define UART_USR_TFNF              BIT(1)
#define UART_USR_TFE               BIT(2)
#define UART_USR_RFNE              BIT(3)
#define UART_USR_RFF               BIT(4)
#define UART_SRR_UR                BIT(0)
#define UART_SRR_RFR               BIT(1)

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

typedef struct C900PLICContext {
    const char *output;
    uint32_t context;
    uint32_t hart;
} C900PLICContext;

static const C900PLICContext c900_plic_contexts[] = {
    { "mext", 0, 0 }, { "sext", 1, 0 },
    { "mext", 2, 1 }, { "sext", 3, 1 },
    { "mext", 4, 2 }, { "sext", 5, 2 },
    { "mext", 6, 3 }, { "sext", 7, 3 },
};

static uint64_t get_csr(QTestState *qts, uint32_t hart, uint32_t csr)
{
    uint64_t value = 0;

    g_assert_cmpint(qtest_csr_call(qts, "get_csr", hart, csr, &value), ==, 0);
    return value;
}

static void set_csr(QTestState *qts, uint32_t hart, uint32_t csr,
                    uint64_t value)
{
    g_assert_cmpint(qtest_csr_call(qts, "set_csr", hart, csr, &value), ==,
                    0);
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

static void c900_plic_set_enable(QTestState *qts, uint32_t context,
                                 uint32_t irq, bool enable)
{
    uint64_t addr = C900_PLIC_ENABLE(context, irq >> 5);
    uint32_t value = qtest_readl(qts, addr);

    value = deposit32(value, irq & 31, 1, enable);
    qtest_writel(qts, addr, value);
}

static void c900_plic_set_pending(QTestState *qts, uint32_t irq, bool pending)
{
    uint64_t addr = C900_PLIC_PENDING(irq >> 5);
    uint32_t value = qtest_readl(qts, addr);

    value = deposit32(value, irq & 31, 1, pending);
    qtest_writel(qts, addr, value);
}

static bool c900_plic_pending(QTestState *qts, uint32_t irq)
{
    return extract32(qtest_readl(qts, C900_PLIC_PENDING(irq >> 5)),
                     irq & 31, 1);
}

static void enable_uart0_supervisor_irq(QTestState *qts)
{
    qtest_writel(qts, C900_PLIC_PRIORITY(TH1520_UART0_IRQ), 5);
    c900_plic_set_enable(qts, 1, TH1520_UART0_IRQ, true);
}

static void test_direct_boot_contract(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");
    struct fdt_header header;
    const fdt32_t *cold_boot_harts;
    const char *compatible;
    g_autofree uint8_t *fdt = NULL;
    uint64_t fdt_addr;
    uint32_t cpu0_phandle;
    int config_offset;
    int cpu0_offset;
    int fdt_size;
    int len;

    /* Check both FW_DYNAMIC stages which select hart 0 for direct boot. */
    g_assert_cmphex(qtest_readq(qts, BROM_FW_DYNAMIC_INFO), ==,
                    FW_DYNAMIC_MAGIC);
    g_assert_cmphex(qtest_readq(qts, BROM_FW_DYNAMIC_INFO + 8), ==,
                    FW_DYNAMIC_VERSION);
    g_assert_cmphex(qtest_readq(qts, BROM_FW_DYNAMIC_INFO + 40), ==, 0);

    fdt_addr = qtest_readq(qts, BROM_RESET_FDT_ADDR);
    qtest_memread(qts, fdt_addr, &header, sizeof(header));
    g_assert_cmpint(fdt_check_header(&header), ==, 0);
    fdt_size = fdt_totalsize(&header);
    g_assert_cmpint(fdt_size, >=, sizeof(header));

    fdt = g_malloc(fdt_size);
    qtest_memread(qts, fdt_addr, fdt, fdt_size);
    g_assert_cmpint(fdt_check_header(fdt), ==, 0);

    config_offset = fdt_path_offset(fdt, "/chosen/opensbi-config");
    g_assert_cmpint(config_offset, >=, 0);
    compatible = fdt_getprop(fdt, config_offset, "compatible", &len);
    g_assert_nonnull(compatible);
    g_assert_cmpstr(compatible, ==, "opensbi,config");

    cpu0_offset = fdt_path_offset(fdt, "/cpus/cpu@0");
    g_assert_cmpint(cpu0_offset, >=, 0);
    cpu0_phandle = fdt_get_phandle(fdt, cpu0_offset);
    g_assert_cmphex(cpu0_phandle, !=, 0);

    cold_boot_harts = fdt_getprop(fdt, config_offset, "cold-boot-harts",
                                  &len);
    g_assert_nonnull(cold_boot_harts);
    g_assert_cmpint(len, ==, sizeof(*cold_boot_harts));
    g_assert_cmphex(fdt32_to_cpu(*cold_boot_harts), ==, cpu0_phandle);

    qtest_quit(qts);
}

static uint8_t read_serial_byte(int fd)
{
    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN,
    };
    uint8_t value;

    g_assert_cmpint(poll(&pfd, 1, 1000), ==, 1);
    g_assert_true(pfd.revents & POLLIN);
    g_assert_cmpint(recv(fd, &value, sizeof(value), 0), ==, sizeof(value));
    return value;
}

static void wait_for_uart_rx(QTestState *qts)
{
    int64_t deadline = g_get_monotonic_time() + G_USEC_PER_SEC;

    while (g_get_monotonic_time() < deadline) {
        if (qtest_readl(qts, DW_UART_LSR) & UART_LSR_DR) {
            return;
        }
        g_usleep(1000);
    }
    g_error("UART input was not received within one second");
}

static void c900_plic_set_input(QTestState *qts, const char *name,
                                uint32_t irq, int level)
{
    qtest_set_irq_in(qts, C900_PLIC_QOM_PATH, name, irq, level);
}

static void assert_plic_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CONTROL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_PRIORITY(0)), ==, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_PRIORITY(1)), ==, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_PRIORITY(240)), ==, 0);

    for (uint32_t word = 0; word < C900_PLIC_WORDS; word++) {
        g_assert_cmphex(qtest_readl(qts, C900_PLIC_PENDING(word)), ==, 0);
    }
    for (uint32_t context = 0; context < C900_PLIC_CONTEXTS; context++) {
        for (uint32_t word = 0; word < C900_PLIC_WORDS; word++) {
            g_assert_cmphex(qtest_readl(qts,
                                       C900_PLIC_ENABLE(context, word)),
                            ==, 0);
        }
        g_assert_cmphex(qtest_readl(qts, C900_PLIC_THRESHOLD(context)),
                        ==, 0);
        g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(context)), ==, 0);
    }
}

static void test_c900_plic_reset(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_plic_reset_state(qts);
    qtest_quit(qts);
}

static void test_c900_plic_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_writel(qts, C900_PLIC_PRIORITY(0), UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_PRIORITY(0)), ==, 0);
    qtest_writel(qts, C900_PLIC_PRIORITY(1), UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_PRIORITY(1)), ==, 31);
    qtest_writel(qts, C900_PLIC_PRIORITY(240), 0xa5);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_PRIORITY(240)), ==, 5);

    qtest_writel(qts, C900_PLIC_PENDING(0), UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_PENDING(0)), ==,
                    UINT32_MAX & ~1U);
    qtest_writel(qts, C900_PLIC_PENDING(7), UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_PENDING(7)), ==, 0x1ffff);

    qtest_writel(qts, C900_PLIC_ENABLE(0, 0), UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_ENABLE(0, 0)), ==,
                    UINT32_MAX & ~1U);
    qtest_writel(qts, C900_PLIC_ENABLE(7, 7), UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_ENABLE(7, 7)), ==, 0x1ffff);

    qtest_writel(qts, C900_PLIC_THRESHOLD(7), UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_THRESHOLD(7)), ==, 31);
    qtest_writel(qts, C900_PLIC_CONTROL, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CONTROL), ==, 1);

    qtest_system_reset(qts);
    assert_plic_reset_state(qts);
    qtest_quit(qts);
}

static void test_c900_plic_context(const void *opaque)
{
    const C900PLICContext *context = opaque;
    const uint32_t irq = 100;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH,
                                  context->output);
    qtest_writel(qts, C900_PLIC_PRIORITY(irq), 5);
    c900_plic_set_enable(qts, context->context, irq, true);
    c900_plic_set_input(qts, "source", irq, 1);
    g_assert_true(c900_plic_pending(qts, irq));
    assert_only_irq(qts, context->hart);

    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(context->context)),
                    ==, irq);
    g_assert_false(c900_plic_pending(qts, irq));
    assert_no_irq(qts);

    c900_plic_set_input(qts, "source", irq, 0);
    qtest_writel(qts, C900_PLIC_CLAIM(context->context), irq);
    assert_no_irq(qts);
    qtest_quit(qts);
}

static void test_c900_plic_arbitration(void)
{
    const uint32_t machine_irq = 100;
    const uint32_t supervisor_irq = 101;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");

    /* The machine-enable bit is the high arbitration bit in the C900 RTL. */
    qtest_writel(qts, C900_PLIC_PRIORITY(machine_irq), 1);
    qtest_writel(qts, C900_PLIC_PRIORITY(supervisor_irq), 31);
    c900_plic_set_enable(qts, 0, machine_irq, true);
    c900_plic_set_enable(qts, 1, supervisor_irq, true);
    c900_plic_set_pending(qts, machine_irq, true);
    c900_plic_set_pending(qts, supervisor_irq, true);
    assert_no_irq(qts);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(0)), ==, machine_irq);
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    supervisor_irq);
    assert_no_irq(qts);

    /* Equal priorities select the lower source ID. */
    for (uint32_t irq = 102; irq <= 103; irq++) {
        qtest_writel(qts, C900_PLIC_PRIORITY(irq), 7);
        c900_plic_set_enable(qts, 1, irq, true);
        c900_plic_set_pending(qts, irq, true);
    }
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==, 102);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==, 103);
    assert_no_irq(qts);

    /* Public RTL stores a claim candidate even when threshold blocks output. */
    qtest_writel(qts, C900_PLIC_PRIORITY(104), 3);
    c900_plic_set_enable(qts, 1, 104, true);
    qtest_writel(qts, C900_PLIC_THRESHOLD(1), 3);
    c900_plic_set_pending(qts, 104, true);
    assert_no_irq(qts);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==, 104);

    qtest_quit(qts);
}

static void test_c900_plic_trigger_modes(void)
{
    const uint32_t level_irq = 110;
    const uint32_t edge_irq = 111;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");

    qtest_writel(qts, C900_PLIC_PRIORITY(level_irq), 5);
    c900_plic_set_enable(qts, 1, level_irq, true);
    c900_plic_set_input(qts, "source", level_irq, 1);
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==, level_irq);
    assert_no_irq(qts);
    qtest_writel(qts, C900_PLIC_CLAIM(1), level_irq);
    assert_only_irq(qts, 0);
    c900_plic_set_input(qts, "source", level_irq, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==, level_irq);
    qtest_writel(qts, C900_PLIC_CLAIM(1), level_irq);
    assert_no_irq(qts);

    qtest_writel(qts, C900_PLIC_PRIORITY(edge_irq), 5);
    c900_plic_set_enable(qts, 1, edge_irq, true);
    c900_plic_set_input(qts, "edge-trigger", edge_irq, 1);
    c900_plic_set_input(qts, "source", edge_irq, 1);
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==, edge_irq);
    c900_plic_set_input(qts, "source", edge_irq, 0);
    c900_plic_set_input(qts, "source", edge_irq, 1);
    assert_no_irq(qts);
    qtest_writel(qts, C900_PLIC_CLAIM(1), edge_irq);
    assert_no_irq(qts);
    c900_plic_set_input(qts, "source", edge_irq, 1);
    assert_no_irq(qts);
    c900_plic_set_input(qts, "source", edge_irq, 0);
    c900_plic_set_input(qts, "source", edge_irq, 1);
    assert_only_irq(qts, 0);

    qtest_quit(qts);
}

static void test_c900_plic_completion_qualification(void)
{
    const uint32_t irq = 105;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(irq), 5);
    c900_plic_set_enable(qts, 1, irq, true);
    c900_plic_set_pending(qts, irq, true);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==, irq);
    assert_no_irq(qts);

    /* Clearing the claiming context's enable makes its completion a no-op. */
    c900_plic_set_enable(qts, 1, irq, false);
    qtest_writel(qts, C900_PLIC_CLAIM(1), irq);
    c900_plic_set_enable(qts, 1, irq, true);
    c900_plic_set_pending(qts, irq, true);
    assert_no_irq(qts);

    /* A different context also needs its enable bit before it can complete. */
    qtest_writel(qts, C900_PLIC_CLAIM(0), irq);
    assert_no_irq(qts);
    c900_plic_set_enable(qts, 0, irq, true);
    qtest_writel(qts, C900_PLIC_CLAIM(0), irq);
    assert_no_irq(qts);

    /* M wins while dual-enabled; removing it exposes the retained S pending. */
    c900_plic_set_enable(qts, 0, irq, false);
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==, irq);
    qtest_quit(qts);
}

static void assert_dw_uart_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, DW_UART_IER_DLH), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_UART_IIR_FCR), ==,
                    UART_IIR_NO_INT);
    g_assert_cmphex(qtest_readl(qts, DW_UART_LCR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_UART_LSR), ==,
                    UART_LSR_THRE | UART_LSR_TEMT);
    g_assert_cmphex(qtest_readl(qts, DW_UART_SCR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_UART_USR), ==,
                    UART_USR_TFNF | UART_USR_TFE);
    g_assert_cmphex(qtest_readl(qts, DW_UART_DLF), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_UART_CPR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_UART_UCV), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_UART_CTR), ==, 0x44570110);
}

static void test_dw_uart_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_dw_uart_reset_state(qts);

    qtest_writel(qts, DW_UART_SCR, 0x123456a5);
    g_assert_cmphex(qtest_readl(qts, DW_UART_SCR), ==, 0xa5);

    /* Unknown TH1520 synthesis options stay disabled until measured. */
    qtest_writel(qts, DW_UART_DLF, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, DW_UART_DLF), ==, 0);
    qtest_writel(qts, DW_UART_RE_EN, 1);
    g_assert_cmphex(qtest_readl(qts, DW_UART_RE_EN), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_UART_TFL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_UART_RFL), ==, 0);

    qtest_writel(qts, DW_UART_SRR, UART_SRR_UR);
    assert_dw_uart_reset_state(qts);

    qtest_writel(qts, DW_UART_SCR, 0x5a);
    qtest_system_reset(qts);
    assert_dw_uart_reset_state(qts);
    qtest_quit(qts);
}

static void test_dw_uart_configurable_features(void)
{
    QTestState *qts = qtest_init(
        "-machine beaglev-ahead -bios none "
        "-global dw-apb-uart.dlf-width=4 "
        "-global dw-apb-uart.fifo-size=32 "
        "-global dw-apb-uart.component-parameters=0x20000 "
        "-global dw-apb-uart.component-version=0x3331302a "
        "-global dw-apb-uart.fifo-stat=on");

    g_assert_cmphex(qtest_readl(qts, DW_UART_CPR), ==, 0x20000);
    g_assert_cmphex(qtest_readl(qts, DW_UART_UCV), ==, 0x3331302a);
    qtest_writel(qts, DW_UART_DLF, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, DW_UART_DLF), ==, 0xf);

    qtest_writel(qts, DW_UART_SRR, UART_SRR_UR);
    g_assert_cmphex(qtest_readl(qts, DW_UART_DLF), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_UART_IIR_FCR), ==,
                    UART_IIR_NO_INT);
    qtest_quit(qts);
}

static void test_dw_uart_tx_rx(void)
{
    QTestState *qts;
    int serial_fd;
    uint8_t input = 0x5a;

    qts = qtest_init_with_serial("-machine beaglev-ahead -bios none",
                                 &serial_fd);
    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    enable_uart0_supervisor_irq(qts);

    qtest_writel(qts, DW_UART_LCR, 3);
    qtest_writel(qts, DW_UART_RBR_THR_DLL, 0xa5);
    g_assert_cmphex(read_serial_byte(serial_fd), ==, 0xa5);
    g_assert_true(qtest_readl(qts, DW_UART_LSR) & UART_LSR_THRE);
    g_assert_false(qtest_readl(qts, DW_UART_LSR) & UART_LSR_TEMT);
    g_assert_true(qtest_readl(qts, DW_UART_USR) & UART_USR_BUSY);
    qtest_clock_step(qts, 5 * G_USEC_PER_SEC);
    g_assert_true(qtest_readl(qts, DW_UART_LSR) & UART_LSR_TEMT);

    qtest_writel(qts, DW_UART_IER_DLH, UART_IER_RDI);
    g_assert_cmpint(send(serial_fd, &input, sizeof(input), 0), ==,
                    sizeof(input));
    wait_for_uart_rx(qts);
    g_assert_true(qtest_readl(qts, DW_UART_USR) & UART_USR_RFNE);
    g_assert_true(qtest_readl(qts, DW_UART_USR) & UART_USR_RFF);
    g_assert_true(c900_plic_pending(qts, TH1520_UART0_IRQ));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    TH1520_UART0_IRQ);
    g_assert_cmphex(qtest_readl(qts, DW_UART_IIR_FCR), ==, UART_IIR_RDI);
    g_assert_cmphex(qtest_readl(qts, DW_UART_RBR_THR_DLL), ==, input);
    qtest_writel(qts, C900_PLIC_CLAIM(1), TH1520_UART0_IRQ);
    assert_no_irq(qts);

    g_assert_cmpint(send(serial_fd, &input, sizeof(input), 0), ==,
                    sizeof(input));
    wait_for_uart_rx(qts);
    qtest_writel(qts, DW_UART_SRR, UART_SRR_RFR);
    g_assert_false(qtest_readl(qts, DW_UART_LSR) & UART_LSR_DR);

    close(serial_fd);
    qtest_quit(qts);
}

static void test_dw_uart_interrupts(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");
    uint32_t usr;

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    enable_uart0_supervisor_irq(qts);

    qtest_writel(qts, DW_UART_IER_DLH, UART_IER_THRI);
    g_assert_true(c900_plic_pending(qts, TH1520_UART0_IRQ));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    TH1520_UART0_IRQ);
    g_assert_cmphex(qtest_readl(qts, DW_UART_IIR_FCR), ==, UART_IIR_THRI);
    qtest_writel(qts, C900_PLIC_CLAIM(1), TH1520_UART0_IRQ);
    assert_no_irq(qts);

    qtest_writel(qts, DW_UART_IER_DLH, 0);
    qtest_writel(qts, DW_UART_LCR, 3);
    qtest_writel(qts, DW_UART_RBR_THR_DLL, 'x');
    qtest_writel(qts, DW_UART_LCR, UART_LCR_DLAB | 3);
    g_assert_cmphex(qtest_readl(qts, DW_UART_LCR), ==, 3);
    g_assert_cmphex(qtest_readl(qts, DW_UART_IIR_FCR) & 0xf, ==,
                    UART_IIR_BUSY);
    g_assert_true(c900_plic_pending(qts, TH1520_UART0_IRQ));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    TH1520_UART0_IRQ);

    usr = qtest_readl(qts, DW_UART_USR);
    g_assert_true(usr & UART_USR_BUSY);
    g_assert_cmphex(qtest_readl(qts, DW_UART_IIR_FCR), ==,
                    UART_IIR_NO_INT);
    qtest_writel(qts, C900_PLIC_CLAIM(1), TH1520_UART0_IRQ);
    assert_no_irq(qts);

    qtest_clock_step(qts, 200 * G_USEC_PER_SEC);
    g_assert_true(qtest_readl(qts, DW_UART_LSR) & UART_LSR_TEMT);
    qtest_writel(qts, DW_UART_LCR, UART_LCR_DLAB | 3);
    g_assert_cmphex(qtest_readl(qts, DW_UART_LCR), ==,
                    UART_LCR_DLAB | 3);
    qtest_quit(qts);
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

static void test_c900_plic_migration(void)
{
    const uint32_t irq = 120;
    const uint32_t edge_irq = 121;
    const uint32_t context = 3;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-plic-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_irq_intercept_out_named(dst, C900_PLIC_QOM_PATH, "sext");

    qtest_writel(src, C900_PLIC_CONTROL, 1);
    qtest_writel(src, C900_PLIC_PRIORITY(irq), 7);
    qtest_writel(src, C900_PLIC_THRESHOLD(context), 2);
    c900_plic_set_enable(src, context, irq, true);
    c900_plic_set_input(src, "source", irq, 1);
    g_assert_cmphex(qtest_readl(src, C900_PLIC_CLAIM(context)), ==, irq);
    c900_plic_set_pending(src, irq, true);
    c900_plic_set_enable(src, context, edge_irq, true);
    c900_plic_set_input(src, "edge-trigger", edge_irq, 1);
    c900_plic_set_input(src, "source", edge_irq, 1);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, C900_PLIC_CONTROL), ==, 1);
    g_assert_cmphex(qtest_readl(dst, C900_PLIC_PRIORITY(irq)), ==, 7);
    g_assert_cmphex(qtest_readl(dst, C900_PLIC_THRESHOLD(context)), ==, 2);
    g_assert_true(qtest_readl(dst, C900_PLIC_ENABLE(context, irq >> 5)) &
                  (1U << (irq & 31)));
    g_assert_true(c900_plic_pending(dst, irq));
    g_assert_true(c900_plic_pending(dst, edge_irq));
    assert_no_irq(dst);

    /* Active and sampled input state survive; completion re-pends the level. */
    qtest_writel(dst, C900_PLIC_CLAIM(context), irq);
    assert_only_irq(dst, 1);
    g_assert_cmphex(qtest_readl(dst, C900_PLIC_CLAIM(context)), ==, irq);
    c900_plic_set_input(dst, "source", irq, 0);
    qtest_writel(dst, C900_PLIC_CLAIM(context), irq);
    assert_no_irq(dst);

    /* Trigger configuration is migrated, not reconstructed as level mode. */
    qtest_writel(dst, C900_PLIC_PRIORITY(edge_irq), 6);
    assert_only_irq(dst, 1);
    g_assert_cmphex(qtest_readl(dst, C900_PLIC_CLAIM(context)), ==,
                    edge_irq);
    qtest_writel(dst, C900_PLIC_CLAIM(context), edge_irq);
    assert_no_irq(dst);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_dw_uart_migration(void)
{
    const char *const properties =
        "-global dw-apb-uart.dlf-width=4 "
        "-global dw-apb-uart.component-parameters=0x10000";
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-uart-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_initf("-machine beaglev-ahead -bios none %s", properties);
    dst = qtest_initf("-machine beaglev-ahead -bios none -incoming defer %s",
                      properties);

    qtest_writel(src, DW_UART_LCR, UART_LCR_DLAB | 3);
    qtest_writel(src, DW_UART_RBR_THR_DLL, 0x34);
    qtest_writel(src, DW_UART_IER_DLH, 0x12);
    qtest_writel(src, DW_UART_DLF, 0xb);
    qtest_writel(src, DW_UART_LCR, 3);
    qtest_writel(src, DW_UART_SCR, 0x5a);
    qtest_writel(src, DW_UART_RBR_THR_DLL, 'm');
    qtest_writel(src, DW_UART_LCR, UART_LCR_DLAB | 3);
    g_assert_cmphex(qtest_readl(src, DW_UART_IIR_FCR) & 0xf, ==,
                    UART_IIR_BUSY);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, DW_UART_SCR), ==, 0x5a);
    g_assert_cmphex(qtest_readl(dst, DW_UART_DLF), ==, 0xb);
    g_assert_cmphex(qtest_readl(dst, DW_UART_LCR), ==, 3);
    g_assert_cmphex(qtest_readl(dst, DW_UART_IIR_FCR) & 0xf, ==,
                    UART_IIR_BUSY);
    g_assert_true(qtest_readl(dst, DW_UART_USR) & UART_USR_BUSY);
    g_assert_cmphex(qtest_readl(dst, DW_UART_IIR_FCR), ==,
                    UART_IIR_NO_INT);

    qtest_clock_step(dst, 20 * G_USEC_PER_SEC);
    qtest_writel(dst, DW_UART_LCR, UART_LCR_DLAB | 3);
    g_assert_cmphex(qtest_readl(dst, DW_UART_RBR_THR_DLL), ==, 0x34);
    g_assert_cmphex(qtest_readl(dst, DW_UART_IER_DLH), ==, 0x12);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_whole_machine_migration(void)
{
    const char *const properties =
        "-global dw-apb-uart.dlf-width=4 "
        "-global dw-apb-uart.component-parameters=0x10000";
    const uint32_t plic_irq = 88;
    const uint32_t plic_context = 5;
    const uint64_t dram_addr = 0x01000000;
    const uint64_t sram_addr = TH1520_SRAM_BASE + 0x12340;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-machine-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_initf("-machine beaglev-ahead -bios none %s", properties);
    dst = qtest_initf("-machine beaglev-ahead -bios none -incoming defer %s",
                      properties);

    qtest_writeq(src, dram_addr, 0x0123456789abcdefULL);
    qtest_writeq(src, sram_addr, 0xfedcba9876543210ULL);

    set_csr(src, 0, CSR_MSCRATCH, 0x1122334455667788ULL);
    set_csr(src, 1, CSR_TH_MCOR, 3);
    set_csr(src, 2, CSR_TH_MCOUNTERWEN, BIT(3));
    g_assert_cmphex(get_csr(src, 2, CSR_TH_CPUID), ==, 0x090c090d);
    g_assert_cmphex(get_csr(src, 2, CSR_TH_CPUID), ==, 0x110c9000);
    set_csr(src, 3, CSR_MSCRATCH, 0x8877665544332211ULL);

    g_assert_cmpint(qtest_clock_set(src, 1000), ==, 1000);
    qtest_writel(src, C900_MSIP(3), 1);
    qtest_writel(src, C900_SSIP(2), 1);
    write_compare(src, C900_MTIMECMP(1), 30);

    qtest_writel(src, C900_PLIC_CONTROL, 1);
    qtest_writel(src, C900_PLIC_PRIORITY(plic_irq), 9);
    qtest_writel(src, C900_PLIC_THRESHOLD(plic_context), 3);
    c900_plic_set_enable(src, plic_context, plic_irq, true);
    c900_plic_set_pending(src, plic_irq, true);

    qtest_writel(src, DW_UART_LCR, UART_LCR_DLAB | 3);
    qtest_writel(src, DW_UART_RBR_THR_DLL, 0x34);
    qtest_writel(src, DW_UART_IER_DLH, 0x12);
    qtest_writel(src, DW_UART_DLF, 0xb);
    qtest_writel(src, DW_UART_LCR, 3);
    qtest_writel(src, DW_UART_SCR, 0x5a);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readq(dst, dram_addr), ==,
                    0x0123456789abcdefULL);
    g_assert_cmphex(qtest_readq(dst, sram_addr), ==,
                    0xfedcba9876543210ULL);

    g_assert_cmphex(get_csr(dst, 0, CSR_MSCRATCH), ==,
                    0x1122334455667788ULL);
    g_assert_cmphex(get_csr(dst, 1, CSR_TH_MCOR), ==, 3);
    g_assert_cmphex(get_csr(dst, 2, CSR_TH_MCOUNTERWEN), ==, BIT(3));
    g_assert_cmphex(get_csr(dst, 2, CSR_TH_CPUID), ==, 0x260c0001);
    g_assert_cmphex(get_csr(dst, 3, CSR_MSCRATCH), ==,
                    0x8877665544332211ULL);
    g_assert_cmphex(get_csr(dst, 0, CSR_TIME), ==, 3);

    g_assert_cmphex(qtest_readl(dst, C900_MSIP(3)), ==, 1);
    g_assert_cmphex(qtest_readl(dst, C900_SSIP(2)), ==, 1);
    g_assert_cmphex(qtest_readl(dst, C900_MTIMECMP(1)), ==, 30);
    g_assert_cmphex(qtest_readl(dst, C900_MTIMECMP(1) + 4), ==, 0);

    g_assert_cmphex(qtest_readl(dst, C900_PLIC_CONTROL), ==, 1);
    g_assert_cmphex(qtest_readl(dst, C900_PLIC_PRIORITY(plic_irq)), ==, 9);
    g_assert_cmphex(qtest_readl(dst, C900_PLIC_THRESHOLD(plic_context)), ==,
                    3);
    g_assert_true(qtest_readl(dst,
                  C900_PLIC_ENABLE(plic_context, plic_irq >> 5)) &
                  BIT(plic_irq & 31));
    g_assert_true(c900_plic_pending(dst, plic_irq));

    g_assert_cmphex(qtest_readl(dst, DW_UART_LCR), ==, 3);
    g_assert_cmphex(qtest_readl(dst, DW_UART_DLF), ==, 0xb);
    g_assert_cmphex(qtest_readl(dst, DW_UART_SCR), ==, 0x5a);
    qtest_writel(dst, DW_UART_LCR, UART_LCR_DLAB | 3);
    g_assert_cmphex(qtest_readl(dst, DW_UART_RBR_THR_DLL), ==, 0x34);
    g_assert_cmphex(qtest_readl(dst, DW_UART_IER_DLH), ==, 0x12);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine("beaglev-ahead")) {
        qtest_add_func("/beaglev-ahead/boot/direct-contract",
                       test_direct_boot_contract);
        qtest_add_func("/beaglev-ahead/migration/whole-machine",
                       test_whole_machine_migration);
        qtest_add_func("/beaglev-ahead/c900-plic/reset",
                       test_c900_plic_reset);
        qtest_add_func("/beaglev-ahead/c900-plic/registers",
                       test_c900_plic_registers);
        for (size_t i = 0; i < ARRAY_SIZE(c900_plic_contexts); i++) {
            g_autofree char *name =
                g_strdup_printf("/beaglev-ahead/c900-plic/%s-hart%u",
                                c900_plic_contexts[i].output,
                                c900_plic_contexts[i].hart);

            qtest_add_data_func(name, &c900_plic_contexts[i],
                                test_c900_plic_context);
        }
        qtest_add_func("/beaglev-ahead/c900-plic/arbitration",
                       test_c900_plic_arbitration);
        qtest_add_func("/beaglev-ahead/c900-plic/trigger-modes",
                       test_c900_plic_trigger_modes);
        qtest_add_func("/beaglev-ahead/c900-plic/completion-qualification",
                       test_c900_plic_completion_qualification);
        qtest_add_func("/beaglev-ahead/c900-plic/migration",
                       test_c900_plic_migration);
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
        qtest_add_func("/beaglev-ahead/dw-uart/registers",
                       test_dw_uart_registers);
        qtest_add_func("/beaglev-ahead/dw-uart/configurable-features",
                       test_dw_uart_configurable_features);
        qtest_add_func("/beaglev-ahead/dw-uart/tx-rx",
                       test_dw_uart_tx_rx);
        qtest_add_func("/beaglev-ahead/dw-uart/interrupts",
                       test_dw_uart_interrupts);
        qtest_add_func("/beaglev-ahead/dw-uart/migration",
                       test_dw_uart_migration);
    }

    return g_test_run();
}
