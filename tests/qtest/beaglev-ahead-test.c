/*
 * BeagleV Ahead machine tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/units.h"
#include "hw/sd/sdhci.h"
#include "hw/sd/sdhci-internal.h"
#include "libqtest.h"
#include "libqos/sdhci-cmd.h"
#include "qobject/qdict.h"

#include <libfdt.h>
#include <poll.h>

#define TH1520_BROM_BASE           0xffffd00000ULL
#define TH1520_CLINT_BASE          0xffdc000000ULL
#define TH1520_PLIC_BASE           0xffd8000000ULL
#define TH1520_SRAM_BASE           0xffe0000000ULL
#define TH1520_UART0_BASE          0xffe7014000ULL
#define TH1520_EMMC_BASE           0xffe7080000ULL
#define TH1520_SDIO0_BASE          0xffe7090000ULL
#define TH1520_SDIO1_BASE          0xffe70a0000ULL
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
#define TH1520_EMMC_IRQ            62
#define TH1520_SDIO0_IRQ           64
#define TH1520_SDIO1_IRQ           71

#define DWCMSHC_VENDOR_POINTER     0x0e8
#define DWCMSHC_PHY_CNFG           0x300
#define DWCMSHC_PHY_CMDPAD_CNFG    0x304
#define DWCMSHC_PHY_DATAPAD_CNFG   0x306
#define DWCMSHC_PHY_CLKPAD_CNFG    0x308
#define DWCMSHC_PHY_STBPAD_CNFG    0x30a
#define DWCMSHC_PHY_RSTNPAD_CNFG   0x30c
#define DWCMSHC_PHY_PRBS_SEED      0x318
#define DWCMSHC_PHY_DLL_CNFG1      0x31e
#define DWCMSHC_PHY_SMPLDL_CNFG    0x320
#define DWCMSHC_PHY_DLL_CTRL       0x324
#define DWCMSHC_PHY_DLL_STATUS     0x32e
#define DWCMSHC_PHY_DLLDBG_MLKDC   0x330
#define DWCMSHC_PHY_DLLDBG_SLKDC   0x332
#define DWCMSHC_MSHC_VER_ID        0x500
#define DWCMSHC_MSHC_VER_TYPE      0x504
#define DWCMSHC_MSHC_CTRL          0x508
#define DWCMSHC_MBIU_CTRL          0x510
#define DWCMSHC_EMMC_CTRL          0x52c
#define DWCMSHC_BOOT_CTRL          0x52e
#define DWCMSHC_AT_CTRL            0x540
#define DWCMSHC_AT_STAT            0x544
#define DWCMSHC_EMBEDDED_CTRL      0xf6c

#define DWCMSHC_VENDOR_POINTER_RESET 0x01800500
#define DWCMSHC_CAPABILITIES_RESET   0x080081773f6dc881ULL
#define DWCMSHC_MAX_CURRENT_RESET    0x0000000000191919ULL
#define DWCMSHC_HOST_VERSION_RESET   0x0005
#define DWCMSHC_AT_CTRL_RESET        0x03000005
#define DWCMSHC_AT_STAT_RESET        0x00000006

#define DWCMSHC_PHY_RSTN           BIT(0)
#define DWCMSHC_PHY_PWRGOOD        BIT(1)
#define DWCMSHC_DLL_ENABLE         BIT(0)
#define DWCMSHC_DLL_UPDATE         BIT(2)
#define DWCMSHC_DLL_LOCK           BIT(0)

#define DWCMSHC_TEST_IMAGE_SIZE    (1 * MiB)
#define DWCMSHC_BLOCK_SIZE         512
#define DWCMSHC_ADMA_DESC_ADDR     (TH1520_SRAM_BASE + 0x10000)
#define DWCMSHC_ADMA_DATA_ADDR     (TH1520_SRAM_BASE + 0x20000)

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

typedef struct DWCMSHCController {
    const char *name;
    uint64_t base;
    uint32_t irq;
    uint32_t bus_width;
} DWCMSHCController;

static const DWCMSHCController dwcmshc_controllers[] = {
    { "emmc",  TH1520_EMMC_BASE,  TH1520_EMMC_IRQ,  8 },
    { "sdio0", TH1520_SDIO0_BASE, TH1520_SDIO0_IRQ, 4 },
    { "sdio1", TH1520_SDIO1_BASE, TH1520_SDIO1_IRQ, 4 },
};

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

static uint32_t fdt_prop_u32(const void *fdt, int node, const char *name)
{
    const fdt32_t *prop;
    int len;

    prop = fdt_getprop(fdt, node, name, &len);
    g_assert_nonnull(prop);
    g_assert_cmpint(len, ==, sizeof(*prop));
    return fdt32_to_cpu(*prop);
}

static void assert_fdt_bool(const void *fdt, int node, const char *name,
                            bool present)
{
    int len;
    const void *prop = fdt_getprop(fdt, node, name, &len);

    if (present) {
        g_assert_nonnull(prop);
        g_assert_cmpint(len, ==, 0);
    } else {
        g_assert_null(prop);
        g_assert_cmpint(len, ==, -FDT_ERR_NOTFOUND);
    }
}

static void assert_dwcmshc_fdt(const void *fdt,
                               const DWCMSHCController *controller,
                               uint32_t clock_phandle)
{
    g_autofree char *path =
        g_strdup_printf("/soc/mmc@%" PRIx64, controller->base);
    const fdt32_t *cells;
    const char *text;
    int node;
    int len;

    node = fdt_path_offset(fdt, path);
    g_assert_cmpint(node, >=, 0);

    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "thead,th1520-dwcmshc");
    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "okay");
    text = fdt_getprop(fdt, node, "clock-names", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "core");

    cells = fdt_getprop(fdt, node, "reg", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 4 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, controller->base >> 32);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==,
                    (uint32_t)controller->base);
    g_assert_cmphex(fdt32_to_cpu(cells[2]), ==, 0);
    g_assert_cmphex(fdt32_to_cpu(cells[3]), ==, 0x10000);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, controller->irq);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);

    g_assert_cmphex(fdt_prop_u32(fdt, node, "clocks"), ==,
                    clock_phandle);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "max-frequency"), ==,
                    198000000);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "bus-width"), ==,
                    controller->bus_width);

    assert_fdt_bool(fdt, node, "mmc-hs400-1_8v",
                    controller->base == TH1520_EMMC_BASE);
    assert_fdt_bool(fdt, node, "no-sd",
                    controller->base == TH1520_EMMC_BASE);
    assert_fdt_bool(fdt, node, "no-sdio",
                    controller->base == TH1520_EMMC_BASE);
    assert_fdt_bool(fdt, node, "non-removable",
                    controller->base != TH1520_SDIO0_BASE);
    assert_fdt_bool(fdt, node, "keep-power-in-suspend",
                    controller->base == TH1520_SDIO1_BASE);
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
    uint32_t mshc_clock_phandle;
    int clock_offset;
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

    clock_offset = fdt_path_offset(fdt, "/mshc-clock");
    g_assert_cmpint(clock_offset, >=, 0);
    compatible = fdt_getprop(fdt, clock_offset, "compatible", &len);
    g_assert_nonnull(compatible);
    g_assert_cmpstr(compatible, ==, "fixed-clock");
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "#clock-cells"), ==, 0);
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "clock-frequency"), ==,
                    198000000);
    mshc_clock_phandle = fdt_get_phandle(fdt, clock_offset);
    g_assert_cmphex(mshc_clock_phandle, !=, 0);

    for (size_t i = 0; i < ARRAY_SIZE(dwcmshc_controllers); i++) {
        assert_dwcmshc_fdt(fdt, &dwcmshc_controllers[i],
                           mshc_clock_phandle);
    }

    qtest_quit(qts);
}

static void assert_dwcmshc_reset_state(QTestState *qts, uint64_t base)
{
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_VENDOR_POINTER), ==,
                    DWCMSHC_VENDOR_POINTER_RESET);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_VENDOR_POINTER), ==,
                    0x500);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_VENDOR_POINTER + 2), ==,
                    0x180);
    g_assert_cmphex(qtest_readq(qts, base + SDHC_CAPAB), ==,
                    DWCMSHC_CAPABILITIES_RESET);
    g_assert_cmphex(qtest_readq(qts, base + SDHC_MAXCURR), ==,
                    DWCMSHC_MAX_CURRENT_RESET);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_HCVER), ==,
                    DWCMSHC_HOST_VERSION_RESET);
    g_assert_cmphex(qtest_readb(qts, base + SDHC_HOSTCTL), ==, 0);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_HOSTCTL2), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SDHC_ARGUMENT), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SDHC_ADMASYSADDR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SDHC_ADMASYSADDR + 4), ==, 0);

    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_MSHC_VER_ID), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_MSHC_VER_TYPE), ==, 0);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_MSHC_CTRL), ==, 1);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_MBIU_CTRL), ==, 0x0f);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_EMMC_CTRL), ==, 0x0c);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_BOOT_CTRL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_AT_CTRL), ==,
                    DWCMSHC_AT_CTRL_RESET);
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_AT_STAT), ==,
                    DWCMSHC_AT_STAT_RESET);
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_EMBEDDED_CTRL), ==, 0);

    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_PHY_CNFG), ==, 0);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_PHY_CMDPAD_CNFG), ==,
                    0x0440);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_PHY_DATAPAD_CNFG), ==,
                    0x0440);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_PHY_CLKPAD_CNFG), ==,
                    0x0440);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_PHY_STBPAD_CNFG), ==,
                    0x0440);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_PHY_RSTNPAD_CNFG), ==,
                    0x0440);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_PHY_PRBS_SEED), ==,
                    0xffff);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_PHY_SMPLDL_CNFG), ==,
                    0x0e);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_PHY_DLL_STATUS), ==, 0);
}

static void test_dwcmshc_registers(void)
{
    const uint64_t base = TH1520_EMMC_BASE;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (size_t i = 0; i < ARRAY_SIZE(dwcmshc_controllers); i++) {
        assert_dwcmshc_reset_state(qts, dwcmshc_controllers[i].base);
    }

    qtest_writel(qts, base + DWCMSHC_VENDOR_POINTER, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_VENDOR_POINTER), ==,
                    DWCMSHC_VENDOR_POINTER_RESET);
    qtest_writel(qts, base + DWCMSHC_MSHC_VER_ID, UINT32_MAX);
    qtest_writel(qts, base + DWCMSHC_MSHC_VER_TYPE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_MSHC_VER_ID), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_MSHC_VER_TYPE), ==, 0);

    qtest_writeb(qts, base + DWCMSHC_MSHC_CTRL, UINT8_MAX);
    qtest_writeb(qts, base + DWCMSHC_MBIU_CTRL, UINT8_MAX);
    qtest_writew(qts, base + DWCMSHC_EMMC_CTRL, UINT16_MAX);
    qtest_writew(qts, base + DWCMSHC_BOOT_CTRL, UINT16_MAX);
    qtest_writel(qts, base + DWCMSHC_AT_CTRL, UINT32_MAX);
    qtest_writel(qts, base + DWCMSHC_AT_STAT, 0xa5a5a5a5);
    qtest_writel(qts, base + DWCMSHC_EMBEDDED_CTRL, 0x89abcdef);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_MSHC_CTRL), ==, 0x11);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_MBIU_CTRL), ==, 0x0f);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_EMMC_CTRL), ==, 0x070f);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_BOOT_CTRL), ==, 0xf100);
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_AT_CTRL), ==,
                    0x7f1f0f1f);
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_AT_STAT), ==,
                    0x000000a5);
    g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_EMBEDDED_CTRL), ==,
                    0x09230000);

    qtest_writew(qts, base + DWCMSHC_PHY_CMDPAD_CNFG, UINT16_MAX);
    g_assert_cmphex(qtest_readw(qts, base + DWCMSHC_PHY_CMDPAD_CNFG), ==,
                    0x1fff);
    qtest_writeb(qts, base + DWCMSHC_PHY_DLL_CNFG1, 0x55);
    qtest_writeb(qts, base + DWCMSHC_PHY_CNFG, DWCMSHC_PHY_RSTN);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_PHY_CNFG), ==,
                    DWCMSHC_PHY_RSTN | DWCMSHC_PHY_PWRGOOD);
    qtest_writeb(qts, base + DWCMSHC_PHY_DLL_CTRL,
                  DWCMSHC_DLL_ENABLE | DWCMSHC_DLL_UPDATE);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_PHY_DLL_CTRL), ==,
                    DWCMSHC_DLL_ENABLE);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_PHY_DLL_STATUS), ==,
                    DWCMSHC_DLL_LOCK);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_PHY_DLLDBG_MLKDC), ==,
                    0x55);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_PHY_DLLDBG_SLKDC), ==,
                    0x55);
    qtest_writeb(qts, base + DWCMSHC_PHY_CNFG, 0);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_PHY_CNFG), ==, 0);
    g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_PHY_DLL_STATUS), ==, 0);

    /* The three 64 KiB apertures must contain independent state. */
    assert_dwcmshc_reset_state(qts, TH1520_SDIO0_BASE);
    assert_dwcmshc_reset_state(qts, TH1520_SDIO1_BASE);

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(dwcmshc_controllers); i++) {
        assert_dwcmshc_reset_state(qts, dwcmshc_controllers[i].base);
    }
    qtest_quit(qts);
}

static void test_dwcmshc_configurable_ids(void)
{
    QTestState *qts = qtest_init(
        "-machine beaglev-ahead -bios none "
        "-global dwcmshc.mshc-version-id=0x3130302a "
        "-global dwcmshc.mshc-version-type=0x4457432a");

    for (size_t i = 0; i < ARRAY_SIZE(dwcmshc_controllers); i++) {
        uint64_t base = dwcmshc_controllers[i].base;

        g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_MSHC_VER_ID), ==,
                        0x3130302a);
        g_assert_cmphex(qtest_readl(qts, base + DWCMSHC_MSHC_VER_TYPE), ==,
                        0x4457432a);
    }
    qtest_quit(qts);
}

static void test_dwcmshc_interrupt(const void *opaque)
{
    const DWCMSHCController *controller = opaque;
    uint64_t base = controller->base;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(controller->irq), 5);
    c900_plic_set_enable(qts, 1, controller->irq, true);

    qtest_writew(qts, base + SDHC_CLKCON,
                  SDHC_CLOCK_SDCLK_EN | SDHC_CLOCK_INT_STABLE |
                  SDHC_CLOCK_INT_EN);
    qtest_writew(qts, base + SDHC_ERRINTSTSEN,
                  SDHC_EISEN_CMDTIMEOUT);
    qtest_writew(qts, base + SDHC_ERRINTSIGEN,
                  SDHC_EISEN_CMDTIMEOUT);
    sdhci_cmd_regs(qts, base, 0, 0, 0, 0,
                   (13 << 8) | SDHC_CMD_RESPONSE);

    g_assert_true(qtest_readw(qts, base + SDHC_NORINTSTS) & SDHC_NIS_ERR);
    g_assert_true(qtest_readw(qts, base + SDHC_ERRINTSTS) &
                  SDHC_EIS_CMDTIMEOUT);
    g_assert_true(c900_plic_pending(qts, controller->irq));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    controller->irq);
    assert_no_irq(qts);

    qtest_writew(qts, base + SDHC_ERRINTSTS, SDHC_EIS_CMDTIMEOUT);
    qtest_writel(qts, C900_PLIC_CLAIM(1), controller->irq);
    g_assert_false(c900_plic_pending(qts, controller->irq));
    assert_no_irq(qts);
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

static char *dwcmshc_create_image(const void *initial, size_t initial_size)
{
    g_autoptr(GError) error = NULL;
    char *path = NULL;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-storage-XXXXXX", &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    g_assert_nonnull(path);
    g_assert_cmpint(ftruncate(fd, DWCMSHC_TEST_IMAGE_SIZE), ==, 0);
    if (initial_size) {
        g_assert_cmpint(pwrite(fd, initial, initial_size, 0), ==,
                        initial_size);
    }
    g_assert_cmpint(fsync(fd), ==, 0);
    close(fd);
    return path;
}

static void dwcmshc_enable_card(QTestState *qts, uint64_t base)
{
    qtest_writeb(qts, base + SDHC_SWRST, SDHC_RESET_ALL);
    qtest_writeb(qts, base + SDHC_PWRCON,
                  SDHC_POWER_ON | (7 << R_SDHC_PWRCON_BUS_VOLTAGE_SHIFT));
    qtest_writew(qts, base + SDHC_CLKCON,
                  SDHC_CLOCK_SDCLK_EN | SDHC_CLOCK_INT_STABLE |
                  SDHC_CLOCK_INT_EN);
    g_assert_true(qtest_readl(qts, base + SDHC_PRNSTS) &
                  SDHC_CARD_PRESENT);
}

static void dwcmshc_init_sd(QTestState *qts, uint64_t base)
{
    uint16_t rca;

    dwcmshc_enable_card(qts, base);
    sdhci_cmd_regs(qts, base, 0, 0, 0, 0, 0 << 8);
    sdhci_cmd_regs(qts, base, 0, 0, 0, 0, SDHC_APP_CMD);
    sdhci_cmd_regs(qts, base, 0, 0, 0x41200000, 0, 41 << 8);
    sdhci_cmd_regs(qts, base, 0, 0, 0, 0,
                   SDHC_ALL_SEND_CID | SDHC_CMD_RESPONSE);
    sdhci_cmd_regs(qts, base, 0, 0, 0, 0,
                   SDHC_SEND_RELATIVE_ADDR | SDHC_CMD_RESPONSE);
    rca = qtest_readl(qts, base + SDHC_RSPREG0) >> 16;
    g_assert_cmphex(rca, !=, 0);
    sdhci_cmd_regs(qts, base, 0, 0, rca << 16, 0,
                   SDHC_SELECT_DESELECT_CARD | SDHC_CMD_RESPONSE);
}

static void dwcmshc_init_emmc(QTestState *qts, uint64_t base)
{
    const uint32_t rca = 1;

    dwcmshc_enable_card(qts, base);
    sdhci_cmd_regs(qts, base, 0, 0, 0, 0, 0 << 8);
    sdhci_cmd_regs(qts, base, 0, 0, 0x40ff8080, 0,
                   (1 << 8) | SDHC_CMD_RESPONSE);
    sdhci_cmd_regs(qts, base, 0, 0, 0, 0,
                   SDHC_ALL_SEND_CID | SDHC_CMD_RESPONSE);
    sdhci_cmd_regs(qts, base, 0, 0, rca << 16, 0,
                   SDHC_SEND_RELATIVE_ADDR | SDHC_CMD_RESPONSE);
    sdhci_cmd_regs(qts, base, 0, 0, rca << 16, 0,
                   SDHC_SELECT_DESELECT_CARD | SDHC_CMD_RESPONSE);
}

static void dwcmshc_pio_read_block(QTestState *qts, uint64_t base,
                                    uint32_t argument, uint8_t *data)
{
    sdhci_cmd_regs(qts, base, DWCMSHC_BLOCK_SIZE, 1, argument,
                   SDHC_TRNS_READ | SDHC_TRNS_BLK_CNT_EN,
                   (17 << 8) | SDHC_CMD_RESPONSE | SDHC_CMD_DATA_PRESENT);
    for (size_t i = 0; i < DWCMSHC_BLOCK_SIZE; i += sizeof(uint32_t)) {
        stl_le_p(&data[i], qtest_readl(qts, base + SDHC_BDATA));
    }
}

static void dwcmshc_pio_write_block(QTestState *qts, uint64_t base,
                                     uint32_t argument, const uint8_t *data)
{
    sdhci_cmd_regs(qts, base, DWCMSHC_BLOCK_SIZE, 1, argument,
                   SDHC_TRNS_BLK_CNT_EN,
                   (24 << 8) | SDHC_CMD_RESPONSE | SDHC_CMD_DATA_PRESENT);
    for (size_t i = 0; i < DWCMSHC_BLOCK_SIZE; i += sizeof(uint32_t)) {
        qtest_writel(qts, base + SDHC_BDATA, ldl_le_p(&data[i]));
    }
}

static void test_dwcmshc_emmc_pio(void)
{
    uint8_t initial[DWCMSHC_BLOCK_SIZE];
    uint8_t replacement[DWCMSHC_BLOCK_SIZE];
    uint8_t actual[DWCMSHC_BLOCK_SIZE];
    g_autofree char *path = NULL;
    QTestState *qts;
    int fd;

    for (size_t i = 0; i < sizeof(initial); i++) {
        initial[i] = i ^ 0xa5;
        replacement[i] = 0xff - i;
    }
    path = dwcmshc_create_image(initial, sizeof(initial));
    qts = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=0,file=%s,format=raw,auto-read-only=off",
        path);

    dwcmshc_init_emmc(qts, TH1520_EMMC_BASE);
    dwcmshc_pio_read_block(qts, TH1520_EMMC_BASE, 0, actual);
    g_assert_cmpmem(actual, sizeof(actual), initial, sizeof(initial));
    dwcmshc_pio_write_block(qts, TH1520_EMMC_BASE, 0, replacement);
    qtest_quit(qts);

    fd = open(path, O_RDONLY);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(read(fd, actual, sizeof(actual)), ==, sizeof(actual));
    close(fd);
    g_assert_cmpmem(actual, sizeof(actual), replacement, sizeof(replacement));
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void dwcmshc_write_adma_address(QTestState *qts, uint64_t base,
                                        uint64_t address)
{
    qtest_writel(qts, base + SDHC_ADMASYSADDR, address);
    qtest_writel(qts, base + SDHC_ADMASYSADDR + 4, address >> 32);
}

static uint64_t dwcmshc_read_adma_address(QTestState *qts, uint64_t base)
{
    uint64_t low = qtest_readl(qts, base + SDHC_ADMASYSADDR);
    uint64_t high = qtest_readl(qts, base + SDHC_ADMASYSADDR + 4);

    return low | (high << 32);
}

static void test_dwcmshc_v4_adma(void)
{
    uint8_t expected[2 * DWCMSHC_BLOCK_SIZE];
    uint8_t actual[sizeof(expected)];
    uint8_t descriptor[16] = {};
    g_autofree char *path = NULL;
    QTestState *qts;

    for (size_t i = 0; i < sizeof(expected); i++) {
        expected[i] = (i * 29) ^ (i >> 3) ^ 0x6d;
    }
    path = dwcmshc_create_image(expected, sizeof(expected));
    qts = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=1,file=%s,format=raw,auto-read-only=off",
        path);
    dwcmshc_init_sd(qts, TH1520_SDIO0_BASE);

    descriptor[0] = SDHC_ADMA_ATTR_VALID | SDHC_ADMA_ATTR_END |
                    SDHC_ADMA_ATTR_ACT_TRAN;
    stw_le_p(&descriptor[2], sizeof(expected));
    stq_le_p(&descriptor[4], DWCMSHC_ADMA_DATA_ADDR);
    memset(actual, 0xa5, sizeof(actual));
    qtest_memwrite(qts, DWCMSHC_ADMA_DESC_ADDR,
                   descriptor, sizeof(descriptor));
    qtest_memwrite(qts, DWCMSHC_ADMA_DATA_ADDR, actual, sizeof(actual));

    qtest_writeb(qts, TH1520_SDIO0_BASE + SDHC_HOSTCTL,
                  SDHC_CTRL_ADMA2_32);
    qtest_writew(qts, TH1520_SDIO0_BASE + SDHC_HOSTCTL2,
                  R_SDHC_HOSTCTL2_VERSION4_MASK |
                  R_SDHC_HOSTCTL2_ADDRESSING_MASK |
                  R_SDHC_HOSTCTL2_CMD23_ENA_MASK);
    qtest_writew(qts, TH1520_SDIO0_BASE + SDHC_NORINTSTSEN,
                  SDHC_NISEN_CMDCMP | SDHC_NISEN_TRSCMP);
    dwcmshc_write_adma_address(qts, TH1520_SDIO0_BASE,
                                DWCMSHC_ADMA_DESC_ADDR);

    sdhci_cmd_regs(qts, TH1520_SDIO0_BASE, DWCMSHC_BLOCK_SIZE, 2, 0,
                   SDHC_TRNS_DMA | SDHC_TRNS_BLK_CNT_EN |
                   SDHC_TRNS_ACMD_AUTO | SDHC_TRNS_READ | SDHC_TRNS_MULTI,
                   (18 << 8) | SDHC_CMD_RESPONSE | SDHC_CMD_DATA_PRESENT);

    qtest_memread(qts, DWCMSHC_ADMA_DATA_ADDR, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    g_assert_cmphex(dwcmshc_read_adma_address(qts, TH1520_SDIO0_BASE), ==,
                    DWCMSHC_ADMA_DESC_ADDR + sizeof(descriptor));
    g_assert_cmphex(qtest_readw(qts, TH1520_SDIO0_BASE + SDHC_BLKCNT), ==,
                    0);
    g_assert_true(qtest_readw(qts,
                  TH1520_SDIO0_BASE + SDHC_NORINTSTS) & SDHC_NIS_CMDCMP);
    g_assert_true(qtest_readw(qts,
                  TH1520_SDIO0_BASE + SDHC_NORINTSTS) & SDHC_NIS_TRSCMP);
    g_assert_cmphex(qtest_readw(qts,
                    TH1520_SDIO0_BASE + SDHC_ERRINTSTS), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                    TH1520_SDIO0_BASE + SDHC_RSPREG3), !=, 0);

    /* Invalid v4 descriptors must stop in fetch state and signal source 64. */
    memset(descriptor, 0, sizeof(descriptor));
    qtest_memwrite(qts, DWCMSHC_ADMA_DESC_ADDR,
                   descriptor, sizeof(descriptor));
    dwcmshc_write_adma_address(qts, TH1520_SDIO0_BASE,
                                DWCMSHC_ADMA_DESC_ADDR);
    qtest_writew(qts, TH1520_SDIO0_BASE + SDHC_NORINTSTS,
                  SDHC_NIS_CMDCMP | SDHC_NIS_TRSCMP);
    qtest_writew(qts, TH1520_SDIO0_BASE + SDHC_ERRINTSTSEN,
                  SDHC_EISEN_ADMAERR);
    qtest_writew(qts, TH1520_SDIO0_BASE + SDHC_ERRINTSIGEN,
                  SDHC_EISEN_ADMAERR);
    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(TH1520_SDIO0_IRQ), 5);
    c900_plic_set_enable(qts, 1, TH1520_SDIO0_IRQ, true);

    sdhci_cmd_regs(qts, TH1520_SDIO0_BASE, DWCMSHC_BLOCK_SIZE, 1, 0,
                   SDHC_TRNS_DMA | SDHC_TRNS_BLK_CNT_EN | SDHC_TRNS_READ,
                   (17 << 8) | SDHC_CMD_RESPONSE | SDHC_CMD_DATA_PRESENT);
    g_assert_true(qtest_readw(qts,
                  TH1520_SDIO0_BASE + SDHC_NORINTSTS) & SDHC_NIS_ERR);
    g_assert_true(qtest_readw(qts,
                  TH1520_SDIO0_BASE + SDHC_ERRINTSTS) & SDHC_EIS_ADMAERR);
    g_assert_cmphex(qtest_readb(qts,
                    TH1520_SDIO0_BASE + SDHC_ADMAERR) &
                    SDHC_ADMAERR_STATE_MASK, ==,
                    SDHC_ADMAERR_STATE_ST_FDS);
    g_assert_true(c900_plic_pending(qts, TH1520_SDIO0_IRQ));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    TH1520_SDIO0_IRQ);
    qtest_writew(qts, TH1520_SDIO0_BASE + SDHC_ERRINTSTS,
                  SDHC_EIS_ADMAERR);
    qtest_writel(qts, C900_PLIC_CLAIM(1), TH1520_SDIO0_IRQ);
    assert_no_irq(qts);

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
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

static void test_dwcmshc_migration(void)
{
    const uint64_t base = TH1520_EMMC_BASE;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-dwcmshc-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    qtest_writeb(src, base + DWCMSHC_MSHC_CTRL, 0x10);
    qtest_writeb(src, base + DWCMSHC_MBIU_CTRL, 0x05);
    qtest_writew(src, base + DWCMSHC_EMMC_CTRL, 0x0305);
    qtest_writew(src, base + DWCMSHC_BOOT_CTRL, 0x3101);
    qtest_writel(src, base + DWCMSHC_AT_CTRL, 0x40100510);
    qtest_writel(src, base + DWCMSHC_AT_STAT, 0x5a);
    qtest_writel(src, base + DWCMSHC_EMBEDDED_CTRL, 0x76543210);
    qtest_writeb(src, base + DWCMSHC_PHY_DLL_CNFG1, 0x2a);
    qtest_writeb(src, base + DWCMSHC_PHY_CNFG, DWCMSHC_PHY_RSTN);
    qtest_writeb(src, base + DWCMSHC_PHY_DLL_CTRL, DWCMSHC_DLL_ENABLE);

    qtest_writeb(src, base + SDHC_HOSTCTL, SDHC_CTRL_ADMA2_32);
    qtest_writew(src, base + SDHC_HOSTCTL2,
                  R_SDHC_HOSTCTL2_VERSION4_MASK |
                  R_SDHC_HOSTCTL2_ADDRESSING_MASK |
                  R_SDHC_HOSTCTL2_CMD23_ENA_MASK);
    qtest_writel(src, base + SDHC_ARGUMENT, 0x89abcdef);
    dwcmshc_write_adma_address(src, base, DWCMSHC_ADMA_DESC_ADDR);

    /* A distinct second instance proves array elements do not alias. */
    qtest_writeb(src, TH1520_SDIO1_BASE + DWCMSHC_MSHC_CTRL, 0x11);
    qtest_writel(src, TH1520_SDIO1_BASE + DWCMSHC_EMBEDDED_CTRL,
                  0x0badf00d);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readb(dst, base + DWCMSHC_MSHC_CTRL), ==, 0x10);
    g_assert_cmphex(qtest_readb(dst, base + DWCMSHC_MBIU_CTRL), ==, 0x05);
    g_assert_cmphex(qtest_readw(dst, base + DWCMSHC_EMMC_CTRL), ==, 0x0305);
    g_assert_cmphex(qtest_readw(dst, base + DWCMSHC_BOOT_CTRL), ==, 0x3100);
    g_assert_cmphex(qtest_readl(dst, base + DWCMSHC_AT_CTRL), ==,
                    0x40100510);
    g_assert_cmphex(qtest_readl(dst, base + DWCMSHC_AT_STAT), ==, 0x5a);
    g_assert_cmphex(qtest_readl(dst, base + DWCMSHC_EMBEDDED_CTRL), ==,
                    0x76540000);
    g_assert_cmphex(qtest_readb(dst, base + DWCMSHC_PHY_CNFG), ==,
                    DWCMSHC_PHY_RSTN | DWCMSHC_PHY_PWRGOOD);
    g_assert_cmphex(qtest_readb(dst, base + DWCMSHC_PHY_DLL_STATUS), ==,
                    DWCMSHC_DLL_LOCK);
    g_assert_cmphex(qtest_readb(dst, base + DWCMSHC_PHY_DLLDBG_MLKDC), ==,
                    0x2a);
    g_assert_cmphex(qtest_readb(dst, base + SDHC_HOSTCTL), ==,
                    SDHC_CTRL_ADMA2_32);
    g_assert_cmphex(qtest_readw(dst, base + SDHC_HOSTCTL2), ==,
                    R_SDHC_HOSTCTL2_VERSION4_MASK |
                    R_SDHC_HOSTCTL2_ADDRESSING_MASK |
                    R_SDHC_HOSTCTL2_CMD23_ENA_MASK);
    g_assert_cmphex(qtest_readl(dst, base + SDHC_ARGUMENT), ==,
                    0x89abcdef);
    g_assert_cmphex(dwcmshc_read_adma_address(dst, base), ==,
                    DWCMSHC_ADMA_DESC_ADDR);
    g_assert_cmphex(qtest_readb(dst,
                    TH1520_SDIO1_BASE + DWCMSHC_MSHC_CTRL), ==, 0x11);
    g_assert_cmphex(qtest_readl(dst,
                    TH1520_SDIO1_BASE + DWCMSHC_EMBEDDED_CTRL), ==,
                    0x0b250000);
    assert_dwcmshc_reset_state(dst, TH1520_SDIO0_BASE);

    qtest_system_reset(dst);
    for (size_t i = 0; i < ARRAY_SIZE(dwcmshc_controllers); i++) {
        assert_dwcmshc_reset_state(dst, dwcmshc_controllers[i].base);
    }

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
        qtest_add_func("/beaglev-ahead/dwcmshc/registers",
                       test_dwcmshc_registers);
        qtest_add_func("/beaglev-ahead/dwcmshc/configurable-ids",
                       test_dwcmshc_configurable_ids);
        for (size_t i = 0; i < ARRAY_SIZE(dwcmshc_controllers); i++) {
            g_autofree char *name =
                g_strdup_printf("/beaglev-ahead/dwcmshc/%s-interrupt",
                                dwcmshc_controllers[i].name);

            qtest_add_data_func(name, &dwcmshc_controllers[i],
                                test_dwcmshc_interrupt);
        }
        qtest_add_func("/beaglev-ahead/dwcmshc/emmc-pio",
                       test_dwcmshc_emmc_pio);
        qtest_add_func("/beaglev-ahead/dwcmshc/v4-64bit-adma",
                       test_dwcmshc_v4_adma);
        qtest_add_func("/beaglev-ahead/dwcmshc/migration",
                       test_dwcmshc_migration);
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
