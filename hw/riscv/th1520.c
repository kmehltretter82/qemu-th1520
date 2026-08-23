/*
 * T-Head TH1520 SoC and BeagleV Ahead board
 *
 * This starts with the software-visible boot-critical subset of the SoC.
 * Unverified behavior and later peripherals are tracked in
 * docs/devel/beaglev-ahead-hardware-validation.md and
 * docs/devel/beaglev-ahead-emulation-plan.md.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu-qom.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "net/eth.h"
#include "net/net.h"
#include "system/device_tree.h"
#include "system/blockdev.h"
#include "system/system.h"
#include "system/memory.h"
#include "target/riscv/cpu.h"
#include "target/riscv/cpu_bits.h"
#include "hw/char/dw_apb_uart.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/intc/thead_c900_clint.h"
#include "hw/intc/thead_c900_plic.h"
#include "hw/net/dw_gmac.h"
#include "hw/net/th1520_gmac.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/fdt-common.h"
#include "hw/riscv/machines-qom.h"
#include "hw/riscv/th1520.h"
#include "hw/sd/sd.h"

#include <libfdt.h>

static const MemMapEntry th1520_memmap[] = {
    [TH1520_DEV_DRAM]  = { 0x0000000000, 0x100000000 },
    [TH1520_DEV_PLIC]  = { 0xffd8000000, 0x01000000 },
    [TH1520_DEV_CLINT] = { 0xffdc000000, 0x00010000 },
    [TH1520_DEV_SRAM]  = { 0xffe0000000, 0x00180000 },
    [TH1520_DEV_UART0] = { 0xffe7014000, 0x00000100 },
    [TH1520_DEV_DMAC0] = { 0xffefc00000, 0x00001000 },
    [TH1520_DEV_GMAC0] = { 0xffe7070000, 0x00002000 },
    [TH1520_DEV_GMAC1] = { 0xffe7060000, 0x00002000 },
    [TH1520_DEV_GMAC0_APB] = { 0xffec003000, 0x00001000 },
    [TH1520_DEV_GMAC1_APB] = { 0xffec004000, 0x00001000 },
    [TH1520_DEV_EMMC]  = { 0xffe7080000, 0x00010000 },
    [TH1520_DEV_SDIO0] = { 0xffe7090000, 0x00010000 },
    [TH1520_DEV_SDIO1] = { 0xffe70a0000, 0x00010000 },
    [TH1520_DEV_BROM]  = { 0xffffd00000, 0x00100000 },
};

static const int th1520_mshc_memmap[TH1520_MSHC_COUNT] = {
    TH1520_DEV_EMMC,
    TH1520_DEV_SDIO0,
    TH1520_DEV_SDIO1,
};

static const uint32_t th1520_mshc_irqs[TH1520_MSHC_COUNT] = {
    TH1520_EMMC_IRQ,
    TH1520_SDIO0_IRQ,
    TH1520_SDIO1_IRQ,
};

static const int th1520_gmac_memmap[TH1520_GMAC_COUNT] = {
    TH1520_DEV_GMAC0,
    TH1520_DEV_GMAC1,
};

static const int th1520_gmac_apb_memmap[TH1520_GMAC_COUNT] = {
    TH1520_DEV_GMAC0_APB,
    TH1520_DEV_GMAC1_APB,
};

static const uint32_t th1520_gmac_irqs[TH1520_GMAC_COUNT] = {
    TH1520_GMAC0_IRQ,
    TH1520_GMAC1_IRQ,
};

static GlobalProperty beaglev_ahead_cpu_defaults[] = {
    /* CPU-011: do not advertise standardized Sdtrig without evidence. */
    { TYPE_RISCV_CPU_THEAD_C910, "debug", "off" },
};

static void th1520_create_pmu_fdt(void *fdt)
{
    static const uint32_t events[] = {
        0x00003, 0x00004, 0x00005, 0x00006,
        0x00007, 0x00008, 0x00009, 0x0000a,
        0x10000, 0x10001, 0x10002, 0x10003,
        0x10010, 0x10011, 0x10012, 0x10013,
    };
    static const uint32_t selectors[] = {
        0x01, 0x02, 0x07, 0x06,
        0x08, 0x09, 0x0a, 0x0b,
        0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13,
    };
    uint32_t event_ctr_map[ARRAY_SIZE(events) * 3];
    uint32_t event_selector_map[ARRAY_SIZE(events) * 3];
    uint32_t raw_event_ctr_map[42 * 5];
    const uint32_t counter_mask = 0x0007fff8;

    /*
     * These bindings match the upstream TH1520 DTS.  Several microarchitectural
     * events remain intentionally zero until they can be calibrated on the
     * board; see the hardware-validation ledger.
     */
    for (size_t i = 0; i < ARRAY_SIZE(events); i++) {
        event_ctr_map[i * 3] = cpu_to_be32(events[i]);
        event_ctr_map[i * 3 + 1] = cpu_to_be32(events[i]);
        event_ctr_map[i * 3 + 2] = cpu_to_be32(counter_mask);

        event_selector_map[i * 3] = cpu_to_be32(events[i]);
        event_selector_map[i * 3 + 1] = 0;
        event_selector_map[i * 3 + 2] = cpu_to_be32(selectors[i]);
    }

    for (size_t i = 0; i < 42; i++) {
        raw_event_ctr_map[i * 5] = 0;
        raw_event_ctr_map[i * 5 + 1] = cpu_to_be32(i + 1);
        raw_event_ctr_map[i * 5 + 2] = cpu_to_be32(UINT32_MAX);
        raw_event_ctr_map[i * 5 + 3] = cpu_to_be32(UINT32_MAX);
        raw_event_ctr_map[i * 5 + 4] = cpu_to_be32(counter_mask);
    }

    qemu_fdt_add_subnode(fdt, "/pmu");
    qemu_fdt_setprop_string(fdt, "/pmu", "compatible", "riscv,pmu");
    qemu_fdt_setprop(fdt, "/pmu", "riscv,event-to-mhpmcounters",
                     event_ctr_map, sizeof(event_ctr_map));
    qemu_fdt_setprop(fdt, "/pmu", "riscv,event-to-mhpmevent",
                     event_selector_map, sizeof(event_selector_map));
    qemu_fdt_setprop(fdt, "/pmu", "riscv,raw-event-to-mhpmcounters",
                     raw_event_ctr_map, sizeof(raw_event_ctr_map));
}

static void th1520_soc_init(Object *obj)
{
    static const char *const mshc_names[TH1520_MSHC_COUNT] = {
        "emmc", "sdio0", "sdio1",
    };
    static const char *const gmac_names[TH1520_GMAC_COUNT] = {
        "gmac0", "gmac1",
    };
    static const char *const gmac_apb_names[TH1520_GMAC_COUNT] = {
        "gmac0-apb", "gmac1-apb",
    };
    TH1520SoCState *s = RISCV_TH1520_SOC(obj);

    object_initialize_child(obj, "c910-cpus", &s->c910_cpus,
                            TYPE_RISCV_HART_ARRAY);
    object_initialize_child(obj, "clint", &s->clint,
                            TYPE_THEAD_C900_CLINT);
    object_initialize_child(obj, "plic", &s->plic,
                            TYPE_THEAD_C900_PLIC);
    object_initialize_child(obj, "uart0", &s->uart0, TYPE_DW_APB_UART);
    object_initialize_child(obj, "dmac0", &s->dmac0, TYPE_DW_AXI_DMAC);
    qdev_prop_set_uint32(DEVICE(&s->dmac0), "num-channels",
                         TH1520_DMAC_CHANNELS);
    qdev_prop_set_uint32(DEVICE(&s->dmac0), "block-size",
                         TH1520_DMAC_BLOCK_SIZE);
    qdev_prop_set_uint32(DEVICE(&s->dmac0), "data-width",
                         TH1520_DMAC_DATA_WIDTH);
    for (int i = 0; i < TH1520_GMAC_COUNT; i++) {
        object_initialize_child(obj, gmac_names[i], &s->gmac[i],
                                TYPE_DW_GMAC);
        object_initialize_child(obj, gmac_apb_names[i], &s->gmac_apb[i],
                                TYPE_TH1520_GMAC_APB);
        qdev_prop_set_uint32(DEVICE(&s->gmac[i]), "version",
                             TH1520_GMAC_VERSION);
        qdev_prop_set_uint32(DEVICE(&s->gmac[i]), "hw-feature",
                             TH1520_GMAC_HW_FEATURE);
        qdev_prop_set_uint8(DEVICE(&s->gmac[i]), "phy-addr",
                            TH1520_GMAC_PHY_ADDR);
        qdev_prop_set_uint16(DEVICE(&s->gmac[i]), "phy-id1",
                             TH1520_RTL8211F_PHY_ID1);
        qdev_prop_set_uint16(DEVICE(&s->gmac[i]), "phy-id2",
                             TH1520_RTL8211F_PHY_ID2);
    }
    for (int i = 0; i < TH1520_MSHC_COUNT; i++) {
        object_initialize_child(obj, mshc_names[i], &s->mshc[i],
                                TYPE_DWC_MSHC);
    }
    qdev_prop_set_uint32(DEVICE(&s->c910_cpus), "hartid-base", 0);
    qdev_prop_set_uint32(DEVICE(&s->c910_cpus), "num-harts",
                         TH1520_C910_HARTS);
    qdev_prop_set_string(DEVICE(&s->c910_cpus), "cpu-type",
                         TYPE_RISCV_CPU_THEAD_C910);
    qdev_prop_set_uint64(DEVICE(&s->c910_cpus), "resetvec",
                         th1520_memmap[TH1520_DEV_BROM].base);
    qdev_prop_set_uint32(DEVICE(&s->clint), "hartid-base", 0);
    qdev_prop_set_uint32(DEVICE(&s->clint), "num-harts",
                         TH1520_C910_HARTS);
    qdev_prop_set_uint32(DEVICE(&s->clint), "timebase-freq",
                         TH1520_TIMEBASE_FREQ);
    qdev_prop_set_uint32(DEVICE(&s->plic), "hartid-base", 0);
    qdev_prop_set_uint32(DEVICE(&s->plic), "num-harts",
                         TH1520_C910_HARTS);
    qdev_prop_set_uint32(DEVICE(&s->plic), "num-sources",
                         TH1520_PLIC_NUM_SOURCES);
    qdev_prop_set_uint32(DEVICE(&s->uart0), "baudbase",
                         TH1520_UART_INPUT_FREQ / 16);
}

static void th1520_soc_realize(DeviceState *dev, Error **errp)
{
    TH1520SoCState *s = RISCV_TH1520_SOC(dev);
    MemoryRegion *system_memory = get_system_memory();

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->c910_cpus), errp)) {
        return;
    }

    memory_region_init_ram(&s->sram, OBJECT(dev), "th1520.sram",
                           th1520_memmap[TH1520_DEV_SRAM].size, errp);
    if (*errp) {
        return;
    }
    memory_region_add_subregion(system_memory,
                                th1520_memmap[TH1520_DEV_SRAM].base,
                                &s->sram);

    memory_region_init_rom(&s->brom, OBJECT(dev), "th1520.brom",
                           th1520_memmap[TH1520_DEV_BROM].size, errp);
    if (*errp) {
        return;
    }
    memory_region_add_subregion(system_memory,
                                th1520_memmap[TH1520_DEV_BROM].base,
                                &s->brom);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->plic), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->plic), 0,
                    th1520_memmap[TH1520_DEV_PLIC].base);
    for (int i = 0; i < TH1520_C910_HARTS; i++) {
        DeviceState *cpu = DEVICE(qemu_get_cpu(i));

        qdev_connect_gpio_out_named(DEVICE(&s->plic), "mext", i,
                                    qdev_get_gpio_in(cpu, IRQ_M_EXT));
        qdev_connect_gpio_out_named(DEVICE(&s->plic), "sext", i,
                                    qdev_get_gpio_in(cpu, IRQ_S_EXT));
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->clint), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->clint), 0,
                    th1520_memmap[TH1520_DEV_CLINT].base);
    for (int i = 0; i < TH1520_C910_HARTS; i++) {
        DeviceState *cpu = DEVICE(qemu_get_cpu(i));

        qdev_connect_gpio_out_named(DEVICE(&s->clint), "msip", i,
                                    qdev_get_gpio_in(cpu, IRQ_M_SOFT));
        qdev_connect_gpio_out_named(DEVICE(&s->clint), "mtimer", i,
                                    qdev_get_gpio_in(cpu, IRQ_M_TIMER));
        qdev_connect_gpio_out_named(DEVICE(&s->clint), "ssip", i,
                                    qdev_get_gpio_in(cpu, IRQ_S_SOFT));
        qdev_connect_gpio_out_named(DEVICE(&s->clint), "stimer", i,
                                    qdev_get_gpio_in(cpu, IRQ_S_TIMER));
    }

    qdev_prop_set_chr(DEVICE(&s->uart0), "chardev", serial_hd(0));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart0), 0,
                    th1520_memmap[TH1520_DEV_UART0].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart0), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                              TH1520_UART0_IRQ));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dmac0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dmac0), 0,
                    th1520_memmap[TH1520_DEV_DMAC0].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dmac0), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                              TH1520_DMAC0_IRQ));

    for (int i = 0; i < TH1520_GMAC_COUNT; i++) {
        SysBusDevice *gmac = SYS_BUS_DEVICE(&s->gmac[i]);
        SysBusDevice *apb = SYS_BUS_DEVICE(&s->gmac_apb[i]);
        char alias[6];

        snprintf(alias, sizeof(alias), "gmac%d", i);
        qemu_configure_nic_device(DEVICE(gmac), true, alias);
        if (!sysbus_realize(gmac, errp)) {
            return;
        }
        sysbus_mmio_map(gmac, 0,
                        th1520_memmap[th1520_gmac_memmap[i]].base);
        sysbus_connect_irq(gmac, 0,
                           qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                                  th1520_gmac_irqs[i]));

        if (!sysbus_realize(apb, errp)) {
            return;
        }
        sysbus_mmio_map(apb, 0,
                        th1520_memmap[th1520_gmac_apb_memmap[i]].base);
    }

    for (int i = 0; i < TH1520_MSHC_COUNT; i++) {
        SysBusDevice *mshc = SYS_BUS_DEVICE(&s->mshc[i]);

        if (!sysbus_realize(mshc, errp)) {
            return;
        }
        sysbus_mmio_map(mshc, 0,
                        th1520_memmap[th1520_mshc_memmap[i]].base);
        sysbus_connect_irq(mshc, 0,
                           qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                                  th1520_mshc_irqs[i]));
    }
}

static void th1520_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = th1520_soc_realize;
}

static void beaglev_ahead_create_fdt(BeagleVAheadState *s)
{
    static const char *const board_compat[] = {
        "beagle,beaglev-ahead", "thead,th1520"
    };
    static const char *const cpu_compat[] = {
        "thead,c910", "riscv"
    };
    static const char *const plic_compat[] = {
        "thead,th1520-plic", "thead,c900-plic"
    };
    static const char *const clint_compat[] = {
        "thead,th1520-clint", "thead,c900-clint"
    };
    static const char *const uart_clock_names[] = {
        "baudclk", "apb_pclk"
    };
    static const char *const dmac_clock_names[] = {
        "core-clk", "cfgr-clk"
    };
    static const char *const gmac_compat[] = {
        "thead,th1520-gmac", "snps,dwmac-3.70a"
    };
    static const char *const gmac_reg_names[] = {
        "dwmac", "apb"
    };
    static const char *const gmac_clock_names[] = {
        "stmmaceth", "pclk", "apb"
    };
    MachineState *ms = MACHINE(s);
    uint32_t intc_phandles[TH1520_C910_HARTS];
    uint32_t plic_cells[TH1520_C910_HARTS * 4];
    uint32_t phandle = 1;
    uint32_t l2_phandle;
    uint32_t plic_phandle;
    uint32_t uart_clock_phandle;
    uint32_t dmac_clock_phandle;
    uint32_t mshc_clock_phandle;
    uint32_t gmac_axi_clock_phandle;
    uint32_t gmac_pclk_phandle;
    uint32_t gmac_apb_clock_phandle;
    uint32_t stmmac_axi_phandle;
    uint32_t phy_phandle;
    g_autofree char *plic_name = NULL;
    g_autofree char *clint_name = NULL;
    g_autofree char *uart_name = NULL;
    g_autofree char *dmac_name = NULL;
    int fdt_size;

    ms->fdt = create_board_device_tree("BeagleV Ahead",
                                        board_compat[0], &fdt_size);
    qemu_fdt_setprop_string_array(ms->fdt, "/", "compatible",
                                  (char **)&board_compat,
                                  ARRAY_SIZE(board_compat));
    qemu_fdt_setprop(ms->fdt, "/soc", "dma-noncoherent", NULL, 0);

    qemu_fdt_add_subnode(ms->fdt, "/aliases");
    qemu_fdt_add_subnode(ms->fdt, "/chosen");

    fdt_create_cpu_socket_subnode(ms->fdt, TH1520_TIMEBASE_FREQ);
    create_fdt_socket_cpus(ms->fdt, s->soc.c910_cpus.harts, 0,
                           TH1520_C910_HARTS, 0, &phandle,
                           intc_phandles, false, false);

    /*
     * All harts currently enter the direct-boot trampoline together.  Limit
     * OpenSBI's later cold-boot lottery to hart 0 so that its HSM path, and
     * consequently Linux's boot hart, are deterministic.  This node is
     * consumed and removed by OpenSBI.  It does not model the still-unknown
     * physical secondary-hart reset/release sequence (BOOT-001).
     */
    qemu_fdt_add_subnode(ms->fdt, "/chosen/opensbi-config");
    qemu_fdt_setprop_string(ms->fdt, "/chosen/opensbi-config", "compatible",
                            "opensbi,config");
    qemu_fdt_setprop_phandle(ms->fdt, "/chosen/opensbi-config",
                             "cold-boot-harts", "/cpus/cpu@0");

    create_fdt_socket_memory(ms->fdt,
                             th1520_memmap[TH1520_DEV_DRAM].base,
                             ms->ram_size, 0, false);
    th1520_create_pmu_fdt(ms->fdt);

    l2_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, "/cpus/l2-cache");
    qemu_fdt_setprop_string(ms->fdt, "/cpus/l2-cache", "compatible",
                            "cache");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus/l2-cache", "cache-block-size",
                          64);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus/l2-cache", "cache-level", 2);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus/l2-cache", "cache-size", MiB);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus/l2-cache", "cache-sets", 1024);
    qemu_fdt_setprop(ms->fdt, "/cpus/l2-cache", "cache-unified", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus/l2-cache", "phandle",
                          l2_phandle);

    for (int cpu = 0; cpu < TH1520_C910_HARTS; cpu++) {
        g_autofree char *cpu_name = g_strdup_printf("/cpus/cpu@%d", cpu);

        qemu_fdt_setprop_string_array(ms->fdt, cpu_name, "compatible",
                                      (char **)&cpu_compat,
                                      ARRAY_SIZE(cpu_compat));
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "thead,vlenb",
                              TH1520_C910_VLENB);
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "i-cache-block-size", 64);
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "i-cache-size", 64 * KiB);
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "i-cache-sets", 512);
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "d-cache-block-size", 64);
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "d-cache-size", 64 * KiB);
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "d-cache-sets", 512);
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "next-level-cache",
                              l2_phandle);

        plic_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
        plic_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
        plic_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
        plic_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
    }

    plic_phandle = phandle++;
    create_fdt_plic(ms->fdt, th1520_memmap[TH1520_DEV_PLIC].base,
                    th1520_memmap[TH1520_DEV_PLIC].size, plic_phandle,
                    2, 0, plic_cells, sizeof(plic_cells),
                    TH1520_PLIC_NDEV, false, 0);
    plic_name = g_strdup_printf("/soc/interrupt-controller@%" HWADDR_PRIx,
                                th1520_memmap[TH1520_DEV_PLIC].base);
    qemu_fdt_setprop_string_array(ms->fdt, plic_name, "compatible",
                                  (char **)&plic_compat,
                                  ARRAY_SIZE(plic_compat));
    qemu_fdt_setprop_cell(ms->fdt, "/soc", "interrupt-parent",
                          plic_phandle);

    create_fdt_socket_clint(ms->fdt,
                            th1520_memmap[TH1520_DEV_CLINT].base,
                            th1520_memmap[TH1520_DEV_CLINT].size, 0,
                            intc_phandles, TH1520_C910_HARTS, false);
    clint_name = g_strdup_printf("/soc/clint@%" HWADDR_PRIx,
                                 th1520_memmap[TH1520_DEV_CLINT].base);
    qemu_fdt_setprop_string_array(ms->fdt, clint_name, "compatible",
                                  (char **)&clint_compat,
                                  ARRAY_SIZE(clint_compat));

    uart_clock_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, "/uart-clock");
    qemu_fdt_setprop_string(ms->fdt, "/uart-clock", "compatible",
                            "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/uart-clock", "#clock-cells", 0);
    qemu_fdt_setprop_cell(ms->fdt, "/uart-clock", "clock-frequency",
                          TH1520_UART_INPUT_FREQ);
    qemu_fdt_setprop_string(ms->fdt, "/uart-clock", "clock-output-names",
                            "uart-sclk");
    qemu_fdt_setprop_cell(ms->fdt, "/uart-clock", "phandle",
                          uart_clock_phandle);

    uart_name = g_strdup_printf("/soc/serial@%" HWADDR_PRIx,
                                th1520_memmap[TH1520_DEV_UART0].base);
    qemu_fdt_add_subnode(ms->fdt, uart_name);
    qemu_fdt_setprop_string(ms->fdt, uart_name, "compatible",
                            "snps,dw-apb-uart");
    qemu_fdt_setprop_sized_cells(ms->fdt, uart_name, "reg", 2,
                                 th1520_memmap[TH1520_DEV_UART0].base, 2,
                                 th1520_memmap[TH1520_DEV_UART0].size);
    qemu_fdt_setprop_cells(ms->fdt, uart_name, "interrupts",
                           TH1520_UART0_IRQ, 4);
    qemu_fdt_setprop_cells(ms->fdt, uart_name, "clocks",
                           uart_clock_phandle, uart_clock_phandle);
    qemu_fdt_setprop_string_array(ms->fdt, uart_name, "clock-names",
                                  (char **)&uart_clock_names,
                                  ARRAY_SIZE(uart_clock_names));
    qemu_fdt_setprop_cell(ms->fdt, uart_name, "reg-shift", 2);
    qemu_fdt_setprop_cell(ms->fdt, uart_name, "reg-io-width", 4);
    qemu_fdt_setprop_string(ms->fdt, uart_name, "status", "okay");
    qemu_fdt_setprop_string(ms->fdt, "/aliases", "serial0", uart_name);
    qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path",
                            "serial0:115200n8");

    dmac_clock_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, "/dmac-clock");
    qemu_fdt_setprop_string(ms->fdt, "/dmac-clock", "compatible",
                            "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/dmac-clock", "#clock-cells", 0);
    qemu_fdt_setprop_cell(ms->fdt, "/dmac-clock", "clock-frequency",
                          TH1520_DMAC_INPUT_FREQ);
    qemu_fdt_setprop_string(ms->fdt, "/dmac-clock", "clock-output-names",
                            "perisys-apb-pclk");
    qemu_fdt_setprop_cell(ms->fdt, "/dmac-clock", "phandle",
                          dmac_clock_phandle);

    dmac_name = g_strdup_printf("/soc/dma-controller@%" HWADDR_PRIx,
                                th1520_memmap[TH1520_DEV_DMAC0].base);
    qemu_fdt_add_subnode(ms->fdt, dmac_name);
    qemu_fdt_setprop_string(ms->fdt, dmac_name, "compatible",
                            "snps,axi-dma-1.01a");
    qemu_fdt_setprop_sized_cells(ms->fdt, dmac_name, "reg", 2,
                                 th1520_memmap[TH1520_DEV_DMAC0].base, 2,
                                 th1520_memmap[TH1520_DEV_DMAC0].size);
    qemu_fdt_setprop_cells(ms->fdt, dmac_name, "interrupts",
                           TH1520_DMAC0_IRQ, 4);
    qemu_fdt_setprop_cells(ms->fdt, dmac_name, "clocks",
                           dmac_clock_phandle, dmac_clock_phandle);
    qemu_fdt_setprop_string_array(ms->fdt, dmac_name, "clock-names",
                                  (char **)&dmac_clock_names,
                                  ARRAY_SIZE(dmac_clock_names));
    qemu_fdt_setprop_cell(ms->fdt, dmac_name, "#dma-cells", 1);
    qemu_fdt_setprop_cell(ms->fdt, dmac_name, "dma-channels",
                          TH1520_DMAC_CHANNELS);
    qemu_fdt_setprop_cells(ms->fdt, dmac_name, "snps,block-size",
                           TH1520_DMAC_BLOCK_SIZE, TH1520_DMAC_BLOCK_SIZE,
                           TH1520_DMAC_BLOCK_SIZE, TH1520_DMAC_BLOCK_SIZE);
    qemu_fdt_setprop_cells(ms->fdt, dmac_name, "snps,priority", 0, 1, 2, 3);
    qemu_fdt_setprop_cell(ms->fdt, dmac_name, "snps,dma-masters", 1);
    qemu_fdt_setprop_cell(ms->fdt, dmac_name, "snps,data-width",
                          TH1520_DMAC_DATA_WIDTH);
    qemu_fdt_setprop_cell(ms->fdt, dmac_name, "snps,axi-max-burst-len", 16);
    qemu_fdt_setprop_string(ms->fdt, dmac_name, "status", "okay");

    mshc_clock_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, "/mshc-clock");
    qemu_fdt_setprop_string(ms->fdt, "/mshc-clock", "compatible",
                            "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/mshc-clock", "#clock-cells", 0);
    qemu_fdt_setprop_cell(ms->fdt, "/mshc-clock", "clock-frequency",
                          TH1520_MSHC_INPUT_FREQ);
    qemu_fdt_setprop_string(ms->fdt, "/mshc-clock", "clock-output-names",
                            "emmc-sdio");
    qemu_fdt_setprop_cell(ms->fdt, "/mshc-clock", "phandle",
                          mshc_clock_phandle);

    for (int i = 0; i < TH1520_MSHC_COUNT; i++) {
        const MemMapEntry *map = &th1520_memmap[th1520_mshc_memmap[i]];
        g_autofree char *name =
            g_strdup_printf("/soc/mmc@%" HWADDR_PRIx, map->base);

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible",
                                "thead,th1520-dwcmshc");
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg", 2, map->base,
                                     2, map->size);
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                               th1520_mshc_irqs[i], 4);
        qemu_fdt_setprop_cell(ms->fdt, name, "clocks",
                              mshc_clock_phandle);
        qemu_fdt_setprop_string(ms->fdt, name, "clock-names", "core");
        qemu_fdt_setprop_cell(ms->fdt, name, "max-frequency",
                              TH1520_MSHC_INPUT_FREQ);
        qemu_fdt_setprop_cell(ms->fdt, name, "bus-width", i ? 4 : 8);
        qemu_fdt_setprop_string(ms->fdt, name, "status", "okay");

        if (i == 0) {
            qemu_fdt_setprop(ms->fdt, name, "mmc-hs400-1_8v", NULL, 0);
            qemu_fdt_setprop(ms->fdt, name, "non-removable", NULL, 0);
            qemu_fdt_setprop(ms->fdt, name, "no-sd", NULL, 0);
            qemu_fdt_setprop(ms->fdt, name, "no-sdio", NULL, 0);
        } else if (i == 2) {
            /* The CYW43012 SDIO function is a later device milestone. */
            qemu_fdt_setprop(ms->fdt, name, "non-removable", NULL, 0);
            qemu_fdt_setprop(ms->fdt, name, "keep-power-in-suspend",
                             NULL, 0);
        }
    }

    /*
     * The TH1520 clock tree feeds GMAC's AXI interface at 500 MHz, its
     * peripheral clock directly from the 1 GHz GMAC PLL, and its APB glue at
     * 500 MHz.  Physical divider/phase behavior still needs board
     * measurements; these fixed clocks expose the software-visible rates
     * while the APB model retains all programmed digital state (GMAC-001).
     */
    gmac_axi_clock_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, "/gmac-axi-clock");
    qemu_fdt_setprop_string(ms->fdt, "/gmac-axi-clock", "compatible",
                            "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/gmac-axi-clock", "#clock-cells", 0);
    qemu_fdt_setprop_cell(ms->fdt, "/gmac-axi-clock", "clock-frequency",
                          TH1520_GMAC_AXI_FREQ);
    qemu_fdt_setprop_string(ms->fdt, "/gmac-axi-clock",
                            "clock-output-names", "gmac-axi");
    qemu_fdt_setprop_cell(ms->fdt, "/gmac-axi-clock", "phandle",
                          gmac_axi_clock_phandle);

    gmac_pclk_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, "/gmac-pclk");
    qemu_fdt_setprop_string(ms->fdt, "/gmac-pclk", "compatible",
                            "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/gmac-pclk", "#clock-cells", 0);
    qemu_fdt_setprop_cell(ms->fdt, "/gmac-pclk", "clock-frequency",
                          TH1520_GMAC_PCLK_FREQ);
    qemu_fdt_setprop_string(ms->fdt, "/gmac-pclk", "clock-output-names",
                            "gmac-pll");
    qemu_fdt_setprop_cell(ms->fdt, "/gmac-pclk", "phandle",
                          gmac_pclk_phandle);

    gmac_apb_clock_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, "/gmac-apb-clock");
    qemu_fdt_setprop_string(ms->fdt, "/gmac-apb-clock", "compatible",
                            "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/gmac-apb-clock", "#clock-cells", 0);
    qemu_fdt_setprop_cell(ms->fdt, "/gmac-apb-clock", "clock-frequency",
                          TH1520_GMAC_APB_FREQ);
    qemu_fdt_setprop_string(ms->fdt, "/gmac-apb-clock",
                            "clock-output-names", "gmac-apb");
    qemu_fdt_setprop_cell(ms->fdt, "/gmac-apb-clock", "phandle",
                          gmac_apb_clock_phandle);

    stmmac_axi_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, "/stmmac-axi-config");
    qemu_fdt_setprop_cell(ms->fdt, "/stmmac-axi-config", "phandle",
                          stmmac_axi_phandle);
    qemu_fdt_setprop_cell(ms->fdt, "/stmmac-axi-config", "snps,wr_osr_lmt",
                          15);
    qemu_fdt_setprop_cell(ms->fdt, "/stmmac-axi-config", "snps,rd_osr_lmt",
                          15);
    qemu_fdt_setprop_cells(ms->fdt, "/stmmac-axi-config", "snps,blen",
                           0, 0, 64, 32, 0, 0, 0);

    phy_phandle = phandle++;
    for (int i = 0; i < TH1520_GMAC_COUNT; i++) {
        const MemMapEntry *core = &th1520_memmap[th1520_gmac_memmap[i]];
        const MemMapEntry *apb = &th1520_memmap[th1520_gmac_apb_memmap[i]];
        g_autofree char *name =
            g_strdup_printf("/soc/ethernet@%" HWADDR_PRIx, core->base);
        g_autofree char *mdio = g_strdup_printf("%s/mdio", name);

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string_array(ms->fdt, name, "compatible",
                                      (char **)&gmac_compat,
                                      ARRAY_SIZE(gmac_compat));
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                     2, core->base, 2, core->size,
                                     2, apb->base, 2, apb->size);
        qemu_fdt_setprop_string_array(ms->fdt, name, "reg-names",
                                      (char **)&gmac_reg_names,
                                      ARRAY_SIZE(gmac_reg_names));
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                               th1520_gmac_irqs[i], 4);
        qemu_fdt_setprop_string(ms->fdt, name, "interrupt-names", "macirq");
        qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                               gmac_axi_clock_phandle, gmac_pclk_phandle,
                               gmac_apb_clock_phandle);
        qemu_fdt_setprop_string_array(ms->fdt, name, "clock-names",
                                      (char **)&gmac_clock_names,
                                      ARRAY_SIZE(gmac_clock_names));
        qemu_fdt_setprop_cell(ms->fdt, name, "snps,pbl", 32);
        qemu_fdt_setprop(ms->fdt, name, "snps,fixed-burst", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, name,
                              "snps,multicast-filter-bins", 64);
        qemu_fdt_setprop_cell(ms->fdt, name,
                              "snps,perfect-filter-entries", 32);
        qemu_fdt_setprop_cell(ms->fdt, name, "snps,axi-config",
                              stmmac_axi_phandle);
        qemu_fdt_setprop(ms->fdt, name, "local-mac-address",
                         s->soc.gmac[i].conf.macaddr.a, ETH_ALEN);

        qemu_fdt_add_subnode(ms->fdt, mdio);
        qemu_fdt_setprop_string(ms->fdt, mdio, "compatible",
                                "snps,dwmac-mdio");
        qemu_fdt_setprop_cell(ms->fdt, mdio, "#address-cells", 1);
        qemu_fdt_setprop_cell(ms->fdt, mdio, "#size-cells", 0);

        if (i == 0) {
            g_autofree char *phy =
                g_strdup_printf("%s/ethernet-phy@%u", mdio,
                                TH1520_GMAC_PHY_ADDR);

            qemu_fdt_add_subnode(ms->fdt, phy);
            qemu_fdt_setprop_cell(ms->fdt, phy, "reg",
                                  TH1520_GMAC_PHY_ADDR);
            qemu_fdt_setprop_cell(ms->fdt, phy, "phandle", phy_phandle);
            qemu_fdt_setprop_cell(ms->fdt, name, "phy-handle", phy_phandle);
            qemu_fdt_setprop_string(ms->fdt, name, "phy-mode", "rgmii-id");
            qemu_fdt_setprop_string(ms->fdt, name, "status", "okay");
            qemu_fdt_setprop_string(ms->fdt, "/aliases", "ethernet0", name);
        } else {
            /* GMAC1 has no Ethernet PHY routed on the BeagleV Ahead. */
            qemu_fdt_setprop_string(ms->fdt, name, "status", "disabled");
        }
    }
}

static void beaglev_ahead_attach_storage(BeagleVAheadState *s)
{
    for (int i = 0; i < 2; i++) {
        DriveInfo *dinfo = drive_get(IF_SD, 0, i);
        DeviceState *card;

        if (!dinfo) {
            continue;
        }

        card = qdev_new(i == 0 ? TYPE_EMMC : TYPE_SD_CARD);
        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card, s->soc.mshc[i].bus, &error_fatal);
    }
}

static void beaglev_ahead_machine_done(Notifier *notifier, void *data)
{
    BeagleVAheadState *s = container_of(notifier, BeagleVAheadState,
                                        machine_done);
    MachineState *ms = MACHINE(s);
    RISCVBootInfo boot_info;
    const char *firmware_name =
        riscv_default_firmware_name(&s->soc.c910_cpus);
    hwaddr start_addr = th1520_memmap[TH1520_DEV_DRAM].base;
    hwaddr firmware_end;
    vaddr kernel_start;
    uint64_t kernel_entry;
    uint64_t fdt_addr;

    riscv_boot_info_init(&boot_info, &s->soc.c910_cpus);
    firmware_end = riscv_find_and_load_firmware(ms, &boot_info,
                                                firmware_name,
                                                &start_addr, NULL);
    kernel_start = riscv_calc_kernel_start_addr(&boot_info, firmware_end);

    if (ms->kernel_filename) {
        riscv_load_kernel(ms, &boot_info, kernel_start, true, NULL);
        kernel_entry = boot_info.image_low_addr;
    } else {
        /* Give FW_DYNAMIC a valid next address even for a firmware-only run. */
        kernel_entry = kernel_start;
    }

    fdt_addr = riscv_compute_fdt_addr(th1520_memmap[TH1520_DEV_DRAM].base,
                                      ms->ram_size, ms, &boot_info);
    riscv_load_fdt(fdt_addr, ms->fdt);

    /*
     * Temporary direct-boot shim.  It intentionally does not claim to model
     * the mask-ROM strap/media selection sequence (ledger item BOOT-001).
     */
    riscv_setup_rom_reset_vec(ms, &s->soc.c910_cpus, start_addr,
                              th1520_memmap[TH1520_DEV_BROM].base,
                              th1520_memmap[TH1520_DEV_BROM].size,
                              kernel_entry, fdt_addr);
}

static void beaglev_ahead_machine_init(MachineState *ms)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    BeagleVAheadState *s = BEAGLEV_AHEAD_MACHINE(ms);

    if (ms->ram_size != mc->default_ram_size) {
        g_autofree char *size = size_to_str(mc->default_ram_size);

        error_report("BeagleV Ahead RAM size must be %s", size);
        exit(EXIT_FAILURE);
    }

    if (ms->smp.cpus != TH1520_C910_HARTS) {
        error_report("BeagleV Ahead has exactly %d C910 harts",
                     TH1520_C910_HARTS);
        exit(EXIT_FAILURE);
    }

    memory_region_add_subregion(get_system_memory(),
                                th1520_memmap[TH1520_DEV_DRAM].base,
                                ms->ram);

    object_initialize_child(OBJECT(ms), "soc", &s->soc,
                            TYPE_RISCV_TH1520_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    beaglev_ahead_attach_storage(s);

    beaglev_ahead_create_fdt(s);

    s->machine_done.notify = beaglev_ahead_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void beaglev_ahead_machine_class_init(ObjectClass *oc,
                                              const void *data)
{
    static const char *const valid_cpu_types[] = {
        TYPE_RISCV_CPU_THEAD_C910,
        NULL,
    };
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "BeagleV Ahead (T-Head TH1520)";
    mc->init = beaglev_ahead_machine_init;
    mc->min_cpus = TH1520_C910_HARTS;
    mc->max_cpus = TH1520_C910_HARTS;
    mc->default_cpus = TH1520_C910_HARTS;
    mc->default_cpu_type = TYPE_RISCV_CPU_THEAD_C910;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 4 * GiB;
    mc->default_ram_id = "beaglev-ahead.ram";
    mc->block_default_type = IF_SD;
    mc->no_cdrom = true;
    compat_props_add(mc->compat_props, beaglev_ahead_cpu_defaults,
                     G_N_ELEMENTS(beaglev_ahead_cpu_defaults));
}

static const TypeInfo beaglev_ahead_types[] = {
    {
        .name = TYPE_RISCV_TH1520_SOC,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(TH1520SoCState),
        .instance_init = th1520_soc_init,
        .class_init = th1520_soc_class_init,
    },
    {
        .name = TYPE_BEAGLEV_AHEAD_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(BeagleVAheadState),
        .class_init = beaglev_ahead_machine_class_init,
        .interfaces = riscv64_machine_interfaces,
    },
};

DEFINE_TYPES(beaglev_ahead_types)
