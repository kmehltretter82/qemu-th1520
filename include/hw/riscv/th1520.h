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
#include "hw/i2c/designware_i2c.h"
#include "hw/intc/thead_c900_clint.h"
#include "hw/intc/thead_c900_plic.h"
#include "hw/misc/th1520_aon_reset.h"
#include "hw/misc/th1520_bootsel.h"
#include "hw/misc/th1520_cpr.h"
#include "hw/misc/th1520_iopmp.h"
#include "hw/misc/th1520_iso7816.h"
#include "hw/misc/th1520_mbox.h"
#include "hw/misc/th1520_miscsys.h"
#include "hw/misc/th1520_pinctrl.h"
#include "hw/misc/th1520_tee_miscsys_clock.h"
#include "hw/misc/th1520_video_sysreg.h"
#include "hw/net/dw_gmac.h"
#include "hw/net/th1520_gmac.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/rtc/xgene_rtc.h"
#include "hw/sd/dwcmshc.h"
#include "hw/sensor/mr75203.h"
#include "hw/ssi/dw_apb_ssi.h"
#include "hw/timer/dw_apb_timer.h"
#include "hw/timer/th1520_pwm.h"
#include "hw/usb/th1520_usb.h"
#include "hw/watchdog/dw_apb_wdt.h"

#define TH1520_MSHC_COUNT 3
#define TH1520_GMAC_COUNT 2
#define TH1520_UART_COUNT 6
#define TH1520_GPIO_COUNT 6
#define TH1520_PADCTRL_COUNT 3
#define TH1520_I2C_COUNT 6
#define TH1520_SPI_COUNT 1
#define TH1520_TIMER_GROUP_COUNT 2
#define TH1520_WDT_COUNT 2
#define TH1520_IOPMP_COUNT 30

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
    MemoryRegion ap_clock_vendor_alias;
    TH1520APResetState ap_reset;
    MemoryRegion ap_reset_vendor_npu_alias;
    TH1520AONResetState aon_reset;
    TH1520MiscSysState miscsys;
    MemoryRegion tee_miscsys_usb_clock_alias;
    TH1520TEEMiscSysClockState tee_miscsys_clock;
    TH1520VideoSysRegState visys;
    TH1520VideoSysRegState vosys;
    TH1520ISO7816ConfigState iso7816_config;
    TH1520BootSelState bootsel;
    TH1520USBState usb;
    TH1520MboxState mbox;
    TH1520IOPMPState iopmp[TH1520_IOPMP_COUNT];
    MR75203State pvt;
    Clock *pvt_clk;
    XGeneRTCState rtc;
    Clock *rtc_clk;
    DWAPBUARTState uart[TH1520_UART_COUNT];
    DWAPBGPIOState gpio[TH1520_GPIO_COUNT];
    TH1520PadCtrlState padctrl[TH1520_PADCTRL_COUNT];
    DesignWareI2CState i2c[TH1520_I2C_COUNT];
    DWAPBSSIState spi[TH1520_SPI_COUNT];
    DWAPBTimerState timer[TH1520_TIMER_GROUP_COUNT];
    DWAPBWDTState wdt[TH1520_WDT_COUNT];
    TH1520PWMState pwm;
    DWAxiDMACState dmac0;
    DWGMACState gmac[TH1520_GMAC_COUNT];
    TH1520GMACAPBState gmac_apb[TH1520_GMAC_COUNT];
    DWCMSHCState mshc[TH1520_MSHC_COUNT];
};

#define TYPE_BEAGLEV_AHEAD_MACHINE MACHINE_TYPE_NAME("beaglev-ahead")
OBJECT_DECLARE_SIMPLE_TYPE(BeagleVAheadState, BEAGLEV_AHEAD_MACHINE)

typedef enum BeagleVAheadBootMode {
    BEAGLEV_AHEAD_BOOT_DIRECT,
    BEAGLEV_AHEAD_BOOT_MASK_ROM,
} BeagleVAheadBootMode;

struct BeagleVAheadState {
    MachineState parent_obj;

    TH1520SoCState soc;
    Notifier machine_done;
    BeagleVAheadBootMode boot_mode;
    uint8_t boot_sel;
};

enum {
    TH1520_DEV_DRAM,
    TH1520_DEV_PLIC,
    TH1520_DEV_CLINT,
    TH1520_DEV_SRAM,
    TH1520_DEV_AP_CLOCK,
    TH1520_DEV_AP_RESET,
    TH1520_DEV_AON_AUDIO_RESET,
    TH1520_DEV_MISCSYS,
    TH1520_DEV_TEE_MISCSYS_CLOCK,
    TH1520_DEV_VISYS,
    TH1520_DEV_VOSYS,
    TH1520_DEV_ISO7816_CONFIG,
    TH1520_DEV_BOOTSEL,
    TH1520_DEV_USB_DRD,
    TH1520_DEV_USB_CORE,
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
    TH1520_DEV_PADCTRL_AOSYS,
    TH1520_DEV_PADCTRL1_APSYS,
    TH1520_DEV_PADCTRL0_APSYS,
    TH1520_DEV_I2C0,
    TH1520_DEV_I2C1,
    TH1520_DEV_I2C2,
    TH1520_DEV_I2C3,
    TH1520_DEV_I2C4,
    TH1520_DEV_I2C5,
    TH1520_DEV_SPI0,
    TH1520_DEV_PWM,
    TH1520_DEV_TIMER0_3,
    TH1520_DEV_TIMER4_7,
    TH1520_DEV_WDT0,
    TH1520_DEV_WDT1,
    TH1520_DEV_MBOX_LOCAL,
    TH1520_DEV_MBOX_REMOTE0,
    TH1520_DEV_MBOX_REMOTE1,
    TH1520_DEV_MBOX_REMOTE2,
    TH1520_DEV_PVT_COMMON,
    TH1520_DEV_PVT_TS,
    TH1520_DEV_PVT_PD,
    TH1520_DEV_PVT_VM,
    TH1520_DEV_RTC,
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
#define TH1520_CLK_PADCTRL1 45
#define TH1520_CLK_PADCTRL0 47
#define TH1520_CLK_GMAC_AXI 48
#define TH1520_CLK_GPIO3 49
#define TH1520_CLK_GMAC0 50
#define TH1520_CLK_SPI 54
#define TH1520_CLK_UART0_PCLK 55
#define TH1520_CLK_UART1_PCLK 56
#define TH1520_CLK_UART2_PCLK 57
#define TH1520_CLK_UART3_PCLK 58
#define TH1520_CLK_UART4_PCLK 59
#define TH1520_CLK_UART5_PCLK 60
#define TH1520_CLK_GPIO0 61
#define TH1520_CLK_GPIO1 62
#define TH1520_CLK_GPIO2 63
#define TH1520_CLK_I2C0 64
#define TH1520_CLK_I2C1 65
#define TH1520_CLK_I2C2 66
#define TH1520_CLK_I2C3 67
#define TH1520_CLK_I2C4 68
#define TH1520_CLK_I2C5 69
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

#define TH1520_I2C0_IRQ 44
#define TH1520_I2C1_IRQ 45
#define TH1520_I2C2_IRQ 46
#define TH1520_I2C3_IRQ 47
#define TH1520_I2C4_IRQ 48
#define TH1520_I2C5_IRQ 49
#define TH1520_I2C_COMPONENT_PARAMETERS 0x000f0fee
#define TH1520_I2C_COMPONENT_VERSION 0x3230322a
#define TH1520_I2C_COMPONENT_TYPE 0x44570140
#define TH1520_I2C_INTR_MASK_RESET 0x000048ff
#define TH1520_I2C_INTR_MASK_VALID 0x00004fff

#define TH1520_SPI0_IRQ 54

#define TH1520_PWM_CLOCK_ID 51

#define TH1520_TIMER0_IRQ 16
#define TH1520_TIMER4_IRQ 20
#define TH1520_TIMER_CHANNEL_STRIDE 0x14
#define TH1520_TIMER_COMPONENT_VERSION 0x3231322a

#define TH1520_WDT0_IRQ 24
#define TH1520_WDT1_IRQ 25
#define TH1520_CLK_WDT0 76
#define TH1520_CLK_WDT1 77
#define TH1520_RESET_ID_WDT0 3
#define TH1520_RESET_ID_WDT1 4
#define TH1520_WDT_COMPONENT_PARAM_1 DW_APB_WDT_PARAM_1_USE_FIX_TOP
#define TH1520_WDT_COUNTER_RESET 0x0000ffff

#define TH1520_USB_IRQ 68

#define TH1520_MBOX_IRQ 28
#define TH1520_CLK_MBOX0 72
#define TH1520_CLK_MBOX1 73
#define TH1520_CLK_MBOX2 74
#define TH1520_CLK_MBOX3 75

#define TH1520_PVT_INPUT_FREQ 73728000
#define TH1520_PVT_COMPONENT_ID 0x9b487060
#define TH1520_PVT_ID_NUMBER 0x12345678
#define TH1520_PVT_TS_COUNT 2
#define TH1520_PVT_PD_COUNT 11
#define TH1520_PVT_VM_COUNT 1
#define TH1520_PVT_VM_CHANNELS 16
#define TH1520_PVT_TS_COEFF_G 42740
#define TH1520_PVT_TS_COEFF_H 220500
#define TH1520_PVT_TS_COEFF_J (-160)
#define TH1520_PVT_TS_COEFF_CAL5 4094

#define TH1520_RTC_IRQ 74
#define TH1520_RTC_INPUT_FREQ 32768

#define BEAGLEV_AHEAD_EEPROM_ADDRESS 0x50
#define BEAGLEV_AHEAD_EEPROM_SIZE 4096
#define BEAGLEV_AHEAD_EEPROM_PAGE_SIZE 32

#define TH1520_DMAC0_IRQ 27
#define TH1520_DMAC_CHANNELS 4
#define TH1520_DMAC_BLOCK_SIZE 65536
#define TH1520_DMAC_DATA_WIDTH 4

#define TH1520_GMAC0_IRQ 66
#define TH1520_GMAC1_IRQ 67
#define TH1520_GMAC_VERSION 0x00001037
#define TH1520_GMAC_HW_FEATURE 0x110d0107
#define TH1520_GMAC_HASH_BINS 64
#define TH1520_GMAC_MAC_ADDRS 32
#define TH1520_GMAC_PHY_ADDR 1
#define TH1520_RTL8211F_PHY_ID1 0x001c
#define TH1520_RTL8211F_PHY_ID2 0xc878

#define TH1520_EMMC_IRQ 62
#define TH1520_SDIO0_IRQ 64
#define TH1520_SDIO1_IRQ 71
#define TH1520_MSHC_INPUT_FREQ 198000000

#endif /* HW_RISCV_TH1520_H */
