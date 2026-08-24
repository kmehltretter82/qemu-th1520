/*
 * BeagleV Ahead machine tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/iov.h"
#include "qemu/units.h"
#include "hw/net/mii.h"
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
#define TH1520_AP_CLOCK_BASE       0xffef010000ULL
#define TH1520_AP_RESET_BASE       0xffef014000ULL
#define TH1520_UART0_BASE          0xffe7014000ULL
#define TH1520_UART1_BASE          0xffe7f00000ULL
#define TH1520_UART2_BASE          0xffec010000ULL
#define TH1520_UART3_BASE          0xffe7f04000ULL
#define TH1520_UART4_BASE          0xfff7f08000ULL
#define TH1520_UART5_BASE          0xfff7f0c000ULL
#define TH1520_GPIO0_BASE          0xffec005000ULL
#define TH1520_GPIO1_BASE          0xffec006000ULL
#define TH1520_GPIO2_BASE          0xffe7f34000ULL
#define TH1520_GPIO3_BASE          0xffe7f38000ULL
#define TH1520_GPIO4_BASE          0xfffff52000ULL
#define TH1520_AOGPIO_BASE         0xfffff41000ULL
#define TH1520_PADCTRL_AOSYS_BASE  0xfffff4a000ULL
#define TH1520_PADCTRL1_APSYS_BASE 0xffe7f3c000ULL
#define TH1520_PADCTRL0_APSYS_BASE 0xffec007000ULL
#define TH1520_I2C0_BASE           0xffe7f20000ULL
#define TH1520_I2C1_BASE           0xffe7f24000ULL
#define TH1520_I2C2_BASE           0xffec00c000ULL
#define TH1520_I2C3_BASE           0xffec014000ULL
#define TH1520_I2C4_BASE           0xffe7f28000ULL
#define TH1520_I2C5_BASE           0xfff7f2c000ULL
#define TH1520_SPI0_BASE           0xffe700c000ULL
#define TH1520_PWM_BASE            0xffec01c000ULL
#define TH1520_TIMER0_3_BASE       0xffefc32000ULL
#define TH1520_TIMER4_7_BASE       0xffffc33000ULL
#define TH1520_MBOX_LOCAL_BASE      0xffffc38000ULL
#define TH1520_MBOX_REMOTE0_BASE    0xffffc40000ULL
#define TH1520_MBOX_REMOTE1_BASE    0xffffc4c000ULL
#define TH1520_MBOX_REMOTE2_BASE    0xffffc54000ULL
#define TH1520_DMAC0_BASE          0xffefc00000ULL
#define TH1520_GMAC1_BASE          0xffe7060000ULL
#define TH1520_GMAC0_BASE          0xffe7070000ULL
#define TH1520_EMMC_BASE           0xffe7080000ULL
#define TH1520_SDIO0_BASE          0xffe7090000ULL
#define TH1520_SDIO1_BASE          0xffe70a0000ULL
#define TH1520_GMAC0_APB_BASE      0xffec003000ULL
#define TH1520_GMAC1_APB_BASE      0xffec004000ULL
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

#define DW_UART_IER_DLH_OFFSET     0x04
#define DW_UART_IIR_FCR_OFFSET     0x08
#define DW_UART_LSR_OFFSET         0x14
#define DW_UART_SCR_OFFSET         0x1c
#define DW_UART_CTR_OFFSET         0xfc

#define DW_GPIO_SWPORTA_DR         0x00
#define DW_GPIO_SWPORTA_DDR        0x04
#define DW_GPIO_SWPORTA_CTL        0x08
#define DW_GPIO_INTEN              0x30
#define DW_GPIO_INTMASK            0x34
#define DW_GPIO_INTTYPE_LEVEL      0x38
#define DW_GPIO_INT_POLARITY       0x3c
#define DW_GPIO_INTSTATUS          0x40
#define DW_GPIO_RAW_INTSTATUS      0x44
#define DW_GPIO_PORTA_DEBOUNCE     0x48
#define DW_GPIO_PORTA_EOI          0x4c
#define DW_GPIO_EXT_PORTA          0x50
#define DW_GPIO_LS_SYNC            0x60
#define DW_GPIO_ID_CODE            0x64
#define DW_GPIO_VER_ID_CODE        0x6c
#define DW_GPIO_CONFIG_REG2        0x70
#define DW_GPIO_CONFIG_REG1        0x74

#define DW_I2C_CON                 0x00
#define DW_I2C_TAR                 0x04
#define DW_I2C_DATA_CMD            0x10
#define DW_I2C_INTR_STAT           0x2c
#define DW_I2C_INTR_MASK           0x30
#define DW_I2C_RAW_INTR_STAT       0x34
#define DW_I2C_RX_TL               0x38
#define DW_I2C_CLR_INTR            0x40
#define DW_I2C_CLR_TX_ABRT         0x54
#define DW_I2C_ENABLE              0x6c
#define DW_I2C_STATUS              0x70
#define DW_I2C_RXFLR               0x78
#define DW_I2C_TX_ABRT_SOURCE      0x80
#define DW_I2C_SDA_SETUP           0x94
#define DW_I2C_ACK_GENERAL_CALL    0x98
#define DW_I2C_ENABLE_STATUS       0x9c
#define DW_I2C_FS_SPKLEN           0xa0
#define DW_I2C_HS_SPKLEN           0xa4
#define DW_I2C_SCL_STUCK_TIMEOUT   0xac
#define DW_I2C_SDA_STUCK_TIMEOUT   0xb0
#define DW_I2C_COMP_PARAM1         0xf4
#define DW_I2C_COMP_VERSION        0xf8
#define DW_I2C_COMP_TYPE           0xfc

#define DW_I2C_CON_MASTER          BIT(0)
#define DW_I2C_CON_SPEED_FAST      BIT(2)
#define DW_I2C_CON_RESTART         BIT(5)
#define DW_I2C_CON_SLAVE_DISABLE   BIT(6)
#define DW_I2C_DATA_READ           BIT(8)
#define DW_I2C_DATA_STOP           BIT(9)
#define DW_I2C_DATA_RESTART        BIT(10)
#define DW_I2C_INTR_RX_FULL        BIT(2)
#define DW_I2C_INTR_TX_ABRT        BIT(6)

#define DW_TIMER_STRIDE            0x14
#define DW_TIMER_LOAD_COUNT        0x00
#define DW_TIMER_CURRENT_VALUE     0x04
#define DW_TIMER_CONTROL           0x08
#define DW_TIMER_EOI               0x0c
#define DW_TIMER_INT_STATUS        0x10
#define DW_TIMERS_INT_STATUS       0xa0
#define DW_TIMERS_EOI              0xa4
#define DW_TIMERS_RAW_INT_STATUS   0xa8
#define DW_TIMERS_COMP_VERSION     0xac
#define DW_TIMER_LOAD_COUNT2(n)    (0xb0 + 4 * (n))
#define DW_TIMER_PROTECTION(n)     (0xd0 + 4 * (n))

#define DW_TIMER_ENABLE            BIT(0)
#define DW_TIMER_PERIODIC          BIT(1)
#define DW_TIMER_INT_MASK          BIT(2)
#define DW_TIMER_PWM               BIT(3)

#define TH1520_MBOX_STATUS         0x000
#define TH1520_MBOX_CLEAR          0x004
#define TH1520_MBOX_MASK           0x00c
#define TH1520_MBOX_GENERATE       0x010
#define TH1520_MBOX_INFO(word)     (0x014 + 4 * (word))
#define TH1520_MBOX_CHANNEL(channel) \
    (TH1520_MBOX_LOCAL_BASE + 0x1000 * (channel))
#define TH1520_MBOX_REMOTE0_CHANNEL (TH1520_MBOX_REMOTE0_BASE + 0x4000)

#define DW_SSI_CTRLR0              0x00
#define DW_SSI_CTRLR1              0x04
#define DW_SSI_SSIENR              0x08
#define DW_SSI_SER                 0x10
#define DW_SSI_BAUDR               0x14
#define DW_SSI_TXFTLR              0x18
#define DW_SSI_RXFTLR              0x1c
#define DW_SSI_TXFLR               0x20
#define DW_SSI_RXFLR               0x24
#define DW_SSI_SR                  0x28
#define DW_SSI_IMR                 0x2c
#define DW_SSI_ISR                 0x30
#define DW_SSI_RISR                0x34
#define DW_SSI_TXOICR              0x38
#define DW_SSI_RXOICR              0x3c
#define DW_SSI_RXUICR              0x40
#define DW_SSI_ICR                 0x48
#define DW_SSI_IDR                 0x58
#define DW_SSI_VERSION             0x5c
#define DW_SSI_DR                  0x60

#define DW_SSI_CTRLR0_DFS_8        7
#define DW_SSI_CTRLR0_TMOD_RO      (2 << 8)
#define DW_SSI_CTRLR0_SRL          BIT(11)

#define DW_SSI_SR_TF_NOT_FULL      BIT(1)
#define DW_SSI_SR_TF_EMPTY         BIT(2)
#define DW_SSI_SR_RF_NOT_EMPTY     BIT(3)

#define DW_SSI_INT_TXEI            BIT(0)
#define DW_SSI_INT_TXOI            BIT(1)
#define DW_SSI_INT_RXUI            BIT(2)
#define DW_SSI_INT_RXOI            BIT(3)
#define DW_SSI_INT_RXFI            BIT(4)

#define TH1520_PWM_STRIDE          0x20
#define TH1520_PWM_CTRL(channel)   \
    (TH1520_PWM_BASE + TH1520_PWM_STRIDE * (channel))
#define TH1520_PWM_PERIOD(channel) (TH1520_PWM_CTRL(channel) + 0x08)
#define TH1520_PWM_FP(channel)     (TH1520_PWM_CTRL(channel) + 0x0c)

#define TH1520_PWM_START            BIT(0)
#define TH1520_PWM_CFG_UPDATE       BIT(2)
#define TH1520_PWM_CONTINUOUS       BIT(5)
#define TH1520_PWM_FPOUT            BIT(8)

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
#define TH1520_AP_RESET_QOM_PATH   "/machine/soc/ap-reset"
#define TH1520_MBOX_QOM_PATH       "/machine/soc/mbox"
#define TH1520_PWM_QOM_PATH        "/machine/soc/pwm"

#define TH1520_UART0_IRQ           36
#define TH1520_UART1_IRQ           37
#define TH1520_UART2_IRQ           38
#define TH1520_UART3_IRQ           39
#define TH1520_UART4_IRQ           40
#define TH1520_UART5_IRQ           41
#define TH1520_GPIO0_IRQ           56
#define TH1520_GPIO1_IRQ           57
#define TH1520_GPIO2_IRQ           58
#define TH1520_GPIO3_IRQ           59
#define TH1520_GPIO4_IRQ           55
#define TH1520_AOGPIO_IRQ          76
#define TH1520_I2C0_IRQ            44
#define TH1520_I2C1_IRQ            45
#define TH1520_I2C2_IRQ            46
#define TH1520_I2C3_IRQ            47
#define TH1520_I2C4_IRQ            48
#define TH1520_I2C5_IRQ            49
#define TH1520_SPI0_IRQ            54
#define TH1520_TIMER0_IRQ          16
#define TH1520_MBOX_IRQ            28
#define TH1520_DMAC0_IRQ           27
#define TH1520_EMMC_IRQ            62
#define TH1520_SDIO0_IRQ           64
#define TH1520_GMAC0_IRQ           66
#define TH1520_GMAC1_IRQ           67
#define TH1520_SDIO1_IRQ           71

#define TH1520_CLK_PERI_APB_PCLK   20
#define TH1520_CLK_PERISYS_APB4    25
#define TH1520_CLK_EMMC_SDIO       43
#define TH1520_CLK_GMAC1           44
#define TH1520_CLK_PADCTRL1        45
#define TH1520_CLK_PADCTRL0        47
#define TH1520_CLK_GMAC_AXI        48
#define TH1520_CLK_GPIO3           49
#define TH1520_CLK_GMAC0           50
#define TH1520_CLK_PWM             51
#define TH1520_CLK_SPI             54
#define TH1520_CLK_UART0_PCLK      55
#define TH1520_CLK_UART1_PCLK      56
#define TH1520_CLK_UART2_PCLK      57
#define TH1520_CLK_UART3_PCLK      58
#define TH1520_CLK_UART4_PCLK      59
#define TH1520_CLK_UART5_PCLK      60
#define TH1520_CLK_GPIO0           61
#define TH1520_CLK_GPIO1           62
#define TH1520_CLK_GPIO2           63
#define TH1520_CLK_I2C0            64
#define TH1520_CLK_I2C1            65
#define TH1520_CLK_I2C2            66
#define TH1520_CLK_I2C3            67
#define TH1520_CLK_I2C4            68
#define TH1520_CLK_I2C5            69
#define TH1520_CLK_MBOX0           72
#define TH1520_CLK_MBOX1           73
#define TH1520_CLK_MBOX2           74
#define TH1520_CLK_MBOX3           75
#define TH1520_CLK_UART_SCLK       85

#define TH1520_I2C_COMP_PARAM1     0x000f0fee
#define TH1520_I2C_COMP_VERSION    0x3230322a
#define TH1520_I2C_COMP_TYPE       0x44570140
#define TH1520_I2C_INTR_RESET      0x000048ff
#define TH1520_I2C_INTR_VALID      0x00004fff
#define BEAGLEV_AHEAD_EEPROM_ADDR  0x50
#define TH1520_TIMER_COMP_VERSION  0x3231322a
#define TH1520_TIMER_TICK_NS       8
#define TH1520_PWM_TICK_NS         8

#define TH1520_PLL_STS             0x080
#define TH1520_C910_CLK_CFG        0x100
#define TH1520_AHB2_CLK_CFG        0x120
#define TH1520_PERISYS_AHB_CFG     0x140
#define TH1520_PERISYS_APB_CFG     0x150
#define TH1520_PERI_CLK_CFG        0x204
#define TH1520_CTRL_CLK_CFG        0x208
#define TH1520_UART_SCLK_CFG       0x210
#define TH1520_PLL_VCO_RST         BIT(29)
#define TH1520_PLL_LOCK_TIME_NS    21250
#define TH1520_PLL_RESET_LOCKS     0x0000039a

#define DWMAC_MAC_CONFIG           0x0000
#define DWMAC_FRAME_FILTER         0x0004
#define DWMAC_MII_ADDR             0x0010
#define DWMAC_MII_DATA             0x0014
#define DWMAC_VERSION              0x0020
#define DWMAC_DMA_BUS_MODE         0x1000
#define DWMAC_DMA_XMT_POLL_DEMAND  0x1004
#define DWMAC_DMA_RX_BASE_ADDR     0x100c
#define DWMAC_DMA_TX_BASE_ADDR     0x1010
#define DWMAC_DMA_STATUS           0x1014
#define DWMAC_DMA_CONTROL          0x1018
#define DWMAC_DMA_INTR_ENA         0x101c
#define DWMAC_DMA_HOST_TX_DESC     0x1048
#define DWMAC_DMA_HOST_RX_DESC     0x104c
#define DWMAC_DMA_HW_FEATURE       0x1058

#define TH1520_GMAC_VERSION_RESET  0x00001037
#define TH1520_GMAC_FEATURE_RESET  0x110d0107
#define TH1520_GMAC_PHY_ADDR       1
#define TH1520_GMAC_PHY_ID1        0x001c
#define TH1520_GMAC_PHY_ID2        0xc878

#define GMAC_APB_CLK_EN            0x00
#define GMAC_APB_RXCLK_DELAY       0x04
#define GMAC_APB_TXCLK_DELAY       0x08
#define GMAC_APB_PLLCLK_DIV        0x0c
#define GMAC_APB_EPHY_DIV          0x10
#define GMAC_APB_PTPCLK_DIV        0x14
#define GMAC_APB_GTXCLK_SEL        0x18
#define GMAC_APB_INTF_CTRL         0x1c
#define GMAC_APB_TXCLK_OEN         0x20

#define GMAC_TEST_DESC_ADDR        0x00100000
#define GMAC_TEST_DATA_ADDR        0x00110000
#define GMAC_ENHANCED_DESC_STRIDE  32
#define GMAC_TEST_TIMEOUT_S        5

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

#define DMAC_ID                    0x000
#define DMAC_COMPONENT_VERSION     0x008
#define DMAC_CFG                   0x010
#define DMAC_CHEN                  0x018
#define DMAC_INTSTATUS             0x030
#define DMAC_RESET                 0x058
#define DMAC_CHANNEL(channel)      (0x100 + (channel) * 0x100)
#define DMAC_CH_SAR(channel)       (DMAC_CHANNEL(channel) + 0x00)
#define DMAC_CH_DAR(channel)       (DMAC_CHANNEL(channel) + 0x08)
#define DMAC_CH_BLOCK_TS(channel)  (DMAC_CHANNEL(channel) + 0x10)
#define DMAC_CH_CTL(channel)       (DMAC_CHANNEL(channel) + 0x18)
#define DMAC_CH_CFG(channel)       (DMAC_CHANNEL(channel) + 0x20)
#define DMAC_CH_LLP(channel)       (DMAC_CHANNEL(channel) + 0x28)
#define DMAC_CH_STATUS(channel)    (DMAC_CHANNEL(channel) + 0x30)
#define DMAC_CH_INTSTATUS_EN(channel) (DMAC_CHANNEL(channel) + 0x80)
#define DMAC_CH_INTSTATUS(channel) (DMAC_CHANNEL(channel) + 0x88)
#define DMAC_CH_INTSIGNAL_EN(channel) (DMAC_CHANNEL(channel) + 0x90)
#define DMAC_CH_INTCLEAR(channel)  (DMAC_CHANNEL(channel) + 0x98)

#define DMAC_CFG_ENABLE            BIT(0)
#define DMAC_CFG_INTERRUPT_ENABLE  BIT(1)
#define DMAC_CH_ENABLE(channel)    BIT(channel)
#define DMAC_CH_ENABLE_WE(channel) BIT((channel) + 8)
#define DMAC_CTL_LLI_LAST          BIT(30)
#define DMAC_CTL_LLI_VALID         BIT(31)
#define DMAC_IRQ_BLOCK_TRANSFER    BIT(0)
#define DMAC_IRQ_DMA_TRANSFER      BIT(1)
#define DMAC_IRQ_LLI_READ_ERROR    BIT(9)
#define DMAC_IRQ_INVALID_ERROR     BIT(13)
#define DMAC_IRQ_ALL_ERRORS        0x003f7fe0U
#define DMAC_COMPONENT_VERSION_RESET 0x3130312a
#define DMAC_BLOCK_TS_MASK         0x003fffffU
#define DMAC_TEST_SOURCE_ADDR      0x00200000
#define DMAC_TEST_DEST_ADDR        0x00210000
#define DMAC_TEST_LLI_ADDR         (TH1520_SRAM_BASE + 0x30000)

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

typedef struct TH1520GMACController {
    const char *name;
    uint64_t base;
    uint64_t apb_base;
    uint32_t irq;
    bool board_enabled;
} TH1520GMACController;

typedef struct TH1520UARTController {
    const char *name;
    uint64_t base;
    uint32_t size;
    uint32_t irq;
    uint32_t pclk_id;
    bool board_enabled;
} TH1520UARTController;

typedef struct TH1520GPIOController {
    const char *name;
    uint64_t base;
    uint32_t irq;
    uint32_t ngpios;
    int32_t clock_id;
    uint32_t pad_group;
    uint32_t nranges;
    struct {
        uint32_t gpio_offset;
        uint32_t pin_offset;
        uint32_t count;
    } ranges[2];
} TH1520GPIOController;

typedef struct TH1520PadCtrl {
    const char *name;
    uint64_t base;
    uint32_t size;
    uint32_t group;
    int32_t clock_id;
} TH1520PadCtrl;

typedef struct TH1520I2CController {
    const char *name;
    uint64_t base;
    uint32_t irq;
    uint32_t clock_id;
    bool board_enabled;
} TH1520I2CController;

typedef struct TH1520Timer {
    const char *name;
    uint64_t base;
    uint64_t component_base;
    uint32_t channel;
    uint32_t irq;
} TH1520Timer;

typedef struct TH1520SPIController {
    uint64_t base;
    uint32_t irq;
    uint32_t clock_id;
} TH1520SPIController;

static const DWCMSHCController dwcmshc_controllers[] = {
    { "emmc",  TH1520_EMMC_BASE,  TH1520_EMMC_IRQ,  8 },
    { "sdio0", TH1520_SDIO0_BASE, TH1520_SDIO0_IRQ, 4 },
    { "sdio1", TH1520_SDIO1_BASE, TH1520_SDIO1_IRQ, 4 },
};

static const TH1520GMACController th1520_gmac_controllers[] = {
    { "gmac0", TH1520_GMAC0_BASE, TH1520_GMAC0_APB_BASE,
      TH1520_GMAC0_IRQ, true },
    { "gmac1", TH1520_GMAC1_BASE, TH1520_GMAC1_APB_BASE,
      TH1520_GMAC1_IRQ, false },
};

static const TH1520UARTController th1520_uart_controllers[] = {
    { "uart0", TH1520_UART0_BASE, 0x100,  TH1520_UART0_IRQ,
      TH1520_CLK_UART0_PCLK, true },
    { "uart1", TH1520_UART1_BASE, 0x100,  TH1520_UART1_IRQ,
      TH1520_CLK_UART1_PCLK, false },
    { "uart2", TH1520_UART2_BASE, 0x4000, TH1520_UART2_IRQ,
      TH1520_CLK_UART2_PCLK, false },
    { "uart3", TH1520_UART3_BASE, 0x100,  TH1520_UART3_IRQ,
      TH1520_CLK_UART3_PCLK, false },
    { "uart4", TH1520_UART4_BASE, 0x4000, TH1520_UART4_IRQ,
      TH1520_CLK_UART4_PCLK, false },
    { "uart5", TH1520_UART5_BASE, 0x4000, TH1520_UART5_IRQ,
      TH1520_CLK_UART5_PCLK, false },
};

static const TH1520GPIOController th1520_gpio_controllers[] = {
    { "gpio0",  TH1520_GPIO0_BASE,  TH1520_GPIO0_IRQ,  32,
      TH1520_CLK_GPIO0, 2, 1, { { 0, 0, 32 } } },
    { "gpio1",  TH1520_GPIO1_BASE,  TH1520_GPIO1_IRQ,  31,
      TH1520_CLK_GPIO1, 2, 1, { { 0, 32, 31 } } },
    { "gpio2",  TH1520_GPIO2_BASE,  TH1520_GPIO2_IRQ,  32,
      TH1520_CLK_GPIO2, 3, 1, { { 0, 0, 32 } } },
    { "gpio3",  TH1520_GPIO3_BASE,  TH1520_GPIO3_IRQ,  23,
      TH1520_CLK_GPIO3, 3, 1, { { 0, 32, 23 } } },
    { "gpio4",  TH1520_GPIO4_BASE,  TH1520_GPIO4_IRQ,  23, -1,
      1, 2, { { 0, 25, 22 }, { 22, 7, 1 } } },
    { "aogpio", TH1520_AOGPIO_BASE, TH1520_AOGPIO_IRQ, 16, -1,
      1, 1, { { 0, 9, 16 } } },
};

static const TH1520PadCtrl th1520_padctrls[] = {
    { "padctrl-aosys", TH1520_PADCTRL_AOSYS_BASE, 0x2000, 1, -1 },
    { "padctrl1-apsys", TH1520_PADCTRL1_APSYS_BASE, 0x1000, 2,
      TH1520_CLK_PADCTRL1 },
    { "padctrl0-apsys", TH1520_PADCTRL0_APSYS_BASE, 0x1000, 3,
      TH1520_CLK_PADCTRL0 },
};

static const TH1520I2CController th1520_i2c_controllers[] = {
    { "i2c0", TH1520_I2C0_BASE, TH1520_I2C0_IRQ, TH1520_CLK_I2C0, true },
    { "i2c1", TH1520_I2C1_BASE, TH1520_I2C1_IRQ, TH1520_CLK_I2C1, false },
    { "i2c2", TH1520_I2C2_BASE, TH1520_I2C2_IRQ, TH1520_CLK_I2C2, false },
    { "i2c3", TH1520_I2C3_BASE, TH1520_I2C3_IRQ, TH1520_CLK_I2C3, false },
    { "i2c4", TH1520_I2C4_BASE, TH1520_I2C4_IRQ, TH1520_CLK_I2C4, false },
    { "i2c5", TH1520_I2C5_BASE, TH1520_I2C5_IRQ, TH1520_CLK_I2C5, false },
};

static const TH1520SPIController th1520_spi0 = {
    TH1520_SPI0_BASE, TH1520_SPI0_IRQ, TH1520_CLK_SPI,
};

static const TH1520Timer th1520_timers[] = {
    { "timer0", TH1520_TIMER0_3_BASE + 0 * DW_TIMER_STRIDE,
      TH1520_TIMER0_3_BASE, 0, TH1520_TIMER0_IRQ + 0 },
    { "timer1", TH1520_TIMER0_3_BASE + 1 * DW_TIMER_STRIDE,
      TH1520_TIMER0_3_BASE, 1, TH1520_TIMER0_IRQ + 1 },
    { "timer2", TH1520_TIMER0_3_BASE + 2 * DW_TIMER_STRIDE,
      TH1520_TIMER0_3_BASE, 2, TH1520_TIMER0_IRQ + 2 },
    { "timer3", TH1520_TIMER0_3_BASE + 3 * DW_TIMER_STRIDE,
      TH1520_TIMER0_3_BASE, 3, TH1520_TIMER0_IRQ + 3 },
    { "timer4", TH1520_TIMER4_7_BASE + 0 * DW_TIMER_STRIDE,
      TH1520_TIMER4_7_BASE, 0, TH1520_TIMER0_IRQ + 4 },
    { "timer5", TH1520_TIMER4_7_BASE + 1 * DW_TIMER_STRIDE,
      TH1520_TIMER4_7_BASE, 1, TH1520_TIMER0_IRQ + 5 },
    { "timer6", TH1520_TIMER4_7_BASE + 2 * DW_TIMER_STRIDE,
      TH1520_TIMER4_7_BASE, 2, TH1520_TIMER0_IRQ + 6 },
    { "timer7", TH1520_TIMER4_7_BASE + 3 * DW_TIMER_STRIDE,
      TH1520_TIMER4_7_BASE, 3, TH1520_TIMER0_IRQ + 7 },
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

static void assert_fdt_mmio(const void *fdt, int node, uint64_t base,
                            uint32_t size)
{
    const fdt32_t *cells;
    int len;

    cells = fdt_getprop(fdt, node, "reg", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 4 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, base >> 32);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, (uint32_t)base);
    g_assert_cmphex(fdt32_to_cpu(cells[2]), ==, 0);
    g_assert_cmphex(fdt32_to_cpu(cells[3]), ==, size);
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

static void assert_fdt_stringlist(const void *fdt, int node, const char *name,
                                  const char *const *expected, size_t count)
{
    int len;
    const char *prop = fdt_getprop(fdt, node, name, &len);

    g_assert_nonnull(prop);
    g_assert_cmpint(fdt_stringlist_count(fdt, node, name), ==, count);
    for (size_t i = 0; i < count; i++) {
        const char *value = fdt_stringlist_get(fdt, node, name, i, &len);

        g_assert_nonnull(value);
        g_assert_cmpstr(value, ==, expected[i]);
    }
}

enum {
    PIN_ASSERT_BIAS_DISABLE    = BIT(0),
    PIN_ASSERT_BIAS_PULL_UP    = BIT(1),
    PIN_ASSERT_INPUT_ENABLE    = BIT(2),
    PIN_ASSERT_INPUT_DISABLE   = BIT(3),
    PIN_ASSERT_SCHMITT_ENABLE  = BIT(4),
    PIN_ASSERT_SCHMITT_DISABLE = BIT(5),
};

static uint32_t assert_padctrl_fdt(const void *fdt,
                                   const TH1520PadCtrl *controller,
                                   uint32_t ap_clock_phandle,
                                   uint32_t aonsys_clock_phandle)
{
    g_autofree char *path =
        g_strdup_printf("/soc/pinctrl@%" PRIx64, controller->base);
    const fdt32_t *cells;
    const char *text;
    uint32_t phandle;
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "thead,th1520-pinctrl");
    assert_fdt_mmio(fdt, node, controller->base, controller->size);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "thead,pad-group"), ==,
                    controller->group);

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    if (controller->clock_id < 0) {
        g_assert_cmpint(len, ==, sizeof(*cells));
        g_assert_cmphex(fdt32_to_cpu(cells[0]), ==,
                        aonsys_clock_phandle);
    } else {
        g_assert_cmpint(len, ==, 2 * sizeof(*cells));
        g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, ap_clock_phandle);
        g_assert_cmphex(fdt32_to_cpu(cells[1]), ==,
                        controller->clock_id);
    }

    phandle = fdt_get_phandle(fdt, node);
    g_assert_cmphex(phandle, !=, 0);
    return phandle;
}

static void assert_pin_config_fdt(const void *fdt, const char *path,
                                  const char *const *pins, size_t npins,
                                  const char *function,
                                  uint32_t drive_strength, uint32_t flags)
{
    const char *text;
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    assert_fdt_stringlist(fdt, node, "pins", pins, npins);
    text = fdt_getprop(fdt, node, "function", &len);
    if (function) {
        g_assert_nonnull(text);
        g_assert_cmpstr(text, ==, function);
    } else {
        g_assert_null(text);
        g_assert_cmpint(len, ==, -FDT_ERR_NOTFOUND);
    }
    g_assert_cmphex(fdt_prop_u32(fdt, node, "drive-strength"), ==,
                    drive_strength);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "slew-rate"), ==, 0);
    assert_fdt_bool(fdt, node, "bias-disable",
                    flags & PIN_ASSERT_BIAS_DISABLE);
    assert_fdt_bool(fdt, node, "bias-pull-up",
                    flags & PIN_ASSERT_BIAS_PULL_UP);
    assert_fdt_bool(fdt, node, "input-enable",
                    flags & PIN_ASSERT_INPUT_ENABLE);
    assert_fdt_bool(fdt, node, "input-disable",
                    flags & PIN_ASSERT_INPUT_DISABLE);
    assert_fdt_bool(fdt, node, "input-schmitt-enable",
                    flags & PIN_ASSERT_SCHMITT_ENABLE);
    assert_fdt_bool(fdt, node, "input-schmitt-disable",
                    flags & PIN_ASSERT_SCHMITT_DISABLE);
}

static void assert_board_pinctrl_fdt(const void *fdt,
                                     uint32_t *led_phandle,
                                     uint32_t *gmac0_phandle,
                                     uint32_t *uart0_phandle,
                                     uint32_t *wifi_phandle)
{
    static const char *const leds[] = {
        "AUDIO_PA8", "AUDIO_PA9", "AUDIO_PA10", "AUDIO_PA11",
        "AUDIO_PA12",
    };
    static const char *const gmac_tx[] = {
        "GMAC0_TX_CLK", "GMAC0_TXEN", "GMAC0_TXD0", "GMAC0_TXD1",
        "GMAC0_TXD2", "GMAC0_TXD3",
    };
    static const char *const gmac_rx[] = {
        "GMAC0_RX_CLK", "GMAC0_RXDV", "GMAC0_RXD0", "GMAC0_RXD1",
        "GMAC0_RXD2", "GMAC0_RXD3",
    };
    static const char *const gmac_mdc[] = { "GMAC0_MDC" };
    static const char *const gmac_mdio[] = { "GMAC0_MDIO" };
    static const char *const gmac_reset[] = { "GMAC0_COL" };
    static const char *const gmac_irq[] = { "GMAC0_CRS" };
    static const char *const uart_tx[] = { "UART0_TXD" };
    static const char *const uart_rx[] = { "UART0_RXD" };
    static const char *const wifi_wake[] = { "GPIO2_25" };
    static const char *const wifi_reg_on[] = { "GPIO2_31" };
    const uint32_t output_flags = PIN_ASSERT_BIAS_DISABLE |
                                  PIN_ASSERT_INPUT_DISABLE |
                                  PIN_ASSERT_SCHMITT_DISABLE;
    const uint32_t input_flags = PIN_ASSERT_BIAS_DISABLE |
                                 PIN_ASSERT_INPUT_ENABLE |
                                 PIN_ASSERT_SCHMITT_DISABLE;
    const uint32_t pulled_input_flags = PIN_ASSERT_BIAS_PULL_UP |
                                        PIN_ASSERT_INPUT_ENABLE |
                                        PIN_ASSERT_SCHMITT_ENABLE;
    int node;

    node = fdt_path_offset(fdt, "/soc/pinctrl@fffff4a000/led-0");
    g_assert_cmpint(node, >=, 0);
    *led_phandle = fdt_get_phandle(fdt, node);
    g_assert_cmphex(*led_phandle, !=, 0);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@fffff4a000/led-0/led-pins", leds,
        ARRAY_SIZE(leds), NULL, 3, output_flags);

    node = fdt_path_offset(fdt, "/soc/pinctrl@ffec007000/gmac0-0");
    g_assert_cmpint(node, >=, 0);
    *gmac0_phandle = fdt_get_phandle(fdt, node);
    g_assert_cmphex(*gmac0_phandle, !=, 0);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@ffec007000/gmac0-0/tx-pins", gmac_tx,
        ARRAY_SIZE(gmac_tx), "gmac0", 25, output_flags);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@ffec007000/gmac0-0/rx-pins", gmac_rx,
        ARRAY_SIZE(gmac_rx), "gmac0", 1, input_flags);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@ffec007000/gmac0-0/mdc-pins", gmac_mdc,
        ARRAY_SIZE(gmac_mdc), "gmac0", 13, output_flags);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@ffec007000/gmac0-0/mdio-pins", gmac_mdio,
        ARRAY_SIZE(gmac_mdio), "gmac0", 13,
        PIN_ASSERT_BIAS_DISABLE | PIN_ASSERT_INPUT_ENABLE |
        PIN_ASSERT_SCHMITT_ENABLE);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@ffec007000/gmac0-0/phy-reset-pins", gmac_reset,
        ARRAY_SIZE(gmac_reset), NULL, 3, output_flags);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@ffec007000/gmac0-0/phy-interrupt-pins", gmac_irq,
        ARRAY_SIZE(gmac_irq), "gpio", 1, pulled_input_flags);

    node = fdt_path_offset(fdt, "/soc/pinctrl@ffec007000/uart0-0");
    g_assert_cmpint(node, >=, 0);
    *uart0_phandle = fdt_get_phandle(fdt, node);
    g_assert_cmphex(*uart0_phandle, !=, 0);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@ffec007000/uart0-0/tx-pins", uart_tx,
        ARRAY_SIZE(uart_tx), "uart", 3, output_flags);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@ffec007000/uart0-0/rx-pins", uart_rx,
        ARRAY_SIZE(uart_rx), "uart", 1, pulled_input_flags);

    node = fdt_path_offset(fdt, "/soc/pinctrl@ffec007000/wifi-0");
    g_assert_cmpint(node, >=, 0);
    *wifi_phandle = fdt_get_phandle(fdt, node);
    g_assert_cmphex(*wifi_phandle, !=, 0);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@ffec007000/wifi-0/host-wake-pins", wifi_wake,
        ARRAY_SIZE(wifi_wake), "gpio", 1, input_flags);
    assert_pin_config_fdt(fdt,
        "/soc/pinctrl@ffec007000/wifi-0/reg-on-pins", wifi_reg_on,
        ARRAY_SIZE(wifi_reg_on), "gpio", 3, output_flags);
}

static void assert_pinctrl_reference(const void *fdt, const char *path,
                                     uint32_t expected_phandle)
{
    const char *text;
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "pinctrl-names", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "default");
    g_assert_cmphex(fdt_prop_u32(fdt, node, "pinctrl-0"), ==,
                    expected_phandle);
}

static void assert_gmac_fdt(const void *fdt,
                            const TH1520GMACController *controller,
                            uint32_t clock_phandle,
                            uint32_t axi_phandle)
{
    static const char *const compat[] = {
        "thead,th1520-gmac", "snps,dwmac-3.70a",
    };
    static const char *const reg_names[] = { "dwmac", "apb" };
    static const char *const clock_names[] = {
        "stmmaceth", "pclk", "apb",
    };
    g_autofree char *path =
        g_strdup_printf("/soc/ethernet@%" PRIx64, controller->base);
    g_autofree char *mdio_path = g_strdup_printf("%s/mdio", path);
    const fdt32_t *cells;
    const void *mac;
    const char *text;
    int mdio;
    int node;
    int len;

    node = fdt_path_offset(fdt, path);
    g_assert_cmpint(node, >=, 0);
    assert_fdt_stringlist(fdt, node, "compatible", compat,
                          ARRAY_SIZE(compat));
    assert_fdt_stringlist(fdt, node, "reg-names", reg_names,
                          ARRAY_SIZE(reg_names));
    assert_fdt_stringlist(fdt, node, "clock-names", clock_names,
                          ARRAY_SIZE(clock_names));

    cells = fdt_getprop(fdt, node, "reg", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 8 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, controller->base >> 32);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==,
                    (uint32_t)controller->base);
    g_assert_cmphex(fdt32_to_cpu(cells[2]), ==, 0);
    g_assert_cmphex(fdt32_to_cpu(cells[3]), ==, 0x2000);
    g_assert_cmphex(fdt32_to_cpu(cells[4]), ==, controller->apb_base >> 32);
    g_assert_cmphex(fdt32_to_cpu(cells[5]), ==,
                    (uint32_t)controller->apb_base);
    g_assert_cmphex(fdt32_to_cpu(cells[6]), ==, 0);
    g_assert_cmphex(fdt32_to_cpu(cells[7]), ==, 0x1000);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, controller->irq);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 6 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, TH1520_CLK_GMAC_AXI);
    g_assert_cmphex(fdt32_to_cpu(cells[2]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[3]), ==,
                    controller->base == TH1520_GMAC0_BASE ?
                    TH1520_CLK_GMAC0 : TH1520_CLK_GMAC1);
    g_assert_cmphex(fdt32_to_cpu(cells[4]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[5]), ==, TH1520_CLK_PERISYS_APB4);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "snps,pbl"), ==, 32);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "snps,multicast-filter-bins"),
                    ==, 64);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "snps,perfect-filter-entries"),
                    ==, 32);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "snps,axi-config"), ==,
                    axi_phandle);
    assert_fdt_bool(fdt, node, "snps,fixed-burst", true);

    mac = fdt_getprop(fdt, node, "local-mac-address", &len);
    g_assert_nonnull(mac);
    g_assert_cmpint(len, ==, 6);

    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, controller->board_enabled ? "okay" : "disabled");

    mdio = fdt_path_offset(fdt, mdio_path);
    g_assert_cmpint(mdio, >=, 0);
    text = fdt_getprop(fdt, mdio, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "snps,dwmac-mdio");
    g_assert_cmphex(fdt_prop_u32(fdt, mdio, "#address-cells"), ==, 1);
    g_assert_cmphex(fdt_prop_u32(fdt, mdio, "#size-cells"), ==, 0);

    if (controller->board_enabled) {
        g_autofree char *phy_path =
            g_strdup_printf("%s/ethernet-phy@1", mdio_path);
        const char *alias = fdt_get_alias(fdt, "ethernet0");
        int phy = fdt_path_offset(fdt, phy_path);

        g_assert_cmpint(phy, >=, 0);
        g_assert_cmphex(fdt_prop_u32(fdt, phy, "reg"), ==,
                        TH1520_GMAC_PHY_ADDR);
        g_assert_cmphex(fdt_prop_u32(fdt, node, "phy-handle"), ==,
                        fdt_get_phandle(fdt, phy));
        text = fdt_getprop(fdt, node, "phy-mode", &len);
        g_assert_nonnull(text);
        g_assert_cmpstr(text, ==, "rgmii-id");
        g_assert_nonnull(alias);
        g_assert_cmpstr(alias, ==, path);
    } else {
        g_assert_null(fdt_getprop(fdt, node, "phy-handle", &len));
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

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, TH1520_CLK_EMMC_SDIO);
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

static void assert_dmac_fdt(const void *fdt, uint32_t clock_phandle)
{
    static const char *const clock_names[] = { "core-clk", "cfgr-clk" };
    const char *const path = "/soc/dma-controller@ffefc00000";
    const fdt32_t *cells;
    const char *text;
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "snps,axi-dma-1.01a");
    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "okay");
    assert_fdt_stringlist(fdt, node, "clock-names", clock_names,
                          ARRAY_SIZE(clock_names));

    cells = fdt_getprop(fdt, node, "reg", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 4 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, TH1520_DMAC0_BASE >> 32);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==,
                    (uint32_t)TH1520_DMAC0_BASE);
    g_assert_cmphex(fdt32_to_cpu(cells[2]), ==, 0);
    g_assert_cmphex(fdt32_to_cpu(cells[3]), ==, 0x1000);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, TH1520_DMAC0_IRQ);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 4 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, TH1520_CLK_PERI_APB_PCLK);
    g_assert_cmphex(fdt32_to_cpu(cells[2]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[3]), ==, TH1520_CLK_PERI_APB_PCLK);

    g_assert_cmphex(fdt_prop_u32(fdt, node, "#dma-cells"), ==, 1);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "dma-channels"), ==, 4);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "snps,dma-masters"), ==, 1);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "snps,data-width"), ==, 4);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "snps,axi-max-burst-len"), ==,
                    16);

    cells = fdt_getprop(fdt, node, "snps,block-size", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 4 * sizeof(*cells));
    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmphex(fdt32_to_cpu(cells[i]), ==, 65536);
    }

    cells = fdt_getprop(fdt, node, "snps,priority", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 4 * sizeof(*cells));
    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmphex(fdt32_to_cpu(cells[i]), ==, i);
    }
}

static void assert_uart_fdt(const void *fdt,
                            const TH1520UARTController *controller,
                            uint32_t clock_phandle)
{
    static const char *const clock_names[] = { "baudclk", "apb_pclk" };
    g_autofree char *path =
        g_strdup_printf("/soc/serial@%" PRIx64, controller->base);
    const fdt32_t *cells;
    const char *text;
    char alias[8];
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "snps,dw-apb-uart");
    assert_fdt_mmio(fdt, node, controller->base, controller->size);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, controller->irq);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 4 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, TH1520_CLK_UART_SCLK);
    g_assert_cmphex(fdt32_to_cpu(cells[2]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[3]), ==, controller->pclk_id);
    assert_fdt_stringlist(fdt, node, "clock-names", clock_names,
                          ARRAY_SIZE(clock_names));
    g_assert_cmphex(fdt_prop_u32(fdt, node, "reg-shift"), ==, 2);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "reg-io-width"), ==, 4);

    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, controller->board_enabled ?
                    "okay" : "disabled");
    snprintf(alias, sizeof(alias), "serial%u",
             (unsigned)(controller - th1520_uart_controllers));
    g_assert_cmpstr(fdt_get_alias(fdt, alias), ==, path);
}

static void assert_i2c_fdt(const void *fdt,
                           const TH1520I2CController *controller,
                           uint32_t clock_phandle)
{
    static const char *const compatibles[] = {
        "thead,th1520-i2c", "snps,designware-i2c"
    };
    g_autofree char *path =
        g_strdup_printf("/soc/i2c@%" PRIx64, controller->base);
    g_autofree char *eeprom_path =
        g_strdup_printf("%s/eeprom@%x", path, BEAGLEV_AHEAD_EEPROM_ADDR);
    const fdt32_t *cells;
    const char *text;
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    assert_fdt_stringlist(fdt, node, "compatible", compatibles,
                          ARRAY_SIZE(compatibles));
    assert_fdt_mmio(fdt, node, controller->base, 0x4000);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#address-cells"), ==, 1);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#size-cells"), ==, 0);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, controller->irq);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, controller->clock_id);

    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, controller->board_enabled ?
                    "okay" : "disabled");

    node = fdt_path_offset(fdt, eeprom_path);
    if (!controller->board_enabled) {
        g_assert_cmpint(node, ==, -FDT_ERR_NOTFOUND);
        return;
    }

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "atmel,24c32");
    g_assert_cmphex(fdt_prop_u32(fdt, node, "reg"), ==,
                    BEAGLEV_AHEAD_EEPROM_ADDR);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "pagesize"), ==, 32);
}

static void assert_timer_fdt(const void *fdt, const TH1520Timer *timer,
                             uint32_t clock_phandle)
{
    g_autofree char *path =
        g_strdup_printf("/soc/timer@%" PRIx64, timer->base);
    const fdt32_t *cells;
    const char *text;
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "snps,dw-apb-timer");
    assert_fdt_mmio(fdt, node, timer->base, DW_TIMER_STRIDE);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, timer->irq);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==,
                    TH1520_CLK_PERI_APB_PCLK);
    text = fdt_getprop(fdt, node, "clock-names", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "timer");
    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "disabled");
}

static void assert_spi_fdt(const void *fdt,
                           const TH1520SPIController *controller,
                           uint32_t clock_phandle)
{
    static const char *const compatibles[] = {
        "thead,th1520-spi", "snps,dw-apb-ssi"
    };
    const fdt32_t *cells;
    const char *text;
    const char *const path = "/soc/spi@ffe700c000";
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    assert_fdt_stringlist(fdt, node, "compatible", compatibles,
                          ARRAY_SIZE(compatibles));
    assert_fdt_mmio(fdt, node, controller->base, 0x1000);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#address-cells"), ==, 1);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#size-cells"), ==, 0);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, controller->irq);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, controller->clock_id);

    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "disabled");
    g_assert_cmpstr(fdt_get_alias(fdt, "spi0"), ==, path);
}

static void assert_pwm_fdt(const void *fdt, uint32_t clock_phandle)
{
    const fdt32_t *cells;
    const char *text;
    const char *const path = "/soc/pwm@ffec01c000";
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "thead,th1520-pwm");
    assert_fdt_mmio(fdt, node, TH1520_PWM_BASE, 0x4000);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#pwm-cells"), ==, 3);

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, TH1520_CLK_PWM);

    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_null(text);
    g_assert_cmpint(len, ==, -FDT_ERR_NOTFOUND);
}

static void assert_mbox_fdt(const void *fdt, uint32_t clock_phandle)
{
    static const char *const reg_names[] = {
        "local", "remote-icu0", "remote-icu1", "remote-icu2"
    };
    static const char *const clock_names[] = {
        "clk-local", "clk-remote-icu0", "clk-remote-icu1",
        "clk-remote-icu2"
    };
    static const uint64_t bases[] = {
        TH1520_MBOX_LOCAL_BASE,
        TH1520_MBOX_REMOTE0_BASE,
        TH1520_MBOX_REMOTE1_BASE,
        TH1520_MBOX_REMOTE2_BASE,
    };
    static const uint32_t sizes[] = { 0x6000, 0x6000, 0x2000, 0x2000 };
    static const uint32_t clock_ids[] = {
        TH1520_CLK_MBOX0,
        TH1520_CLK_MBOX1,
        TH1520_CLK_MBOX2,
        TH1520_CLK_MBOX3,
    };
    const fdt32_t *cells;
    const char *text;
    int node = fdt_path_offset(fdt, "/soc/mailbox@ffffc38000");
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "thead,th1520-mbox");
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#mbox-cells"), ==, 1);
    assert_fdt_stringlist(fdt, node, "reg-names", reg_names,
                          ARRAY_SIZE(reg_names));
    assert_fdt_stringlist(fdt, node, "clock-names", clock_names,
                          ARRAY_SIZE(clock_names));

    cells = fdt_getprop(fdt, node, "reg", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, ARRAY_SIZE(bases) * 4 * sizeof(*cells));
    for (size_t i = 0; i < ARRAY_SIZE(bases); i++) {
        g_assert_cmphex(fdt32_to_cpu(cells[i * 4]), ==, bases[i] >> 32);
        g_assert_cmphex(fdt32_to_cpu(cells[i * 4 + 1]), ==,
                        (uint32_t)bases[i]);
        g_assert_cmphex(fdt32_to_cpu(cells[i * 4 + 2]), ==, 0);
        g_assert_cmphex(fdt32_to_cpu(cells[i * 4 + 3]), ==, sizes[i]);
    }

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, ARRAY_SIZE(clock_ids) * 2 * sizeof(*cells));
    for (size_t i = 0; i < ARRAY_SIZE(clock_ids); i++) {
        g_assert_cmphex(fdt32_to_cpu(cells[i * 2]), ==, clock_phandle);
        g_assert_cmphex(fdt32_to_cpu(cells[i * 2 + 1]), ==, clock_ids[i]);
    }

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, TH1520_MBOX_IRQ);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);
    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_null(text);
    g_assert_cmpint(len, ==, -FDT_ERR_NOTFOUND);
}

static uint32_t assert_gpio_fdt(const void *fdt,
                                const TH1520GPIOController *controller,
                                uint32_t clock_phandle,
                                const uint32_t *padctrl_phandles)
{
    g_autofree char *path =
        g_strdup_printf("/soc/gpio@%" PRIx64, controller->base);
    g_autofree char *port =
        g_strdup_printf("%s/gpio-controller@0", path);
    const fdt32_t *cells;
    const char *text;
    char alias[8];
    uint32_t phandle;
    int node;
    int len;

    node = fdt_path_offset(fdt, path);
    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "snps,dw-apb-gpio");
    assert_fdt_mmio(fdt, node, controller->base, 0x1000);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#address-cells"), ==, 1);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#size-cells"), ==, 0);

    cells = fdt_getprop(fdt, node, "clocks", &len);
    if (controller->clock_id >= 0) {
        g_assert_nonnull(cells);
        g_assert_cmpint(len, ==, 2 * sizeof(*cells));
        g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
        g_assert_cmphex(fdt32_to_cpu(cells[1]), ==,
                        controller->clock_id);
        text = fdt_getprop(fdt, node, "clock-names", &len);
        g_assert_nonnull(text);
        g_assert_cmpstr(text, ==, "bus");
    } else {
        g_assert_null(cells);
        g_assert_cmpint(len, ==, -FDT_ERR_NOTFOUND);
        g_assert_null(fdt_getprop(fdt, node, "clock-names", &len));
        g_assert_cmpint(len, ==, -FDT_ERR_NOTFOUND);
    }

    node = fdt_path_offset(fdt, port);
    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "snps,dw-apb-gpio-port");
    g_assert_cmphex(fdt_prop_u32(fdt, node, "reg"), ==, 0);
    assert_fdt_bool(fdt, node, "gpio-controller", true);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#gpio-cells"), ==, 2);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "ngpios"), ==,
                    controller->ngpios);
    assert_fdt_bool(fdt, node, "interrupt-controller", true);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#interrupt-cells"), ==, 2);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, controller->irq);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);
    cells = fdt_getprop(fdt, node, "gpio-ranges", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==,
                    controller->nranges * 4 * sizeof(*cells));
    for (uint32_t range = 0; range < controller->nranges; range++) {
        g_assert_cmphex(fdt32_to_cpu(cells[range * 4]), ==,
                        padctrl_phandles[controller->pad_group - 1]);
        g_assert_cmphex(fdt32_to_cpu(cells[range * 4 + 1]), ==,
                        controller->ranges[range].gpio_offset);
        g_assert_cmphex(fdt32_to_cpu(cells[range * 4 + 2]), ==,
                        controller->ranges[range].pin_offset);
        g_assert_cmphex(fdt32_to_cpu(cells[range * 4 + 3]), ==,
                        controller->ranges[range].count);
    }

    phandle = fdt_get_phandle(fdt, node);
    g_assert_cmphex(phandle, !=, 0);
    snprintf(alias, sizeof(alias), "gpio%u",
             (unsigned)(controller - th1520_gpio_controllers));
    g_assert_cmpstr(fdt_get_alias(fdt, alias), ==, port);

    return phandle;
}

static void assert_led_fdt(const void *fdt, uint32_t gpio4_phandle)
{
    const fdt32_t *cells;
    const char *text;
    int node = fdt_path_offset(fdt, "/leds");
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "gpio-leds");

    for (unsigned int i = 0; i < 5; i++) {
        g_autofree char *path = g_strdup_printf("/leds/led-%u", i + 1);
        g_autofree char *label = g_strdup_printf("led%u", i + 1);

        node = fdt_path_offset(fdt, path);
        g_assert_cmpint(node, >=, 0);
        cells = fdt_getprop(fdt, node, "gpios", &len);
        g_assert_nonnull(cells);
        g_assert_cmpint(len, ==, 3 * sizeof(*cells));
        g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, gpio4_phandle);
        g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 8 + i);
        g_assert_cmphex(fdt32_to_cpu(cells[2]), ==, 0);
        g_assert_cmphex(fdt_prop_u32(fdt, node, "color"), ==, 3);
        text = fdt_getprop(fdt, node, "label", &len);
        g_assert_nonnull(text);
        g_assert_cmpstr(text, ==, label);
    }
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
    uint32_t osc_phandle;
    uint32_t aonsys_clock_phandle;
    uint32_t ap_clock_phandle;
    uint32_t padctrl_phandles[ARRAY_SIZE(th1520_padctrls)];
    uint32_t led_pins_phandle;
    uint32_t gmac0_pins_phandle;
    uint32_t uart0_pins_phandle;
    uint32_t wifi_pins_phandle;
    uint32_t gpio_phandles[ARRAY_SIZE(th1520_gpio_controllers)];
    uint32_t stmmac_axi_phandle;
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

    clock_offset = fdt_path_offset(fdt, "/oscillator");
    g_assert_cmpint(clock_offset, >=, 0);
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "clock-frequency"), ==,
                    24000000);
    osc_phandle = fdt_get_phandle(fdt, clock_offset);
    g_assert_cmphex(osc_phandle, !=, 0);

    clock_offset = fdt_path_offset(fdt, "/clock-73728000");
    g_assert_cmpint(clock_offset, >=, 0);
    compatible = fdt_getprop(fdt, clock_offset, "compatible", &len);
    g_assert_nonnull(compatible);
    g_assert_cmpstr(compatible, ==, "fixed-clock");
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "#clock-cells"), ==,
                    0);
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "clock-frequency"), ==,
                    73728000);
    compatible = fdt_getprop(fdt, clock_offset, "clock-output-names", &len);
    g_assert_nonnull(compatible);
    g_assert_cmpstr(compatible, ==, "aonsys_clk");
    aonsys_clock_phandle = fdt_get_phandle(fdt, clock_offset);
    g_assert_cmphex(aonsys_clock_phandle, !=, 0);

    clock_offset = fdt_path_offset(fdt,
                                   "/soc/clock-controller@ffef010000");
    g_assert_cmpint(clock_offset, >=, 0);
    compatible = fdt_getprop(fdt, clock_offset, "compatible", &len);
    g_assert_nonnull(compatible);
    g_assert_cmpstr(compatible, ==, "thead,th1520-clk-ap");
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "#clock-cells"), ==, 1);
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "clocks"), ==,
                    osc_phandle);
    assert_fdt_mmio(fdt, clock_offset, TH1520_AP_CLOCK_BASE, 0x1000);
    ap_clock_phandle = fdt_get_phandle(fdt, clock_offset);
    g_assert_cmphex(ap_clock_phandle, !=, 0);

    clock_offset = fdt_path_offset(fdt,
                                   "/soc/reset-controller@ffef014000");
    g_assert_cmpint(clock_offset, >=, 0);
    compatible = fdt_getprop(fdt, clock_offset, "compatible", &len);
    g_assert_nonnull(compatible);
    g_assert_cmpstr(compatible, ==, "thead,th1520-reset-ap");
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "#reset-cells"), ==, 1);
    assert_fdt_mmio(fdt, clock_offset, TH1520_AP_RESET_BASE, 0x1000);
    g_assert_cmphex(fdt_get_phandle(fdt, clock_offset), !=, 0);

    assert_dmac_fdt(fdt, ap_clock_phandle);

    for (size_t i = 0; i < ARRAY_SIZE(th1520_padctrls); i++) {
        padctrl_phandles[i] = assert_padctrl_fdt(
            fdt, &th1520_padctrls[i], ap_clock_phandle,
            aonsys_clock_phandle);
    }
    assert_board_pinctrl_fdt(fdt, &led_pins_phandle,
                             &gmac0_pins_phandle, &uart0_pins_phandle,
                             &wifi_pins_phandle);

    for (size_t i = 0; i < ARRAY_SIZE(dwcmshc_controllers); i++) {
        assert_dwcmshc_fdt(fdt, &dwcmshc_controllers[i],
                           ap_clock_phandle);
    }

    for (size_t i = 0; i < ARRAY_SIZE(th1520_uart_controllers); i++) {
        assert_uart_fdt(fdt, &th1520_uart_controllers[i],
                        ap_clock_phandle);
    }

    for (size_t i = 0; i < ARRAY_SIZE(th1520_i2c_controllers); i++) {
        assert_i2c_fdt(fdt, &th1520_i2c_controllers[i],
                       ap_clock_phandle);
    }

    assert_spi_fdt(fdt, &th1520_spi0, ap_clock_phandle);
    assert_pwm_fdt(fdt, ap_clock_phandle);
    assert_mbox_fdt(fdt, ap_clock_phandle);

    for (size_t i = 0; i < ARRAY_SIZE(th1520_timers); i++) {
        assert_timer_fdt(fdt, &th1520_timers[i], ap_clock_phandle);
    }

    for (size_t i = 0; i < ARRAY_SIZE(th1520_gpio_controllers); i++) {
        gpio_phandles[i] = assert_gpio_fdt(fdt,
                                           &th1520_gpio_controllers[i],
                                           ap_clock_phandle,
                                           padctrl_phandles);
    }
    assert_led_fdt(fdt, gpio_phandles[4]);
    assert_pinctrl_reference(fdt, "/leds", led_pins_phandle);
    assert_pinctrl_reference(fdt, "/soc/serial@ffe7014000",
                             uart0_pins_phandle);
    assert_pinctrl_reference(fdt, "/soc/mmc@ffe70a0000",
                             wifi_pins_phandle);
    assert_pinctrl_reference(fdt, "/soc/ethernet@ffe7070000",
                             gmac0_pins_phandle);

    g_assert_cmpint(fdt_path_offset(fdt, "/dmac-clock"), ==,
                    -FDT_ERR_NOTFOUND);
    g_assert_cmpint(fdt_path_offset(fdt, "/mshc-clock"), ==,
                    -FDT_ERR_NOTFOUND);
    g_assert_cmpint(fdt_path_offset(fdt, "/gmac-axi-clock"), ==,
                    -FDT_ERR_NOTFOUND);
    g_assert_cmpint(fdt_path_offset(fdt, "/gmac-pclk"), ==,
                    -FDT_ERR_NOTFOUND);
    g_assert_cmpint(fdt_path_offset(fdt, "/gmac-apb-clock"), ==,
                    -FDT_ERR_NOTFOUND);

    config_offset = fdt_path_offset(fdt, "/stmmac-axi-config");
    g_assert_cmpint(config_offset, >=, 0);
    stmmac_axi_phandle = fdt_get_phandle(fdt, config_offset);
    g_assert_cmphex(stmmac_axi_phandle, !=, 0);
    g_assert_cmphex(fdt_prop_u32(fdt, config_offset, "snps,wr_osr_lmt"), ==,
                    15);
    g_assert_cmphex(fdt_prop_u32(fdt, config_offset, "snps,rd_osr_lmt"), ==,
                    15);

    for (size_t i = 0; i < ARRAY_SIZE(th1520_gmac_controllers); i++) {
        assert_gmac_fdt(fdt, &th1520_gmac_controllers[i],
                        ap_clock_phandle, stmmac_axi_phandle);
    }

    qtest_quit(qts);
}

static void test_external_dtb(void)
{
    enum { FDT_BUFFER_SIZE = 4096 };
    static const char marker[] = "beaglev-ahead-external-dtb";
    g_autoptr(GError) error = NULL;
    g_autofree char *path = NULL;
    g_autofree uint8_t *source_fdt = g_malloc0(FDT_BUFFER_SIZE);
    g_autofree uint8_t *guest_fdt = NULL;
    struct fdt_header header;
    const char *value;
    QTestState *qts;
    uint64_t fdt_addr;
    int fdt_size;
    int fd;
    int len;

    g_assert_cmpint(fdt_create_empty_tree(source_fdt, FDT_BUFFER_SIZE), ==, 0);
    g_assert_cmpint(fdt_setprop_string(source_fdt, 0,
                                      "qemu,external-dtb-test", marker), ==, 0);

    fd = g_file_open_tmp("beaglev-ahead-test-XXXXXX.dtb", &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(close(fd), ==, 0);
    g_assert_true(g_file_set_contents(path, (const char *)source_fdt,
                                     fdt_totalsize(source_fdt), &error));
    g_assert_no_error(error);

    qts = qtest_initf("-machine beaglev-ahead -bios none -dtb %s", path);
    fdt_addr = qtest_readq(qts, BROM_RESET_FDT_ADDR);
    qtest_memread(qts, fdt_addr, &header, sizeof(header));
    g_assert_cmpint(fdt_check_header(&header), ==, 0);
    fdt_size = fdt_totalsize(&header);
    guest_fdt = g_malloc(fdt_size);
    qtest_memread(qts, fdt_addr, guest_fdt, fdt_size);

    value = fdt_getprop(guest_fdt, 0, "qemu,external-dtb-test", &len);
    g_assert_nonnull(value);
    g_assert_cmpint(len, ==, sizeof(marker));
    g_assert_cmpstr(value, ==, marker);

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_ap_clock_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_CLOCK_BASE + 0x000), ==,
                    0x02507d01);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_CLOCK_BASE + 0x020), ==,
                    0x01307d01);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_CLOCK_BASE + 0x060), ==,
                    0x01306301);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_CLOCK_BASE + 0x064), ==,
                    0x63000000);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_C910_CLK_CFG),
                    ==, 0x000009f0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_AHB2_CLK_CFG),
                    ==, 0x000000d4);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE +
                                TH1520_PERISYS_AHB_CFG), ==, 0x00000258);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE +
                                TH1520_PERISYS_APB_CFG), ==, 0x00001f28);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PERI_CLK_CFG),
                    ==, 0x55ffffff);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_CTRL_CLK_CFG),
                    ==, 0x000007ff);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_UART_SCLK_CFG),
                    ==, 0);

    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==, 0);
    qtest_clock_step(qts, TH1520_PLL_LOCK_TIME_NS - 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==,
                    TH1520_PLL_RESET_LOCKS);

    /* TEE PLL is bypassed and held in VCO reset at silicon reset. */
    qtest_writel(qts, TH1520_AP_CLOCK_BASE + 0x064, 0x43000000);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS) &
                    BIT(10), ==, 0);
    qtest_clock_step(qts, TH1520_PLL_LOCK_TIME_NS);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==,
                    TH1520_PLL_RESET_LOCKS | BIT(10));

    qtest_writel(qts, TH1520_AP_CLOCK_BASE + 0x000, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_CLOCK_BASE + 0x000), ==,
                    0x077fff3f);
    qtest_writel(qts, TH1520_AP_CLOCK_BASE + 0x00c,
                  0x07fff400 | BIT(9));
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_CLOCK_BASE + 0x00c), ==,
                    0x07fff400);
    qtest_writel(qts, TH1520_AP_CLOCK_BASE + TH1520_AHB2_CLK_CFG,
                  UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_AHB2_CLK_CFG),
                    ==, 0x000000f7);
    qtest_writel(qts, TH1520_AP_CLOCK_BASE + TH1520_PERI_CLK_CFG, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PERI_CLK_CFG),
                    ==, 0);
    qtest_writel(qts, TH1520_AP_CLOCK_BASE + TH1520_PERI_CLK_CFG,
                  UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PERI_CLK_CFG),
                    ==, 0x55ffffff);
    qtest_writel(qts, TH1520_AP_CLOCK_BASE + TH1520_PLL_STS, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==,
                    (TH1520_PLL_RESET_LOCKS & ~BIT(1)) | BIT(10));
    qtest_clock_step(qts, TH1520_PLL_LOCK_TIME_NS);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==,
                    TH1520_PLL_RESET_LOCKS | BIT(10));

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_CLOCK_BASE + 0x000), ==,
                    0x02507d01);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_CLOCK_BASE + 0x064), ==,
                    0x63000000);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==, 0);
    qtest_quit(qts);
}

static void test_ap_reset_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    /* C910 reset releases top/core0; cores 1..3 remain asserted. */
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x004), ==, 0x3);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x068), ==, 0xf);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x070), ==, 0x3);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x14c), ==, 0x3);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x1b0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x204), ==, 0xf);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x220), ==, 0x8);

    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x004, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x004), ==, 0x1f);
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x004, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x004), ==, 0);
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x0cc, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x0cc), ==, 0x2);
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x220, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x220), ==, 0xf);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x004), ==, 0x3);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x0cc), ==, 0x2);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x220), ==, 0x8);
    qtest_quit(qts);
}

static void test_ap_reset_outputs(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, TH1520_AP_RESET_QOM_PATH,
                                  "peripheral-reset");

    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x03c, 0x2);
    g_assert_true(qtest_get_irq(qts, 1));
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_false(qtest_get_irq(qts, 2));
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x03c, 0x3);
    g_assert_false(qtest_get_irq(qts, 1));

    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x040, 0x1);
    g_assert_true(qtest_get_irq(qts, 2));
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x040, 0x3);
    g_assert_false(qtest_get_irq(qts, 2));

    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x0c0, 0x2);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x0c0, 0x3);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_system_reset(qts);
    for (unsigned int i = 0; i < 3; i++) {
        g_assert_false(qtest_get_irq(qts, i));
    }
    qtest_quit(qts);
}

static void assert_th1520_mbox_reset_state(QTestState *qts)
{
    static const uint64_t remote_bases[] = {
        TH1520_MBOX_REMOTE0_CHANNEL,
        TH1520_MBOX_REMOTE1_BASE,
        TH1520_MBOX_REMOTE2_BASE,
    };

    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_LOCAL_BASE +
                                TH1520_MBOX_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_LOCAL_BASE +
                                TH1520_MBOX_MASK), ==, 0);
    for (unsigned int channel = 0; channel < 4; channel++) {
        uint64_t base = TH1520_MBOX_CHANNEL(channel);

        g_assert_cmphex(qtest_readl(qts, base + TH1520_MBOX_GENERATE), ==,
                        0);
        for (unsigned int word = 0; word < 8; word++) {
            g_assert_cmphex(qtest_readl(qts, base + TH1520_MBOX_INFO(word)),
                            ==, 0);
        }
    }
    for (size_t channel = 0; channel < ARRAY_SIZE(remote_bases); channel++) {
        uint64_t base = remote_bases[channel];

        g_assert_cmphex(qtest_readl(qts, base + TH1520_MBOX_GENERATE), ==,
                        0);
        for (unsigned int word = 0; word < 8; word++) {
            g_assert_cmphex(qtest_readl(qts, base + TH1520_MBOX_INFO(word)),
                            ==, 0);
        }
    }
}

static void test_th1520_mbox_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_mbox_reset_state(qts);

    qtest_writel(qts, TH1520_MBOX_CHANNEL(1) + TH1520_MBOX_INFO(0),
                 0x10203040);
    qtest_writel(qts, TH1520_MBOX_CHANNEL(1) + TH1520_MBOX_INFO(6),
                 0x50607080);
    qtest_writel(qts, TH1520_MBOX_CHANNEL(1) + TH1520_MBOX_GENERATE,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_CHANNEL(1) +
                                TH1520_MBOX_INFO(0)), ==, 0x10203040);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_CHANNEL(1) +
                                TH1520_MBOX_INFO(6)), ==, 0x50607080);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_CHANNEL(1) +
                                TH1520_MBOX_GENERATE), ==, 0xff);

    qtest_writel(qts, TH1520_MBOX_REMOTE0_CHANNEL + TH1520_MBOX_INFO(0),
                 0xaabbccdd);
    qtest_writel(qts, TH1520_MBOX_REMOTE1_BASE + TH1520_MBOX_INFO(7),
                 0x11223344);
    qtest_writel(qts, TH1520_MBOX_REMOTE2_BASE + TH1520_MBOX_GENERATE,
                 0xc0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_REMOTE0_CHANNEL +
                                TH1520_MBOX_INFO(0)), ==, 0xaabbccdd);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_REMOTE1_BASE +
                                TH1520_MBOX_INFO(7)), ==, 0x11223344);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_REMOTE2_BASE +
                                TH1520_MBOX_GENERATE), ==, 0xc0);

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(TH1520_MBOX_IRQ), 5);
    c900_plic_set_enable(qts, 1, TH1520_MBOX_IRQ, true);
    qtest_writel(qts, TH1520_MBOX_LOCAL_BASE + TH1520_MBOX_MASK,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_LOCAL_BASE +
                                TH1520_MBOX_MASK), ==, 0x7);

    qtest_set_irq_in(qts, TH1520_MBOX_QOM_PATH, "remote-event", 1, 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_LOCAL_BASE +
                                TH1520_MBOX_STATUS), ==, BIT(1));
    g_assert_true(c900_plic_pending(qts, TH1520_MBOX_IRQ));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    TH1520_MBOX_IRQ);
    qtest_writel(qts, TH1520_MBOX_LOCAL_BASE + TH1520_MBOX_CLEAR, BIT(1));
    qtest_writel(qts, C900_PLIC_CLAIM(1), TH1520_MBOX_IRQ);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_MBOX_LOCAL_BASE +
                                TH1520_MBOX_STATUS), ==, 0);
    g_assert_false(c900_plic_pending(qts, TH1520_MBOX_IRQ));
    assert_no_irq(qts);
    qtest_set_irq_in(qts, TH1520_MBOX_QOM_PATH, "remote-event", 1, 0);

    qtest_system_reset(qts);
    assert_th1520_mbox_reset_state(qts);
    qtest_quit(qts);
}

typedef struct QEMU_PACKED DWAxiDMACTestLLI {
    uint64_t sar;
    uint64_t dar;
    uint32_t block_ts_low;
    uint32_t block_ts_high;
    uint64_t llp;
    uint32_t ctl_low;
    uint32_t ctl_high;
    uint32_t source_status;
    uint32_t destination_status;
    uint32_t status_low;
    uint32_t status_high;
    uint32_t reserved_low;
    uint32_t reserved_high;
} DWAxiDMACTestLLI;

static void dmac_write_lli(QTestState *qts, uint64_t address,
                           const DWAxiDMACTestLLI *lli)
{
    DWAxiDMACTestLLI le = {
        .sar = cpu_to_le64(lli->sar),
        .dar = cpu_to_le64(lli->dar),
        .block_ts_low = cpu_to_le32(lli->block_ts_low),
        .block_ts_high = cpu_to_le32(lli->block_ts_high),
        .llp = cpu_to_le64(lli->llp),
        .ctl_low = cpu_to_le32(lli->ctl_low),
        .ctl_high = cpu_to_le32(lli->ctl_high),
        .source_status = cpu_to_le32(lli->source_status),
        .destination_status = cpu_to_le32(lli->destination_status),
        .status_low = cpu_to_le32(lli->status_low),
        .status_high = cpu_to_le32(lli->status_high),
        .reserved_low = cpu_to_le32(lli->reserved_low),
        .reserved_high = cpu_to_le32(lli->reserved_high),
    };

    qtest_memwrite(qts, address, &le, sizeof(le));
}

static void dmac_read_lli(QTestState *qts, uint64_t address,
                          DWAxiDMACTestLLI *lli)
{
    qtest_memread(qts, address, lli, sizeof(*lli));
    lli->sar = le64_to_cpu(lli->sar);
    lli->dar = le64_to_cpu(lli->dar);
    lli->block_ts_low = le32_to_cpu(lli->block_ts_low);
    lli->block_ts_high = le32_to_cpu(lli->block_ts_high);
    lli->llp = le64_to_cpu(lli->llp);
    lli->ctl_low = le32_to_cpu(lli->ctl_low);
    lli->ctl_high = le32_to_cpu(lli->ctl_high);
    lli->source_status = le32_to_cpu(lli->source_status);
    lli->destination_status = le32_to_cpu(lli->destination_status);
    lli->status_low = le32_to_cpu(lli->status_low);
    lli->status_high = le32_to_cpu(lli->status_high);
    lli->reserved_low = le32_to_cpu(lli->reserved_low);
    lli->reserved_high = le32_to_cpu(lli->reserved_high);
}

static void assert_dmac_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE + DMAC_ID), ==, 0);
    g_assert_cmphex(qtest_readq(qts,
                                TH1520_DMAC0_BASE +
                                DMAC_COMPONENT_VERSION), ==,
                    DMAC_COMPONENT_VERSION_RESET);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE + DMAC_CFG), ==, 0);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE + DMAC_CHEN), ==, 0);
    g_assert_cmphex(qtest_readq(qts,
                                TH1520_DMAC0_BASE + DMAC_INTSTATUS), ==, 0);

    for (unsigned channel = 0; channel < 4; channel++) {
        g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                    DMAC_CH_SAR(channel)), ==, 0);
        g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                    DMAC_CH_DAR(channel)), ==, 0);
        g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                    DMAC_CH_BLOCK_TS(channel)), ==, 0);
        g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                    DMAC_CH_CTL(channel)), ==, 0);
        g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                    DMAC_CH_CFG(channel)), ==, 0);
        g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                    DMAC_CH_LLP(channel)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, TH1520_DMAC0_BASE +
                                    DMAC_CH_INTSTATUS_EN(channel)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, TH1520_DMAC0_BASE +
                                    DMAC_CH_INTSTATUS(channel)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, TH1520_DMAC0_BASE +
                                    DMAC_CH_INTSIGNAL_EN(channel)), ==, 0);
    }
}

static void test_dmac_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_dmac_reset_state(qts);

    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_BLOCK_TS(3), UINT64_MAX);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_BLOCK_TS(3)), ==,
                    DMAC_BLOCK_TS_MASK);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_SAR(3),
                  0x1122334455667788ULL);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_DAR(3),
                  0x8877665544332211ULL);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CTL(3),
                  0x123456789abcdef0ULL);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CFG(3),
                  0x0fedcba987654321ULL);

    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_RESET, 1);
    assert_dmac_reset_state(qts);

    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_SAR(0), 0x1234);
    qtest_system_reset(qts);
    assert_dmac_reset_state(qts);
    qtest_quit(qts);
}

static void test_dmac_direct_transfer(void)
{
    enum { LENGTH = 257 };
    uint8_t source[LENGTH];
    uint8_t destination[LENGTH];
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");
    uint32_t irq_mask = DMAC_IRQ_BLOCK_TRANSFER | DMAC_IRQ_DMA_TRANSFER |
                        DMAC_IRQ_ALL_ERRORS;

    for (unsigned i = 0; i < LENGTH; i++) {
        source[i] = i ^ 0xa5;
    }
    memset(destination, 0, sizeof(destination));
    qtest_memwrite(qts, DMAC_TEST_SOURCE_ADDR, source, sizeof(source));
    qtest_memwrite(qts, DMAC_TEST_DEST_ADDR, destination,
                   sizeof(destination));

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(TH1520_DMAC0_IRQ), 5);
    c900_plic_set_enable(qts, 1, TH1520_DMAC0_IRQ, true);

    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_SAR(0),
                  DMAC_TEST_SOURCE_ADDR);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_DAR(0),
                  DMAC_TEST_DEST_ADDR);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_BLOCK_TS(0), LENGTH - 1);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CTL(0), 0);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CFG(0), 0);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CH_INTSTATUS_EN(0),
                  irq_mask);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CH_INTSIGNAL_EN(0),
                  DMAC_IRQ_DMA_TRANSFER | DMAC_IRQ_ALL_ERRORS);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CFG,
                  DMAC_CFG_ENABLE | DMAC_CFG_INTERRUPT_ENABLE);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CHEN,
                  DMAC_CH_ENABLE(0) | DMAC_CH_ENABLE_WE(0));

    qtest_memread(qts, DMAC_TEST_DEST_ADDR, destination,
                  sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination), source, sizeof(source));
    g_assert_cmphex(qtest_readl(qts, TH1520_DMAC0_BASE + DMAC_CHEN) &
                    DMAC_CH_ENABLE(0), ==, 0);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_SAR(0)), ==,
                    DMAC_TEST_SOURCE_ADDR + LENGTH);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_DAR(0)), ==,
                    DMAC_TEST_DEST_ADDR + LENGTH);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_STATUS(0)), ==, LENGTH - 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_INTSTATUS(0)), ==,
                    DMAC_IRQ_BLOCK_TRANSFER | DMAC_IRQ_DMA_TRANSFER);
    g_assert_cmphex(qtest_readl(qts, TH1520_DMAC0_BASE + DMAC_INTSTATUS), ==,
                    BIT(0));
    g_assert_true(c900_plic_pending(qts, TH1520_DMAC0_IRQ));
    assert_only_irq(qts, 0);

    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    TH1520_DMAC0_IRQ);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CH_INTCLEAR(0),
                  DMAC_IRQ_BLOCK_TRANSFER | DMAC_IRQ_DMA_TRANSFER);
    qtest_writel(qts, C900_PLIC_CLAIM(1), TH1520_DMAC0_IRQ);
    g_assert_false(c900_plic_pending(qts, TH1520_DMAC0_IRQ));
    assert_no_irq(qts);

    qtest_quit(qts);
}

static void test_dmac_linked_list(void)
{
    enum { FIRST_LENGTH = 256, SECOND_LENGTH = 128 };
    uint8_t source[FIRST_LENGTH + SECOND_LENGTH];
    uint8_t destination[sizeof(source)];
    DWAxiDMACTestLLI first = {
        .sar = DMAC_TEST_SOURCE_ADDR,
        .dar = DMAC_TEST_DEST_ADDR,
        .block_ts_low = FIRST_LENGTH - 1,
        .llp = DMAC_TEST_LLI_ADDR + sizeof(DWAxiDMACTestLLI),
        .ctl_high = DMAC_CTL_LLI_VALID,
    };
    DWAxiDMACTestLLI second = {
        .sar = DMAC_TEST_SOURCE_ADDR + FIRST_LENGTH,
        .dar = DMAC_TEST_DEST_ADDR + FIRST_LENGTH,
        .block_ts_low = SECOND_LENGTH - 1,
        .ctl_high = DMAC_CTL_LLI_VALID | DMAC_CTL_LLI_LAST,
    };
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");
    uint32_t irq_mask = DMAC_IRQ_BLOCK_TRANSFER | DMAC_IRQ_DMA_TRANSFER |
                        DMAC_IRQ_ALL_ERRORS;

    g_assert_cmpuint(sizeof(DWAxiDMACTestLLI), ==, 64);
    for (unsigned i = 0; i < sizeof(source); i++) {
        source[i] = i * 17 + 3;
    }
    memset(destination, 0, sizeof(destination));
    qtest_memwrite(qts, DMAC_TEST_SOURCE_ADDR, source, sizeof(source));
    qtest_memwrite(qts, DMAC_TEST_DEST_ADDR, destination,
                   sizeof(destination));
    dmac_write_lli(qts, DMAC_TEST_LLI_ADDR, &first);
    dmac_write_lli(qts, DMAC_TEST_LLI_ADDR + sizeof(first), &second);

    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CFG(1), 0xf);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_LLP(1),
                  DMAC_TEST_LLI_ADDR);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CH_INTSTATUS_EN(1),
                  irq_mask);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CH_INTSIGNAL_EN(1),
                  DMAC_IRQ_DMA_TRANSFER | DMAC_IRQ_ALL_ERRORS);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CFG,
                  DMAC_CFG_ENABLE | DMAC_CFG_INTERRUPT_ENABLE);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CHEN,
                  DMAC_CH_ENABLE(1) | DMAC_CH_ENABLE_WE(1));

    qtest_memread(qts, DMAC_TEST_DEST_ADDR, destination,
                  sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination), source, sizeof(source));
    dmac_read_lli(qts, DMAC_TEST_LLI_ADDR, &first);
    dmac_read_lli(qts, DMAC_TEST_LLI_ADDR + sizeof(first), &second);
    g_assert_false(first.ctl_high & DMAC_CTL_LLI_VALID);
    g_assert_false(second.ctl_high & DMAC_CTL_LLI_VALID);
    g_assert_cmphex(first.status_low, ==, DMAC_IRQ_BLOCK_TRANSFER);
    g_assert_cmphex(second.status_low, ==,
                    DMAC_IRQ_BLOCK_TRANSFER | DMAC_IRQ_DMA_TRANSFER);
    g_assert_cmphex(qtest_readl(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_INTSTATUS(1)), ==,
                    DMAC_IRQ_BLOCK_TRANSFER | DMAC_IRQ_DMA_TRANSFER);

    /* An invalid LLI must fail visibly rather than copying stale data. */
    first = (DWAxiDMACTestLLI) {
        .sar = DMAC_TEST_SOURCE_ADDR,
        .dar = DMAC_TEST_DEST_ADDR,
        .block_ts_low = 31,
    };
    dmac_write_lli(qts, DMAC_TEST_LLI_ADDR, &first);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CFG(2), 0xf);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_LLP(2),
                  DMAC_TEST_LLI_ADDR);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CH_INTSTATUS_EN(2),
                  irq_mask);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CH_INTSIGNAL_EN(2),
                  DMAC_IRQ_ALL_ERRORS);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CHEN,
                  DMAC_CH_ENABLE(2) | DMAC_CH_ENABLE_WE(2));
    g_assert_cmphex(qtest_readl(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_INTSTATUS(2)), ==,
                    DMAC_IRQ_INVALID_ERROR);
    g_assert_cmphex(qtest_readl(qts, TH1520_DMAC0_BASE + DMAC_CHEN) &
                    DMAC_CH_ENABLE(2), ==, 0);

    qtest_quit(qts);
}

typedef struct GMACDesc {
    uint32_t des0;
    uint32_t des1;
    uint32_t des2;
    uint32_t des3;
} GMACDesc;

static void gmac_write_desc(QTestState *qts, uint32_t addr,
                            const GMACDesc *desc)
{
    GMACDesc le_desc = {
        .des0 = cpu_to_le32(desc->des0),
        .des1 = cpu_to_le32(desc->des1),
        .des2 = cpu_to_le32(desc->des2),
        .des3 = cpu_to_le32(desc->des3),
    };

    qtest_memwrite(qts, addr, &le_desc, sizeof(le_desc));
}

static void gmac_read_desc(QTestState *qts, uint32_t addr, GMACDesc *desc)
{
    qtest_memread(qts, addr, desc, sizeof(*desc));
    desc->des0 = le32_to_cpu(desc->des0);
    desc->des1 = le32_to_cpu(desc->des1);
    desc->des2 = le32_to_cpu(desc->des2);
    desc->des3 = le32_to_cpu(desc->des3);
}

static uint16_t gmac_mdio_read(QTestState *qts, uint64_t base,
                               uint8_t phy, uint8_t reg)
{
    qtest_writel(qts, base + DWMAC_MII_ADDR,
                  BIT(0) | (phy << 11) | (reg << 6));
    g_assert_cmphex(qtest_readl(qts, base + DWMAC_MII_ADDR) & BIT(0), ==, 0);
    return qtest_readl(qts, base + DWMAC_MII_DATA);
}

static void gmac_mdio_write(QTestState *qts, uint64_t base,
                            uint8_t phy, uint8_t reg, uint16_t value)
{
    qtest_writel(qts, base + DWMAC_MII_DATA, value);
    qtest_writel(qts, base + DWMAC_MII_ADDR,
                  BIT(0) | BIT(1) | (phy << 11) | (reg << 6));
    g_assert_cmphex(qtest_readl(qts, base + DWMAC_MII_ADDR) & BIT(0), ==, 0);
}

static void assert_gmac_reset_state(QTestState *qts,
                                    const TH1520GMACController *controller)
{
    static const uint32_t apb_reset[] = {
        0x00000008, 0x00008000, 0x00008000,
        0x00000004, 0x00000014, 0x00000002,
        0x00000001, 0x00000000, 0x00000001,
    };

    g_assert_cmphex(qtest_readl(qts, controller->base + DWMAC_VERSION), ==,
                    TH1520_GMAC_VERSION_RESET);
    g_assert_cmphex(qtest_readl(qts,
                                controller->base + DWMAC_DMA_HW_FEATURE), ==,
                    TH1520_GMAC_FEATURE_RESET);
    g_assert_cmphex(qtest_readl(qts, controller->base + DWMAC_DMA_BUS_MODE),
                    ==, 0x00020100);
    g_assert_cmphex(gmac_mdio_read(qts, controller->base,
                                  TH1520_GMAC_PHY_ADDR, MII_PHYID1), ==,
                    TH1520_GMAC_PHY_ID1);
    g_assert_cmphex(gmac_mdio_read(qts, controller->base,
                                  TH1520_GMAC_PHY_ADDR, MII_PHYID2), ==,
                    TH1520_GMAC_PHY_ID2);
    g_assert_cmphex(gmac_mdio_read(qts, controller->base, 0, MII_PHYID1), ==,
                    UINT16_MAX);

    for (size_t i = 0; i < ARRAY_SIZE(apb_reset); i++) {
        g_assert_cmphex(qtest_readl(qts, controller->apb_base + 4 * i), ==,
                        apb_reset[i]);
    }
}

static void test_gmac_registers(void)
{
    static const uint32_t apb_masks[] = {
        0x000000ff, 0x0000c01f, 0x0000c01f,
        0x800000ff, 0x800000ff, 0x8000000f,
        0x00000001, 0x00000001, 0x00000001,
    };
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (size_t controller = 0;
         controller < ARRAY_SIZE(th1520_gmac_controllers); controller++) {
        const TH1520GMACController *gmac =
            &th1520_gmac_controllers[controller];

        assert_gmac_reset_state(qts, gmac);
        for (size_t reg = 0; reg < ARRAY_SIZE(apb_masks); reg++) {
            qtest_writel(qts, gmac->apb_base + 4 * reg, UINT32_MAX);
            g_assert_cmphex(qtest_readl(qts, gmac->apb_base + 4 * reg), ==,
                            apb_masks[reg]);
        }
        gmac_mdio_write(qts, gmac->base, TH1520_GMAC_PHY_ADDR, MII_BMCR,
                        MII_BMCR_AUTOEN | MII_BMCR_FD | MII_BMCR_SPEED100);
        g_assert_cmphex(gmac_mdio_read(qts, gmac->base,
                                      TH1520_GMAC_PHY_ADDR, MII_BMCR), ==,
                        MII_BMCR_AUTOEN | MII_BMCR_FD | MII_BMCR_SPEED100);
    }

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(th1520_gmac_controllers); i++) {
        assert_gmac_reset_state(qts, &th1520_gmac_controllers[i]);
    }
    qtest_quit(qts);
}

static void test_gmac_interrupt(gconstpointer test_data)
{
    const TH1520GMACController *controller = test_data;
    static const uint8_t packet[64] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    GMACDesc desc = {
        .des0 = BIT(31) | BIT(30) | BIT(29) | BIT(28),
        .des1 = sizeof(packet),
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_memwrite(qts, GMAC_TEST_DATA_ADDR, packet, sizeof(packet));
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    desc = (GMACDesc) { 0 };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                    &desc);

    qtest_writel(qts, C900_PLIC_PRIORITY(controller->irq), 5);
    c900_plic_set_enable(qts, 1, controller->irq, true);
    qtest_writel(qts, controller->base + DWMAC_DMA_BUS_MODE,
                  0x00020100 | BIT(7));
    qtest_writel(qts, controller->base + DWMAC_DMA_TX_BASE_ADDR,
                  GMAC_TEST_DESC_ADDR);
    qtest_writel(qts, controller->base + DWMAC_DMA_INTR_ENA,
                  BIT(16) | BIT(0));
    qtest_writel(qts, controller->base + DWMAC_MAC_CONFIG, BIT(3));
    qtest_writel(qts, controller->base + DWMAC_DMA_CONTROL, BIT(13));

    g_assert_true(c900_plic_pending(qts, controller->irq));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    controller->irq);
    assert_no_irq(qts);
    qtest_writel(qts, controller->base + DWMAC_DMA_STATUS,
                  BIT(16) | BIT(0));
    qtest_writel(qts, C900_PLIC_CLAIM(1), controller->irq);
    g_assert_false(c900_plic_pending(qts, controller->irq));
    assert_no_irq(qts);

    qtest_quit(qts);
}

#ifndef _WIN32

static bool gmac_wait_socket_readable(int fd)
{
    fd_set read_fds;
    struct timeval tv = { .tv_sec = GMAC_TEST_TIMEOUT_S };

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    return select(fd + 1, &read_fds, NULL, NULL, &tv) == 1;
}

static bool gmac_wait_status(QTestState *qts, uint32_t mask)
{
    gint64 deadline = g_get_monotonic_time() +
                      GMAC_TEST_TIMEOUT_S * G_TIME_SPAN_SECOND;

    do {
        if (qtest_readl(qts, TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) & mask) {
            return true;
        }
        qtest_clock_step(qts, 1000);
    } while (g_get_monotonic_time() < deadline);

    return false;
}

static uint32_t gmac_test_crc32(const uint8_t *buf, size_t len)
{
    uint32_t crc = UINT32_MAX;

    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
        }
    }
    return ~crc;
}

static QTestState *gmac_packet_test_init(int sockets[2])
{
    QTestState *qts;

    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, sockets), ==, 0);
    qts = qtest_initf("-machine beaglev-ahead -bios none "
                      "-nic socket,fd=%d,model=gmac0", sockets[1]);
    close(sockets[1]);
    return qts;
}

static void test_gmac_enhanced_descriptors(void)
{
    static const uint8_t packet[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00,
    };
    static const uint32_t extension[4] = {
        0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00,
    };
    GMACDesc desc;
    QTestState *qts;
    int sockets[2];
    uint32_t wire_len;
    uint8_t received[sizeof(packet)];
    uint32_t actual_extension[ARRAY_SIZE(extension)];

    qts = gmac_packet_test_init(sockets);
    qtest_memwrite(qts, GMAC_TEST_DATA_ADDR, packet, sizeof(packet));
    desc = (GMACDesc) {
        .des0 = BIT(31) | BIT(30) | BIT(29) | BIT(28),
        .des1 = sizeof(packet),
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    qtest_memwrite(qts, GMAC_TEST_DESC_ADDR + sizeof(desc), extension,
                   sizeof(extension));
    desc = (GMACDesc) { 0 };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                    &desc);

    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                  0x00020100 | BIT(7));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_TX_BASE_ADDR,
                  GMAC_TEST_DESC_ADDR);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                  BIT(16) | BIT(0));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG, BIT(3));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL, BIT(13));

    g_assert_true(gmac_wait_socket_readable(sockets[0]));
    g_assert_cmpint(recv(sockets[0], &wire_len, sizeof(wire_len), MSG_WAITALL),
                    ==, sizeof(wire_len));
    g_assert_cmpuint(ntohl(wire_len), ==, sizeof(packet));
    g_assert_cmpint(recv(sockets[0], received, sizeof(received), MSG_WAITALL),
                    ==, sizeof(received));
    g_assert_cmpmem(received, sizeof(received), packet, sizeof(packet));
    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0 & BIT(31), ==, 0);
    g_assert_true(gmac_wait_status(qts, BIT(0)));
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_HOST_TX_DESC),
                    ==, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE);
    qtest_memread(qts, GMAC_TEST_DESC_ADDR + sizeof(desc), actual_extension,
                  sizeof(actual_extension));
    g_assert_cmpmem(actual_extension, sizeof(actual_extension), extension,
                    sizeof(extension));
    qtest_quit(qts);
    close(sockets[0]);

    qts = gmac_packet_test_init(sockets);
    desc = (GMACDesc) {
        .des0 = BIT(31),
        .des1 = 2048,
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    qtest_memwrite(qts, GMAC_TEST_DESC_ADDR + sizeof(desc), extension,
                   sizeof(extension));
    desc = (GMACDesc) { 0 };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                    &desc);

    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                  0x00020100 | BIT(7));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RX_BASE_ADDR,
                  GMAC_TEST_DESC_ADDR);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                  BIT(16) | BIT(6));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG, BIT(2));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL, BIT(1));

    wire_len = htonl(sizeof(packet));
    const struct iovec iov[] = {
        { .iov_base = &wire_len, .iov_len = sizeof(wire_len) },
        { .iov_base = (void *)packet, .iov_len = sizeof(packet) },
    };
    g_assert_cmpint(iov_send(sockets[0], iov, ARRAY_SIZE(iov), 0,
                             sizeof(wire_len) + sizeof(packet)),
                    ==, sizeof(wire_len) + sizeof(packet));
    g_assert_true(gmac_wait_status(qts, BIT(6)));

    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0 & BIT(31), ==, 0);
    g_assert_cmphex(desc.des0 & (BIT(9) | BIT(8)), ==, BIT(9) | BIT(8));
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==,
                     sizeof(packet) + sizeof(uint32_t));
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_HOST_RX_DESC),
                    ==, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE);
    qtest_memread(qts, GMAC_TEST_DESC_ADDR + sizeof(desc), actual_extension,
                  sizeof(actual_extension));
    g_assert_cmpmem(actual_extension, sizeof(actual_extension), extension,
                    sizeof(extension));

    uint8_t frame[sizeof(packet) + sizeof(uint32_t)];
    uint32_t expected_fcs = cpu_to_le32(gmac_test_crc32(packet,
                                                        sizeof(packet)));
    qtest_memread(qts, GMAC_TEST_DATA_ADDR, frame, sizeof(frame));
    g_assert_cmpmem(frame, sizeof(packet), packet, sizeof(packet));
    g_assert_cmpmem(frame + sizeof(packet), sizeof(expected_fcs),
                    &expected_fcs, sizeof(expected_fcs));

    qtest_quit(qts);
    close(sockets[0]);
}

#endif /* _WIN32 */

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

static void test_dw_uart_instances(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    for (size_t i = 0; i < ARRAY_SIZE(th1520_uart_controllers); i++) {
        const TH1520UARTController *controller =
            &th1520_uart_controllers[i];
        uint64_t base = controller->base;

        g_assert_cmphex(qtest_readl(qts, base + DW_UART_IER_DLH_OFFSET),
                        ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + DW_UART_IIR_FCR_OFFSET),
                        ==, UART_IIR_NO_INT);
        g_assert_cmphex(qtest_readl(qts, base + DW_UART_LSR_OFFSET), ==,
                        UART_LSR_THRE | UART_LSR_TEMT);
        g_assert_cmphex(qtest_readl(qts, base + DW_UART_CTR_OFFSET), ==,
                        0x44570110);

        qtest_writel(qts, C900_PLIC_PRIORITY(controller->irq), 5);
        c900_plic_set_enable(qts, 1, controller->irq, true);
        qtest_writel(qts, base + DW_UART_IER_DLH_OFFSET, UART_IER_THRI);
        g_assert_true(c900_plic_pending(qts, controller->irq));
        assert_only_irq(qts, 0);
        g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                        controller->irq);
        g_assert_cmphex(qtest_readl(qts, base + DW_UART_IIR_FCR_OFFSET), ==,
                        UART_IIR_THRI);
        qtest_writel(qts, base + DW_UART_IER_DLH_OFFSET, 0);
        qtest_writel(qts, C900_PLIC_CLAIM(1), controller->irq);
        c900_plic_set_enable(qts, 1, controller->irq, false);
        assert_no_irq(qts);

        qtest_writel(qts, base + DW_UART_SCR_OFFSET, 0x40 + i);
        g_assert_cmphex(qtest_readl(qts, base + DW_UART_SCR_OFFSET), ==,
                        0x40 + i);
    }

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(th1520_uart_controllers); i++) {
        g_assert_cmphex(qtest_readl(qts, th1520_uart_controllers[i].base +
                                    DW_UART_SCR_OFFSET), ==, 0);
    }
    qtest_quit(qts);
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

static void dw_i2c_enable(QTestState *qts, uint64_t base, uint8_t target,
                          uint32_t intr_mask)
{
    qtest_writel(qts, base + DW_I2C_ENABLE, 0);
    qtest_readl(qts, base + DW_I2C_CLR_INTR);
    qtest_writel(qts, base + DW_I2C_CON,
                  DW_I2C_CON_MASTER | DW_I2C_CON_SPEED_FAST |
                  DW_I2C_CON_RESTART | DW_I2C_CON_SLAVE_DISABLE);
    qtest_writel(qts, base + DW_I2C_TAR, target);
    qtest_writel(qts, base + DW_I2C_RX_TL, 0);
    qtest_writel(qts, base + DW_I2C_INTR_MASK, intr_mask);
    qtest_writel(qts, base + DW_I2C_ENABLE, 1);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_ENABLE_STATUS), ==, 1);
}

static void dw_i2c_disable(QTestState *qts, uint64_t base)
{
    qtest_writel(qts, base + DW_I2C_ENABLE, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_ENABLE_STATUS), ==, 0);
}

static void dw_i2c_eeprom_write(QTestState *qts, uint16_t address,
                                const uint8_t *data, size_t len)
{
    uint64_t base = TH1520_I2C0_BASE;

    g_assert_cmpuint(len, >, 0);
    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_EEPROM_ADDR, 0);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, address >> 8);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, address & 0xff);
    for (size_t i = 0; i < len; i++) {
        uint32_t command = data[i];

        if (i == len - 1) {
            command |= DW_I2C_DATA_STOP;
        }
        qtest_writel(qts, base + DW_I2C_DATA_CMD, command);
    }
    dw_i2c_disable(qts, base);
}

static void dw_i2c_eeprom_queue_read(QTestState *qts, uint16_t address,
                                     uint32_t intr_mask)
{
    uint64_t base = TH1520_I2C0_BASE;

    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_EEPROM_ADDR, intr_mask);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, address >> 8);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, address & 0xff);
    qtest_writel(qts, base + DW_I2C_DATA_CMD,
                  DW_I2C_DATA_READ | DW_I2C_DATA_RESTART |
                  DW_I2C_DATA_STOP);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_RXFLR), ==, 1);
}

static uint8_t dw_i2c_eeprom_read(QTestState *qts, uint16_t address)
{
    uint64_t base = TH1520_I2C0_BASE;
    uint8_t value;

    dw_i2c_eeprom_queue_read(qts, address, 0);
    value = qtest_readl(qts, base + DW_I2C_DATA_CMD);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_RXFLR), ==, 0);
    dw_i2c_disable(qts, base);
    return value;
}

static uint8_t dw_i2c_eeprom_current_read(QTestState *qts)
{
    uint64_t base = TH1520_I2C0_BASE;
    uint8_t value;

    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_EEPROM_ADDR, 0);
    qtest_writel(qts, base + DW_I2C_DATA_CMD,
                  DW_I2C_DATA_READ | DW_I2C_DATA_STOP);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_RXFLR), ==, 1);
    value = qtest_readl(qts, base + DW_I2C_DATA_CMD);
    dw_i2c_disable(qts, base);
    return value;
}

static void assert_dw_i2c_reset_state(QTestState *qts, uint64_t base)
{
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_INTR_MASK), ==,
                    TH1520_I2C_INTR_RESET);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_STATUS), ==, 6);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_ENABLE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_ENABLE_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_RXFLR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_SDA_SETUP), ==, 0x64);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_ACK_GENERAL_CALL), ==, 1);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_FS_SPKLEN), ==, 1);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_HS_SPKLEN), ==, 1);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_SCL_STUCK_TIMEOUT), ==,
                    UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_SDA_STUCK_TIMEOUT), ==,
                    UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_COMP_PARAM1), ==,
                    TH1520_I2C_COMP_PARAM1);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_COMP_VERSION), ==,
                    TH1520_I2C_COMP_VERSION);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_COMP_TYPE), ==,
                    TH1520_I2C_COMP_TYPE);
}

static void test_dw_i2c_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (size_t i = 0; i < ARRAY_SIZE(th1520_i2c_controllers); i++) {
        uint64_t base = th1520_i2c_controllers[i].base;

        assert_dw_i2c_reset_state(qts, base);
        qtest_writel(qts, base + DW_I2C_INTR_MASK, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, base + DW_I2C_INTR_MASK), ==,
                        TH1520_I2C_INTR_VALID);
        qtest_writel(qts, base + DW_I2C_COMP_PARAM1, 0);
        qtest_writel(qts, base + DW_I2C_COMP_VERSION, 0);
        qtest_writel(qts, base + DW_I2C_COMP_TYPE, 0);
        g_assert_cmphex(qtest_readl(qts, base + DW_I2C_COMP_PARAM1), ==,
                        TH1520_I2C_COMP_PARAM1);
        g_assert_cmphex(qtest_readl(qts, base + DW_I2C_COMP_VERSION), ==,
                        TH1520_I2C_COMP_VERSION);
        g_assert_cmphex(qtest_readl(qts, base + DW_I2C_COMP_TYPE), ==,
                        TH1520_I2C_COMP_TYPE);

        qtest_writel(qts, base + DW_I2C_FS_SPKLEN, 0);
        qtest_writel(qts, base + DW_I2C_HS_SPKLEN, 0);
        g_assert_cmphex(qtest_readl(qts, base + DW_I2C_FS_SPKLEN), ==, 1);
        g_assert_cmphex(qtest_readl(qts, base + DW_I2C_HS_SPKLEN), ==, 1);
        qtest_writel(qts, base + DW_I2C_ENABLE, 1);
        qtest_writel(qts, base + DW_I2C_FS_SPKLEN, 7);
        qtest_writel(qts, base + DW_I2C_HS_SPKLEN, 7);
        g_assert_cmphex(qtest_readl(qts, base + DW_I2C_FS_SPKLEN), ==, 1);
        g_assert_cmphex(qtest_readl(qts, base + DW_I2C_HS_SPKLEN), ==, 1);
        dw_i2c_disable(qts, base);
    }

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(th1520_i2c_controllers); i++) {
        assert_dw_i2c_reset_state(qts, th1520_i2c_controllers[i].base);
    }
    qtest_quit(qts);
}

static void test_dw_i2c_eeprom(void)
{
    static const uint8_t edge_data[] = { 0x12, 0x34, 0x56 };
    const uint8_t next_page = 0xa5;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x000), ==, 0xff);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0xfff), ==, 0xff);

    /* End one 32-byte page without depending on unsupported page wrapping. */
    dw_i2c_eeprom_write(qts, 0x01d, edge_data, ARRAY_SIZE(edge_data));
    dw_i2c_eeprom_write(qts, 0x020, &next_page, 1);
    for (size_t i = 0; i < ARRAY_SIZE(edge_data); i++) {
        g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x01d + i), ==,
                        edge_data[i]);
    }
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x020), ==, next_page);

    qtest_system_reset(qts);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x01d), ==, edge_data[0]);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x020), ==, next_page);
    qtest_quit(qts);
}

static void test_dw_i2c_eeprom_backing(void)
{
    uint8_t initial[4096];
    uint8_t actual[4096];
    const uint8_t replacement = 0x77;
    g_autoptr(GError) error = NULL;
    g_autofree char *path = NULL;
    QTestState *qts;
    int fd;

    for (size_t i = 0; i < sizeof(initial); i++) {
        initial[i] = i ^ (i >> 4) ^ 0xa5;
    }
    fd = g_file_open_tmp("beaglev-ahead-eeprom-XXXXXX", &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(pwrite(fd, initial, sizeof(initial), 0), ==,
                    sizeof(initial));
    g_assert_cmpint(fsync(fd), ==, 0);
    close(fd);

    qts = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=none,id=board-eeprom,format=raw,file=%s "
        "-global at24c-eeprom.drive=board-eeprom", path);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x000), ==, initial[0x000]);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x123), ==, initial[0x123]);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0xfff), ==, initial[0xfff]);
    dw_i2c_eeprom_write(qts, 0x123, &replacement, 1);
    qtest_quit(qts);

    fd = open(path, O_RDONLY);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(read(fd, actual, sizeof(actual)), ==, sizeof(actual));
    close(fd);
    initial[0x123] = replacement;
    g_assert_cmpmem(actual, sizeof(actual), initial, sizeof(initial));
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_dw_i2c_interrupts(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    for (size_t i = 0; i < ARRAY_SIZE(th1520_i2c_controllers); i++) {
        const TH1520I2CController *controller =
            &th1520_i2c_controllers[i];
        uint64_t base = controller->base;

        qtest_writel(qts, C900_PLIC_PRIORITY(controller->irq), 5);
        c900_plic_set_enable(qts, 1, controller->irq, true);
        dw_i2c_enable(qts, base, BEAGLEV_AHEAD_EEPROM_ADDR + 1,
                      DW_I2C_INTR_TX_ABRT);
        qtest_writel(qts, base + DW_I2C_DATA_CMD,
                      0xa5 | DW_I2C_DATA_STOP);

        g_assert_true(qtest_readl(qts, base + DW_I2C_RAW_INTR_STAT) &
                      DW_I2C_INTR_TX_ABRT);
        g_assert_true(qtest_readl(qts, base + DW_I2C_INTR_STAT) &
                      DW_I2C_INTR_TX_ABRT);
        g_assert_true(qtest_readl(qts, base + DW_I2C_TX_ABRT_SOURCE) &
                      BIT(0));
        g_assert_true(c900_plic_pending(qts, controller->irq));
        assert_only_irq(qts, 0);
        g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                        controller->irq);
        assert_no_irq(qts);

        qtest_readl(qts, base + DW_I2C_CLR_TX_ABRT);
        g_assert_false(qtest_readl(qts, base + DW_I2C_INTR_STAT) &
                       DW_I2C_INTR_TX_ABRT);
        g_assert_cmphex(qtest_readl(qts, base + DW_I2C_TX_ABRT_SOURCE), ==,
                        0);
        qtest_writel(qts, C900_PLIC_CLAIM(1), controller->irq);
        dw_i2c_disable(qts, base);
        c900_plic_set_enable(qts, 1, controller->irq, false);
        assert_no_irq(qts);
    }

    qtest_quit(qts);
}

static void assert_dw_spi_reset_state(QTestState *qts)
{
    uint64_t base = th1520_spi0.base;

    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_CTRLR0), ==,
                    DW_SSI_CTRLR0_DFS_8);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_CTRLR1), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_SSIENR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_SER), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_TXFTLR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_RXFTLR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_TXFLR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_RXFLR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_SR), ==,
                    DW_SSI_SR_TF_NOT_FULL | DW_SSI_SR_TF_EMPTY);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_IDR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_VERSION), ==, 0);
}

static void test_dw_spi_registers(void)
{
    uint64_t base = th1520_spi0.base;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_dw_spi_reset_state(qts);

    /* The generic controller exposes a 16-entry FIFO to the Linux probe. */
    qtest_writel(qts, base + DW_SSI_TXFTLR, 15);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_TXFTLR), ==, 15);
    qtest_writel(qts, base + DW_SSI_TXFTLR, 16);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_TXFTLR), ==, 15);
    qtest_writel(qts, base + DW_SSI_RXFTLR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_RXFTLR), ==, 15);
    qtest_writel(qts, base + DW_SSI_BAUDR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_BAUDR), ==, UINT16_MAX);

    qtest_writel(qts, base + DW_SSI_CTRLR0,
                  DW_SSI_CTRLR0_DFS_8 | DW_SSI_CTRLR0_SRL);
    qtest_writel(qts, base + DW_SSI_SSIENR, 1);
    qtest_writel(qts, base + DW_SSI_CTRLR0, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_CTRLR0), ==,
                    DW_SSI_CTRLR0_DFS_8 | DW_SSI_CTRLR0_SRL);
    qtest_writel(qts, base + DW_SSI_SSIENR, 0);

    qtest_system_reset(qts);
    assert_dw_spi_reset_state(qts);
    qtest_quit(qts);
}

static void test_dw_spi_loopback_and_interrupt(void)
{
    uint64_t base = th1520_spi0.base;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(th1520_spi0.irq), 5);
    c900_plic_set_enable(qts, 1, th1520_spi0.irq, true);
    qtest_writel(qts, base + DW_SSI_CTRLR0,
                  DW_SSI_CTRLR0_DFS_8 | DW_SSI_CTRLR0_SRL);
    qtest_writel(qts, base + DW_SSI_RXFTLR, 0);
    qtest_writel(qts, base + DW_SSI_SSIENR, 1);
    qtest_writel(qts, base + DW_SSI_SER, 1);
    qtest_writel(qts, base + DW_SSI_IMR, DW_SSI_INT_RXFI);
    qtest_writel(qts, base + DW_SSI_DR, 0xa5);

    g_assert_true(qtest_readl(qts, base + DW_SSI_SR) &
                  DW_SSI_SR_RF_NOT_EMPTY);
    g_assert_true(qtest_readl(qts, base + DW_SSI_RISR) & DW_SSI_INT_RXFI);
    g_assert_true(qtest_readl(qts, base + DW_SSI_ISR) & DW_SSI_INT_RXFI);
    g_assert_true(c900_plic_pending(qts, th1520_spi0.irq));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    th1520_spi0.irq);
    assert_no_irq(qts);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_DR), ==, 0xa5);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_RXFLR), ==, 0);
    qtest_writel(qts, C900_PLIC_CLAIM(1), th1520_spi0.irq);
    c900_plic_set_enable(qts, 1, th1520_spi0.irq, false);

    qtest_writel(qts, base + DW_SSI_SSIENR, 0);
    qtest_writel(qts, base + DW_SSI_CTRLR0,
                  DW_SSI_CTRLR0_DFS_8 | DW_SSI_CTRLR0_TMOD_RO |
                  DW_SSI_CTRLR0_SRL);
    qtest_writel(qts, base + DW_SSI_CTRLR1, 2);
    qtest_writel(qts, base + DW_SSI_SSIENR, 1);
    qtest_writel(qts, base + DW_SSI_SER, 1);
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_RXFLR), ==, 3);
    for (unsigned int i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(qts, base + DW_SSI_DR), ==, 0);
    }

    qtest_quit(qts);
}

static void test_dw_spi_error_status(void)
{
    uint64_t base = th1520_spi0.base;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (unsigned int i = 0; i < 17; i++) {
        qtest_writel(qts, base + DW_SSI_DR, i);
    }
    g_assert_cmphex(qtest_readl(qts, base + DW_SSI_TXFLR), ==, 16);
    g_assert_true(qtest_readl(qts, base + DW_SSI_RISR) & DW_SSI_INT_TXOI);
    qtest_readl(qts, base + DW_SSI_TXOICR);
    g_assert_false(qtest_readl(qts, base + DW_SSI_RISR) & DW_SSI_INT_TXOI);

    qtest_readl(qts, base + DW_SSI_DR);
    g_assert_true(qtest_readl(qts, base + DW_SSI_RISR) & DW_SSI_INT_RXUI);
    qtest_readl(qts, base + DW_SSI_RXUICR);
    g_assert_false(qtest_readl(qts, base + DW_SSI_RISR) & DW_SSI_INT_RXUI);
    qtest_readl(qts, base + DW_SSI_ICR);
    qtest_quit(qts);
}

static void assert_dw_timer_reset_state(QTestState *qts, uint64_t base)
{
    for (unsigned int channel = 0; channel < 4; channel++) {
        uint64_t timer = base + channel * DW_TIMER_STRIDE;

        g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_LOAD_COUNT), ==,
                        0);
        g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==,
                        0x80000000);
        g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CONTROL), ==, 0);
        g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_INT_STATUS), ==,
                        0);
        g_assert_cmphex(qtest_readl(qts,
                                    base + DW_TIMER_LOAD_COUNT2(channel)),
                        ==, 0);
        g_assert_cmphex(qtest_readl(qts,
                                    base + DW_TIMER_PROTECTION(channel)),
                        ==, 2);
    }
    g_assert_cmphex(qtest_readl(qts, base + DW_TIMERS_INT_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_TIMERS_RAW_INT_STATUS), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, base + DW_TIMERS_COMP_VERSION), ==,
                    TH1520_TIMER_COMP_VERSION);
}

static void test_dw_timer_registers(void)
{
    static const uint64_t components[] = {
        TH1520_TIMER0_3_BASE, TH1520_TIMER4_7_BASE,
    };
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (size_t component = 0; component < ARRAY_SIZE(components);
         component++) {
        uint64_t base = components[component];

        assert_dw_timer_reset_state(qts, base);
        for (unsigned int channel = 0; channel < 4; channel++) {
            uint64_t timer = base + channel * DW_TIMER_STRIDE;
            uint32_t load2 = 0x10203040 + channel + component * 0x100;

            qtest_writel(qts, timer + DW_TIMER_CONTROL,
                          DW_TIMER_ENABLE | DW_TIMER_PERIODIC |
                          DW_TIMER_INT_MASK | DW_TIMER_PWM);
            g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CONTROL), ==,
                            0xf);
            qtest_writel(qts, timer + DW_TIMER_CONTROL, 0);
            qtest_writel(qts, base + DW_TIMER_LOAD_COUNT2(channel), load2);
            qtest_writel(qts, base + DW_TIMER_PROTECTION(channel),
                          UINT32_MAX);
            g_assert_cmphex(qtest_readl(
                                qts, base + DW_TIMER_LOAD_COUNT2(channel)),
                            ==, load2);
            g_assert_cmphex(qtest_readl(
                                qts, base + DW_TIMER_PROTECTION(channel)),
                            ==, 7);
        }
    }

    qtest_system_reset(qts);
    for (size_t component = 0; component < ARRAY_SIZE(components);
         component++) {
        assert_dw_timer_reset_state(qts, components[component]);
    }
    qtest_quit(qts);
}

static void test_dw_timer_timing(void)
{
    const uint64_t base = TH1520_TIMER0_3_BASE;
    const uint64_t timer = base;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);

    /* Periodic mode expires on the third 125 MHz tick and then reloads. */
    qtest_writel(qts, timer + DW_TIMER_LOAD_COUNT, 3);
    qtest_writel(qts, timer + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_PERIODIC);
    qtest_clock_step(qts, 3 * TH1520_TIMER_TICK_NS - 1);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==, 1);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_INT_STATUS), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_INT_STATUS), ==, 1);
    g_assert_cmphex(qtest_readl(qts, base + DW_TIMERS_INT_STATUS), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(qts, base + DW_TIMERS_RAW_INT_STATUS), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_EOI), ==, 0);

    qtest_clock_step(qts, TH1520_TIMER_TICK_NS - 1);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==, 3);

    qtest_clock_step(qts, 3 * TH1520_TIMER_TICK_NS - 1);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_INT_STATUS), ==, 1);
    qtest_writel(qts, timer + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_PERIODIC | DW_TIMER_INT_MASK);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_INT_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_TIMERS_RAW_INT_STATUS), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(qts, base + DW_TIMERS_EOI), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_TIMERS_RAW_INT_STATUS), ==,
                    0);

    /* Clearing ENABLE pauses rather than discarding the current count. */
    qtest_writel(qts, timer + DW_TIMER_CONTROL, DW_TIMER_PERIODIC);
    qtest_writel(qts, timer + DW_TIMER_LOAD_COUNT, 5);
    qtest_writel(qts, timer + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_PERIODIC);
    qtest_clock_step(qts, 2 * TH1520_TIMER_TICK_NS + 1);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==, 3);
    qtest_writel(qts, timer + DW_TIMER_CONTROL, DW_TIMER_PERIODIC);
    qtest_clock_step(qts, 100 * TH1520_TIMER_TICK_NS);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==, 3);

    /* Free-running mode uses LOAD_COUNT once, then wraps to all ones. */
    qtest_writel(qts, timer + DW_TIMER_LOAD_COUNT, 2);
    qtest_writel(qts, timer + DW_TIMER_CONTROL, DW_TIMER_ENABLE);
    qtest_clock_step(qts, 2 * TH1520_TIMER_TICK_NS);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_INT_STATUS), ==, 1);
    qtest_readl(qts, timer + DW_TIMER_EOI);
    qtest_clock_step(qts, TH1520_TIMER_TICK_NS);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==,
                    UINT32_MAX);
    qtest_clock_step(qts, TH1520_TIMER_TICK_NS);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_CURRENT_VALUE), ==,
                    UINT32_MAX - 1);

    qtest_writel(qts, timer + DW_TIMER_CONTROL, 0);
    qtest_writel(qts, timer + DW_TIMER_LOAD_COUNT, 1);
    qtest_writel(qts, timer + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_INT_MASK);
    qtest_clock_step(qts, TH1520_TIMER_TICK_NS);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_INT_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_TIMERS_RAW_INT_STATUS), ==,
                    BIT(0));
    qtest_writel(qts, timer + DW_TIMER_CONTROL, DW_TIMER_ENABLE);
    g_assert_cmphex(qtest_readl(qts, timer + DW_TIMER_INT_STATUS), ==, 1);
    qtest_readl(qts, timer + DW_TIMER_EOI);

    qtest_quit(qts);
}

static void test_dw_timer_interrupt_routes(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);
    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    for (size_t i = 0; i < ARRAY_SIZE(th1520_timers); i++) {
        const TH1520Timer *timer = &th1520_timers[i];

        qtest_writel(qts, C900_PLIC_PRIORITY(timer->irq), 5);
        c900_plic_set_enable(qts, 1, timer->irq, true);
        qtest_writel(qts, timer->base + DW_TIMER_LOAD_COUNT, 1);
        qtest_writel(qts, timer->base + DW_TIMER_CONTROL, DW_TIMER_ENABLE);
        qtest_clock_step(qts, TH1520_TIMER_TICK_NS);

        g_assert_true(c900_plic_pending(qts, timer->irq));
        assert_only_irq(qts, 0);
        g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                        timer->irq);
        assert_no_irq(qts);
        g_assert_cmphex(qtest_readl(qts,
                                    timer->component_base +
                                    DW_TIMERS_RAW_INT_STATUS), ==,
                        BIT(timer->channel));
        qtest_readl(qts, timer->base + DW_TIMER_EOI);
        qtest_writel(qts, timer->base + DW_TIMER_CONTROL, 0);
        qtest_writel(qts, C900_PLIC_CLAIM(1), timer->irq);
        c900_plic_set_enable(qts, 1, timer->irq, false);
        assert_no_irq(qts);
    }
    qtest_quit(qts);
}

static void assert_padctrl_reset_state(QTestState *qts,
                                       const TH1520PadCtrl *controller)
{
    uint64_t base = controller->base;
    unsigned int pad_words;
    unsigned int mux_words;

    if (controller->group == 1) {
        g_assert_cmphex(qtest_readl(qts, base + 0x000), ==, 0x00000125);
        g_assert_cmphex(qtest_readl(qts, base + 0x004), ==, 0x001a0000);
        g_assert_cmphex(qtest_readl(qts, base + 0x010), ==, 0x02380000);
        g_assert_cmphex(qtest_readl(qts, base + 0x014), ==, 0x02080238);
        g_assert_cmphex(qtest_readl(qts, base + 0x01c), ==, 0x02380208);
        g_assert_cmphex(qtest_readl(qts, base + 0x020), ==, 0x02080218);
        g_assert_cmphex(qtest_readl(qts, base + 0x024), ==, 0x02180208);
        for (hwaddr offset = 0x028; offset <= 0x058; offset += 4) {
            g_assert_cmphex(qtest_readl(qts, base + offset), ==,
                            0x02080208);
        }
        g_assert_cmphex(qtest_readl(qts, base + 0x05c), ==, 0x00000208);
        mux_words = 6;
    } else {
        pad_words = controller->group == 2 ? 32 : 28;
        for (unsigned int word = 0; word < pad_words; word++) {
            uint32_t expected = 0x02080208;

            if (controller->group == 2 &&
                (word == 3 || word == 4 || word == 14)) {
                expected = 0x02380238;
            } else if (controller->group == 2 && word == 13) {
                expected = 0x02380208;
            } else if (word == pad_words - 1) {
                expected = 0x00000208;
            }
            g_assert_cmphex(qtest_readl(qts, base + 4 * word), ==,
                            expected);
        }
        mux_words = controller->group == 2 ? 8 : 7;
    }

    for (unsigned int word = 0; word < mux_words; word++) {
        g_assert_cmphex(qtest_readl(qts, base + 0x400 + 4 * word), ==, 0);
    }
}

static void test_padctrl_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (size_t i = 0; i < ARRAY_SIZE(th1520_padctrls); i++) {
        assert_padctrl_reset_state(qts, &th1520_padctrls[i]);
    }

    qtest_writel(qts, TH1520_PADCTRL_AOSYS_BASE + 0x000, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL_AOSYS_BASE + 0x004, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL_AOSYS_BASE + 0x010, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL_AOSYS_BASE + 0x014, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL_AOSYS_BASE + 0x05c, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL_AOSYS_BASE + 0x400, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL_AOSYS_BASE + 0x404, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL_AOSYS_BASE + 0x414, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL_AOSYS_BASE + 0x000),
                    ==, 0x000001ff);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL_AOSYS_BASE + 0x004),
                    ==, 0x001f0000);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL_AOSYS_BASE + 0x010),
                    ==, 0x03ff0000);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL_AOSYS_BASE + 0x014),
                    ==, 0x03ff03ff);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL_AOSYS_BASE + 0x05c),
                    ==, 0x000003ff);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL_AOSYS_BASE + 0x400),
                    ==, 0xf0000000);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL_AOSYS_BASE + 0x404),
                    ==, 0xfffffff0);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL_AOSYS_BASE + 0x414),
                    ==, 0x0fffffff);

    qtest_writel(qts, TH1520_PADCTRL1_APSYS_BASE + 0x000, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL1_APSYS_BASE + 0x07c, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL1_APSYS_BASE + 0x41c, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL1_APSYS_BASE + 0x000),
                    ==, 0x03ff03ff);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL1_APSYS_BASE + 0x07c),
                    ==, 0x000003ff);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL1_APSYS_BASE + 0x41c),
                    ==, 0x0fffffff);

    qtest_writel(qts, TH1520_PADCTRL0_APSYS_BASE + 0x000, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL0_APSYS_BASE + 0x06c, UINT32_MAX);
    qtest_writel(qts, TH1520_PADCTRL0_APSYS_BASE + 0x418, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL0_APSYS_BASE + 0x000),
                    ==, 0x03ff03ff);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL0_APSYS_BASE + 0x06c),
                    ==, 0x000003ff);
    g_assert_cmphex(qtest_readl(qts, TH1520_PADCTRL0_APSYS_BASE + 0x418),
                    ==, 0x0fffffff);

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(th1520_padctrls); i++) {
        assert_padctrl_reset_state(qts, &th1520_padctrls[i]);
    }
    qtest_quit(qts);
}

static uint32_t dw_gpio_mask(const TH1520GPIOController *controller)
{
    return controller->ngpios == 32 ? UINT32_MAX :
           BIT(controller->ngpios) - 1;
}

static void assert_dw_gpio_reset_state(
    QTestState *qts, const TH1520GPIOController *controller)
{
    uint64_t base = controller->base;

    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_SWPORTA_DR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_SWPORTA_DDR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_SWPORTA_CTL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_INTEN), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_INTMASK), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_INTTYPE_LEVEL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_INT_POLARITY), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_INTSTATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_RAW_INTSTATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_PORTA_DEBOUNCE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_EXT_PORTA), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_LS_SYNC), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_ID_CODE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_VER_ID_CODE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_CONFIG_REG2), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_CONFIG_REG1), ==, 0);
}

static void test_dw_gpio_registers(void)
{
    const char *const gpio0_path = "/machine/soc/gpio0";
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, gpio0_path, "gpio-out");
    for (size_t i = 0; i < ARRAY_SIZE(th1520_gpio_controllers); i++) {
        const TH1520GPIOController *controller =
            &th1520_gpio_controllers[i];
        uint32_t mask = dw_gpio_mask(controller);
        uint64_t base = controller->base;

        assert_dw_gpio_reset_state(qts, controller);
        qtest_writel(qts, base + DW_GPIO_SWPORTA_DR, UINT32_MAX);
        qtest_writel(qts, base + DW_GPIO_SWPORTA_CTL, UINT32_MAX);
        qtest_writel(qts, base + DW_GPIO_PORTA_DEBOUNCE, UINT32_MAX);
        qtest_writel(qts, base + DW_GPIO_LS_SYNC, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_SWPORTA_DR), ==,
                        mask);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_SWPORTA_CTL), ==,
                        mask);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_PORTA_DEBOUNCE),
                        ==, mask);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_LS_SYNC), ==, 1);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_EXT_PORTA), ==, 0);

        qtest_writel(qts, base + DW_GPIO_SWPORTA_DDR, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_SWPORTA_DDR), ==,
                        mask);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_EXT_PORTA), ==,
                        mask);

        qtest_writel(qts, base + DW_GPIO_SWPORTA_DDR, 0);
        qtest_writel(qts, base + DW_GPIO_SWPORTA_DR, 0);
        qtest_writel(qts, base + DW_GPIO_SWPORTA_CTL, 0);
        qtest_writel(qts, base + DW_GPIO_PORTA_DEBOUNCE, 0);
        qtest_writel(qts, base + DW_GPIO_LS_SYNC, 0);
    }

    qtest_writel(qts, TH1520_GPIO0_BASE + DW_GPIO_SWPORTA_DR, BIT(5));
    g_assert_false(qtest_get_irq(qts, 5));
    qtest_writel(qts, TH1520_GPIO0_BASE + DW_GPIO_SWPORTA_DDR, BIT(5));
    g_assert_true(qtest_get_irq(qts, 5));
    qtest_writel(qts, TH1520_GPIO0_BASE + DW_GPIO_SWPORTA_DR, 0);
    g_assert_false(qtest_get_irq(qts, 5));
    qtest_writel(qts, TH1520_GPIO0_BASE + DW_GPIO_SWPORTA_DDR, 0);

    qtest_set_irq_in(qts, gpio0_path, "gpio-in", 5, 1);
    g_assert_true(qtest_get_irq(qts, 5));
    g_assert_true(qtest_readl(qts, TH1520_GPIO0_BASE + DW_GPIO_EXT_PORTA) &
                  BIT(5));
    qtest_set_irq_in(qts, gpio0_path, "gpio-in", 5, -1);
    g_assert_false(qtest_get_irq(qts, 5));

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(th1520_gpio_controllers); i++) {
        assert_dw_gpio_reset_state(qts, &th1520_gpio_controllers[i]);
    }
    qtest_quit(qts);
}

static void test_dw_gpio_interrupts(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    for (size_t i = 0; i < ARRAY_SIZE(th1520_gpio_controllers); i++) {
        const TH1520GPIOController *controller =
            &th1520_gpio_controllers[i];
        g_autofree char *path =
            g_strdup_printf("/machine/soc/%s", controller->name);
        uint32_t pin = controller->ngpios - 1;
        uint32_t bit = BIT(pin);
        uint64_t base = controller->base;

        qtest_writel(qts, C900_PLIC_PRIORITY(controller->irq), 5);
        c900_plic_set_enable(qts, 1, controller->irq, true);

        /* A rising edge is latched until PORTA_EOI is written. */
        qtest_set_irq_in(qts, path, "gpio-in", pin, 0);
        qtest_writel(qts, base + DW_GPIO_INTTYPE_LEVEL, bit);
        qtest_writel(qts, base + DW_GPIO_INT_POLARITY, bit);
        qtest_writel(qts, base + DW_GPIO_INTEN, bit);
        qtest_set_irq_in(qts, path, "gpio-in", pin, 1);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_RAW_INTSTATUS),
                        ==, bit);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_INTSTATUS), ==,
                        bit);
        g_assert_true(c900_plic_pending(qts, controller->irq));
        assert_only_irq(qts, 0);
        g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                        controller->irq);
        assert_no_irq(qts);
        qtest_writel(qts, base + DW_GPIO_PORTA_EOI, bit);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_INTSTATUS), ==, 0);
        qtest_writel(qts, C900_PLIC_CLAIM(1), controller->irq);
        assert_no_irq(qts);

        /* Active-low level interrupts ignore EOI and obey INTMASK. */
        qtest_writel(qts, base + DW_GPIO_INTTYPE_LEVEL, 0);
        qtest_writel(qts, base + DW_GPIO_INT_POLARITY, 0);
        qtest_set_irq_in(qts, path, "gpio-in", pin, 0);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_INTSTATUS), ==,
                        bit);
        assert_only_irq(qts, 0);
        g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                        controller->irq);
        qtest_writel(qts, base + DW_GPIO_PORTA_EOI, bit);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_INTSTATUS), ==,
                        bit);
        qtest_writel(qts, base + DW_GPIO_INTMASK, bit);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_RAW_INTSTATUS),
                        ==, bit);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_INTSTATUS), ==, 0);
        qtest_writel(qts, C900_PLIC_CLAIM(1), controller->irq);
        assert_no_irq(qts);

        qtest_set_irq_in(qts, path, "gpio-in", pin, 1);
        qtest_writel(qts, base + DW_GPIO_INTMASK, 0);
        qtest_writel(qts, base + DW_GPIO_INTEN, 0);
        qtest_set_irq_in(qts, path, "gpio-in", pin, -1);
        c900_plic_set_enable(qts, 1, controller->irq, false);
        assert_no_irq(qts);
    }

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

static void test_dw_timer_migration(void)
{
    const TH1520Timer *running = &th1520_timers[2];
    const TH1520Timer *pending = &th1520_timers[5];
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-timer-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    g_assert_cmpint(qtest_clock_set(src, 0), ==, 0);

    qtest_writel(src, running->base + DW_TIMER_LOAD_COUNT, 10);
    qtest_writel(src, running->base + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_PERIODIC);
    qtest_clock_step(src, 3 * TH1520_TIMER_TICK_NS + 1);
    g_assert_cmphex(qtest_readl(src,
                                running->base + DW_TIMER_CURRENT_VALUE),
                    ==, 7);

    qtest_writel(src, pending->base + DW_TIMER_LOAD_COUNT, 1);
    qtest_writel(src, pending->base + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_INT_MASK);
    qtest_writel(src,
                  pending->component_base + DW_TIMER_LOAD_COUNT2(3),
                  0x89abcdef);
    qtest_writel(src,
                  pending->component_base + DW_TIMER_PROTECTION(3), 5);
    qtest_clock_step(src, TH1520_TIMER_TICK_NS);
    g_assert_cmphex(qtest_readl(src,
                                running->base + DW_TIMER_CURRENT_VALUE),
                    ==, 6);
    g_assert_cmphex(qtest_readl(src,
                                pending->component_base +
                                DW_TIMERS_RAW_INT_STATUS), ==,
                    BIT(pending->channel));
    g_assert_cmphex(qtest_readl(src,
                                pending->base + DW_TIMER_INT_STATUS), ==,
                    0);

    /* Migration preserves absolute virtual timer deadlines. */
    g_assert_cmpint(qtest_clock_set(dst,
                                    4 * TH1520_TIMER_TICK_NS + 1), ==,
                    4 * TH1520_TIMER_TICK_NS + 1);
    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst,
                                running->base + DW_TIMER_CURRENT_VALUE),
                    ==, 6);
    g_assert_cmphex(qtest_readl(dst, running->base + DW_TIMER_CONTROL), ==,
                    DW_TIMER_ENABLE | DW_TIMER_PERIODIC);
    g_assert_cmphex(qtest_readl(dst,
                                pending->component_base +
                                DW_TIMERS_RAW_INT_STATUS), ==,
                    BIT(pending->channel));
    g_assert_cmphex(qtest_readl(dst,
                                pending->base + DW_TIMER_INT_STATUS), ==,
                    0);
    g_assert_cmphex(qtest_readl(
                        dst,
                        pending->component_base + DW_TIMER_LOAD_COUNT2(3)),
                    ==, 0x89abcdef);
    g_assert_cmphex(qtest_readl(
                        dst,
                        pending->component_base + DW_TIMER_PROTECTION(3)),
                    ==, 5);

    qtest_writel(dst, pending->base + DW_TIMER_CONTROL, DW_TIMER_ENABLE);
    g_assert_cmphex(qtest_readl(dst,
                                pending->base + DW_TIMER_INT_STATUS), ==,
                    1);
    qtest_readl(dst, pending->base + DW_TIMER_EOI);
    qtest_writel(dst, pending->base + DW_TIMER_CONTROL, 0);

    qtest_clock_step(dst, 6 * TH1520_TIMER_TICK_NS - 2);
    g_assert_cmphex(qtest_readl(dst,
                                running->base + DW_TIMER_CURRENT_VALUE),
                    ==, 1);
    g_assert_cmphex(qtest_readl(dst,
                                running->base + DW_TIMER_INT_STATUS), ==,
                    0);
    qtest_clock_step(dst, 1);
    g_assert_cmphex(qtest_readl(dst,
                                running->base + DW_TIMER_CURRENT_VALUE),
                    ==, 0);
    g_assert_cmphex(qtest_readl(dst,
                                running->base + DW_TIMER_INT_STATUS), ==,
                    1);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_th1520_pwm_reset_state(QTestState *qts)
{
    for (unsigned int i = 0; i < 6; i++) {
        g_assert_cmphex(qtest_readl(qts, TH1520_PWM_CTRL(i)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, TH1520_PWM_PERIOD(i)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, TH1520_PWM_FP(i)), ==, 0);
        g_assert_false(qtest_get_irq(qts, i));
    }
}

static void th1520_pwm_stage(QTestState *qts, unsigned int channel,
                              uint32_t ctrl, uint32_t period, uint32_t fp)
{
    qtest_writel(qts, TH1520_PWM_CTRL(channel), ctrl);
    qtest_writel(qts, TH1520_PWM_PERIOD(channel), period);
    qtest_writel(qts, TH1520_PWM_FP(channel), fp);
    qtest_writel(qts, TH1520_PWM_CTRL(channel),
                  ctrl | TH1520_PWM_CFG_UPDATE);
}

static void th1520_pwm_start(QTestState *qts, unsigned int channel,
                              uint32_t ctrl)
{
    qtest_writel(qts, TH1520_PWM_CTRL(channel), ctrl | TH1520_PWM_START);
}

static void test_th1520_pwm_registers(void)
{
    const uint32_t ctrl = TH1520_PWM_CONTINUOUS | TH1520_PWM_FPOUT;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, TH1520_PWM_QOM_PATH, "pwm");
    assert_th1520_pwm_reset_state(qts);

    for (unsigned int i = 0; i < 6; i++) {
        th1520_pwm_stage(qts, i, ctrl, 0x100 + i, 0x80 + i);
        g_assert_cmphex(qtest_readl(qts, TH1520_PWM_CTRL(i)), ==, ctrl);
        g_assert_cmphex(qtest_readl(qts, TH1520_PWM_PERIOD(i)), ==,
                        0x100 + i);
        g_assert_cmphex(qtest_readl(qts, TH1520_PWM_FP(i)), ==, 0x80 + i);
        g_assert_false(qtest_get_irq(qts, i));
    }

    qtest_system_reset(qts);
    assert_th1520_pwm_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_pwm_waveform(void)
{
    const uint32_t normal = TH1520_PWM_CONTINUOUS | TH1520_PWM_FPOUT;
    const uint32_t inverse = TH1520_PWM_CONTINUOUS;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, TH1520_PWM_QOM_PATH, "pwm");
    g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);

    /* A normal waveform is high for FP cycles, then low to period end. */
    th1520_pwm_stage(qts, 0, normal, 10, 3);
    th1520_pwm_start(qts, 0, normal);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, 3 * TH1520_PWM_TICK_NS - 1);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, 1);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, 7 * TH1520_PWM_TICK_NS);
    g_assert_true(qtest_get_irq(qts, 0));

    /* FPOUT clear reverses the phase: low for FP cycles, then high. */
    th1520_pwm_stage(qts, 1, inverse, 10, 4);
    th1520_pwm_start(qts, 1, inverse);
    g_assert_false(qtest_get_irq(qts, 1));
    qtest_clock_step(qts, 4 * TH1520_PWM_TICK_NS - 1);
    g_assert_false(qtest_get_irq(qts, 1));
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_get_irq(qts, 1));

    /* CFG_UPDATE changes a running waveform only at its next boundary. */
    th1520_pwm_stage(qts, 2, normal, 8, 2);
    th1520_pwm_start(qts, 2, normal);
    g_assert_true(qtest_get_irq(qts, 2));
    qtest_clock_step(qts, 2 * TH1520_PWM_TICK_NS);
    g_assert_false(qtest_get_irq(qts, 2));
    th1520_pwm_stage(qts, 2, normal, 10, 6);
    qtest_clock_step(qts, 6 * TH1520_PWM_TICK_NS - 1);
    g_assert_false(qtest_get_irq(qts, 2));
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_get_irq(qts, 2));
    qtest_clock_step(qts, 6 * TH1520_PWM_TICK_NS - 1);
    g_assert_true(qtest_get_irq(qts, 2));
    qtest_clock_step(qts, 1);
    g_assert_false(qtest_get_irq(qts, 2));

    qtest_system_reset(qts);
    assert_th1520_pwm_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_pwm_migration(void)
{
    const uint32_t ctrl = TH1520_PWM_CONTINUOUS | TH1520_PWM_FPOUT;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-pwm-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_irq_intercept_out_named(dst, TH1520_PWM_QOM_PATH, "pwm");
    g_assert_cmpint(qtest_clock_set(src, 0), ==, 0);

    th1520_pwm_stage(src, 0, ctrl, 10, 3);
    th1520_pwm_start(src, 0, ctrl);
    qtest_clock_step(src, TH1520_PWM_TICK_NS);
    th1520_pwm_stage(src, 0, ctrl, 12, 5);

    /* QEMUTimer migration preserves absolute virtual deadlines. */
    g_assert_cmpint(qtest_clock_set(dst, TH1520_PWM_TICK_NS), ==,
                    TH1520_PWM_TICK_NS);
    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_PWM_CTRL(0)), ==, ctrl);
    g_assert_cmphex(qtest_readl(dst, TH1520_PWM_PERIOD(0)), ==, 12);
    g_assert_cmphex(qtest_readl(dst, TH1520_PWM_FP(0)), ==, 5);
    g_assert_true(qtest_get_irq(dst, 0));

    qtest_clock_step(dst, 2 * TH1520_PWM_TICK_NS - 1);
    g_assert_true(qtest_get_irq(dst, 0));
    qtest_clock_step(dst, 1);
    g_assert_false(qtest_get_irq(dst, 0));
    qtest_clock_step(dst, 7 * TH1520_PWM_TICK_NS - 1);
    g_assert_false(qtest_get_irq(dst, 0));
    qtest_clock_step(dst, 1);
    g_assert_true(qtest_get_irq(dst, 0));
    qtest_clock_step(dst, 5 * TH1520_PWM_TICK_NS - 1);
    g_assert_true(qtest_get_irq(dst, 0));
    qtest_clock_step(dst, 1);
    g_assert_false(qtest_get_irq(dst, 0));

    qtest_system_reset(dst);
    assert_th1520_pwm_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_ap_reset_peripherals(void)
{
    const uint32_t ctrl = TH1520_PWM_CONTINUOUS | TH1520_PWM_FPOUT;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, TH1520_PWM_QOM_PATH, "pwm");
    g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);

    th1520_pwm_stage(qts, 0, ctrl, 10, 3);
    th1520_pwm_start(qts, 0, ctrl);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x0c0, 0x2);
    assert_th1520_pwm_reset_state(qts);
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x0c0, 0x3);
    assert_th1520_pwm_reset_state(qts);

    qtest_writel(qts, TH1520_TIMER0_3_BASE + DW_TIMER_LOAD_COUNT, 10);
    qtest_writel(qts, TH1520_TIMER0_3_BASE + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_PERIODIC);
    qtest_writel(qts, TH1520_TIMER4_7_BASE + DW_TIMER_LOAD_COUNT, 10);
    qtest_writel(qts, TH1520_TIMER4_7_BASE + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_PERIODIC);
    qtest_clock_step(qts, 2 * TH1520_TIMER_TICK_NS + 1);
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x03c, 0x2);
    assert_dw_timer_reset_state(qts, TH1520_TIMER0_3_BASE);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_TIMER4_7_BASE + DW_TIMER_CONTROL),
                    ==, DW_TIMER_ENABLE | DW_TIMER_PERIODIC);
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x03c, 0x3);

    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x040, 0x1);
    assert_dw_timer_reset_state(qts, TH1520_TIMER4_7_BASE);
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x040, 0x3);

    qtest_quit(qts);
}

static void test_dw_i2c_migration(void)
{
    static const uint8_t contents[] = { 0xa5, 0x5a };
    uint64_t base = TH1520_I2C0_BASE;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-i2c-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    dw_i2c_eeprom_write(src, 0x123, contents, ARRAY_SIZE(contents));
    dw_i2c_eeprom_queue_read(src, 0x123, DW_I2C_INTR_RX_FULL);
    g_assert_true(qtest_readl(src, base + DW_I2C_RAW_INTR_STAT) &
                  DW_I2C_INTR_RX_FULL);
    g_assert_true(qtest_readl(src, base + DW_I2C_INTR_STAT) &
                  DW_I2C_INTR_RX_FULL);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, base + DW_I2C_RXFLR), ==, 1);
    g_assert_true(qtest_readl(dst, base + DW_I2C_RAW_INTR_STAT) &
                  DW_I2C_INTR_RX_FULL);
    g_assert_true(qtest_readl(dst, base + DW_I2C_INTR_STAT) &
                  DW_I2C_INTR_RX_FULL);
    g_assert_cmphex(qtest_readl(dst, base + DW_I2C_DATA_CMD), ==,
                    contents[0]);
    g_assert_cmphex(qtest_readl(dst, base + DW_I2C_RXFLR), ==, 0);
    dw_i2c_disable(dst, base);

    /* The EEPROM data and its post-read address counter both migrate. */
    g_assert_cmphex(dw_i2c_eeprom_current_read(dst), ==, contents[1]);
    qtest_system_reset(dst);
    g_assert_cmphex(dw_i2c_eeprom_read(dst, 0x124), ==, contents[1]);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_dw_spi_migration(void)
{
    uint64_t base = th1520_spi0.base;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-spi-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    qtest_writel(src, C900_PLIC_PRIORITY(th1520_spi0.irq), 5);
    c900_plic_set_enable(src, 1, th1520_spi0.irq, true);
    qtest_writel(src, base + DW_SSI_CTRLR0,
                  DW_SSI_CTRLR0_DFS_8 | DW_SSI_CTRLR0_SRL);
    qtest_writel(src, base + DW_SSI_RXFTLR, 0);
    qtest_writel(src, base + DW_SSI_SSIENR, 1);
    qtest_writel(src, base + DW_SSI_SER, 1);
    qtest_writel(src, base + DW_SSI_IMR, DW_SSI_INT_RXFI);
    qtest_writel(src, base + DW_SSI_DR, 0x5a);
    g_assert_true(c900_plic_pending(src, th1520_spi0.irq));

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, base + DW_SSI_CTRLR0), ==,
                    DW_SSI_CTRLR0_DFS_8 | DW_SSI_CTRLR0_SRL);
    g_assert_cmphex(qtest_readl(dst, base + DW_SSI_SSIENR), ==, 1);
    g_assert_cmphex(qtest_readl(dst, base + DW_SSI_SER), ==, 1);
    g_assert_cmphex(qtest_readl(dst, base + DW_SSI_IMR), ==,
                    DW_SSI_INT_RXFI);
    g_assert_cmphex(qtest_readl(dst, base + DW_SSI_RXFLR), ==, 1);
    g_assert_true(c900_plic_pending(dst, th1520_spi0.irq));
    g_assert_cmphex(qtest_readl(dst, base + DW_SSI_DR), ==, 0x5a);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_padctrl_migration(void)
{
    static const struct {
        uint64_t base;
        uint32_t pad_offset;
        uint32_t pad_value;
        uint32_t mux_offset;
        uint32_t mux_value;
    } values[] = {
        { TH1520_PADCTRL_AOSYS_BASE, 0x014, 0x01230345,
          0x410, 0x54321012 },
        { TH1520_PADCTRL1_APSYS_BASE, 0x030, 0x03210123,
          0x418, 0x54321012 },
        { TH1520_PADCTRL0_APSYS_BASE, 0x020, 0x02340321,
          0x418, 0x07654321 },
    };
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-padctrl-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
        qtest_writel(src, values[i].base + values[i].pad_offset,
                      values[i].pad_value);
        qtest_writel(src, values[i].base + values[i].mux_offset,
                      values[i].mux_value);
    }

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
        g_assert_cmphex(qtest_readl(dst,
                                    values[i].base + values[i].pad_offset),
                        ==, values[i].pad_value);
        g_assert_cmphex(qtest_readl(dst,
                                    values[i].base + values[i].mux_offset),
                        ==, values[i].mux_value);
    }

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_dw_gpio_migration(void)
{
    const TH1520GPIOController *edge_controller =
        &th1520_gpio_controllers[2];
    const uint32_t edge_pin = 15;
    const uint32_t edge_bit = BIT(edge_pin);
    g_autofree char *edge_path =
        g_strdup_printf("/machine/soc/%s", edge_controller->name);
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-gpio-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    for (size_t i = 0; i < ARRAY_SIZE(th1520_gpio_controllers); i++) {
        uint64_t base = th1520_gpio_controllers[i].base;

        qtest_writel(src, base + DW_GPIO_SWPORTA_DR, BIT(i));
        qtest_writel(src, base + DW_GPIO_SWPORTA_DDR, BIT(i));
        qtest_writel(src, base + DW_GPIO_SWPORTA_CTL, BIT(i + 6));
        qtest_writel(src, base + DW_GPIO_PORTA_DEBOUNCE, BIT(i + 8));
        qtest_writel(src, base + DW_GPIO_LS_SYNC, i & 1);
    }

    qtest_set_irq_in(src, edge_path, "gpio-in", edge_pin, 0);
    qtest_writel(src, edge_controller->base + DW_GPIO_INTTYPE_LEVEL,
                  edge_bit);
    qtest_writel(src, edge_controller->base + DW_GPIO_INT_POLARITY,
                  edge_bit);
    qtest_writel(src, edge_controller->base + DW_GPIO_INTEN, edge_bit);
    qtest_set_irq_in(src, edge_path, "gpio-in", edge_pin, 1);
    g_assert_cmphex(qtest_readl(src, edge_controller->base +
                                DW_GPIO_INTSTATUS), ==, edge_bit);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    for (size_t i = 0; i < ARRAY_SIZE(th1520_gpio_controllers); i++) {
        uint64_t base = th1520_gpio_controllers[i].base;

        g_assert_cmphex(qtest_readl(dst, base + DW_GPIO_SWPORTA_DR), ==,
                        BIT(i));
        g_assert_cmphex(qtest_readl(dst, base + DW_GPIO_SWPORTA_DDR), ==,
                        BIT(i));
        g_assert_cmphex(qtest_readl(dst, base + DW_GPIO_SWPORTA_CTL), ==,
                        BIT(i + 6));
        g_assert_cmphex(qtest_readl(dst, base + DW_GPIO_PORTA_DEBOUNCE),
                        ==, BIT(i + 8));
        g_assert_cmphex(qtest_readl(dst, base + DW_GPIO_LS_SYNC), ==,
                        i & 1);
    }
    g_assert_cmphex(qtest_readl(dst, edge_controller->base +
                                DW_GPIO_EXT_PORTA) & edge_bit, ==,
                    edge_bit);
    g_assert_cmphex(qtest_readl(dst, edge_controller->base +
                                DW_GPIO_RAW_INTSTATUS), ==, edge_bit);
    g_assert_cmphex(qtest_readl(dst, edge_controller->base +
                                DW_GPIO_INTSTATUS), ==, edge_bit);
    qtest_writel(dst, edge_controller->base + DW_GPIO_PORTA_EOI,
                  edge_bit);
    g_assert_cmphex(qtest_readl(dst, edge_controller->base +
                                DW_GPIO_INTSTATUS), ==, 0);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_ap_cpr_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    uint32_t unlocked = TH1520_PLL_RESET_LOCKS & ~BIT(1);
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-cpr-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_irq_intercept_out_named(dst, TH1520_AP_RESET_QOM_PATH,
                                  "peripheral-reset");

    g_assert_cmpint(qtest_clock_set(src, 0), ==, 0);
    qtest_clock_step(src, TH1520_PLL_LOCK_TIME_NS);
    g_assert_cmphex(qtest_readl(src,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==,
                    TH1520_PLL_RESET_LOCKS);

    qtest_writel(src, TH1520_AP_CLOCK_BASE + 0x004,
                  0x03000000 | TH1520_PLL_VCO_RST);
    qtest_writel(src, TH1520_AP_CLOCK_BASE + 0x004, 0x03000000);
    qtest_clock_step(src, 10000);
    g_assert_cmphex(qtest_readl(src,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==,
                    unlocked);
    qtest_writel(src, TH1520_AP_CLOCK_BASE + TH1520_PERI_CLK_CFG, 0);
    qtest_writel(src, TH1520_AP_RESET_BASE + 0x004, 0x1f);
    qtest_writel(src, TH1520_AP_RESET_BASE + 0x1b0, 1);
    qtest_writel(src, TH1520_AP_RESET_BASE + 0x0c0, 0x2);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==,
                    unlocked);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_AP_CLOCK_BASE + TH1520_PERI_CLK_CFG),
                    ==, 0);
    g_assert_cmphex(qtest_readl(dst, TH1520_AP_RESET_BASE + 0x004), ==,
                    0x1f);
    g_assert_cmphex(qtest_readl(dst, TH1520_AP_RESET_BASE + 0x1b0), ==, 1);
    g_assert_cmphex(qtest_readl(dst, TH1520_AP_RESET_BASE + 0x0c0), ==, 2);
    g_assert_true(qtest_get_irq(dst, 0));
    g_assert_false(qtest_get_irq(dst, 1));
    g_assert_false(qtest_get_irq(dst, 2));

    qtest_clock_step(dst, TH1520_PLL_LOCK_TIME_NS - 10000 - 1);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==,
                    unlocked);
    qtest_clock_step(dst, 1);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_AP_CLOCK_BASE + TH1520_PLL_STS), ==,
                    TH1520_PLL_RESET_LOCKS);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_dmac_migration(void)
{
    static const uint8_t source[64] = {
        0x5a, 0xa5, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc,
    };
    uint8_t destination[sizeof(source)];
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    uint32_t irq_mask = DMAC_IRQ_BLOCK_TRANSFER | DMAC_IRQ_DMA_TRANSFER |
                        DMAC_IRQ_ALL_ERRORS;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-dmac-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    qtest_memwrite(src, DMAC_TEST_SOURCE_ADDR, source, sizeof(source));
    qtest_writeq(src, TH1520_DMAC0_BASE + DMAC_CH_SAR(2),
                  0x1122334455667788ULL);
    qtest_writeq(src, TH1520_DMAC0_BASE + DMAC_CH_CFG(2),
                  0x8877665544332211ULL);

    qtest_writeq(src, TH1520_DMAC0_BASE + DMAC_CH_SAR(3),
                  DMAC_TEST_SOURCE_ADDR);
    qtest_writeq(src, TH1520_DMAC0_BASE + DMAC_CH_DAR(3),
                  DMAC_TEST_DEST_ADDR);
    qtest_writeq(src, TH1520_DMAC0_BASE + DMAC_CH_BLOCK_TS(3),
                  sizeof(source) - 1);
    qtest_writel(src, TH1520_DMAC0_BASE + DMAC_CH_INTSTATUS_EN(3),
                  irq_mask);
    qtest_writel(src, TH1520_DMAC0_BASE + DMAC_CH_INTSIGNAL_EN(3),
                  DMAC_IRQ_DMA_TRANSFER | DMAC_IRQ_ALL_ERRORS);
    qtest_writel(src, TH1520_DMAC0_BASE + DMAC_CFG,
                  DMAC_CFG_ENABLE | DMAC_CFG_INTERRUPT_ENABLE);
    qtest_writel(src, TH1520_DMAC0_BASE + DMAC_CHEN,
                  DMAC_CH_ENABLE(3) | DMAC_CH_ENABLE_WE(3));
    g_assert_true(c900_plic_pending(src, TH1520_DMAC0_IRQ));

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    qtest_memread(dst, DMAC_TEST_DEST_ADDR, destination,
                  sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination), source, sizeof(source));
    g_assert_cmphex(qtest_readq(dst, TH1520_DMAC0_BASE +
                                DMAC_CH_SAR(2)), ==,
                    0x1122334455667788ULL);
    g_assert_cmphex(qtest_readq(dst, TH1520_DMAC0_BASE +
                                DMAC_CH_CFG(2)), ==,
                    0x8877665544332211ULL);
    g_assert_cmphex(qtest_readq(dst, TH1520_DMAC0_BASE +
                                DMAC_CH_SAR(3)), ==,
                    DMAC_TEST_SOURCE_ADDR + sizeof(source));
    g_assert_cmphex(qtest_readq(dst, TH1520_DMAC0_BASE +
                                DMAC_CH_DAR(3)), ==,
                    DMAC_TEST_DEST_ADDR + sizeof(source));
    g_assert_cmphex(qtest_readq(dst, TH1520_DMAC0_BASE +
                                DMAC_CH_STATUS(3)), ==,
                    sizeof(source) - 1);
    g_assert_cmphex(qtest_readl(dst, TH1520_DMAC0_BASE +
                                DMAC_CH_INTSTATUS(3)), ==,
                    DMAC_IRQ_BLOCK_TRANSFER | DMAC_IRQ_DMA_TRANSFER);
    g_assert_true(c900_plic_pending(dst, TH1520_DMAC0_IRQ));

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
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

static void test_th1520_mbox_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-mbox-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_irq_intercept_out_named(dst, C900_PLIC_QOM_PATH, "sext");

    qtest_writel(src, TH1520_MBOX_CHANNEL(2) + TH1520_MBOX_INFO(0),
                 0x10203040);
    qtest_writel(src, TH1520_MBOX_CHANNEL(2) + TH1520_MBOX_INFO(7),
                 0x50607080);
    qtest_writel(src, TH1520_MBOX_CHANNEL(2) + TH1520_MBOX_GENERATE,
                 0xc0);
    qtest_writel(src, TH1520_MBOX_REMOTE0_CHANNEL + TH1520_MBOX_INFO(3),
                 0xabcdef01);
    qtest_writel(src, TH1520_MBOX_REMOTE2_BASE + TH1520_MBOX_GENERATE,
                 0x40);
    qtest_writel(src, TH1520_MBOX_LOCAL_BASE + TH1520_MBOX_MASK, BIT(2));
    qtest_writel(src, C900_PLIC_PRIORITY(TH1520_MBOX_IRQ), 5);
    c900_plic_set_enable(src, 1, TH1520_MBOX_IRQ, true);
    qtest_set_irq_in(src, TH1520_MBOX_QOM_PATH, "remote-event", 2, 1);
    g_assert_true(c900_plic_pending(src, TH1520_MBOX_IRQ));

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst,
                                TH1520_MBOX_CHANNEL(2) +
                                TH1520_MBOX_INFO(0)), ==, 0x10203040);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_MBOX_CHANNEL(2) +
                                TH1520_MBOX_INFO(7)), ==, 0x50607080);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_MBOX_CHANNEL(2) +
                                TH1520_MBOX_GENERATE), ==, 0xc0);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_MBOX_REMOTE0_CHANNEL +
                                TH1520_MBOX_INFO(3)), ==, 0xabcdef01);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_MBOX_REMOTE2_BASE +
                                TH1520_MBOX_GENERATE), ==, 0x40);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_MBOX_LOCAL_BASE +
                                TH1520_MBOX_MASK), ==, BIT(2));
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_MBOX_LOCAL_BASE +
                                TH1520_MBOX_STATUS), ==, BIT(2));
    g_assert_true(c900_plic_pending(dst, TH1520_MBOX_IRQ));
    assert_only_irq(dst, 0);
    g_assert_cmphex(qtest_readl(dst, C900_PLIC_CLAIM(1)), ==,
                    TH1520_MBOX_IRQ);
    qtest_writel(dst, TH1520_MBOX_LOCAL_BASE + TH1520_MBOX_CLEAR, BIT(2));
    qtest_writel(dst, C900_PLIC_CLAIM(1), TH1520_MBOX_IRQ);
    g_assert_false(c900_plic_pending(dst, TH1520_MBOX_IRQ));
    assert_no_irq(dst);

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
    for (size_t i = 1; i < ARRAY_SIZE(th1520_uart_controllers); i++) {
        qtest_writel(src, th1520_uart_controllers[i].base +
                     DW_UART_SCR_OFFSET, 0x60 + i);
    }

    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_FRAME_FILTER, 0x80000401);
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE, 0x00020180);
    qtest_writel(src, TH1520_GMAC0_APB_BASE + GMAC_APB_RXCLK_DELAY,
                  0x00004015);
    gmac_mdio_write(src, TH1520_GMAC0_BASE, TH1520_GMAC_PHY_ADDR,
                    MII_BMCR,
                    MII_BMCR_AUTOEN | MII_BMCR_FD | MII_BMCR_SPEED100);
    qtest_writel(src, TH1520_GMAC1_BASE + DWMAC_FRAME_FILTER, 0x80000010);
    qtest_writel(src, TH1520_GMAC1_APB_BASE + GMAC_APB_PLLCLK_DIV,
                  0x80000008);
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
    for (size_t i = 1; i < ARRAY_SIZE(th1520_uart_controllers); i++) {
        g_assert_cmphex(qtest_readl(dst, th1520_uart_controllers[i].base +
                                    DW_UART_SCR_OFFSET), ==, 0x60 + i);
    }
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

    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC0_BASE + DWMAC_FRAME_FILTER), ==,
                    0x80000401);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE), ==,
                    0x00020180);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC0_APB_BASE +
                                GMAC_APB_RXCLK_DELAY), ==, 0x00004015);
    g_assert_cmphex(gmac_mdio_read(dst, TH1520_GMAC0_BASE,
                                  TH1520_GMAC_PHY_ADDR, MII_BMCR), ==,
                    MII_BMCR_AUTOEN | MII_BMCR_FD | MII_BMCR_SPEED100);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC1_BASE + DWMAC_FRAME_FILTER), ==,
                    0x80000010);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC1_APB_BASE +
                                GMAC_APB_PLLCLK_DIV), ==, 0x80000008);

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
    qtest_writel(src, TH1520_AP_CLOCK_BASE + TH1520_PERI_CLK_CFG, 0);
    qtest_writel(src, TH1520_AP_RESET_BASE + 0x1b0, 1);
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
    for (size_t i = 1; i < ARRAY_SIZE(th1520_uart_controllers); i++) {
        qtest_writel(src, th1520_uart_controllers[i].base +
                     DW_UART_SCR_OFFSET, 0x60 + i);
    }
    for (size_t i = 0; i < ARRAY_SIZE(th1520_gpio_controllers); i++) {
        uint64_t base = th1520_gpio_controllers[i].base;

        qtest_writel(src, base + DW_GPIO_SWPORTA_DR, BIT(i));
        qtest_writel(src, base + DW_GPIO_SWPORTA_DDR, BIT(i));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th1520_padctrls); i++) {
        uint32_t value = (i + 1) * 0x00010001;

        qtest_writel(src, th1520_padctrls[i].base + 0x014, value);
        qtest_writel(src, th1520_padctrls[i].base + 0x410,
                      0x11111111 * (i + 1));
    }

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

    g_assert_cmphex(qtest_readl(dst,
                                TH1520_AP_CLOCK_BASE +
                                TH1520_PERI_CLK_CFG), ==, 0);
    g_assert_cmphex(qtest_readl(dst, TH1520_AP_RESET_BASE + 0x1b0), ==, 1);

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
    for (size_t i = 1; i < ARRAY_SIZE(th1520_uart_controllers); i++) {
        g_assert_cmphex(qtest_readl(dst, th1520_uart_controllers[i].base +
                                    DW_UART_SCR_OFFSET), ==, 0x60 + i);
    }
    for (size_t i = 0; i < ARRAY_SIZE(th1520_gpio_controllers); i++) {
        uint64_t base = th1520_gpio_controllers[i].base;

        g_assert_cmphex(qtest_readl(dst, base + DW_GPIO_SWPORTA_DR), ==,
                        BIT(i));
        g_assert_cmphex(qtest_readl(dst, base + DW_GPIO_SWPORTA_DDR), ==,
                        BIT(i));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th1520_padctrls); i++) {
        g_assert_cmphex(qtest_readl(dst,
                                    th1520_padctrls[i].base + 0x014), ==,
                        (i + 1) * 0x00010001);
        g_assert_cmphex(qtest_readl(dst,
                                    th1520_padctrls[i].base + 0x410), ==,
                        0x11111111 * (i + 1));
    }
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
        qtest_add_func("/beaglev-ahead/boot/external-dtb",
                       test_external_dtb);
        qtest_add_func("/beaglev-ahead/cpr/clock-registers",
                       test_ap_clock_registers);
        qtest_add_func("/beaglev-ahead/cpr/reset-registers",
                       test_ap_reset_registers);
        qtest_add_func("/beaglev-ahead/cpr/reset-outputs",
                       test_ap_reset_outputs);
        qtest_add_func("/beaglev-ahead/cpr/migration",
                       test_ap_cpr_migration);
        qtest_add_func("/beaglev-ahead/padctrl/registers",
                       test_padctrl_registers);
        qtest_add_func("/beaglev-ahead/padctrl/migration",
                       test_padctrl_migration);
        qtest_add_func("/beaglev-ahead/dw-gpio/registers",
                       test_dw_gpio_registers);
        qtest_add_func("/beaglev-ahead/dw-gpio/interrupts",
                       test_dw_gpio_interrupts);
        qtest_add_func("/beaglev-ahead/dw-gpio/migration",
                       test_dw_gpio_migration);
        qtest_add_func("/beaglev-ahead/dw-i2c/registers",
                       test_dw_i2c_registers);
        qtest_add_func("/beaglev-ahead/dw-i2c/eeprom",
                       test_dw_i2c_eeprom);
        qtest_add_func("/beaglev-ahead/dw-i2c/eeprom-backing",
                       test_dw_i2c_eeprom_backing);
        qtest_add_func("/beaglev-ahead/dw-i2c/interrupts",
                       test_dw_i2c_interrupts);
        qtest_add_func("/beaglev-ahead/dw-i2c/migration",
                       test_dw_i2c_migration);
        qtest_add_func("/beaglev-ahead/dw-spi/registers",
                       test_dw_spi_registers);
        qtest_add_func("/beaglev-ahead/dw-spi/loopback-interrupt",
                       test_dw_spi_loopback_and_interrupt);
        qtest_add_func("/beaglev-ahead/dw-spi/error-status",
                       test_dw_spi_error_status);
        qtest_add_func("/beaglev-ahead/dw-spi/migration",
                       test_dw_spi_migration);
        qtest_add_func("/beaglev-ahead/th1520-pwm/registers",
                       test_th1520_pwm_registers);
        qtest_add_func("/beaglev-ahead/th1520-pwm/waveform",
                       test_th1520_pwm_waveform);
        qtest_add_func("/beaglev-ahead/th1520-pwm/migration",
                       test_th1520_pwm_migration);
        qtest_add_func("/beaglev-ahead/cpr/peripheral-resets",
                       test_ap_reset_peripherals);
        qtest_add_func("/beaglev-ahead/th1520-mbox/registers",
                       test_th1520_mbox_registers);
        qtest_add_func("/beaglev-ahead/th1520-mbox/migration",
                       test_th1520_mbox_migration);
        qtest_add_func("/beaglev-ahead/dw-timer/registers",
                       test_dw_timer_registers);
        qtest_add_func("/beaglev-ahead/dw-timer/timing",
                       test_dw_timer_timing);
        qtest_add_func("/beaglev-ahead/dw-timer/interrupt-routes",
                       test_dw_timer_interrupt_routes);
        qtest_add_func("/beaglev-ahead/dw-timer/migration",
                       test_dw_timer_migration);
        qtest_add_func("/beaglev-ahead/dmac/registers",
                       test_dmac_registers);
        qtest_add_func("/beaglev-ahead/dmac/direct-transfer",
                       test_dmac_direct_transfer);
        qtest_add_func("/beaglev-ahead/dmac/linked-list",
                       test_dmac_linked_list);
        qtest_add_func("/beaglev-ahead/dmac/migration",
                       test_dmac_migration);
        qtest_add_func("/beaglev-ahead/migration/whole-machine",
                       test_whole_machine_migration);
        qtest_add_func("/beaglev-ahead/gmac/registers",
                       test_gmac_registers);
        for (size_t i = 0; i < ARRAY_SIZE(th1520_gmac_controllers); i++) {
            g_autofree char *name =
                g_strdup_printf("/beaglev-ahead/gmac/%s-interrupt",
                                th1520_gmac_controllers[i].name);

            qtest_add_data_func(name, &th1520_gmac_controllers[i],
                                test_gmac_interrupt);
        }
#ifndef _WIN32
        qtest_add_func("/beaglev-ahead/gmac/enhanced-descriptors",
                       test_gmac_enhanced_descriptors);
#endif
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
        qtest_add_func("/beaglev-ahead/dw-uart/instances",
                       test_dw_uart_instances);
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
