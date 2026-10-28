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
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "net/eth.h"
#include "net/net.h"
#include "system/device_tree.h"
#include "system/blockdev.h"
#include "system/system.h"
#include "system/memory.h"
#include "system/qtest.h"
#include "system/tcg.h"
#include "target/riscv/cpu.h"
#include "target/riscv/cpu_bits.h"
#include "hw/char/dw_apb_uart.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/intc/thead_c900_clint.h"
#include "hw/intc/thead_c900_plic.h"
#include "hw/misc/led.h"
#include "hw/net/dw_gmac.h"
#include "hw/net/th1520_gmac.h"
#include "hw/i2c/da9063.h"
#include "hw/i2c/i2c.h"
#include "hw/nvram/eeprom_at24c.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/fdt-common.h"
#include "hw/riscv/machines-qom.h"
#include "hw/riscv/th1520.h"
#include "hw/sd/sd.h"
#include "hw/ssi/ssi.h"
#include "chardev/char.h"
#include "migration/vmstate.h"
#include "qobject/qdict.h"
#include "qemu/timer.h"
#include "system/block-backend.h"

#include <libfdt.h>

#define TH1520_GMAC_RIWT_CLOCK_HZ 500000000

/*
 * Vendor U-Boot accesses the AP clock register layout through this fullmask
 * address, whereas the RevyOS DT exposes the same layout at
 * 0xffef010000.  Keep it as an explicit compatibility aperture, with shared
 * state, until hardware establishes whether this is a fabric alias or a
 * firmware-only address convention.  It deliberately has no DT node.
 */
#define TH1520_VENDOR_UBOOT_AP_CLOCK_BASE 0xffff011000ULL

/*
 * Vendor SPL writes the NPU reset-generator word through this fullmask
 * address, while the generated DT exposes the AP reset block at
 * 0xffef014000.  Map only this source-backed word as a shared compatibility
 * alias until hardware establishes its aperture, aliasing and reset effects.
 * It deliberately has no DT node and no NPU reset output.
 */
#define TH1520_VENDOR_UBOOT_AP_RESET_NPU_BASE 0xffff0151b0ULL
#define TH1520_VENDOR_UBOOT_AP_RESET_NPU_OFFSET 0x1b0
#define TH1520_VENDOR_UBOOT_AP_RESET_NPU_SIZE 0x4

/*
 * Vendor U-Boot enables the four USB gates through this TEE-misc address,
 * whereas vendor Linux describes the gate register at REE misc-system +
 * 0x104.  Keep the precise compatibility access shared with the REE state;
 * it neither claims a physical fabric alias nor creates a generic TEE
 * aperture or DT node.
 */
#define TH1520_VENDOR_UBOOT_USB_CLOCK_BASE 0xfffc02d104ULL

/* Vendor AON-I2C firmware exposes only IC_ENABLE.ENABLE. */
#define TH1520_AON_I2C_ENABLE_MASK 0x1

static const MemMapEntry th1520_memmap[] = {
    [TH1520_DEV_DRAM]  = { 0x0000000000, 0x100000000 },
    [TH1520_DEV_PLIC]  = { 0xffd8000000, 0x01000000 },
    [TH1520_DEV_CLINT] = { 0xffdc000000, 0x00010000 },
    [TH1520_DEV_PMP_PORTAL] = { 0xffdc020000,
                                 TH1520_PMP_PORTAL_APERTURE_SIZE },
    [TH1520_DEV_SRAM]  = { 0xffe0000000, 0x00180000 },
    [TH1520_DEV_AP_CLOCK] = { 0xffef010000, 0x00001000 },
    [TH1520_DEV_DDR_CONTROLLER] = { 0xffff000000,
                                     TH1520_DDR_CONTROLLER_MMIO_SIZE },
    [TH1520_DEV_DDR_PHY0] = { 0xfffd000000, TH1520_DDR_PHY_MMIO_SIZE },
    [TH1520_DEV_DDR_PHY1] = { 0xfffe000000, TH1520_DDR_PHY_MMIO_SIZE },
    [TH1520_DEV_DDR_CFG0] = { 0xffff005000,
                               TH1520_DDR_CONTROL_MMIO_SIZE },
    [TH1520_DEV_DDR_PLL_CFG0] = { 0xffff005008,
                                   TH1520_DDR_PLL_MMIO_SIZE },
    [TH1520_DEV_DDR_PLL_CFG1] = { 0xffff00500c,
                                   TH1520_DDR_PLL_MMIO_SIZE },
    [TH1520_DEV_DDR_PLL_STS] = { 0xffff005018,
                                  TH1520_DDR_PLL_MMIO_SIZE },
    [TH1520_DEV_AP_RESET] = { 0xffef014000, 0x00001000 },
    [TH1520_DEV_AON_AUDIO_RESET] = {
        0xfffff4403c, TH1520_AON_RESET_MMIO_SIZE },
    [TH1520_DEV_MISCSYS] = { 0xffec02c000, 0x00001000 },
    [TH1520_DEV_TEE_MISCSYS_CLOCK] = { 0xfffc02d120,
                                        TH1520_TEE_MISCSYS_CLOCK_MMIO_SIZE },
    [TH1520_DEV_TEE_DSP_RESET] = { 0xffff041028,
                                    TH1520_TEE_DSP_RESET_MMIO_SIZE },
    [TH1520_DEV_TEE_VOSYS_DPU_RESET] = {
        0xffff529004, TH1520_TEE_VOSYS_DPU_RESET_MMIO_SIZE
    },
    [TH1520_DEV_VISYS] = { 0xffe4040000, 0x00001000 },
    [TH1520_DEV_VOSYS] = { 0xffef528000, 0x00001000 },
    [TH1520_DEV_ISO7816_CONFIG] = { 0xfff7f30010,
                                    TH1520_ISO7816_CONFIG_MMIO_SIZE },
    [TH1520_DEV_BOOTSEL] = { 0xffef018010,
                              TH1520_BOOTSEL_MMIO_SIZE },
    [TH1520_DEV_USB_DRD] = { 0xffec03f000, 0x00001000 },
    [TH1520_DEV_USB_CORE] = { 0xffe7040000, 0x00010000 },
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
    [TH1520_DEV_AON_I2C] = { 0xfffff4c000, 0x00001000 },
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
    [TH1520_DEV_WDT0] = { 0xffefc30000, 0x00001000 },
    [TH1520_DEV_WDT1] = { 0xffefc31000, 0x00001000 },
    [TH1520_DEV_MBOX_LOCAL] = { 0xffffc38000,
                                 TH1520_MBOX_LOCAL_MMIO_SIZE },
    [TH1520_DEV_MBOX_REMOTE0] = { 0xffffc40000,
                                   TH1520_MBOX_REMOTE0_MMIO_SIZE },
    [TH1520_DEV_MBOX_REMOTE1] = { 0xffffc4c000,
                                   TH1520_MBOX_REMOTE1_MMIO_SIZE },
    [TH1520_DEV_MBOX_REMOTE2] = { 0xffffc54000,
                                   TH1520_MBOX_REMOTE2_MMIO_SIZE },
    [TH1520_DEV_PVT_COMMON] = { 0xfffff4e000, 0x00000080 },
    [TH1520_DEV_PVT_TS]     = { 0xfffff4e080, 0x00000100 },
    [TH1520_DEV_PVT_PD]     = { 0xfffff4e180, 0x00000680 },
    [TH1520_DEV_PVT_VM]     = { 0xfffff4e800, 0x00000600 },
    [TH1520_DEV_RTC]        = { 0xfffff40000, 0x00001000 },
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

static const uint32_t th1520_pmp_portal_offsets[
    TH1520_PMP_PORTAL_REG_COUNT] = {
    TH1520_PMP_PORTAL_CONFIG,
    TH1520_PMP_PORTAL_WORD_100,
    TH1520_PMP_PORTAL_WORD_104,
    TH1520_PMP_PORTAL_WORD_108,
    TH1520_PMP_PORTAL_WORD_10C,
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
    int16_t vendor_pclk_id;
    int16_t vendor_dbclk_id;
} TH1520GPIOInfo;

static const TH1520GPIOInfo th1520_gpio_info[TH1520_GPIO_COUNT] = {
    { "gpio0",  TH1520_DEV_GPIO0,  TH1520_GPIO0_IRQ,  32,
      TH1520_CLK_GPIO0, 2, 1, { { 0, 0, 32 } },
      TH1520_VCLK_GPIO0_PCLK, TH1520_VCLK_GPIO0_DBCLK },
    { "gpio1",  TH1520_DEV_GPIO1,  TH1520_GPIO1_IRQ,  31,
      TH1520_CLK_GPIO1, 2, 1, { { 0, 32, 31 } },
      TH1520_VCLK_GPIO1_PCLK, TH1520_VCLK_GPIO1_DBCLK },
    { "gpio2",  TH1520_DEV_GPIO2,  TH1520_GPIO2_IRQ,  32,
      TH1520_CLK_GPIO2, 3, 1, { { 0, 0, 32 } },
      TH1520_VCLK_GPIO2_PCLK, TH1520_VCLK_GPIO2_DBCLK },
    { "gpio3",  TH1520_DEV_GPIO3,  TH1520_GPIO3_IRQ,  23,
      TH1520_CLK_GPIO3, 3, 1, { { 0, 32, 23 } },
      TH1520_VCLK_GPIO3_PCLK, TH1520_VCLK_GPIO3_DBCLK },
    { "gpio4",  TH1520_DEV_GPIO4,  TH1520_GPIO4_IRQ,  23, -1,
      1, 2, { { 0, 25, 22 }, { 22, 7, 1 } }, -1, -1 },
    { "aogpio", TH1520_DEV_AOGPIO, TH1520_AOGPIO_IRQ, 16, -1,
      1, 1, { { 0, 9, 16 } }, -1, -1 },
};

typedef struct TH1520PadCtrlInfo {
    const char *name;
    int memmap;
    uint8_t group;
    int16_t clock_id;
    int16_t vendor_clock_id;
} TH1520PadCtrlInfo;

static const TH1520PadCtrlInfo
th1520_padctrl_info[TH1520_PADCTRL_COUNT] = {
    { "padctrl-aosys",  TH1520_DEV_PADCTRL_AOSYS,  1, -1, -1 },
    { "padctrl1-apsys", TH1520_DEV_PADCTRL1_APSYS, 2,
      TH1520_CLK_PADCTRL1, TH1520_VCLK_PADCTRL1_APSYS_PCLK },
    { "padctrl0-apsys", TH1520_DEV_PADCTRL0_APSYS, 3,
      TH1520_CLK_PADCTRL0, TH1520_VCLK_PADCTRL0_APSYS_PCLK },
};

typedef struct TH1520I2CInfo {
    const char *name;
    int memmap;
    uint32_t irq;
    uint32_t clock_id;
    uint32_t vendor_clock_id;
} TH1520I2CInfo;

static const TH1520I2CInfo th1520_i2c_info[TH1520_I2C_COUNT] = {
    { "i2c0", TH1520_DEV_I2C0, TH1520_I2C0_IRQ, TH1520_CLK_I2C0,
      TH1520_VCLK_I2C0_PCLK },
    { "i2c1", TH1520_DEV_I2C1, TH1520_I2C1_IRQ, TH1520_CLK_I2C1,
      TH1520_VCLK_I2C1_PCLK },
    { "i2c2", TH1520_DEV_I2C2, TH1520_I2C2_IRQ, TH1520_CLK_I2C2,
      TH1520_VCLK_I2C2_PCLK },
    { "i2c3", TH1520_DEV_I2C3, TH1520_I2C3_IRQ, TH1520_CLK_I2C3,
      TH1520_VCLK_I2C3_PCLK },
    { "i2c4", TH1520_DEV_I2C4, TH1520_I2C4_IRQ, TH1520_CLK_I2C4,
      TH1520_VCLK_I2C4_PCLK },
    { "i2c5", TH1520_DEV_I2C5, TH1520_I2C5_IRQ, TH1520_CLK_I2C5,
      TH1520_VCLK_I2C5_PCLK },
};

static void th1520_i2c_configure(DeviceState *i2c)
{
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

static void th1520_aon_i2c_configure(DeviceState *i2c)
{
    th1520_i2c_configure(i2c);
    qdev_prop_set_uint32(i2c, "enable-mask", TH1520_AON_I2C_ENABLE_MASK);
    /* Vendor SPL observes FIFO drain before waiting for STOP_DET. */
    qdev_prop_set_bit(i2c, "implicit-stop-on-fifo-drain", true);
}

typedef struct TH1520TimerInfo {
    const char *name;
    int memmap;
    uint32_t first_irq;
} TH1520TimerInfo;

typedef struct TH1520WDTInfo {
    const char *name;
    int memmap;
    uint32_t irq;
    uint32_t clock_id;
    uint32_t reset_id;
    unsigned int reset_output;
    uint32_t vendor_clock_id;
} TH1520WDTInfo;

typedef struct TH1520SPIInfo {
    const char *name;
    int memmap;
    uint32_t irq;
    uint32_t clock_id;
    uint32_t vendor_sclk_id;
    uint32_t vendor_pclk_id;
} TH1520SPIInfo;

typedef struct TH1520IOPMPInfo {
    const char *name;
    hwaddr base;
} TH1520IOPMPInfo;

static const TH1520TimerInfo
th1520_timer_info[TH1520_TIMER_GROUP_COUNT] = {
    { "timer0-3", TH1520_DEV_TIMER0_3, TH1520_TIMER0_IRQ },
    { "timer4-7", TH1520_DEV_TIMER4_7, TH1520_TIMER4_IRQ },
};

static const TH1520WDTInfo th1520_wdt_info[TH1520_WDT_COUNT] = {
    { "wdt0", TH1520_DEV_WDT0, TH1520_WDT0_IRQ, TH1520_CLK_WDT0,
      TH1520_RESET_ID_WDT0, TH1520_AP_RESET_WDT0,
      TH1520_VCLK_WDT0_PCLK },
    { "wdt1", TH1520_DEV_WDT1, TH1520_WDT1_IRQ, TH1520_CLK_WDT1,
      TH1520_RESET_ID_WDT1, TH1520_AP_RESET_WDT1,
      TH1520_VCLK_WDT1_PCLK },
};

static const TH1520SPIInfo th1520_spi_info[TH1520_SPI_COUNT] = {
    { "spi0", TH1520_DEV_SPI0, TH1520_SPI0_IRQ, TH1520_CLK_SPI,
      TH1520_VCLK_SPI_SSI_CLK, TH1520_VCLK_SPI_PCLK },
};

/*
 * Vendor U-Boot writes the default attribute in all of these apertures
 * during board_init().  Its IOPMP_AUD and IOPMP_AUDIO IDs both refer to the
 * aud aperture, so this table intentionally contains the 30 unique windows.
 * No DT node is generated: the vendor IOPMP binding is not established for
 * the BeagleV Ahead board and QEMU does not connect this configuration state
 * to a modeled DMA master.
 */
static const TH1520IOPMPInfo th1520_iopmp_info[TH1520_IOPMP_COUNT] = {
    { "iopmp-emmc",       0xfffc028000ULL },
    { "iopmp-sdio0",      0xfffc029000ULL },
    { "iopmp-sdio1",      0xfffc02a000ULL },
    { "iopmp-usb0",       0xfffc02e000ULL },
    { "iopmp-ao",         0xffffc21000ULL },
    { "iopmp-aud",        0xffffc22000ULL },
    { "iopmp-chip-dbg",   0xffffc37000ULL },
    { "iopmp-eip120i",    0xffff220000ULL },
    { "iopmp-eip120ii",   0xffff230000ULL },
    { "iopmp-eip120iii",  0xffff240000ULL },
    { "iopmp-isp0",       0xfff4080000ULL },
    { "iopmp-isp1",       0xfff4081000ULL },
    { "iopmp-dw200",      0xfff4082000ULL },
    { "iopmp-vipre",      0xfff4083000ULL },
    { "iopmp-venc",       0xfffcc60000ULL },
    { "iopmp-vdec",       0xfffcc61000ULL },
    { "iopmp-g2d",        0xfffcc62000ULL },
    { "iopmp-fce",        0xfffcc63000ULL },
    { "iopmp-npu",        0xffff01c000ULL },
    { "iopmp-dpu0",       0xffff520000ULL },
    { "iopmp-dpu1",       0xffff521000ULL },
    { "iopmp-gpu",        0xffff522000ULL },
    { "iopmp-gmac1",      0xfffc001000ULL },
    { "iopmp-gmac2",      0xfffc002000ULL },
    { "iopmp-dmac",       0xffffc20000ULL },
    { "iopmp-tee-dmac",   0xffff250000ULL },
    { "iopmp-dsp0",       0xffff058000ULL },
    { "iopmp-dsp1",       0xffff059000ULL },
    { "iopmp-audio0",     0xffcb02e000ULL },
    { "iopmp-audio1",     0xffcb02f000ULL },
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

static void th1520_soc_reset_gmac(TH1520SoCState *s, unsigned int index)
{
    /* Whether an individual/shared reset also covers the PHY is unverified. */
    device_cold_reset(DEVICE(&s->gmac[index]));
    device_cold_reset(DEVICE(&s->gmac_apb[index]));
}

/*
 * Linux publishes reset IDs and bit groupings, but not the effect of each
 * member or the bus contract while reset remains asserted.  Model an
 * assertion as an immediate whole-device cold reset, leave MMIO accessible,
 * and record the provisional pulse/level/domain semantics under RST-001.
 */
static void th1520_soc_ap_reset(void *opaque, int n, int level)
{
    TH1520SoCState *s = opaque;

    if (!level) {
        return;
    }

    if (n >= TH1520_AP_RESET_UART0 && n <= TH1520_AP_RESET_UART5) {
        device_cold_reset(DEVICE(&s->uart[n - TH1520_AP_RESET_UART0]));
        return;
    }
    if (n >= TH1520_AP_RESET_I2C0 && n <= TH1520_AP_RESET_I2C5) {
        device_cold_reset(DEVICE(&s->i2c[n - TH1520_AP_RESET_I2C0]));
        return;
    }
    if (n >= TH1520_AP_RESET_GPIO0 && n <= TH1520_AP_RESET_GPIO3) {
        device_cold_reset(DEVICE(&s->gpio[n - TH1520_AP_RESET_GPIO0]));
        return;
    }

    switch (n) {
    case TH1520_AP_RESET_SPI0:
        device_cold_reset(DEVICE(&s->spi[0]));
        break;
    case TH1520_AP_RESET_PADCTRL0:
        device_cold_reset(DEVICE(&s->padctrl[2]));
        break;
    case TH1520_AP_RESET_PADCTRL1:
        device_cold_reset(DEVICE(&s->padctrl[1]));
        break;
    case TH1520_AP_RESET_DMAC0:
        device_cold_reset(DEVICE(&s->dmac0));
        break;
    case TH1520_AP_RESET_GMAC0:
        th1520_soc_reset_gmac(s, 0);
        break;
    case TH1520_AP_RESET_GMAC1:
        th1520_soc_reset_gmac(s, 1);
        break;
    case TH1520_AP_RESET_GMAC_SHARED:
        for (unsigned int i = 0; i < TH1520_GMAC_COUNT; i++) {
            th1520_soc_reset_gmac(s, i);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void th1520_soc_storage_reset(void *opaque, int n, int level)
{
    TH1520SoCState *s = opaque;

    if (level) {
        g_assert(n >= 0 && n < TH1520_MSHC_COUNT);
        device_cold_reset(DEVICE(&s->mshc[n]));
    }
}

static void th1520_soc_connect_ap_reset(TH1520SoCState *s,
                                         unsigned int output)
{
    qdev_connect_gpio_out_named(
        DEVICE(&s->ap_reset), "peripheral-reset", output,
        qdev_get_gpio_in_named(DEVICE(s), "ap-peripheral-reset", output));
}

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

    qdev_init_gpio_in_named(DEVICE(s), th1520_soc_ap_reset,
                            "ap-peripheral-reset",
                            TH1520_AP_RESET_OUTPUT_COUNT);
    qdev_init_gpio_in_named(DEVICE(s), th1520_soc_storage_reset,
                            "storage-reset",
                            TH1520_MISCSYS_STORAGE_RESET_COUNT);

    object_initialize_child(obj, "c910-cpus", &s->c910_cpus,
                            TYPE_RISCV_HART_ARRAY);
    object_initialize_child(obj, "clint", &s->clint,
                            TYPE_THEAD_C900_CLINT);
    object_initialize_child(obj, "pmp-portal", &s->pmp_portal,
                            TYPE_TH1520_PMP_PORTAL);
    object_initialize_child(obj, "plic", &s->plic,
                            TYPE_THEAD_C900_PLIC);
    object_initialize_child(obj, "ap-clock", &s->ap_clock,
                            TYPE_TH1520_AP_CLOCK);
    object_initialize_child(obj, "ddr-phy0", &s->ddr_phy[0],
                            TYPE_TH1520_DDR_PHY);
    object_initialize_child(obj, "ddr-phy1", &s->ddr_phy[1],
                            TYPE_TH1520_DDR_PHY);
    object_initialize_child(obj, "ddr-controller", &s->ddr_controller,
                            TYPE_TH1520_DDR_CONTROLLER);
    s->ddr_controller.phy[0] = &s->ddr_phy[0];
    s->ddr_controller.phy[1] = &s->ddr_phy[1];
    object_initialize_child(obj, "ddr-control", &s->ddr_control,
                            TYPE_TH1520_DDR_CONTROL);
    object_initialize_child(obj, "ddr-pll", &s->ddr_pll,
                            TYPE_TH1520_DDR_PLL);
    object_initialize_child(obj, "ap-reset", &s->ap_reset,
                            TYPE_TH1520_AP_RESET);
    object_initialize_child(obj, "aon-reset", &s->aon_reset,
                            TYPE_TH1520_AON_RESET);
    object_initialize_child(obj, "miscsys", &s->miscsys,
                            TYPE_TH1520_MISCSYS);
    object_initialize_child(obj, "tee-miscsys-clock", &s->tee_miscsys_clock,
                            TYPE_TH1520_TEE_MISCSYS_CLOCK);
    object_initialize_child(obj, "tee-dsp-reset", &s->tee_dsp_reset,
                            TYPE_TH1520_TEE_DSP_RESET);
    object_initialize_child(obj, "tee-vosys-dpu-reset", &s->tee_vosys_dpu_reset,
                            TYPE_TH1520_TEE_VOSYS_DPU_RESET);
    object_initialize_child(obj, "usb", &s->usb, TYPE_TH1520_USB);
    for (int i = 0; i < TH1520_UART_COUNT; i++) {
        object_initialize_child(obj, uart_names[i], &s->uart[i],
                                TYPE_DW_APB_UART);
        qdev_prop_set_uint32(DEVICE(&s->uart[i]), "baudbase",
                             TH1520_UART_INPUT_FREQ / 16);
        if (i == 0) {
            qdev_prop_set_bit(DEVICE(&s->uart[i]),
                              "legacy-ahead-vmstate", true);
        }
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
    object_initialize_child(obj, "aon-i2c", &s->aon_i2c,
                            TYPE_DESIGNWARE_I2C);
    th1520_aon_i2c_configure(DEVICE(&s->aon_i2c));
    for (int i = 0; i < TH1520_I2C_COUNT; i++) {
        object_initialize_child(obj, th1520_i2c_info[i].name, &s->i2c[i],
                                TYPE_DESIGNWARE_I2C);
        th1520_i2c_configure(DEVICE(&s->i2c[i]));
    }
    for (int i = 0; i < TH1520_SPI_COUNT; i++) {
        object_initialize_child(obj, th1520_spi_info[i].name, &s->spi[i],
                                TYPE_DW_APB_SSI);
    }
    object_initialize_child(obj, "pwm", &s->pwm, TYPE_TH1520_PWM);
    qdev_connect_clock_in(
        DEVICE(&s->pwm), "pwm",
        qdev_get_clock_out(DEVICE(&s->ap_clock),
                           TH1520_AP_CLOCK_PWM_OUTPUT));
    for (int i = 0; i < TH1520_TIMER_GROUP_COUNT; i++) {
        object_initialize_child(obj, th1520_timer_info[i].name,
                                &s->timer[i], TYPE_DW_APB_TIMER);
        qdev_prop_set_uint32(DEVICE(&s->timer[i]), "component-version",
                             TH1520_TIMER_COMPONENT_VERSION);
        qdev_connect_clock_in(
            DEVICE(&s->timer[i]), "timer",
            qdev_get_clock_out(
                DEVICE(&s->ap_clock),
                i ? TH1520_AP_CLOCK_TIMER1_OUTPUT :
                    TH1520_AP_CLOCK_TIMER0_OUTPUT));
    }
    for (int i = 0; i < TH1520_WDT_COUNT; i++) {
        DeviceState *wdt;

        object_initialize_child(obj, th1520_wdt_info[i].name, &s->wdt[i],
                                TYPE_DW_APB_WDT);
        wdt = DEVICE(&s->wdt[i]);
        qdev_prop_set_uint32(wdt, "component-param1",
                             TH1520_WDT_COMPONENT_PARAM_1);
        qdev_prop_set_uint32(wdt, "component-type",
                             DW_APB_WDT_COMP_TYPE_VALUE);
        qdev_prop_set_uint32(wdt, "counter-reset-value",
                             TH1520_WDT_COUNTER_RESET);
        qdev_connect_clock_in(
            wdt, "pclk",
            qdev_get_clock_out(
                DEVICE(&s->ap_clock),
                i ? TH1520_AP_CLOCK_WDT1_OUTPUT :
                    TH1520_AP_CLOCK_WDT0_OUTPUT));
    }
    object_initialize_child(obj, "mbox", &s->mbox, TYPE_TH1520_MBOX);
    for (int i = 0; i < TH1520_IOPMP_COUNT; i++) {
        object_initialize_child(obj, th1520_iopmp_info[i].name,
                                &s->iopmp[i], TYPE_TH1520_IOPMP);
    }
    object_initialize_child(obj, "visys", &s->visys,
                            TYPE_TH1520_VIDEO_SYSREG);
    object_initialize_child(obj, "vosys", &s->vosys,
                            TYPE_TH1520_VIDEO_SYSREG);
    qdev_prop_set_bit(DEVICE(&s->vosys), "vosys", true);
    object_initialize_child(obj, "iso7816-config", &s->iso7816_config,
                            TYPE_TH1520_ISO7816_CONFIG);
    object_initialize_child(obj, "bootsel", &s->bootsel, TYPE_TH1520_BOOTSEL);
    object_initialize_child(obj, "pvt", &s->pvt, TYPE_MR75203);
    qdev_prop_set_uint8(DEVICE(&s->pvt), "ts-count", TH1520_PVT_TS_COUNT);
    qdev_prop_set_uint8(DEVICE(&s->pvt), "pd-count", TH1520_PVT_PD_COUNT);
    qdev_prop_set_uint8(DEVICE(&s->pvt), "vm-count", TH1520_PVT_VM_COUNT);
    qdev_prop_set_uint8(DEVICE(&s->pvt), "vm-channels",
                        TH1520_PVT_VM_CHANNELS);
    qdev_prop_set_uint32(DEVICE(&s->pvt), "common-mmio-size",
                         th1520_memmap[TH1520_DEV_PVT_COMMON].size);
    qdev_prop_set_uint32(DEVICE(&s->pvt), "ts-mmio-size",
                         th1520_memmap[TH1520_DEV_PVT_TS].size);
    qdev_prop_set_uint32(DEVICE(&s->pvt), "pd-mmio-size",
                         th1520_memmap[TH1520_DEV_PVT_PD].size);
    qdev_prop_set_uint32(DEVICE(&s->pvt), "vm-mmio-size",
                         th1520_memmap[TH1520_DEV_PVT_VM].size);
    qdev_prop_set_uint32(DEVICE(&s->pvt), "component-id",
                         TH1520_PVT_COMPONENT_ID);
    qdev_prop_set_uint32(DEVICE(&s->pvt), "id-number",
                         TH1520_PVT_ID_NUMBER);
    qdev_prop_set_uint32(DEVICE(&s->pvt), "ts-coeff-g",
                         TH1520_PVT_TS_COEFF_G);
    qdev_prop_set_uint32(DEVICE(&s->pvt), "ts-coeff-h",
                         TH1520_PVT_TS_COEFF_H);
    qdev_prop_set_int32(DEVICE(&s->pvt), "ts-coeff-j",
                        TH1520_PVT_TS_COEFF_J);
    qdev_prop_set_uint32(DEVICE(&s->pvt), "ts-coeff-cal5",
                         TH1520_PVT_TS_COEFF_CAL5);
    s->pvt_clk = clock_new(obj, "pvt-clock");
    clock_set_hz(s->pvt_clk, TH1520_PVT_INPUT_FREQ);
    qdev_connect_clock_in(DEVICE(&s->pvt), "clock", s->pvt_clk);
    object_initialize_child(obj, "rtc", &s->rtc, TYPE_XGENE_RTC);
    s->rtc_clk = clock_new(obj, "rtc-clock");
    clock_set_hz(s->rtc_clk, TH1520_RTC_INPUT_FREQ);
    qdev_connect_clock_in(DEVICE(&s->rtc), "rtc", s->rtc_clk);
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
        qdev_connect_clock_in(DEVICE(&s->gmac[i]), "stmmaceth",
                              qdev_get_clock_out(
                                  DEVICE(&s->ap_clock),
                                  TH1520_AP_CLOCK_GMAC_AXI_OUTPUT));
        qdev_prop_set_uint64(DEVICE(&s->gmac[i]), "riwt-clock-frequency",
                             TH1520_GMAC_RIWT_CLOCK_HZ);
        qdev_prop_set_bit(DEVICE(&s->gmac[i]), "rx-filtering", true);
        qdev_prop_set_bit(DEVICE(&s->gmac[i]), "rx-coe-type2", true);
        qdev_prop_set_uint16(DEVICE(&s->gmac[i]), "hash-bins",
                             TH1520_GMAC_HASH_BINS);
        qdev_prop_set_uint8(DEVICE(&s->gmac[i]), "num-mac-addresses",
                            TH1520_GMAC_MAC_ADDRS);
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
        qdev_connect_clock_in(DEVICE(&s->mshc[i]), "core",
                              qdev_get_clock_out(
                                  DEVICE(&s->ap_clock),
                                  TH1520_AP_CLOCK_EMMC_SDIO_OUTPUT));
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
    qdev_prop_set_bit(DEVICE(&s->clint), "legacy-ahead-vmstate", true);
    qdev_prop_set_uint32(DEVICE(&s->plic), "hartid-base", 0);
    qdev_prop_set_uint32(DEVICE(&s->plic), "num-harts",
                         TH1520_C910_HARTS);
    qdev_prop_set_uint32(DEVICE(&s->plic), "num-sources",
                         TH1520_PLIC_NUM_SOURCES);
    qdev_prop_set_bit(DEVICE(&s->plic), "legacy-ahead-vmstate", true);
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
    memory_region_init_alias(&s->ap_clock_vendor_alias, OBJECT(dev),
                             "th1520.ap-clock-vendor-alias",
                             sysbus_mmio_get_region(
                                 SYS_BUS_DEVICE(&s->ap_clock), 0),
                             0, TH1520_AP_CLOCK_MMIO_SIZE);
    memory_region_add_subregion(system_memory,
                                TH1520_VENDOR_UBOOT_AP_CLOCK_BASE,
                                &s->ap_clock_vendor_alias);

    for (int i = 0; i < ARRAY_SIZE(s->ddr_phy); i++) {
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->ddr_phy[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddr_phy[i]), 0,
                        th1520_memmap[TH1520_DEV_DDR_PHY0 + i].base);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ddr_controller), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddr_controller), 0,
                    th1520_memmap[TH1520_DEV_DDR_CONTROLLER].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ddr_control), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddr_control), 0,
                    th1520_memmap[TH1520_DEV_DDR_CFG0].base);

    /*
     * Only generated DDR SYSREG fields used by the current public SPL are
     * mapped.  They deliberately have no DT node or physical DDR-clock,
     * reset, rail or training side effect.
     */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ddr_pll), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddr_pll), 0,
                    th1520_memmap[TH1520_DEV_DDR_PLL_CFG0].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddr_pll), 1,
                    th1520_memmap[TH1520_DEV_DDR_PLL_CFG1].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddr_pll), 2,
                    th1520_memmap[TH1520_DEV_DDR_PLL_STS].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ap_reset), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ap_reset), 0,
                    th1520_memmap[TH1520_DEV_AP_RESET].base);
    memory_region_init_alias(&s->ap_reset_vendor_npu_alias, OBJECT(dev),
                             "th1520.ap-reset-vendor-npu-alias",
                             sysbus_mmio_get_region(
                                 SYS_BUS_DEVICE(&s->ap_reset), 0),
                             TH1520_VENDOR_UBOOT_AP_RESET_NPU_OFFSET,
                             TH1520_VENDOR_UBOOT_AP_RESET_NPU_SIZE);
    memory_region_add_subregion(system_memory,
                                TH1520_VENDOR_UBOOT_AP_RESET_NPU_BASE,
                                &s->ap_reset_vendor_npu_alias);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->aon_reset), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->aon_reset), 0,
                    th1520_memmap[TH1520_DEV_AON_AUDIO_RESET].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->miscsys), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->miscsys), 0,
                    th1520_memmap[TH1520_DEV_MISCSYS].base);
    memory_region_init_alias(&s->tee_miscsys_usb_clock_alias, OBJECT(dev),
                             "th1520.tee-miscsys-usb-clock-alias",
                             sysbus_mmio_get_region(
                                 SYS_BUS_DEVICE(&s->miscsys), 0),
                             TH1520_MISCSYS_USB_CLOCK_CTRL,
                             sizeof(uint32_t));
    memory_region_add_subregion(system_memory,
                                TH1520_VENDOR_UBOOT_USB_CLOCK_BASE,
                                &s->tee_miscsys_usb_clock_alias);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tee_miscsys_clock), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->tee_miscsys_clock), 0,
                    th1520_memmap[TH1520_DEV_TEE_MISCSYS_CLOCK].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tee_dsp_reset), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->tee_dsp_reset), 0,
                    th1520_memmap[TH1520_DEV_TEE_DSP_RESET].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tee_vosys_dpu_reset), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->tee_vosys_dpu_reset), 0,
                    th1520_memmap[TH1520_DEV_TEE_VOSYS_DPU_RESET].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->visys), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->visys), 0,
                    th1520_memmap[TH1520_DEV_VISYS].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->vosys), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->vosys), 0,
                    th1520_memmap[TH1520_DEV_VOSYS].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->iso7816_config), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->iso7816_config), 0,
                    th1520_memmap[TH1520_DEV_ISO7816_CONFIG].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->bootsel), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->bootsel), 0,
                    th1520_memmap[TH1520_DEV_BOOTSEL].base);

    object_property_set_link(OBJECT(&s->usb), "dma", OBJECT(system_memory),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->usb), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb), 0,
                    th1520_memmap[TH1520_DEV_USB_DRD].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb), 1,
                    th1520_memmap[TH1520_DEV_USB_CORE].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                              TH1520_USB_IRQ));
    for (int i = 0; i < TH1520_MISCSYS_USB_RESET_COUNT; i++) {
        qdev_connect_gpio_out_named(
            DEVICE(&s->miscsys), "usb-reset", i,
            qdev_get_gpio_in_named(DEVICE(&s->usb), "reset", i));
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
     * The public DTS describes a 4 KiB PMP portal, but public SPL source
     * touches only five words.  Do not map the unknown remainder or connect
     * this firmware state to CPU PMP/security enforcement.
     */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pmp_portal), errp)) {
        return;
    }
    for (int i = 0; i < TH1520_PMP_PORTAL_REG_COUNT; i++) {
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->pmp_portal), i,
                        th1520_memmap[TH1520_DEV_PMP_PORTAL].base +
                        th1520_pmp_portal_offsets[i]);
    }

    for (int i = 0; i < TH1520_UART_COUNT; i++) {
        SysBusDevice *uart = SYS_BUS_DEVICE(&s->uart[i]);
        Chardev *chr = serial_hd(i);

        /*
         * The DW register model occupies 0x100 bytes.  Linux describes a
         * 0x4000-byte silicon aperture for UART2, UART4 and UART5, but the
         * behavior of the reserved portion is not established (UART-001).
         * Leave it unmapped until it can be checked on physical hardware.
         */
        if (i == 4 && !chr && !qtest_enabled()) {
            chr = qemu_chardev_new("ap6203bm-hci", "chardev-ap6203bm-hci",
                                   NULL, NULL, errp);
            if (!chr) {
                return;
            }
        }
        qdev_prop_set_chr(DEVICE(uart), "chardev", chr);
        if (!sysbus_realize(uart, errp)) {
            return;
        }
        sysbus_mmio_map(uart, 0,
                        th1520_memmap[th1520_uart_memmap[i]].base);
        sysbus_connect_irq(uart, 0,
                           qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                                  th1520_uart_irqs[i]));
        th1520_soc_connect_ap_reset(s, TH1520_AP_RESET_UART0 + i);
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
        if (i < 4) {
            th1520_soc_connect_ap_reset(s, TH1520_AP_RESET_GPIO0 + i);
        }
    }

    /*
     * The upstream BeagleV Ahead DTS routes the RTL8211F's active-low reset
     * and interrupt pins to GPIO3_21 and GPIO3_22.  The generic PHY leaves
     * the interrupt deasserted until PHY interrupt sources are modeled.
     */
    qdev_connect_gpio_out_named(
        DEVICE(&s->gpio[3]), "gpio-out", TH1520_GMAC_PHY_RESET_GPIO,
        qdev_get_gpio_in_named(DEVICE(&s->gmac[0]), "phy-reset-n", 0));
    qdev_connect_gpio_out_named(
        DEVICE(&s->gmac[0]), "phy-irq-n", 0,
        qdev_get_gpio_in_named(DEVICE(&s->gpio[3]), "gpio-in",
                               TH1520_GMAC_PHY_IRQ_GPIO));

    for (int i = 0; i < TH1520_PADCTRL_COUNT; i++) {
        const TH1520PadCtrlInfo *info = &th1520_padctrl_info[i];
        SysBusDevice *padctrl = SYS_BUS_DEVICE(&s->padctrl[i]);

        if (!sysbus_realize(padctrl, errp)) {
            return;
        }
        sysbus_mmio_map(padctrl, 0, th1520_memmap[info->memmap].base);
        if (i == 1) {
            th1520_soc_connect_ap_reset(s, TH1520_AP_RESET_PADCTRL1);
        } else if (i == 2) {
            th1520_soc_connect_ap_reset(s, TH1520_AP_RESET_PADCTRL0);
        }
    }

    /*
     * Vendor SPL uses this AON controller for PMIC setup before Linux.  It is
     * intentionally not part of the AP I2C list: no generated DT node or AP
     * clock/reset connection is implied.
     */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->aon_i2c), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->aon_i2c), 0,
                    th1520_memmap[TH1520_DEV_AON_I2C].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->aon_i2c), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                              TH1520_AON_I2C_IRQ));

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
        th1520_soc_connect_ap_reset(s, TH1520_AP_RESET_I2C0 + i);
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
        th1520_soc_connect_ap_reset(s, TH1520_AP_RESET_SPI0 + i);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pwm), errp)) {
        return;
    }
    /* The DTS aperture is 0x4000 bytes; the Linux driver uses only 0xb0. */
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pwm), 0,
                    th1520_memmap[TH1520_DEV_PWM].base);

    /*
     * reset-th1520.c maps these APB/core pairs to the modeled blocks.  An
     * assertion resets the QEMU device; exact pulse, hold and retention
     * semantics remain hardware-validation work.
     */
    qdev_connect_gpio_out_named(DEVICE(&s->ap_reset), "peripheral-reset",
                                TH1520_AP_RESET_PWM,
                                qdev_get_gpio_in_named(DEVICE(&s->pwm),
                                                       "reset", 0));

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
        qdev_connect_gpio_out_named(
            DEVICE(&s->ap_reset), "peripheral-reset",
            TH1520_AP_RESET_TIMER0_3 + i,
            qdev_get_gpio_in_named(DEVICE(timer), "reset", 0));
    }

    for (int i = 0; i < TH1520_WDT_COUNT; i++) {
        const TH1520WDTInfo *info = &th1520_wdt_info[i];
        SysBusDevice *wdt = SYS_BUS_DEVICE(&s->wdt[i]);

        if (!sysbus_realize(wdt, errp)) {
            return;
        }
        sysbus_mmio_map(wdt, 0, th1520_memmap[info->memmap].base);
        sysbus_connect_irq(wdt, 0,
                           qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                                  info->irq));
        qdev_connect_gpio_out_named(
            DEVICE(&s->ap_reset), "peripheral-reset", info->reset_output,
            qdev_get_gpio_in_named(DEVICE(wdt), "reset", 0));
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mbox), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mbox), 0,
                    th1520_memmap[TH1520_DEV_MBOX_LOCAL].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mbox), 1,
                    th1520_memmap[TH1520_DEV_MBOX_REMOTE0].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mbox), 2,
                    th1520_memmap[TH1520_DEV_MBOX_REMOTE1].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mbox), 3,
                    th1520_memmap[TH1520_DEV_MBOX_REMOTE2].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mbox), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                              TH1520_MBOX_IRQ));

    for (int i = 0; i < TH1520_IOPMP_COUNT; i++) {
        SysBusDevice *iopmp = SYS_BUS_DEVICE(&s->iopmp[i]);

        if (!sysbus_realize(iopmp, errp)) {
            return;
        }
        sysbus_mmio_map(iopmp, 0, th1520_iopmp_info[i].base);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pvt), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pvt), MR75203_WINDOW_COMMON,
                    th1520_memmap[TH1520_DEV_PVT_COMMON].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pvt), MR75203_WINDOW_TS,
                    th1520_memmap[TH1520_DEV_PVT_TS].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pvt), MR75203_WINDOW_PD,
                    th1520_memmap[TH1520_DEV_PVT_PD].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pvt), MR75203_WINDOW_VM,
                    th1520_memmap[TH1520_DEV_PVT_VM].base);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rtc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rtc), 0,
                    th1520_memmap[TH1520_DEV_RTC].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                              TH1520_RTC_IRQ));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dmac0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dmac0), 0,
                    th1520_memmap[TH1520_DEV_DMAC0].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dmac0), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->plic), "source",
                                              TH1520_DMAC0_IRQ));
    th1520_soc_connect_ap_reset(s, TH1520_AP_RESET_DMAC0);

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
        th1520_soc_connect_ap_reset(s, TH1520_AP_RESET_GMAC0 + i);
    }
    th1520_soc_connect_ap_reset(s, TH1520_AP_RESET_GMAC_SHARED);

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
        qdev_connect_gpio_out_named(
            DEVICE(&s->miscsys), "storage-reset", i,
            qdev_get_gpio_in_named(DEVICE(s), "storage-reset", i));
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

static void th1520_fdt_add_fixed_clock(void *fdt, const char *path,
                                       const char *output_name, uint32_t hz,
                                       uint32_t phandle)
{
    qemu_fdt_add_subnode(fdt, path);
    qemu_fdt_setprop_string(fdt, path, "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(fdt, path, "#clock-cells", 0);
    qemu_fdt_setprop_cell(fdt, path, "clock-frequency", hz);
    qemu_fdt_setprop_string(fdt, path, "clock-output-names", output_name);
    qemu_fdt_setprop_cell(fdt, path, "phandle", phandle);
}

static void th1520_create_pinctrl_fdt(
    void *fdt, bool vendor, uint32_t ap_clock_phandle,
    uint32_t aonsys_clock_phandle,
    uint32_t *phandle, uint32_t padctrl_phandles[TH1520_PADCTRL_COUNT],
    uint32_t *led_phandle, uint32_t *gmac0_phandle,
    uint32_t *uart0_phandle, uint32_t *wifi_phandle)
{
    for (int i = 0; i < TH1520_PADCTRL_COUNT; i++) {
        const TH1520PadCtrlInfo *info = &th1520_padctrl_info[i];
        const MemMapEntry *map = &th1520_memmap[info->memmap];
        g_autofree char *name =
            g_strdup_printf("/soc/pinctrl@%" HWADDR_PRIx, map->base);
        g_autofree char *compatible = g_strdup("thead,th1520-pinctrl");
        g_autofree char *vendor_compatible =
            g_strdup_printf("xuantie,th1520-group%u-pinctrl", info->group);
        const char *const compatibles[] = {
            compatible,
            vendor_compatible,
        };

        padctrl_phandles[i] = (*phandle)++;
        qemu_fdt_add_subnode(fdt, name);
        /*
         * The mainline binding documents exactly one compatible,
         * "thead,th1520-pinctrl", with the pad group carried by
         * "thead,pad-group".  RevyOS' driver instead matches only its older
         * group-specific "xuantie" spelling.  Emit both so either kernel binds
         * the same modeled register block and GPIO ranges; a pad controller
         * that fails to bind takes every gpiochip and the GMAC0 PHY with it.
         */
        qemu_fdt_setprop_string_array(fdt, name, "compatible",
                                      (char **)compatibles,
                                      ARRAY_SIZE(compatibles));
        qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, map->base,
                                     2, map->size);
        if (info->clock_id < 0) {
            qemu_fdt_setprop_cell(fdt, name, "clocks",
                                  aonsys_clock_phandle);
        } else if (vendor) {
            qemu_fdt_setprop_cells(fdt, name, "clocks",
                                   ap_clock_phandle, info->vendor_clock_id);
            qemu_fdt_setprop_string(fdt, name, "clock-names", "pclk");
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
    static const char ap_clock_node[] = "/soc/clock-controller@ffef010000";
    static const char *const vendor_ap_clock_parents[] = {
        "osc_32k", "osc_24m", "rc_24m"
    };
    static const char *const vendor_gpio_clock_names[] = { "bus", "db" };
    static const char *const vendor_spi_clock_names[] = { "sclk", "pclk" };
    static const char *const vendor_gmac_clock_names[] = {
        "stmmaceth", "pclk", "axi_aclk", "axi_pclk"
    };
    const bool vendor = s->clock_abi == TH1520_CLOCK_ABI_VENDOR;
    static const char *const i2c_compat[] = {
        "thead,th1520-i2c", "snps,designware-i2c"
    };
    static const char *const mshc_compat[] = {
        /* The vendor kernel retained the older XuanTie binding spelling. */
        "xuantie,th1520-dwcmshc", "thead,th1520-dwcmshc"
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
    static const char *const mbox_reg_names[] = {
        "local", "remote-icu0", "remote-icu1", "remote-icu2"
    };
    static const char *const mbox_clock_names[] = {
        "clk-local", "clk-remote-icu0", "clk-remote-icu1",
        "clk-remote-icu2"
    };
    static const char *const pvt_reg_names[] = {
        "common", "ts", "pd", "vm"
    };
    static const char *const misc_sysreg_compat[] = {
        "thead,light-misc-sysreg", "syscon"
    };
    static const char *const usb_drd_compat[] = {
        "thead,light-usb3-drd", "syscon"
    };
    MachineState *ms = MACHINE(s);
    uint32_t intc_phandles[TH1520_C910_HARTS];
    uint32_t plic_cells[TH1520_C910_HARTS * 4];
    uint32_t phandle = 1;
    uint32_t l2_phandle;
    uint32_t plic_phandle;
    uint32_t osc_phandle;
    uint32_t aonsys_clock_phandle;
    uint32_t rtc_clock_phandle;
    uint32_t osc_32k_phandle = 0;
    uint32_t rc_24m_phandle = 0;
    uint32_t apb_clock_phandle = 0;
    uint32_t uart_sclk_phandle = 0;
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
    th1520_fdt_add_fixed_clock(ms->fdt, "/oscillator", "osc_24m",
                               TH1520_OSC_FREQ, osc_phandle);

    aonsys_clock_phandle = phandle++;
    th1520_fdt_add_fixed_clock(ms->fdt, "/clock-73728000", "aonsys_clk",
                               73728000, aonsys_clock_phandle);

    rtc_clock_phandle = phandle++;
    th1520_fdt_add_fixed_clock(ms->fdt, "/clock-32768", "rtc_clk",
                               TH1520_RTC_INPUT_FREQ, rtc_clock_phandle);

    if (vendor) {
        /*
         * The vendor tree feeds the AP clock controller from three root
         * oscillators and binds the UARTs, timers and mailbox to root
         * fixed clocks instead of provider IDs; the rates are the ones the
         * vendor th1520-beaglev-ahead.dts assigns.
         */
        osc_32k_phandle = phandle++;
        th1520_fdt_add_fixed_clock(ms->fdt, "/32k-oscillator", "osc_32k",
                                   TH1520_RTC_INPUT_FREQ, osc_32k_phandle);
        rc_24m_phandle = phandle++;
        th1520_fdt_add_fixed_clock(ms->fdt, "/clock-rc-24m", "rc_24m",
                                   TH1520_VENDOR_RC_24M_FREQ, rc_24m_phandle);
        apb_clock_phandle = phandle++;
        th1520_fdt_add_fixed_clock(ms->fdt, "/apb-clk-clock", "apb_clk",
                                   TH1520_VENDOR_APB_CLK_FREQ,
                                   apb_clock_phandle);
        uart_sclk_phandle = phandle++;
        th1520_fdt_add_fixed_clock(ms->fdt, "/uart-sclk-clock", "uart_sclk",
                                   TH1520_VENDOR_UART_SCLK_FREQ,
                                   uart_sclk_phandle);
    }

    ap_clock_phandle = phandle++;
    qemu_fdt_add_subnode(ms->fdt, ap_clock_node);
    qemu_fdt_setprop_string(ms->fdt, ap_clock_node, "compatible",
                            vendor ? "xuantie,th1520-fm-ree-clk"
                                   : "thead,th1520-clk-ap");
    qemu_fdt_setprop_sized_cells(ms->fdt, ap_clock_node, "reg",
                                 2, th1520_memmap[TH1520_DEV_AP_CLOCK].base,
                                 2, th1520_memmap[TH1520_DEV_AP_CLOCK].size);
    if (vendor) {
        qemu_fdt_setprop_cells(ms->fdt, ap_clock_node, "clocks",
                               osc_32k_phandle, osc_phandle, rc_24m_phandle);
        qemu_fdt_setprop_string_array(ms->fdt, ap_clock_node, "clock-names",
                                      (char **)&vendor_ap_clock_parents,
                                      ARRAY_SIZE(vendor_ap_clock_parents));
    } else {
        qemu_fdt_setprop_cell(ms->fdt, ap_clock_node, "clocks", osc_phandle);
    }
    qemu_fdt_setprop_cell(ms->fdt, ap_clock_node, "#clock-cells", 1);
    qemu_fdt_setprop_cell(ms->fdt, ap_clock_node, "phandle",
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

    th1520_create_pinctrl_fdt(ms->fdt, vendor, ap_clock_phandle,
                              aonsys_clock_phandle, &phandle,
                              padctrl_phandles, &led_pins_phandle,
                              &gmac0_pins_phandle, &uart0_pins_phandle,
                              &wifi_pins_phandle);

    qemu_fdt_add_subnode(ms->fdt, "/soc/pvt@fffff4e000");
    qemu_fdt_setprop_string(ms->fdt, "/soc/pvt@fffff4e000", "compatible",
                            "moortec,mr75203");
    qemu_fdt_setprop_sized_cells(
        ms->fdt, "/soc/pvt@fffff4e000", "reg",
        2, th1520_memmap[TH1520_DEV_PVT_COMMON].base,
        2, th1520_memmap[TH1520_DEV_PVT_COMMON].size,
        2, th1520_memmap[TH1520_DEV_PVT_TS].base,
        2, th1520_memmap[TH1520_DEV_PVT_TS].size,
        2, th1520_memmap[TH1520_DEV_PVT_PD].base,
        2, th1520_memmap[TH1520_DEV_PVT_PD].size,
        2, th1520_memmap[TH1520_DEV_PVT_VM].base,
        2, th1520_memmap[TH1520_DEV_PVT_VM].size);
    qemu_fdt_setprop_string_array(ms->fdt, "/soc/pvt@fffff4e000",
                                  "reg-names", (char **)&pvt_reg_names,
                                  ARRAY_SIZE(pvt_reg_names));
    qemu_fdt_setprop_cell(ms->fdt, "/soc/pvt@fffff4e000", "clocks",
                          aonsys_clock_phandle);
    qemu_fdt_setprop_cell(ms->fdt, "/soc/pvt@fffff4e000",
                          "#thermal-sensor-cells", 1);
    qemu_fdt_setprop_cell(ms->fdt, "/soc/pvt@fffff4e000",
                          "moortec,ts-coeff-g", TH1520_PVT_TS_COEFF_G);
    qemu_fdt_setprop_cell(ms->fdt, "/soc/pvt@fffff4e000",
                          "moortec,ts-coeff-h", TH1520_PVT_TS_COEFF_H);
    qemu_fdt_setprop_cell(ms->fdt, "/soc/pvt@fffff4e000",
                          "moortec,ts-coeff-j", TH1520_PVT_TS_COEFF_J);
    qemu_fdt_setprop_cell(ms->fdt, "/soc/pvt@fffff4e000",
                          "moortec,ts-coeff-cal5",
                          TH1520_PVT_TS_COEFF_CAL5);

    /*
     * The public vendor tree establishes this X-Gene-compatible block and
     * its 32.768 kHz prescaler extension.  Mainline's generic X-Gene driver
     * does not program that extension, so leave the node disabled until a
     * TH1520-specific compatible/driver contract is upstream.
     */
    qemu_fdt_add_subnode(ms->fdt, "/soc/rtc@fffff40000");
    qemu_fdt_setprop_string(ms->fdt, "/soc/rtc@fffff40000", "compatible",
                            "apm,xgene-rtc");
    qemu_fdt_setprop_sized_cells(
        ms->fdt, "/soc/rtc@fffff40000", "reg",
        2, th1520_memmap[TH1520_DEV_RTC].base,
        2, th1520_memmap[TH1520_DEV_RTC].size);
    qemu_fdt_setprop_cells(ms->fdt, "/soc/rtc@fffff40000", "interrupts",
                           TH1520_RTC_IRQ, 4);
    qemu_fdt_setprop_cell(ms->fdt, "/soc/rtc@fffff40000", "clocks",
                          rtc_clock_phandle);
    qemu_fdt_setprop_string(ms->fdt, "/soc/rtc@fffff40000", "clock-names",
                            "rtc");
    qemu_fdt_setprop(ms->fdt, "/soc/rtc@fffff40000", "wakeup-source",
                     NULL, 0);
    qemu_fdt_setprop_string(ms->fdt, "/soc/rtc@fffff40000", "status",
                            "disabled");

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
        if (info->clock_id >= 0 && vendor) {
            qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                                   ap_clock_phandle, info->vendor_pclk_id,
                                   ap_clock_phandle, info->vendor_dbclk_id);
            qemu_fdt_setprop_string_array(ms->fdt, name, "clock-names",
                                          (char **)&vendor_gpio_clock_names,
                                          ARRAY_SIZE(vendor_gpio_clock_names));
        } else if (info->clock_id >= 0) {
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
        if (vendor) {
            qemu_fdt_setprop_cell(ms->fdt, name, "clocks",
                                  uart_sclk_phandle);
        } else {
            qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                                   ap_clock_phandle, TH1520_CLK_UART_SCLK,
                                   ap_clock_phandle,
                                   th1520_uart_pclk_ids[i]);
            qemu_fdt_setprop_string_array(ms->fdt, name, "clock-names",
                                          (char **)&uart_clock_names,
                                          ARRAY_SIZE(uart_clock_names));
        }
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
        if (vendor) {
            qemu_fdt_setprop_cells(ms->fdt, name, "clocks", ap_clock_phandle,
                                   info->vendor_clock_id);
            qemu_fdt_setprop_string(ms->fdt, name, "clock-names", "pclk");
        } else {
            qemu_fdt_setprop_cells(ms->fdt, name, "clocks", ap_clock_phandle,
                                   info->clock_id);
        }
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
        if (vendor) {
            qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                                   ap_clock_phandle, info->vendor_sclk_id,
                                   ap_clock_phandle, info->vendor_pclk_id);
            qemu_fdt_setprop_string_array(ms->fdt, name, "clock-names",
                                          (char **)&vendor_spi_clock_names,
                                          ARRAY_SIZE(vendor_spi_clock_names));
        } else {
            qemu_fdt_setprop_cells(ms->fdt, name, "clocks", ap_clock_phandle,
                                   info->clock_id);
        }
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
    if (vendor) {
        qemu_fdt_setprop_cell(ms->fdt, "/soc/pwm@ffec01c000", "clocks",
                              osc_phandle);
    } else {
        qemu_fdt_setprop_cells(ms->fdt, "/soc/pwm@ffec01c000", "clocks",
                               ap_clock_phandle, TH1520_PWM_CLOCK_ID);
    }
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
            if (vendor) {
                qemu_fdt_setprop_cell(ms->fdt, name, "clocks",
                                      apb_clock_phandle);
            } else {
                qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                                       ap_clock_phandle,
                                       TH1520_CLK_PERI_APB_PCLK);
            }
            qemu_fdt_setprop_string(ms->fdt, name, "clock-names", "timer");
            qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                                   info->first_irq + channel, 4);
            qemu_fdt_setprop_string(ms->fdt, name, "status", "disabled");
        }
    }

    /*
     * These resources agree across the public vendor tree and the Linux
     * reset-controller RFC, but no watchdog nodes have reached the current
     * mainline TH1520 DTS.  Describe the modeled hardware without enabling
     * a board policy that Linux upstream has not yet established.
     */
    for (int i = 0; i < TH1520_WDT_COUNT; i++) {
        const TH1520WDTInfo *info = &th1520_wdt_info[i];
        const MemMapEntry *map = &th1520_memmap[info->memmap];
        g_autofree char *name =
            g_strdup_printf("/soc/watchdog@%" HWADDR_PRIx, map->base);

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible",
                                "snps,dw-wdt");
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                     2, map->base, 2, map->size);
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts", info->irq, 4);
        qemu_fdt_setprop_cells(ms->fdt, name, "clocks", ap_clock_phandle,
                               vendor ? info->vendor_clock_id
                                      : info->clock_id);
        qemu_fdt_setprop_string(ms->fdt, name, "clock-names", "tclk");
        qemu_fdt_setprop_cells(ms->fdt, name, "resets", ap_reset_phandle,
                               info->reset_id);
        qemu_fdt_setprop_string(ms->fdt, name, "status", "disabled");
    }

    qemu_fdt_add_subnode(ms->fdt, "/soc/syscon@ffec02c000");
    qemu_fdt_setprop_string_array(ms->fdt, "/soc/syscon@ffec02c000",
                                  "compatible", (char **)&misc_sysreg_compat,
                                  ARRAY_SIZE(misc_sysreg_compat));
    qemu_fdt_setprop_sized_cells(
        ms->fdt, "/soc/syscon@ffec02c000", "reg",
        2, th1520_memmap[TH1520_DEV_MISCSYS].base,
        2, th1520_memmap[TH1520_DEV_MISCSYS].size);

    qemu_fdt_add_subnode(ms->fdt, "/soc/syscon@ffec03f000");
    qemu_fdt_setprop_string_array(ms->fdt, "/soc/syscon@ffec03f000",
                                  "compatible", (char **)&usb_drd_compat,
                                  ARRAY_SIZE(usb_drd_compat));
    qemu_fdt_setprop_sized_cells(
        ms->fdt, "/soc/syscon@ffec03f000", "reg",
        2, th1520_memmap[TH1520_DEV_USB_DRD].base,
        2, th1520_memmap[TH1520_DEV_USB_DRD].size);

    /*
     * Mainline Linux has neither a TH1520 glue binding nor a driver.  Keep
     * the accurately located generic core disabled until that parent contract
     * exists instead of silently bypassing the required clock/reset sequence.
     */
    qemu_fdt_add_subnode(ms->fdt, "/soc/usb@ffe7040000");
    qemu_fdt_setprop_string(ms->fdt, "/soc/usb@ffe7040000", "compatible",
                            "snps,dwc3");
    qemu_fdt_setprop_sized_cells(
        ms->fdt, "/soc/usb@ffe7040000", "reg",
        2, th1520_memmap[TH1520_DEV_USB_CORE].base,
        2, th1520_memmap[TH1520_DEV_USB_CORE].size);
    qemu_fdt_setprop_cells(ms->fdt, "/soc/usb@ffe7040000", "interrupts",
                           TH1520_USB_IRQ, 4);
    qemu_fdt_setprop_string(ms->fdt, "/soc/usb@ffe7040000",
                            "interrupt-names", "dwc_usb3");
    qemu_fdt_setprop_string(ms->fdt, "/soc/usb@ffe7040000",
                            "maximum-speed", "super-speed");
    qemu_fdt_setprop_string(ms->fdt, "/soc/usb@ffe7040000", "dr_mode",
                            "host");
    qemu_fdt_setprop(ms->fdt, "/soc/usb@ffe7040000",
                     "snps,usb3_lpm_capable", NULL, 0);
    qemu_fdt_setprop_string(ms->fdt, "/soc/usb@ffe7040000", "status",
                            "disabled");

    qemu_fdt_add_subnode(ms->fdt, "/soc/mailbox@ffffc38000");
    qemu_fdt_setprop_string(ms->fdt, "/soc/mailbox@ffffc38000",
                            "compatible", "thead,th1520-mbox");
    qemu_fdt_setprop_sized_cells(
        ms->fdt, "/soc/mailbox@ffffc38000", "reg",
        2, th1520_memmap[TH1520_DEV_MBOX_LOCAL].base,
        2, th1520_memmap[TH1520_DEV_MBOX_LOCAL].size,
        2, th1520_memmap[TH1520_DEV_MBOX_REMOTE0].base,
        2, th1520_memmap[TH1520_DEV_MBOX_REMOTE0].size,
        2, th1520_memmap[TH1520_DEV_MBOX_REMOTE1].base,
        2, th1520_memmap[TH1520_DEV_MBOX_REMOTE1].size,
        2, th1520_memmap[TH1520_DEV_MBOX_REMOTE2].base,
        2, th1520_memmap[TH1520_DEV_MBOX_REMOTE2].size);
    qemu_fdt_setprop_string_array(ms->fdt, "/soc/mailbox@ffffc38000",
                                  "reg-names", (char **)&mbox_reg_names,
                                  ARRAY_SIZE(mbox_reg_names));
    if (vendor) {
        qemu_fdt_setprop_cell(ms->fdt, "/soc/mailbox@ffffc38000", "clocks",
                              apb_clock_phandle);
        qemu_fdt_setprop_string(ms->fdt, "/soc/mailbox@ffffc38000",
                                "clock-names", "ipg");
    } else {
        qemu_fdt_setprop_cells(ms->fdt, "/soc/mailbox@ffffc38000", "clocks",
                               ap_clock_phandle, TH1520_CLK_MBOX0,
                               ap_clock_phandle, TH1520_CLK_MBOX1,
                               ap_clock_phandle, TH1520_CLK_MBOX2,
                               ap_clock_phandle, TH1520_CLK_MBOX3);
        qemu_fdt_setprop_string_array(ms->fdt, "/soc/mailbox@ffffc38000",
                                      "clock-names",
                                      (char **)&mbox_clock_names,
                                      ARRAY_SIZE(mbox_clock_names));
    }
    qemu_fdt_setprop_cells(ms->fdt, "/soc/mailbox@ffffc38000",
                           "interrupts", TH1520_MBOX_IRQ, 4);
    qemu_fdt_setprop_cell(ms->fdt, "/soc/mailbox@ffffc38000",
                          "#mbox-cells", 1);

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
    if (vendor) {
        qemu_fdt_setprop_cells(ms->fdt, dmac_name, "clocks",
                               ap_clock_phandle, TH1520_VCLK_DMAC_CPUSYS_ACLK,
                               ap_clock_phandle,
                               TH1520_VCLK_DMAC_CPUSYS_HCLK);
    } else {
        qemu_fdt_setprop_cells(ms->fdt, dmac_name, "clocks",
                               ap_clock_phandle, TH1520_CLK_PERI_APB_PCLK,
                               ap_clock_phandle, TH1520_CLK_PERI_APB_PCLK);
    }
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
        char alias[8];

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string_array(ms->fdt, name, "compatible",
                                      (char **)&mshc_compat,
                                      ARRAY_SIZE(mshc_compat));
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg", 2, map->base,
                                     2, map->size);
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                               th1520_mshc_irqs[i], 4);
        /*
         * Both kernels' dwcmshc drivers take a mandatory "core" clock: the
         * CLK_EMMC_SDIO gate upstream, CLKGEN_EMMC_SDIO_REF_CLK (the same
         * 0x204 bit 30) in the vendor namespace.  The vendor "bus" clock is
         * optional and lives on a MISCSYS gate provider QEMU does not
         * describe.
         */
        qemu_fdt_setprop_cells(ms->fdt, name, "clocks", ap_clock_phandle,
                               vendor ? TH1520_VCLK_EMMC_SDIO_REF_CLK
                                      : TH1520_CLK_EMMC_SDIO);
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

        snprintf(alias, sizeof(alias), "mmc%d", i);
        qemu_fdt_setprop_string(ms->fdt, "/aliases", alias, name);
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
        if (vendor) {
            qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                                   ap_clock_phandle,
                                   i ? TH1520_VCLK_GMAC1_CCLK
                                     : TH1520_VCLK_GMAC0_CCLK,
                                   ap_clock_phandle,
                                   i ? TH1520_VCLK_GMAC1_PCLK
                                     : TH1520_VCLK_GMAC0_PCLK,
                                   ap_clock_phandle, TH1520_VCLK_GMAC_AXI_ACLK,
                                   ap_clock_phandle, TH1520_VCLK_GMAC_AXI_PCLK);
            qemu_fdt_setprop_string_array(ms->fdt, name, "clock-names",
                                          (char **)&vendor_gmac_clock_names,
                                          ARRAY_SIZE(vendor_gmac_clock_names));
        } else {
            qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                                   ap_clock_phandle, TH1520_CLK_GMAC_AXI,
                                   ap_clock_phandle,
                                   i ? TH1520_CLK_GMAC1 : TH1520_CLK_GMAC0,
                                   ap_clock_phandle,
                                   TH1520_CLK_PERISYS_APB4_HCLK);
            qemu_fdt_setprop_string_array(ms->fdt, name, "clock-names",
                                          (char **)&gmac_clock_names,
                                          ARRAY_SIZE(gmac_clock_names));
        }
        qemu_fdt_setprop_cell(ms->fdt, name, "snps,pbl", 32);
        qemu_fdt_setprop(ms->fdt, name, "snps,fixed-burst", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, name,
                              "snps,multicast-filter-bins",
                              TH1520_GMAC_HASH_BINS);
        qemu_fdt_setprop_cell(ms->fdt, name,
                              "snps,perfect-filter-entries",
                              TH1520_GMAC_MAC_ADDRS);
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
            /* Match the upstream board DTS's active-low PHY GPIO wiring. */
            qemu_fdt_setprop_cell(ms->fdt, phy, "interrupt-parent",
                                  gpio_phandles[3]);
            qemu_fdt_setprop_cells(ms->fdt, phy, "interrupts",
                                   TH1520_GMAC_PHY_IRQ_GPIO,
                                   TH1520_IRQ_TYPE_LEVEL_LOW);
            qemu_fdt_setprop_cells(ms->fdt, phy, "reset-gpios",
                                   gpio_phandles[3],
                                   TH1520_GMAC_PHY_RESET_GPIO,
                                   TH1520_GPIO_ACTIVE_LOW);
            qemu_fdt_setprop_cell(ms->fdt, phy, "reset-delay-us",
                                  TH1520_GMAC_PHY_RESET_DELAY_US);
            qemu_fdt_setprop_cell(ms->fdt, phy, "reset-post-delay-us",
                                  TH1520_GMAC_PHY_RESET_POST_DELAY_US);
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
        BlockBackend *blk = NULL;
        DeviceState *card;

        if (dinfo) {
            blk = blk_by_legacy_dinfo(dinfo);
        } else if (i == 0 && !qtest_enabled()) {
            g_autofree char *tmp_path = NULL;
            int fd = g_file_open_tmp("beaglev-ahead-emmc-XXXXXX",
                                     &tmp_path, NULL);
            if (fd >= 0) {
                if (ftruncate(fd, 2 * MiB) == 0) {
                    QDict *opts = qdict_new();
                    qdict_put_str(opts, "driver", "raw");
                    qdict_put_str(opts, "file.driver", "file");
                    qdict_put_str(opts, "file.filename", tmp_path);
                    blk = blk_new_open(NULL, NULL, opts, BDRV_O_RDWR, NULL);
                }
                close(fd);
                g_unlink(tmp_path);
            }
        }

        if (!blk) {
            continue;
        }

        card = qdev_new(i == 0 ? TYPE_EMMC : TYPE_SD_CARD);
        if (i == 0) {
            qdev_prop_set_bit(card, "emmc-5.1-hs400-1.8v", true);
        }
        qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
        qdev_realize_and_unref(card, s->soc.mshc[i].bus, &error_fatal);
    }
}

#define TYPE_BEAGLEV_AHEAD_BME280 "beaglev-ahead-bme280"
OBJECT_DECLARE_SIMPLE_TYPE(BeagleVAheadBME280State, BEAGLEV_AHEAD_BME280)

struct BeagleVAheadBME280State {
    I2CSlave parent_obj;
    uint8_t pointer;
    bool addr_byte;
    uint8_t ctrl_hum;
    uint8_t ctrl_meas;
    uint8_t config;
};

static uint8_t bme280_read_reg(BeagleVAheadBME280State *s, uint8_t reg)
{
    /* Bosch BME280 factory trim & raw ADC values (~25.08 C, ~100.8 kPa, ~50.4 %RH) */
    static const uint8_t calib0[26] = {
        0x70, 0x6b, /* dig_t1 = 27504 */
        0x43, 0x67, /* dig_t2 = 26435 */
        0x18, 0xfc, /* dig_t3 = -1000 */
        0x7d, 0x8e, /* dig_p1 = 36477 */
        0x43, 0xd6, /* dig_p2 = -10685 */
        0xd0, 0x0b, /* dig_p3 = 3024 */
        0x27, 0x0b, /* dig_p4 = 2855 */
        0x8c, 0x00, /* dig_p5 = 140 */
        0xf9, 0xff, /* dig_p6 = -7 */
        0x8c, 0x3c, /* dig_p7 = 15500 */
        0xf8, 0xc6, /* dig_p8 = -14600 */
        0x70, 0x17, /* dig_p9 = 6000 */
        0x00,       /* reserved */
        0x4b,       /* dig_h1 = 75 */
    };
    static const uint8_t calib1[7] = {
        0x72, 0x01, /* dig_h2 = 370 */
        0x00,       /* dig_h3 = 0 */
        0x13, 0x29, 0x03, /* dig_h4 = 313, dig_h5 = 50 */
        0x1e,       /* dig_h6 = 30 */
    };
    static const uint8_t raw_adc[8] = {
        0x65, 0x5a, 0xc0, /* press_msb..xlsb: adc_P = 415148 */
        0x7e, 0xf1, 0x00, /* temp_msb..xlsb:  adc_T = 519888 */
        0x73, 0x3c,       /* hum_msb..lsb:    adc_H = 29500 */
    };

    if (reg >= 0x88 && reg <= 0xa1) {
        return calib0[reg - 0x88];
    }
    if (reg >= 0xe1 && reg <= 0xe7) {
        return calib1[reg - 0xe1];
    }
    if (reg >= 0xf7 && reg <= 0xfe) {
        return raw_adc[reg - 0xf7];
    }
    switch (reg) {
    case 0xd0:
        return 0x60; /* BME280_CHIP_ID */
    case 0xf2:
        return s->ctrl_hum;
    case 0xf3:
        return 0x00; /* status: idle */
    case 0xf4:
        return s->ctrl_meas;
    case 0xf5:
        return s->config;
    default:
        return 0x00;
    }
}

static int bme280_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    BeagleVAheadBME280State *s = BEAGLEV_AHEAD_BME280(i2c);

    if (event == I2C_START_SEND) {
        s->addr_byte = true;
    }
    return 0;
}

static uint8_t bme280_i2c_rx(I2CSlave *i2c)
{
    BeagleVAheadBME280State *s = BEAGLEV_AHEAD_BME280(i2c);
    uint8_t val = bme280_read_reg(s, s->pointer);

    s->pointer++;
    return val;
}

static int bme280_i2c_tx(I2CSlave *i2c, uint8_t data)
{
    BeagleVAheadBME280State *s = BEAGLEV_AHEAD_BME280(i2c);

    if (s->addr_byte) {
        s->pointer = data;
        s->addr_byte = false;
        return 0;
    }

    switch (s->pointer) {
    case 0xe0:
        if (data == 0xb6) {
            s->ctrl_hum = 0;
            s->ctrl_meas = 0;
            s->config = 0;
        }
        break;
    case 0xf2:
        s->ctrl_hum = data & 0x07;
        break;
    case 0xf4:
        s->ctrl_meas = data;
        break;
    case 0xf5:
        s->config = data;
        break;
    default:
        break;
    }
    s->pointer++;
    return 0;
}

static void bme280_reset(DeviceState *dev)
{
    BeagleVAheadBME280State *s = BEAGLEV_AHEAD_BME280(dev);

    s->pointer = 0;
    s->addr_byte = false;
    s->ctrl_hum = 0;
    s->ctrl_meas = 0;
    s->config = 0;
}

static const VMStateDescription vmstate_beaglev_ahead_bme280 = {
    .name = TYPE_BEAGLEV_AHEAD_BME280,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, BeagleVAheadBME280State),
        VMSTATE_UINT8(pointer, BeagleVAheadBME280State),
        VMSTATE_BOOL(addr_byte, BeagleVAheadBME280State),
        VMSTATE_UINT8(ctrl_hum, BeagleVAheadBME280State),
        VMSTATE_UINT8(ctrl_meas, BeagleVAheadBME280State),
        VMSTATE_UINT8(config, BeagleVAheadBME280State),
        VMSTATE_END_OF_LIST()
    }
};

static void bme280_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->desc = "Bosch BME280 Environmental Sensor (MikroBUS Weather Click)";
    device_class_set_legacy_reset(dc, bme280_reset);
    dc->vmsd = &vmstate_beaglev_ahead_bme280;
    k->event = bme280_i2c_event;
    k->recv = bme280_i2c_rx;
    k->send = bme280_i2c_tx;
}

#define TYPE_BEAGLEV_AHEAD_MCP2518FD "beaglev-ahead-mcp2518fd"
OBJECT_DECLARE_SIMPLE_TYPE(BeagleVAheadMCP2518FDState, BEAGLEV_AHEAD_MCP2518FD)

struct BeagleVAheadMCP2518FDState {
    SSIPeripheral parent_obj;
    qemu_irq irq;
    bool irq_driven;
    uint32_t phase;
    uint8_t cmd;
    uint16_t addr;
    uint8_t crc_len;
    uint16_t crc_accum;
    uint8_t cmd_hdr[3];
    uint8_t mem[0x1000];
    uint8_t tef_head;
    uint8_t tef_tail;
    uint8_t tef_count;
    uint8_t txq_head;
    uint8_t rx1_head;
    uint8_t rx1_tail;
    uint8_t rx1_count;
};

static uint16_t mcp2518fd_crc16(uint16_t crc, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x8005;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static uint32_t mcp2518fd_plsize_bytes(uint32_t con)
{
    static const uint32_t map[8] = { 8, 12, 16, 20, 24, 32, 48, 64 };
    return map[(con >> 29) & 0x7];
}

static void mcp2518fd_sync_fifo_regs(BeagleVAheadMCP2518FDState *s)
{
    uint32_t tefcon = ldl_le_p(&s->mem[0x040]);
    uint32_t txqcon = ldl_le_p(&s->mem[0x050]);
    uint32_t fifocon1 = ldl_le_p(&s->mem[0x05c]);
    uint32_t tef_cap = ((tefcon >> 24) & 0x1f) + 1;
    uint32_t txq_cap = ((txqcon >> 24) & 0x1f) + 1;
    uint32_t rx1_cap = ((fifocon1 >> 24) & 0x1f) + 1;
    uint32_t tef_item = 8;
    uint32_t txq_item = 8 + mcp2518fd_plsize_bytes(txqcon);
    uint32_t rx1_item = ((fifocon1 & BIT(5)) ? 12 : 8) +
                        mcp2518fd_plsize_bytes(fifocon1);
    uint32_t tef_base = 0;
    uint32_t txq_base = tef_cap * tef_item;
    uint32_t rx1_base = txq_base + txq_cap * txq_item;

    uint32_t tefsta = (s->tef_count > 0) ? BIT(0) : 0;
    uint32_t tefua = tef_base + (s->tef_tail % tef_cap) * tef_item;
    stl_le_p(&s->mem[0x044], tefsta);
    stl_le_p(&s->mem[0x048], tefua);

    uint32_t txqsta = BIT(0) | BIT(2) | ((s->txq_head % txq_cap) << 8);
    uint32_t txqua = txq_base + (s->txq_head % txq_cap) * txq_item;
    stl_le_p(&s->mem[0x054], txqsta);
    stl_le_p(&s->mem[0x058], txqua);

    uint32_t fifosta1 = ((s->rx1_count > 0) ? BIT(0) : 0) |
                        ((s->rx1_head % rx1_cap) << 8);
    uint32_t fifoua1 = rx1_base + (s->rx1_tail % rx1_cap) * rx1_item;
    stl_le_p(&s->mem[0x060], fifosta1);
    stl_le_p(&s->mem[0x064], fifoua1);
}

static void mcp2518fd_update_irq(BeagleVAheadMCP2518FDState *s)
{
    uint32_t reg_int = ldl_le_p(&s->mem[0x01c]);
    uint16_t ie = (reg_int >> 16) & 0xffff;
    uint16_t flags = reg_int & 0xffff;
    bool active = (ie & flags) != 0;

    if (ie != 0 || s->irq_driven) {
        s->irq_driven = true;
        qemu_set_irq(s->irq, active ? 1 : 0);
    }
}

static void mcp2518fd_reset_chip(BeagleVAheadMCP2518FDState *s)
{
    memset(s->mem, 0, sizeof(s->mem));
    s->tef_head = 0;
    s->tef_tail = 0;
    s->tef_count = 0;
    s->txq_head = 0;
    s->rx1_head = 0;
    s->rx1_tail = 0;
    s->rx1_count = 0;

    /* REG_CON: REQOP=4 (CONFIG), OPMOD=4 (CONFIG), TXQEN, STEF, WAKFIL, ISOCRCEN */
    stl_le_p(&s->mem[0x000], (4U << 24) | (4U << 21) | BIT(20) | BIT(19) |
                             BIT(8) | BIT(5));
    /* REG_OSC: OSCRDY | SCLKRDY */
    stl_le_p(&s->mem[0xe00], BIT(10) | BIT(12));
    /* REG_IOCON: TRIS0 | TRIS1 | PM0 | PM1 */
    stl_le_p(&s->mem[0xe04], BIT(25) | BIT(24) | BIT(1) | BIT(0));
    /* REG_DEVID: MCP2518FD */
    stl_le_p(&s->mem[0xe14], 0x00000010);

    mcp2518fd_sync_fifo_regs(s);
    if (s->irq_driven) {
        qemu_set_irq(s->irq, 0);
    }
}

static void mcp2518fd_handle_txreq(BeagleVAheadMCP2518FDState *s)
{
    uint32_t con = ldl_le_p(&s->mem[0x000]);
    uint32_t tefcon = ldl_le_p(&s->mem[0x040]);
    uint32_t txqcon = ldl_le_p(&s->mem[0x050]);
    uint32_t fifocon1 = ldl_le_p(&s->mem[0x05c]);
    uint32_t tef_cap = ((tefcon >> 24) & 0x1f) + 1;
    uint32_t txq_cap = ((txqcon >> 24) & 0x1f) + 1;
    uint32_t rx1_cap = ((fifocon1 >> 24) & 0x1f) + 1;
    uint32_t tef_item = 8;
    uint32_t txq_pl = mcp2518fd_plsize_bytes(txqcon);
    uint32_t txq_item = 8 + txq_pl;
    bool rx_ts = (fifocon1 & BIT(5)) != 0;
    uint32_t rx1_pl = mcp2518fd_plsize_bytes(fifocon1);
    uint32_t rx1_item = (rx_ts ? 12 : 8) + rx1_pl;
    uint32_t tef_base = 0;
    uint32_t txq_base = tef_cap * tef_item;
    uint32_t rx1_base = txq_base + txq_cap * txq_item;

    uint32_t tx_ram_off = 0x400 + txq_base + (s->txq_head % txq_cap) * txq_item;
    if (tx_ram_off + txq_item > sizeof(s->mem)) {
        return;
    }

    uint32_t tx_id = ldl_le_p(&s->mem[tx_ram_off + 0]);
    uint32_t tx_flags = ldl_le_p(&s->mem[tx_ram_off + 4]);
    const uint8_t *tx_data = &s->mem[tx_ram_off + 8];

    s->txq_head = (s->txq_head + 1) % txq_cap;

    /* 1. Store into TEF if STEF is set */
    if ((con & BIT(19)) && s->tef_count < tef_cap) {
        uint32_t tef_ram_off = 0x400 + tef_base +
                               (s->tef_head % tef_cap) * tef_item;
        if (tef_ram_off + 8 <= sizeof(s->mem)) {
            stl_le_p(&s->mem[tef_ram_off + 0], tx_id);
            stl_le_p(&s->mem[tef_ram_off + 4], tx_flags);
            s->tef_head = (s->tef_head + 1) % tef_cap;
            s->tef_count++;
            if (tefcon & BIT(0)) {
                uint32_t reg_int = ldl_le_p(&s->mem[0x01c]);
                stl_le_p(&s->mem[0x01c], reg_int | BIT(4)); /* TEFIF */
            }
        }
    }

    /* 2. Loopback / deliver into RX FIFO 1 */
    if (s->rx1_count < rx1_cap) {
        uint32_t filhit = 0;
        for (uint32_t f = 0; f < 32; f++) {
            uint8_t fltcon = s->mem[0x1d0 + f];
            if (fltcon & BIT(7)) {
                uint32_t fltobj = ldl_le_p(&s->mem[0x1f0 + 8 * f]);
                uint32_t fltmask = ldl_le_p(&s->mem[0x1f4 + 8 * f]);
                if (((tx_id ^ fltobj) & (fltmask & 0x1fffffffU)) == 0) {
                    filhit = f;
                    break;
                }
            }
        }
        uint32_t rx_ram_off = 0x400 + rx1_base +
                              (s->rx1_head % rx1_cap) * rx1_item;
        if (rx_ram_off + rx1_item <= sizeof(s->mem)) {
            uint32_t rx_flags = (tx_flags & ~0x0000f800U) | (filhit << 11);
            stl_le_p(&s->mem[rx_ram_off + 0], tx_id);
            stl_le_p(&s->mem[rx_ram_off + 4], rx_flags);
            if (rx_ts) {
                uint32_t tbc = ldl_le_p(&s->mem[0x010]) + 100;
                stl_le_p(&s->mem[0x010], tbc);
                stl_le_p(&s->mem[rx_ram_off + 8], tbc);
                memcpy(&s->mem[rx_ram_off + 12], tx_data, MIN(txq_pl, rx1_pl));
            } else {
                memcpy(&s->mem[rx_ram_off + 8], tx_data, MIN(txq_pl, rx1_pl));
            }
            s->rx1_head = (s->rx1_head + 1) % rx1_cap;
            s->rx1_count++;
            stl_le_p(&s->mem[0x020], BIT(1)); /* REG_RXIF */
            if (fifocon1 & BIT(0)) {
                uint32_t reg_int = ldl_le_p(&s->mem[0x01c]);
                stl_le_p(&s->mem[0x01c], reg_int | BIT(1)); /* RXIF */
            }
        }
    }

    mcp2518fd_sync_fifo_regs(s);
}

static void mcp2518fd_on_write_byte(BeagleVAheadMCP2518FDState *s,
                                    uint16_t addr, uint8_t val)
{
    if (addr >= sizeof(s->mem)) {
        return;
    }

    if (addr >= 0x01c && addr <= 0x01d) {
        /* Low 16 bits of REG_INT: clearable IF flags */
        s->mem[addr] &= val | ~0xf808U >> ((addr - 0x01c) * 8);
        s->mem[addr] = (s->mem[addr] & 0x03) | (val & ~0x03);
        mcp2518fd_update_irq(s);
        return;
    }

    s->mem[addr] = val;

    if (addr <= 0x003) {
        uint32_t con = ldl_le_p(&s->mem[0x000]);
        uint32_t reqop = (con >> 24) & 0x7;
        uint32_t opmod = (con >> 21) & 0x7;
        con &= ~BIT(27); /* Clear ABAT */
        if (reqop != opmod) {
            con = (con & ~(0x7U << 21)) | (reqop << 21);
            uint32_t reg_int = ldl_le_p(&s->mem[0x01c]);
            stl_le_p(&s->mem[0x01c], reg_int | BIT(3)); /* MODIF */
        }
        stl_le_p(&s->mem[0x000], con);
        mcp2518fd_update_irq(s);
    } else if (addr >= 0x01c && addr <= 0x01f) {
        mcp2518fd_update_irq(s);
    } else if (addr >= 0x040 && addr <= 0x043) {
        uint32_t tefcon = ldl_le_p(&s->mem[0x040]);
        uint32_t tef_cap = ((tefcon >> 24) & 0x1f) + 1;
        if (tefcon & BIT(10)) { /* FRESET */
            s->tef_head = 0;
            s->tef_tail = 0;
            s->tef_count = 0;
            tefcon &= ~BIT(10);
        }
        if (tefcon & BIT(8)) { /* UINC */
            if (s->tef_count > 0) {
                s->tef_tail = (s->tef_tail + 1) % tef_cap;
                s->tef_count--;
            }
            if (s->tef_count == 0) {
                uint32_t reg_int = ldl_le_p(&s->mem[0x01c]);
                stl_le_p(&s->mem[0x01c], reg_int & ~BIT(4)); /* Clear TEFIF */
            }
            tefcon &= ~BIT(8);
        }
        stl_le_p(&s->mem[0x040], tefcon);
        mcp2518fd_sync_fifo_regs(s);
        mcp2518fd_update_irq(s);
    } else if (addr >= 0x050 && addr <= 0x053) {
        uint32_t txqcon = ldl_le_p(&s->mem[0x050]);
        if (txqcon & BIT(10)) { /* FRESET */
            s->txq_head = 0;
            txqcon &= ~BIT(10);
        }
        if (txqcon & (BIT(9) | BIT(8))) { /* TXREQ | UINC */
            txqcon &= ~(BIT(9) | BIT(8));
            stl_le_p(&s->mem[0x050], txqcon);
            mcp2518fd_handle_txreq(s);
        } else {
            stl_le_p(&s->mem[0x050], txqcon);
            mcp2518fd_sync_fifo_regs(s);
        }
        mcp2518fd_update_irq(s);
    } else if (addr >= 0x05c && addr <= 0x05f) {
        uint32_t fifocon1 = ldl_le_p(&s->mem[0x05c]);
        uint32_t rx1_cap = ((fifocon1 >> 24) & 0x1f) + 1;
        if (fifocon1 & BIT(10)) { /* FRESET */
            s->rx1_head = 0;
            s->rx1_tail = 0;
            s->rx1_count = 0;
            fifocon1 &= ~BIT(10);
        }
        if (fifocon1 & BIT(8)) { /* UINC */
            if (s->rx1_count > 0) {
                s->rx1_tail = (s->rx1_tail + 1) % rx1_cap;
                s->rx1_count--;
            }
            if (s->rx1_count == 0) {
                stl_le_p(&s->mem[0x020], 0);
                uint32_t reg_int = ldl_le_p(&s->mem[0x01c]);
                stl_le_p(&s->mem[0x01c], reg_int & ~BIT(1)); /* Clear RXIF */
            }
            fifocon1 &= ~BIT(8);
        }
        stl_le_p(&s->mem[0x05c], fifocon1);
        mcp2518fd_sync_fifo_regs(s);
        mcp2518fd_update_irq(s);
    } else if (addr >= 0xe00 && addr <= 0xe03) {
        uint32_t osc = ldl_le_p(&s->mem[0xe00]);
        osc |= BIT(10) | BIT(12); /* OSCRDY | SCLKRDY */
        if (osc & BIT(0)) {
            osc |= BIT(8); /* PLLRDY */
        }
        stl_le_p(&s->mem[0xe00], osc);
    }
}

static int mcp2518fd_set_cs(SSIPeripheral *dev, bool select)
{
    BeagleVAheadMCP2518FDState *s = BEAGLEV_AHEAD_MCP2518FD(dev);

    if (select) {
        s->phase = 0;
        s->cmd = 0;
        s->addr = 0;
        s->crc_len = 0;
        s->crc_accum = 0xffff;
    }
    return 0;
}

static uint32_t mcp2518fd_transfer(SSIPeripheral *dev, uint32_t tx)
{
    BeagleVAheadMCP2518FDState *s = BEAGLEV_AHEAD_MCP2518FD(dev);
    uint8_t byte = tx & 0xff;
    uint8_t rx = 0;

    if (s->phase == 0) {
        s->cmd_hdr[0] = byte;
        s->cmd = (byte >> 4) & 0x0f;
        s->addr = (uint16_t)(byte & 0x0f) << 8;
        s->phase = 1;
        return 0;
    }
    if (s->phase == 1) {
        s->cmd_hdr[1] = byte;
        s->addr |= byte;
        s->phase = 2;
        if (s->cmd == 0x0 && s->addr == 0x000) {
            mcp2518fd_reset_chip(s);
        }
        return 0;
    }

    switch (s->cmd) {
    case 0x2: /* WRITE */
    case 0xa: /* WRITE_CRC */
    case 0xc: /* WRITE_SAFE */
        mcp2518fd_on_write_byte(s, s->addr, byte);
        s->addr = (s->addr + 1) & 0x0fff;
        break;
    case 0x3: /* READ */
        rx = (s->addr < sizeof(s->mem)) ? s->mem[s->addr] : 0;
        s->addr = (s->addr + 1) & 0x0fff;
        break;
    case 0xb: /* READ_CRC */
        if (s->phase == 2) {
            s->cmd_hdr[2] = byte;
            s->crc_len = byte;
            s->crc_accum = mcp2518fd_crc16(0xffff, s->cmd_hdr, 3);
            s->phase = 3;
            return 0;
        } else if (s->phase < 3U + s->crc_len) {
            rx = (s->addr < sizeof(s->mem)) ? s->mem[s->addr] : 0;
            s->crc_accum = mcp2518fd_crc16(s->crc_accum, &rx, 1);
            s->addr = (s->addr + 1) & 0x0fff;
            s->phase++;
            return rx;
        } else if (s->phase == 3U + s->crc_len) {
            rx = (s->crc_accum >> 8) & 0xff;
            s->phase++;
            return rx;
        } else if (s->phase == 4U + s->crc_len) {
            rx = s->crc_accum & 0xff;
            s->phase++;
            return rx;
        }
        break;
    default:
        break;
    }

    return rx;
}

static void mcp2518fd_reset(DeviceState *dev)
{
    BeagleVAheadMCP2518FDState *s = BEAGLEV_AHEAD_MCP2518FD(dev);

    s->phase = 0;
    s->cmd = 0;
    s->addr = 0;
    s->crc_len = 0;
    s->crc_accum = 0xffff;
    mcp2518fd_reset_chip(s);
}

static void mcp2518fd_realize(SSIPeripheral *dev, Error **errp)
{
    BeagleVAheadMCP2518FDState *s = BEAGLEV_AHEAD_MCP2518FD(dev);

    qdev_init_gpio_out(DEVICE(dev), &s->irq, 1);
    mcp2518fd_reset_chip(s);
}

static const VMStateDescription vmstate_beaglev_ahead_mcp2518fd = {
    .name = TYPE_BEAGLEV_AHEAD_MCP2518FD,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_SSI_PERIPHERAL(parent_obj, BeagleVAheadMCP2518FDState),
        VMSTATE_BOOL(irq_driven, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT32(phase, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT8(cmd, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT16(addr, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT8(crc_len, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT16(crc_accum, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT8_ARRAY(cmd_hdr, BeagleVAheadMCP2518FDState, 3),
        VMSTATE_UINT8_ARRAY(mem, BeagleVAheadMCP2518FDState, 0x1000),
        VMSTATE_UINT8(tef_head, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT8(tef_tail, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT8(tef_count, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT8(txq_head, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT8(rx1_head, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT8(rx1_tail, BeagleVAheadMCP2518FDState),
        VMSTATE_UINT8(rx1_count, BeagleVAheadMCP2518FDState),
        VMSTATE_END_OF_LIST()
    }
};

static void mcp2518fd_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    dc->desc = "Microchip MCP2518FD CAN-FD Controller (MikroBUS CAN-FD Click)";
    device_class_set_legacy_reset(dc, mcp2518fd_reset);
    dc->vmsd = &vmstate_beaglev_ahead_mcp2518fd;
    k->realize = mcp2518fd_realize;
    k->transfer = mcp2518fd_transfer;
    k->set_cs = mcp2518fd_set_cs;
    k->cs_polarity = SSI_CS_LOW;
}

#define TYPE_CHARDEV_AP6203BM_HCI "chardev-ap6203bm-hci"
OBJECT_DECLARE_SIMPLE_TYPE(AP6203BMHCIChardev, CHARDEV_AP6203BM_HCI)

struct AP6203BMHCIChardev {
    Chardev parent_obj;
    QEMUTimer *tx_timer;
    uint8_t cmd_buf[264];
    uint32_t cmd_len;
    uint8_t tx_buf[512];
    uint32_t tx_head;
    uint32_t tx_len;
};

static void ap6203bm_hci_drain_tx(void *opaque)
{
    AP6203BMHCIChardev *s = CHARDEV_AP6203BM_HCI(opaque);
    Chardev *chr = CHARDEV(s);

    if (s->tx_len == 0) {
        return;
    }

    int can = qemu_chr_be_can_write(chr);
    if (can > 0) {
        uint32_t n = MIN((uint32_t)can, s->tx_len);
        qemu_chr_be_write(chr, &s->tx_buf[s->tx_head], n);
        s->tx_head += n;
        s->tx_len -= n;
        if (s->tx_len == 0) {
            s->tx_head = 0;
        }
    }

    if (s->tx_len > 0) {
        timer_mod_ns(s->tx_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 50000);
    }
}

static void ap6203bm_hci_queue_evt(AP6203BMHCIChardev *s,
                                   const uint8_t *data, size_t len)
{
    if (s->tx_head + s->tx_len + len > sizeof(s->tx_buf)) {
        if (s->tx_len > 0) {
            memmove(s->tx_buf, &s->tx_buf[s->tx_head], s->tx_len);
        }
        s->tx_head = 0;
    }
    if (s->tx_len + len <= sizeof(s->tx_buf)) {
        memcpy(&s->tx_buf[s->tx_head + s->tx_len], data, len);
        s->tx_len += len;
        timer_mod_ns(s->tx_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 50000);
    }
}

static void ap6203bm_hci_handle_cmd(AP6203BMHCIChardev *s, uint16_t opcode,
                                    const uint8_t *params, uint8_t plen)
{
    uint8_t evt[128];
    uint8_t extra[72];
    size_t extra_len = 0;

    memset(extra, 0, sizeof(extra));

    switch (opcode) {
    case 0x1001: /* HCI_Read_Local_Version_Info */
        extra[0] = 0x09; /* HCI Version: Bluetooth 5.0 */
        extra[1] = 0x15;
        extra[2] = 0x00;
        extra[3] = 0x09; /* LMP Version: 5.0 */
        extra[4] = 0x0f; /* Broadcom (AP6203BM) */
        extra[5] = 0x00;
        extra[6] = 0x03;
        extra[7] = 0x41;
        extra_len = 8;
        break;
    case 0x1002: /* HCI_Read_Local_Supported_Commands */
        /* Enable standard HCI & LE commands (octets 0..38) */
        memset(extra, 0xff, 36);
        extra_len = 64;
        break;
    case 0x1003: /* HCI_Read_Local_Supported_Features */
        /* Byte 4: LMP_BREDR_NOT_SUPPORTED (0x20) | LMP_LE_SUPPORTED (0x40) */
        extra[4] = 0x60;
        extra_len = 8;
        break;
    case 0x1005: /* HCI_Read_Buffer_Size */
        extra[0] = 0xfb;
        extra[1] = 0x00;
        extra[2] = 0x40;
        extra[3] = 0x08;
        extra[4] = 0x00;
        extra[5] = 0x04;
        extra[6] = 0x00;
        extra_len = 7;
        break;
    case 0x1009: /* HCI_Read_BD_ADDR: 20:15:03:62:BE:00 */
        extra[0] = 0x20;
        extra[1] = 0x15;
        extra[2] = 0x03;
        extra[3] = 0x62;
        extra[4] = 0xbe;
        extra[5] = 0x00;
        extra_len = 6;
        break;
    case 0x2002: /* HCI_LE_Read_Buffer_Size */
        extra[0] = 0xfb;
        extra[1] = 0x00;
        extra[2] = 0x08;
        extra_len = 3;
        break;
    case 0x2003: /* HCI_LE_Read_Local_Supported_Features */
        extra[0] = 0x01; /* LE Encryption */
        extra_len = 8;
        break;
    case 0x2007: /* HCI_LE_Read_Advertising_Physical_Channel_Tx_Power */
        extra[0] = 0x08; /* +8 dBm */
        extra_len = 1;
        break;
    case 0x200f: /* HCI_LE_Read_Filter_Accept_List_Size */
        extra[0] = 0x08;
        extra_len = 1;
        break;
    case 0x2018: /* HCI_LE_Rand */
        for (int i = 0; i < 8; i++) {
            extra[i] = 0x11 * (i + 1);
        }
        extra_len = 8;
        break;
    case 0x201c: /* HCI_LE_Read_Supported_States */
        extra[0] = 0xff;
        extra[1] = 0xff;
        extra[2] = 0xff;
        extra[3] = 0x03;
        extra_len = 8;
        break;
    default:
        extra_len = 0;
        break;
    }

    evt[0] = 0x04; /* H4_EVT */
    evt[1] = 0x0e; /* BT_HCI_EVT_CMD_COMPLETE */
    evt[2] = 4 + extra_len;
    evt[3] = 0x01; /* num_hci_cmd_packets */
    evt[4] = opcode & 0xff;
    evt[5] = (opcode >> 8) & 0xff;
    evt[6] = 0x00; /* status: SUCCESS */
    if (extra_len > 0) {
        memcpy(&evt[7], extra, extra_len);
    }
    ap6203bm_hci_queue_evt(s, evt, 7 + extra_len);

    /* If LE Scan is enabled (0x200c, enable=1), emit an LE Advertising Report */
    if (opcode == 0x200c && plen >= 1 && params[0] == 0x01) {
        static const uint8_t adv_report[] = {
            0x04, /* H4_EVT */
            0x3e, /* BT_HCI_EVT_LE_META_EVENT */
            29,   /* param len */
            0x02, /* BT_HCI_EVT_LE_ADVERTISING_REPORT */
            0x01, /* num_reports = 1 */
            0x00, /* evt_type = BT_HCI_ADV_IND */
            0x00, /* addr_type = public */
            0xaa, 0xbb, 0xcc, 0x20, 0x15, 0x00, /* peer addr 00:15:20:CC:BB:AA */
            17,   /* data_len = 17 */
            0x02, 0x01, 0x06, /* Flags: LE General Discoverable | BR/EDR Not Supp */
            13,   0x09, 'A', 'P', '6', '2', '0', '3', 'B', 'M', '-', 'B', 'L', 'E',
            0xd6, /* RSSI = -42 dBm */
        };
        ap6203bm_hci_queue_evt(s, adv_report, sizeof(adv_report));
    }
}

static int ap6203bm_hci_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    AP6203BMHCIChardev *s = CHARDEV_AP6203BM_HCI(chr);

    for (int i = 0; i < len; i++) {
        uint8_t b = buf[i];
        if (s->cmd_len == 0 && b != 0x01) {
            /* Ignore non-command packet headers or sync bytes */
            continue;
        }
        if (s->cmd_len < sizeof(s->cmd_buf)) {
            s->cmd_buf[s->cmd_len++] = b;
        }
        if (s->cmd_len >= 4) {
            uint8_t plen = s->cmd_buf[3];
            if (s->cmd_len == 4U + plen) {
                uint16_t opcode = lduw_le_p(&s->cmd_buf[1]);
                ap6203bm_hci_handle_cmd(s, opcode, &s->cmd_buf[4], plen);
                s->cmd_len = 0;
            }
        }
    }
    return len;
}

static void ap6203bm_hci_chr_init(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    AP6203BMHCIChardev *s = CHARDEV_AP6203BM_HCI(obj);

    chr->be_open = true;
    s->tx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, ap6203bm_hci_drain_tx, s);
}

static void ap6203bm_hci_chr_class_init(ObjectClass *oc, const void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->internal = true;
    cc->chr_write = ap6203bm_hci_chr_write;
}

static void beaglev_ahead_attach_mikrobus(BeagleVAheadState *s)
{
    DeviceState *mcp;

    /* MikroBUS I2C (i2c3): Bosch BME280 Weather Click at 0x76 */
    i2c_slave_create_simple(s->soc.i2c[3].bus, TYPE_BEAGLEV_AHEAD_BME280, 0x76);

    /* MikroBUS SPI (spi0, CS0, INT on GPIO0_20): Microchip MCP2518FD CAN-FD Click */
    mcp = ssi_create_peripheral(s->soc.spi[0].spi, TYPE_BEAGLEV_AHEAD_MCP2518FD);
    qdev_connect_gpio_out_named(DEVICE(&s->soc.spi[0]), "cs", 0,
                                qdev_get_gpio_in_named(mcp, SSI_GPIO_CS, 0));
    qdev_connect_gpio_out(mcp, 0,
                          qdev_get_gpio_in_named(DEVICE(&s->soc.gpio[0]),
                                                 "gpio-in", 20));
}

static void beaglev_ahead_attach_ap6203bm(BeagleVAheadState *s)
{
    DeviceState *gpio2 = DEVICE(&s->soc.gpio[2]);
    DeviceState *module;

    object_initialize_child(OBJECT(s), "ap6203bm", &s->ap6203bm,
                            TYPE_TH1520_AP6203BM);
    module = DEVICE(&s->ap6203bm);

    /*
     * Board schematic sheet 17 routes these module control/wake nets:
     * GPIO2_31/WL_REG_ON, GPIO2_28/BT_REG_ON,
     * GPIO2_30/BT_WAKE_HOST, GPIO2_25/WL_HW_OOB, and
     * GPIO2_29/HOST_WAKE_BT.  This is deliberately not an SDIO, WLAN or
     * Bluetooth protocol model; the AP6203BM peer only preserves the wires.
     */
    qdev_connect_gpio_out_named(gpio2, "gpio-out", 31,
                                qdev_get_gpio_in_named(module,
                                                       "wl-reg-on-in", 0));
    qdev_connect_gpio_out_named(gpio2, "gpio-out", 28,
                                qdev_get_gpio_in_named(module,
                                                       "bt-reg-on-in", 0));
    qdev_connect_gpio_out_named(gpio2, "gpio-out", 30,
                                qdev_get_gpio_in_named(module,
                                                       "bt-wake-host-in", 0));
    qdev_connect_gpio_out_named(module, "host-wake",
                                TH1520_AP6203BM_WL_HOST_WAKE,
                                qdev_get_gpio_in_named(gpio2, "gpio-in", 25));
    qdev_connect_gpio_out_named(module, "host-wake",
                                TH1520_AP6203BM_BT_HOST_WAKE,
                                qdev_get_gpio_in_named(gpio2, "gpio-in", 29));
    qdev_realize(module, NULL, &error_fatal);
}

static void beaglev_ahead_attach_pmic(BeagleVAheadState *s)
{
    /*
     * The board schematic identifies U81 as a DA9063.  Public vendor SPL
     * accesses it at 0x5a through AON I2C before DRAM initialization.
     * The device only carries that SPL's DVC/register state; it creates no
     * generated DT node or modeled rail, IRQ, RTC, watchdog, or GPIO effect.
     */
    i2c_slave_create_simple(s->soc.aon_i2c.bus, TYPE_DA9063,
                            BEAGLEV_AHEAD_PMIC_ADDRESS);
}

static void beaglev_ahead_attach_eeprom(BeagleVAheadState *s)
{
    g_autofree uint8_t *contents = g_malloc(BEAGLEV_AHEAD_EEPROM_SIZE);

    /* Factory-programmed, board-unique bytes are deliberately not invented. */
    memset(contents, 0xff, BEAGLEV_AHEAD_EEPROM_SIZE);
    at24c_eeprom_init_rom_page_size(s->soc.i2c[0].bus,
                                    BEAGLEV_AHEAD_EEPROM_ADDRESS,
                                    BEAGLEV_AHEAD_EEPROM_SIZE,
                                    BEAGLEV_AHEAD_EEPROM_PAGE_SIZE,
                                    contents, BEAGLEV_AHEAD_EEPROM_SIZE);
}

static void beaglev_ahead_attach_leds(BeagleVAheadState *s)
{
    static const char *const user_led_names[] = {
        "USR0", "USR1", "USR2", "USR3", "USR4",
    };

    for (size_t i = 0; i < ARRAY_SIZE(user_led_names); i++) {
        LEDState *led = led_create_simple(OBJECT(s),
                                          GPIO_POLARITY_ACTIVE_HIGH,
                                          LED_COLOR_BLUE,
                                          user_led_names[i]);

        qdev_connect_gpio_out_named(DEVICE(&s->soc.gpio[4]), "gpio-out",
                                    8 + i,
                                    qdev_get_gpio_in(DEVICE(led), 0));
    }

    /* LED6 is the always-on green power indicator in the board schematic. */
    led_set_state(led_create_simple(OBJECT(s), GPIO_POLARITY_ACTIVE_HIGH,
                                    LED_COLOR_GREEN, "POWER"), true);
}

static void beaglev_ahead_validate_boot_options(BeagleVAheadState *s)
{
    MachineState *ms = MACHINE(s);
    g_autofree char *filename = NULL;
    int64_t image_size;

    if (s->boot_mode != BEAGLEV_AHEAD_BOOT_MASK_ROM) {
        return;
    }

    if (!ms->firmware || !strcmp(ms->firmware, "none") ||
        !strcmp(ms->firmware, "default")) {
        error_report("BeagleV Ahead mask-rom boot requires -bios <raw-image>");
        exit(EXIT_FAILURE);
    }

    if (ms->kernel_filename || ms->initrd_filename ||
        ms->kernel_cmdline[0] || ms->dtb) {
        error_report("BeagleV Ahead mask-rom boot does not accept "
                     "-kernel, -initrd, -append, or -dtb");
        exit(EXIT_FAILURE);
    }

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, ms->firmware);
    if (!filename) {
        error_report("Unable to find BeagleV Ahead mask-ROM image '%s'",
                     ms->firmware);
        exit(EXIT_FAILURE);
    }

    image_size = get_image_size(filename, &error_fatal);
    if (image_size == 0) {
        error_report("empty file: %s", filename);
        exit(EXIT_FAILURE);
    }
    if (image_size > th1520_memmap[TH1520_DEV_BROM].size) {
        error_report("%s exceeds maximum image size (1 MiB)", filename);
        exit(EXIT_FAILURE);
    }
}

static void beaglev_ahead_load_mask_rom(BeagleVAheadState *s)
{
    MachineState *ms = MACHINE(s);
    const MemMapEntry *brom = &th1520_memmap[TH1520_DEV_BROM];
    g_autofree char *filename =
        qemu_find_file(QEMU_FILE_TYPE_BIOS, ms->firmware);

    /* The early validation found this exact image through the BIOS path. */
    g_assert(filename);

    if (load_image_targphys(filename, brom->base, brom->size,
                            &error_fatal) < 0) {
        g_assert_not_reached();
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

    if (s->boot_mode == BEAGLEV_AHEAD_BOOT_MASK_ROM) {
        beaglev_ahead_load_mask_rom(s);
        return;
    }

    riscv_boot_info_init(&boot_info, &s->soc.c910_cpus);
    firmware_end = riscv_find_and_load_firmware(ms, &boot_info,
                                                firmware_name,
                                                &start_addr, NULL);
    kernel_start = riscv_calc_kernel_start_addr(&boot_info, firmware_end);

    if (ms->kernel_filename) {
        riscv_load_kernel(ms, &boot_info, kernel_start, true, NULL);
        kernel_entry = boot_info.image_low_addr;
        if (firmware_end == start_addr) {
            start_addr = kernel_entry;
        }
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

    if (!tcg_enabled() && !qtest_enabled()) {
        error_report("BeagleV Ahead requires TCG acceleration");
        exit(EXIT_FAILURE);
    }

    beaglev_ahead_validate_boot_options(s);

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

    if (s->boot_sel & ~TH1520_BOOTSEL_SELECT_MASK) {
        error_report("BeagleV Ahead boot-sel must be in the range 0x0..0xf");
        exit(EXIT_FAILURE);
    }

    memory_region_add_subregion(get_system_memory(),
                                th1520_memmap[TH1520_DEV_DRAM].base,
                                ms->ram);

    object_initialize_child(OBJECT(ms), "soc", &s->soc,
                            TYPE_RISCV_TH1520_SOC);
    qdev_prop_set_uint8(DEVICE(&s->soc.bootsel), "boot-sel", s->boot_sel);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    beaglev_ahead_attach_storage(s);
    beaglev_ahead_attach_ap6203bm(s);
    beaglev_ahead_attach_pmic(s);
    beaglev_ahead_attach_eeprom(s);
    beaglev_ahead_attach_leds(s);
    beaglev_ahead_attach_mikrobus(s);

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

static char *beaglev_ahead_get_boot_mode(Object *obj, Error **errp)
{
    BeagleVAheadState *s = BEAGLEV_AHEAD_MACHINE(obj);

    return g_strdup(s->boot_mode == BEAGLEV_AHEAD_BOOT_MASK_ROM ?
                    "mask-rom" : "direct");
}

static void beaglev_ahead_set_boot_mode(Object *obj, const char *value,
                                        Error **errp)
{
    BeagleVAheadState *s = BEAGLEV_AHEAD_MACHINE(obj);

    if (!strcmp(value, "direct")) {
        s->boot_mode = BEAGLEV_AHEAD_BOOT_DIRECT;
    } else if (!strcmp(value, "mask-rom")) {
        s->boot_mode = BEAGLEV_AHEAD_BOOT_MASK_ROM;
    } else {
        error_setg(errp, "unsupported BeagleV Ahead boot mode '%s' "
                   "(expected 'direct' or 'mask-rom')", value);
    }
}

static char *beaglev_ahead_get_clock_abi(Object *obj, Error **errp)
{
    BeagleVAheadState *s = BEAGLEV_AHEAD_MACHINE(obj);

    return g_strdup(s->clock_abi == TH1520_CLOCK_ABI_VENDOR ?
                    "vendor" : "upstream");
}

static void beaglev_ahead_set_clock_abi(Object *obj, const char *value,
                                        Error **errp)
{
    BeagleVAheadState *s = BEAGLEV_AHEAD_MACHINE(obj);

    if (!strcmp(value, "upstream")) {
        s->clock_abi = TH1520_CLOCK_ABI_UPSTREAM;
    } else if (!strcmp(value, "vendor")) {
        s->clock_abi = TH1520_CLOCK_ABI_VENDOR;
    } else {
        error_setg(errp, "unsupported BeagleV Ahead clock ABI '%s' "
                   "(expected 'upstream' or 'vendor')", value);
    }
}

static void beaglev_ahead_machine_instance_init(Object *obj)
{
    BeagleVAheadState *s = BEAGLEV_AHEAD_MACHINE(obj);

    s->boot_sel = TH1520_BOOTSEL_EMMC;
    object_property_add_uint8_ptr(obj, "boot-sel", &s->boot_sel,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "boot-sel",
        "BOOT_SEL[3:0] pin state: 0-3 USB, 4 eMMC, 5 SD, 6 SPI-NAND, "
        "or 7 SPI-NOR");
}

static void beaglev_ahead_machine_class_init(ObjectClass *oc,
                                              const void *data)
{
    static const char *const valid_cpu_types[] = {
        TYPE_RISCV_CPU_THEAD_C910,
        NULL,
    };
    MachineClass *mc = MACHINE_CLASS(oc);
    ObjectProperty *prop;

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

    prop = object_class_property_add_str(oc, "boot-mode",
                                         beaglev_ahead_get_boot_mode,
                                         beaglev_ahead_set_boot_mode);
    object_class_property_set_description(oc, "boot-mode",
        "Boot path: direct (QEMU firmware/FDT trampoline) or mask-rom "
        "(execute a user-supplied raw -bios image from the ROM aperture)");
    object_property_set_default_str(prop, "direct");

    prop = object_class_property_add_str(oc, "clock-abi",
                                         beaglev_ahead_get_clock_abi,
                                         beaglev_ahead_set_clock_abi);
    object_class_property_set_description(oc, "clock-abi",
        "Clock binding the generated device tree describes: upstream "
        "(thead,th1520-clk-ap, mainline Linux) or vendor "
        "(xuantie,th1520-fm-ree-clk, RevyOS/T-Head kernels)");
    object_property_set_default_str(prop, "upstream");
}

static const TypeInfo beaglev_ahead_types[] = {
    {
        .name = TYPE_BEAGLEV_AHEAD_BME280,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(BeagleVAheadBME280State),
        .class_init = bme280_class_init,
    },
    {
        .name = TYPE_BEAGLEV_AHEAD_MCP2518FD,
        .parent = TYPE_SSI_PERIPHERAL,
        .instance_size = sizeof(BeagleVAheadMCP2518FDState),
        .class_init = mcp2518fd_class_init,
    },
    {
        .name = TYPE_CHARDEV_AP6203BM_HCI,
        .parent = TYPE_CHARDEV,
        .instance_size = sizeof(AP6203BMHCIChardev),
        .instance_init = ap6203bm_hci_chr_init,
        .class_init = ap6203bm_hci_chr_class_init,
    },
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
        .instance_init = beaglev_ahead_machine_instance_init,
        .class_init = beaglev_ahead_machine_class_init,
        .interfaces = riscv64_machine_interfaces,
    },
};

DEFINE_TYPES(beaglev_ahead_types)
