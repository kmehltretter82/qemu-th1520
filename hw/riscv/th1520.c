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
#include "hw/core/qdev-clock.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/intc/thead_c900_clint.h"
#include "hw/intc/thead_c900_plic.h"
#include "hw/net/dw_gmac.h"
#include "hw/net/th1520_gmac.h"
#include "hw/nvram/eeprom_at24c.h"
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
    [TH1520_DEV_AP_CLOCK] = { 0xffef010000, 0x00001000 },
    [TH1520_DEV_AP_RESET] = { 0xffef014000, 0x00001000 },
    [TH1520_DEV_UART0] = { 0xffe7014000, 0x00000100 },
    [TH1520_DEV_UART1] = { 0xffe7f00000, 0x00000100 },
    [TH1520_DEV_UART2] = { 0xffec010000, 0x00004000 },
    [TH1520_DEV_UART3] = { 0xffe7f04000, 0x00000100 },
    [TH1520_DEV_UART4] = { 0xfff7f08000, 0x00004000 },
    [TH1520_DEV_UART5] = { 0xfff7f0c000, 0x00004000 },
    [TH1520_DEV_GPIO0] = { 0xffec005000, 0x00001000 },
    [TH1520_DEV_GPIO1] = { 0xffec006000, 0x00001000 },
    [TH1520_DEV_GPIO2] = { 0xffe7f34000, 0x00001000 },
    [TH1520_DEV_GPIO3] = { 0xffe7f38000, 0x00001000 },
    [TH1520_DEV_GPIO4] = { 0xfffff52000, 0x00001000 },
    [TH1520_DEV_AOGPIO] = { 0xfffff41000, 0x00001000 },
    [TH1520_DEV_PADCTRL_AOSYS] = { 0xfffff4a000, 0x00002000 },
    [TH1520_DEV_PADCTRL1_APSYS] = { 0xffe7f3c000, 0x00001000 },
    [TH1520_DEV_PADCTRL0_APSYS] = { 0xffec007000, 0x00001000 },
    [TH1520_DEV_I2C0]  = { 0xffe7f20000, 0x00004000 },
    [TH1520_DEV_I2C1]  = { 0xffe7f24000, 0x00004000 },
    [TH1520_DEV_I2C2]  = { 0xffec00c000, 0x00004000 },
    [TH1520_DEV_I2C3]  = { 0xffec014000, 0x00004000 },
    [TH1520_DEV_I2C4]  = { 0xffe7f28000, 0x00004000 },
    [TH1520_DEV_I2C5]  = { 0xfff7f2c000, 0x00004000 },
    [TH1520_DEV_SPI0]  = { 0xffe700c000, 0x00001000 },
    [TH1520_DEV_PWM]   = { 0xffec01c000, 0x00004000 },
    [TH1520_DEV_TIMER0_3] = { 0xffefc32000, 0x00001000 },
    [TH1520_DEV_TIMER4_7] = { 0xffffc33000, 0x00001000 },
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

static const int th1520_uart_memmap[TH1520_UART_COUNT] = {
    TH1520_DEV_UART0,
    TH1520_DEV_UART1,
    TH1520_DEV_UART2,
    TH1520_DEV_UART3,
    TH1520_DEV_UART4,
    TH1520_DEV_UART5,
};

static const uint32_t th1520_uart_irqs[TH1520_UART_COUNT] = {
    TH1520_UART0_IRQ,
    TH1520_UART1_IRQ,
    TH1520_UART2_IRQ,
    TH1520_UART3_IRQ,
    TH1520_UART4_IRQ,
    TH1520_UART5_IRQ,
};

static const uint32_t th1520_uart_pclk_ids[TH1520_UART_COUNT] = {
    TH1520_CLK_UART0_PCLK,
    TH1520_CLK_UART1_PCLK,
    TH1520_CLK_UART2_PCLK,
    TH1520_CLK_UART3_PCLK,
    TH1520_CLK_UART4_PCLK,
    TH1520_CLK_UART5_PCLK,
};

typedef struct TH1520GPIOInfo {
    const char *name;
    int memmap;
    uint32_t irq;
    uint8_t ngpios;
    int16_t clock_id;
    uint8_t pad_group;
    uint8_t nranges;
    struct {
        uint8_t gpio_offset;
        uint8_t pin_offset;
        uint8_t count;
    } ranges[2];
} TH1520GPIOInfo;

static const TH1520GPIOInfo th1520_gpio_info[TH1520_GPIO_COUNT] = {
    { "gpio0",  TH1520_DEV_GPIO0,  TH1520_GPIO0_IRQ,  32,
      TH1520_CLK_GPIO0, 2, 1, { { 0, 0, 32 } } },
    { "gpio1",  TH1520_DEV_GPIO1,  TH1520_GPIO1_IRQ,  31,
      TH1520_CLK_GPIO1, 2, 1, { { 0, 32, 31 } } },
    { "gpio2",  TH1520_DEV_GPIO2,  TH1520_GPIO2_IRQ,  32,
      TH1520_CLK_GPIO2, 3, 1, { { 0, 0, 32 } } },
    { "gpio3",  TH1520_DEV_GPIO3,  TH1520_GPIO3_IRQ,  23,
      TH1520_CLK_GPIO3, 3, 1, { { 0, 32, 23 } } },
    { "gpio4",  TH1520_DEV_GPIO4,  TH1520_GPIO4_IRQ,  23, -1,
      1, 2, { { 0, 25, 22 }, { 22, 7, 1 } } },
    { "aogpio", TH1520_DEV_AOGPIO, TH1520_AOGPIO_IRQ, 16, -1,
      1, 1, { { 0, 9, 16 } } },
};

typedef struct TH1520PadCtrlInfo {
    const char *name;
    int memmap;
    uint8_t group;
    int16_t clock_id;
} TH1520PadCtrlInfo;

static const TH1520PadCtrlInfo
th1520_padctrl_info[TH1520_PADCTRL_COUNT] = {
    { "padctrl-aosys",  TH1520_DEV_PADCTRL_AOSYS,  1, -1 },
    { "padctrl1-apsys", TH1520_DEV_PADCTRL1_APSYS, 2,
      TH1520_CLK_PADCTRL1 },
    { "padctrl0-apsys", TH1520_DEV_PADCTRL0_APSYS, 3,
      TH1520_CLK_PADCTRL0 },
};

typedef struct TH1520I2CInfo {
    const char *name;
    int memmap;
    uint32_t irq;
    uint32_t clock_id;
} TH1520I2CInfo;

static const TH1520I2CInfo th1520_i2c_info[TH1520_I2C_COUNT] = {
    { "i2c0", TH1520_DEV_I2C0, TH1520_I2C0_IRQ, TH1520_CLK_I2C0 },
    { "i2c1", TH1520_DEV_I2C1, TH1520_I2C1_IRQ, TH1520_CLK_I2C1 },
    { "i2c2", TH1520_DEV_I2C2, TH1520_I2C2_IRQ, TH1520_CLK_I2C2 },
    { "i2c3", TH1520_DEV_I2C3, TH1520_I2C3_IRQ, TH1520_CLK_I2C3 },
    { "i2c4", TH1520_DEV_I2C4, TH1520_I2C4_IRQ, TH1520_CLK_I2C4 },
    { "i2c5", TH1520_DEV_I2C5, TH1520_I2C5_IRQ, TH1520_CLK_I2C5 },
};

typedef struct TH1520TimerInfo {
    const char *name;
    int memmap;
    uint32_t first_irq;
} TH1520TimerInfo;

typedef struct TH1520SPIInfo {
    const char *name;
    int memmap;
    uint32_t irq;
    uint32_t clock_id;
} TH1520SPIInfo;

static const TH1520TimerInfo
th1520_timer_info[TH1520_TIMER_GROUP_COUNT] = {
    { "timer0-3", TH1520_DEV_TIMER0_3, TH1520_TIMER0_IRQ },
    { "timer4-7", TH1520_DEV_TIMER4_7, TH1520_TIMER4_IRQ },
};

static const TH1520SPIInfo th1520_spi_info[TH1520_SPI_COUNT] = {
    { "spi0", TH1520_DEV_SPI0, TH1520_SPI0_IRQ, TH1520_CLK_SPI },
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
    static const char *const uart_names[TH1520_UART_COUNT] = {
        "uart0", "uart1", "uart2", "uart3", "uart4", "uart5",
    };
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
    object_initialize_child(obj, "ap-clock", &s->ap_clock,
                            TYPE_TH1520_AP_CLOCK);
    object_initialize_child(obj, "ap-reset", &s->ap_reset,
                            TYPE_TH1520_AP_RESET);
    for (int i = 0; i < TH1520_UART_COUNT; i++) {
        object_initialize_child(obj, uart_names[i], &s->uart[i],
                                TYPE_DW_APB_UART);
        qdev_prop_set_uint32(DEVICE(&s->uart[i]), "baudbase",
                             TH1520_UART_INPUT_FREQ / 16);
    }
    for (int i = 0; i < TH1520_GPIO_COUNT; i++) {
        object_initialize_child(obj, th1520_gpio_info[i].name, &s->gpio[i],
                                TYPE_DW_APB_GPIO);
        qdev_prop_set_uint8(DEVICE(&s->gpio[i]), "ngpios",
                            th1520_gpio_info[i].ngpios);
    }
    for (int i = 0; i < TH1520_PADCTRL_COUNT; i++) {
        object_initialize_child(obj, th1520_padctrl_info[i].name,
                                &s->padctrl[i], TYPE_TH1520_PADCTRL);
        qdev_prop_set_uint8(DEVICE(&s->padctrl[i]), "pad-group",
                            th1520_padctrl_info[i].group);
    }
    for (int i = 0; i < TH1520_I2C_COUNT; i++) {
        DeviceState *i2c;

        object_initialize_child(obj, th1520_i2c_info[i].name, &s->i2c[i],
                                TYPE_DESIGNWARE_I2C);
        i2c = DEVICE(&s->i2c[i]);
        qdev_prop_set_uint32(i2c, "component-parameters",
                             TH1520_I2C_COMPONENT_PARAMETERS);
        qdev_prop_set_uint32(i2c, "component-version",
                             TH1520_I2C_COMPONENT_VERSION);
        qdev_prop_set_uint32(i2c, "component-type",
                             TH1520_I2C_COMPONENT_TYPE);
        qdev_prop_set_uint32(i2c, "intr-mask-reset",
                             TH1520_I2C_INTR_MASK_RESET);
        qdev_prop_set_uint32(i2c, "intr-mask-valid",
                             TH1520_I2C_INTR_MASK_VALID);
        qdev_prop_set_uint32(i2c, "fs-spklen-reset", 1);
        qdev_prop_set_uint32(i2c, "hs-spklen-reset", 1);
        qdev_prop_set_uint32(i2c, "scl-stuck-timeout-reset", UINT32_MAX);
        qdev_prop_set_uint32(i2c, "sda-stuck-timeout-reset", UINT32_MAX);
        qdev_prop_set_uint32(i2c, "ack-general-call-reset", 1);
    }
    for (int i = 0; i < TH1520_SPI_COUNT; i++) {
        object_initialize_child(obj, th1520_spi_info[i].name, &s->spi[i],
                                TYPE_DW_APB_SSI);
    }
    object_initialize_child(obj, "pwm", &s->pwm, TYPE_TH1520_PWM);
    s->pwm_clk = clock_new(obj, "pwm-clock");
    clock_set_hz(s->pwm_clk, TH1520_PWM_INPUT_FREQ);
    qdev_connect_clock_in(DEVICE(&s->pwm), "pwm", s->pwm_clk);
    s->timer_clk = clock_new(obj, "timer-clock");
    clock_set_hz(s->timer_clk, TH1520_TIMER_INPUT_FREQ);
    for (int i = 0; i < TH1520_TIMER_GROUP_COUNT; i++) {
        object_initialize_child(obj, th1520_timer_info[i].name,
                                &s->timer[i], TYPE_DW_APB_TIMER);
        qdev_prop_set_uint32(DEVICE(&s->timer[i]), "component-version",
                             TH1520_TIMER_COMPONENT_VERSION);
        qdev_connect_clock_in(DEVICE(&s->timer[i]), "timer", s->timer_clk);
    }
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

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ap_clock), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ap_clock), 0,
                    th1520_memmap[TH1520_DEV_AP_CLOCK].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ap_reset), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ap_reset), 0,
                    th1520_memmap[TH1520_DEV_AP_RESET].base);

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

    for (int i = 0; i < TH1520_UART_COUNT; i++) {
        SysBusDevice *uart = SYS_BUS_DEVICE(&s->uart[i]);

        /*
         * The DW register model occupies 0x100 bytes.  Linux describes a
         * 0x4000-byte silicon aperture for UART2, UART4 and UART5, but the
         * behavior of the reserved portion is not established (UART-001).
         * Leave it unmapped until it can be checked on physical hardware.
         */
        qdev_prop_set_chr(DEVICE(uart), "chardev", serial_hd(i));
        if (!sysbus_realize(uart, errp)) {
            return;
        }
        sysbus_mmio_map(uart, 0,
                        th1520_memmap[th1520_uart_memmap[i]].base);
        sysbus_connect_irq(uart, 0,
                           qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                                  th1520_uart_irqs[i]));
    }

    for (int i = 0; i < TH1520_GPIO_COUNT; i++) {
        const TH1520GPIOInfo *info = &th1520_gpio_info[i];
        SysBusDevice *gpio = SYS_BUS_DEVICE(&s->gpio[i]);

        if (!sysbus_realize(gpio, errp)) {
            return;
        }
        sysbus_mmio_map(gpio, 0, th1520_memmap[info->memmap].base);
        sysbus_connect_irq(gpio, 0,
                           qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                                  info->irq));
    }

    for (int i = 0; i < TH1520_PADCTRL_COUNT; i++) {
        const TH1520PadCtrlInfo *info = &th1520_padctrl_info[i];
        SysBusDevice *padctrl = SYS_BUS_DEVICE(&s->padctrl[i]);

        if (!sysbus_realize(padctrl, errp)) {
            return;
        }
        sysbus_mmio_map(padctrl, 0, th1520_memmap[info->memmap].base);
    }

    for (int i = 0; i < TH1520_I2C_COUNT; i++) {
        const TH1520I2CInfo *info = &th1520_i2c_info[i];
        SysBusDevice *i2c = SYS_BUS_DEVICE(&s->i2c[i]);

        if (!sysbus_realize(i2c, errp)) {
            return;
        }
        sysbus_mmio_map(i2c, 0, th1520_memmap[info->memmap].base);
        sysbus_connect_irq(i2c, 0,
                           qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                                  info->irq));
    }

    for (int i = 0; i < TH1520_SPI_COUNT; i++) {
        const TH1520SPIInfo *info = &th1520_spi_info[i];
        SysBusDevice *spi = SYS_BUS_DEVICE(&s->spi[i]);

        if (!sysbus_realize(spi, errp)) {
            return;
        }
        sysbus_mmio_map(spi, 0, th1520_memmap[info->memmap].base);
        sysbus_connect_irq(spi, 0,
                           qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                                  info->irq));
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pwm), errp)) {
        return;
    }
    /* The DTS aperture is 0x4000 bytes; the Linux driver uses only 0xb0. */
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pwm), 0,
                    th1520_memmap[TH1520_DEV_PWM].base);

    for (int i = 0; i < TH1520_TIMER_GROUP_COUNT; i++) {
        const TH1520TimerInfo *info = &th1520_timer_info[i];
        SysBusDevice *timer = SYS_BUS_DEVICE(&s->timer[i]);

        if (!sysbus_realize(timer, errp)) {
            return;
        }
        sysbus_mmio_map(timer, 0, th1520_memmap[info->memmap].base);
        for (int channel = 0; channel < DW_APB_TIMER_CHANNELS; channel++) {
            sysbus_connect_irq(
                timer, channel,
                qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                       info->first_irq + channel));
        }
    }

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

enum {
    TH1520_PIN_BIAS_DISABLE    = BIT(0),
    TH1520_PIN_BIAS_PULL_UP    = BIT(1),
    TH1520_PIN_INPUT_ENABLE    = BIT(2),
    TH1520_PIN_INPUT_DISABLE   = BIT(3),
    TH1520_PIN_SCHMITT_ENABLE  = BIT(4),
    TH1520_PIN_SCHMITT_DISABLE = BIT(5),
};

typedef struct TH1520PinConfigFDT {
    const char *name;
    const char *const *pins;
    size_t npins;
    const char *function;
    uint32_t drive_strength;
    uint32_t flags;
} TH1520PinConfigFDT;

static const char *const th1520_led_pins[] = {
    "AUDIO_PA8", "AUDIO_PA9", "AUDIO_PA10", "AUDIO_PA11", "AUDIO_PA12",
};

static const TH1520PinConfigFDT th1520_led_configs[] = {
    {
        .name = "led-pins",
        .pins = th1520_led_pins,
        .npins = ARRAY_SIZE(th1520_led_pins),
        .drive_strength = 3,
        .flags = TH1520_PIN_BIAS_DISABLE | TH1520_PIN_INPUT_DISABLE |
                 TH1520_PIN_SCHMITT_DISABLE,
    },
};

static const char *const th1520_gmac0_tx_pins[] = {
    "GMAC0_TX_CLK", "GMAC0_TXEN", "GMAC0_TXD0", "GMAC0_TXD1",
    "GMAC0_TXD2", "GMAC0_TXD3",
};

static const char *const th1520_gmac0_rx_pins[] = {
    "GMAC0_RX_CLK", "GMAC0_RXDV", "GMAC0_RXD0", "GMAC0_RXD1",
    "GMAC0_RXD2", "GMAC0_RXD3",
};

static const char *const th1520_gmac0_mdc_pins[] = { "GMAC0_MDC" };
static const char *const th1520_gmac0_mdio_pins[] = { "GMAC0_MDIO" };
static const char *const th1520_gmac0_reset_pins[] = { "GMAC0_COL" };
static const char *const th1520_gmac0_irq_pins[] = { "GMAC0_CRS" };

static const TH1520PinConfigFDT th1520_gmac0_configs[] = {
    {
        .name = "tx-pins",
        .pins = th1520_gmac0_tx_pins,
        .npins = ARRAY_SIZE(th1520_gmac0_tx_pins),
        .function = "gmac0",
        .drive_strength = 25,
        .flags = TH1520_PIN_BIAS_DISABLE | TH1520_PIN_INPUT_DISABLE |
                 TH1520_PIN_SCHMITT_DISABLE,
    }, {
        .name = "rx-pins",
        .pins = th1520_gmac0_rx_pins,
        .npins = ARRAY_SIZE(th1520_gmac0_rx_pins),
        .function = "gmac0",
        .drive_strength = 1,
        .flags = TH1520_PIN_BIAS_DISABLE | TH1520_PIN_INPUT_ENABLE |
                 TH1520_PIN_SCHMITT_DISABLE,
    }, {
        .name = "mdc-pins",
        .pins = th1520_gmac0_mdc_pins,
        .npins = ARRAY_SIZE(th1520_gmac0_mdc_pins),
        .function = "gmac0",
        .drive_strength = 13,
        .flags = TH1520_PIN_BIAS_DISABLE | TH1520_PIN_INPUT_DISABLE |
                 TH1520_PIN_SCHMITT_DISABLE,
    }, {
        .name = "mdio-pins",
        .pins = th1520_gmac0_mdio_pins,
        .npins = ARRAY_SIZE(th1520_gmac0_mdio_pins),
        .function = "gmac0",
        .drive_strength = 13,
        .flags = TH1520_PIN_BIAS_DISABLE | TH1520_PIN_INPUT_ENABLE |
                 TH1520_PIN_SCHMITT_ENABLE,
    }, {
        .name = "phy-reset-pins",
        .pins = th1520_gmac0_reset_pins,
        .npins = ARRAY_SIZE(th1520_gmac0_reset_pins),
        .drive_strength = 3,
        .flags = TH1520_PIN_BIAS_DISABLE | TH1520_PIN_INPUT_DISABLE |
                 TH1520_PIN_SCHMITT_DISABLE,
    }, {
        .name = "phy-interrupt-pins",
        .pins = th1520_gmac0_irq_pins,
        .npins = ARRAY_SIZE(th1520_gmac0_irq_pins),
        .function = "gpio",
        .drive_strength = 1,
        .flags = TH1520_PIN_BIAS_PULL_UP | TH1520_PIN_INPUT_ENABLE |
                 TH1520_PIN_SCHMITT_ENABLE,
    },
};

static const char *const th1520_uart0_tx_pins[] = { "UART0_TXD" };
static const char *const th1520_uart0_rx_pins[] = { "UART0_RXD" };

static const TH1520PinConfigFDT th1520_uart0_configs[] = {
    {
        .name = "tx-pins",
        .pins = th1520_uart0_tx_pins,
        .npins = ARRAY_SIZE(th1520_uart0_tx_pins),
        .function = "uart",
        .drive_strength = 3,
        .flags = TH1520_PIN_BIAS_DISABLE | TH1520_PIN_INPUT_DISABLE |
                 TH1520_PIN_SCHMITT_DISABLE,
    }, {
        .name = "rx-pins",
        .pins = th1520_uart0_rx_pins,
        .npins = ARRAY_SIZE(th1520_uart0_rx_pins),
        .function = "uart",
        .drive_strength = 1,
        .flags = TH1520_PIN_BIAS_PULL_UP | TH1520_PIN_INPUT_ENABLE |
                 TH1520_PIN_SCHMITT_ENABLE,
    },
};

static const char *const th1520_wifi_wake_pins[] = { "GPIO2_25" };
static const char *const th1520_wifi_reg_on_pins[] = { "GPIO2_31" };

static const TH1520PinConfigFDT th1520_wifi_configs[] = {
    {
        .name = "host-wake-pins",
        .pins = th1520_wifi_wake_pins,
        .npins = ARRAY_SIZE(th1520_wifi_wake_pins),
        .function = "gpio",
        .drive_strength = 1,
        .flags = TH1520_PIN_BIAS_DISABLE | TH1520_PIN_INPUT_ENABLE |
                 TH1520_PIN_SCHMITT_DISABLE,
    }, {
        .name = "reg-on-pins",
        .pins = th1520_wifi_reg_on_pins,
        .npins = ARRAY_SIZE(th1520_wifi_reg_on_pins),
        .function = "gpio",
        .drive_strength = 3,
        .flags = TH1520_PIN_BIAS_DISABLE | TH1520_PIN_INPUT_DISABLE |
                 TH1520_PIN_SCHMITT_DISABLE,
    },
};

static uint32_t th1520_create_pin_group_fdt(
    void *fdt, const char *controller, const char *name,
    const TH1520PinConfigFDT *configs, size_t nconfigs, uint32_t *phandle)
{
    g_autofree char *group = g_strdup_printf("%s/%s", controller, name);
    uint32_t group_phandle = (*phandle)++;

    qemu_fdt_add_subnode(fdt, group);
    qemu_fdt_setprop_cell(fdt, group, "phandle", group_phandle);
    for (size_t i = 0; i < nconfigs; i++) {
        const TH1520PinConfigFDT *config = &configs[i];
        g_autofree char *node = g_strdup_printf("%s/%s", group,
                                                config->name);

        qemu_fdt_add_subnode(fdt, node);
        qemu_fdt_setprop_string_array(fdt, node, "pins",
                                      (char **)config->pins,
                                      config->npins);
        if (config->function) {
            qemu_fdt_setprop_string(fdt, node, "function",
                                    config->function);
        }
        if (config->flags & TH1520_PIN_BIAS_DISABLE) {
            qemu_fdt_setprop(fdt, node, "bias-disable", NULL, 0);
        }
        if (config->flags & TH1520_PIN_BIAS_PULL_UP) {
            qemu_fdt_setprop(fdt, node, "bias-pull-up", NULL, 0);
        }
        if (config->flags & TH1520_PIN_INPUT_ENABLE) {
            qemu_fdt_setprop(fdt, node, "input-enable", NULL, 0);
        }
        if (config->flags & TH1520_PIN_INPUT_DISABLE) {
            qemu_fdt_setprop(fdt, node, "input-disable", NULL, 0);
        }
        if (config->flags & TH1520_PIN_SCHMITT_ENABLE) {
            qemu_fdt_setprop(fdt, node, "input-schmitt-enable", NULL, 0);
        }
        if (config->flags & TH1520_PIN_SCHMITT_DISABLE) {
            qemu_fdt_setprop(fdt, node, "input-schmitt-disable", NULL, 0);
        }
        qemu_fdt_setprop_cell(fdt, node, "drive-strength",
                              config->drive_strength);
        qemu_fdt_setprop_cell(fdt, node, "slew-rate", 0);
    }

    return group_phandle;
}

static void th1520_create_pinctrl_fdt(
    void *fdt, uint32_t ap_clock_phandle, uint32_t aonsys_clock_phandle,
    uint32_t *phandle, uint32_t padctrl_phandles[TH1520_PADCTRL_COUNT],
    uint32_t *led_phandle, uint32_t *gmac0_phandle,
    uint32_t *uart0_phandle, uint32_t *wifi_phandle)
{
    for (int i = 0; i < TH1520_PADCTRL_COUNT; i++) {
        const TH1520PadCtrlInfo *info = &th1520_padctrl_info[i];
        const MemMapEntry *map = &th1520_memmap[info->memmap];
        g_autofree char *name =
            g_strdup_printf("/soc/pinctrl@%" HWADDR_PRIx, map->base);

        padctrl_phandles[i] = (*phandle)++;
        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible",
                                "thead,th1520-pinctrl");
        qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, map->base,
                                     2, map->size);
        if (info->clock_id < 0) {
            qemu_fdt_setprop_cell(fdt, name, "clocks",
                                  aonsys_clock_phandle);
        } else {
            qemu_fdt_setprop_cells(fdt, name, "clocks",
                                   ap_clock_phandle, info->clock_id);
        }
        qemu_fdt_setprop_cell(fdt, name, "thead,pad-group", info->group);
        qemu_fdt_setprop_cell(fdt, name, "phandle",
                              padctrl_phandles[i]);
    }

    *led_phandle = th1520_create_pin_group_fdt(
        fdt, "/soc/pinctrl@fffff4a000", "led-0", th1520_led_configs,
        ARRAY_SIZE(th1520_led_configs), phandle);
    *gmac0_phandle = th1520_create_pin_group_fdt(
        fdt, "/soc/pinctrl@ffec007000", "gmac0-0", th1520_gmac0_configs,
        ARRAY_SIZE(th1520_gmac0_configs), phandle);
    *uart0_phandle = th1520_create_pin_group_fdt(
        fdt, "/soc/pinctrl@ffec007000", "uart0-0", th1520_uart0_configs,
        ARRAY_SIZE(th1520_uart0_configs), phandle);
    *wifi_phandle = th1520_create_pin_group_fdt(
        fdt, "/soc/pinctrl@ffec007000", "wifi-0", th1520_wifi_configs,
        ARRAY_SIZE(th1520_wifi_configs), phandle);
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
    static const char *const i2c_compat[] = {
        "thead,th1520-i2c", "snps,designware-i2c"
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
    static const char *const spi_compat[] = {
        "thead,th1520-spi", "snps,dw-apb-ssi"
    };
    MachineState *ms = MACHINE(s);
    uint32_t intc_phandles[TH1520_C910_HARTS];
    uint32_t plic_cells[TH1520_C910_HARTS * 4];
    uint32_t phandle = 1;
    uint32_t l2_phandle;
    uint32_t plic_phandle;
    uint32_t osc_phandle;
    uint32_t aonsys_clock_phandle;
    uint32_t ap_clock_phandle;
    uint32_t ap_reset_phandle;
    uint32_t padctrl_phandles[TH1520_PADCTRL_COUNT];
    uint32_t led_pins_phandle;
    uint32_t gmac0_pins_phandle;
    uint32_t uart0_pins_phandle;
    uint32_t wifi_pins_phandle;
    uint32_t gpio_phandles[TH1520_GPIO_COUNT];
    uint32_t stmmac_axi_phandle;
    uint32_t phy_phandle;
    g_autofree char *plic_name = NULL;
    g_autofree char *clint_name = NULL;
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

    osc_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, "/oscillator");
    qemu_fdt_setprop_string(ms->fdt, "/oscillator", "compatible",
                            "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/oscillator", "#clock-cells", 0);
    qemu_fdt_setprop_cell(ms->fdt, "/oscillator", "clock-frequency",
                          TH1520_OSC_FREQ);
    qemu_fdt_setprop_string(ms->fdt, "/oscillator", "clock-output-names",
                            "osc_24m");
    qemu_fdt_setprop_cell(ms->fdt, "/oscillator", "phandle", osc_phandle);

    aonsys_clock_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, "/clock-73728000");
    qemu_fdt_setprop_string(ms->fdt, "/clock-73728000", "compatible",
                            "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/clock-73728000", "#clock-cells", 0);
    qemu_fdt_setprop_cell(ms->fdt, "/clock-73728000", "clock-frequency",
                          73728000);
    qemu_fdt_setprop_string(ms->fdt, "/clock-73728000",
                            "clock-output-names", "aonsys_clk");
    qemu_fdt_setprop_cell(ms->fdt, "/clock-73728000", "phandle",
                          aonsys_clock_phandle);

    ap_clock_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt,
                         "/soc/clock-controller@ffef010000");
    qemu_fdt_setprop_string(ms->fdt,
                            "/soc/clock-controller@ffef010000", "compatible",
                            "thead,th1520-clk-ap");
    qemu_fdt_setprop_sized_cells(ms->fdt,
                                 "/soc/clock-controller@ffef010000", "reg",
                                 2, th1520_memmap[TH1520_DEV_AP_CLOCK].base,
                                 2, th1520_memmap[TH1520_DEV_AP_CLOCK].size);
    qemu_fdt_setprop_cell(ms->fdt,
                          "/soc/clock-controller@ffef010000", "clocks",
                          osc_phandle);
    qemu_fdt_setprop_cell(ms->fdt,
                          "/soc/clock-controller@ffef010000", "#clock-cells",
                          1);
    qemu_fdt_setprop_cell(ms->fdt,
                          "/soc/clock-controller@ffef010000", "phandle",
                          ap_clock_phandle);

    ap_reset_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt,
                         "/soc/reset-controller@ffef014000");
    qemu_fdt_setprop_string(ms->fdt,
                            "/soc/reset-controller@ffef014000", "compatible",
                            "thead,th1520-reset-ap");
    qemu_fdt_setprop_sized_cells(ms->fdt,
                                 "/soc/reset-controller@ffef014000", "reg",
                                 2, th1520_memmap[TH1520_DEV_AP_RESET].base,
                                 2, th1520_memmap[TH1520_DEV_AP_RESET].size);
    qemu_fdt_setprop_cell(ms->fdt,
                          "/soc/reset-controller@ffef014000", "#reset-cells",
                          1);
    qemu_fdt_setprop_cell(ms->fdt,
                          "/soc/reset-controller@ffef014000", "phandle",
                          ap_reset_phandle);

    th1520_create_pinctrl_fdt(ms->fdt, ap_clock_phandle,
                              aonsys_clock_phandle, &phandle,
                              padctrl_phandles, &led_pins_phandle,
                              &gmac0_pins_phandle, &uart0_pins_phandle,
                              &wifi_pins_phandle);

    for (int i = 0; i < TH1520_GPIO_COUNT; i++) {
        const TH1520GPIOInfo *info = &th1520_gpio_info[i];
        const MemMapEntry *map = &th1520_memmap[info->memmap];
        fdt32_t ranges[2 * 4];
        g_autofree char *name =
            g_strdup_printf("/soc/gpio@%" HWADDR_PRIx, map->base);
        g_autofree char *port =
            g_strdup_printf("%s/gpio-controller@0", name);
        char alias[8];

        gpio_phandles[i] = phandle++;
        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible",
                                "snps,dw-apb-gpio");
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg", 2, map->base,
                                     2, map->size);
        qemu_fdt_setprop_cell(ms->fdt, name, "#address-cells", 1);
        qemu_fdt_setprop_cell(ms->fdt, name, "#size-cells", 0);
        if (info->clock_id >= 0) {
            qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                                   ap_clock_phandle, info->clock_id);
            qemu_fdt_setprop_string(ms->fdt, name, "clock-names", "bus");
        }

        qemu_fdt_add_subnode(ms->fdt, port);
        qemu_fdt_setprop_string(ms->fdt, port, "compatible",
                                "snps,dw-apb-gpio-port");
        qemu_fdt_setprop_cell(ms->fdt, port, "reg", 0);
        qemu_fdt_setprop(ms->fdt, port, "gpio-controller", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, port, "#gpio-cells", 2);
        qemu_fdt_setprop_cell(ms->fdt, port, "ngpios", info->ngpios);

        for (int range = 0; range < info->nranges; range++) {
            ranges[range * 4] = cpu_to_fdt32(
                padctrl_phandles[info->pad_group - 1]);
            ranges[range * 4 + 1] = cpu_to_fdt32(
                info->ranges[range].gpio_offset);
            ranges[range * 4 + 2] = cpu_to_fdt32(
                info->ranges[range].pin_offset);
            ranges[range * 4 + 3] = cpu_to_fdt32(
                info->ranges[range].count);
        }
        qemu_fdt_setprop(ms->fdt, port, "gpio-ranges", ranges,
                         info->nranges * 4 * sizeof(*ranges));
        qemu_fdt_setprop(ms->fdt, port, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, port, "#interrupt-cells", 2);
        qemu_fdt_setprop_cells(ms->fdt, port, "interrupts", info->irq, 4);
        qemu_fdt_setprop_cell(ms->fdt, port, "phandle",
                              gpio_phandles[i]);

        snprintf(alias, sizeof(alias), "gpio%d", i);
        qemu_fdt_setprop_string(ms->fdt, "/aliases", alias, port);
    }

    qemu_fdt_add_subnode(ms->fdt, "/leds");
    qemu_fdt_setprop_string(ms->fdt, "/leds", "compatible", "gpio-leds");
    qemu_fdt_setprop_string(ms->fdt, "/leds", "pinctrl-names", "default");
    qemu_fdt_setprop_cell(ms->fdt, "/leds", "pinctrl-0",
                          led_pins_phandle);
    for (int i = 0; i < 5; i++) {
        g_autofree char *name = g_strdup_printf("/leds/led-%d", i + 1);
        g_autofree char *label = g_strdup_printf("led%d", i + 1);

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_cells(ms->fdt, name, "gpios", gpio_phandles[4],
                               8 + i, 0);
        qemu_fdt_setprop_cell(ms->fdt, name, "color", 3);
        qemu_fdt_setprop_string(ms->fdt, name, "label", label);
    }

    for (int i = 0; i < TH1520_UART_COUNT; i++) {
        const MemMapEntry *map = &th1520_memmap[th1520_uart_memmap[i]];
        g_autofree char *name =
            g_strdup_printf("/soc/serial@%" HWADDR_PRIx, map->base);
        char alias[8];

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible",
                                "snps,dw-apb-uart");
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg", 2, map->base,
                                     2, map->size);
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                               th1520_uart_irqs[i], 4);
        qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                               ap_clock_phandle, TH1520_CLK_UART_SCLK,
                               ap_clock_phandle, th1520_uart_pclk_ids[i]);
        qemu_fdt_setprop_string_array(ms->fdt, name, "clock-names",
                                      (char **)&uart_clock_names,
                                      ARRAY_SIZE(uart_clock_names));
        qemu_fdt_setprop_cell(ms->fdt, name, "reg-shift", 2);
        qemu_fdt_setprop_cell(ms->fdt, name, "reg-io-width", 4);
        qemu_fdt_setprop_string(ms->fdt, name, "status",
                                i ? "disabled" : "okay");
        if (i == 0) {
            qemu_fdt_setprop_string(ms->fdt, name, "pinctrl-names",
                                    "default");
            qemu_fdt_setprop_cell(ms->fdt, name, "pinctrl-0",
                                  uart0_pins_phandle);
        }
        snprintf(alias, sizeof(alias), "serial%d", i);
        qemu_fdt_setprop_string(ms->fdt, "/aliases", alias, name);
    }
    qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path",
                            "serial0:115200n8");

    for (int i = 0; i < TH1520_I2C_COUNT; i++) {
        const TH1520I2CInfo *info = &th1520_i2c_info[i];
        const MemMapEntry *map = &th1520_memmap[info->memmap];
        g_autofree char *name =
            g_strdup_printf("/soc/i2c@%" HWADDR_PRIx, map->base);

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string_array(ms->fdt, name, "compatible",
                                      (char **)&i2c_compat,
                                      ARRAY_SIZE(i2c_compat));
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg", 2, map->base,
                                     2, map->size);
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts", info->irq, 4);
        qemu_fdt_setprop_cells(ms->fdt, name, "clocks", ap_clock_phandle,
                               info->clock_id);
        qemu_fdt_setprop_cell(ms->fdt, name, "#address-cells", 1);
        qemu_fdt_setprop_cell(ms->fdt, name, "#size-cells", 0);
        qemu_fdt_setprop_string(ms->fdt, name, "status",
                                i ? "disabled" : "okay");

        if (i == 0) {
            g_autofree char *eeprom = g_strdup_printf(
                "%s/eeprom@%x", name, BEAGLEV_AHEAD_EEPROM_ADDRESS);

            qemu_fdt_add_subnode(ms->fdt, eeprom);
            qemu_fdt_setprop_string(ms->fdt, eeprom, "compatible",
                                    "atmel,24c32");
            qemu_fdt_setprop_cell(ms->fdt, eeprom, "reg",
                                  BEAGLEV_AHEAD_EEPROM_ADDRESS);
            qemu_fdt_setprop_cell(ms->fdt, eeprom, "pagesize",
                                  BEAGLEV_AHEAD_EEPROM_PAGE_SIZE);
        }
    }

    for (int i = 0; i < TH1520_SPI_COUNT; i++) {
        const TH1520SPIInfo *info = &th1520_spi_info[i];
        const MemMapEntry *map = &th1520_memmap[info->memmap];
        g_autofree char *name =
            g_strdup_printf("/soc/spi@%" HWADDR_PRIx, map->base);
        char alias[8];

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string_array(ms->fdt, name, "compatible",
                                      (char **)&spi_compat,
                                      ARRAY_SIZE(spi_compat));
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg", 2, map->base,
                                     2, map->size);
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts", info->irq, 4);
        qemu_fdt_setprop_cells(ms->fdt, name, "clocks", ap_clock_phandle,
                               info->clock_id);
        qemu_fdt_setprop_cell(ms->fdt, name, "#address-cells", 1);
        qemu_fdt_setprop_cell(ms->fdt, name, "#size-cells", 0);
        qemu_fdt_setprop_string(ms->fdt, name, "status", "disabled");
        snprintf(alias, sizeof(alias), "spi%d", i);
        qemu_fdt_setprop_string(ms->fdt, "/aliases", alias, name);
    }

    qemu_fdt_add_subnode(ms->fdt, "/soc/pwm@ffec01c000");
    qemu_fdt_setprop_string(ms->fdt, "/soc/pwm@ffec01c000", "compatible",
                            "thead,th1520-pwm");
    qemu_fdt_setprop_sized_cells(ms->fdt, "/soc/pwm@ffec01c000", "reg",
                                 2, th1520_memmap[TH1520_DEV_PWM].base, 2,
                                 th1520_memmap[TH1520_DEV_PWM].size);
    qemu_fdt_setprop_cells(ms->fdt, "/soc/pwm@ffec01c000", "clocks",
                           ap_clock_phandle, TH1520_PWM_CLOCK_ID);
    qemu_fdt_setprop_cell(ms->fdt, "/soc/pwm@ffec01c000", "#pwm-cells", 3);

    for (int group = 0; group < TH1520_TIMER_GROUP_COUNT; group++) {
        const TH1520TimerInfo *info = &th1520_timer_info[group];
        const MemMapEntry *map = &th1520_memmap[info->memmap];

        for (int channel = 0; channel < DW_APB_TIMER_CHANNELS; channel++) {
            hwaddr base = map->base + channel * TH1520_TIMER_CHANNEL_STRIDE;
            g_autofree char *name =
                g_strdup_printf("/soc/timer@%" HWADDR_PRIx, base);

            qemu_fdt_add_subnode(ms->fdt, name);
            qemu_fdt_setprop_string(ms->fdt, name, "compatible",
                                    "snps,dw-apb-timer");
            qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                         2, base, 2,
                                         TH1520_TIMER_CHANNEL_STRIDE);
            qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                                   ap_clock_phandle,
                                   TH1520_CLK_PERI_APB_PCLK);
            qemu_fdt_setprop_string(ms->fdt, name, "clock-names", "timer");
            qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                                   info->first_irq + channel, 4);
            qemu_fdt_setprop_string(ms->fdt, name, "status", "disabled");
        }
    }

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
                           ap_clock_phandle, TH1520_CLK_PERI_APB_PCLK,
                           ap_clock_phandle, TH1520_CLK_PERI_APB_PCLK);
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
        qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                               ap_clock_phandle, TH1520_CLK_EMMC_SDIO);
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
            qemu_fdt_setprop_string(ms->fdt, name, "pinctrl-names",
                                    "default");
            qemu_fdt_setprop_cell(ms->fdt, name, "pinctrl-0",
                                  wifi_pins_phandle);
        }
    }

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
                               ap_clock_phandle, TH1520_CLK_GMAC_AXI,
                               ap_clock_phandle,
                               i ? TH1520_CLK_GMAC1 : TH1520_CLK_GMAC0,
                               ap_clock_phandle,
                               TH1520_CLK_PERISYS_APB4_HCLK);
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
            qemu_fdt_setprop_string(ms->fdt, name, "pinctrl-names",
                                    "default");
            qemu_fdt_setprop_cell(ms->fdt, name, "pinctrl-0",
                                  gmac0_pins_phandle);
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

static void beaglev_ahead_attach_eeprom(BeagleVAheadState *s)
{
    g_autofree uint8_t *contents = g_malloc(BEAGLEV_AHEAD_EEPROM_SIZE);

    /* Factory-programmed, board-unique bytes are deliberately not invented. */
    memset(contents, 0xff, BEAGLEV_AHEAD_EEPROM_SIZE);
    at24c_eeprom_init_rom(s->soc.i2c[0].bus,
                          BEAGLEV_AHEAD_EEPROM_ADDRESS,
                          BEAGLEV_AHEAD_EEPROM_SIZE, contents,
                          BEAGLEV_AHEAD_EEPROM_SIZE);
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
    int fdt_size;

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
    beaglev_ahead_attach_eeprom(s);

    if (ms->dtb) {
        ms->fdt = load_device_tree(ms->dtb, &fdt_size);
        if (!ms->fdt) {
            error_report("load_device_tree() failed");
            exit(EXIT_FAILURE);
        }
    } else {
        beaglev_ahead_create_fdt(s);
    }

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
