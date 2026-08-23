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
#include "system/device_tree.h"
#include "system/system.h"
#include "system/memory.h"
#include "target/riscv/cpu.h"
#include "target/riscv/cpu_bits.h"
#include "hw/char/serial-mm.h"
#include "hw/core/loader.h"
#include "hw/core/sysbus.h"
#include "hw/intc/thead_c900_clint.h"
#include "hw/intc/thead_c900_plic.h"
#include "hw/misc/unimp.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/fdt-common.h"
#include "hw/riscv/machines-qom.h"
#include "hw/riscv/th1520.h"

#include <libfdt.h>

static const MemMapEntry th1520_memmap[] = {
    [TH1520_DEV_DRAM]  = { 0x0000000000, 0x100000000 },
    [TH1520_DEV_PLIC]  = { 0xffd8000000, 0x01000000 },
    [TH1520_DEV_CLINT] = { 0xffdc000000, 0x00010000 },
    [TH1520_DEV_SRAM]  = { 0xffe0000000, 0x00180000 },
    [TH1520_DEV_UART0] = { 0xffe7014000, 0x00000100 },
    [TH1520_DEV_BROM]  = { 0xffffd00000, 0x00100000 },
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
    TH1520SoCState *s = RISCV_TH1520_SOC(obj);

    object_initialize_child(obj, "c910-cpus", &s->c910_cpus,
                            TYPE_RISCV_HART_ARRAY);
    object_initialize_child(obj, "clint", &s->clint,
                            TYPE_THEAD_C900_CLINT);
    object_initialize_child(obj, "plic", &s->plic,
                            TYPE_THEAD_C900_PLIC);
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

    /*
     * TH1520 uses a DesignWare APB UART.  serial-mm implements its 16550
     * register subset; the low-priority region covers DW-specific registers
     * until a complete DW APB UART model is available.
     */
    create_unimplemented_device("th1520.uart0",
                                th1520_memmap[TH1520_DEV_UART0].base,
                                th1520_memmap[TH1520_DEV_UART0].size);
    serial_mm_init(system_memory, th1520_memmap[TH1520_DEV_UART0].base, 2,
                   qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                          TH1520_UART0_IRQ),
                   TH1520_UART_INPUT_FREQ / 16, serial_hd(0),
                   DEVICE_LITTLE_ENDIAN);
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
    MachineState *ms = MACHINE(s);
    uint32_t intc_phandles[TH1520_C910_HARTS];
    uint32_t plic_cells[TH1520_C910_HARTS * 4];
    uint32_t phandle = 1;
    uint32_t l2_phandle;
    uint32_t plic_phandle;
    uint32_t uart_clock_phandle;
    g_autofree char *plic_name = NULL;
    g_autofree char *clint_name = NULL;
    g_autofree char *uart_name = NULL;
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
