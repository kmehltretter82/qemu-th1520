/*
 * T-Head TH1520 SoC and BeagleV Ahead board
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_TH1520_H
#define HW_RISCV_TH1520_H

#include "hw/core/boards.h"
#include "hw/char/dw_apb_uart.h"
#include "hw/dma/dw_axi_dmac.h"
#include "hw/gpio/dw_apb_gpio.h"
#include "hw/intc/thead_c900_clint.h"
#include "hw/intc/thead_c900_plic.h"
#include "hw/misc/th1520_cpr.h"
#include "hw/net/dw_gmac.h"
#include "hw/net/th1520_gmac.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/sd/dwcmshc.h"

#define TH1520_MSHC_COUNT 3
#define TH1520_GMAC_COUNT 2
#define TH1520_UART_COUNT 6
#define TH1520_GPIO_COUNT 6

#define TYPE_RISCV_TH1520_SOC "riscv.th1520.soc"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520SoCState, RISCV_TH1520_SOC)

struct TH1520SoCState {
    DeviceState parent_obj;

    RISCVHartArrayState c910_cpus;
    MemoryRegion sram;
    MemoryRegion brom;
    THeadC900CLINTState clint;
    THeadC900PLICState plic;
    TH1520APClockState ap_clock;
    TH1520APResetState ap_reset;
    DWAPBUARTState uart[TH1520_UART_COUNT];
    DWAPBGPIOState gpio[TH1520_GPIO_COUNT];
    DWAxiDMACState dmac0;
    DWGMACState gmac[TH1520_GMAC_COUNT];
    TH1520GMACAPBState gmac_apb[TH1520_GMAC_COUNT];
    DWCMSHCState mshc[TH1520_MSHC_COUNT];
};

#define TYPE_BEAGLEV_AHEAD_MACHINE MACHINE_TYPE_NAME("beaglev-ahead")
OBJECT_DECLARE_SIMPLE_TYPE(BeagleVAheadState, BEAGLEV_AHEAD_MACHINE)

struct BeagleVAheadState {
    MachineState parent_obj;

    TH1520SoCState soc;
    Notifier machine_done;
};

enum {
    TH1520_DEV_DRAM,
    TH1520_DEV_PLIC,
    TH1520_DEV_CLINT,
    TH1520_DEV_SRAM,
    TH1520_DEV_AP_CLOCK,
    TH1520_DEV_AP_RESET,
    TH1520_DEV_UART0,
    TH1520_DEV_UART1,
    TH1520_DEV_UART2,
    TH1520_DEV_UART3,
    TH1520_DEV_UART4,
    TH1520_DEV_UART5,
    TH1520_DEV_GPIO0,
    TH1520_DEV_GPIO1,
    TH1520_DEV_GPIO2,
    TH1520_DEV_GPIO3,
    TH1520_DEV_GPIO4,
    TH1520_DEV_AOGPIO,
    TH1520_DEV_DMAC0,
    TH1520_DEV_GMAC0,
    TH1520_DEV_GMAC1,
    TH1520_DEV_GMAC0_APB,
    TH1520_DEV_GMAC1_APB,
    TH1520_DEV_EMMC,
    TH1520_DEV_SDIO0,
    TH1520_DEV_SDIO1,
    TH1520_DEV_BROM,
};

#define TH1520_C910_HARTS 4
#define TH1520_C910_VLENB 16
#define TH1520_TIMEBASE_FREQ 3000000
#define TH1520_OSC_FREQ 24000000

/* IDs from the thead,th1520-clk-ap binding used by modeled peripherals. */
#define TH1520_CLK_PERI_APB_PCLK 20
#define TH1520_CLK_PERISYS_APB4_HCLK 25
#define TH1520_CLK_EMMC_SDIO 43
#define TH1520_CLK_GMAC1 44
#define TH1520_CLK_GMAC_AXI 48
#define TH1520_CLK_GPIO3 49
#define TH1520_CLK_GMAC0 50
#define TH1520_CLK_UART0_PCLK 55
#define TH1520_CLK_UART1_PCLK 56
#define TH1520_CLK_UART2_PCLK 57
#define TH1520_CLK_UART3_PCLK 58
#define TH1520_CLK_UART4_PCLK 59
#define TH1520_CLK_UART5_PCLK 60
#define TH1520_CLK_GPIO0 61
#define TH1520_CLK_GPIO1 62
#define TH1520_CLK_GPIO2 63
#define TH1520_CLK_UART_SCLK 85

/* riscv,ndev describes IDs 1..240; QEMU's PLIC count includes ID zero. */
#define TH1520_PLIC_NDEV 240
#define TH1520_PLIC_NUM_SOURCES (TH1520_PLIC_NDEV + 1)

#define TH1520_UART0_IRQ 36
#define TH1520_UART1_IRQ 37
#define TH1520_UART2_IRQ 38
#define TH1520_UART3_IRQ 39
#define TH1520_UART4_IRQ 40
#define TH1520_UART5_IRQ 41
#define TH1520_UART_INPUT_FREQ 100000000

#define TH1520_GPIO0_IRQ 56
#define TH1520_GPIO1_IRQ 57
#define TH1520_GPIO2_IRQ 58
#define TH1520_GPIO3_IRQ 59
#define TH1520_GPIO4_IRQ 55
#define TH1520_AOGPIO_IRQ 76

#define TH1520_DMAC0_IRQ 27
#define TH1520_DMAC_CHANNELS 4
#define TH1520_DMAC_BLOCK_SIZE 65536
#define TH1520_DMAC_DATA_WIDTH 4

#define TH1520_GMAC0_IRQ 66
#define TH1520_GMAC1_IRQ 67
#define TH1520_GMAC_VERSION 0x00001037
#define TH1520_GMAC_HW_FEATURE 0x110d0107
#define TH1520_GMAC_PHY_ADDR 1
#define TH1520_RTL8211F_PHY_ID1 0x001c
#define TH1520_RTL8211F_PHY_ID2 0xc878

#define TH1520_EMMC_IRQ 62
#define TH1520_SDIO0_IRQ 64
#define TH1520_SDIO1_IRQ 71
#define TH1520_MSHC_INPUT_FREQ 198000000

#endif /* HW_RISCV_TH1520_H */
