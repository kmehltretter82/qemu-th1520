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
#include "hw/misc/th1520_aon_reset.h"
#include "hw/misc/th1520_ap6203bm.h"
#include "hw/misc/th1520_bootsel.h"
#include "hw/misc/th1520_cpr.h"
#include "hw/misc/th1520_ddr.h"
#include "hw/misc/th1520_ddr_control.h"
#include "hw/misc/th1520_ddr_pll.h"
#include "hw/misc/th1520_iopmp.h"
#include "hw/misc/th1520_iso7816.h"
#include "hw/misc/th1520_miscsys.h"
#include "hw/misc/th1520_pmp_portal.h"
#include "hw/misc/th1520_tee_dsp_reset.h"
#include "hw/misc/th1520_tee_miscsys_clock.h"
#include "hw/misc/th1520_tee_vosys_dpu_reset.h"
#include "hw/misc/th1520_video_sysreg.h"
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
#define TH1520_PMP_PORTAL_BASE     0xffdc020000ULL
#define TH1520_PLIC_BASE           0xffd8000000ULL
#define TH1520_SRAM_BASE           0xffe0000000ULL
#define TH1520_AP_CLOCK_BASE       0xffef010000ULL
#define TH1520_DDR_CONTROLLER_BASE 0xffff000000ULL
#define TH1520_DDR_PHY0_BASE       0xfffd000000ULL
#define TH1520_DDR_PHY1_BASE       0xfffe000000ULL
#define TH1520_DDR_CFG0_BASE       0xffff005000ULL
#define TH1520_DDR_CFG1_BASE       0xffff005004ULL
#define TH1520_DDR_PLL_CFG0_BASE   0xffff005008ULL
#define TH1520_DDR_PLL_CFG1_BASE   0xffff00500cULL
#define TH1520_DDR_PLL_STS_BASE    0xffff005018ULL
#define TH1520_VENDOR_UBOOT_AP_CLOCK_BASE 0xffff011000ULL
#define TH1520_VENDOR_UBOOT_AP_RESET_NPU_BASE 0xffff0151b0ULL
#define TH1520_VENDOR_UBOOT_USB_CLOCK_BASE 0xfffc02d104ULL
#define TH1520_TEE_MISCSYS_CLOCK_BASE 0xfffc02d120ULL
#define TH1520_TEE_DSP_RESET_BASE    0xffff041028ULL
#define TH1520_TEE_VOSYS_DPU_RESET_BASE 0xffff529004ULL
#define TH1520_AON_AUDIO_RESET_BASE  0xfffff4403cULL
#define TH1520_AP_RESET_BASE       0xffef014000ULL
#define TH1520_MISCSYS_BASE        0xffec02c000ULL
#define TH1520_VISYS_BASE          0xffe4040000ULL
#define TH1520_VOSYS_BASE          0xffef528000ULL
#define TH1520_ISO7816_CONFIG_BASE 0xfff7f30010ULL
#define TH1520_BOOTSEL_BASE         0xffef018010ULL
#define TH1520_USB_DRD_BASE        0xffec03f000ULL
#define TH1520_USB_CORE_BASE       0xffe7040000ULL
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
#define TH1520_AON_I2C_BASE        0xfffff4c000ULL
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
#define TH1520_WDT0_BASE           0xffefc30000ULL
#define TH1520_WDT1_BASE           0xffefc31000ULL
#define TH1520_MBOX_LOCAL_BASE      0xffffc38000ULL
#define TH1520_MBOX_REMOTE0_BASE    0xffffc40000ULL
#define TH1520_MBOX_REMOTE1_BASE    0xffffc4c000ULL
#define TH1520_MBOX_REMOTE2_BASE    0xffffc54000ULL
#define TH1520_PVT_COMMON_BASE      0xfffff4e000ULL
#define TH1520_PVT_TS_BASE          0xfffff4e080ULL
#define TH1520_PVT_PD_BASE          0xfffff4e180ULL
#define TH1520_PVT_VM_BASE          0xfffff4e800ULL
#define TH1520_RTC_BASE             0xfffff40000ULL
#define TH1520_DMAC0_BASE          0xffefc00000ULL
#define TH1520_GMAC1_BASE          0xffe7060000ULL
#define TH1520_GMAC0_BASE          0xffe7070000ULL
#define TH1520_EMMC_BASE           0xffe7080000ULL
#define TH1520_SDIO0_BASE          0xffe7090000ULL
#define TH1520_SDIO1_BASE          0xffe70a0000ULL
#define TH1520_GMAC0_APB_BASE      0xffec003000ULL
#define TH1520_GMAC1_APB_BASE      0xffec004000ULL
#define TH1520_AP6203BM_QOM_PATH   "/machine/ap6203bm"
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

#define AP6203BM_WL_REG_ON_GPIO    31
#define AP6203BM_BT_REG_ON_GPIO    28
#define AP6203BM_BT_WAKE_HOST_GPIO 30
#define AP6203BM_WL_HOST_WAKE_GPIO 25
#define AP6203BM_BT_HOST_WAKE_GPIO 29
#define AP6203BM_CONTROL_MASK      (BIT(AP6203BM_WL_REG_ON_GPIO) | \
                                   BIT(AP6203BM_BT_REG_ON_GPIO) | \
                                   BIT(AP6203BM_BT_WAKE_HOST_GPIO))
#define AP6203BM_HOST_WAKE_MASK    (BIT(AP6203BM_WL_HOST_WAKE_GPIO) | \
                                   BIT(AP6203BM_BT_HOST_WAKE_GPIO))

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
#define DW_I2C_TXFLR               0x74
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
#define DW_I2C_INTR_STOP_DET       BIT(9)

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

#define DW_WDT_CR                  0x00
#define DW_WDT_TORR                0x04
#define DW_WDT_CCVR                0x08
#define DW_WDT_CRR                 0x0c
#define DW_WDT_STAT                0x10
#define DW_WDT_EOI                 0x14
#define DW_WDT_COMP_PARAM_5        0xe4
#define DW_WDT_COMP_PARAM_4        0xe8
#define DW_WDT_COMP_PARAM_3        0xec
#define DW_WDT_COMP_PARAM_2        0xf0
#define DW_WDT_COMP_PARAM_1        0xf4
#define DW_WDT_COMP_VERSION        0xf8
#define DW_WDT_COMP_TYPE           0xfc

#define DW_WDT_ENABLE              BIT(0)
#define DW_WDT_RMOD                BIT(1)
#define DW_WDT_RESTART             0x76
#define DW_WDT_FIXED_TOP           BIT(6)
#define DW_WDT_COMPONENT_TYPE      0x44570120
#define DW_WDT_TICK_NS             8
#define DW_WDT_TOP0_COUNT          BIT(16)
#define DW_WDT_TOP0_NS             (DW_WDT_TOP0_COUNT * DW_WDT_TICK_NS)

#define XGENE_RTC_CCVR             0x00
#define XGENE_RTC_CMR              0x04
#define XGENE_RTC_CLR              0x08
#define XGENE_RTC_CCR              0x0c
#define XGENE_RTC_STAT             0x10
#define XGENE_RTC_RSTAT            0x14
#define XGENE_RTC_EOI              0x18
#define XGENE_RTC_VER              0x1c
#define XGENE_RTC_CPSR             0x20
#define XGENE_RTC_CPCVR            0x24

#define XGENE_RTC_IE               BIT(0)
#define XGENE_RTC_MASK             BIT(1)
#define XGENE_RTC_EN               BIT(2)
#define XGENE_RTC_WEN              BIT(3)
#define XGENE_RTC_PSCLR_EN         BIT(4)
#define XGENE_RTC_PRESCALER        32768
#define XGENE_RTC_SECOND_NS        1000000000LL
#define XGENE_RTC_TEST_EPOCH       946684800

#define TH1520_MBOX_STATUS         0x000
#define TH1520_MBOX_CLEAR          0x004
#define TH1520_MBOX_MASK           0x00c
#define TH1520_MBOX_GENERATE       0x010
#define TH1520_MBOX_INFO(word)     (0x014 + 4 * (word))
#define TH1520_MBOX_CHANNEL(channel) \
    (TH1520_MBOX_LOCAL_BASE + 0x1000 * (channel))
#define TH1520_MBOX_REMOTE0_CHANNEL (TH1520_MBOX_REMOTE0_BASE + 0x4000)

#define MR75203_COMP_ID             0x00
#define MR75203_IP_CONFIG           0x04
#define MR75203_ID_NUM              0x08
#define MR75203_SCRATCH             0x0c
#define MR75203_REG_LOCK            0x10
#define MR75203_LOCK_STATUS         0x14
#define MR75203_CLK_SYNTH           0x00
#define MR75203_SDIF_DISABLE        0x04
#define MR75203_SDIF_STATUS         0x08
#define MR75203_SDIF_W              0x0c
#define MR75203_SDIF_HALT           0x10
#define MR75203_SDIF_CTRL           0x14
#define MR75203_SAMPLE_CTRL         0x20
#define MR75203_SAMPLE_CLEAR        0x24
#define MR75203_SAMPLE_COUNT        0x28
#define MR75203_SDIF_DONE(n)        (0x54 + 0x40 * (n))
#define MR75203_SDIF_DATA(n)        (0x58 + 0x40 * (n))
#define MR75203_VM_DONE(vm)         (0x234 + 0x200 * (vm))
#define MR75203_VM_DATA(vm, ch)     (0x240 + 0x200 * (vm) + 4 * (ch))

#define MR75203_UNLOCK_VALUE        0x1acce551
#define MR75203_CLK_SYNTH_VALUE     0x01050505
#define MR75203_SDIF_LOCK           BIT(1)
#define MR75203_TS_CONFIG_WRITE     0x89000001
#define MR75203_TS_TIMER_WRITE      0x8d000100
#define MR75203_TS_CTRL_WRITE       0x8800010a
#define MR75203_VM_POLL_WRITE       0x8c10ffff
#define MR75203_VM_CONFIG_WRITE     0x89000000
#define MR75203_VM_TIMER_WRITE      0x8d000040
#define MR75203_VM_CTRL_WRITE       0x8800050a
#define MR75203_INPUT_FREQUENCY     73728000
#define MR75203_TS_COEFF_G          42740
#define MR75203_TS_COEFF_H          220500
#define MR75203_TS_COEFF_J          (-160)
#define MR75203_TS_COEFF_CAL5       4094

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
#define CSR_FCSR                   0x003
#define CSR_MSTATUS                0x300
#define CSR_MSCRATCH               0x340
#define CSR_TH_MCOR                0x7c2
#define CSR_TH_MCOUNTERWEN         0x7c9
#define CSR_TH_FXCR                0x800
#define CSR_TH_CPUID               0xfc0

#define MIP_SSIP                   BIT(1)
#define MIP_MSIP                   BIT(3)

#define C910_HARTS                 4
#define C900_PLIC_CONTEXTS         (C910_HARTS * 2)
#define C900_PLIC_WORDS            8
#define C900_CLINT_QOM_PATH        "/machine/soc/clint"
#define C900_PLIC_QOM_PATH         "/machine/soc/plic"
#define DW_UART_QOM_PATH           "/machine/soc/uart0"
#define TH1520_AP_CLOCK_QOM_PATH   "/machine/soc/ap-clock"
#define TH1520_AP_RESET_QOM_PATH   "/machine/soc/ap-reset"
#define TH1520_EMMC_QOM_PATH       "/machine/soc/emmc"
#define TH1520_MISCSYS_QOM_PATH    "/machine/soc/miscsys"
#define TH1520_MBOX_QOM_PATH       "/machine/soc/mbox"
#define TH1520_PVT_QOM_PATH        "/machine/soc/pvt"
#define TH1520_PWM_QOM_PATH        "/machine/soc/pwm"
#define TH1520_TIMER0_3_QOM_PATH   "/machine/soc/timer0-3"
#define TH1520_TIMER4_7_QOM_PATH   "/machine/soc/timer4-7"

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
#define TH1520_AON_I2C_IRQ         79
#define TH1520_I2C0_IRQ            44
#define TH1520_I2C1_IRQ            45
#define TH1520_I2C2_IRQ            46
#define TH1520_I2C3_IRQ            47
#define TH1520_I2C4_IRQ            48
#define TH1520_I2C5_IRQ            49
#define TH1520_SPI0_IRQ            54
#define TH1520_TIMER0_IRQ          16
#define TH1520_WDT0_IRQ            24
#define TH1520_WDT1_IRQ            25
#define TH1520_MBOX_IRQ            28
#define TH1520_DMAC0_IRQ           27
#define TH1520_EMMC_IRQ            62
#define TH1520_SDIO0_IRQ           64
#define TH1520_GMAC0_IRQ           66
#define TH1520_GMAC1_IRQ           67
#define TH1520_SDIO1_IRQ           71
#define TH1520_USB_IRQ             68
#define TH1520_RTC_IRQ             74

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
#define TH1520_CLK_WDT0            76
#define TH1520_CLK_WDT1            77
#define TH1520_CLK_UART_SCLK       85

#define TH1520_I2C_COMP_PARAM1     0x000f0fee
#define TH1520_I2C_COMP_VERSION    0x3230322a
#define TH1520_I2C_COMP_TYPE       0x44570140
#define TH1520_I2C_INTR_RESET      0x000048ff
#define TH1520_I2C_INTR_VALID      0x00004fff
#define BEAGLEV_AHEAD_EEPROM_ADDR  0x50
#define BEAGLEV_AHEAD_PMIC_ADDR    0x5a
#define DA9063_REG_CONTROL_D       0x11
#define DA9063_REG_DVC_1           0x32
#define DA9063_REG_DVC_2           0x33
#define DA9063_REG_VBCORE2_A       0xa3
#define DA9063_REG_VBCORE1_A       0xa4
#define DA9063_REG_VBIO_A          0xa7
#define DA9063_REG_VBCORE2_B       0xb4
#define DA9063_REG_VBCORE1_B       0xb5
#define DA9063_REG_VBIO_B          0xb8
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

#define TH1520_MISCSYS_USB_SWRST   0x014
#define TH1520_MISCSYS_BUS_CLK     0x100
#define TH1520_MISCSYS_USB_CLK     0x104
#define TH1520_MISCSYS_EMMC_CLK    0x108
#define TH1520_MISCSYS_SDIO0_CLK   0x10c
#define TH1520_MISCSYS_SDIO1_CLK   0x110

#define TH1520_USB_GCTL            0xc110
#define TH1520_USB_GSNPSID         0xc120
#define TH1520_USB_GHWPARAMS0      0xc140
#define TH1520_USB_GHWPARAMS1      0xc144
#define TH1520_USB_DCTL            0xc704
#define TH1520_USB_XHCI_CAPLENGTH  0x0000
#define TH1520_USB_XHCI_HCSPARAMS1 0x0004
#define TH1520_USB_XHCI_DBOFF      0x0014
#define TH1520_USB_XHCI_RTSOFF     0x0018
#define TH1520_USB_XHCI_USB2_PORTS 0x0028
#define TH1520_USB_XHCI_USB3_PORTS 0x0038
#define TH1520_USB_XHCI_OPER       0x0040
#define TH1520_USB_XHCI_USBCMD     (TH1520_USB_XHCI_OPER + 0x00)
#define TH1520_USB_XHCI_USBSTS     (TH1520_USB_XHCI_OPER + 0x04)
#define TH1520_USB_XHCI_DNCTRL     (TH1520_USB_XHCI_OPER + 0x14)
#define TH1520_USB_XHCI_CRCR       (TH1520_USB_XHCI_OPER + 0x18)
#define TH1520_USB_XHCI_CONFIG     (TH1520_USB_XHCI_OPER + 0x38)
#define TH1520_USB_XHCI_USB2_PORTSC (TH1520_USB_XHCI_OPER + 0x410)
#define TH1520_USB_XHCI_RUNTIME    0x1000
#define TH1520_USB_XHCI_IMAN       (TH1520_USB_XHCI_RUNTIME + 0x20)
#define TH1520_USB_XHCI_ERSTSZ     (TH1520_USB_XHCI_RUNTIME + 0x28)
#define TH1520_USB_XHCI_ERSTBA     (TH1520_USB_XHCI_RUNTIME + 0x30)
#define TH1520_USB_XHCI_ERDP       (TH1520_USB_XHCI_RUNTIME + 0x38)
#define TH1520_USB_XHCI_DOORBELL   0x2000

#define TH1520_USB_GCTL_RESET      0x30c13004
#define TH1520_USB_GSNPSID_QEMU    0x5533330a
#define TH1520_USB_DCTL_RUN_STOP   BIT(31)
#define TH1520_USB_DCTL_CSFTRST    BIT(30)
#define TH1520_USB_XHCI_USBCMD_RS  BIT(0)
#define TH1520_USB_XHCI_USBCMD_INTE BIT(2)
#define TH1520_USB_XHCI_USBSTS_HCH BIT(0)
#define TH1520_USB_XHCI_USBSTS_EINT BIT(3)
#define TH1520_USB_XHCI_IMAN_IP    BIT(0)
#define TH1520_USB_XHCI_IMAN_IE    BIT(1)
#define TH1520_USB_XHCI_ERDP_EHB   BIT(3)
#define TH1520_USB_TRB_CYCLE       BIT(0)
#define TH1520_USB_TRB_TYPE_SHIFT  10
#define TH1520_USB_CR_NOOP         23
#define TH1520_USB_ER_CMD_COMPLETE 33
#define TH1520_USB_ER_PORT_CHANGE  34
#define TH1520_USB_CC_SUCCESS      1
#define TH1520_USB_XHCI_PORTSC_CCS BIT(0)
#define TH1520_USB_XHCI_PORTSC_CSC BIT(17)

#define TH1520_USB_ERST_ADDR       0x00300000
#define TH1520_USB_EVENT_RING_ADDR 0x00301000
#define TH1520_USB_COMMAND_RING_ADDR 0x00302000
#define TH1520_USB_EVENT_RING_TRBS 16

#define DWMAC_MAC_CONFIG           0x0000
#define DWMAC_FRAME_FILTER         0x0004
#define DWMAC_HASH_HIGH            0x0008
#define DWMAC_HASH_LOW             0x000c
#define DWMAC_FLOW_CTRL            0x0018
#define DWMAC_VLAN_TAG             0x001c
#define DWMAC_MAC0_ADDR_HI         0x0040
#define DWMAC_MAC0_ADDR_LO         0x0044
#define DWMAC_VLAN_HASH_TABLE      0x0588
#define DWMAC_MAC_ADDR_HI(index) \
    ((index) < 16 ? 0x40 + 8 * (index) : 0x800 + 8 * ((index) - 16))
#define DWMAC_MAC_ADDR_LO(index)   (DWMAC_MAC_ADDR_HI(index) + 4)
#define DWMAC_MII_ADDR             0x0010
#define DWMAC_MII_DATA             0x0014
#define DWMAC_VERSION              0x0020
#define DWMAC_DMA_BUS_MODE         0x1000
#define DWMAC_DMA_XMT_POLL_DEMAND  0x1004
#define DWMAC_DMA_RCV_POLL_DEMAND  0x1008
#define DWMAC_DMA_RX_BASE_ADDR     0x100c
#define DWMAC_DMA_TX_BASE_ADDR     0x1010
#define DWMAC_DMA_STATUS           0x1014
#define DWMAC_DMA_CONTROL          0x1018
#define DWMAC_DMA_INTR_ENA         0x101c
#define DWMAC_DMA_RX_WATCHDOG      0x1024
#define DWMAC_DMA_HOST_TX_DESC     0x1048
#define DWMAC_DMA_HOST_RX_DESC     0x104c
#define DWMAC_DMA_HW_FEATURE       0x1058

#define DWMAC_DMA_STATUS_NIS       BIT(16)
#define DWMAC_DMA_STATUS_AIS       BIT(15)
#define DWMAC_DMA_STATUS_RWT       BIT(9)
#define DWMAC_DMA_STATUS_RI        BIT(6)
#define DWMAC_DMA_STATUS_RU        BIT(7)
#define DWMAC_DMA_STATUS_TU        BIT(2)

#define DWMAC_MAC_CONFIG_IPC       BIT(10)
#define DWMAC_MAC_CONFIG_RX_EN     BIT(2)

#define DWMAC_DMA_CONTROL_TSF      BIT(21)
#define DWMAC_DMA_CONTROL_ST       BIT(13)

#define DWMAC_TX_DESC_OWN          BIT(31)
#define DWMAC_TX_DESC_IC           BIT(30)
#define DWMAC_TX_DESC_LS           BIT(29)
#define DWMAC_TX_DESC_FS           BIT(28)
#define DWMAC_TX_DESC_CIC_SHIFT    22
#define DWMAC_TX_DESC_CIC_MASK     (3U << DWMAC_TX_DESC_CIC_SHIFT)
#define DWMAC_TX_DESC_IHE          BIT(16)
#define DWMAC_TX_DESC_ES           BIT(15)
#define DWMAC_TX_DESC_IPE          BIT(12)
#define DWMAC_TX_DESC_COE_STATUS   \
    (DWMAC_TX_DESC_IHE | DWMAC_TX_DESC_ES | DWMAC_TX_DESC_IPE)

#define DWMAC_RX_DESC_ES           BIT(15)
#define DWMAC_RX_DESC_FS           BIT(9)
#define DWMAC_RX_DESC_LS           BIT(8)
#define DWMAC_RX_DESC_RER          BIT(15)
#define DWMAC_RX_DESC_FT           BIT(5)
#define DWMAC_RX_DESC_ESA          BIT(0)
#define DWMAC_RX_DESC4_IPHE        BIT(3)
#define DWMAC_RX_DESC4_IPPE        BIT(4)
#define DWMAC_RX_DESC4_BYPASS      BIT(5)
#define DWMAC_RX_DESC4_IPV4        BIT(6)
#define DWMAC_RX_DESC4_IPV6        BIT(7)
#define DWMAC_RX_DESC4_UDP         1
#define DWMAC_RX_DESC4_TCP         2
#define DWMAC_VLAN_TAG_ESVL        BIT(18)

#define TH1520_GMAC_VERSION_RESET  0x00001037
#define TH1520_GMAC_FEATURE_RESET  0x110d0107
#define TH1520_GMAC_PHY_ADDR       1
#define TH1520_GMAC_PHY_ID1        0x001c
#define TH1520_GMAC_PHY_ID2        0xc878
#define TH1520_GMAC_PHY_RESET_GPIO 21
#define TH1520_GMAC_PHY_IRQ_GPIO   22
#define TH1520_GMAC_PHY_RESET_DELAY_US 10000
#define TH1520_GMAC_PHY_RESET_POST_DELAY_US 50000
#define TH1520_GPIO_ACTIVE_LOW     1
#define TH1520_IRQ_TYPE_LEVEL_LOW  8

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
#define GMAC_TEST_DATA2_ADDR       0x00111000
#define GMAC_ENHANCED_DESC_STRIDE  32
#define GMAC_TEST_TIMEOUT_S        5
#define TH1520_GMAC_RIWT_CLOCK_HZ  500000000ULL

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
#define DWCMSHC_EMMC_TUNING_SIZE_4BIT 64
#define DWCMSHC_EMMC_TUNING_SIZE   128
#define DWCMSHC_ADMA_DESC_ADDR     (TH1520_SRAM_BASE + 0x10000)
#define DWCMSHC_ADMA_DATA_ADDR     (TH1520_SRAM_BASE + 0x20000)

#define EMMC_EXT_CSD_BUS_WIDTH         183
#define EMMC_EXT_CSD_STROBE_SUPPORT    184
#define EMMC_EXT_CSD_HS_TIMING         185
#define EMMC_EXT_CSD_REV               192
#define EMMC_EXT_CSD_CARD_TYPE         196
#define EMMC_EXT_CSD_GENERIC_CMD6_TIME 248

#define EMMC_BUS_WIDTH_1               0x00
#define EMMC_BUS_WIDTH_4               0x01
#define EMMC_BUS_WIDTH_8               0x02
#define EMMC_BUS_WIDTH_DDR_8           0x06
#define EMMC_BUS_WIDTH_DDR_8_STROBE    0x86
#define EMMC_HS_TIMING_LEGACY          0x00
#define EMMC_HS_TIMING_HS              0x01
#define EMMC_HS_TIMING_HS200           0x02
#define EMMC_HS_TIMING_HS400           0x03
#define EMMC_STATUS_SWITCH_ERROR       BIT(7)

#define DWCMSHC_UHS_MODE_HS200         3
#define DWCMSHC_UHS_MODE_HS400         5

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
#define DMAC_CTL_SRC_INCREMENT_SHIFT    4
#define DMAC_CTL_DST_INCREMENT_SHIFT    6
#define DMAC_CTL_SRC_WIDTH_SHIFT        8
#define DMAC_CTL_DST_WIDTH_SHIFT        11
#define DMAC_CTL_INCREMENT_NO_CHANGE    1
#define DMAC_CTL_WIDTH_8                0
#define DMAC_CTL_WIDTH_16               1
#define DMAC_CTL_WIDTH_32               2
#define DMAC_CTL_WIDTH_64               3
#define DMAC_CTL_WIDTH_128              4
#define DMAC_CTL_WIDTH_256              5
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

/*
 * Raw RV64 mask-ROM payload.  Hart 0 resets UART0, writes 'R', and parks;
 * the other harts park without touching the UART.  Keep this independent of
 * a guest toolchain so the qtest can run in dependency-minimal builds.
 */
static const uint8_t mask_rom_uart_guest[] = {
    0x73, 0x25, 0x40, 0xf1, /* csrr a0, mhartid */
    0x63, 0x14, 0x05, 0x02, /* bnez a0, park */
    0xb7, 0xa2, 0xff, 0x03, /* li t0, 0xffe7014000 (part 1) */
    0x9b, 0x82, 0x52, 0xc0, /* li t0, 0xffe7014000 (part 2) */
    0x93, 0x92, 0xe2, 0x00, /* li t0, 0xffe7014000 (part 3) */
    0x13, 0x03, 0x10, 0x00, /* li t1, 1 */
    0x23, 0xa4, 0x62, 0x08, /* sw t1, 0x88(t0) */
    0x13, 0x03, 0x30, 0x00, /* li t1, 3 */
    0x23, 0xa6, 0x62, 0x00, /* sw t1, 0x0c(t0) */
    0x13, 0x03, 0x20, 0x05, /* li t1, 'R' */
    0x23, 0xa0, 0x62, 0x00, /* sw t1, 0(t0) */
    0x73, 0x00, 0x50, 0x10, /* park: wfi */
    0x6f, 0xf0, 0xdf, 0xff, /* j park */
};

/*
 * Raw RV64 mask-ROM payload for the Linux PLL timing contract.  Hart 0
 * releases the TEE PLL, then samples on the first 3 MHz timer tick after the
 * modeled 21.25 us lock deadline (64 ticks, or about 21.333 us).  It reports
 * 'P' when PLL_STS[10] is visible or 'F' otherwise.  The public manual quotes
 * about 21.25 us for the default PLL configurations and Linux allows 44 us.
 * The narrow post-deadline window exercises status-read observability before
 * the I/O thread is likely to dispatch the timer callback.  Keeping this
 * prebuilt makes the regression available in dependency-minimal builds
 * without a guest compiler.
 */
static const uint8_t pll_poll_mask_rom_guest[] = {
    0x73, 0x25, 0x40, 0xf1, /* csrr a0, mhartid */
    0x63, 0x1e, 0x05, 0x04, /* bnez a0, park */
    0xb7, 0xf2, 0xff, 0x00, /* li t0, 0xffef010000 (part 1) */
    0x9b, 0x82, 0x12, 0xf0, /* li t0, 0xffef010000 (part 2) */
    0x93, 0x92, 0x02, 0x01, /* li t0, 0xffef010000 (part 3) */
    0x37, 0x03, 0x00, 0x43, /* li t1, 0x43000000 */
    0x23, 0xa2, 0x62, 0x06, /* sw t1, 0x64(t0) */
    0x73, 0x23, 0x10, 0xc0, /* rdtime t1 */
    0x13, 0x03, 0x03, 0x04, /* addi t1, t1, 64 */
    0xf3, 0x23, 0x10, 0xc0, /* wait: rdtime t2 */
    0xe3, 0xee, 0x63, 0xfe, /* bltu t2, t1, wait */
    0x03, 0xae, 0x02, 0x08, /* lw t3, 0x80(t0) */
    0x13, 0x7e, 0x0e, 0x40, /* andi t3, t3, BIT(10) */
    0x93, 0x0e, 0x60, 0x04, /* li t4, 'F' */
    0x63, 0x04, 0x0e, 0x00, /* beqz t3, report */
    0x93, 0x0e, 0x00, 0x05, /* li t4, 'P' */
    0xb7, 0xa2, 0xff, 0x03, /* li t0, 0xffe7014000 (part 1) */
    0x9b, 0x82, 0x52, 0xc0, /* li t0, 0xffe7014000 (part 2) */
    0x93, 0x92, 0xe2, 0x00, /* li t0, 0xffe7014000 (part 3) */
    0x13, 0x03, 0x10, 0x00, /* li t1, 1 */
    0x23, 0xa4, 0x62, 0x08, /* sw t1, 0x88(t0) */
    0x13, 0x03, 0x30, 0x00, /* li t1, 3 */
    0x23, 0xa6, 0x62, 0x00, /* sw t1, 0x0c(t0) */
    0x23, 0xa0, 0xd2, 0x01, /* report: sw t4, 0(t0) */
    0x73, 0x00, 0x50, 0x10, /* park: wfi */
    0x6f, 0xf0, 0xdf, 0xff, /* j park */
};

static uint8_t read_serial_byte(int fd);
static void wait_for_migration_complete(QTestState *qts);

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

typedef struct TH1520WDT {
    const char *name;
    uint64_t base;
    uint32_t irq;
    uint32_t clock_id;
    uint32_t reset_id;
    uint32_t reset_offset;
} TH1520WDT;

typedef struct TH1520SPIController {
    uint64_t base;
    uint32_t irq;
    uint32_t clock_id;
} TH1520SPIController;

typedef struct TH1520IOPMPController {
    const char *name;
    uint64_t base;
} TH1520IOPMPController;

typedef struct TH1520USBReg {
    uint32_t offset;
    uint32_t reset;
    uint32_t write_mask;
} TH1520USBReg;

typedef struct TH1520ResetTestOutput {
    uint32_t offset;
    uint32_t deasserted;
} TH1520ResetTestOutput;

typedef struct TH1520ClockGateTestOutput {
    uint32_t offset;
    uint32_t mask;
} TH1520ClockGateTestOutput;

static const TH1520ClockGateTestOutput
th1520_ap_clock_gate_test_outputs[TH1520_AP_CLOCK_GATE_COUNT] = {
    [TH1520_AP_CLOCK_GATE_EMMC_SDIO] = { 0x204, BIT(30) },
    [TH1520_AP_CLOCK_GATE_GMAC1] = { 0x204, BIT(26) },
    [TH1520_AP_CLOCK_GATE_PADCTRL1] = { 0x204, BIT(24) },
    [TH1520_AP_CLOCK_GATE_PADCTRL0] = { 0x204, BIT(22) },
    [TH1520_AP_CLOCK_GATE_GMAC_AXI] = { 0x204, BIT(21) },
    [TH1520_AP_CLOCK_GATE_GPIO3] = { 0x204, BIT(20) },
    [TH1520_AP_CLOCK_GATE_GMAC0] = { 0x204, BIT(19) },
    [TH1520_AP_CLOCK_GATE_PWM] = { 0x204, BIT(18) },
    [TH1520_AP_CLOCK_GATE_SPI] = { 0x204, BIT(15) },
    [TH1520_AP_CLOCK_GATE_UART0] = { 0x204, BIT(14) },
    [TH1520_AP_CLOCK_GATE_UART1] = { 0x204, BIT(13) },
    [TH1520_AP_CLOCK_GATE_UART2] = { 0x204, BIT(12) },
    [TH1520_AP_CLOCK_GATE_UART3] = { 0x204, BIT(11) },
    [TH1520_AP_CLOCK_GATE_UART4] = { 0x204, BIT(10) },
    [TH1520_AP_CLOCK_GATE_UART5] = { 0x204, BIT(9) },
    [TH1520_AP_CLOCK_GATE_GPIO0] = { 0x204, BIT(8) },
    [TH1520_AP_CLOCK_GATE_GPIO1] = { 0x204, BIT(7) },
    [TH1520_AP_CLOCK_GATE_GPIO2] = { 0x204, BIT(6) },
    [TH1520_AP_CLOCK_GATE_I2C0] = { 0x204, BIT(5) },
    [TH1520_AP_CLOCK_GATE_I2C1] = { 0x204, BIT(4) },
    [TH1520_AP_CLOCK_GATE_I2C2] = { 0x204, BIT(3) },
    [TH1520_AP_CLOCK_GATE_I2C3] = { 0x204, BIT(2) },
    [TH1520_AP_CLOCK_GATE_I2C4] = { 0x204, BIT(1) },
    [TH1520_AP_CLOCK_GATE_I2C5] = { 0x204, BIT(0) },
    [TH1520_AP_CLOCK_GATE_DMA] = { 0x208, BIT(8) },
    [TH1520_AP_CLOCK_GATE_MBOX0] = { 0x208, BIT(7) },
    [TH1520_AP_CLOCK_GATE_MBOX1] = { 0x208, BIT(6) },
    [TH1520_AP_CLOCK_GATE_MBOX2] = { 0x208, BIT(5) },
    [TH1520_AP_CLOCK_GATE_MBOX3] = { 0x208, BIT(4) },
    [TH1520_AP_CLOCK_GATE_WDT0] = { 0x208, BIT(3) },
    [TH1520_AP_CLOCK_GATE_WDT1] = { 0x208, BIT(2) },
    [TH1520_AP_CLOCK_GATE_TIMER0] = { 0x208, BIT(1) },
    [TH1520_AP_CLOCK_GATE_TIMER1] = { 0x208, BIT(0) },
};

static const TH1520ClockGateTestOutput
th1520_miscsys_clock_test_outputs[TH1520_MISCSYS_CLOCK_COUNT] = {
    [TH1520_MISCSYS_CLOCK_BUS] = { TH1520_MISCSYS_BUS_CLK, BIT(0) },
    [TH1520_MISCSYS_CLOCK_USB0] = { TH1520_MISCSYS_USB_CLK, BIT(0) },
    [TH1520_MISCSYS_CLOCK_USB1] = { TH1520_MISCSYS_USB_CLK, BIT(1) },
    [TH1520_MISCSYS_CLOCK_USB2] = { TH1520_MISCSYS_USB_CLK, BIT(2) },
    [TH1520_MISCSYS_CLOCK_USB3] = { TH1520_MISCSYS_USB_CLK, BIT(3) },
    [TH1520_MISCSYS_CLOCK_EMMC] = { TH1520_MISCSYS_EMMC_CLK, BIT(0) },
    [TH1520_MISCSYS_CLOCK_SDIO0] = { TH1520_MISCSYS_SDIO0_CLK, BIT(0) },
    [TH1520_MISCSYS_CLOCK_SDIO1] = { TH1520_MISCSYS_SDIO1_CLK, BIT(0) },
};

static const TH1520ResetTestOutput
th1520_ap_reset_test_outputs[TH1520_AP_RESET_OUTPUT_COUNT] = {
    [TH1520_AP_RESET_PWM] = { 0x0c0, 0x3 },
    [TH1520_AP_RESET_TIMER0_3] = { 0x03c, 0x3 },
    [TH1520_AP_RESET_TIMER4_7] = { 0x040, 0x3 },
    [TH1520_AP_RESET_WDT0] = { 0x034, 0x1 },
    [TH1520_AP_RESET_WDT1] = { 0x038, 0x1 },
    [TH1520_AP_RESET_UART0] = { 0x070, 0x3 },
    [TH1520_AP_RESET_UART1] = { 0x074, 0x3 },
    [TH1520_AP_RESET_UART2] = { 0x078, 0x3 },
    [TH1520_AP_RESET_UART3] = { 0x07c, 0x3 },
    [TH1520_AP_RESET_UART4] = { 0x080, 0x3 },
    [TH1520_AP_RESET_UART5] = { 0x084, 0x3 },
    [TH1520_AP_RESET_I2C0] = { 0x098, 0x3 },
    [TH1520_AP_RESET_I2C1] = { 0x09c, 0x3 },
    [TH1520_AP_RESET_I2C2] = { 0x0a0, 0x3 },
    [TH1520_AP_RESET_I2C3] = { 0x0a4, 0x3 },
    [TH1520_AP_RESET_I2C4] = { 0x0a8, 0x3 },
    [TH1520_AP_RESET_I2C5] = { 0x0ac, 0x3 },
    [TH1520_AP_RESET_SPI0] = { 0x094, 0x3 },
    [TH1520_AP_RESET_GPIO0] = { 0x0b0, 0x3 },
    [TH1520_AP_RESET_GPIO1] = { 0x0b4, 0x3 },
    [TH1520_AP_RESET_GPIO2] = { 0x0b8, 0x3 },
    [TH1520_AP_RESET_GPIO3] = { 0x1a8, 0x3 },
    [TH1520_AP_RESET_PADCTRL0] = { 0x0c4, 0x1 },
    [TH1520_AP_RESET_PADCTRL1] = { 0x20c, 0x1 },
    [TH1520_AP_RESET_DMAC0] = { 0x14c, 0x3 },
    [TH1520_AP_RESET_GMAC0] = { 0x068, 0xf },
    [TH1520_AP_RESET_GMAC1] = { 0x204, 0xf },
    [TH1520_AP_RESET_GMAC_SHARED] = { 0x208, 0x3 },
};

static const TH1520ResetTestOutput
th1520_storage_reset_test_outputs[TH1520_MISCSYS_STORAGE_RESET_COUNT] = {
    [TH1520_MISCSYS_STORAGE_EMMC] = { 0x000, 0x3 },
    [TH1520_MISCSYS_STORAGE_SDIO0] = { 0x00c, 0x1 },
    [TH1520_MISCSYS_STORAGE_SDIO1] = { 0x010, 0x1 },
};

static const TH1520USBReg th1520_miscsys_regs[] = {
    { 0x000, 0x00000003, 0x00000003 },
    { 0x008, 0x00000003, 0x00000003 },
    { 0x00c, 0x00000001, 0x00000001 },
    { 0x010, 0x00000001, 0x00000001 },
    { 0x014, 0x00000001, 0x00000007 },
    { 0x100, 0x00000001, 0x00000001 },
    { 0x104, 0x0000000f, 0x0000000f },
    { 0x108, 0x00000001, 0x00000001 },
    { 0x10c, 0x00000001, 0x00000001 },
    { 0x110, 0x00000001, 0x00000001 },
};

static const TH1520USBReg th1520_usb_drd_regs[] = {
    { 0x00, 0x00000000, 0x00000000 },
    { 0x04, 0x00000000, 0x00000000 },
    { 0x08, 0x00000000, 0x00000000 },
    { 0x0c, 0x00000000, 0x0000ffff },
    { 0x10, 0x00000040, 0x00000000 },
    { 0x14, 0x0003c400, 0x00000000 },
    { 0x18, 0x00000000, 0x00000000 },
    { 0x1c, 0x00000020, 0x0000003f },
    { 0x20, 0x00002a00, 0x1ff7ff7f },
    { 0x24, 0x00095182, 0x1f1f77f3 },
    { 0x28, 0x10303344, 0x3331f777 },
    { 0x2c, 0x01c1c0f0, 0x03f3f3ff },
    { 0x30, 0x0000047f, 0x00000f7f },
    { 0x34, 0x00000000, 0x00000001 },
    { 0x38, 0x00000000, 0x0000000f },
    { 0x3c, 0x00000000, 0x000001ff },
    { 0x40, 0x00000000, 0x00000000 },
    { 0x44, 0x00001101, 0x0333ff7f },
    { 0x48, 0x00000018, 0x0000003f },
    { 0x4c, 0x00000000, 0x00000000 },
    { 0x50, 0x00000000, 0xffffffff },
    { 0x54, 0x00000000, 0xffffffff },
    { 0x58, 0xffffffff, 0xffffffff },
    { 0x5c, 0xffffffff, 0xffffffff },
};

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

static const TH1520IOPMPController th1520_iopmp_controllers[] = {
    { "emmc",      0xfffc028000ULL },
    { "sdio0",     0xfffc029000ULL },
    { "sdio1",     0xfffc02a000ULL },
    { "usb0",      0xfffc02e000ULL },
    { "ao",        0xffffc21000ULL },
    { "aud",       0xffffc22000ULL },
    { "chip-dbg",  0xffffc37000ULL },
    { "eip120i",   0xffff220000ULL },
    { "eip120ii",  0xffff230000ULL },
    { "eip120iii", 0xffff240000ULL },
    { "isp0",      0xfff4080000ULL },
    { "isp1",      0xfff4081000ULL },
    { "dw200",     0xfff4082000ULL },
    { "vipre",     0xfff4083000ULL },
    { "venc",      0xfffcc60000ULL },
    { "vdec",      0xfffcc61000ULL },
    { "g2d",       0xfffcc62000ULL },
    { "fce",       0xfffcc63000ULL },
    { "npu",       0xffff01c000ULL },
    { "dpu0",      0xffff520000ULL },
    { "dpu1",      0xffff521000ULL },
    { "gpu",       0xffff522000ULL },
    { "gmac1",     0xfffc001000ULL },
    { "gmac2",     0xfffc002000ULL },
    { "dmac",      0xffffc20000ULL },
    { "tee-dmac",  0xffff250000ULL },
    { "dsp0",      0xffff058000ULL },
    { "dsp1",      0xffff059000ULL },
    { "audio0",    0xffcb02e000ULL },
    { "audio1",    0xffcb02f000ULL },
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

static const TH1520WDT th1520_wdts[] = {
    { "wdt0", TH1520_WDT0_BASE, TH1520_WDT0_IRQ, TH1520_CLK_WDT0,
      3, 0x034 },
    { "wdt1", TH1520_WDT1_BASE, TH1520_WDT1_IRQ, TH1520_CLK_WDT1,
      4, 0x038 },
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
    static const char *const compatibles[][2] = {
        {
            "thead,th1520-pinctrl",
            "xuantie,th1520-group1-pinctrl",
        }, {
            "thead,th1520-pinctrl",
            "xuantie,th1520-group2-pinctrl",
        }, {
            "thead,th1520-pinctrl",
            "xuantie,th1520-group3-pinctrl",
        },
    };
    g_autofree char *path =
        g_strdup_printf("/soc/pinctrl@%" PRIx64, controller->base);
    const fdt32_t *cells;
    uint32_t phandle;
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    g_assert_cmpuint(controller->group, >=, 1);
    g_assert_cmpuint(controller->group, <=, ARRAY_SIZE(compatibles));
    assert_fdt_stringlist(fdt, node, "compatible",
                          compatibles[controller->group - 1],
                          ARRAY_SIZE(compatibles[0]));
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
                            uint32_t axi_phandle,
                            uint32_t gpio3_phandle)
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
        g_assert_cmphex(fdt_prop_u32(fdt, phy, "interrupt-parent"), ==,
                        gpio3_phandle);
        cells = fdt_getprop(fdt, phy, "interrupts", &len);
        g_assert_nonnull(cells);
        g_assert_cmpint(len, ==, 2 * sizeof(*cells));
        g_assert_cmphex(fdt32_to_cpu(cells[0]), ==,
                        TH1520_GMAC_PHY_IRQ_GPIO);
        g_assert_cmphex(fdt32_to_cpu(cells[1]), ==,
                        TH1520_IRQ_TYPE_LEVEL_LOW);
        cells = fdt_getprop(fdt, phy, "reset-gpios", &len);
        g_assert_nonnull(cells);
        g_assert_cmpint(len, ==, 3 * sizeof(*cells));
        g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, gpio3_phandle);
        g_assert_cmphex(fdt32_to_cpu(cells[1]), ==,
                        TH1520_GMAC_PHY_RESET_GPIO);
        g_assert_cmphex(fdt32_to_cpu(cells[2]), ==,
                        TH1520_GPIO_ACTIVE_LOW);
        g_assert_cmphex(fdt_prop_u32(fdt, phy, "reset-delay-us"), ==,
                        TH1520_GMAC_PHY_RESET_DELAY_US);
        g_assert_cmphex(fdt_prop_u32(fdt, phy, "reset-post-delay-us"), ==,
                        TH1520_GMAC_PHY_RESET_POST_DELAY_US);
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
                               uint32_t mshc_clock_phandle)
{
    static const char *const compat[] = {
        "xuantie,th1520-dwcmshc", "thead,th1520-dwcmshc",
    };
    g_autofree char *path =
        g_strdup_printf("/soc/mmc@%" PRIx64, controller->base);
    const fdt32_t *cells;
    const char *text;
    char alias[8];
    int node;
    int len;

    node = fdt_path_offset(fdt, path);
    g_assert_cmpint(node, >=, 0);

    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    assert_fdt_stringlist(fdt, node, "compatible", compat,
                          ARRAY_SIZE(compat));
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
    g_assert_cmpint(len, ==, sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, mshc_clock_phandle);
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

    snprintf(alias, sizeof(alias), "mmc%u",
             (unsigned)(controller - dwcmshc_controllers));
    g_assert_cmpstr(fdt_get_alias(fdt, alias), ==, path);
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

static void assert_wdt_fdt(const void *fdt, const TH1520WDT *wdt,
                           uint32_t clock_phandle,
                           uint32_t reset_phandle)
{
    g_autofree char *path =
        g_strdup_printf("/soc/watchdog@%" PRIx64, wdt->base);
    const fdt32_t *cells;
    const char *text;
    int node = fdt_path_offset(fdt, path);
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "snps,dw-wdt");
    assert_fdt_mmio(fdt, node, wdt->base, 0x1000);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, wdt->irq);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);

    cells = fdt_getprop(fdt, node, "clocks", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, wdt->clock_id);
    text = fdt_getprop(fdt, node, "clock-names", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "tclk");

    cells = fdt_getprop(fdt, node, "resets", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, reset_phandle);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, wdt->reset_id);
    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "disabled");
}

static void assert_usb_fdt(const void *fdt)
{
    static const char *const misc_compatibles[] = {
        "thead,light-misc-sysreg", "syscon"
    };
    static const char *const drd_compatibles[] = {
        "thead,light-usb3-drd", "syscon"
    };
    const fdt32_t *cells;
    const char *text;
    int node;
    int len;

    node = fdt_path_offset(fdt, "/soc/syscon@ffec02c000");
    g_assert_cmpint(node, >=, 0);
    assert_fdt_stringlist(fdt, node, "compatible", misc_compatibles,
                          ARRAY_SIZE(misc_compatibles));
    assert_fdt_mmio(fdt, node, TH1520_MISCSYS_BASE, 0x1000);

    node = fdt_path_offset(fdt, "/soc/syscon@ffec03f000");
    g_assert_cmpint(node, >=, 0);
    assert_fdt_stringlist(fdt, node, "compatible", drd_compatibles,
                          ARRAY_SIZE(drd_compatibles));
    assert_fdt_mmio(fdt, node, TH1520_USB_DRD_BASE, 0x1000);

    node = fdt_path_offset(fdt, "/soc/usb@ffe7040000");
    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "snps,dwc3");
    assert_fdt_mmio(fdt, node, TH1520_USB_CORE_BASE, 0x10000);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, TH1520_USB_IRQ);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);

    text = fdt_getprop(fdt, node, "interrupt-names", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "dwc_usb3");
    text = fdt_getprop(fdt, node, "maximum-speed", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "super-speed");
    text = fdt_getprop(fdt, node, "dr_mode", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "host");
    assert_fdt_bool(fdt, node, "snps,usb3_lpm_capable", true);
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

static void assert_pvt_fdt(const void *fdt, uint32_t clock_phandle)
{
    static const char *const reg_names[] = {
        "common", "ts", "pd", "vm"
    };
    static const uint64_t bases[] = {
        TH1520_PVT_COMMON_BASE,
        TH1520_PVT_TS_BASE,
        TH1520_PVT_PD_BASE,
        TH1520_PVT_VM_BASE,
    };
    static const uint32_t sizes[] = { 0x80, 0x100, 0x680, 0x600 };
    const fdt32_t *cells;
    const char *text;
    int node = fdt_path_offset(fdt, "/soc/pvt@fffff4e000");
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "moortec,mr75203");
    assert_fdt_stringlist(fdt, node, "reg-names", reg_names,
                          ARRAY_SIZE(reg_names));

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
    g_assert_cmpint(len, ==, sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, clock_phandle);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "#thermal-sensor-cells"), ==, 1);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "moortec,ts-coeff-g"), ==,
                    MR75203_TS_COEFF_G);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "moortec,ts-coeff-h"), ==,
                    MR75203_TS_COEFF_H);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "moortec,ts-coeff-j"), ==,
                    (uint32_t)MR75203_TS_COEFF_J);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "moortec,ts-coeff-cal5"), ==,
                    MR75203_TS_COEFF_CAL5);
    assert_fdt_bool(fdt, node, "interrupts", false);

    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_null(text);
    g_assert_cmpint(len, ==, -FDT_ERR_NOTFOUND);
}

static void assert_rtc_fdt(const void *fdt, uint32_t clock_phandle)
{
    const fdt32_t *cells;
    const char *text;
    int node = fdt_path_offset(fdt, "/soc/rtc@fffff40000");
    int len;

    g_assert_cmpint(node, >=, 0);
    text = fdt_getprop(fdt, node, "compatible", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "apm,xgene-rtc");
    assert_fdt_mmio(fdt, node, TH1520_RTC_BASE, 0x1000);

    cells = fdt_getprop(fdt, node, "interrupts", &len);
    g_assert_nonnull(cells);
    g_assert_cmpint(len, ==, 2 * sizeof(*cells));
    g_assert_cmphex(fdt32_to_cpu(cells[0]), ==, TH1520_RTC_IRQ);
    g_assert_cmphex(fdt32_to_cpu(cells[1]), ==, 4);
    g_assert_cmphex(fdt_prop_u32(fdt, node, "clocks"), ==,
                    clock_phandle);
    text = fdt_getprop(fdt, node, "clock-names", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "rtc");
    assert_fdt_bool(fdt, node, "wakeup-source", true);
    text = fdt_getprop(fdt, node, "status", &len);
    g_assert_nonnull(text);
    g_assert_cmpstr(text, ==, "disabled");
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
    uint32_t rtc_clock_phandle;
    uint32_t mshc_clock_phandle;
    uint32_t ap_clock_phandle;
    uint32_t ap_reset_phandle;
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

    clock_offset = fdt_path_offset(fdt, "/clock-32768");
    g_assert_cmpint(clock_offset, >=, 0);
    compatible = fdt_getprop(fdt, clock_offset, "compatible", &len);
    g_assert_nonnull(compatible);
    g_assert_cmpstr(compatible, ==, "fixed-clock");
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "#clock-cells"), ==,
                    0);
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "clock-frequency"), ==,
                    XGENE_RTC_PRESCALER);
    compatible = fdt_getprop(fdt, clock_offset, "clock-output-names", &len);
    g_assert_nonnull(compatible);
    g_assert_cmpstr(compatible, ==, "rtc_clk");
    rtc_clock_phandle = fdt_get_phandle(fdt, clock_offset);
    g_assert_cmphex(rtc_clock_phandle, !=, 0);

    clock_offset = fdt_path_offset(fdt, "/mshc-clock");
    g_assert_cmpint(clock_offset, >=, 0);
    compatible = fdt_getprop(fdt, clock_offset, "compatible", &len);
    g_assert_nonnull(compatible);
    g_assert_cmpstr(compatible, ==, "fixed-clock");
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "#clock-cells"), ==,
                    0);
    g_assert_cmphex(fdt_prop_u32(fdt, clock_offset, "clock-frequency"), ==,
                    198000000);
    compatible = fdt_getprop(fdt, clock_offset, "clock-output-names", &len);
    g_assert_nonnull(compatible);
    g_assert_cmpstr(compatible, ==, "mshc-input");
    mshc_clock_phandle = fdt_get_phandle(fdt, clock_offset);
    g_assert_cmphex(mshc_clock_phandle, !=, 0);

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
    ap_reset_phandle = fdt_get_phandle(fdt, clock_offset);
    g_assert_cmphex(ap_reset_phandle, !=, 0);

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
                           mshc_clock_phandle);
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
    assert_pvt_fdt(fdt, aonsys_clock_phandle);
    assert_rtc_fdt(fdt, rtc_clock_phandle);

    for (size_t i = 0; i < ARRAY_SIZE(th1520_timers); i++) {
        assert_timer_fdt(fdt, &th1520_timers[i], ap_clock_phandle);
    }
    for (size_t i = 0; i < ARRAY_SIZE(th1520_wdts); i++) {
        assert_wdt_fdt(fdt, &th1520_wdts[i], ap_clock_phandle,
                       ap_reset_phandle);
    }
    assert_usb_fdt(fdt);

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
                        ap_clock_phandle, stmmac_axi_phandle,
                        gpio_phandles[3]);
    }

    qtest_quit(qts);
}

static char *create_mask_rom(const uint8_t *contents, size_t size)
{
    g_autoptr(GError) error = NULL;
    char *path = NULL;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-mask-rom-XXXXXX", &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, contents, size), ==, size);
    g_assert_cmpint(close(fd), ==, 0);
    return path;
}

static char *create_mask_rom_guest(void)
{
    return create_mask_rom(mask_rom_uart_guest, sizeof(mask_rom_uart_guest));
}

static void test_mask_rom_contract(void)
{
    g_autofree char *path = create_mask_rom_guest();
    g_autofree char *rom_dir = g_path_get_dirname(path);
    g_autofree char *rom_basename = g_path_get_basename(path);
    g_autofree char *destination_path = NULL;
    g_autofree char *migration_path = NULL;
    g_autofree char *uri = NULL;
    uint8_t alternate[sizeof(mask_rom_uart_guest)];
    uint8_t contents[sizeof(mask_rom_uart_guest)];
    QTestState *src;
    QTestState *dst;
    QTestState *search_path;
    uint32_t word;
    int fd;

    for (size_t i = 0; i < sizeof(alternate); i++) {
        alternate[i] = ~mask_rom_uart_guest[i];
    }
    destination_path = create_mask_rom(alternate, sizeof(alternate));

    search_path = qtest_initf(
        "-machine beaglev-ahead,boot-mode=mask-rom -L %s -bios %s",
        rom_dir, rom_basename);
    qtest_memread(search_path, TH1520_BROM_BASE, contents, sizeof(contents));
    g_assert_cmpmem(contents, sizeof(contents), mask_rom_uart_guest,
                    sizeof(mask_rom_uart_guest));
    qtest_quit(search_path);

    src = qtest_initf(
        "-machine beaglev-ahead,boot-mode=mask-rom -bios %s", path);
    fd = g_file_open_tmp("beaglev-ahead-mask-rom-migration-XXXXXX",
                         &migration_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(close(fd), ==, 0);
    uri = g_strdup_printf("file:%s", migration_path);
    dst = qtest_initf(
        "-machine beaglev-ahead,boot-mode=mask-rom -bios %s "
        "-incoming defer", destination_path);

    qtest_memread(src, TH1520_BROM_BASE, contents, sizeof(contents));
    g_assert_cmpmem(contents, sizeof(contents), mask_rom_uart_guest,
                    sizeof(mask_rom_uart_guest));

    /* The image occupies the SoC's ROM aperture, not writable guest RAM. */
    word = qtest_readl(src, TH1520_BROM_BASE + 8);
    qtest_writel(src, TH1520_BROM_BASE + 8, ~word);
    g_assert_cmphex(qtest_readl(src, TH1520_BROM_BASE + 8), ==, word);

    qtest_system_reset(src);
    qtest_memread(src, TH1520_BROM_BASE, contents, sizeof(contents));
    g_assert_cmpmem(contents, sizeof(contents), mask_rom_uart_guest,
                    sizeof(mask_rom_uart_guest));

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    /* Migration must replace the deliberately different destination ROM. */
    qtest_memread(dst, TH1520_BROM_BASE, contents, sizeof(contents));
    g_assert_cmpmem(contents, sizeof(contents), mask_rom_uart_guest,
                    sizeof(mask_rom_uart_guest));
    qtest_system_reset(dst);
    qtest_memread(dst, TH1520_BROM_BASE, contents, sizeof(contents));
    g_assert_cmpmem(contents, sizeof(contents), mask_rom_uart_guest,
                    sizeof(mask_rom_uart_guest));

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(migration_path), ==, 0);
    g_assert_cmpint(g_unlink(destination_path), ==, 0);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_mask_rom_execution_reset(void)
{
    g_autofree char *path = create_mask_rom_guest();
    g_autofree char *args = NULL;
    QTestState *qts;
    int serial_fd;

    if (!qtest_has_accel("tcg")) {
        g_test_skip("TCG is required to execute the mask-ROM payload");
        g_assert_cmpint(g_unlink(path), ==, 0);
        return;
    }

    args = g_strdup_printf(
        "-machine beaglev-ahead,boot-mode=mask-rom -bios %s -accel tcg",
        path);
    qts = qtest_init_with_serial(args, &serial_fd);

    g_assert_cmphex(read_serial_byte(serial_fd), ==, 'R');
    qtest_system_reset(qts);
    g_assert_cmphex(read_serial_byte(serial_fd), ==, 'R');

    close(serial_fd);
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_ap_clock_poll_execution(void)
{
    g_autofree char *path = create_mask_rom(pll_poll_mask_rom_guest,
                                             sizeof(pll_poll_mask_rom_guest));
    g_autofree char *args = NULL;
    QTestState *qts;
    int serial_fd;

    if (!qtest_has_accel("tcg")) {
        g_test_skip("TCG is required to execute the PLL polling payload");
        g_assert_cmpint(g_unlink(path), ==, 0);
        return;
    }

    /* RR TCG gives the payload a narrow post-deadline observation window. */
    args = g_strdup_printf(
        "-machine beaglev-ahead,boot-mode=mask-rom -bios %s "
        "-accel tcg,thread=single",
        path);
    qts = qtest_init_with_serial(args, &serial_fd);

    g_assert_cmphex(read_serial_byte(serial_fd), ==, 'P');
    qtest_system_reset(qts);
    g_assert_cmphex(read_serial_byte(serial_fd), ==, 'P');

    close(serial_fd);
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_qemu_start_fails(const char *const arguments[],
                                    const char *message)
{
    g_autoptr(GError) error = NULL;
    g_autofree char *dumpdtb_path = NULL;
    g_autofree char *stderr_text = NULL;
    g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func(g_free);
    int status = 0;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-invalid-XXXXXX.dtb",
                         &dumpdtb_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(close(fd), ==, 0);
    g_assert_cmpint(g_unlink(dumpdtb_path), ==, 0);

    g_ptr_array_add(argv, g_strdup(qtest_qemu_binary(NULL)));
    for (size_t i = 0; arguments[i]; i++) {
        g_ptr_array_add(argv, g_strdup(arguments[i]));
        if (!strcmp(arguments[i], "-machine")) {
            g_assert_nonnull(arguments[++i]);
            g_ptr_array_add(argv,
                g_strdup_printf("%s,dumpdtb=%s", arguments[i],
                                dumpdtb_path));
        }
    }
    g_ptr_array_add(argv, NULL);

    g_assert_true(g_spawn_sync(NULL, (char **)argv->pdata, NULL,
                              G_SPAWN_SEARCH_PATH |
                              G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL,
                              NULL, &stderr_text, &status, &error));
    g_assert_no_error(error);
    g_assert_cmpint(status, !=, 0);
    g_assert_nonnull(strstr(stderr_text, message));
}

static void test_mask_rom_errors(void)
{
    g_autofree char *path = create_mask_rom_guest();
    g_autofree char *empty_path = NULL;
    g_autofree char *oversized_path = NULL;
    int fd;
    const char *const implicit[] = {
        "-machine", "beaglev-ahead,boot-mode=mask-rom",
        "-display", "none", NULL,
    };
    const char *const missing[] = {
        "-machine", "beaglev-ahead,boot-mode=mask-rom",
        "-bios", "none", "-display", "none", NULL,
    };
    const char *const default_image[] = {
        "-machine", "beaglev-ahead,boot-mode=mask-rom",
        "-bios", "default", "-display", "none", NULL,
    };
    const char *const invalid[] = {
        "-machine", "beaglev-ahead,boot-mode=invalid",
        "-bios", "none", "-display", "none", NULL,
    };

    fd = g_file_open_tmp("beaglev-ahead-empty-mask-rom-XXXXXX",
                         &empty_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(close(fd), ==, 0);
    fd = g_file_open_tmp("beaglev-ahead-oversized-mask-rom-XXXXXX",
                         &oversized_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, MiB + 1), ==, 0);
    g_assert_cmpint(close(fd), ==, 0);

    {
        const char *const empty[] = {
            "-machine", "beaglev-ahead,boot-mode=mask-rom",
            "-bios", empty_path, "-display", "none", NULL,
        };
        const char *const oversized[] = {
            "-machine", "beaglev-ahead,boot-mode=mask-rom",
            "-bios", oversized_path, "-display", "none", NULL,
        };
        const char *const conflicting_kernel[] = {
            "-machine", "beaglev-ahead,boot-mode=mask-rom",
            "-bios", path, "-kernel", path, "-display", "none", NULL,
        };
        const char *const conflicting_initrd[] = {
            "-machine", "beaglev-ahead,boot-mode=mask-rom",
            "-bios", path, "-initrd", path, "-display", "none", NULL,
        };
        const char *const conflicting_append[] = {
            "-machine", "beaglev-ahead,boot-mode=mask-rom",
            "-bios", path, "-append", "console=ttyS0",
            "-display", "none", NULL,
        };
        const char *const conflicting_dtb[] = {
            "-machine", "beaglev-ahead,boot-mode=mask-rom",
            "-bios", path, "-dtb", "/does/not/exist",
            "-display", "none", NULL,
        };
        const char *const conflict_message =
            "mask-rom boot does not accept -kernel, -initrd, -append, or -dtb";

        assert_qemu_start_fails(implicit,
            "mask-rom boot requires -bios <raw-image>");
        assert_qemu_start_fails(missing,
            "mask-rom boot requires -bios <raw-image>");
        assert_qemu_start_fails(default_image,
            "mask-rom boot requires -bios <raw-image>");
        assert_qemu_start_fails(empty, "empty file:");
        assert_qemu_start_fails(oversized,
                                "exceeds maximum image size (1 MiB)");
        assert_qemu_start_fails(conflicting_kernel, conflict_message);
        assert_qemu_start_fails(conflicting_initrd,
                                "-initrd only allowed with -kernel option");
        assert_qemu_start_fails(conflicting_append,
                                "-append only allowed with -kernel option");
        assert_qemu_start_fails(conflicting_dtb, conflict_message);
        assert_qemu_start_fails(invalid,
            "unsupported BeagleV Ahead boot mode 'invalid'");
    }

    g_assert_cmpint(g_unlink(oversized_path), ==, 0);
    g_assert_cmpint(g_unlink(empty_path), ==, 0);
    g_assert_cmpint(g_unlink(path), ==, 0);
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

static uint64_t qtest_qom_clock_period(QTestState *qts, const char *path)
{
    QDict *response = qtest_qmp(
        qts, "{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
        "'property': 'qtest-clock-period' } }", path);
    uint64_t period;

    g_assert_nonnull(response);
    g_assert_true(qdict_haskey(response, "return"));
    period = qdict_get_int(response, "return");
    qobject_unref(response);
    return period;
}

static void th1520_set_ap_clock_gate(QTestState *qts, uint32_t offset,
                                     uint32_t mask, bool enabled)
{
    uint64_t address = TH1520_AP_CLOCK_BASE + offset;
    uint32_t value = qtest_readl(qts, address);

    qtest_writel(qts, address, enabled ? value | mask : value & ~mask);
}

static void assert_dw_timer_reset_state(QTestState *qts, uint64_t base);
static void assert_dw_wdt_reset_state(QTestState *qts, uint64_t base);
static void assert_th1520_pwm_reset_state(QTestState *qts);
static void th1520_pwm_stage(QTestState *qts, unsigned int channel,
                              uint32_t ctrl, uint32_t period, uint32_t fp);
static void th1520_pwm_start(QTestState *qts, unsigned int channel,
                              uint32_t ctrl);

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

    /* Vendor U-Boot's fullmask aperture shares the RevyOS clock state. */
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VENDOR_UBOOT_AP_CLOCK_BASE +
                                TH1520_C910_CLK_CFG), ==, 0x000009f0);
    qtest_writel(qts, TH1520_VENDOR_UBOOT_AP_CLOCK_BASE +
                 TH1520_C910_CLK_CFG, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_AP_CLOCK_BASE + TH1520_C910_CLK_CFG),
                    ==, 0);
    qtest_writel(qts, TH1520_AP_CLOCK_BASE + TH1520_C910_CLK_CFG,
                 0x000009f0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VENDOR_UBOOT_AP_CLOCK_BASE +
                                TH1520_C910_CLK_CFG), ==, 0x000009f0);

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

static void test_ap_clock_gate_outputs(void)
{
    static const struct {
        unsigned int output;
        const char *path;
    } timed_clocks[] = {
        { TH1520_AP_CLOCK_GATE_PWM,
          TH1520_AP_CLOCK_QOM_PATH "/" TH1520_AP_CLOCK_PWM_OUTPUT },
        { TH1520_AP_CLOCK_GATE_TIMER0,
          TH1520_AP_CLOCK_QOM_PATH "/" TH1520_AP_CLOCK_TIMER0_OUTPUT },
        { TH1520_AP_CLOCK_GATE_TIMER1,
          TH1520_AP_CLOCK_QOM_PATH "/" TH1520_AP_CLOCK_TIMER1_OUTPUT },
        { TH1520_AP_CLOCK_GATE_WDT0,
          TH1520_AP_CLOCK_QOM_PATH "/" TH1520_AP_CLOCK_WDT0_OUTPUT },
        { TH1520_AP_CLOCK_GATE_WDT1,
          TH1520_AP_CLOCK_QOM_PATH "/" TH1520_AP_CLOCK_WDT1_OUTPUT },
    };
    const uint64_t enabled_period = CLOCK_PERIOD_FROM_HZ(125000000);
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, TH1520_AP_CLOCK_QOM_PATH,
                                  "peripheral-clock-enable");
    qtest_writel(qts, TH1520_AP_CLOCK_BASE + TH1520_PERI_CLK_CFG, 0);
    qtest_writel(qts, TH1520_AP_CLOCK_BASE + TH1520_CTRL_CLK_CFG, 0);
    for (size_t i = 0;
         i < ARRAY_SIZE(th1520_ap_clock_gate_test_outputs); i++) {
        g_assert_false(qtest_get_irq(qts, i));
    }
    qtest_system_reset(qts);
    for (size_t i = 0;
         i < ARRAY_SIZE(th1520_ap_clock_gate_test_outputs); i++) {
        g_assert_true(qtest_get_irq(qts, i));
    }
    for (size_t i = 0; i < ARRAY_SIZE(timed_clocks); i++) {
        g_assert_cmpuint(qtest_qom_clock_period(qts, timed_clocks[i].path),
                         ==, enabled_period);
    }

    for (size_t output = 0;
         output < ARRAY_SIZE(th1520_ap_clock_gate_test_outputs); output++) {
        const TH1520ClockGateTestOutput *info =
            &th1520_ap_clock_gate_test_outputs[output];
        uint64_t address = TH1520_AP_CLOCK_BASE + info->offset;
        uint32_t original = qtest_readl(qts, address);

        qtest_writel(qts, address, original & ~info->mask);
        for (size_t line = 0;
             line < ARRAY_SIZE(th1520_ap_clock_gate_test_outputs); line++) {
            g_assert_cmpint(qtest_get_irq(qts, line), ==, line != output);
        }
        qtest_writel(qts, address, original);
        g_assert_true(qtest_get_irq(qts, output));
    }

    for (size_t i = 0; i < ARRAY_SIZE(timed_clocks); i++) {
        const TH1520ClockGateTestOutput *info =
            &th1520_ap_clock_gate_test_outputs[timed_clocks[i].output];

        g_assert_cmpuint(qtest_qom_clock_period(qts, timed_clocks[i].path),
                         ==, enabled_period);
        th1520_set_ap_clock_gate(qts, info->offset, info->mask, false);
        g_assert_cmpuint(qtest_qom_clock_period(qts, timed_clocks[i].path),
                         ==, 0);
        th1520_set_ap_clock_gate(qts, info->offset, info->mask, true);
        g_assert_cmpuint(qtest_qom_clock_period(qts, timed_clocks[i].path),
                         ==, enabled_period);
    }

    qtest_quit(qts);
}

static void test_ap_clock_timed_gates(void)
{
    const TH1520ClockGateTestOutput *timer_gate =
        &th1520_ap_clock_gate_test_outputs[TH1520_AP_CLOCK_GATE_TIMER0];
    const TH1520ClockGateTestOutput *wdt_gate =
        &th1520_ap_clock_gate_test_outputs[TH1520_AP_CLOCK_GATE_WDT0];
    const TH1520ClockGateTestOutput *pwm_gate =
        &th1520_ap_clock_gate_test_outputs[TH1520_AP_CLOCK_GATE_PWM];
    const uint32_t pwm_ctrl = TH1520_PWM_CONTINUOUS | TH1520_PWM_FPOUT;
    uint32_t frozen;
    QTestState *qts = qtest_init(
        "-machine beaglev-ahead -bios none -watchdog-action none");

    qtest_irq_intercept_out_named(qts, TH1520_PWM_QOM_PATH, "pwm");
    g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);

    qtest_writel(qts, TH1520_TIMER0_3_BASE + DW_TIMER_LOAD_COUNT, 5);
    qtest_writel(qts, TH1520_TIMER0_3_BASE + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_PERIODIC);
    qtest_clock_step(qts, 2 * TH1520_TIMER_TICK_NS + 1);
    frozen = qtest_readl(qts,
                          TH1520_TIMER0_3_BASE + DW_TIMER_CURRENT_VALUE);
    g_assert_cmphex(frozen, ==, 3);
    th1520_set_ap_clock_gate(qts, timer_gate->offset, timer_gate->mask,
                             false);
    qtest_clock_step(qts, 100 * TH1520_TIMER_TICK_NS);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_TIMER0_3_BASE +
                                DW_TIMER_CURRENT_VALUE), ==, frozen);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_TIMER0_3_BASE +
                                DW_TIMER_INT_STATUS), ==, 0);
    th1520_set_ap_clock_gate(qts, timer_gate->offset, timer_gate->mask,
                             true);
    qtest_clock_step(qts, TH1520_TIMER_TICK_NS + 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_TIMER0_3_BASE +
                                DW_TIMER_CURRENT_VALUE), <, frozen);
    qtest_writel(qts, TH1520_TIMER0_3_BASE + DW_TIMER_CONTROL, 0);

    qtest_writel(qts, TH1520_WDT0_BASE + DW_WDT_TORR, 0);
    qtest_writel(qts, TH1520_WDT0_BASE + DW_WDT_CR,
                  DW_WDT_RMOD | DW_WDT_ENABLE);
    qtest_clock_step(qts, 3 * DW_WDT_TICK_NS);
    frozen = qtest_readl(qts, TH1520_WDT0_BASE + DW_WDT_CCVR);
    g_assert_cmphex(frozen, <, DW_WDT_TOP0_COUNT);
    th1520_set_ap_clock_gate(qts, wdt_gate->offset, wdt_gate->mask, false);
    qtest_clock_step(qts, 100 * DW_WDT_TICK_NS);
    g_assert_cmphex(qtest_readl(qts, TH1520_WDT0_BASE + DW_WDT_CCVR), ==,
                    frozen);
    g_assert_cmphex(qtest_readl(qts, TH1520_WDT0_BASE + DW_WDT_STAT), ==,
                    0);
    th1520_set_ap_clock_gate(qts, wdt_gate->offset, wdt_gate->mask, true);
    qtest_clock_step(qts, DW_WDT_TICK_NS + 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_WDT0_BASE + DW_WDT_CCVR), <,
                    frozen);

    th1520_pwm_stage(qts, 0, pwm_ctrl, 10, 3);
    th1520_pwm_start(qts, 0, pwm_ctrl);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, TH1520_PWM_TICK_NS);
    th1520_set_ap_clock_gate(qts, pwm_gate->offset, pwm_gate->mask, false);
    qtest_clock_step(qts, 100 * TH1520_PWM_TICK_NS);
    g_assert_true(qtest_get_irq(qts, 0));
    th1520_set_ap_clock_gate(qts, pwm_gate->offset, pwm_gate->mask, true);
    qtest_clock_step(qts, 2 * TH1520_PWM_TICK_NS - 1);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, 1);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_system_reset(qts);
    assert_dw_timer_reset_state(qts, TH1520_TIMER0_3_BASE);
    assert_dw_wdt_reset_state(qts, TH1520_WDT0_BASE);
    assert_th1520_pwm_reset_state(qts);
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
    g_assert_cmphex(qtest_readl(qts, TH1520_VENDOR_UBOOT_AP_RESET_NPU_BASE),
                    ==, 0);
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
    qtest_writel(qts, TH1520_VENDOR_UBOOT_AP_RESET_NPU_BASE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x1b0), ==, 1);
    qtest_writel(qts, TH1520_AP_RESET_BASE + 0x1b0, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_VENDOR_UBOOT_AP_RESET_NPU_BASE),
                    ==, 0);
    qtest_writel(qts, TH1520_VENDOR_UBOOT_AP_RESET_NPU_BASE, 1);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x004), ==, 0x3);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x0cc), ==, 0x2);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x220), ==, 0x8);
    g_assert_cmphex(qtest_readl(qts, TH1520_AP_RESET_BASE + 0x1b0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_VENDOR_UBOOT_AP_RESET_NPU_BASE),
                    ==, 0);
    qtest_quit(qts);
}

static void test_ap_reset_outputs(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, TH1520_AP_RESET_QOM_PATH,
                                  "peripheral-reset");

    for (size_t output = 0;
         output < ARRAY_SIZE(th1520_ap_reset_test_outputs); output++) {
        const TH1520ResetTestOutput *info =
            &th1520_ap_reset_test_outputs[output];

        qtest_writel(qts, TH1520_AP_RESET_BASE + info->offset,
                      info->deasserted - 1);
        for (size_t line = 0;
             line < ARRAY_SIZE(th1520_ap_reset_test_outputs); line++) {
            g_assert_cmpint(qtest_get_irq(qts, line), ==, line == output);
        }
        qtest_writel(qts, TH1520_AP_RESET_BASE + info->offset,
                      info->deasserted);
        g_assert_false(qtest_get_irq(qts, output));
    }

    qtest_system_reset(qts);
    for (size_t i = 0;
         i < ARRAY_SIZE(th1520_ap_reset_test_outputs); i++) {
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

static void assert_th1520_iopmp_reset_state(QTestState *qts)
{
    for (size_t i = 0; i < ARRAY_SIZE(th1520_iopmp_controllers); i++) {
        uint64_t base = th1520_iopmp_controllers[i].base;

        g_assert_cmphex(qtest_readl(qts,
                                    base + TH1520_IOPMP_MISC_CTRL), ==, 0);
        g_assert_cmphex(qtest_readl(qts,
                                    base + TH1520_IOPMP_DUMMY_ADDR), ==, 0);
        g_assert_cmphex(qtest_readl(qts,
                                    base + TH1520_IOPMP_PAGE_LOCK0), ==, 0);
        g_assert_cmphex(qtest_readl(qts,
                                    base + TH1520_IOPMP_DEFAULT_ATTR_CFG),
                        ==, 0);
    }
}

static void test_th1520_iopmp_registers(void)
{
    const uint64_t regions = th1520_iopmp_controllers[0].base;
    const uint64_t bypass = th1520_iopmp_controllers[1].base;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_iopmp_reset_state(qts);

    /* Every vendor-referenced aperture has independent default state. */
    for (size_t i = 0; i < ARRAY_SIZE(th1520_iopmp_controllers); i++) {
        uint64_t base = th1520_iopmp_controllers[i].base;
        uint32_t attr = 0x50000000 | i;

        qtest_writel(qts, base + TH1520_IOPMP_DEFAULT_ATTR_CFG, attr);
        g_assert_cmphex(qtest_readl(qts,
                                    base + TH1520_IOPMP_DEFAULT_ATTR_CFG),
                        ==, attr);
    }

    qtest_writel(qts, regions + TH1520_IOPMP_REGION0_SADDR, 0x10000000);
    qtest_writel(qts, regions + TH1520_IOPMP_REGION0_EADDR, 0x20000000);
    qtest_writel(qts, regions + TH1520_IOPMP_ATTR_CFG0, 0x01234567);
    qtest_writel(qts, regions + TH1520_IOPMP_DUMMY_ADDR, 0x00800000);
    qtest_writel(qts, regions + TH1520_IOPMP_PAGE_LOCK0,
                 BIT(0) | TH1520_IOPMP_PAGE_LOCK_DUMMY_ADDR |
                 TH1520_IOPMP_PAGE_LOCK_DEFAULT_CFG);
    qtest_writel(qts, regions + TH1520_IOPMP_REGION0_SADDR, 0x30000000);
    qtest_writel(qts, regions + TH1520_IOPMP_REGION0_EADDR, 0x40000000);
    qtest_writel(qts, regions + TH1520_IOPMP_ATTR_CFG0, 0x76543210);
    qtest_writel(qts, regions + TH1520_IOPMP_DUMMY_ADDR, 0x00400000);
    qtest_writel(qts, regions + TH1520_IOPMP_DEFAULT_ATTR_CFG, 0xdeadbeef);
    g_assert_cmphex(qtest_readl(qts,
                                regions + TH1520_IOPMP_REGION0_SADDR), ==,
                    0x10000000);
    g_assert_cmphex(qtest_readl(qts,
                                regions + TH1520_IOPMP_REGION0_EADDR), ==,
                    0x20000000);
    g_assert_cmphex(qtest_readl(qts, regions + TH1520_IOPMP_ATTR_CFG0), ==,
                    0x01234567);
    g_assert_cmphex(qtest_readl(qts, regions + TH1520_IOPMP_DUMMY_ADDR), ==,
                    0x00800000);
    g_assert_cmphex(qtest_readl(qts,
                                regions + TH1520_IOPMP_DEFAULT_ATTR_CFG), ==,
                    0x50000000);

    /* Region locks are independent; only region zero was made sticky. */
    qtest_writel(qts, regions + TH1520_IOPMP_ATTR_CFG0 + 4, 0x89abcdef);
    g_assert_cmphex(qtest_readl(qts,
                                regions + TH1520_IOPMP_ATTR_CFG0 + 4), ==,
                    0x89abcdef);

    qtest_writel(qts, bypass + TH1520_IOPMP_MISC_CTRL, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, bypass + TH1520_IOPMP_MISC_CTRL), ==,
                    TH1520_IOPMP_CTRL_BYPASS);
    qtest_writel(qts, bypass + TH1520_IOPMP_DEFAULT_ATTR_CFG, 0xabcdef01);
    qtest_writel(qts, bypass + TH1520_IOPMP_REGION0_SADDR, 0x10000000);
    g_assert_cmphex(qtest_readl(qts,
                                bypass + TH1520_IOPMP_DEFAULT_ATTR_CFG), ==,
                    0x50000001);
    g_assert_cmphex(qtest_readl(qts,
                                bypass + TH1520_IOPMP_REGION0_SADDR), ==,
                    0);
    qtest_writel(qts, bypass + TH1520_IOPMP_PAGE_LOCK0,
                 TH1520_IOPMP_PAGE_LOCK_BYPASS_EN);
    qtest_writel(qts, bypass + TH1520_IOPMP_MISC_CTRL, 0);
    g_assert_cmphex(qtest_readl(qts, bypass + TH1520_IOPMP_MISC_CTRL), ==,
                    TH1520_IOPMP_CTRL_BYPASS);

    qtest_system_reset(qts);
    assert_th1520_iopmp_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_iopmp_migration(void)
{
    const uint64_t regions = th1520_iopmp_controllers[0].base;
    const uint64_t bypass = th1520_iopmp_controllers[1].base;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-iopmp-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    for (size_t i = 0; i < ARRAY_SIZE(th1520_iopmp_controllers); i++) {
        qtest_writel(src, th1520_iopmp_controllers[i].base +
                     TH1520_IOPMP_DEFAULT_ATTR_CFG, 0x60000000 | i);
    }
    qtest_writel(src, regions + TH1520_IOPMP_REGION0_SADDR, 0x01000000);
    qtest_writel(src, regions + TH1520_IOPMP_REGION0_EADDR, 0x01ffffff);
    qtest_writel(src, regions + TH1520_IOPMP_ATTR_CFG0, 0x10203040);
    qtest_writel(src, regions + TH1520_IOPMP_DUMMY_ADDR, 0x00800000);
    qtest_writel(src, regions + TH1520_IOPMP_PAGE_LOCK0,
                 BIT(0) | TH1520_IOPMP_PAGE_LOCK_DUMMY_ADDR |
                 TH1520_IOPMP_PAGE_LOCK_DEFAULT_CFG);
    qtest_writel(src, bypass + TH1520_IOPMP_MISC_CTRL,
                 TH1520_IOPMP_CTRL_BYPASS);
    qtest_writel(src, bypass + TH1520_IOPMP_PAGE_LOCK0,
                 TH1520_IOPMP_PAGE_LOCK_BYPASS_EN);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    for (size_t i = 0; i < ARRAY_SIZE(th1520_iopmp_controllers); i++) {
        g_assert_cmphex(qtest_readl(dst, th1520_iopmp_controllers[i].base +
                                    TH1520_IOPMP_DEFAULT_ATTR_CFG), ==,
                        0x60000000 | i);
    }
    g_assert_cmphex(qtest_readl(dst,
                                regions + TH1520_IOPMP_REGION0_SADDR), ==,
                    0x01000000);
    g_assert_cmphex(qtest_readl(dst,
                                regions + TH1520_IOPMP_REGION0_EADDR), ==,
                    0x01ffffff);
    g_assert_cmphex(qtest_readl(dst, regions + TH1520_IOPMP_ATTR_CFG0), ==,
                    0x10203040);
    g_assert_cmphex(qtest_readl(dst, regions + TH1520_IOPMP_DUMMY_ADDR), ==,
                    0x00800000);
    g_assert_cmphex(qtest_readl(dst, regions + TH1520_IOPMP_PAGE_LOCK0), ==,
                    BIT(0) | TH1520_IOPMP_PAGE_LOCK_DUMMY_ADDR |
                    TH1520_IOPMP_PAGE_LOCK_DEFAULT_CFG);
    g_assert_cmphex(qtest_readl(dst, bypass + TH1520_IOPMP_MISC_CTRL), ==,
                    TH1520_IOPMP_CTRL_BYPASS);
    g_assert_cmphex(qtest_readl(dst, bypass + TH1520_IOPMP_PAGE_LOCK0), ==,
                    TH1520_IOPMP_PAGE_LOCK_BYPASS_EN);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_th1520_video_sysreg_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_ISP0_CLK_CFG), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_ISP1_CLK_CFG), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_ISP_RY_CLK_CFG), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_MIPI_CSI0_PIXELCLK), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_GPU_RST_CFG), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_CLK_GATE), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_CLK_GATE1), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_DPU_CCLK_CFG), ==, 0);
}

static void test_th1520_video_sysreg_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_video_sysreg_reset_state(qts);

    qtest_writel(qts, TH1520_VISYS_BASE + TH1520_VISYS_ISP0_CLK_CFG,
                 UINT32_MAX);
    qtest_writel(qts, TH1520_VISYS_BASE + TH1520_VISYS_ISP1_CLK_CFG,
                 UINT32_MAX);
    qtest_writel(qts, TH1520_VISYS_BASE + TH1520_VISYS_ISP_RY_CLK_CFG,
                 UINT32_MAX);
    qtest_writel(qts, TH1520_VISYS_BASE + TH1520_VISYS_MIPI_CSI0_PIXELCLK,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_ISP0_CLK_CFG), ==,
                    TH1520_VISYS_CLK_DIV_MASK);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_ISP1_CLK_CFG), ==,
                    TH1520_VISYS_CLK_DIV_MASK);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_ISP_RY_CLK_CFG), ==,
                    TH1520_VISYS_CLK_DIV_MASK);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_MIPI_CSI0_PIXELCLK), ==,
                    TH1520_VISYS_CLK_DIV_MASK);

    qtest_writel(qts, TH1520_VOSYS_BASE + TH1520_VOSYS_GPU_RST_CFG,
                 UINT32_MAX);
    qtest_writel(qts, TH1520_VOSYS_BASE + TH1520_VOSYS_CLK_GATE,
                 UINT32_MAX);
    qtest_writel(qts, TH1520_VOSYS_BASE + TH1520_VOSYS_CLK_GATE1,
                 UINT32_MAX);
    qtest_writel(qts, TH1520_VOSYS_BASE + TH1520_VOSYS_DPU_CCLK_CFG,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_GPU_RST_CFG), ==,
                    TH1520_VOSYS_GPU_RST_CFG_MASK);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_CLK_GATE), ==,
                    TH1520_VOSYS_CLK_GATE_MASK);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_CLK_GATE1), ==,
                    TH1520_VOSYS_CLK_GATE1_MASK);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_DPU_CCLK_CFG), ==,
                    TH1520_VOSYS_DPU_CCLK_MASK);

    qtest_system_reset(qts);
    assert_th1520_video_sysreg_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_video_sysreg_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-video-sysreg-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    qtest_writel(src, TH1520_VISYS_BASE + TH1520_VISYS_ISP0_CLK_CFG,
                 0x1f);
    qtest_writel(src, TH1520_VISYS_BASE + TH1520_VISYS_ISP1_CLK_CFG,
                 0x13);
    qtest_writel(src, TH1520_VISYS_BASE + TH1520_VISYS_ISP_RY_CLK_CFG,
                 0x0c);
    qtest_writel(src, TH1520_VISYS_BASE + TH1520_VISYS_MIPI_CSI0_PIXELCLK,
                 0x1c);
    qtest_writel(src, TH1520_VOSYS_BASE + TH1520_VOSYS_GPU_RST_CFG, 0x2);
    qtest_writel(src, TH1520_VOSYS_BASE + TH1520_VOSYS_CLK_GATE,
                 0x80800019);
    qtest_writel(src, TH1520_VOSYS_BASE + TH1520_VOSYS_CLK_GATE1, 1);
    qtest_writel(src, TH1520_VOSYS_BASE + TH1520_VOSYS_DPU_CCLK_CFG,
                 0x14);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_ISP0_CLK_CFG), ==, 0x1f);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_ISP1_CLK_CFG), ==, 0x13);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_ISP_RY_CLK_CFG), ==, 0x0c);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_VISYS_BASE +
                                TH1520_VISYS_MIPI_CSI0_PIXELCLK), ==,
                    0x1c);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_GPU_RST_CFG), ==, 0x2);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_CLK_GATE), ==, 0x80800019);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_CLK_GATE1), ==, 1);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_VOSYS_BASE +
                                TH1520_VOSYS_DPU_CCLK_CFG), ==, 0x14);

    qtest_system_reset(dst);
    assert_th1520_video_sysreg_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_th1520_ddr_pll_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_PLL_CFG0_BASE), ==,
                    TH1520_DDR_PLL_CFG0_RESET);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_PLL_CFG1_BASE), ==,
                    TH1520_DDR_PLL_CFG1_RESET);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_PLL_STS_BASE), ==, 0);
}

static void test_th1520_ddr_pll_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_ddr_pll_reset_state(qts);

    qtest_writel(qts, TH1520_DDR_PLL_CFG0_BASE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_PLL_CFG0_BASE), ==,
                    TH1520_DDR_PLL_CFG0_WRITABLE_MASK);

    qtest_writel(qts, TH1520_DDR_PLL_CFG1_BASE, 0x4b000000);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_PLL_CFG1_BASE), ==,
                    0x4b000000);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_PLL_STS_BASE), ==, 0);

    qtest_writel(qts, TH1520_DDR_PLL_CFG1_BASE, 0x0b000000);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_PLL_CFG1_BASE), ==,
                    0x0b000000);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_PLL_STS_BASE), ==,
                    TH1520_DDR_PLL_STS_LOCK);

    qtest_writel(qts, TH1520_DDR_PLL_STS_BASE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_PLL_STS_BASE), ==,
                    TH1520_DDR_PLL_STS_LOCK |
                    TH1520_DDR_PLL_STS_CORE_CLOCK_CG);

    qtest_system_reset(qts);
    assert_th1520_ddr_pll_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_ddr_pll_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-ddr-pll-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    qtest_writel(src, TH1520_DDR_PLL_CFG0_BASE, 0x01204d01);
    qtest_writel(src, TH1520_DDR_PLL_CFG1_BASE, 0x0b000000);
    qtest_writel(src, TH1520_DDR_PLL_STS_BASE,
                  TH1520_DDR_PLL_STS_CORE_CLOCK_CG);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_DDR_PLL_CFG0_BASE), ==,
                    0x01204d01);
    g_assert_cmphex(qtest_readl(dst, TH1520_DDR_PLL_CFG1_BASE), ==,
                    0x0b000000);
    g_assert_cmphex(qtest_readl(dst, TH1520_DDR_PLL_STS_BASE), ==,
                    TH1520_DDR_PLL_STS_LOCK |
                    TH1520_DDR_PLL_STS_CORE_CLOCK_CG);

    qtest_system_reset(dst);
    assert_th1520_ddr_pll_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_th1520_ddr_control_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CFG0_BASE), ==,
                    TH1520_DDR_CFG0_RESET);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CFG1_BASE), ==,
                    TH1520_DDR_CFG1_RESET);
}

static void test_th1520_ddr_control_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_ddr_control_reset_state(qts);

    qtest_writel(qts, TH1520_DDR_CFG0_BASE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CFG0_BASE), ==,
                    TH1520_DDR_CFG0_WRITABLE_MASK);

    qtest_writel(qts, TH1520_DDR_CFG0_BASE, 0x000000d0);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CFG0_BASE), ==,
                    0x000000d0);

    qtest_writel(qts, TH1520_DDR_CFG1_BASE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CFG1_BASE), ==,
                    TH1520_DDR_CFG1_WRITABLE_MASK);

    qtest_writel(qts, TH1520_DDR_CFG1_BASE, 0x000a0000);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CFG1_BASE), ==,
                    0x000a0000);

    qtest_system_reset(qts);
    assert_th1520_ddr_control_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_ddr_control_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-ddr-control-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    qtest_writel(src, TH1520_DDR_CFG0_BASE, 0x000000d0);
    qtest_writel(src, TH1520_DDR_CFG1_BASE, 0x000a0000);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_DDR_CFG0_BASE), ==,
                    0x000000d0);
    g_assert_cmphex(qtest_readl(dst, TH1520_DDR_CFG1_BASE), ==,
                    0x000a0000);

    qtest_system_reset(dst);
    assert_th1520_ddr_control_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void th1520_ddr_phy_complete_training(QTestState *qts, uint64_t base)
{
    qtest_writew(qts, base + TH1520_DDR_PHY_TRAINING_TRIGGER, 0x9);
    qtest_writew(qts, base + TH1520_DDR_PHY_TRAINING_TRIGGER, 0x1);
    qtest_writew(qts, base + TH1520_DDR_PHY_TRAINING_TRIGGER, 0x0);
    g_assert_cmphex(qtest_readw(qts, base + TH1520_DDR_PHY_MAILBOX_STATUS),
                    ==, 0);
    g_assert_cmphex(qtest_readw(qts, base + TH1520_DDR_PHY_MAILBOX_MSG0),
                    ==, 0x7);
    g_assert_cmphex(qtest_readw(qts, base + TH1520_DDR_PHY_MAILBOX_MSG1),
                    ==, 0);
    qtest_writew(qts, base + TH1520_DDR_PHY_MAILBOX_ACK, 0);
    g_assert_cmphex(qtest_readw(qts, base + TH1520_DDR_PHY_MAILBOX_STATUS),
                    ==, 1);
}

static void assert_th1520_ddr_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_DDR_CONTROLLER_BASE +
                                TH1520_DDR_CTRL_STAT), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_DDR_CONTROLLER_BASE +
                                TH1520_DDR_CTRL_DFISTAT), ==, 0);
    g_assert_cmphex(qtest_readw(qts,
                                TH1520_DDR_PHY0_BASE +
                                TH1520_DDR_PHY_MAILBOX_STATUS), ==, 1);
    g_assert_cmphex(qtest_readw(qts,
                                TH1520_DDR_PHY1_BASE +
                                TH1520_DDR_PHY_MAILBOX_STATUS), ==, 1);
}

static void test_th1520_ddr_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_ddr_reset_state(qts);

    qtest_writel(qts, TH1520_DDR_CONTROLLER_BASE + 0x304, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CONTROLLER_BASE + 0x304),
                    ==, 1);
    qtest_writew(qts, TH1520_DDR_PHY0_BASE + (0x1005f * 2), 0x55f);
    g_assert_cmphex(qtest_readw(qts,
                                TH1520_DDR_PHY0_BASE + (0x1005f * 2)),
                    ==, 0x55f);

    qtest_writel(qts, TH1520_DDR_CONTROLLER_BASE +
                 TH1520_DDR_CTRL_DFIMISC, 0x30);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CONTROLLER_BASE +
                                TH1520_DDR_CTRL_DFISTAT), ==, 0);
    th1520_ddr_phy_complete_training(qts, TH1520_DDR_PHY0_BASE);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CONTROLLER_BASE +
                                TH1520_DDR_CTRL_DFISTAT), ==, 0);
    th1520_ddr_phy_complete_training(qts, TH1520_DDR_PHY1_BASE);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CONTROLLER_BASE +
                                TH1520_DDR_CTRL_DFISTAT), ==, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CONTROLLER_BASE +
                                TH1520_DDR_CTRL_DCH1_DFISTAT), ==, 1);

    qtest_writel(qts, TH1520_DDR_CONTROLLER_BASE +
                 TH1520_DDR_CTRL_DFIMISC, 0x11);
    qtest_writel(qts, TH1520_DDR_CONTROLLER_BASE +
                 TH1520_DDR_CTRL_SWCTL, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CONTROLLER_BASE +
                                TH1520_DDR_CTRL_SWSTAT), ==, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CONTROLLER_BASE +
                                TH1520_DDR_CTRL_STAT), ==, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_DDR_CONTROLLER_BASE +
                                TH1520_DDR_CTRL_DCH1_STAT), ==, 1);

    qtest_system_reset(qts);
    assert_th1520_ddr_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_ddr_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-ddr-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    qtest_writel(src, TH1520_DDR_CONTROLLER_BASE + 0x304, 1);
    qtest_writew(src, TH1520_DDR_PHY0_BASE + (0x1005f * 2), 0x55f);
    th1520_ddr_phy_complete_training(src, TH1520_DDR_PHY0_BASE);
    th1520_ddr_phy_complete_training(src, TH1520_DDR_PHY1_BASE);
    qtest_writel(src, TH1520_DDR_CONTROLLER_BASE +
                 TH1520_DDR_CTRL_DFIMISC, 0x30);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_DDR_CONTROLLER_BASE + 0x304),
                    ==, 1);
    g_assert_cmphex(qtest_readw(dst,
                                TH1520_DDR_PHY0_BASE + (0x1005f * 2)),
                    ==, 0x55f);
    g_assert_cmphex(qtest_readl(dst, TH1520_DDR_CONTROLLER_BASE +
                                TH1520_DDR_CTRL_DFISTAT), ==, 1);

    qtest_system_reset(dst);
    assert_th1520_ddr_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_th1520_iso7816_config_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, TH1520_ISO7816_CONFIG_BASE), ==, 0);
}

static void test_th1520_iso7816_config_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_iso7816_config_reset_state(qts);
    qtest_writel(qts, TH1520_ISO7816_CONFIG_BASE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_ISO7816_CONFIG_BASE), ==,
                    TH1520_ISO7816_CONFIG_MIE);
    qtest_writel(qts, TH1520_ISO7816_CONFIG_BASE, 0);
    assert_th1520_iso7816_config_reset_state(qts);

    qtest_writel(qts, TH1520_ISO7816_CONFIG_BASE,
                 TH1520_ISO7816_CONFIG_MIE);
    qtest_system_reset(qts);
    assert_th1520_iso7816_config_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_iso7816_config_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-iso7816-config-XXXXXX", &path,
                         NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_writel(src, TH1520_ISO7816_CONFIG_BASE,
                 TH1520_ISO7816_CONFIG_MIE);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_ISO7816_CONFIG_BASE), ==,
                    TH1520_ISO7816_CONFIG_MIE);
    qtest_system_reset(dst);
    assert_th1520_iso7816_config_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static const uint32_t th1520_pmp_portal_offsets[
    TH1520_PMP_PORTAL_REG_COUNT] = {
    TH1520_PMP_PORTAL_CONFIG,
    TH1520_PMP_PORTAL_WORD_100,
    TH1520_PMP_PORTAL_WORD_104,
    TH1520_PMP_PORTAL_WORD_108,
    TH1520_PMP_PORTAL_WORD_10C,
};

/* Values from the public vendor SPL's clear_ddr_pmp() sequence. */
static const uint32_t th1520_pmp_portal_restore_values[
    TH1520_PMP_PORTAL_REG_COUNT] = {
    0x00004040,
    0x00000000,
    0x00400000,
    0x0ffe0180,
    0x0ffe1000,
};

static void assert_th1520_pmp_portal_values(
    QTestState *qts, const uint32_t values[TH1520_PMP_PORTAL_REG_COUNT])
{
    for (int i = 0; i < TH1520_PMP_PORTAL_REG_COUNT; i++) {
        g_assert_cmphex(qtest_readl(qts, TH1520_PMP_PORTAL_BASE +
                                    th1520_pmp_portal_offsets[i]), ==,
                        values[i]);
    }
}

static void assert_th1520_pmp_portal_reset_state(QTestState *qts)
{
    static const uint32_t reset_values[TH1520_PMP_PORTAL_REG_COUNT];

    assert_th1520_pmp_portal_values(qts, reset_values);
}

static void write_th1520_pmp_portal_restore_values(QTestState *qts)
{
    /* Vendor SPL writes 0x104, 0x100, 0x10c, 0x108, then configuration. */
    static const uint8_t spl_write_order[] = { 2, 1, 4, 3, 0 };

    for (int i = 0; i < ARRAY_SIZE(spl_write_order); i++) {
        uint8_t reg = spl_write_order[i];

        qtest_writel(qts, TH1520_PMP_PORTAL_BASE +
                     th1520_pmp_portal_offsets[reg],
                     th1520_pmp_portal_restore_values[reg]);
    }
}

static void test_th1520_pmp_portal_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_pmp_portal_reset_state(qts);
    write_th1520_pmp_portal_restore_values(qts);
    assert_th1520_pmp_portal_values(qts, th1520_pmp_portal_restore_values);
    qtest_system_reset(qts);
    assert_th1520_pmp_portal_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_pmp_portal_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-pmp-portal-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    write_th1520_pmp_portal_restore_values(src);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    assert_th1520_pmp_portal_values(dst, th1520_pmp_portal_restore_values);
    qtest_system_reset(dst);
    assert_th1520_pmp_portal_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_th1520_bootsel_state(QTestState *qts, uint8_t boot_sel)
{
    g_assert_cmphex(qtest_readl(qts, TH1520_BOOTSEL_BASE), ==,
                    (boot_sel & TH1520_BOOTSEL_SELECT_MASK) |
                    TH1520_BOOTSEL_UPDATE);
}

static void test_th1520_bootsel_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_bootsel_state(qts, TH1520_BOOTSEL_EMMC);
    qtest_writel(qts, TH1520_BOOTSEL_BASE, UINT32_MAX);
    assert_th1520_bootsel_state(qts, TH1520_BOOTSEL_EMMC);
    qtest_system_reset(qts);
    assert_th1520_bootsel_state(qts, TH1520_BOOTSEL_EMMC);
    qtest_quit(qts);

    qts = qtest_init("-machine beaglev-ahead,boot-sel=5 -bios none");
    assert_th1520_bootsel_state(qts, 5);
    qtest_quit(qts);
}

static void test_th1520_bootsel_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-bootsel-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead,boot-sel=5 -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    assert_th1520_bootsel_state(src, 5);
    assert_th1520_bootsel_state(dst, TH1520_BOOTSEL_EMMC);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    assert_th1520_bootsel_state(dst, 5);
    qtest_system_reset(dst);
    assert_th1520_bootsel_state(dst, 5);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_th1520_tee_miscsys_clock_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, TH1520_TEE_MISCSYS_CLOCK_BASE), ==,
                    TH1520_TEE_MISCSYS_CLOCK_RESET);
}

static void test_th1520_tee_miscsys_clock_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_tee_miscsys_clock_reset_state(qts);
    qtest_writel(qts, TH1520_TEE_MISCSYS_CLOCK_BASE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_TEE_MISCSYS_CLOCK_BASE), ==,
                    TH1520_TEE_MISCSYS_CLOCK_ENABLE_MASK);
    qtest_writel(qts, TH1520_TEE_MISCSYS_CLOCK_BASE,
                 TH1520_TEE_MISCSYS_CLOCK_RESET & ~BIT(6));
    g_assert_cmphex(qtest_readl(qts, TH1520_TEE_MISCSYS_CLOCK_BASE), ==,
                    TH1520_TEE_MISCSYS_CLOCK_RESET & ~BIT(6));
    qtest_system_reset(qts);
    assert_th1520_tee_miscsys_clock_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_tee_miscsys_clock_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-tee-miscsys-clock-XXXXXX", &path,
                         NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_writel(src, TH1520_TEE_MISCSYS_CLOCK_BASE, 0x5a5);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_TEE_MISCSYS_CLOCK_BASE), ==,
                    0x5a5);
    qtest_system_reset(dst);
    assert_th1520_tee_miscsys_clock_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_th1520_tee_dsp_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, TH1520_TEE_DSP_RESET_BASE), ==,
                    TH1520_TEE_DSP_RESET_SW_RST_RESET);
}

static void test_th1520_tee_dsp_reset_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_tee_dsp_reset_state(qts);
    qtest_writel(qts, TH1520_TEE_DSP_RESET_BASE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_TEE_DSP_RESET_BASE), ==,
                    TH1520_TEE_DSP_RESET_SW_RST_MASK);
    qtest_writel(qts, TH1520_TEE_DSP_RESET_BASE,
                 TH1520_TEE_DSP_RESET_SW_RST_RESET & ~BIT(30));
    g_assert_cmphex(qtest_readl(qts, TH1520_TEE_DSP_RESET_BASE), ==,
                    TH1520_TEE_DSP_RESET_SW_RST_RESET & ~BIT(30));
    qtest_system_reset(qts);
    assert_th1520_tee_dsp_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_tee_dsp_reset_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-tee-dsp-reset-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_writel(src, TH1520_TEE_DSP_RESET_BASE, UINT32_MAX);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_TEE_DSP_RESET_BASE), ==,
                    TH1520_TEE_DSP_RESET_SW_RST_MASK);
    qtest_system_reset(dst);
    assert_th1520_tee_dsp_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_th1520_tee_vosys_dpu_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, TH1520_TEE_VOSYS_DPU_RESET_BASE), ==,
                    TH1520_TEE_VOSYS_DPU_RESET_VALUE);
}

static void test_th1520_tee_vosys_dpu_reset_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_tee_vosys_dpu_reset_state(qts);
    qtest_writel(qts, TH1520_TEE_VOSYS_DPU_RESET_BASE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_TEE_VOSYS_DPU_RESET_BASE), ==,
                    TH1520_TEE_VOSYS_DPU_RESET_MASK);
    qtest_writel(qts, TH1520_TEE_VOSYS_DPU_RESET_BASE, 0);
    assert_th1520_tee_vosys_dpu_reset_state(qts);
    qtest_writel(qts, TH1520_TEE_VOSYS_DPU_RESET_BASE,
                 TH1520_TEE_VOSYS_DPU_RESET_MASK);
    qtest_system_reset(qts);
    assert_th1520_tee_vosys_dpu_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_tee_vosys_dpu_reset_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-tee-vosys-dpu-reset-XXXXXX", &path,
                         NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_writel(src, TH1520_TEE_VOSYS_DPU_RESET_BASE, UINT32_MAX);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_TEE_VOSYS_DPU_RESET_BASE), ==,
                    TH1520_TEE_VOSYS_DPU_RESET_MASK);
    qtest_system_reset(dst);
    assert_th1520_tee_vosys_dpu_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void assert_th1520_aon_audio_reset_state(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, TH1520_AON_AUDIO_RESET_BASE), ==,
                    TH1520_AON_RESET_AUDIO_RST_CFG_RESET);
}

static void test_th1520_aon_audio_reset_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    assert_th1520_aon_audio_reset_state(qts);
    qtest_writel(qts, TH1520_AON_AUDIO_RESET_BASE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_AON_AUDIO_RESET_BASE), ==,
                    TH1520_AON_RESET_AUDIO_RST_CFG_MASK);
    qtest_writel(qts, TH1520_AON_AUDIO_RESET_BASE, 0x37);
    g_assert_cmphex(qtest_readl(qts, TH1520_AON_AUDIO_RESET_BASE), ==,
                    0x37);
    qtest_system_reset(qts);
    assert_th1520_aon_audio_reset_state(qts);
    qtest_quit(qts);
}

static void test_th1520_aon_audio_reset_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-aon-audio-reset-XXXXXX", &path,
                         NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_writel(src, TH1520_AON_AUDIO_RESET_BASE, 0x37);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_AON_AUDIO_RESET_BASE), ==,
                    0x37);
    qtest_system_reset(dst);
    assert_th1520_aon_audio_reset_state(dst);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void mr75203_qom_set(QTestState *qts, const char *property,
                            int64_t value)
{
    qtest_qmp_assert_success(
        qts, "{ 'execute': 'qom-set', 'arguments': { 'path': %s, "
        "'property': %s, 'value': %" PRId64 " } }",
        TH1520_PVT_QOM_PATH, property, value);
}

static int64_t mr75203_qom_get(QTestState *qts, const char *property)
{
    QDict *response = qtest_qmp(
        qts, "{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
        "'property': %s } }", TH1520_PVT_QOM_PATH, property);
    int64_t value;

    g_assert_nonnull(response);
    g_assert_true(qdict_haskey(response, "return"));
    value = qdict_get_int(response, "return");
    qobject_unref(response);
    return value;
}

static int64_t mr75203_temperature_from_raw(uint16_t raw)
{
    const uint64_t ip_frequency = MR75203_INPUT_FREQUENCY / 12;

    return MR75203_TS_COEFF_G +
           (int64_t)MR75203_TS_COEFF_H * raw / MR75203_TS_COEFF_CAL5 -
           MR75203_TS_COEFF_H / 2 +
           (int64_t)MR75203_TS_COEFF_J * (int64_t)ip_frequency / 1000000;
}

static int32_t mr75203_voltage_from_raw(uint16_t raw)
{
    return ((int64_t)90 * raw - 245805) / 1024;
}

static void mr75203_program_ts(QTestState *qts)
{
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_SAMPLE_CTRL, 0);
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_SDIF_HALT, 0);
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_CLK_SYNTH,
                 MR75203_CLK_SYNTH_VALUE);
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_SDIF_DISABLE, 0);
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_SDIF_W,
                 MR75203_TS_CONFIG_WRITE);
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_SDIF_W,
                 MR75203_TS_TIMER_WRITE);
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_SDIF_W,
                 MR75203_TS_CTRL_WRITE);
}

static void mr75203_program_vm(QTestState *qts)
{
    qtest_writel(qts, TH1520_PVT_VM_BASE + MR75203_SAMPLE_CTRL, 0);
    qtest_writel(qts, TH1520_PVT_VM_BASE + MR75203_SDIF_HALT, 0);
    qtest_writel(qts, TH1520_PVT_VM_BASE + MR75203_CLK_SYNTH,
                 MR75203_CLK_SYNTH_VALUE);
    qtest_writel(qts, TH1520_PVT_VM_BASE + MR75203_SDIF_DISABLE, 0);
    qtest_writel(qts, TH1520_PVT_VM_BASE + MR75203_SDIF_W,
                 MR75203_VM_POLL_WRITE);
    qtest_writel(qts, TH1520_PVT_VM_BASE + MR75203_SDIF_W,
                 MR75203_VM_CONFIG_WRITE);
    qtest_writel(qts, TH1520_PVT_VM_BASE + MR75203_SDIF_W,
                 MR75203_VM_TIMER_WRITE);
    qtest_writel(qts, TH1520_PVT_VM_BASE + MR75203_SDIF_W,
                 MR75203_VM_CTRL_WRITE);
}

static void assert_mr75203_reset_state(QTestState *qts)
{
    static const uint64_t macro_bases[] = {
        TH1520_PVT_TS_BASE,
        TH1520_PVT_PD_BASE,
        TH1520_PVT_VM_BASE,
    };

    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_COMMON_BASE + MR75203_COMP_ID),
                    ==, 0x9b487060);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_COMMON_BASE + MR75203_IP_CONFIG),
                    ==, 0x10010b02);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_COMMON_BASE + MR75203_ID_NUM),
                    ==, 0x12345678);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_COMMON_BASE + MR75203_SCRATCH),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_COMMON_BASE +
                                MR75203_LOCK_STATUS), ==, 0);

    for (size_t i = 0; i < ARRAY_SIZE(macro_bases); i++) {
        uint64_t base = macro_bases[i];

        g_assert_cmphex(qtest_readl(qts, base + MR75203_CLK_SYNTH), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + MR75203_SDIF_DISABLE), ==,
                        0);
        g_assert_cmphex(qtest_readl(qts, base + MR75203_SDIF_STATUS), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + MR75203_SDIF_W), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + MR75203_SDIF_CTRL), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + MR75203_SAMPLE_CTRL), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + MR75203_SAMPLE_COUNT), ==, 0);
    }
}

static void test_mr75203_registers(void)
{
    const int32_t temperatures[] = { -12500, 73000 };
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");
    uint16_t raw;

    assert_mr75203_reset_state(qts);
    g_assert_cmpint(mr75203_qom_get(qts, "temperature[0]"), ==, 25000);
    g_assert_cmpint(mr75203_qom_get(qts, "voltage[0]"), ==, 800);

    qtest_writel(qts, TH1520_PVT_COMMON_BASE + MR75203_ID_NUM, 0xabcdef01);
    qtest_writel(qts, TH1520_PVT_COMMON_BASE + MR75203_SCRATCH, 0x10203040);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_COMMON_BASE + MR75203_ID_NUM),
                    ==, 0xabcdef01);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_COMMON_BASE + MR75203_SCRATCH),
                    ==, 0x10203040);

    qtest_writel(qts, TH1520_PVT_COMMON_BASE + MR75203_REG_LOCK, 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_COMMON_BASE +
                                MR75203_LOCK_STATUS), ==, 1);
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_CLK_SYNTH,
                 MR75203_CLK_SYNTH_VALUE);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_TS_BASE + MR75203_CLK_SYNTH),
                    ==, 0);
    qtest_writel(qts, TH1520_PVT_COMMON_BASE + MR75203_REG_LOCK,
                 MR75203_UNLOCK_VALUE);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_COMMON_BASE +
                                MR75203_LOCK_STATUS), ==, 0);

    for (size_t i = 0; i < ARRAY_SIZE(temperatures); i++) {
        g_autofree char *property = g_strdup_printf("temperature[%zu]", i);

        mr75203_qom_set(qts, property, temperatures[i]);
    }
    mr75203_program_ts(qts);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_TS_BASE + MR75203_SDIF_STATUS),
                    ==, MR75203_SDIF_LOCK);
    for (size_t i = 0; i < ARRAY_SIZE(temperatures); i++) {
        g_assert_cmphex(qtest_readl(qts,
                                    TH1520_PVT_TS_BASE +
                                    MR75203_SDIF_DONE(i)), ==, 1);
        raw = qtest_readl(qts, TH1520_PVT_TS_BASE + MR75203_SDIF_DATA(i));
        g_assert_cmpint(llabs(mr75203_temperature_from_raw(raw) -
                              temperatures[i]), <=, 55);
    }
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_TS_BASE +
                                MR75203_SAMPLE_COUNT), ==, 2);

    /* Counter disable leaves conversion running but freezes the count. */
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_SAMPLE_CTRL, BIT(0));
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_TS_BASE + MR75203_SDIF_DONE(0)),
                    ==, 1);
    qtest_readl(qts, TH1520_PVT_TS_BASE + MR75203_SDIF_DATA(0));
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_TS_BASE +
                                MR75203_SAMPLE_COUNT), ==, 2);
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_SAMPLE_CLEAR, 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_TS_BASE +
                                MR75203_SAMPLE_COUNT), ==, 0);
    qtest_writel(qts, TH1520_PVT_TS_BASE + MR75203_SAMPLE_CTRL, 0);

    mr75203_qom_set(qts, "voltage[3]", 900);
    mr75203_program_vm(qts);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_VM_BASE + MR75203_SDIF_STATUS),
                    ==, MR75203_SDIF_LOCK);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_PVT_VM_BASE + MR75203_VM_DONE(0)),
                    ==, 1);
    raw = qtest_readl(qts,
                      TH1520_PVT_VM_BASE + MR75203_VM_DATA(0, 3));
    g_assert_cmpint(mr75203_voltage_from_raw(raw), ==, 900);

    qtest_system_reset(qts);
    assert_mr75203_reset_state(qts);
    g_assert_cmpint(mr75203_qom_get(qts, "temperature[0]"), ==,
                    temperatures[0]);
    g_assert_cmpint(mr75203_qom_get(qts, "temperature[1]"), ==,
                    temperatures[1]);
    g_assert_cmpint(mr75203_qom_get(qts, "voltage[3]"), ==, 900);
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

static void test_dmac_width_and_fixed_address(void)
{
    enum {
        WIDE_TRANSFERS = 8,
        FIXED_SOURCE_TRANSFERS = 9,
        FIXED_DESTINATION_TRANSFERS = 7,
    };
    static const uint8_t wide_source[WIDE_TRANSFERS * 2] = {
        0x9e, 0x37, 0x42, 0xa1, 0x5c, 0xe8, 0x13, 0x76,
        0x2d, 0xb4, 0x68, 0xf0, 0x09, 0xcd, 0x54, 0x8b,
    };
    static const uint8_t fixed_source[] = {
        0x5a, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
    };
    static const uint8_t fixed_destination_source[] = {
        0x81, 0x22, 0xe4, 0x35, 0x76, 0x48, 0x9b,
    };
    const uint64_t wide_source_addr = DMAC_TEST_SOURCE_ADDR + 0x200;
    const uint64_t wide_destination_addr = DMAC_TEST_DEST_ADDR + 0x200;
    const uint64_t fixed_source_addr = DMAC_TEST_SOURCE_ADDR + 0x300;
    const uint64_t fixed_source_destination_addr =
        DMAC_TEST_DEST_ADDR + 0x300;
    const uint64_t fixed_destination_source_addr =
        DMAC_TEST_SOURCE_ADDR + 0x400;
    const uint64_t fixed_destination_addr = DMAC_TEST_DEST_ADDR + 0x400;
    uint8_t destination[WIDE_TRANSFERS * 2];
    uint8_t expected[WIDE_TRANSFERS * 2];
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    /*
     * The Linux DW AXI DMAC driver defines increment as zero and no-change
     * as one.
     */
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CFG, DMAC_CFG_ENABLE);

    /* Source e16, destination e8: BLOCK_TS counts source transfers. */
    memset(destination, 0, sizeof(destination));
    qtest_memwrite(qts, wide_source_addr, wide_source, sizeof(wide_source));
    qtest_memwrite(qts, wide_destination_addr, destination,
                   sizeof(destination));
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_SAR(0),
                  wide_source_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_DAR(0),
                  wide_destination_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_BLOCK_TS(0),
                  WIDE_TRANSFERS - 1);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CTL(0),
                  (uint64_t)DMAC_CTL_WIDTH_16 <<
                  DMAC_CTL_SRC_WIDTH_SHIFT);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CHEN,
                  DMAC_CH_ENABLE(0) | DMAC_CH_ENABLE_WE(0));
    qtest_memread(qts, wide_destination_addr, destination,
                  sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination), wide_source,
                    sizeof(wide_source));
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_SAR(0)), ==,
                    wide_source_addr + sizeof(wide_source));
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_DAR(0)), ==,
                    wide_destination_addr + sizeof(destination));

    /* A fixed source repeats the first byte while its SAR remains stable. */
    memset(destination, 0, FIXED_SOURCE_TRANSFERS);
    memset(expected, fixed_source[0], FIXED_SOURCE_TRANSFERS);
    qtest_memwrite(qts, fixed_source_addr, fixed_source,
                   sizeof(fixed_source));
    qtest_memwrite(qts, fixed_source_destination_addr, destination,
                   FIXED_SOURCE_TRANSFERS);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_SAR(1),
                  fixed_source_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_DAR(1),
                  fixed_source_destination_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_BLOCK_TS(1),
                  FIXED_SOURCE_TRANSFERS - 1);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CTL(1),
                  (uint64_t)DMAC_CTL_INCREMENT_NO_CHANGE <<
                  DMAC_CTL_SRC_INCREMENT_SHIFT);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CHEN,
                  DMAC_CH_ENABLE(1) | DMAC_CH_ENABLE_WE(1));
    qtest_memread(qts, fixed_source_destination_addr, destination,
                  FIXED_SOURCE_TRANSFERS);
    g_assert_cmpmem(destination, FIXED_SOURCE_TRANSFERS, expected,
                    FIXED_SOURCE_TRANSFERS);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_SAR(1)), ==, fixed_source_addr);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_DAR(1)), ==,
                    fixed_source_destination_addr + FIXED_SOURCE_TRANSFERS);

    /*
     * A fixed destination consumes all source bytes and retains only the
     * last.
     */
    memset(destination, 0xa5, FIXED_DESTINATION_TRANSFERS);
    memcpy(expected, destination, FIXED_DESTINATION_TRANSFERS);
    expected[0] = fixed_destination_source[FIXED_DESTINATION_TRANSFERS - 1];
    qtest_memwrite(qts, fixed_destination_source_addr,
                   fixed_destination_source,
                   sizeof(fixed_destination_source));
    qtest_memwrite(qts, fixed_destination_addr, destination,
                   FIXED_DESTINATION_TRANSFERS);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_SAR(2),
                  fixed_destination_source_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_DAR(2),
                  fixed_destination_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_BLOCK_TS(2),
                  FIXED_DESTINATION_TRANSFERS - 1);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CTL(2),
                  (uint64_t)DMAC_CTL_INCREMENT_NO_CHANGE <<
                  DMAC_CTL_DST_INCREMENT_SHIFT);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CHEN,
                  DMAC_CH_ENABLE(2) | DMAC_CH_ENABLE_WE(2));
    qtest_memread(qts, fixed_destination_addr, destination,
                  FIXED_DESTINATION_TRANSFERS);
    g_assert_cmpmem(destination, FIXED_DESTINATION_TRANSFERS, expected,
                    FIXED_DESTINATION_TRANSFERS);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_SAR(2)), ==,
                    fixed_destination_source_addr +
                    FIXED_DESTINATION_TRANSFERS);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_DAR(2)), ==, fixed_destination_addr);

    qtest_quit(qts);
}

static void test_dmac_advertised_widths(void)
{
    enum {
        E64_TRANSFERS = 6,
        E64_BYTES = E64_TRANSFERS * 8,
        E128_TRANSFERS = 257,
        E128_BYTES = E128_TRANSFERS * 16,
    };
    const uint64_t e64_source_addr = DMAC_TEST_SOURCE_ADDR + 0x1000;
    const uint64_t e64_destination_addr = DMAC_TEST_DEST_ADDR + 0x1000;
    const uint64_t e128_source_addr = DMAC_TEST_SOURCE_ADDR + 0x3000;
    const uint64_t e128_destination_addr = DMAC_TEST_DEST_ADDR + 0x3000;
    const uint64_t invalid_source_addr = DMAC_TEST_SOURCE_ADDR + 0x5000;
    const uint64_t invalid_destination_addr = DMAC_TEST_DEST_ADDR + 0x5000;
    uint8_t e64_source[E64_BYTES];
    uint8_t e64_destination[E64_BYTES];
    uint8_t e128_source[E128_BYTES];
    uint8_t e128_destination[E128_BYTES];
    uint8_t invalid_source[32];
    uint8_t invalid_destination[32];
    uint8_t invalid_expected[32];
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (unsigned i = 0; i < E64_BYTES; i++) {
        e64_source[i] = i * 37 + 11;
    }
    for (unsigned i = 0; i < E128_BYTES; i++) {
        e128_source[i] = i * 19 + 7;
    }
    memset(e64_destination, 0, sizeof(e64_destination));
    memset(e128_destination, 0, sizeof(e128_destination));
    memset(invalid_source, 0x3c, sizeof(invalid_source));
    memset(invalid_destination, 0xa5, sizeof(invalid_destination));
    memset(invalid_expected, 0xa5, sizeof(invalid_expected));

    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CFG, DMAC_CFG_ENABLE);

    /* BLOCK_TS counts source transfers, even when the destination is wider. */
    qtest_memwrite(qts, e64_source_addr, e64_source, sizeof(e64_source));
    qtest_memwrite(qts, e64_destination_addr, e64_destination,
                   sizeof(e64_destination));
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_SAR(0), e64_source_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_DAR(0),
                  e64_destination_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_BLOCK_TS(0),
                  E64_TRANSFERS - 1);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CTL(0),
                  (uint64_t)DMAC_CTL_WIDTH_64 <<
                  DMAC_CTL_SRC_WIDTH_SHIFT |
                  (uint64_t)DMAC_CTL_WIDTH_128 <<
                  DMAC_CTL_DST_WIDTH_SHIFT);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CHEN,
                  DMAC_CH_ENABLE(0) | DMAC_CH_ENABLE_WE(0));
    qtest_memread(qts, e64_destination_addr, e64_destination,
                  sizeof(e64_destination));
    g_assert_cmpmem(e64_destination, sizeof(e64_destination), e64_source,
                    sizeof(e64_source));
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_SAR(0)), ==,
                    e64_source_addr + sizeof(e64_source));
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_DAR(0)), ==,
                    e64_destination_addr + sizeof(e64_destination));
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_STATUS(0)), ==,
                    E64_TRANSFERS - 1);

    /* The advertised 128-bit mode must preserve a transfer after 4 KiB. */
    qtest_memwrite(qts, e128_source_addr, e128_source, sizeof(e128_source));
    qtest_memwrite(qts, e128_destination_addr, e128_destination,
                   sizeof(e128_destination));
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_SAR(1), e128_source_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_DAR(1),
                  e128_destination_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_BLOCK_TS(1),
                  E128_TRANSFERS - 1);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CTL(1),
                  (uint64_t)DMAC_CTL_WIDTH_128 <<
                  DMAC_CTL_SRC_WIDTH_SHIFT |
                  (uint64_t)DMAC_CTL_WIDTH_128 <<
                  DMAC_CTL_DST_WIDTH_SHIFT);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CHEN,
                  DMAC_CH_ENABLE(1) | DMAC_CH_ENABLE_WE(1));
    qtest_memread(qts, e128_destination_addr, e128_destination,
                  sizeof(e128_destination));
    g_assert_cmpmem(e128_destination, sizeof(e128_destination), e128_source,
                    sizeof(e128_source));
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_SAR(1)), ==,
                    e128_source_addr + sizeof(e128_source));
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_DAR(1)), ==,
                    e128_destination_addr + sizeof(e128_destination));
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_STATUS(1)), ==,
                    E128_TRANSFERS - 1);

    /* DT code 4 makes the next width code invalid for this controller. */
    qtest_memwrite(qts, invalid_source_addr, invalid_source,
                   sizeof(invalid_source));
    qtest_memwrite(qts, invalid_destination_addr, invalid_destination,
                   sizeof(invalid_destination));
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_SAR(2),
                  invalid_source_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_DAR(2),
                  invalid_destination_addr);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_BLOCK_TS(2), 0);
    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_CTL(2),
                  (uint64_t)DMAC_CTL_WIDTH_256 <<
                  DMAC_CTL_SRC_WIDTH_SHIFT |
                  (uint64_t)DMAC_CTL_WIDTH_256 <<
                  DMAC_CTL_DST_WIDTH_SHIFT);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CH_INTSTATUS_EN(2),
                  DMAC_IRQ_INVALID_ERROR);
    qtest_writel(qts, TH1520_DMAC0_BASE + DMAC_CHEN,
                  DMAC_CH_ENABLE(2) | DMAC_CH_ENABLE_WE(2));
    qtest_memread(qts, invalid_destination_addr, invalid_destination,
                  sizeof(invalid_destination));
    g_assert_cmpmem(invalid_destination, sizeof(invalid_destination),
                    invalid_expected, sizeof(invalid_expected));
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_SAR(2)), ==, invalid_source_addr);
    g_assert_cmphex(qtest_readq(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_DAR(2)), ==, invalid_destination_addr);
    g_assert_cmphex(qtest_readl(qts, TH1520_DMAC0_BASE +
                                DMAC_CH_INTSTATUS(2)), ==,
                    DMAC_IRQ_INVALID_ERROR);

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
    g_assert_cmphex(qtest_readl(qts,
                                controller->base + DWMAC_FRAME_FILTER), ==, 0);
    g_assert_cmphex(qtest_readl(qts, controller->base + DWMAC_HASH_HIGH), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, controller->base + DWMAC_HASH_LOW), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, controller->base + DWMAC_VLAN_TAG), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts,
                                controller->base + DWMAC_VLAN_HASH_TABLE), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts,
                                controller->base + DWMAC_MAC0_ADDR_HI), ==,
                    0x8000ffff);
    g_assert_cmphex(qtest_readl(qts,
                                controller->base + DWMAC_MAC0_ADDR_LO), ==,
                    UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, controller->base +
                                DWMAC_MAC_ADDR_HI(31)), ==, 0x0000ffff);
    g_assert_cmphex(qtest_readl(qts, controller->base +
                                DWMAC_MAC_ADDR_LO(31)), ==, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, controller->base +
                                DWMAC_MAC_ADDR_HI(32)), ==, 0);
    g_assert_cmphex(gmac_mdio_read(qts, controller->base,
                                  TH1520_GMAC_PHY_ADDR, MII_BMCR), ==,
                    MII_BMCR_AUTOEN | MII_BMCR_FD | MII_BMCR_SPEED1000);
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
        qtest_writel(qts, gmac->base + DWMAC_FRAME_FILTER, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, gmac->base + DWMAC_FRAME_FILTER), ==,
                        0x800107ff);
        qtest_writel(qts, gmac->base + DWMAC_VLAN_TAG, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, gmac->base + DWMAC_VLAN_TAG), ==,
                        0x0007ffff);
        qtest_writel(qts, gmac->base + DWMAC_VLAN_HASH_TABLE, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, gmac->base +
                                    DWMAC_VLAN_HASH_TABLE), ==, 0);
        qtest_writel(qts, gmac->base + DWMAC_HASH_HIGH, 0x89abcdef);
        qtest_writel(qts, gmac->base + DWMAC_HASH_LOW, 0x01234567);
        g_assert_cmphex(qtest_readl(qts, gmac->base + DWMAC_HASH_HIGH), ==,
                        0x89abcdef);
        g_assert_cmphex(qtest_readl(qts, gmac->base + DWMAC_HASH_LOW), ==,
                        0x01234567);
        qtest_writel(qts, gmac->base + DWMAC_MAC0_ADDR_HI, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, gmac->base + DWMAC_MAC0_ADDR_HI), ==,
                        0x8000ffff);
        qtest_writel(qts, gmac->base + DWMAC_MAC_ADDR_HI(31), UINT32_MAX);
        qtest_writel(qts, gmac->base + DWMAC_MAC_ADDR_LO(31), 0x12345678);
        g_assert_cmphex(qtest_readl(qts, gmac->base +
                                    DWMAC_MAC_ADDR_HI(31)), ==, 0xff00ffff);
        g_assert_cmphex(qtest_readl(qts, gmac->base +
                                    DWMAC_MAC_ADDR_LO(31)), ==, 0x12345678);
        qtest_writel(qts, gmac->base + DWMAC_MAC_ADDR_HI(32), UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, gmac->base +
                                    DWMAC_MAC_ADDR_HI(32)), ==, 0);
    }

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(th1520_gmac_controllers); i++) {
        assert_gmac_reset_state(qts, &th1520_gmac_controllers[i]);
    }
    qtest_quit(qts);
}

static void gmac_drive_phy_reset(QTestState *qts, bool deasserted)
{
    qtest_writel(qts, TH1520_GPIO3_BASE + DW_GPIO_SWPORTA_DR,
                  deasserted ? BIT(TH1520_GMAC_PHY_RESET_GPIO) : 0);
    qtest_writel(qts, TH1520_GPIO3_BASE + DW_GPIO_SWPORTA_DDR,
                  BIT(TH1520_GMAC_PHY_RESET_GPIO));
}

static void assert_gmac_phy_reset_asserted(QTestState *qts)
{
    uint16_t bmsr = gmac_mdio_read(qts, TH1520_GMAC0_BASE,
                                   TH1520_GMAC_PHY_ADDR, MII_BMSR);

    g_assert_cmphex(gmac_mdio_read(qts, TH1520_GMAC0_BASE,
                                  TH1520_GMAC_PHY_ADDR, MII_BMCR), ==,
                    MII_BMCR_AUTOEN | MII_BMCR_FD | MII_BMCR_SPEED1000);
    g_assert_cmphex(bmsr & (MII_BMSR_LINK_ST | MII_BMSR_AN_COMP), ==, 0);
}

static void test_gmac_phy_gpio(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    /* The generic PHY holds the board's active-low interrupt deasserted. */
    g_assert_true(qtest_readl(qts, TH1520_GPIO3_BASE + DW_GPIO_EXT_PORTA) &
                  BIT(TH1520_GMAC_PHY_IRQ_GPIO));
    assert_gmac_phy_reset_asserted(qts);

    gmac_drive_phy_reset(qts, true);
    gmac_mdio_write(qts, TH1520_GMAC0_BASE, TH1520_GMAC_PHY_ADDR,
                    MII_BMCR, MII_BMCR_AUTOEN | MII_BMCR_FD |
                    MII_BMCR_SPEED100);
    g_assert_cmphex(gmac_mdio_read(qts, TH1520_GMAC0_BASE,
                                  TH1520_GMAC_PHY_ADDR, MII_BMCR), ==,
                    MII_BMCR_AUTOEN | MII_BMCR_FD | MII_BMCR_SPEED100);

    gmac_drive_phy_reset(qts, false);
    assert_gmac_phy_reset_asserted(qts);

    qtest_system_reset(qts);
    g_assert_true(qtest_readl(qts, TH1520_GPIO3_BASE + DW_GPIO_EXT_PORTA) &
                  BIT(TH1520_GMAC_PHY_IRQ_GPIO));
    assert_gmac_phy_reset_asserted(qts);
    qtest_quit(qts);
}

static void test_gmac_phy_gpio_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-gmac-phy-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    gmac_drive_phy_reset(src, true);
    gmac_mdio_write(src, TH1520_GMAC0_BASE, TH1520_GMAC_PHY_ADDR,
                    MII_BMCR, MII_BMCR_AUTOEN | MII_BMCR_FD |
                    MII_BMCR_SPEED100);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_true(qtest_readl(dst, TH1520_GPIO3_BASE + DW_GPIO_EXT_PORTA) &
                  BIT(TH1520_GMAC_PHY_IRQ_GPIO));
    g_assert_cmphex(gmac_mdio_read(dst, TH1520_GMAC0_BASE,
                                  TH1520_GMAC_PHY_ADDR, MII_BMCR), ==,
                    MII_BMCR_AUTOEN | MII_BMCR_FD | MII_BMCR_SPEED100);

    gmac_drive_phy_reset(dst, false);
    assert_gmac_phy_reset_asserted(dst);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
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

/* Independent test oracle; do not use the checksum helper under test. */
static uint32_t gmac_test_checksum_add(uint32_t sum, const uint8_t *buf,
                                       size_t len)
{
    while (len >= 2) {
        sum += ((uint16_t)buf[0] << 8) | buf[1];
        buf += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint16_t)buf[0] << 8;
    }
    return sum;
}

static uint16_t gmac_test_checksum_fold(uint32_t sum)
{
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return sum;
}

static uint16_t gmac_test_checksum_finish(uint32_t sum)
{
    return ~gmac_test_checksum_fold(sum);
}

static QTestState *gmac_packet_test_init_extra(int sockets[2],
                                               const char *extra)
{
    QTestState *qts;

    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, sockets), ==, 0);
    qts = qtest_initf("-machine beaglev-ahead -bios none %s "
                      "-nic socket,fd=%d,model=gmac0", extra, sockets[1]);
    close(sockets[1]);
    return qts;
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

static void gmac_send_two_packets(int fd,
                                  const uint8_t *first, size_t first_len,
                                  const uint8_t *second, size_t second_len)
{
    uint32_t first_wire_len = htonl(first_len);
    uint32_t second_wire_len = htonl(second_len);
    const struct iovec iov[] = {
        { .iov_base = &first_wire_len, .iov_len = sizeof(first_wire_len) },
        { .iov_base = (void *)first, .iov_len = first_len },
        { .iov_base = &second_wire_len, .iov_len = sizeof(second_wire_len) },
        { .iov_base = (void *)second, .iov_len = second_len },
    };
    size_t total = sizeof(first_wire_len) + first_len +
                   sizeof(second_wire_len) + second_len;

    g_assert_cmpint(iov_send(fd, iov, ARRAY_SIZE(iov), 0, total), ==, total);
}

static void gmac_send_packet(int fd, const uint8_t *packet, size_t packet_len)
{
    uint32_t wire_len = htonl(packet_len);
    const struct iovec iov[] = {
        { .iov_base = &wire_len, .iov_len = sizeof(wire_len) },
        { .iov_base = (void *)packet, .iov_len = packet_len },
    };
    size_t total = sizeof(wire_len) + packet_len;

    g_assert_cmpint(iov_send(fd, iov, ARRAY_SIZE(iov), 0, total), ==, total);
}

static int gmac_wait_for_packet(QTestState *qts, uint32_t first_buffer,
                                uint32_t second_buffer,
                                const uint8_t *packet, size_t packet_len)
{
    g_autofree uint8_t *first = g_malloc(packet_len);
    g_autofree uint8_t *second = g_malloc(packet_len);
    gint64 deadline = g_get_monotonic_time() +
                      GMAC_TEST_TIMEOUT_S * G_TIME_SPAN_SECOND;

    do {
        qtest_memread(qts, first_buffer, first, packet_len);
        qtest_memread(qts, second_buffer, second, packet_len);
        if (!memcmp(first, packet, packet_len)) {
            return 0;
        }
        if (!memcmp(second, packet, packet_len)) {
            return 1;
        }
        qtest_clock_step(qts, 1000);
    } while (g_get_monotonic_time() < deadline);

    return -1;
}

static void gmac_wait_rx_desc_complete(QTestState *qts, uint32_t desc_addr,
                                       GMACDesc *desc)
{
    gint64 deadline = g_get_monotonic_time() +
                      GMAC_TEST_TIMEOUT_S * G_TIME_SPAN_SECOND;

    do {
        gmac_read_desc(qts, desc_addr, desc);
        if (!(desc->des0 & BIT(31))) {
            return;
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);

    g_assert_not_reached();
}

static void gmac_prepare_rx_desc(QTestState *qts, uint32_t desc_addr,
                                 uint32_t buffer_addr, bool dic)
{
    GMACDesc desc = {
        .des0 = BIT(31),
        .des1 = (dic ? BIT(31) : 0) | 2048,
        .des2 = buffer_addr,
    };

    gmac_write_desc(qts, desc_addr, &desc);
}

static void gmac_configure_rx_watchdog(QTestState *qts, uint32_t desc_addr,
                                       uint32_t riwt)
{
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_HI,
                  BIT(31) | 0x5634);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_LO, 0x12005452);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                  0x00020100 | BIT(7));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RX_BASE_ADDR, desc_addr);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                  BIT(16) | BIT(6));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RX_WATCHDOG, riwt);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG, BIT(2));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL, BIT(1));
}

typedef struct GMACFilterRegWrite {
    uint32_t offset;
    uint32_t value;
} GMACFilterRegWrite;

typedef struct GMACFilterCase {
    const char *name;
    uint32_t frame_filter;
    const uint8_t *candidate;
    size_t candidate_len;
    const uint8_t *barrier;
    size_t barrier_len;
    bool candidate_accepted;
    uint32_t candidate_status;
    const GMACFilterRegWrite *writes;
    size_t write_count;
} GMACFilterCase;

static void gmac_assert_rx_frame(QTestState *qts, uint32_t desc_addr,
                                 uint32_t buffer_addr, const uint8_t *packet,
                                 size_t packet_len, uint32_t filter_status)
{
    g_autofree uint8_t *actual = g_malloc(packet_len + sizeof(uint32_t));
    uint32_t expected_fcs = cpu_to_le32(gmac_test_crc32(packet, packet_len));
    GMACDesc desc;

    gmac_read_desc(qts, desc_addr, &desc);
    g_assert_cmphex(desc.des0 & BIT(31), ==, 0);
    g_assert_cmphex(desc.des0 & (BIT(9) | BIT(8)), ==, BIT(9) | BIT(8));
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==,
                     packet_len + sizeof(expected_fcs));
    g_assert_cmphex(desc.des0 & (BIT(30) | BIT(13) | BIT(10)), ==,
                    filter_status);
    qtest_memread(qts, buffer_addr, actual,
                  packet_len + sizeof(expected_fcs));
    g_assert_cmpmem(actual, packet_len, packet, packet_len);
    g_assert_cmpmem(actual + packet_len, sizeof(expected_fcs),
                    &expected_fcs, sizeof(expected_fcs));
}

static void gmac_run_filter_case(const GMACFilterCase *test)
{
    GMACDesc first = {
        .des0 = BIT(31),
        .des1 = BIT(31) | 2048,
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    GMACDesc second = {
        .des0 = BIT(31),
        .des1 = 2048,
        .des2 = GMAC_TEST_DATA2_ADDR,
    };
    QTestState *qts;
    int sockets[2];
    int barrier_slot;

    g_test_message("GMAC filter case: %s", test->name);
    qts = gmac_packet_test_init(sockets);
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 8192);
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &first);
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                    &second);
    second = (GMACDesc) { 0 };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + 2 * GMAC_ENHANCED_DESC_STRIDE,
                    &second);

    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_HI,
                  BIT(31) | 0x5634);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_LO, 0x12000002);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_FRAME_FILTER,
                  test->frame_filter);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                  0x00020100 | BIT(7));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RX_BASE_ADDR,
                  GMAC_TEST_DESC_ADDR);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                  BIT(16) | BIT(6));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG, BIT(2));
    for (size_t i = 0; i < test->write_count; i++) {
        qtest_writel(qts, TH1520_GMAC0_BASE + test->writes[i].offset,
                      test->writes[i].value);
    }
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL, BIT(1));

    gmac_send_two_packets(sockets[0], test->candidate, test->candidate_len,
                          test->barrier, test->barrier_len);
    barrier_slot = gmac_wait_for_packet(qts, GMAC_TEST_DATA_ADDR,
                                        GMAC_TEST_DATA2_ADDR, test->barrier,
                                        test->barrier_len);
    g_assert_cmpint(barrier_slot, ==, test->candidate_accepted ? 1 : 0);

    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &first);
    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                   &second);
    if (test->candidate_accepted) {
        gmac_assert_rx_frame(qts, GMAC_TEST_DESC_ADDR,
                             GMAC_TEST_DATA_ADDR, test->candidate,
                             test->candidate_len, test->candidate_status);
        gmac_assert_rx_frame(qts,
                             GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                             GMAC_TEST_DATA2_ADDR, test->barrier,
                             test->barrier_len, 0);
        g_assert_cmphex(second.des0 & BIT(31), ==, 0);
    } else {
        uint8_t untouched;

        gmac_assert_rx_frame(qts, GMAC_TEST_DESC_ADDR,
                             GMAC_TEST_DATA_ADDR, test->barrier,
                             test->barrier_len, 0);
        g_assert_cmphex(second.des0 & BIT(31), ==, BIT(31));
        qtest_memread(qts, GMAC_TEST_DATA2_ADDR, &untouched,
                      sizeof(untouched));
        g_assert_cmphex(untouched, ==, 0xa5);
    }
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_HOST_RX_DESC),
                    ==, GMAC_TEST_DESC_ADDR +
                        (test->candidate_accepted ? 2 : 1) *
                        GMAC_ENHANCED_DESC_STRIDE);
    g_assert_cmphex(qtest_readl(qts, TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    (BIT(16) | BIT(7) | BIT(6)), ==,
                    test->candidate_accepted ? BIT(16) | BIT(6) : 0);

    qtest_quit(qts);
    close(sockets[0]);
}

static void test_gmac_rx_filter_perfect(void)
{
    static const uint8_t rejected[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x57,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00,
    };
    static const uint8_t accepted[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x5a,
    };
    const GMACFilterCase test = {
        .name = "primary-perfect-drop",
        .frame_filter = BIT(10),
        .candidate = rejected,
        .candidate_len = sizeof(rejected),
        .barrier = accepted,
        .barrier_len = sizeof(accepted),
    };

    /* The accepted second frame proves that a drop did not consume RX DMA. */
    gmac_run_filter_case(&test);
}

static void test_gmac_rx_filter_matrix(void)
{
    static const uint8_t own[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x01,
    };
    static const uint8_t own_barrier[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x5a,
    };
    static const uint8_t foreign[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x57,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x02,
    };
    static const uint8_t foreign2[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x58,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x03,
    };
    static const uint8_t broadcast[64] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x04,
    };
    static const uint8_t multicast[64] = {
        0x01, 0x00, 0x5e, 0x00, 0x00, 0x01,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x05,
    };
    static const uint8_t additional[64] = {
        0x02, 0x00, 0x00, 0xaa, 0xbb, 0xcc,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x06,
    };
    static const uint8_t source_miss[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x22,
        0x08, 0x00, 0x45, 0x00, 0x07,
    };
    static const uint8_t foreign_source_miss[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x57,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x22,
        0x08, 0x00, 0x45, 0x00, 0x08,
    };
    static const uint8_t control_foreign[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x57,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x88, 0x08, 0x00, 0x02, 0x09,
    };
    static const uint8_t control_own[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x88, 0x08, 0x00, 0x02, 0x0a,
    };
    static const uint8_t control_broadcast[64] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x88, 0x08, 0x00, 0x02, 0x0b,
    };
    static const uint8_t pause_multicast[64] = {
        0x01, 0x80, 0xc2, 0x00, 0x00, 0x01,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x88, 0x08, 0x00, 0x01, 0x0c,
    };
    /* DWC GMAC manual hash examples: indexes 0x2c and 0x07. */
    static const uint8_t hash_multicast[64] = {
        0x1f, 0x52, 0x41, 0x9c, 0xb6, 0xaf,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x0c,
    };
    static const uint8_t hash_multicast_barrier[64] = {
        0x1f, 0x52, 0x41, 0x9c, 0xb6, 0xaf,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x5c,
    };
    static const uint8_t hash_multicast_miss[64] = {
        0x01, 0x00, 0x5e, 0x00, 0x00, 0x02,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x0d,
    };
    static const uint8_t hash_unicast[64] = {
        0xa0, 0x0a, 0x98, 0x00, 0x00, 0x45,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x0e,
    };
    static const uint8_t hash_unicast_barrier[64] = {
        0xa0, 0x0a, 0x98, 0x00, 0x00, 0x45,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x5e,
    };
    static const uint8_t hash_unicast_miss[64] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x02,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x0f,
    };
    static const uint8_t vlan_match[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x81, 0x00, 0x01, 0x23, 0x08, 0x00, 0x10,
    };
    static const uint8_t vlan_miss[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x81, 0x00, 0x01, 0x24, 0x08, 0x00, 0x11,
    };
    static const uint8_t vlan_pcp[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x81, 0x00, 0xa1, 0x23, 0x08, 0x00, 0x12,
    };
    static const uint8_t svlan_match[64] = {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x88, 0xa8, 0x01, 0x23, 0x08, 0x00, 0x13,
    };
    static const uint8_t short_frame[] = { 0x02, 0x00, 0x00, 0x12, 0x34 };
    static const GMACFilterRegWrite address1[] = {
        { DWMAC_MAC_ADDR_HI(1), BIT(31) | 0xccbb },
        { DWMAC_MAC_ADDR_LO(1), 0xaa000002 },
    };
    static const GMACFilterRegWrite address1_disabled[] = {
        { DWMAC_MAC_ADDR_HI(1), 0xccbb },
        { DWMAC_MAC_ADDR_LO(1), 0xaa000002 },
    };
    static const GMACFilterRegWrite address31[] = {
        { DWMAC_MAC_ADDR_HI(31), BIT(31) | 0xccbb },
        { DWMAC_MAC_ADDR_LO(31), 0xaa000002 },
    };
    static const GMACFilterRegWrite address1_masked[] = {
        { DWMAC_MAC_ADDR_HI(1), BIT(31) | BIT(29) | 0xddbb },
        { DWMAC_MAC_ADDR_LO(1), 0xaa000002 },
    };
    static const GMACFilterRegWrite source1[] = {
        { DWMAC_MAC_ADDR_HI(1), BIT(31) | BIT(30) | 0x2143 },
        { DWMAC_MAC_ADDR_LO(1), 0x65000002 },
    };
    static const GMACFilterRegWrite multicast_hash[] = {
        { DWMAC_HASH_HIGH, BIT(12) },
    };
    static const GMACFilterRegWrite unicast_hash[] = {
        { DWMAC_HASH_LOW, BIT(7) },
    };
    static const GMACFilterRegWrite vlan_tag[] = {
        { DWMAC_VLAN_TAG, 0x0123 },
    };
    static const GMACFilterRegWrite vlan_tag_etv[] = {
        { DWMAC_VLAN_TAG, BIT(16) | 0x0123 },
    };
    static const GMACFilterRegWrite vlan_tag_inverse[] = {
        { DWMAC_VLAN_TAG, BIT(17) | 0x0123 },
    };
    static const GMACFilterRegWrite svlan_tag[] = {
        { DWMAC_VLAN_TAG, BIT(18) | 0x0123 },
    };
    static const GMACFilterRegWrite pause_processing[] = {
        { DWMAC_MAC_CONFIG, BIT(11) | BIT(2) },
        { DWMAC_FLOW_CTRL, BIT(2) },
    };
    static const GMACFilterCase cases[] = {
        { "primary-perfect-accept", 0, own, sizeof(own),
          own_barrier, sizeof(own_barrier), true },
        { "broadcast-default", 0, broadcast, sizeof(broadcast),
          own_barrier, sizeof(own_barrier), true },
        { "broadcast-dbf-absolute", BIT(31) | BIT(7) | BIT(5) | BIT(0),
          control_broadcast, sizeof(control_broadcast), own_barrier,
          sizeof(own_barrier), false },
        { "multicast-perfect-miss", 0, multicast, sizeof(multicast),
          own_barrier, sizeof(own_barrier), false },
        { "multicast-pass-all", BIT(4), multicast, sizeof(multicast),
          own_barrier, sizeof(own_barrier), true },
        { "promiscuous-clears-fail", BIT(9) | BIT(0), foreign_source_miss,
          sizeof(foreign_source_miss), own_barrier, sizeof(own_barrier), true,
          0, source1, ARRAY_SIZE(source1) },
        { "receive-all-preserves-fail", BIT(31) | BIT(9),
          foreign_source_miss, sizeof(foreign_source_miss), own_barrier,
          sizeof(own_barrier), true, BIT(30) | BIT(13), source1,
          ARRAY_SIZE(source1) },
        { "destination-inverse-own", BIT(3), own, sizeof(own), foreign2,
          sizeof(foreign2), false },
        { "destination-inverse-foreign", BIT(3), foreign, sizeof(foreign),
          foreign2, sizeof(foreign2), true },
        { "address1-disabled", 0, additional, sizeof(additional),
          own_barrier, sizeof(own_barrier), false, 0, address1_disabled,
          ARRAY_SIZE(address1_disabled) },
        { "address1-perfect", 0, additional, sizeof(additional),
          own_barrier, sizeof(own_barrier), true, 0, address1,
          ARRAY_SIZE(address1) },
        { "address31-perfect", 0, additional, sizeof(additional),
          own_barrier, sizeof(own_barrier), true, 0, address31,
          ARRAY_SIZE(address31) },
        { "address-byte-mask", 0, additional, sizeof(additional),
          own_barrier, sizeof(own_barrier), true, 0, address1_masked,
          ARRAY_SIZE(address1_masked) },
        { "multicast-hash-hit", BIT(2), hash_multicast,
          sizeof(hash_multicast), hash_multicast_barrier,
          sizeof(hash_multicast_barrier), true, 0, multicast_hash,
          ARRAY_SIZE(multicast_hash) },
        { "multicast-hash-miss", BIT(2), hash_multicast_miss,
          sizeof(hash_multicast_miss), hash_multicast_barrier,
          sizeof(hash_multicast_barrier), false, 0, multicast_hash,
          ARRAY_SIZE(multicast_hash) },
        { "unicast-hash-hit", BIT(1), hash_unicast, sizeof(hash_unicast),
          hash_unicast_barrier, sizeof(hash_unicast_barrier), true, 0,
          unicast_hash, ARRAY_SIZE(unicast_hash) },
        { "unicast-hash-miss", BIT(1), hash_unicast_miss,
          sizeof(hash_unicast_miss), hash_unicast_barrier,
          sizeof(hash_unicast_barrier), false, 0, unicast_hash,
          ARRAY_SIZE(unicast_hash) },
        { "hash-or-perfect", BIT(10) | BIT(1), own, sizeof(own),
          own_barrier, sizeof(own_barrier), true },
        { "source-filter-miss", BIT(9), source_miss, sizeof(source_miss),
          own_barrier, sizeof(own_barrier), false, 0, source1,
          ARRAY_SIZE(source1) },
        { "source-inverse-match", BIT(9) | BIT(8), own, sizeof(own),
          source_miss, sizeof(source_miss), false, 0, source1,
          ARRAY_SIZE(source1) },
        { "source-status-without-drop", 0, source_miss,
          sizeof(source_miss), own_barrier, sizeof(own_barrier), true,
          BIT(13), source1, ARRAY_SIZE(source1) },
        { "control-mode-zero", 0, control_foreign,
          sizeof(control_foreign), own_barrier, sizeof(own_barrier), false },
        { "control-mode-one", BIT(6), control_foreign,
          sizeof(control_foreign), own_barrier, sizeof(own_barrier), true,
          BIT(30) },
        { "control-mode-one-processed-pause", BIT(6), pause_multicast,
          sizeof(pause_multicast), own_barrier, sizeof(own_barrier), false,
          0, pause_processing, ARRAY_SIZE(pause_processing) },
        { "control-mode-two", BIT(7), control_foreign,
          sizeof(control_foreign), own_barrier, sizeof(own_barrier), true,
          BIT(30) },
        { "control-mode-three-fail", BIT(7) | BIT(6), control_foreign,
          sizeof(control_foreign), own_barrier, sizeof(own_barrier), false },
        { "control-mode-three-pass", BIT(7) | BIT(6), control_own,
          sizeof(control_own), own_barrier, sizeof(own_barrier), true },
        { "vlan-perfect", BIT(16), vlan_match, sizeof(vlan_match),
          own_barrier, sizeof(own_barrier), true, BIT(10), vlan_tag,
          ARRAY_SIZE(vlan_tag) },
        { "vlan-perfect-miss", BIT(16), vlan_miss, sizeof(vlan_miss),
          own_barrier, sizeof(own_barrier), false, 0, vlan_tag,
          ARRAY_SIZE(vlan_tag) },
        { "vlan-vid-only", BIT(16), vlan_pcp, sizeof(vlan_pcp),
          own_barrier, sizeof(own_barrier), true, BIT(10), vlan_tag_etv,
          ARRAY_SIZE(vlan_tag_etv) },
        { "vlan-inverse-match", BIT(16), vlan_match, sizeof(vlan_match),
          own_barrier, sizeof(own_barrier), false, 0, vlan_tag_inverse,
          ARRAY_SIZE(vlan_tag_inverse) },
        { "vlan-inverse-miss", BIT(16), vlan_miss, sizeof(vlan_miss),
          own_barrier, sizeof(own_barrier), true, BIT(10), vlan_tag_inverse,
          ARRAY_SIZE(vlan_tag_inverse) },
        { "svlan-perfect", BIT(16), svlan_match, sizeof(svlan_match),
          own_barrier, sizeof(own_barrier), true, BIT(10), svlan_tag,
          ARRAY_SIZE(svlan_tag) },
        { "short-frame-bounds", 0, short_frame, sizeof(short_frame),
          own_barrier, sizeof(own_barrier), false },
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
        gmac_run_filter_case(&cases[i]);
    }
}

static const uint8_t gmac_ipv4_udp_packet[64] = {
    0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
    0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
    0x08, 0x00,
    0x45, 0x00, 0x00, 0x32, 0x12, 0x34, 0x40, 0x00,
    0x40, 0x11, 0x3c, 0x50, 0xc0, 0x00, 0x02, 0x01,
    0xc6, 0x33, 0x64, 0x02,
    0x04, 0xd2, 0x16, 0x2e, 0x00, 0x1e, 0x7e, 0xf6,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
};

static const uint8_t gmac_ipv6_tcp_packet[74] = {
    0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
    0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
    0x86, 0xdd,
    0x60, 0x00, 0x00, 0x00, 0x00, 0x14, 0x06, 0x40,
    0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x04, 0xd2, 0x16, 0x2e, 0x01, 0x02, 0x03, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x50, 0x02, 0x40, 0x00,
    0xf5, 0x67, 0x00, 0x00,
};

static const uint32_t gmac_rx_extension[4] = {
    0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00,
};

typedef enum GMACRxCOEPacket {
    GMAC_RX_COE_IPV4_UDP,
    GMAC_RX_COE_IPV4_HEADER_BAD,
    GMAC_RX_COE_IPV4_UDP_BAD,
    GMAC_RX_COE_IPV6_TCP,
    GMAC_RX_COE_IPV6_EXT_TRUNCATED,
    GMAC_RX_COE_IPV4_FRAGMENT,
    GMAC_RX_COE_NON_IP,
    GMAC_RX_COE_VLAN_IPV4_UDP,
    GMAC_RX_COE_SVLAN_IPV4_UDP,
    GMAC_RX_COE_TRUNCATED_VLAN,
} GMACRxCOEPacket;

typedef struct GMACRxCOECase {
    const char *name;
    GMACRxCOEPacket packet;
    bool ipc;
    bool atds;
    bool status_available;
    bool error_summary;
    uint32_t rdes4;
    uint32_t vlan_tag;
} GMACRxCOECase;

static void gmac_write_rx_extension(QTestState *qts, uint32_t desc_addr,
                                    const uint32_t extension[4])
{
    uint32_t le_extension[4];

    for (size_t i = 0; i < ARRAY_SIZE(le_extension); i++) {
        le_extension[i] = cpu_to_le32(extension[i]);
    }
    qtest_memwrite(qts, desc_addr + sizeof(GMACDesc), le_extension,
                   sizeof(le_extension));
}

static void gmac_read_rx_extension(QTestState *qts, uint32_t desc_addr,
                                   uint32_t extension[4])
{
    qtest_memread(qts, desc_addr + sizeof(GMACDesc), extension,
                  4 * sizeof(*extension));
    for (size_t i = 0; i < 4; i++) {
        extension[i] = le32_to_cpu(extension[i]);
    }
}

static size_t gmac_make_rx_coe_packet(GMACRxCOEPacket kind, uint8_t *packet)
{
    size_t packet_len;

    if (kind == GMAC_RX_COE_IPV6_TCP) {
        memcpy(packet, gmac_ipv6_tcp_packet, sizeof(gmac_ipv6_tcp_packet));
        return sizeof(gmac_ipv6_tcp_packet);
    }
    if (kind == GMAC_RX_COE_IPV6_EXT_TRUNCATED) {
        /* The 8-byte payload's Routing header claims a 16-byte extent. */
        memcpy(packet, gmac_ipv6_tcp_packet, 54);
        packet[18] = 0x00;
        packet[19] = 0x08;
        packet[20] = 43;
        packet[54] = 6;
        packet[55] = 1;
        memset(packet + 56, 0, 6);
        return 62;
    }
    if (kind == GMAC_RX_COE_VLAN_IPV4_UDP) {
        memcpy(packet, gmac_ipv4_udp_packet, 12);
        packet[12] = 0x81;
        packet[13] = 0x00;
        packet[14] = 0x01;
        packet[15] = 0x23;
        packet[16] = 0x08;
        packet[17] = 0x00;
        memcpy(packet + 18, gmac_ipv4_udp_packet + 14,
               sizeof(gmac_ipv4_udp_packet) - 14);
        return sizeof(gmac_ipv4_udp_packet) + 4;
    }
    if (kind == GMAC_RX_COE_SVLAN_IPV4_UDP) {
        memcpy(packet, gmac_ipv4_udp_packet, 12);
        packet[12] = 0x88;
        packet[13] = 0xa8;
        packet[14] = 0x01;
        packet[15] = 0x23;
        packet[16] = 0x08;
        packet[17] = 0x00;
        memcpy(packet + 18, gmac_ipv4_udp_packet + 14,
               sizeof(gmac_ipv4_udp_packet) - 14);
        return sizeof(gmac_ipv4_udp_packet) + 4;
    }
    if (kind == GMAC_RX_COE_TRUNCATED_VLAN) {
        memcpy(packet, gmac_ipv4_udp_packet, 12);
        packet[12] = 0x81;
        packet[13] = 0x00;
        packet[14] = 0x01;
        packet[15] = 0x23;
        return 16;
    }

    memcpy(packet, gmac_ipv4_udp_packet, sizeof(gmac_ipv4_udp_packet));
    packet_len = sizeof(gmac_ipv4_udp_packet);
    switch (kind) {
    case GMAC_RX_COE_IPV4_UDP:
        break;
    case GMAC_RX_COE_IPV4_HEADER_BAD:
        packet[24] ^= 1;
        break;
    case GMAC_RX_COE_IPV4_UDP_BAD:
        packet[40] ^= 1;
        break;
    case GMAC_RX_COE_IPV4_FRAGMENT:
        packet[20] = 0x20;
        packet[24] = 0x5c;
        break;
    case GMAC_RX_COE_NON_IP:
        packet[12] = 0x88;
        packet[13] = 0xb5;
        break;
    case GMAC_RX_COE_IPV6_TCP:
    case GMAC_RX_COE_IPV6_EXT_TRUNCATED:
    case GMAC_RX_COE_VLAN_IPV4_UDP:
    case GMAC_RX_COE_SVLAN_IPV4_UDP:
    case GMAC_RX_COE_TRUNCATED_VLAN:
        g_assert_not_reached();
    }
    return packet_len;
}

static void gmac_run_rx_coe_case(const GMACRxCOECase *test)
{
    uint8_t packet[sizeof(gmac_ipv6_tcp_packet)];
    uint32_t actual_extension[4];
    GMACDesc desc = {
        .des0 = BIT(31),
        .des1 = 2048,
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    uint32_t stride = test->atds ? GMAC_ENHANCED_DESC_STRIDE :
                                   sizeof(desc);
    size_t packet_len = gmac_make_rx_coe_packet(test->packet, packet);
    QTestState *qts;
    int sockets[2];
    uint32_t expected_rdes0;

    g_test_message("GMAC RX checksum case: %s", test->name);
    qts = gmac_packet_test_init(sockets);
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    gmac_write_rx_extension(qts, GMAC_TEST_DESC_ADDR, gmac_rx_extension);
    if (test->atds) {
        desc = (GMACDesc) { 0 };
        gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + stride, &desc);
    }

    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_HI,
                  BIT(31) | 0x5634);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_LO, 0x12005452);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                  0x00020100 | (test->atds ? BIT(7) : 0));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RX_BASE_ADDR,
                  GMAC_TEST_DESC_ADDR);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                  BIT(16) | BIT(6));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_VLAN_TAG,
                  test->vlan_tag);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG,
                  (test->ipc ? DWMAC_MAC_CONFIG_IPC : 0) | BIT(2));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL, BIT(1));

    gmac_send_packet(sockets[0], packet, packet_len);
    g_assert_true(gmac_wait_status(qts, BIT(6)));

    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0 & BIT(31), ==, 0);
    g_assert_cmphex(desc.des0 & (BIT(9) | BIT(8)), ==, BIT(9) | BIT(8));
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==,
                     packet_len + sizeof(uint32_t));
    expected_rdes0 = DWMAC_RX_DESC_FT |
                     (test->status_available ? DWMAC_RX_DESC_ESA : 0) |
                     (test->error_summary ? DWMAC_RX_DESC_ES : 0);
    g_assert_cmphex(desc.des0 &
                    (DWMAC_RX_DESC_ES | DWMAC_RX_DESC_FT |
                     DWMAC_RX_DESC_ESA), ==, expected_rdes0);
    gmac_read_rx_extension(qts, GMAC_TEST_DESC_ADDR, actual_extension);
    g_assert_cmphex(actual_extension[0], ==,
                    test->status_available ? test->rdes4 :
                                             gmac_rx_extension[0]);
    for (size_t i = 1; i < ARRAY_SIZE(gmac_rx_extension); i++) {
        g_assert_cmphex(actual_extension[i], ==, gmac_rx_extension[i]);
    }
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_HOST_RX_DESC),
                    ==, GMAC_TEST_DESC_ADDR + stride);

    qtest_quit(qts);
    close(sockets[0]);
}

static void test_gmac_rx_checksum_type2(void)
{
    static const GMACRxCOECase cases[] = {
        { "valid-ipv4-udp", GMAC_RX_COE_IPV4_UDP, true, true, true, false,
          DWMAC_RX_DESC4_IPV4 | DWMAC_RX_DESC4_UDP },
        { "bad-ipv4-header", GMAC_RX_COE_IPV4_HEADER_BAD, true, true, true,
          true, DWMAC_RX_DESC4_IPV4 | DWMAC_RX_DESC4_IPHE },
        { "bad-ipv4-udp", GMAC_RX_COE_IPV4_UDP_BAD, true, true, true, true,
          DWMAC_RX_DESC4_IPV4 | DWMAC_RX_DESC4_IPPE |
          DWMAC_RX_DESC4_UDP },
        { "valid-ipv6-tcp", GMAC_RX_COE_IPV6_TCP, true, true, true, false,
          DWMAC_RX_DESC4_IPV6 | DWMAC_RX_DESC4_TCP },
        { "truncated-ipv6-extension", GMAC_RX_COE_IPV6_EXT_TRUNCATED,
          true, true, true, true,
          DWMAC_RX_DESC4_IPV6 | DWMAC_RX_DESC4_IPHE },
        { "fragmented-ipv4", GMAC_RX_COE_IPV4_FRAGMENT, true, true, true,
          false, DWMAC_RX_DESC4_IPV4 | DWMAC_RX_DESC4_BYPASS },
        { "non-ip", GMAC_RX_COE_NON_IP, true, true, true, false,
          DWMAC_RX_DESC4_BYPASS },
        { "vlan-ipv4-udp", GMAC_RX_COE_VLAN_IPV4_UDP,
          true, true, true, false,
          DWMAC_RX_DESC4_IPV4 | DWMAC_RX_DESC4_UDP },
        { "svlan-esvl-clear", GMAC_RX_COE_SVLAN_IPV4_UDP,
          true, true, true, false, DWMAC_RX_DESC4_BYPASS },
        { "svlan-esvl-set", GMAC_RX_COE_SVLAN_IPV4_UDP,
          true, true, true, false,
          DWMAC_RX_DESC4_IPV4 | DWMAC_RX_DESC4_UDP,
          DWMAC_VLAN_TAG_ESVL },
        { "truncated-vlan", GMAC_RX_COE_TRUNCATED_VLAN,
          true, true, true, false, DWMAC_RX_DESC4_BYPASS },
        { "ipc-disabled", GMAC_RX_COE_IPV4_UDP, false, true, false, false,
          0 },
        { "atds-disabled", GMAC_RX_COE_IPV4_UDP, true, false, false, false,
          0 },
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
        gmac_run_rx_coe_case(&cases[i]);
    }
}

static void test_gmac_rx_checksum_type2_split(void)
{
    uint32_t first_extension[4];
    uint32_t second_extension[4];
    GMACDesc first = {
        .des0 = BIT(31),
        .des1 = 32,
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    GMACDesc second = {
        .des0 = BIT(31),
        .des1 = 2048,
        .des2 = GMAC_TEST_DATA2_ADDR,
    };
    QTestState *qts;
    int sockets[2];

    qts = gmac_packet_test_init(sockets);
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &first);
    gmac_write_rx_extension(qts, GMAC_TEST_DESC_ADDR, gmac_rx_extension);
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                    &second);
    gmac_write_rx_extension(qts,
                            GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                            gmac_rx_extension);

    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_HI,
                  BIT(31) | 0x5634);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_LO, 0x12005452);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                  0x00020100 | BIT(7));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RX_BASE_ADDR,
                  GMAC_TEST_DESC_ADDR);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                  BIT(16) | BIT(6));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG,
                  DWMAC_MAC_CONFIG_IPC | BIT(2));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL, BIT(1));

    gmac_send_packet(sockets[0], gmac_ipv4_udp_packet,
                     sizeof(gmac_ipv4_udp_packet));
    g_assert_true(gmac_wait_status(qts, BIT(6)));

    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &first);
    g_assert_cmphex(first.des0 &
                    (BIT(31) | DWMAC_RX_DESC_ES | BIT(9) | BIT(8) |
                     DWMAC_RX_DESC_FT | DWMAC_RX_DESC_ESA), ==,
                    BIT(9) | DWMAC_RX_DESC_FT);
    gmac_read_rx_extension(qts, GMAC_TEST_DESC_ADDR, first_extension);
    g_assert_cmpmem(first_extension, sizeof(first_extension),
                    gmac_rx_extension, sizeof(gmac_rx_extension));

    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                   &second);
    g_assert_cmphex(second.des0 &
                    (BIT(31) | DWMAC_RX_DESC_ES | BIT(9) | BIT(8) |
                     DWMAC_RX_DESC_FT | DWMAC_RX_DESC_ESA), ==,
                    BIT(8) | DWMAC_RX_DESC_FT | DWMAC_RX_DESC_ESA);
    g_assert_cmpuint(extract32(second.des0, 16, 14), ==,
                     sizeof(gmac_ipv4_udp_packet) + sizeof(uint32_t));
    gmac_read_rx_extension(qts,
                           GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                           second_extension);
    g_assert_cmphex(second_extension[0], ==,
                    DWMAC_RX_DESC4_IPV4 | DWMAC_RX_DESC4_UDP);
    for (size_t i = 1; i < ARRAY_SIZE(gmac_rx_extension); i++) {
        g_assert_cmphex(second_extension[i], ==, gmac_rx_extension[i]);
    }
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_HOST_RX_DESC),
                    ==, GMAC_TEST_DESC_ADDR +
                        2 * GMAC_ENHANCED_DESC_STRIDE);

    qtest_quit(qts);
    close(sockets[0]);
}

typedef enum GMACTxChecksumL4 {
    GMAC_TX_CHECKSUM_UDP,
    GMAC_TX_CHECKSUM_UDP_ZERO_RESULT,
    GMAC_TX_CHECKSUM_TCP,
    GMAC_TX_CHECKSUM_ICMP,
} GMACTxChecksumL4;

typedef enum GMACTxChecksumVLAN {
    GMAC_TX_CHECKSUM_NO_VLAN,
    GMAC_TX_CHECKSUM_C_VLAN,
    GMAC_TX_CHECKSUM_S_VLAN,
} GMACTxChecksumVLAN;

typedef enum GMACTxChecksumError {
    GMAC_TX_CHECKSUM_VALID,
    GMAC_TX_CHECKSUM_BAD_IP_HEADER,
    GMAC_TX_CHECKSUM_BAD_IPV6_HEADER,
    GMAC_TX_CHECKSUM_BAD_PAYLOAD_LENGTH,
    GMAC_TX_CHECKSUM_TRAILING_STUFF,
    GMAC_TX_CHECKSUM_UDP_LENGTH_MISMATCH,
} GMACTxChecksumError;

typedef struct GMACTxChecksumCase {
    const char *name;
    unsigned cic;
    bool ipv6;
    GMACTxChecksumVLAN vlan;
    /* IPv4 options or a single IPv6 hop-by-hop extension header. */
    bool extended_header;
    GMACTxChecksumL4 l4;
    GMACTxChecksumError error;
    bool split;
    bool tsf;
    uint32_t status;
} GMACTxChecksumCase;

typedef struct GMACTxChecksumPacket {
    bool ipv6;
    GMACTxChecksumL4 l4;
    uint8_t protocol;
    size_t ip_offset;
    size_t ip_header_len;
    size_t l4_offset;
    size_t l4_len;
    size_t l4_checksum_offset;
} GMACTxChecksumPacket;

static uint8_t gmac_tx_checksum_protocol(GMACTxChecksumL4 l4, bool ipv6)
{
    switch (l4) {
    case GMAC_TX_CHECKSUM_UDP:
    case GMAC_TX_CHECKSUM_UDP_ZERO_RESULT:
        return 17;
    case GMAC_TX_CHECKSUM_TCP:
        return 6;
    case GMAC_TX_CHECKSUM_ICMP:
        return ipv6 ? 58 : 1;
    }
    g_assert_not_reached();
}

static uint32_t gmac_tx_checksum_pseudo_sum(
    const uint8_t *packet, const GMACTxChecksumPacket *info,
    size_t pseudo_l4_len)
{
    const uint8_t *ip = packet + info->ip_offset;
    uint32_t sum = 0;

    if (info->l4 == GMAC_TX_CHECKSUM_ICMP && !info->ipv6) {
        return 0;
    }
    if (info->ipv6) {
        sum = gmac_test_checksum_add(sum, ip + 8, 32);
        sum += (pseudo_l4_len >> 16) & 0xffff;
        sum += pseudo_l4_len & 0xffff;
    } else {
        sum = gmac_test_checksum_add(sum, ip + 12, 8);
        sum += pseudo_l4_len;
    }
    sum += info->protocol;
    return sum;
}

static size_t gmac_make_tx_checksum_packet(const GMACTxChecksumCase *test,
                                           uint8_t *packet,
                                           GMACTxChecksumPacket *info)
{
    static const uint8_t ethernet_addresses[12] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
    };
    static const uint8_t ipv6_source[16] = {
        0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 1,
    };
    static const uint8_t ipv6_destination[16] = {
        0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 2,
    };
    const size_t payload_len = 7;
    size_t l2_len = test->vlan != GMAC_TX_CHECKSUM_NO_VLAN ? 18 : 14;
    size_t l4_header_len;
    uint16_t ethertype = test->ipv6 ? 0x86dd : 0x0800;
    uint8_t *ip;
    uint8_t *l4;

    memset(packet, 0, 160);
    memcpy(packet, ethernet_addresses, sizeof(ethernet_addresses));
    if (test->vlan != GMAC_TX_CHECKSUM_NO_VLAN) {
        stw_be_p(packet + 12,
                 test->vlan == GMAC_TX_CHECKSUM_S_VLAN ? 0x88a8 : 0x8100);
        stw_be_p(packet + 14, 0x0123);
        stw_be_p(packet + 16, ethertype);
    } else {
        stw_be_p(packet + 12, ethertype);
    }

    switch (test->l4) {
    case GMAC_TX_CHECKSUM_UDP:
    case GMAC_TX_CHECKSUM_UDP_ZERO_RESULT:
    case GMAC_TX_CHECKSUM_ICMP:
        l4_header_len = 8;
        break;
    case GMAC_TX_CHECKSUM_TCP:
        l4_header_len = 20;
        break;
    default:
        g_assert_not_reached();
    }

    *info = (GMACTxChecksumPacket) {
        .ipv6 = test->ipv6,
        .l4 = test->l4,
        .protocol = gmac_tx_checksum_protocol(test->l4, test->ipv6),
        .ip_offset = l2_len,
        .ip_header_len = test->ipv6 ?
                         40 + (test->extended_header ? 8 : 0) :
                         (test->extended_header ? 24 : 20),
        .l4_len = l4_header_len + payload_len,
    };
    info->l4_offset = info->ip_offset + info->ip_header_len;
    ip = packet + info->ip_offset;
    l4 = packet + info->l4_offset;

    if (test->ipv6) {
        ip[0] = 0x60;
        stw_be_p(ip + 4, info->l4_len +
                         (test->extended_header ? 8 : 0));
        ip[6] = test->extended_header ? 0 : info->protocol;
        ip[7] = 64;
        memcpy(ip + 8, ipv6_source, sizeof(ipv6_source));
        memcpy(ip + 24, ipv6_destination, sizeof(ipv6_destination));
        if (test->extended_header) {
            ip[40] = info->protocol;
            ip[41] = 0;
        }
        if (test->error == GMAC_TX_CHECKSUM_BAD_IPV6_HEADER) {
            ip[0] = 0x50;
        }
    } else {
        uint16_t total_len = info->ip_header_len + info->l4_len;

        ip[0] = 0x40 | (info->ip_header_len / 4);
        stw_be_p(ip + 2, total_len);
        stw_be_p(ip + 4, 0x1234);
        stw_be_p(ip + 6, 0x4000);
        ip[8] = 64;
        ip[9] = info->protocol;
        stw_be_p(ip + 10, test->cic ? 0 : 0x1357);
        ip[12] = 192;
        ip[13] = 0;
        ip[14] = 2;
        ip[15] = 1;
        ip[16] = 198;
        ip[17] = 51;
        ip[18] = 100;
        ip[19] = 2;
        if (test->extended_header) {
            ip[20] = 1;
            ip[21] = 1;
            ip[22] = 0;
            ip[23] = 0;
        }
        if (test->error == GMAC_TX_CHECKSUM_BAD_IP_HEADER) {
            ip[0] = 0x44;
        } else if (test->error == GMAC_TX_CHECKSUM_BAD_PAYLOAD_LENGTH) {
            stw_be_p(ip + 2, total_len + 1);
        }
    }

    switch (test->l4) {
    case GMAC_TX_CHECKSUM_UDP:
    case GMAC_TX_CHECKSUM_UDP_ZERO_RESULT:
        stw_be_p(l4, 1234);
        stw_be_p(l4 + 2, 5678);
        stw_be_p(l4 + 4,
                 info->l4_len +
                 (test->error == GMAC_TX_CHECKSUM_BAD_PAYLOAD_LENGTH) -
                 (test->error == GMAC_TX_CHECKSUM_UDP_LENGTH_MISMATCH));
        info->l4_checksum_offset = info->l4_offset + 6;
        break;
    case GMAC_TX_CHECKSUM_TCP:
        stw_be_p(l4, 1234);
        stw_be_p(l4 + 2, 5678);
        stl_be_p(l4 + 4, 0x01020304);
        stl_be_p(l4 + 8, 0x05060708);
        l4[12] = 0x50;
        l4[13] = 0x18;
        stw_be_p(l4 + 14, 0x4000);
        info->l4_checksum_offset = info->l4_offset + 16;
        break;
    case GMAC_TX_CHECKSUM_ICMP:
        l4[0] = test->ipv6 ? 128 : 8;
        l4[1] = 0;
        stw_be_p(l4 + 4, 0x1234);
        stw_be_p(l4 + 6, 1);
        info->l4_checksum_offset = info->l4_offset + 2;
        break;
    default:
        g_assert_not_reached();
    }
    for (size_t i = 0; i < payload_len; i++) {
        l4[l4_header_len + i] = i + 1;
    }
    if (test->l4 == GMAC_TX_CHECKSUM_UDP_ZERO_RESULT) {
        /* Fixed word chosen so the IPv4/UDP pseudo-header sum is 0xffff. */
        l4[l4_header_len] = 0xe9;
        l4[l4_header_len + 1] = 0x8e;
    }

    if (test->cic == 2) {
        uint16_t seed = gmac_test_checksum_fold(
            gmac_tx_checksum_pseudo_sum(packet, info, info->l4_len));

        stw_be_p(packet + info->l4_checksum_offset, seed);
    } else if (test->cic < 2) {
        stw_be_p(packet + info->l4_checksum_offset, 0x2468);
    }

    if (test->error == GMAC_TX_CHECKSUM_TRAILING_STUFF) {
        static const uint8_t stuff[] = { 0xa5, 0x5a, 0xde, 0xad, 0x7e };
        size_t packet_len = info->l4_offset + info->l4_len;

        memcpy(packet + packet_len, stuff, sizeof(stuff));
        return packet_len + sizeof(stuff);
    }
    return info->l4_offset + info->l4_len;
}

static void gmac_make_tx_checksum_expected(
    uint8_t *packet, unsigned cic, const GMACTxChecksumPacket *info,
    size_t pseudo_l4_len)
{
    uint16_t checksum;
    uint32_t sum;

    if (cic && !info->ipv6) {
        stw_be_p(packet + info->ip_offset + 10, 0);
        sum = gmac_test_checksum_add(0, packet + info->ip_offset,
                                     info->ip_header_len);
        stw_be_p(packet + info->ip_offset + 10,
                 gmac_test_checksum_finish(sum));
    }
    if (cic < 2) {
        return;
    }

    if (cic == 3) {
        stw_be_p(packet + info->l4_checksum_offset, 0);
        sum = gmac_tx_checksum_pseudo_sum(packet, info, pseudo_l4_len);
    } else {
        sum = 0;
    }
    sum = gmac_test_checksum_add(sum, packet + info->l4_offset,
                                 info->l4_len);
    checksum = gmac_test_checksum_finish(sum);
    if ((info->l4 == GMAC_TX_CHECKSUM_UDP ||
         info->l4 == GMAC_TX_CHECKSUM_UDP_ZERO_RESULT) && !checksum) {
        checksum = 0xffff;
    }
    stw_be_p(packet + info->l4_checksum_offset, checksum);
}

static void gmac_receive_tx_packet(int fd, uint8_t *packet,
                                   size_t packet_len)
{
    uint32_t wire_len;

    g_assert_true(gmac_wait_socket_readable(fd));
    g_assert_cmpint(recv(fd, &wire_len, sizeof(wire_len), MSG_WAITALL), ==,
                    sizeof(wire_len));
    g_assert_cmpuint(ntohl(wire_len), ==, packet_len);
    g_assert_cmpint(recv(fd, packet, packet_len, MSG_WAITALL), ==,
                    packet_len);
}

static void gmac_assert_tx_extension(QTestState *qts, uint32_t desc_addr,
                                     const uint32_t expected[4])
{
    uint32_t actual[4];

    gmac_read_rx_extension(qts, desc_addr, actual);
    g_assert_cmpmem(actual, sizeof(actual), expected, 4 * sizeof(*expected));
}

static void gmac_assert_tx_desc(const GMACDesc *actual,
                                const GMACDesc *initial,
                                uint32_t status)
{
    g_assert_cmphex(actual->des0, ==,
                    (initial->des0 & ~(DWMAC_TX_DESC_OWN |
                                       DWMAC_TX_DESC_COE_STATUS)) | status);
    g_assert_cmphex(actual->des1, ==, initial->des1);
    g_assert_cmphex(actual->des2, ==, initial->des2);
    g_assert_cmphex(actual->des3, ==, initial->des3);
}

static void gmac_run_tx_checksum_case(const GMACTxChecksumCase *test)
{
    static const uint32_t first_extension[4] = {
        0x10213243, 0x54657687, 0x98a9bacb, 0xdcedfe0f,
    };
    static const uint32_t last_extension[4] = {
        0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00,
    };
    static const uint32_t buffer_addresses[4] = {
        GMAC_TEST_DATA_ADDR, GMAC_TEST_DATA_ADDR + 0x100,
        GMAC_TEST_DATA2_ADDR, GMAC_TEST_DATA2_ADDR + 0x100,
    };
    uint8_t original[160];
    uint8_t expected[sizeof(original)];
    uint8_t received[sizeof(original)];
    uint8_t guest_buffer[sizeof(original)];
    GMACTxChecksumPacket packet_info;
    GMACDesc initial[2] = { 0 };
    GMACDesc actual;
    size_t buffer_sizes[4] = { 0 };
    size_t packet_len;
    size_t offset = 0;
    unsigned desc_count = test->split ? 2 : 1;
    QTestState *qts;
    int sockets[2];

    g_test_message("GMAC TX checksum case: %s", test->name);
    packet_len = gmac_make_tx_checksum_packet(test, original, &packet_info);
    memcpy(expected, original, packet_len);
    if ((test->error == GMAC_TX_CHECKSUM_VALID ||
         test->error == GMAC_TX_CHECKSUM_TRAILING_STUFF ||
         test->error == GMAC_TX_CHECKSUM_UDP_LENGTH_MISMATCH) && test->tsf) {
        gmac_make_tx_checksum_expected(expected, test->cic, &packet_info,
                                       packet_info.l4_len);
    } else if (test->error == GMAC_TX_CHECKSUM_BAD_PAYLOAD_LENGTH) {
        /*
         * The bounded model uses the declared pseudo length, but only the
         * transport bytes actually supplied by the descriptor chain.
         */
        gmac_make_tx_checksum_expected(expected, test->cic, &packet_info,
                                       packet_info.l4_len + 1);
    }
    if (test->cic == 2) {
        g_assert_cmphex(lduw_be_p(original +
                                  packet_info.l4_checksum_offset), !=, 0);
        g_assert_cmphex(lduw_be_p(original +
                                  packet_info.l4_checksum_offset), !=,
                        lduw_be_p(expected +
                                  packet_info.l4_checksum_offset));
    }
    if (test->l4 == GMAC_TX_CHECKSUM_UDP_ZERO_RESULT) {
        uint32_t sum = gmac_tx_checksum_pseudo_sum(original, &packet_info,
                                                   packet_info.l4_len);

        sum = gmac_test_checksum_add(sum, original + packet_info.l4_offset,
                                     packet_info.l4_len);
        g_assert_cmphex(gmac_test_checksum_finish(sum), ==, 0);
        g_assert_cmphex(lduw_be_p(expected +
                                  packet_info.l4_checksum_offset), ==,
                        0xffff);
    }

    qts = gmac_packet_test_init(sockets);
    if (test->split) {
        buffer_sizes[0] = 7;
        buffer_sizes[1] = 9;
        buffer_sizes[2] = (packet_len - 16) / 2;
        buffer_sizes[3] = packet_len - 16 - buffer_sizes[2];
        for (size_t i = 0; i < ARRAY_SIZE(buffer_sizes); i++) {
            qtest_memwrite(qts, buffer_addresses[i], original + offset,
                           buffer_sizes[i]);
            offset += buffer_sizes[i];
        }
        initial[0] = (GMACDesc) {
            .des0 = DWMAC_TX_DESC_OWN | DWMAC_TX_DESC_FS |
                    DWMAC_TX_DESC_COE_STATUS |
                    (test->cic << DWMAC_TX_DESC_CIC_SHIFT),
            .des1 = buffer_sizes[0] | (buffer_sizes[1] << 16),
            .des2 = buffer_addresses[0],
            .des3 = buffer_addresses[1],
        };
        initial[1] = (GMACDesc) {
            /* A non-FS CIC value must not replace the frame's control. */
            .des0 = DWMAC_TX_DESC_OWN | DWMAC_TX_DESC_IC |
                    DWMAC_TX_DESC_LS | DWMAC_TX_DESC_COE_STATUS |
                    (1U << DWMAC_TX_DESC_CIC_SHIFT),
            .des1 = buffer_sizes[2] | (buffer_sizes[3] << 16),
            .des2 = buffer_addresses[2],
            .des3 = buffer_addresses[3],
        };
        gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &initial[0]);
        gmac_write_rx_extension(qts, GMAC_TEST_DESC_ADDR, first_extension);
        gmac_write_desc(qts,
                        GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                        &initial[1]);
        gmac_write_rx_extension(qts,
                                GMAC_TEST_DESC_ADDR +
                                GMAC_ENHANCED_DESC_STRIDE,
                                last_extension);
    } else {
        buffer_sizes[0] = packet_len;
        qtest_memwrite(qts, buffer_addresses[0], original, packet_len);
        initial[0] = (GMACDesc) {
            .des0 = DWMAC_TX_DESC_OWN | DWMAC_TX_DESC_IC |
                    DWMAC_TX_DESC_LS | DWMAC_TX_DESC_FS |
                    DWMAC_TX_DESC_COE_STATUS |
                    (test->cic << DWMAC_TX_DESC_CIC_SHIFT),
            .des1 = packet_len,
            .des2 = buffer_addresses[0],
        };
        gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &initial[0]);
        gmac_write_rx_extension(qts, GMAC_TEST_DESC_ADDR, last_extension);
    }
    actual = (GMACDesc) { 0 };
    gmac_write_desc(qts,
                    GMAC_TEST_DESC_ADDR +
                    desc_count * GMAC_ENHANCED_DESC_STRIDE,
                    &actual);

    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                  0x00020100 | BIT(7));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_TX_BASE_ADDR,
                  GMAC_TEST_DESC_ADDR);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                  BIT(16) | BIT(0));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_VLAN_TAG,
                  test->vlan == GMAC_TX_CHECKSUM_S_VLAN ?
                  DWMAC_VLAN_TAG_ESVL : 0);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG, BIT(3));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL,
                  (test->tsf ? DWMAC_DMA_CONTROL_TSF : 0) |
                  DWMAC_DMA_CONTROL_ST);

    gmac_receive_tx_packet(sockets[0], received, packet_len);
    g_assert_true(gmac_wait_status(qts, BIT(0)));
    g_assert_cmpmem(received, packet_len, expected, packet_len);

    for (unsigned i = 0; i < desc_count; i++) {
        uint32_t desc_addr = GMAC_TEST_DESC_ADDR +
                             i * GMAC_ENHANCED_DESC_STRIDE;

        gmac_read_desc(qts, desc_addr, &actual);
        gmac_assert_tx_desc(&actual, &initial[i],
                            i + 1 == desc_count ? test->status : 0);
        gmac_assert_tx_extension(qts, desc_addr,
                                 i + 1 == desc_count ? last_extension :
                                                       first_extension);
    }
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_HOST_TX_DESC),
                    ==, GMAC_TEST_DESC_ADDR +
                        desc_count * GMAC_ENHANCED_DESC_STRIDE);

    offset = 0;
    for (size_t i = 0; i < (test->split ? 4 : 1); i++) {
        qtest_memread(qts, buffer_addresses[i], guest_buffer,
                      buffer_sizes[i]);
        g_assert_cmpmem(guest_buffer, buffer_sizes[i], original + offset,
                        buffer_sizes[i]);
        offset += buffer_sizes[i];
    }

    qtest_quit(qts);
    close(sockets[0]);
}

/*
 * A multi-descriptor transmit frame may be submitted incrementally: the guest
 * owns the first segment, the DMA consumes it and suspends on the last segment
 * it does not yet own, and the guest supplies that segment afterwards.  The
 * accumulated segments belong to the transmit engine across that suspend, so
 * the resumed frame must carry both of them.
 */
static void gmac_configure_rx_ring(QTestState *qts, uint32_t desc_addr)
{
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_HI,
                 BIT(31) | 0x5634);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_LO, 0x12005452);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                 0x00020100 | BIT(7));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RX_BASE_ADDR, desc_addr);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                 BIT(16) | BIT(15) | DWMAC_DMA_STATUS_RU |
                 DWMAC_DMA_STATUS_RI);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG,
                 DWMAC_MAC_CONFIG_RX_EN);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL, BIT(1));
}

/*
 * A frame that arrives while every receive descriptor is owned by software
 * is held by the MAC, not discarded: the DMA reports RU and suspends, and the
 * frame lands as soon as the guest hands a descriptor back and resumes.
 */
static void test_gmac_rx_ring_full_backpressure(void)
{
    static const uint8_t packet[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0xaa, 0xbb, 0xcc, 0xdd,
    };
    uint8_t buffer[sizeof(packet)];
    QTestState *qts;
    GMACDesc desc;
    int sockets[2];

    qts = gmac_packet_test_init(sockets);
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 2048);

    /* A one-descriptor ring that software still owns: the ring is full. */
    desc = (GMACDesc) {
        .des0 = 0,
        .des1 = DWMAC_RX_DESC_RER | 2048,
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    gmac_configure_rx_ring(qts, GMAC_TEST_DESC_ADDR);

    gmac_send_packet(sockets[0], packet, sizeof(packet));

    /* The DMA cannot place the frame and must say so without touching it. */
    g_assert_true(gmac_wait_status(qts, DWMAC_DMA_STATUS_RU));
    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0, ==, 0);
    qtest_memread(qts, GMAC_TEST_DATA_ADDR, buffer, sizeof(buffer));
    g_assert_cmphex(buffer[0], ==, 0xa5);

    /* Hand the descriptor over and resume: the held frame must land. */
    desc.des0 = BIT(31);
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RCV_POLL_DEMAND, 1);

    gmac_wait_rx_desc_complete(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0 & (DWMAC_RX_DESC_FS | DWMAC_RX_DESC_LS), ==,
                    DWMAC_RX_DESC_FS | DWMAC_RX_DESC_LS);
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==, sizeof(packet) + 4);
    qtest_memread(qts, GMAC_TEST_DATA_ADDR, buffer, sizeof(buffer));
    g_assert_cmpmem(buffer, sizeof(buffer), packet, sizeof(packet));
    g_assert_true(gmac_wait_status(qts, DWMAC_DMA_STATUS_RI));

    qtest_quit(qts);
    close(sockets[0]);
}

/* Let QEMU consume anything queued on the backend socket. */
static void gmac_settle(QTestState *qts)
{
    for (unsigned i = 0; i < 20; i++) {
        qtest_clock_step(qts, 1000000);
        g_usleep(2000);
    }
}

static bool gmac_desc_stays_owned(QTestState *qts, uint32_t desc_addr)
{
    GMACDesc desc;

    for (unsigned i = 0; i < 50; i++) {
        qtest_clock_step(qts, 1000000);
        g_usleep(2000);
        gmac_read_desc(qts, desc_addr, &desc);
        if (!(desc.des0 & BIT(31))) {
            return false;
        }
    }
    return true;
}

/*
 * A frame larger than the first available buffer spans descriptors.  When
 * the second descriptor is still owned by software the frame is held whole
 * and the first descriptor is left untouched.  The next frame's arrival then
 * re-polls the ring and delivers both in order: that is the only resume path
 * a driver which never writes the receive poll demand has.
 */
static void test_gmac_rx_suspend_midframe(void)
{
    const uint32_t desc1_addr = GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE;
    const uint32_t desc2_addr = GMAC_TEST_DESC_ADDR +
                                2 * GMAC_ENHANCED_DESC_STRIDE;
    const uint32_t third_buffer = GMAC_TEST_DATA2_ADDR + 0x800;
    static const uint8_t packet[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x11, 0x22, 0x33, 0x44,
    };
    static const uint8_t second[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x55, 0x66, 0x77, 0x88,
    };
    uint8_t buffer[64];
    QTestState *qts;
    GMACDesc desc;
    int sockets[2];

    qts = gmac_packet_test_init(sockets);
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 2048);
    qtest_memset(qts, GMAC_TEST_DATA2_ADDR, 0x5a, 2048);
    qtest_memset(qts, third_buffer, 0x3c, 2048);

    desc = (GMACDesc) { .des0 = BIT(31), .des1 = 32,
                        .des2 = GMAC_TEST_DATA_ADDR };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    desc = (GMACDesc) { .des0 = 0, .des1 = 2048, .des2 = GMAC_TEST_DATA2_ADDR };
    gmac_write_desc(qts, desc1_addr, &desc);
    desc = (GMACDesc) { .des0 = BIT(31), .des1 = DWMAC_RX_DESC_RER | 2048,
                        .des2 = third_buffer };
    gmac_write_desc(qts, desc2_addr, &desc);
    gmac_configure_rx_ring(qts, GMAC_TEST_DESC_ADDR);

    gmac_send_packet(sockets[0], packet, sizeof(packet));
    g_assert_true(gmac_wait_status(qts, DWMAC_DMA_STATUS_RU));

    /* Nothing is written back until the whole frame fits. */
    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0, ==, BIT(31));
    gmac_read_desc(qts, desc1_addr, &desc);
    g_assert_cmphex(desc.des0, ==, 0);
    qtest_memread(qts, GMAC_TEST_DATA_ADDR, buffer, 1);
    g_assert_cmphex(buffer[0], ==, 0xa5);

    /* Hand the descriptor over; no register write follows. */
    desc.des0 = BIT(31);
    gmac_write_desc(qts, desc1_addr, &desc);
    gmac_send_packet(sockets[0], second, sizeof(second));

    gmac_wait_rx_desc_complete(qts, desc2_addr, &desc);
    g_assert_cmphex(desc.des0 & (DWMAC_RX_DESC_FS | DWMAC_RX_DESC_LS), ==,
                    DWMAC_RX_DESC_FS | DWMAC_RX_DESC_LS);
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==, sizeof(second) + 4);
    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0 & (BIT(31) | DWMAC_RX_DESC_FS |
                                 DWMAC_RX_DESC_LS), ==, DWMAC_RX_DESC_FS);
    gmac_read_desc(qts, desc1_addr, &desc);
    g_assert_cmphex(desc.des0 & (BIT(31) | DWMAC_RX_DESC_FS |
                                 DWMAC_RX_DESC_LS), ==, DWMAC_RX_DESC_LS);
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==, sizeof(packet) + 4);

    qtest_memread(qts, GMAC_TEST_DATA_ADDR, buffer, 32);
    g_assert_cmpmem(buffer, 32, packet, 32);
    qtest_memread(qts, GMAC_TEST_DATA2_ADDR, buffer, 32);
    g_assert_cmpmem(buffer, 32, packet + 32, 32);
    qtest_memread(qts, third_buffer, buffer, sizeof(buffer));
    g_assert_cmpmem(buffer, sizeof(buffer), second, sizeof(second));
    g_assert_true(gmac_wait_status(qts, DWMAC_DMA_STATUS_RI));

    qtest_quit(qts);
    close(sockets[0]);
}

/*
 * The receive FIFO is finite.  With a 100-byte FIFO a first 68-byte frame is
 * held, a second overflows and is counted, and only the held frame is ever
 * delivered.
 */
static void test_gmac_rx_fifo_overflow(void)
{
    static const uint8_t packet[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x01, 0x02, 0x03, 0x04,
    };
    static const uint8_t overflow[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x05, 0x06, 0x07, 0x08,
    };
    uint8_t buffer[64];
    uint32_t missed;
    QTestState *qts;
    GMACDesc desc;
    int sockets[2];

    qts = gmac_packet_test_init_extra(sockets,
                                      "-global dw-gmac.rx-fifo-size=100");
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 2048);
    desc = (GMACDesc) { .des0 = 0, .des1 = DWMAC_RX_DESC_RER | 2048,
                        .des2 = GMAC_TEST_DATA_ADDR };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    gmac_configure_rx_ring(qts, GMAC_TEST_DESC_ADDR);

    gmac_send_packet(sockets[0], packet, sizeof(packet));
    g_assert_true(gmac_wait_status(qts, DWMAC_DMA_STATUS_RU));
    gmac_send_packet(sockets[0], overflow, sizeof(overflow));
    g_assert_true(gmac_wait_status(qts, BIT(4)));

    missed = qtest_readl(qts, TH1520_GMAC0_BASE + 0x1020);
    g_assert_cmpuint(extract32(missed, 17, 11), ==, 1);
    g_assert_cmpuint(extract32(missed, 0, 16), ==, 0);

    desc.des0 = BIT(31);
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RCV_POLL_DEMAND, 1);
    gmac_wait_rx_desc_complete(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==, sizeof(packet) + 4);
    qtest_memread(qts, GMAC_TEST_DATA_ADDR, buffer, sizeof(buffer));
    g_assert_cmpmem(buffer, sizeof(buffer), packet, sizeof(packet));

    /* The overflowed frame is gone: a fresh descriptor stays with the DMA. */
    desc.des0 = BIT(31);
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RCV_POLL_DEMAND, 1);
    g_assert_true(gmac_desc_stays_owned(qts, GMAC_TEST_DESC_ADDR));

    qtest_quit(qts);
    close(sockets[0]);
}

/* Frames held across a ring-full event are delivered in arrival order. */
static void test_gmac_rx_fifo_order(void)
{
    const uint32_t desc1_addr = GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE;
    static const uint8_t first[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0xf1, 0xf1, 0xf1, 0xf1,
    };
    static const uint8_t second[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0xf2, 0xf2, 0xf2, 0xf2,
    };
    uint8_t buffer[64];
    QTestState *qts;
    GMACDesc desc;
    int sockets[2];

    qts = gmac_packet_test_init(sockets);
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 2048);
    qtest_memset(qts, GMAC_TEST_DATA2_ADDR, 0x5a, 2048);
    desc = (GMACDesc) { .des0 = 0, .des1 = 2048, .des2 = GMAC_TEST_DATA_ADDR };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    desc = (GMACDesc) { .des0 = 0, .des1 = DWMAC_RX_DESC_RER | 2048,
                        .des2 = GMAC_TEST_DATA2_ADDR };
    gmac_write_desc(qts, desc1_addr, &desc);
    gmac_configure_rx_ring(qts, GMAC_TEST_DESC_ADDR);

    gmac_send_packet(sockets[0], first, sizeof(first));
    gmac_send_packet(sockets[0], second, sizeof(second));
    g_assert_true(gmac_wait_status(qts, DWMAC_DMA_STATUS_RU));
    gmac_settle(qts);
    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0, ==, 0);

    desc.des0 = BIT(31);
    desc.des1 = 2048;
    desc.des2 = GMAC_TEST_DATA_ADDR;
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    desc.des1 = DWMAC_RX_DESC_RER | 2048;
    desc.des2 = GMAC_TEST_DATA2_ADDR;
    gmac_write_desc(qts, desc1_addr, &desc);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RCV_POLL_DEMAND, 1);

    gmac_wait_rx_desc_complete(qts, desc1_addr, &desc);
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==, sizeof(second) + 4);
    qtest_memread(qts, GMAC_TEST_DATA2_ADDR, buffer, sizeof(buffer));
    g_assert_cmpmem(buffer, sizeof(buffer), second, sizeof(second));
    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0 & BIT(31), ==, 0);
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==, sizeof(first) + 4);
    qtest_memread(qts, GMAC_TEST_DATA_ADDR, buffer, sizeof(buffer));
    g_assert_cmpmem(buffer, sizeof(buffer), first, sizeof(first));

    qtest_quit(qts);
    close(sockets[0]);
}

/*
 * A frame held in the receive FIFO is in-flight device state and must reach
 * the destination, which then delivers it once the guest resumes the ring.
 */
static void test_gmac_rx_fifo_migration(void)
{
    static const uint8_t packet[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0xc0, 0xff, 0xee, 0x00,
    };
    uint8_t buffer[64];
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    GMACDesc desc;
    int src_sockets[2];
    int dst_sockets[2];
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-gmac-rxfifo-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, src_sockets), ==, 0);
    src = qtest_initf("-machine beaglev-ahead -bios none "
                      "-nic socket,fd=%d,model=gmac0", src_sockets[1]);
    close(src_sockets[1]);
    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, dst_sockets), ==, 0);
    dst = qtest_initf("-machine beaglev-ahead -bios none -incoming defer "
                      "-nic socket,fd=%d,model=gmac0", dst_sockets[1]);
    close(dst_sockets[1]);

    qtest_memset(src, GMAC_TEST_DATA_ADDR, 0xa5, 2048);
    desc = (GMACDesc) { .des0 = 0, .des1 = DWMAC_RX_DESC_RER | 2048,
                        .des2 = GMAC_TEST_DATA_ADDR };
    gmac_write_desc(src, GMAC_TEST_DESC_ADDR, &desc);
    gmac_configure_rx_ring(src, GMAC_TEST_DESC_ADDR);
    gmac_send_packet(src_sockets[0], packet, sizeof(packet));
    g_assert_true(gmac_wait_status(src, DWMAC_DMA_STATUS_RU));

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_true(qtest_readl(dst, TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                  DWMAC_DMA_STATUS_RU);
    gmac_read_desc(dst, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0, ==, 0);

    desc.des0 = BIT(31);
    gmac_write_desc(dst, GMAC_TEST_DESC_ADDR, &desc);
    qtest_writel(dst, TH1520_GMAC0_BASE + DWMAC_DMA_RCV_POLL_DEMAND, 1);
    gmac_wait_rx_desc_complete(dst, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==, sizeof(packet) + 4);
    qtest_memread(dst, GMAC_TEST_DATA_ADDR, buffer, sizeof(buffer));
    g_assert_cmpmem(buffer, sizeof(buffer), packet, sizeof(packet));

    qtest_quit(dst);
    qtest_quit(src);
    close(src_sockets[0]);
    close(dst_sockets[0]);
    unlink(path);
}

static void test_gmac_tx_suspend_midframe(void)
{
    const uint32_t last_desc_addr = GMAC_TEST_DESC_ADDR +
                                    GMAC_ENHANCED_DESC_STRIDE;
    uint8_t first[64], last[64], expected[128], received[128];
    QTestState *qts;
    GMACDesc desc;
    int sockets[2];

    for (size_t i = 0; i < sizeof(first); i++) {
        first[i] = 0x40 + i;
    }
    for (size_t i = 0; i < sizeof(last); i++) {
        last[i] = 0x80 + i;
    }
    memcpy(expected, first, sizeof(first));
    memcpy(expected + sizeof(first), last, sizeof(last));

    qts = gmac_packet_test_init(sockets);
    qtest_memwrite(qts, GMAC_TEST_DATA_ADDR, first, sizeof(first));
    qtest_memwrite(qts, GMAC_TEST_DATA2_ADDR, last, sizeof(last));

    desc = (GMACDesc) {
        .des0 = DWMAC_TX_DESC_OWN | DWMAC_TX_DESC_FS,
        .des1 = sizeof(first),
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);

    /* The terminal segment is deliberately still owned by software. */
    desc = (GMACDesc) {
        .des0 = DWMAC_TX_DESC_IC | DWMAC_TX_DESC_LS,
        .des1 = sizeof(last),
        .des2 = GMAC_TEST_DATA2_ADDR,
    };
    gmac_write_desc(qts, last_desc_addr, &desc);

    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                 0x00020100 | BIT(7));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_TX_BASE_ADDR,
                 GMAC_TEST_DESC_ADDR);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                 BIT(16) | BIT(0) | BIT(2));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG, BIT(3));
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL,
                 DWMAC_DMA_CONTROL_TSF | DWMAC_DMA_CONTROL_ST);

    /* The engine consumes the first segment, then reports it is unavailable. */
    g_assert_true(gmac_wait_status(qts, DWMAC_DMA_STATUS_TU));
    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0 & DWMAC_TX_DESC_OWN, ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_HOST_TX_DESC),
                    ==, last_desc_addr);

    /* Hand the terminal segment over and resume the suspended engine. */
    gmac_read_desc(qts, last_desc_addr, &desc);
    desc.des0 |= DWMAC_TX_DESC_OWN;
    gmac_write_desc(qts, last_desc_addr, &desc);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_XMT_POLL_DEMAND, 1);

    gmac_receive_tx_packet(sockets[0], received, sizeof(expected));
    g_assert_cmpmem(received, sizeof(expected), expected, sizeof(expected));

    qtest_quit(qts);
    close(sockets[0]);
}

/*
 * The same suspended frame must survive migration.  The partially assembled
 * bytes live in the transmit engine, not in the descriptor ring, so a stream
 * that omits them silently truncates the frame the destination sends.
 */
static void test_gmac_tx_suspend_migration(void)
{
    const uint32_t last_desc_addr = GMAC_TEST_DESC_ADDR +
                                    GMAC_ENHANCED_DESC_STRIDE;
    uint8_t first[64], last[64], expected[128], received[128];
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    GMACDesc desc;
    int src_sockets[2];
    int dst_sockets[2];
    int fd;

    for (size_t i = 0; i < sizeof(first); i++) {
        first[i] = 0x40 + i;
    }
    for (size_t i = 0; i < sizeof(last); i++) {
        last[i] = 0x80 + i;
    }
    memcpy(expected, first, sizeof(first));
    memcpy(expected + sizeof(first), last, sizeof(last));

    fd = g_file_open_tmp("beaglev-ahead-gmac-txframe-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, src_sockets), ==, 0);
    src = qtest_initf("-machine beaglev-ahead -bios none "
                      "-nic socket,fd=%d,model=gmac0", src_sockets[1]);
    close(src_sockets[1]);
    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, dst_sockets), ==, 0);
    dst = qtest_initf("-machine beaglev-ahead -bios none -incoming defer "
                      "-nic socket,fd=%d,model=gmac0", dst_sockets[1]);
    close(dst_sockets[1]);

    qtest_memwrite(src, GMAC_TEST_DATA_ADDR, first, sizeof(first));
    qtest_memwrite(src, GMAC_TEST_DATA2_ADDR, last, sizeof(last));

    desc = (GMACDesc) {
        .des0 = DWMAC_TX_DESC_OWN | DWMAC_TX_DESC_FS,
        .des1 = sizeof(first),
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    gmac_write_desc(src, GMAC_TEST_DESC_ADDR, &desc);
    desc = (GMACDesc) {
        .des0 = DWMAC_TX_DESC_IC | DWMAC_TX_DESC_LS,
        .des1 = sizeof(last),
        .des2 = GMAC_TEST_DATA2_ADDR,
    };
    gmac_write_desc(src, last_desc_addr, &desc);

    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                 0x00020100 | BIT(7));
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_DMA_TX_BASE_ADDR,
                 GMAC_TEST_DESC_ADDR);
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                 BIT(16) | BIT(0) | BIT(2));
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG, BIT(3));
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL,
                 DWMAC_DMA_CONTROL_TSF | DWMAC_DMA_CONTROL_ST);

    /* Suspend the engine with the first segment already accumulated. */
    g_assert_true(gmac_wait_status(src, DWMAC_DMA_STATUS_TU));
    g_assert_cmphex(qtest_readl(src,
                                TH1520_GMAC0_BASE + DWMAC_DMA_HOST_TX_DESC),
                    ==, last_desc_addr);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    /* The destination owns the accumulated segment and must complete it. */
    gmac_read_desc(dst, last_desc_addr, &desc);
    desc.des0 |= DWMAC_TX_DESC_OWN;
    gmac_write_desc(dst, last_desc_addr, &desc);
    qtest_writel(dst, TH1520_GMAC0_BASE + DWMAC_DMA_XMT_POLL_DEMAND, 1);

    /*
     * QEMU announces a migrated NIC to the network with broadcast frames, so
     * the transmitted frame is not necessarily the first thing on the wire.
     * Skip anything that is not the expected length.
     */
    for (unsigned attempt = 0; ; attempt++) {
        uint32_t wire_len;

        g_assert_cmpuint(attempt, <, 16);
        g_assert_true(gmac_wait_socket_readable(dst_sockets[0]));
        g_assert_cmpint(recv(dst_sockets[0], &wire_len, sizeof(wire_len),
                             MSG_WAITALL), ==, sizeof(wire_len));
        wire_len = ntohl(wire_len);
        if (wire_len != sizeof(expected)) {
            uint8_t discard[256];

            g_assert_cmpuint(wire_len, <=, sizeof(discard));
            g_assert_cmpint(recv(dst_sockets[0], discard, wire_len,
                                 MSG_WAITALL), ==, (int)wire_len);
            continue;
        }
        g_assert_cmpint(recv(dst_sockets[0], received, wire_len, MSG_WAITALL),
                        ==, (int)wire_len);
        break;
    }
    g_assert_cmpmem(received, sizeof(expected), expected, sizeof(expected));

    qtest_quit(dst);
    qtest_quit(src);
    close(src_sockets[0]);
    close(dst_sockets[0]);
    unlink(path);
}

static void test_gmac_tx_checksum(void)
{
    static const GMACTxChecksumCase cases[] = {
        { "cic0-ipv4-udp", 0, false, GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_VALID, false, true, 0 },
        { "cic1-ipv4-options-udp-odd", 1, false,
          GMAC_TX_CHECKSUM_NO_VLAN, true,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_VALID, false, true, 0 },
        { "cic1-ipv6-udp-bypass", 1, true,
          GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_VALID, false, true, 0 },
        { "cic2-ipv4-tcp-seed", 2, false, GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_TCP, GMAC_TX_CHECKSUM_VALID, false, true, 0 },
        { "cic3-vlan-ipv4-udp-split", 3, false,
          GMAC_TX_CHECKSUM_C_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_VALID, true, true, 0 },
        { "cic3-svlan-ipv4-udp", 3, false, GMAC_TX_CHECKSUM_S_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_VALID, false, true, 0 },
        { "cic3-ipv4-udp-zero-result", 3, false,
          GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_UDP_ZERO_RESULT, GMAC_TX_CHECKSUM_VALID,
          false, true, 0 },
        { "cic3-ipv4-udp-trailing-stuff", 3, false,
          GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_TRAILING_STUFF,
          false, true, 0 },
        { "cic3-ipv4-udp-length-mismatch", 3, false,
          GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_UDP_LENGTH_MISMATCH,
          false, true, 0 },
        { "cic3-ipv4-icmp", 3, false, GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_ICMP, GMAC_TX_CHECKSUM_VALID, false, true, 0 },
        { "cic3-ipv6-udp", 3, true, GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_VALID, false, true, 0 },
        { "cic3-ipv6-tcp", 3, true, GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_TCP, GMAC_TX_CHECKSUM_VALID, false, true, 0 },
        { "cic3-ipv6-hbh-tcp", 3, true, GMAC_TX_CHECKSUM_NO_VLAN, true,
          GMAC_TX_CHECKSUM_TCP, GMAC_TX_CHECKSUM_VALID, false, true, 0 },
        { "cic3-ipv6-icmp", 3, true, GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_ICMP, GMAC_TX_CHECKSUM_VALID, false, true, 0 },
        { "cic3-ipv4-udp-tsf-clear", 3, false,
          GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_VALID, false, false, 0 },
        { "cic3-bad-ipv4-header-split", 3, false,
          GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_BAD_IP_HEADER, true, true,
          DWMAC_TX_DESC_IHE | DWMAC_TX_DESC_ES },
        { "cic3-bad-ipv6-header-split", 3, true,
          GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_BAD_IPV6_HEADER, true, true,
          DWMAC_TX_DESC_IHE | DWMAC_TX_DESC_ES },
        { "cic3-bad-payload-length-split", 3, false,
          GMAC_TX_CHECKSUM_NO_VLAN, false,
          GMAC_TX_CHECKSUM_UDP, GMAC_TX_CHECKSUM_BAD_PAYLOAD_LENGTH,
          true, true,
          DWMAC_TX_DESC_IPE | DWMAC_TX_DESC_ES },
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
        gmac_run_tx_checksum_case(&cases[i]);
    }
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

    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_HI,
                  BIT(31) | 0x5634);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_LO, 0x12005452);
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

static void test_gmac_rx_interrupt_watchdog(void)
{
    static const uint8_t packet[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00,
    };
    const uint32_t riwt = 0xa0;
    const int64_t timeout_ns = DIV_ROUND_UP(
        (uint64_t)riwt * 256 * 1000000000ULL,
        TH1520_GMAC_RIWT_CLOCK_HZ);
    const int64_t elapsed_ns = timeout_ns / 2;
    const uint32_t ri_status = DWMAC_DMA_STATUS_NIS |
                               DWMAC_DMA_STATUS_RI;
    const uint32_t ri_status_mask = ri_status |
                                    DWMAC_DMA_STATUS_AIS |
                                    DWMAC_DMA_STATUS_RWT;
    GMACDesc desc;
    QTestState *qts;
    int sockets[2];

    qts = gmac_packet_test_init(sockets);
    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 2048);
    gmac_prepare_rx_desc(qts, GMAC_TEST_DESC_ADDR, GMAC_TEST_DATA_ADDR,
                         true);
    desc = (GMACDesc) { 0 };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                    &desc);

    qtest_writel(qts, C900_PLIC_PRIORITY(TH1520_GMAC0_IRQ), 5);
    c900_plic_set_enable(qts, 1, TH1520_GMAC0_IRQ, true);
    gmac_configure_rx_watchdog(qts, GMAC_TEST_DESC_ADDR, riwt);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RX_WATCHDOG,
                  0xffffff00 | riwt);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE +
                                DWMAC_DMA_RX_WATCHDOG), ==, riwt);
    gmac_send_packet(sockets[0], packet, sizeof(packet));
    gmac_wait_rx_desc_complete(qts, GMAC_TEST_DESC_ADDR, &desc);
    gmac_assert_rx_frame(qts, GMAC_TEST_DESC_ADDR, GMAC_TEST_DATA_ADDR,
                         packet, sizeof(packet), 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, 0);
    g_assert_false(c900_plic_pending(qts, TH1520_GMAC0_IRQ));
    assert_no_irq(qts);

    qtest_clock_step(qts, timeout_ns - 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, 0);
    g_assert_false(c900_plic_pending(qts, TH1520_GMAC0_IRQ));
    assert_no_irq(qts);

    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, ri_status);
    g_assert_true(c900_plic_pending(qts, TH1520_GMAC0_IRQ));
    assert_only_irq(qts, 0);

    /* Reset must lower an already asserted IRQ as well as clear RIWT. */
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_RX_WATCHDOG),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, 0);
    g_assert_false(c900_plic_pending(qts, TH1520_GMAC0_IRQ));
    assert_no_irq(qts);

    /* Reset must also cancel a watchdog which has not expired yet. */
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 2048);
    gmac_prepare_rx_desc(qts, GMAC_TEST_DESC_ADDR, GMAC_TEST_DATA_ADDR,
                         true);
    gmac_configure_rx_watchdog(qts, GMAC_TEST_DESC_ADDR, riwt);
    gmac_send_packet(sockets[0], packet, sizeof(packet));
    gmac_wait_rx_desc_complete(qts, GMAC_TEST_DESC_ADDR, &desc);
    qtest_system_reset(qts);
    qtest_clock_step(qts, timeout_ns);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_RX_WATCHDOG),
                    ==, 0);
    g_assert_false(c900_plic_pending(qts, TH1520_GMAC0_IRQ));
    assert_no_irq(qts);

    /* Writing zero disables and cancels a pending watchdog. */
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 2048);
    gmac_prepare_rx_desc(qts, GMAC_TEST_DESC_ADDR, GMAC_TEST_DATA_ADDR,
                         true);
    gmac_configure_rx_watchdog(qts, GMAC_TEST_DESC_ADDR, riwt);
    gmac_send_packet(sockets[0], packet, sizeof(packet));
    gmac_wait_rx_desc_complete(qts, GMAC_TEST_DESC_ADDR, &desc);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RX_WATCHDOG, 0);
    qtest_clock_step(qts, timeout_ns);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, 0);

    /* A later non-DIC completion sets RI immediately and cancels RIWT. */
    qtest_system_reset(qts);
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 4096);
    gmac_prepare_rx_desc(qts, GMAC_TEST_DESC_ADDR, GMAC_TEST_DATA_ADDR,
                         true);
    gmac_prepare_rx_desc(qts,
                         GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                         GMAC_TEST_DATA2_ADDR, false);
    gmac_configure_rx_watchdog(qts, GMAC_TEST_DESC_ADDR, riwt);
    gmac_send_packet(sockets[0], packet, sizeof(packet));
    gmac_wait_rx_desc_complete(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, 0);
    gmac_send_packet(sockets[0], packet, sizeof(packet));
    gmac_wait_rx_desc_complete(qts,
                               GMAC_TEST_DESC_ADDR +
                               GMAC_ENHANCED_DESC_STRIDE, &desc);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, ri_status);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_STATUS,
                  ri_status);
    qtest_clock_step(qts, timeout_ns);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, 0);

    /* RI and the normal summary are independent W1C status bits. */
    qtest_system_reset(qts);
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 2048);
    gmac_prepare_rx_desc(qts, GMAC_TEST_DESC_ADDR, GMAC_TEST_DATA_ADDR,
                         true);
    desc = (GMACDesc) { 0 };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                    &desc);
    gmac_configure_rx_watchdog(qts, GMAC_TEST_DESC_ADDR, riwt);
    gmac_send_packet(sockets[0], packet, sizeof(packet));
    gmac_wait_rx_desc_complete(qts, GMAC_TEST_DESC_ADDR, &desc);
    qtest_clock_step(qts, timeout_ns);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, ri_status);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_STATUS,
                  DWMAC_DMA_STATUS_RI);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, DWMAC_DMA_STATUS_NIS);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_STATUS,
                  DWMAC_DMA_STATUS_NIS);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, 0);

    /* A second DIC completion must not postpone an armed deadline. */
    qtest_system_reset(qts);
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 4096);
    gmac_prepare_rx_desc(qts, GMAC_TEST_DESC_ADDR, GMAC_TEST_DATA_ADDR,
                         true);
    gmac_prepare_rx_desc(qts,
                         GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                         GMAC_TEST_DATA2_ADDR, true);
    desc = (GMACDesc) { 0 };
    gmac_write_desc(qts,
                    GMAC_TEST_DESC_ADDR + 2 * GMAC_ENHANCED_DESC_STRIDE,
                    &desc);
    gmac_configure_rx_watchdog(qts, GMAC_TEST_DESC_ADDR, riwt);
    gmac_send_packet(sockets[0], packet, sizeof(packet));
    gmac_wait_rx_desc_complete(qts, GMAC_TEST_DESC_ADDR, &desc);
    qtest_clock_step(qts, elapsed_ns);
    gmac_send_packet(sockets[0], packet, sizeof(packet));
    gmac_wait_rx_desc_complete(qts,
                               GMAC_TEST_DESC_ADDR +
                               GMAC_ENHANCED_DESC_STRIDE, &desc);
    qtest_clock_step(qts, timeout_ns - elapsed_ns - 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, ri_status);

    /* Reprogramming RIWT nonzero must not postpone an armed deadline. */
    qtest_system_reset(qts);
    qtest_memset(qts, GMAC_TEST_DATA_ADDR, 0xa5, 2048);
    gmac_prepare_rx_desc(qts, GMAC_TEST_DESC_ADDR, GMAC_TEST_DATA_ADDR,
                         true);
    desc = (GMACDesc) { 0 };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                    &desc);
    gmac_configure_rx_watchdog(qts, GMAC_TEST_DESC_ADDR, riwt);
    gmac_send_packet(sockets[0], packet, sizeof(packet));
    gmac_wait_rx_desc_complete(qts, GMAC_TEST_DESC_ADDR, &desc);
    qtest_clock_step(qts, elapsed_ns);
    qtest_writel(qts, TH1520_GMAC0_BASE + DWMAC_DMA_RX_WATCHDOG, 0xff);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE +
                                DWMAC_DMA_RX_WATCHDOG), ==, 0xff);
    qtest_clock_step(qts, timeout_ns - elapsed_ns - 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    ri_status_mask, ==, ri_status);

    qtest_quit(qts);
    close(sockets[0]);
}

static void test_gmac_rx_watchdog_migration(void)
{
    static const uint8_t packet[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00,
    };
    const uint32_t riwt = 0xa0;
    const int64_t timeout_ns = DIV_ROUND_UP(
        (uint64_t)riwt * 256 * 1000000000ULL,
        TH1520_GMAC_RIWT_CLOCK_HZ);
    const int64_t elapsed_ns = timeout_ns / 2;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    GMACDesc desc;
    int src_sockets[2];
    int dst_sockets[2];
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-gmac-riwt-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, src_sockets), ==, 0);
    src = qtest_initf("-machine beaglev-ahead -bios none "
                      "-nic socket,fd=%d,model=gmac0", src_sockets[1]);
    close(src_sockets[1]);
    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, dst_sockets), ==, 0);
    dst = qtest_initf("-machine beaglev-ahead -bios none -incoming defer "
                      "-nic socket,fd=%d,model=gmac0", dst_sockets[1]);
    close(dst_sockets[1]);
    g_assert_cmpint(qtest_clock_set(src, 0), ==, 0);

    qtest_memset(src, GMAC_TEST_DATA_ADDR, 0xa5, 2048);
    gmac_prepare_rx_desc(src, GMAC_TEST_DESC_ADDR, GMAC_TEST_DATA_ADDR,
                         true);
    gmac_configure_rx_watchdog(src, GMAC_TEST_DESC_ADDR, riwt);
    gmac_send_packet(src_sockets[0], packet, sizeof(packet));
    gmac_wait_rx_desc_complete(src, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(qtest_readl(src,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    (BIT(16) | BIT(6)), ==, 0);
    qtest_clock_step(src, elapsed_ns);
    g_assert_cmpint(qtest_clock_set(dst, elapsed_ns), ==, elapsed_ns);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC0_BASE +
                                DWMAC_DMA_RX_WATCHDOG), ==, riwt);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    (BIT(16) | BIT(6)), ==, 0);
    qtest_clock_step(dst, timeout_ns - elapsed_ns - 1);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    (BIT(16) | BIT(6)), ==, 0);
    qtest_clock_step(dst, 1);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    (BIT(16) | BIT(6)), ==, BIT(16) | BIT(6));

    qtest_quit(dst);
    qtest_quit(src);
    close(dst_sockets[0]);
    close(src_sockets[0]);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_gmac_rx_filter_migration(void)
{
    static const uint8_t old_mac_packet[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x02, 0x00, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00, 0x31,
    };
    uint8_t new_mac_packet[sizeof(gmac_ipv4_udp_packet)];
    uint32_t actual_extension[4];
    GMACDesc desc = {
        .des0 = BIT(31),
        .des1 = BIT(31) | 2048,
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int src_sockets[2];
    int dst_sockets[2];
    int fd;

    memcpy(new_mac_packet, gmac_ipv4_udp_packet,
           sizeof(gmac_ipv4_udp_packet));
    new_mac_packet[0] = 0x02;
    new_mac_packet[1] = 0x00;
    new_mac_packet[2] = 0x00;
    new_mac_packet[3] = 0x12;
    new_mac_packet[4] = 0x34;
    new_mac_packet[5] = 0x56;

    fd = g_file_open_tmp("beaglev-ahead-gmac-filter-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, src_sockets), ==, 0);
    src = qtest_initf("-machine beaglev-ahead -bios none "
                      "-nic socket,fd=%d,model=gmac0,"
                      "mac=52:54:00:12:34:56", src_sockets[1]);
    close(src_sockets[1]);
    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, dst_sockets), ==, 0);
    dst = qtest_initf("-machine beaglev-ahead -bios none -incoming defer "
                      "-nic socket,fd=%d,model=gmac0,"
                      "mac=52:54:00:12:34:56", dst_sockets[1]);
    close(dst_sockets[1]);

    qtest_memset(src, GMAC_TEST_DATA_ADDR, 0xa5, 8192);
    gmac_write_desc(src, GMAC_TEST_DESC_ADDR, &desc);
    gmac_write_rx_extension(src, GMAC_TEST_DESC_ADDR, gmac_rx_extension);
    desc = (GMACDesc) {
        .des0 = BIT(31),
        .des1 = 2048,
        .des2 = GMAC_TEST_DATA2_ADDR,
    };
    gmac_write_desc(src, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                    &desc);
    desc = (GMACDesc) { 0 };
    gmac_write_desc(src,
                    GMAC_TEST_DESC_ADDR + 2 * GMAC_ENHANCED_DESC_STRIDE,
                    &desc);

    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_HI,
                  BIT(31) | 0x5634);
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_MAC0_ADDR_LO, 0x12000002);
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_MAC_ADDR_HI(31),
                  BIT(31) | 0x6655);
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_MAC_ADDR_LO(31),
                  0x44332211);
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_HASH_HIGH, 0x12345678);
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_HASH_LOW, 0x89abcdef);
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_VLAN_TAG,
                  BIT(16) | 0x0123);
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_FRAME_FILTER, BIT(10));
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_DMA_BUS_MODE,
                  0x00020100 | BIT(7));
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_DMA_RX_BASE_ADDR,
                  GMAC_TEST_DESC_ADDR);
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_DMA_INTR_ENA,
                  BIT(16) | BIT(6));
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_MAC_CONFIG,
                  DWMAC_MAC_CONFIG_IPC | BIT(2));
    qtest_writel(src, TH1520_GMAC0_BASE + DWMAC_DMA_CONTROL, BIT(1));

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC0_BASE + DWMAC_FRAME_FILTER), ==,
                    BIT(10));
    g_assert_cmphex(qtest_readl(dst, TH1520_GMAC0_BASE + DWMAC_HASH_HIGH), ==,
                    0x12345678);
    g_assert_cmphex(qtest_readl(dst, TH1520_GMAC0_BASE + DWMAC_HASH_LOW), ==,
                    0x89abcdef);
    g_assert_cmphex(qtest_readl(dst, TH1520_GMAC0_BASE + DWMAC_VLAN_TAG), ==,
                    BIT(16) | 0x0123);
    g_assert_cmphex(qtest_readl(dst, TH1520_GMAC0_BASE +
                                DWMAC_MAC_ADDR_HI(31)), ==,
                    BIT(31) | 0x6655);
    g_assert_cmphex(qtest_readl(dst, TH1520_GMAC0_BASE +
                                DWMAC_MAC_ADDR_LO(31)), ==, 0x44332211);
    g_assert_cmphex(qtest_readl(dst, TH1520_GMAC0_BASE +
                                DWMAC_MAC_CONFIG), ==,
                    DWMAC_MAC_CONFIG_IPC | BIT(2));

    gmac_send_two_packets(dst_sockets[0], old_mac_packet,
                          sizeof(old_mac_packet), new_mac_packet,
                          sizeof(new_mac_packet));
    g_assert_cmpint(gmac_wait_for_packet(dst, GMAC_TEST_DATA_ADDR,
                                         GMAC_TEST_DATA2_ADDR,
                                         new_mac_packet,
                                         sizeof(new_mac_packet)), ==, 0);
    gmac_assert_rx_frame(dst, GMAC_TEST_DESC_ADDR, GMAC_TEST_DATA_ADDR,
                         new_mac_packet, sizeof(new_mac_packet), 0);
    gmac_read_desc(dst, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0 &
                    (DWMAC_RX_DESC_ES | DWMAC_RX_DESC_FT |
                     DWMAC_RX_DESC_ESA), ==,
                    DWMAC_RX_DESC_FT | DWMAC_RX_DESC_ESA);
    gmac_read_rx_extension(dst, GMAC_TEST_DESC_ADDR, actual_extension);
    g_assert_cmphex(actual_extension[0], ==,
                    DWMAC_RX_DESC4_IPV4 | DWMAC_RX_DESC4_UDP);
    for (size_t i = 1; i < ARRAY_SIZE(gmac_rx_extension); i++) {
        g_assert_cmphex(actual_extension[i], ==, gmac_rx_extension[i]);
    }
    gmac_read_desc(dst, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE,
                   &desc);
    g_assert_cmphex(desc.des0 & BIT(31), ==, BIT(31));
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_GMAC0_BASE + DWMAC_DMA_HOST_RX_DESC),
                    ==, GMAC_TEST_DESC_ADDR + GMAC_ENHANCED_DESC_STRIDE);
    g_assert_cmphex(qtest_readl(dst, TH1520_GMAC0_BASE + DWMAC_DMA_STATUS) &
                    (BIT(16) | BIT(7) | BIT(6)), ==, 0);

    qtest_quit(dst);
    qtest_quit(src);
    close(dst_sockets[0]);
    close(src_sockets[0]);
    g_assert_cmpint(g_unlink(path), ==, 0);
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

static void test_storage_reset_outputs(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, TH1520_MISCSYS_QOM_PATH,
                                  "storage-reset");

    for (size_t output = 0;
         output < ARRAY_SIZE(th1520_storage_reset_test_outputs); output++) {
        const TH1520ResetTestOutput *info =
            &th1520_storage_reset_test_outputs[output];

        qtest_writel(qts, TH1520_MISCSYS_BASE + info->offset,
                      info->deasserted - 1);
        for (size_t line = 0;
             line < ARRAY_SIZE(th1520_storage_reset_test_outputs); line++) {
            g_assert_cmpint(qtest_get_irq(qts, line), ==, line == output);
        }
        qtest_writel(qts, TH1520_MISCSYS_BASE + info->offset,
                      info->deasserted);
        g_assert_false(qtest_get_irq(qts, output));
    }

    qtest_system_reset(qts);
    for (size_t i = 0;
         i < ARRAY_SIZE(th1520_storage_reset_test_outputs); i++) {
        g_assert_false(qtest_get_irq(qts, i));
    }
    qtest_quit(qts);
}

static void test_miscsys_clock_outputs(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, TH1520_MISCSYS_QOM_PATH,
                                  "clock-enable");
    qtest_system_reset(qts);
    for (size_t i = 0;
         i < ARRAY_SIZE(th1520_miscsys_clock_test_outputs); i++) {
        g_assert_true(qtest_get_irq(qts, i));
    }

    for (size_t output = 0;
         output < ARRAY_SIZE(th1520_miscsys_clock_test_outputs); output++) {
        const TH1520ClockGateTestOutput *info =
            &th1520_miscsys_clock_test_outputs[output];
        uint64_t address = TH1520_MISCSYS_BASE + info->offset;
        uint32_t original = qtest_readl(qts, address);

        qtest_writel(qts, address, original & ~info->mask);
        for (size_t line = 0;
             line < ARRAY_SIZE(th1520_miscsys_clock_test_outputs); line++) {
            g_assert_cmpint(qtest_get_irq(qts, line), ==, line != output);
        }
        qtest_writel(qts, address, original);
        g_assert_true(qtest_get_irq(qts, output));
    }

    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_BUS_CLK, 0);
    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_CLK, 0);
    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_EMMC_CLK, 0);
    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_SDIO0_CLK, 0);
    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_SDIO1_CLK, 0);
    for (size_t i = 0;
         i < ARRAY_SIZE(th1520_miscsys_clock_test_outputs); i++) {
        g_assert_false(qtest_get_irq(qts, i));
    }
    qtest_system_reset(qts);
    for (size_t i = 0;
         i < ARRAY_SIZE(th1520_miscsys_clock_test_outputs); i++) {
        g_assert_true(qtest_get_irq(qts, i));
    }
    qtest_quit(qts);
}

static void test_storage_reset_peripherals(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (size_t i = 0; i < ARRAY_SIZE(dwcmshc_controllers); i++) {
        const TH1520ResetTestOutput *reset =
            &th1520_storage_reset_test_outputs[i];
        size_t neighbor = (i + 1) % ARRAY_SIZE(dwcmshc_controllers);
        uint64_t base = dwcmshc_controllers[i].base;
        uint64_t neighbor_base = dwcmshc_controllers[neighbor].base;

        qtest_writeb(qts, base + DWCMSHC_MSHC_CTRL, 0x10);
        qtest_writeb(qts, neighbor_base + DWCMSHC_MSHC_CTRL, 0x11);
        g_assert_cmphex(qtest_readb(qts, base + DWCMSHC_MSHC_CTRL), ==,
                        0x10);
        g_assert_cmphex(qtest_readb(qts,
                                    neighbor_base + DWCMSHC_MSHC_CTRL), ==,
                        0x11);

        qtest_writel(qts, TH1520_MISCSYS_BASE + reset->offset,
                      reset->deasserted - 1);
        assert_dwcmshc_reset_state(qts, base);
        g_assert_cmphex(qtest_readb(qts,
                                    neighbor_base + DWCMSHC_MSHC_CTRL), ==,
                        0x11);
        qtest_writel(qts, TH1520_MISCSYS_BASE + reset->offset,
                      reset->deasserted);
    }

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(dwcmshc_controllers); i++) {
        assert_dwcmshc_reset_state(qts, dwcmshc_controllers[i].base);
    }
    qtest_quit(qts);
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

    /* Vendor U-Boot uses aligned byte accesses despite reg-io-width = <4>. */
    qtest_writeb(qts, DW_UART_SCR, 0xa5);
    g_assert_cmphex(qtest_readb(qts, DW_UART_SCR), ==, 0xa5);
    g_assert_cmphex(qtest_readb(qts, DW_UART_LSR), ==,
                    UART_LSR_THRE | UART_LSR_TEMT);

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

static void dw_i2c_pmic_write(QTestState *qts, uint8_t reg, uint8_t value)
{
    uint64_t base = TH1520_AON_I2C_BASE;

    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_PMIC_ADDR, 0);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, reg);
    qtest_writel(qts, base + DW_I2C_DATA_CMD,
                  value | DW_I2C_DATA_STOP);
    dw_i2c_disable(qts, base);
}

static void dw_i2c_pmic_set_pointer(QTestState *qts, uint8_t reg)
{
    uint64_t base = TH1520_AON_I2C_BASE;

    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_PMIC_ADDR, 0);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, reg | DW_I2C_DATA_STOP);
    dw_i2c_disable(qts, base);
}

static uint8_t dw_i2c_pmic_current_read(QTestState *qts)
{
    uint64_t base = TH1520_AON_I2C_BASE;
    uint8_t value;

    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_PMIC_ADDR, 0);
    qtest_writel(qts, base + DW_I2C_DATA_CMD,
                  DW_I2C_DATA_READ | DW_I2C_DATA_STOP);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_RXFLR), ==, 1);
    value = qtest_readl(qts, base + DW_I2C_DATA_CMD);
    dw_i2c_disable(qts, base);
    return value;
}

static uint8_t dw_i2c_pmic_read(QTestState *qts, uint8_t reg)
{
    dw_i2c_pmic_set_pointer(qts, reg);
    return dw_i2c_pmic_current_read(qts);
}

static uint8_t dw_i2c_pmic_restart_read(QTestState *qts, uint8_t reg)
{
    uint64_t base = TH1520_AON_I2C_BASE;
    uint8_t value;

    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_PMIC_ADDR, 0);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, reg);
    qtest_writel(qts, base + DW_I2C_DATA_CMD,
                  DW_I2C_DATA_READ | DW_I2C_DATA_RESTART |
                  DW_I2C_DATA_STOP);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_RXFLR), ==, 1);
    value = qtest_readl(qts, base + DW_I2C_DATA_CMD);
    dw_i2c_disable(qts, base);
    return value;
}

static void dw_i2c_pmic_vendor_write(QTestState *qts, uint8_t reg,
                                      uint8_t value)
{
    uint64_t base = TH1520_AON_I2C_BASE;

    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_PMIC_ADDR, 0);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, reg);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, value);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_TXFLR), ==, 0);
    g_assert_true(qtest_readl(qts, base + DW_I2C_RAW_INTR_STAT) &
                  DW_I2C_INTR_STOP_DET);
    dw_i2c_disable(qts, base);
}

static uint8_t dw_i2c_pmic_vendor_read(QTestState *qts, uint8_t reg)
{
    uint64_t base = TH1520_AON_I2C_BASE;
    uint8_t value;

    /* This is the public vendor SPL's separate send/read transaction. */
    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_PMIC_ADDR, 0);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, reg);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_TXFLR), ==, 0);
    g_assert_true(qtest_readl(qts, base + DW_I2C_RAW_INTR_STAT) &
                  DW_I2C_INTR_STOP_DET);
    dw_i2c_disable(qts, base);

    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_PMIC_ADDR, 0);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, DW_I2C_DATA_READ);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_RXFLR), ==, 1);
    value = qtest_readl(qts, base + DW_I2C_DATA_CMD);
    g_assert_true(qtest_readl(qts, base + DW_I2C_RAW_INTR_STAT) &
                  DW_I2C_INTR_STOP_DET);
    dw_i2c_disable(qts, base);
    return value;
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

static void test_dw_i2c_registers_at(QTestState *qts, uint64_t base)
{
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

static void test_aon_i2c_enable_mask(QTestState *qts)
{
    uint64_t base = TH1520_AON_I2C_BASE;

    /* Vendor SPL disables AON I2C by writing ~IC_ENABLE.ENABLE. */
    qtest_writel(qts, base + DW_I2C_ENABLE, 0xfffffffeU);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_ENABLE), ==, 0);
    g_assert_false(qtest_readl(qts, base + DW_I2C_RAW_INTR_STAT) &
                   DW_I2C_INTR_TX_ABRT);

    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_EEPROM_ADDR + 1, 0);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, 0xa5 | DW_I2C_DATA_STOP);
    g_assert_true(qtest_readl(qts, base + DW_I2C_RAW_INTR_STAT) &
                  DW_I2C_INTR_TX_ABRT);
    g_assert_true(qtest_readl(qts, base + DW_I2C_TX_ABRT_SOURCE) & BIT(0));
    qtest_readl(qts, base + DW_I2C_CLR_TX_ABRT);
    dw_i2c_disable(qts, base);
}

static void test_dw_i2c_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (size_t i = 0; i < ARRAY_SIZE(th1520_i2c_controllers); i++) {
        test_dw_i2c_registers_at(qts, th1520_i2c_controllers[i].base);
    }
    test_dw_i2c_registers_at(qts, TH1520_AON_I2C_BASE);
    test_aon_i2c_enable_mask(qts);

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(th1520_i2c_controllers); i++) {
        assert_dw_i2c_reset_state(qts, th1520_i2c_controllers[i].base);
    }
    assert_dw_i2c_reset_state(qts, TH1520_AON_I2C_BASE);
    qtest_quit(qts);
}

static void test_dw_i2c_eeprom(void)
{
    static const uint8_t page_wrap_data[] = { 0x12, 0x34, 0x56 };
    const uint8_t next_page = 0xa5;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x000), ==, 0xff);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0xfff), ==, 0xff);

    dw_i2c_eeprom_write(qts, 0x040, &next_page, 1);
    dw_i2c_eeprom_write(qts, 0x03f, page_wrap_data,
                        ARRAY_SIZE(page_wrap_data));
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x03f), ==, page_wrap_data[0]);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x020), ==, page_wrap_data[1]);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x021), ==, page_wrap_data[2]);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x040), ==, next_page);

    qtest_system_reset(qts);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x020), ==, page_wrap_data[1]);
    g_assert_cmphex(dw_i2c_eeprom_read(qts, 0x040), ==, next_page);
    qtest_quit(qts);
}

static void test_aon_i2c_pmic(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    /* Virtual PMIC reset defaults used by the vendor SPL's rail-A path. */
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_DVC_1), ==, 0);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_DVC_2), ==, 0);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_VBCORE1_A),
                    ==, 0);

    /* Replay the selected BeagleV Ahead SPL's CPU voltage sequence. */
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_VBCORE1_A, 0x12);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_VBCORE2_A, 0x34);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_VBIO_A, 0x56);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_VBCORE1_B,
                             dw_i2c_pmic_vendor_read(qts,
                                                      DA9063_REG_VBCORE1_A));
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_VBCORE2_B,
                             dw_i2c_pmic_vendor_read(qts,
                                                      DA9063_REG_VBCORE2_A));
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_VBIO_B,
                             dw_i2c_pmic_vendor_read(qts,
                                                      DA9063_REG_VBIO_A));
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_DVC_1, 0x03);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_DVC_2, 0x01);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_VBCORE1_A, 0x32);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_VBCORE2_A, 0x32);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_VBIO_A, 0x00);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_DVC_1, 0x00);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_DVC_2, 0x00);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_CONTROL_D, 0xff);
    dw_i2c_pmic_vendor_write(qts, DA9063_REG_CONTROL_D, 0xf8);

    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_VBCORE1_B),
                    ==, 0x12);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_VBCORE2_B),
                    ==, 0x34);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_VBIO_B),
                    ==, 0x56);
    g_assert_cmphex(dw_i2c_pmic_restart_read(qts, DA9063_REG_VBCORE1_A),
                    ==, 0x32);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_VBCORE2_A),
                    ==, 0x32);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_VBIO_A),
                    ==, 0x00);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_DVC_1), ==, 0);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_DVC_2), ==, 0);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_CONTROL_D),
                    ==, 0xf8);

    qtest_system_reset(qts);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_VBCORE1_A),
                    ==, 0);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_VBCORE1_B),
                    ==, 0);
    g_assert_cmphex(dw_i2c_pmic_vendor_read(qts, DA9063_REG_DVC_1), ==, 0);
    qtest_quit(qts);
}

static void test_aon_i2c_implicit_stop(void)
{
    uint64_t aon_base = TH1520_AON_I2C_BASE;
    uint64_t ap_base = TH1520_I2C0_BASE;
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    dw_i2c_enable(qts, aon_base, BEAGLEV_AHEAD_PMIC_ADDR, 0);
    qtest_writel(qts, aon_base + DW_I2C_DATA_CMD, DA9063_REG_DVC_1);
    g_assert_cmphex(qtest_readl(qts, aon_base + DW_I2C_TXFLR), ==, 0);
    g_assert_true(qtest_readl(qts, aon_base + DW_I2C_RAW_INTR_STAT) &
                  DW_I2C_INTR_STOP_DET);
    dw_i2c_disable(qts, aon_base);

    /* The source-derived completion mode is confined to the AON controller. */
    dw_i2c_enable(qts, ap_base, BEAGLEV_AHEAD_EEPROM_ADDR, 0);
    qtest_writel(qts, ap_base + DW_I2C_DATA_CMD, 0x12);
    g_assert_cmphex(qtest_readl(qts, ap_base + DW_I2C_TXFLR), ==, 0);
    g_assert_false(qtest_readl(qts, ap_base + DW_I2C_RAW_INTR_STAT) &
                   DW_I2C_INTR_STOP_DET);
    qtest_writel(qts, ap_base + DW_I2C_DATA_CMD, 0x34 | DW_I2C_DATA_STOP);
    dw_i2c_disable(qts, ap_base);
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

static void test_dw_i2c_interrupt(QTestState *qts, uint64_t base,
                                  uint32_t irq)
{
    qtest_writel(qts, C900_PLIC_PRIORITY(irq), 5);
    c900_plic_set_enable(qts, 1, irq, true);
    dw_i2c_enable(qts, base, BEAGLEV_AHEAD_EEPROM_ADDR + 1,
                  DW_I2C_INTR_TX_ABRT);
    qtest_writel(qts, base + DW_I2C_DATA_CMD, 0xa5 | DW_I2C_DATA_STOP);

    g_assert_true(qtest_readl(qts, base + DW_I2C_RAW_INTR_STAT) &
                  DW_I2C_INTR_TX_ABRT);
    g_assert_true(qtest_readl(qts, base + DW_I2C_INTR_STAT) &
                  DW_I2C_INTR_TX_ABRT);
    g_assert_true(qtest_readl(qts, base + DW_I2C_TX_ABRT_SOURCE) & BIT(0));
    g_assert_true(c900_plic_pending(qts, irq));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==, irq);
    assert_no_irq(qts);

    qtest_readl(qts, base + DW_I2C_CLR_TX_ABRT);
    g_assert_false(qtest_readl(qts, base + DW_I2C_INTR_STAT) &
                   DW_I2C_INTR_TX_ABRT);
    g_assert_cmphex(qtest_readl(qts, base + DW_I2C_TX_ABRT_SOURCE), ==, 0);
    qtest_writel(qts, C900_PLIC_CLAIM(1), irq);
    dw_i2c_disable(qts, base);
    c900_plic_set_enable(qts, 1, irq, false);
    assert_no_irq(qts);
}

static void test_dw_i2c_interrupts(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    for (size_t i = 0; i < ARRAY_SIZE(th1520_i2c_controllers); i++) {
        const TH1520I2CController *controller =
            &th1520_i2c_controllers[i];

        test_dw_i2c_interrupt(qts, controller->base, controller->irq);
    }
    test_dw_i2c_interrupt(qts, TH1520_AON_I2C_BASE, TH1520_AON_I2C_IRQ);

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

static void test_dw_timer_toggle_pwm(void)
{
    static const struct {
        const char *path;
        uint64_t base;
        unsigned int gate;
    } groups[] = {
        { TH1520_TIMER0_3_QOM_PATH, TH1520_TIMER0_3_BASE,
          TH1520_AP_CLOCK_GATE_TIMER0 },
        { TH1520_TIMER4_7_QOM_PATH, TH1520_TIMER4_7_BASE,
          TH1520_AP_CLOCK_GATE_TIMER1 },
    };

    for (size_t group = 0; group < ARRAY_SIZE(groups); group++) {
        const uint64_t base = groups[group].base;
        const uint64_t timer0 = base;
        const uint64_t timer1 = base + DW_TIMER_STRIDE;
        QTestState *qts = qtest_init(
            "-machine beaglev-ahead -bios none");

        qtest_irq_intercept_out_named(qts, groups[group].path, "toggle");
        g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);
        for (unsigned int channel = 0; channel < 4; channel++) {
            g_assert_false(qtest_get_irq(qts, channel));
        }

        /* LoadCount is low time; LoadCount2 is high time in PWM mode. */
        qtest_writel(qts, timer0 + DW_TIMER_LOAD_COUNT, 3);
        qtest_writel(qts, base + DW_TIMER_LOAD_COUNT2(0), 5);
        qtest_writel(qts, timer0 + DW_TIMER_CONTROL,
                      DW_TIMER_ENABLE | DW_TIMER_PERIODIC | DW_TIMER_PWM);
        g_assert_false(qtest_get_irq(qts, 0));
        qtest_clock_step(qts, 3 * TH1520_TIMER_TICK_NS - 1);
        g_assert_false(qtest_get_irq(qts, 0));
        qtest_clock_step(qts, 1);
        g_assert_true(qtest_get_irq(qts, 0));
        qtest_readl(qts, timer0 + DW_TIMER_EOI);

        /* The AP leaf gate freezes both the counter and output phase. */
        th1520_set_ap_clock_gate(
            qts, th1520_ap_clock_gate_test_outputs[groups[group].gate].offset,
            th1520_ap_clock_gate_test_outputs[groups[group].gate].mask,
            false);
        qtest_clock_step(qts, 100 * TH1520_TIMER_TICK_NS);
        g_assert_true(qtest_get_irq(qts, 0));
        g_assert_cmphex(qtest_readl(qts, timer0 + DW_TIMER_INT_STATUS), ==,
                        0);
        th1520_set_ap_clock_gate(
            qts, th1520_ap_clock_gate_test_outputs[groups[group].gate].offset,
            th1520_ap_clock_gate_test_outputs[groups[group].gate].mask,
            true);

        qtest_clock_step(qts, 6 * TH1520_TIMER_TICK_NS - 1);
        g_assert_true(qtest_get_irq(qts, 0));
        qtest_clock_step(qts, 1);
        g_assert_false(qtest_get_irq(qts, 0));
        qtest_readl(qts, timer0 + DW_TIMER_EOI);

        /* A low-phase write changes the following high interval only. */
        qtest_writel(qts, base + DW_TIMER_LOAD_COUNT2(0), 2);
        qtest_clock_step(qts, 4 * TH1520_TIMER_TICK_NS - 1);
        g_assert_false(qtest_get_irq(qts, 0));
        qtest_clock_step(qts, 1);
        g_assert_true(qtest_get_irq(qts, 0));
        qtest_readl(qts, timer0 + DW_TIMER_EOI);
        qtest_clock_step(qts, 3 * TH1520_TIMER_TICK_NS - 1);
        g_assert_true(qtest_get_irq(qts, 0));
        qtest_clock_step(qts, 1);
        g_assert_false(qtest_get_irq(qts, 0));
        qtest_writel(qts, timer0 + DW_TIMER_CONTROL, 0);

        /* Without PWM, LoadCount2 is ignored and both halves are equal. */
        qtest_writel(qts, timer1 + DW_TIMER_LOAD_COUNT, 2);
        qtest_writel(qts, base + DW_TIMER_LOAD_COUNT2(1), 7);
        qtest_writel(qts, timer1 + DW_TIMER_CONTROL,
                      DW_TIMER_ENABLE | DW_TIMER_PERIODIC |
                      DW_TIMER_INT_MASK);
        qtest_clock_step(qts, 2 * TH1520_TIMER_TICK_NS);
        g_assert_true(qtest_get_irq(qts, 1));
        g_assert_cmphex(qtest_readl(qts, timer1 + DW_TIMER_INT_STATUS), ==,
                        0);
        g_assert_true(qtest_readl(qts, base + DW_TIMERS_RAW_INT_STATUS) &
                      BIT(1));
        qtest_readl(qts, timer1 + DW_TIMER_EOI);
        qtest_clock_step(qts, 3 * TH1520_TIMER_TICK_NS - 1);
        g_assert_true(qtest_get_irq(qts, 1));
        qtest_clock_step(qts, 1);
        g_assert_false(qtest_get_irq(qts, 1));

        qtest_writel(qts, timer1 + DW_TIMER_CONTROL, DW_TIMER_PERIODIC);
        for (unsigned int channel = 0; channel < 4; channel++) {
            g_assert_false(qtest_get_irq(qts, channel));
        }
        qtest_system_reset(qts);
        for (unsigned int channel = 0; channel < 4; channel++) {
            g_assert_false(qtest_get_irq(qts, channel));
        }
        qtest_quit(qts);
    }
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

static void assert_dw_wdt_reset_state(QTestState *qts, uint64_t base)
{
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_CR), ==, DW_WDT_RMOD);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_TORR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_CCVR), ==, 0xffff);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_CRR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_STAT), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_EOI), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_COMP_PARAM_5), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_COMP_PARAM_4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_COMP_PARAM_3), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_COMP_PARAM_2), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_COMP_PARAM_1), ==,
                    DW_WDT_FIXED_TOP);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_COMP_VERSION), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DW_WDT_COMP_TYPE), ==,
                    DW_WDT_COMPONENT_TYPE);
}

static void test_dw_wdt_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (size_t i = 0; i < ARRAY_SIZE(th1520_wdts); i++) {
        uint64_t base = th1520_wdts[i].base;

        assert_dw_wdt_reset_state(qts, base);
        qtest_writel(qts, base + DW_WDT_TORR, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, base + DW_WDT_TORR), ==, 0xff);
        qtest_writel(qts, base + DW_WDT_CR, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, base + DW_WDT_CR), ==, 0x1f);

        /* WDT_EN is sticky until a reset input or system reset. */
        qtest_writel(qts, base + DW_WDT_CR, 0);
        g_assert_cmphex(qtest_readl(qts, base + DW_WDT_CR), ==,
                        DW_WDT_ENABLE);
    }

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(th1520_wdts); i++) {
        assert_dw_wdt_reset_state(qts, th1520_wdts[i].base);
    }
    qtest_quit(qts);
}

static void test_dw_wdt_timing(void)
{
    const TH1520WDT *wdt = &th1520_wdts[0];
    QTestState *qts = qtest_init(
        "-machine beaglev-ahead -bios none -watchdog-action none");
    QDict *event;
    QDict *data;

    g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);

    /* TOP_INIT=1 is used at enable; later stages and kicks use TOP=0. */
    qtest_writel(qts, wdt->base + DW_WDT_TORR, 0x10);
    qtest_writel(qts, wdt->base + DW_WDT_CR,
                  DW_WDT_RMOD | DW_WDT_ENABLE);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_CCVR), ==,
                    2 * DW_WDT_TOP0_COUNT);
    qtest_clock_step(qts, 2 * DW_WDT_TOP0_NS - 1);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_STAT), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_STAT), ==, 1);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_CCVR), ==,
                    DW_WDT_TOP0_COUNT);

    /* EOI deasserts the interrupt but does not restart the second period. */
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_EOI), ==, 1);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_STAT), ==, 0);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_CCVR), ==,
                    DW_WDT_TOP0_COUNT);
    qtest_clock_step(qts, DW_WDT_TOP0_NS - 1);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_STAT), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_STAT), ==, 1);

    /* Only the documented magic value clears and reloads the watchdog. */
    qtest_writel(qts, wdt->base + DW_WDT_CRR, DW_WDT_RESTART - 1);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_STAT), ==, 1);
    qtest_writel(qts, wdt->base + DW_WDT_CRR, DW_WDT_RESTART);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_STAT), ==, 0);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_CCVR), ==,
                    DW_WDT_TOP0_COUNT);

    /* An uncleared first-stage interrupt makes the next expiry reset. */
    qtest_clock_step(qts, DW_WDT_TOP0_NS);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_STAT), ==, 1);
    qtest_clock_step(qts, DW_WDT_TOP0_NS);
    event = qtest_qmp_eventwait_ref(qts, "WATCHDOG");
    data = qdict_get_qdict(event, "data");
    g_assert_cmpstr(qdict_get_str(data, "action"), ==, "none");
    qobject_unref(event);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_STAT), ==, 1);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_CCVR), ==, 0);

    qtest_quit(qts);
}

static void test_dw_wdt_interrupt_route(const void *opaque)
{
    const TH1520WDT *wdt = opaque;
    QTestState *qts = qtest_init(
        "-machine beaglev-ahead -bios none -watchdog-action none");

    g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);
    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(wdt->irq), 5);
    c900_plic_set_enable(qts, 1, wdt->irq, true);

    qtest_writel(qts, wdt->base + DW_WDT_TORR, 0);
    qtest_writel(qts, wdt->base + DW_WDT_CR,
                  DW_WDT_RMOD | DW_WDT_ENABLE);
    qtest_clock_step(qts, DW_WDT_TOP0_NS);
    g_assert_true(c900_plic_pending(qts, wdt->irq));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==, wdt->irq);
    assert_no_irq(qts);
    g_assert_cmphex(qtest_readl(qts, wdt->base + DW_WDT_EOI), ==, 1);
    qtest_writel(qts, C900_PLIC_CLAIM(1), wdt->irq);
    g_assert_false(c900_plic_pending(qts, wdt->irq));
    assert_no_irq(qts);

    qtest_quit(qts);
}

static void test_dw_wdt_reset_outputs(void)
{
    QTestState *qts = qtest_init(
        "-machine beaglev-ahead -bios none -watchdog-action none");

    for (size_t i = 0; i < ARRAY_SIZE(th1520_wdts); i++) {
        const TH1520WDT *wdt = &th1520_wdts[i];
        const TH1520WDT *other = &th1520_wdts[1 - i];

        qtest_writel(qts, wdt->base + DW_WDT_TORR, 0x21 + i);
        qtest_writel(qts, wdt->base + DW_WDT_CR, DW_WDT_ENABLE);
        qtest_writel(qts, other->base + DW_WDT_TORR, 0x43 + i);
        qtest_writel(qts, TH1520_AP_RESET_BASE + wdt->reset_offset, 0);
        assert_dw_wdt_reset_state(qts, wdt->base);
        g_assert_cmphex(qtest_readl(qts, other->base + DW_WDT_TORR), ==,
                        0x43 + i);
        qtest_writel(qts, TH1520_AP_RESET_BASE + wdt->reset_offset, 1);
    }

    qtest_quit(qts);
}

static void test_dw_wdt_action_reset(void)
{
    const TH1520WDT *wdt = &th1520_wdts[0];
    QTestState *qts = qtest_init(
        "-machine beaglev-ahead -bios none -watchdog-action reset");
    QDict *event;
    QDict *data;

    g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);
    qtest_writel(qts, wdt->base + DW_WDT_TORR, 0);
    qtest_writel(qts, wdt->base + DW_WDT_CR, DW_WDT_ENABLE);
    qtest_clock_step(qts, DW_WDT_TOP0_NS);

    event = qtest_qmp_eventwait_ref(qts, "WATCHDOG");
    data = qdict_get_qdict(event, "data");
    g_assert_cmpstr(qdict_get_str(data, "action"), ==, "reset");
    qobject_unref(event);
    qtest_qmp_eventwait(qts, "RESET");
    assert_dw_wdt_reset_state(qts, wdt->base);

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

/*
 * Board peers drive three GPIO inputs from reset: the GMAC0 PHY holds its
 * active-low interrupt deasserted on GPIO3_22, and the AP6203BM module holds
 * WL_HW_OOB and HOST_WAKE_BT low on GPIO2_25 and GPIO2_29.  The controller
 * model lets an external driver win over the internal one, so a bank with
 * externally driven pins does not read back everything the guest drives.
 */
static uint32_t dw_gpio_external_driven(
    const TH1520GPIOController *controller)
{
    if (controller->base == TH1520_GPIO2_BASE) {
        return AP6203BM_HOST_WAKE_MASK;
    }
    if (controller->base == TH1520_GPIO3_BASE) {
        return BIT(TH1520_GMAC_PHY_IRQ_GPIO);
    }
    return 0;
}

static uint32_t dw_gpio_external_level(const TH1520GPIOController *controller)
{
    return controller->base == TH1520_GPIO3_BASE ?
           BIT(TH1520_GMAC_PHY_IRQ_GPIO) : 0;
}

static void assert_dw_gpio_reset_state(
    QTestState *qts, const TH1520GPIOController *controller)
{
    uint64_t base = controller->base;
    uint32_t external_level = dw_gpio_external_level(controller);

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
    g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_EXT_PORTA), ==,
                    external_level);
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
        uint32_t external_driven = dw_gpio_external_driven(controller);
        uint32_t external_level = dw_gpio_external_level(controller);
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
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_EXT_PORTA), ==,
                        external_level);

        qtest_writel(qts, base + DW_GPIO_SWPORTA_DDR, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_SWPORTA_DDR), ==,
                        mask);
        g_assert_cmphex(qtest_readl(qts, base + DW_GPIO_EXT_PORTA), ==,
                        (mask & ~external_driven) | external_level);

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
        uint32_t pin = controller->base == TH1520_GPIO3_BASE ?
            controller->ngpios - 2 : controller->ngpios - 1;
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

static const uint8_t dwcmshc_emmc_tuning_pattern[] = {
    0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
    0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc,
    0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff,
    0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff,
    0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd,
    0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb,
    0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff,
    0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff,
    0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00,
    0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc,
    0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff,
    0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee,
    0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd,
    0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff,
    0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff,
    0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee,
};

QEMU_BUILD_BUG_ON(sizeof(dwcmshc_emmc_tuning_pattern) !=
                  DWCMSHC_EMMC_TUNING_SIZE);

static const uint8_t dwcmshc_emmc_tuning_pattern_4bit[] = {
    0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc,
    0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
    0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb,
    0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
    0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c,
    0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
    0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff,
    0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde,
};

QEMU_BUILD_BUG_ON(sizeof(dwcmshc_emmc_tuning_pattern_4bit) !=
                  DWCMSHC_EMMC_TUNING_SIZE_4BIT);

static const uint8_t dwcmshc_sd_tuning_pattern[] = {
    0xff, 0x0f, 0xff, 0x00, 0x0f, 0xfc, 0xc3, 0xcc,
    0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
    0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb,
    0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
    0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c,
    0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
    0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff,
    0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde,
};

QEMU_BUILD_BUG_ON(sizeof(dwcmshc_sd_tuning_pattern) !=
                  DWCMSHC_EMMC_TUNING_SIZE_4BIT);

static void dwcmshc_read_fifo(QTestState *qts, uint64_t base,
                              uint8_t *data, size_t size)
{
    g_assert_cmpuint(size % sizeof(uint32_t), ==, 0);

    for (size_t i = 0; i < size; i += sizeof(uint32_t)) {
        stl_le_p(&data[i], qtest_readl(qts, base + SDHC_BDATA));
    }
}

static void dwcmshc_read_ext_csd(QTestState *qts, uint64_t base,
                                 uint8_t ext_csd[DWCMSHC_BLOCK_SIZE])
{
    sdhci_cmd_regs(qts, base, DWCMSHC_BLOCK_SIZE, 1, 0,
                   SDHC_TRNS_READ | SDHC_TRNS_BLK_CNT_EN,
                   (8 << 8) | SDHC_CMD_RESPONSE | SDHC_CMD_DATA_PRESENT);
    dwcmshc_read_fifo(qts, base, ext_csd, DWCMSHC_BLOCK_SIZE);
}

static uint32_t dwcmshc_emmc_switch(QTestState *qts, uint64_t base,
                                    uint8_t index, uint8_t value)
{
    uint32_t argument = (3U << 24) | (index << 16) | (value << 8);

    sdhci_cmd_regs(qts, base, 0, 0, argument, 0,
                   (6 << 8) | SDHC_CMD_RESPONSE);
    return qtest_readl(qts, base + SDHC_RSPREG0);
}

static uint32_t dwcmshc_emmc_status(QTestState *qts, uint64_t base)
{
    sdhci_cmd_regs(qts, base, 0, 0, 1 << 16, 0,
                   (13 << 8) | SDHC_CMD_RESPONSE);
    return qtest_readl(qts, base + SDHC_RSPREG0);
}

static void dwcmshc_emmc_switch_ok(QTestState *qts, uint64_t base,
                                   uint8_t index, uint8_t value)
{
    g_assert_cmphex(dwcmshc_emmc_switch(qts, base, index, value) &
                    EMMC_STATUS_SWITCH_ERROR, ==, 0);
}

static void dwcmshc_emmc_switch_rejected(QTestState *qts, uint64_t base,
                                         uint8_t index, uint8_t value)
{
    uint32_t response = dwcmshc_emmc_switch(qts, base, index, value);

    g_assert_cmphex(response & EMMC_STATUS_SWITCH_ERROR, ==,
                    EMMC_STATUS_SWITCH_ERROR);
    response = dwcmshc_emmc_status(qts, base);
    g_assert_cmphex(response & EMMC_STATUS_SWITCH_ERROR, ==,
                    EMMC_STATUS_SWITCH_ERROR);
    response = dwcmshc_emmc_status(qts, base);
    g_assert_cmphex(response & EMMC_STATUS_SWITCH_ERROR, ==, 0);
}

static void dwcmshc_assert_emmc_mode(QTestState *qts, uint64_t base,
                                     uint8_t bus_width, uint8_t hs_timing)
{
    uint8_t ext_csd[DWCMSHC_BLOCK_SIZE];

    dwcmshc_read_ext_csd(qts, base, ext_csd);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_BUS_WIDTH], ==, bus_width);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_HS_TIMING], ==, hs_timing);
}

static void dwcmshc_emmc_enter_hs200(QTestState *qts, uint64_t base)
{
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_HS_TIMING,
                           EMMC_HS_TIMING_HS);
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_BUS_WIDTH,
                           EMMC_BUS_WIDTH_8);
    qtest_writeb(qts, base + SDHC_HOSTCTL,
                 qtest_readb(qts, base + SDHC_HOSTCTL) |
                 SDHC_CTRL_8BITBUS);
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_HS_TIMING,
                           EMMC_HS_TIMING_HS200);
    dwcmshc_assert_emmc_mode(qts, base, EMMC_BUS_WIDTH_8,
                             EMMC_HS_TIMING_HS200);
}

static void dwcmshc_issue_emmc_tuning(QTestState *qts, uint64_t base,
                                      uint16_t size)
{
    sdhci_cmd_regs(qts, base, size, 0, 0, SDHC_TRNS_READ,
                   (21 << 8) | SDHC_CMD_RESPONSE | SDHC_CMD_DATA_PRESENT);
}

static void dwcmshc_read_emmc_tuning_pattern(QTestState *qts, uint64_t base,
                                             const uint8_t *expected,
                                             size_t size)
{
    uint8_t actual[DWCMSHC_EMMC_TUNING_SIZE];

    g_assert_cmpuint(size, <=, sizeof(actual));
    dwcmshc_issue_emmc_tuning(qts, base, size);
    dwcmshc_read_fifo(qts, base, actual, size);
    g_assert_cmpmem(actual, size, expected, size);
}

static void dwcmshc_read_emmc_tuning(QTestState *qts, uint64_t base)
{
    dwcmshc_read_emmc_tuning_pattern(qts, base,
                                     dwcmshc_emmc_tuning_pattern,
                                     sizeof(dwcmshc_emmc_tuning_pattern));
}

static void dwcmshc_issue_sd_tuning(QTestState *qts, uint64_t base,
                                    uint16_t size)
{
    sdhci_cmd_regs(qts, base, size, 0, 0, SDHC_TRNS_READ,
                   (19 << 8) | SDHC_CMD_RESPONSE | SDHC_CMD_DATA_PRESENT);
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

static void test_dwcmshc_sd_tuning(void)
{
    const uint64_t base = TH1520_SDIO0_BASE;
    const uint16_t tuning_bits = R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK |
                                 R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK;
    uint8_t actual[sizeof(dwcmshc_sd_tuning_pattern)];
    g_autofree char *path = dwcmshc_create_image(NULL, 0);
    QTestState *qts = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=1,file=%s,format=raw,auto-read-only=off",
        path);

    dwcmshc_init_sd(qts, base);
    qtest_writeb(qts, base + SDHC_HOSTCTL,
                 qtest_readb(qts, base + SDHC_HOSTCTL) |
                 SDHC_CTRL_4BITBUS);

    /* CMD19 exposes the SD-specific 64-byte pattern during normal PIO. */
    dwcmshc_issue_sd_tuning(qts, base, sizeof(actual));
    dwcmshc_read_fifo(qts, base, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), dwcmshc_sd_tuning_pattern,
                    sizeof(dwcmshc_sd_tuning_pattern));

    /* Execute Tuning consumes that block internally and marks it sampled. */
    qtest_writew(qts, base + SDHC_NORINTSTS, UINT16_MAX);
    qtest_writew(qts, base + SDHC_NORINTSTSEN, SDHC_NISEN_RBUFRDY);
    qtest_writew(qts, base + SDHC_HOSTCTL2,
                 R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK);
    dwcmshc_issue_sd_tuning(qts, base, sizeof(actual));
    g_assert_cmphex(qtest_readw(qts, base + SDHC_HOSTCTL2) & tuning_bits, ==,
                    R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_NORINTSTS) &
                    (SDHC_NIS_CMDCMP | SDHC_NIS_TRSCMP |
                     SDHC_NIS_RBUFRDY), ==, SDHC_NIS_RBUFRDY);
    g_assert_cmphex(qtest_readl(qts, base + SDHC_PRNSTS) &
                    (SDHC_DATA_AVAILABLE | SDHC_DATA_INHIBIT |
                     SDHC_DAT_LINE_ACTIVE | SDHC_DOING_READ), ==, 0);

    /* A short CMD19 transfer must not produce a false tuning success. */
    qtest_system_reset(qts);
    dwcmshc_init_sd(qts, base);
    qtest_writew(qts, base + SDHC_NORINTSTSEN, SDHC_NISEN_RBUFRDY);
    qtest_writew(qts, base + SDHC_HOSTCTL2,
                 R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK);
    dwcmshc_issue_sd_tuning(qts, base, sizeof(actual) / 2);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_HOSTCTL2) & tuning_bits, ==,
                    R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK);
    g_assert_true(qtest_readl(qts, base + SDHC_PRNSTS) &
                  SDHC_DATA_AVAILABLE);

    /* An ordinary data command cannot satisfy Execute Tuning either. */
    qtest_system_reset(qts);
    dwcmshc_init_sd(qts, base);
    qtest_writew(qts, base + SDHC_HOSTCTL2,
                 R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK);
    sdhci_cmd_regs(qts, base, sizeof(actual), 0, 0, SDHC_TRNS_READ,
                   (17 << 8) | SDHC_CMD_RESPONSE | SDHC_CMD_DATA_PRESENT);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_HOSTCTL2) & tuning_bits, ==,
                    R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK);
    g_assert_true(qtest_readl(qts, base + SDHC_PRNSTS) &
                  SDHC_DATA_AVAILABLE);

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_dwcmshc_emmc_hs400_profile(void)
{
    const uint64_t base = TH1520_EMMC_BASE;
    const uint16_t tuning_bits = R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK |
                                 R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK;
    const uint32_t transfer_state = SDHC_DATA_INHIBIT |
                                    SDHC_DAT_LINE_ACTIVE |
                                    SDHC_DOING_READ |
                                    SDHC_DATA_AVAILABLE;
    uint8_t ext_csd[DWCMSHC_BLOCK_SIZE];
    g_autofree char *path = dwcmshc_create_image(NULL, 0);
    QTestState *qts = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=0,file=%s,format=raw,auto-read-only=off",
        path);
    uint16_t hostctl2;
    uint8_t hostctl1;

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(TH1520_EMMC_IRQ), 5);
    c900_plic_set_enable(qts, 1, TH1520_EMMC_IRQ, true);

    dwcmshc_init_emmc(qts, base);
    dwcmshc_read_ext_csd(qts, base, ext_csd);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_REV], ==, 8);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_CARD_TYPE], ==, 0x57);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_GENERIC_CMD6_TIME], ==, 0x32);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_STROBE_SUPPORT], ==, 0);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_BUS_WIDTH], ==,
                    EMMC_BUS_WIDTH_1);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_HS_TIMING], ==,
                    EMMC_HS_TIMING_LEGACY);

    /* STROBE_SUPPORT is read-only and stays at the unadvertised value. */
    dwcmshc_emmc_switch_rejected(qts, base,
                                  EMMC_EXT_CSD_STROBE_SUPPORT, 1);
    dwcmshc_read_ext_csd(qts, base, ext_csd);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_STROBE_SUPPORT], ==, 0);

    /* DDR width and HS400 are illegal directly from legacy timing. */
    dwcmshc_emmc_switch_rejected(qts, base, EMMC_EXT_CSD_BUS_WIDTH,
                                  EMMC_BUS_WIDTH_DDR_8);
    dwcmshc_assert_emmc_mode(qts, base, EMMC_BUS_WIDTH_1,
                             EMMC_HS_TIMING_LEGACY);
    dwcmshc_emmc_switch_rejected(qts, base, EMMC_EXT_CSD_HS_TIMING,
                                  EMMC_HS_TIMING_HS400);
    dwcmshc_assert_emmc_mode(qts, base, EMMC_BUS_WIDTH_1,
                             EMMC_HS_TIMING_LEGACY);

    /* CMD21 selects the 64-byte pattern while the card is four bits wide. */
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_HS_TIMING,
                           EMMC_HS_TIMING_HS);
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_BUS_WIDTH,
                           EMMC_BUS_WIDTH_4);
    hostctl1 = qtest_readb(qts, base + SDHC_HOSTCTL);
    hostctl1 &= ~SDHC_CTRL_8BITBUS;
    qtest_writeb(qts, base + SDHC_HOSTCTL, hostctl1 | SDHC_CTRL_4BITBUS);
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_HS_TIMING,
                           EMMC_HS_TIMING_HS200);
    dwcmshc_assert_emmc_mode(qts, base, EMMC_BUS_WIDTH_4,
                             EMMC_HS_TIMING_HS200);
    dwcmshc_read_emmc_tuning_pattern(qts, base,
                                     dwcmshc_emmc_tuning_pattern_4bit,
                                     sizeof(dwcmshc_emmc_tuning_pattern_4bit));

    /* Return through HS before selecting the eight-bit HS200 mode. */
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_HS_TIMING,
                           EMMC_HS_TIMING_HS);
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_BUS_WIDTH,
                           EMMC_BUS_WIDTH_8);
    hostctl1 = qtest_readb(qts, base + SDHC_HOSTCTL);
    hostctl1 &= ~SDHC_CTRL_4BITBUS;
    qtest_writeb(qts, base + SDHC_HOSTCTL, hostctl1 | SDHC_CTRL_8BITBUS);
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_HS_TIMING,
                           EMMC_HS_TIMING_HS200);
    dwcmshc_assert_emmc_mode(qts, base, EMMC_BUS_WIDTH_8,
                             EMMC_HS_TIMING_HS200);
    dwcmshc_read_emmc_tuning(qts, base);

    /*
     * During Execute Tuning, SDHCI consumes the card's fixed tuning block
     * internally and reports only Buffer Read Ready.  No FIFO data or normal
     * transfer state is exposed to software.
     */
    qtest_writew(qts, base + SDHC_NORINTSTS, UINT16_MAX);
    qtest_writew(qts, base + SDHC_NORINTSTSEN, SDHC_NISEN_RBUFRDY);
    qtest_writew(qts, base + SDHC_NORINTSIGEN, SDHC_NIS_RBUFRDY);
    qtest_writew(qts, base + SDHC_HOSTCTL2,
                  DWCMSHC_UHS_MODE_HS200 |
                  R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK);
    dwcmshc_issue_emmc_tuning(qts, base, DWCMSHC_EMMC_TUNING_SIZE);

    hostctl2 = qtest_readw(qts, base + SDHC_HOSTCTL2);
    g_assert_cmphex(hostctl2 & tuning_bits, ==,
                    R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK);
    g_assert_cmphex(hostctl2 & R_SDHC_HOSTCTL2_UHS_MODE_SEL_MASK, ==,
                    DWCMSHC_UHS_MODE_HS200);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_NORINTSTS) &
                    (SDHC_NIS_CMDCMP | SDHC_NIS_TRSCMP |
                     SDHC_NIS_RBUFRDY), ==, SDHC_NIS_RBUFRDY);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_ERRINTSTS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SDHC_PRNSTS) & transfer_state,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SDHC_BDATA), ==, 0);
    g_assert_true(c900_plic_pending(qts, TH1520_EMMC_IRQ));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    TH1520_EMMC_IRQ);
    assert_no_irq(qts);

    /* A data-line reset clears the tuning IRQ, but not the tuned clock. */
    qtest_writeb(qts, base + SDHC_SWRST, SDHC_RESET_DATA);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_NORINTSTS) &
                    SDHC_NIS_RBUFRDY, ==, 0);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_HOSTCTL2) & tuning_bits, ==,
                    R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK);
    qtest_writel(qts, C900_PLIC_CLAIM(1), TH1520_EMMC_IRQ);
    g_assert_false(c900_plic_pending(qts, TH1520_EMMC_IRQ));
    assert_no_irq(qts);

    /* Internal tuning consumption leaves the card ready for another CMD21. */
    dwcmshc_read_emmc_tuning(qts, base);

    /* A full SDHCI reset clears both tuning-control bits. */
    qtest_writeb(qts, base + SDHC_SWRST, SDHC_RESET_ALL);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_HOSTCTL2) & tuning_bits, ==,
                    0);

    dwcmshc_init_emmc(qts, base);
    dwcmshc_emmc_enter_hs200(qts, base);

    /* Required HS200 -> HS -> DDR8 -> HS400 transition sequence. */
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_HS_TIMING,
                           EMMC_HS_TIMING_HS);
    dwcmshc_assert_emmc_mode(qts, base, EMMC_BUS_WIDTH_8,
                             EMMC_HS_TIMING_HS);
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_BUS_WIDTH,
                           EMMC_BUS_WIDTH_DDR_8);
    dwcmshc_assert_emmc_mode(qts, base, EMMC_BUS_WIDTH_DDR_8,
                             EMMC_HS_TIMING_HS);
    dwcmshc_emmc_switch_ok(qts, base, EMMC_EXT_CSD_HS_TIMING,
                           EMMC_HS_TIMING_HS400);
    dwcmshc_assert_emmc_mode(qts, base, EMMC_BUS_WIDTH_DDR_8,
                             EMMC_HS_TIMING_HS400);

    /* Enhanced strobe is not advertised by the Ahead profile. */
    dwcmshc_emmc_switch_rejected(qts, base, EMMC_EXT_CSD_BUS_WIDTH,
                                  EMMC_BUS_WIDTH_DDR_8_STROBE);
    dwcmshc_assert_emmc_mode(qts, base, EMMC_BUS_WIDTH_DDR_8,
                             EMMC_HS_TIMING_HS400);

    qtest_system_reset(qts);
    dwcmshc_init_emmc(qts, base);
    dwcmshc_read_ext_csd(qts, base, ext_csd);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_REV], ==, 8);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_CARD_TYPE], ==, 0x57);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_GENERIC_CMD6_TIME], ==, 0x32);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_STROBE_SUPPORT], ==, 0);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_BUS_WIDTH], ==,
                    EMMC_BUS_WIDTH_1);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_HS_TIMING], ==,
                    EMMC_HS_TIMING_LEGACY);

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_dwcmshc_emmc_v18(void)
{
    const uint64_t base = TH1520_EMMC_BASE;
    g_autofree char *image = dwcmshc_create_image(NULL, 0);
    g_autofree char *log_path = NULL;
    g_autofree char *log = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-emmc-v18-XXXXXX", &log_path,
                         &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);

    qts = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=0,file=%s,format=raw,auto-read-only=off "
        "-d guest_errors -D %s",
        image, log_path);
    qtest_writew(qts, base + SDHC_HOSTCTL2,
                 R_SDHC_HOSTCTL2_V18_ENA_MASK);
    g_assert_cmphex(qtest_readw(qts, base + SDHC_HOSTCTL2) &
                    R_SDHC_HOSTCTL2_V18_ENA_MASK, ==,
                    R_SDHC_HOSTCTL2_V18_ENA_MASK);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(log_path, &log, NULL, &error));
    g_assert_no_error(error);
    g_assert_null(strstr(log, "SD card voltage not supported"));
    g_assert_cmpint(g_unlink(log_path), ==, 0);
    g_assert_cmpint(g_unlink(image), ==, 0);
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

static void test_th1520_usb_registers(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    for (size_t i = 0; i < ARRAY_SIZE(th1520_miscsys_regs); i++) {
        const TH1520USBReg *reg = &th1520_miscsys_regs[i];

        g_assert_cmphex(qtest_readl(qts, TH1520_MISCSYS_BASE + reg->offset),
                        ==, reg->reset);
        qtest_writel(qts, TH1520_MISCSYS_BASE + reg->offset, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, TH1520_MISCSYS_BASE + reg->offset),
                        ==, (reg->reset & ~reg->write_mask) |
                            reg->write_mask);
        qtest_writel(qts, TH1520_MISCSYS_BASE + reg->offset, 0);
        g_assert_cmphex(qtest_readl(qts, TH1520_MISCSYS_BASE + reg->offset),
                        ==, reg->reset & ~reg->write_mask);
    }

    qtest_system_reset(qts);
    for (size_t i = 0; i < ARRAY_SIZE(th1520_usb_drd_regs); i++) {
        const TH1520USBReg *reg = &th1520_usb_drd_regs[i];

        g_assert_cmphex(qtest_readl(qts, TH1520_USB_DRD_BASE + reg->offset),
                        ==, reg->reset);
        if (!reg->write_mask) {
            continue;
        }
        qtest_writel(qts, TH1520_USB_DRD_BASE + reg->offset, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, TH1520_USB_DRD_BASE + reg->offset),
                        ==, (reg->reset & ~reg->write_mask) |
                            reg->write_mask);
        qtest_writel(qts, TH1520_USB_DRD_BASE + reg->offset, 0);
        g_assert_cmphex(qtest_readl(qts, TH1520_USB_DRD_BASE + reg->offset),
                        ==, reg->reset & ~reg->write_mask);
    }

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_GCTL), ==,
                    TH1520_USB_GCTL_RESET);
    /* Generic QEMU DWC3 synthesis values are provisional for TH1520. */
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_GSNPSID), ==,
                    TH1520_USB_GSNPSID_QEMU);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_GHWPARAMS0), ==,
                    0x40204049);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_GHWPARAMS1), ==,
                    0x0222493b);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_DCTL), ==, 0);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_DCTL,
                  TH1520_USB_DCTL_CSFTRST);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_DCTL), ==, 0);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_DCTL,
                  TH1520_USB_DCTL_RUN_STOP);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_DCTL), ==,
                    TH1520_USB_DCTL_RUN_STOP);

    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_XHCI_CAPLENGTH), ==,
                    0x01000040);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_XHCI_HCSPARAMS1), ==,
                    0x02000102);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_XHCI_DBOFF), ==,
                    TH1520_USB_XHCI_DOORBELL);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_XHCI_RTSOFF), ==,
                    TH1520_USB_XHCI_RUNTIME);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_XHCI_USB2_PORTS), ==,
                    0x00000102);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_XHCI_USB3_PORTS), ==,
                    0x00000101);
    g_assert_true(qtest_readl(qts, TH1520_USB_CORE_BASE +
                              TH1520_USB_XHCI_USBSTS) &
                  TH1520_USB_XHCI_USBSTS_HCH);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_DCTL), ==, 0);

    qtest_quit(qts);
}

static void test_th1520_vendor_uboot_usb_clock_alias(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    g_assert_cmphex(qtest_readl(qts, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_USB_CLK), ==, 0xf);
    g_assert_cmphex(qtest_readl(qts, TH1520_VENDOR_UBOOT_USB_CLOCK_BASE),
                    ==, 0xf);

    qtest_writel(qts, TH1520_VENDOR_UBOOT_USB_CLOCK_BASE, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_USB_CLK), ==, 0);

    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_CLK, 5);
    g_assert_cmphex(qtest_readl(qts, TH1520_VENDOR_UBOOT_USB_CLOCK_BASE),
                    ==, 5);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_USB_CLK), ==, 0xf);
    g_assert_cmphex(qtest_readl(qts, TH1520_VENDOR_UBOOT_USB_CLOCK_BASE),
                    ==, 0xf);

    qtest_quit(qts);
}

static void test_th1520_usb_reset_outputs(void)
{
    const uint32_t changed_gctl = TH1520_USB_GCTL_RESET ^ BIT(0) ^ BIT(2);
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_SWRST, 7);
    qtest_writel(qts, TH1520_USB_DRD_BASE + 0x50, 0x10203040);

    for (unsigned int reset = 0; reset < 3; reset++) {
        qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_GCTL,
                      changed_gctl);
        g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                    TH1520_USB_GCTL), ==,
                        changed_gctl);

        qtest_writel(qts,
                      TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_SWRST,
                      7 & ~BIT(reset));
        g_assert_cmphex(qtest_readl(qts, TH1520_MISCSYS_BASE +
                                    TH1520_MISCSYS_USB_SWRST), ==,
                        7 & ~BIT(reset));
        g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                    TH1520_USB_GCTL), ==,
                        TH1520_USB_GCTL_RESET);
        g_assert_true(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                  TH1520_USB_XHCI_USBSTS) &
                      TH1520_USB_XHCI_USBSTS_HCH);
        g_assert_cmphex(qtest_readl(qts, TH1520_USB_DRD_BASE + 0x50), ==,
                        0x10203040);

        qtest_writel(qts,
                      TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_SWRST, 7);
    }

    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_BUS_CLK, 0);
    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_CLK, 5);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_USB_SWRST), ==, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_BUS_CLK), ==, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_USB_CLK), ==, 0xf);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_DRD_BASE + 0x50), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_GCTL), ==,
                    TH1520_USB_GCTL_RESET);

    qtest_quit(qts);
}

static void test_th1520_usb_host_dma_irq(void)
{
    uint32_t erst[4] = {
        cpu_to_le32(TH1520_USB_EVENT_RING_ADDR),
        0,
        cpu_to_le32(TH1520_USB_EVENT_RING_TRBS),
        0,
    };
    uint32_t command[4] = {
        0,
        0,
        0,
        cpu_to_le32((TH1520_USB_CR_NOOP << TH1520_USB_TRB_TYPE_SHIFT) |
                    TH1520_USB_TRB_CYCLE),
    };
    uint32_t event[4] = { 0 };
    uint8_t empty_ring[TH1520_USB_EVENT_RING_TRBS * 16] = { 0 };
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_SWRST, 7);
    qtest_memwrite(qts, TH1520_USB_ERST_ADDR, erst, sizeof(erst));
    qtest_memwrite(qts, TH1520_USB_EVENT_RING_ADDR, empty_ring,
                   sizeof(empty_ring));
    qtest_memwrite(qts, TH1520_USB_COMMAND_RING_ADDR, command,
                   sizeof(command));

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(TH1520_USB_IRQ), 5);
    c900_plic_set_enable(qts, 1, TH1520_USB_IRQ, true);

    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERSTSZ, 1);
    /* Linux programs this 64-bit register high half first. */
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERSTBA + 4,
                  0);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERSTBA,
                  TH1520_USB_ERST_ADDR);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERDP,
                  TH1520_USB_EVENT_RING_ADDR);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERDP + 4, 0);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_IMAN,
                  TH1520_USB_XHCI_IMAN_IE);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_CRCR,
                  TH1520_USB_COMMAND_RING_ADDR | TH1520_USB_TRB_CYCLE);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_CRCR + 4, 0);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_USBCMD,
                  TH1520_USB_XHCI_USBCMD_RS |
                  TH1520_USB_XHCI_USBCMD_INTE);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_DOORBELL, 0);

    qtest_memread(qts, TH1520_USB_EVENT_RING_ADDR, event, sizeof(event));
    for (size_t i = 0; i < ARRAY_SIZE(event); i++) {
        event[i] = le32_to_cpu(event[i]);
    }
    g_assert_cmphex(event[0], ==, TH1520_USB_COMMAND_RING_ADDR);
    g_assert_cmphex(event[1], ==, 0);
    g_assert_cmphex(event[2], ==, TH1520_USB_CC_SUCCESS << 24);
    g_assert_cmphex(event[3], ==,
                    (TH1520_USB_ER_CMD_COMPLETE <<
                     TH1520_USB_TRB_TYPE_SHIFT) |
                    TH1520_USB_TRB_CYCLE);
    g_assert_true(qtest_readl(qts, TH1520_USB_CORE_BASE +
                              TH1520_USB_XHCI_USBSTS) &
                  TH1520_USB_XHCI_USBSTS_EINT);
    g_assert_cmphex(qtest_readl(qts, TH1520_USB_CORE_BASE +
                                TH1520_USB_XHCI_IMAN), ==,
                    TH1520_USB_XHCI_IMAN_IP | TH1520_USB_XHCI_IMAN_IE);
    g_assert_true(c900_plic_pending(qts, TH1520_USB_IRQ));
    assert_only_irq(qts, 0);

    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    TH1520_USB_IRQ);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_IMAN,
                  TH1520_USB_XHCI_IMAN_IP | TH1520_USB_XHCI_IMAN_IE);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERDP,
                  (TH1520_USB_EVENT_RING_ADDR + 16) |
                  TH1520_USB_XHCI_ERDP_EHB);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_USBSTS,
                  TH1520_USB_XHCI_USBSTS_EINT);
    qtest_writel(qts, C900_PLIC_CLAIM(1), TH1520_USB_IRQ);
    g_assert_false(c900_plic_pending(qts, TH1520_USB_IRQ));
    assert_no_irq(qts);

    qtest_quit(qts);
}

static void test_th1520_usb_pending_irq_migration(void)
{
    uint32_t erst[4] = {
        cpu_to_le32(TH1520_USB_EVENT_RING_ADDR),
        0,
        cpu_to_le32(TH1520_USB_EVENT_RING_TRBS),
        0,
    };
    uint32_t command[8] = {
        0,
        0,
        0,
        cpu_to_le32((TH1520_USB_CR_NOOP << TH1520_USB_TRB_TYPE_SHIFT) |
                    TH1520_USB_TRB_CYCLE),
    };
    uint32_t event[4] = { 0 };
    uint8_t empty_ring[TH1520_USB_EVENT_RING_TRBS * 16] = { 0 };
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-usb-pending-irq-XXXXXX", &path,
                         NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_irq_intercept_out_named(src, C900_PLIC_QOM_PATH, "sext");
    qtest_irq_intercept_out_named(dst, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(src, C900_PLIC_PRIORITY(TH1520_USB_IRQ), 5);
    c900_plic_set_enable(src, 1, TH1520_USB_IRQ, true);
    qtest_writel(src,
                  TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_SWRST, 7);
    qtest_memwrite(src, TH1520_USB_ERST_ADDR, erst, sizeof(erst));
    qtest_memwrite(src, TH1520_USB_EVENT_RING_ADDR, empty_ring,
                   sizeof(empty_ring));
    qtest_memwrite(src, TH1520_USB_COMMAND_RING_ADDR, command,
                   sizeof(command));
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERSTSZ, 1);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERSTBA,
                  TH1520_USB_ERST_ADDR);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERSTBA + 4, 0);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERDP,
                  TH1520_USB_EVENT_RING_ADDR);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERDP + 4, 0);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_IMAN,
                  TH1520_USB_XHCI_IMAN_IE);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_CRCR,
                  TH1520_USB_COMMAND_RING_ADDR | TH1520_USB_TRB_CYCLE);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_CRCR + 4, 0);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_USBCMD,
                  TH1520_USB_XHCI_USBCMD_RS |
                  TH1520_USB_XHCI_USBCMD_INTE);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_DOORBELL, 0);

    qtest_memread(src, TH1520_USB_EVENT_RING_ADDR, event, sizeof(event));
    for (size_t i = 0; i < ARRAY_SIZE(event); i++) {
        event[i] = le32_to_cpu(event[i]);
    }
    g_assert_cmphex(event[0], ==, TH1520_USB_COMMAND_RING_ADDR);
    g_assert_cmphex(event[2], ==, TH1520_USB_CC_SUCCESS << 24);
    g_assert_cmphex(event[3], ==,
                    (TH1520_USB_ER_CMD_COMPLETE <<
                     TH1520_USB_TRB_TYPE_SHIFT) |
                    TH1520_USB_TRB_CYCLE);
    g_assert_true(c900_plic_pending(src, TH1520_USB_IRQ));
    assert_only_irq(src, 0);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    qtest_memread(dst, TH1520_USB_EVENT_RING_ADDR, event, sizeof(event));
    for (size_t i = 0; i < ARRAY_SIZE(event); i++) {
        event[i] = le32_to_cpu(event[i]);
    }
    g_assert_cmphex(event[0], ==, TH1520_USB_COMMAND_RING_ADDR);
    g_assert_cmphex(event[2], ==, TH1520_USB_CC_SUCCESS << 24);
    g_assert_cmphex(event[3], ==,
                    (TH1520_USB_ER_CMD_COMPLETE <<
                     TH1520_USB_TRB_TYPE_SHIFT) |
                    TH1520_USB_TRB_CYCLE);
    g_assert_true(qtest_readl(dst, TH1520_USB_CORE_BASE +
                              TH1520_USB_XHCI_USBSTS) &
                  TH1520_USB_XHCI_USBSTS_EINT);
    g_assert_cmphex(qtest_readl(dst, TH1520_USB_CORE_BASE +
                                TH1520_USB_XHCI_IMAN), ==,
                    TH1520_USB_XHCI_IMAN_IP | TH1520_USB_XHCI_IMAN_IE);
    g_assert_true(c900_plic_pending(dst, TH1520_USB_IRQ));
    assert_only_irq(dst, 0);

    g_assert_cmphex(qtest_readl(dst, C900_PLIC_CLAIM(1)), ==,
                    TH1520_USB_IRQ);
    qtest_writel(dst, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_IMAN,
                  TH1520_USB_XHCI_IMAN_IP | TH1520_USB_XHCI_IMAN_IE);
    qtest_writel(dst, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERDP,
                  (TH1520_USB_EVENT_RING_ADDR + 16) |
                  TH1520_USB_XHCI_ERDP_EHB);
    qtest_writel(dst, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_USBSTS,
                  TH1520_USB_XHCI_USBSTS_EINT);
    qtest_writel(dst, C900_PLIC_CLAIM(1), TH1520_USB_IRQ);
    g_assert_false(c900_plic_pending(dst, TH1520_USB_IRQ));
    assert_no_irq(dst);

    /* The migrated command and event-ring producer cursors must advance. */
    command[7] = cpu_to_le32(
        (TH1520_USB_CR_NOOP << TH1520_USB_TRB_TYPE_SHIFT) |
        TH1520_USB_TRB_CYCLE);
    qtest_memwrite(dst, TH1520_USB_COMMAND_RING_ADDR + 16, command + 4,
                   16);
    qtest_writel(dst, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_DOORBELL, 0);
    qtest_memread(dst, TH1520_USB_EVENT_RING_ADDR + 16, event,
                  sizeof(event));
    for (size_t i = 0; i < ARRAY_SIZE(event); i++) {
        event[i] = le32_to_cpu(event[i]);
    }
    g_assert_cmphex(event[0], ==, TH1520_USB_COMMAND_RING_ADDR + 16);
    g_assert_cmphex(event[2], ==, TH1520_USB_CC_SUCCESS << 24);
    g_assert_cmphex(event[3], ==,
                    (TH1520_USB_ER_CMD_COMPLETE <<
                     TH1520_USB_TRB_TYPE_SHIFT) |
                    TH1520_USB_TRB_CYCLE);
    g_assert_true(c900_plic_pending(dst, TH1520_USB_IRQ));
    assert_only_irq(dst, 0);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_th1520_usb_hid_hotplug(void)
{
    uint32_t erst[4] = {
        cpu_to_le32(TH1520_USB_EVENT_RING_ADDR),
        0,
        cpu_to_le32(TH1520_USB_EVENT_RING_TRBS),
        0,
    };
    uint32_t event[4] = { 0 };
    uint8_t empty_ring[TH1520_USB_EVENT_RING_TRBS * 16] = { 0 };
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");
    uint32_t portsc;

    qtest_writel(qts, TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_SWRST, 7);
    qtest_memwrite(qts, TH1520_USB_ERST_ADDR, erst, sizeof(erst));
    qtest_memwrite(qts, TH1520_USB_EVENT_RING_ADDR, empty_ring,
                   sizeof(empty_ring));

    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(TH1520_USB_IRQ), 5);
    c900_plic_set_enable(qts, 1, TH1520_USB_IRQ, true);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERSTSZ, 1);
    /* Also preserve the low-half-first programming order. */
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERSTBA,
                  TH1520_USB_ERST_ADDR);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERSTBA + 4,
                  0);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERDP,
                  TH1520_USB_EVENT_RING_ADDR);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_ERDP + 4, 0);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_IMAN,
                  TH1520_USB_XHCI_IMAN_IE);
    qtest_writel(qts, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_USBCMD,
                  TH1520_USB_XHCI_USBCMD_RS |
                  TH1520_USB_XHCI_USBCMD_INTE);

    qtest_qmp_device_add(qts, "usb-kbd", "usb-kbd0", "{}");
    portsc = qtest_readl(qts, TH1520_USB_CORE_BASE +
                         TH1520_USB_XHCI_USB2_PORTSC);
    g_assert_true(portsc & TH1520_USB_XHCI_PORTSC_CCS);
    g_assert_true(portsc & TH1520_USB_XHCI_PORTSC_CSC);

    qtest_memread(qts, TH1520_USB_EVENT_RING_ADDR, event, sizeof(event));
    for (size_t i = 0; i < ARRAY_SIZE(event); i++) {
        event[i] = le32_to_cpu(event[i]);
    }
    g_assert_cmphex(event[0], ==, 2 << 24);
    g_assert_cmphex(event[1], ==, 0);
    g_assert_cmphex(event[2], ==, TH1520_USB_CC_SUCCESS << 24);
    g_assert_cmphex(event[3], ==,
                    (TH1520_USB_ER_PORT_CHANGE <<
                     TH1520_USB_TRB_TYPE_SHIFT) |
                    TH1520_USB_TRB_CYCLE);
    g_assert_true(c900_plic_pending(qts, TH1520_USB_IRQ));
    assert_only_irq(qts, 0);

    qtest_qmp_device_del(qts, "usb-kbd0");
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

static void test_th1520_usb_migration(void)
{
    const uint32_t changed_gctl = TH1520_USB_GCTL_RESET ^ BIT(0) ^ BIT(2);
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-usb-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    qtest_writel(src,
                  TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_SWRST, 7);
    qtest_writel(src, TH1520_MISCSYS_BASE + TH1520_MISCSYS_BUS_CLK, 0);
    qtest_writel(src, TH1520_VENDOR_UBOOT_USB_CLOCK_BASE, 5);
    g_assert_cmphex(qtest_readl(src, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_USB_CLK), ==, 5);
    qtest_writel(src, TH1520_USB_DRD_BASE + 0x0c, 0x5a5a);
    qtest_writel(src, TH1520_USB_DRD_BASE + 0x50, 0x10203040);
    qtest_writel(src, TH1520_USB_DRD_BASE + 0x54, 0x89abcdef);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_GCTL,
                  changed_gctl);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_DCTL,
                  TH1520_USB_DCTL_RUN_STOP);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_DNCTRL,
                  0x5aa5);
    qtest_writel(src, TH1520_USB_CORE_BASE + TH1520_USB_XHCI_CONFIG, 2);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_USB_SWRST), ==, 7);
    g_assert_cmphex(qtest_readl(dst, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_BUS_CLK), ==, 0);
    g_assert_cmphex(qtest_readl(dst, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_USB_CLK), ==, 5);
    g_assert_cmphex(qtest_readl(dst, TH1520_VENDOR_UBOOT_USB_CLOCK_BASE),
                    ==, 5);
    g_assert_cmphex(qtest_readl(dst, TH1520_USB_DRD_BASE + 0x0c), ==,
                    0x5a5a);
    g_assert_cmphex(qtest_readl(dst, TH1520_USB_DRD_BASE + 0x50), ==,
                    0x10203040);
    g_assert_cmphex(qtest_readl(dst, TH1520_USB_DRD_BASE + 0x54), ==,
                    0x89abcdef);
    g_assert_cmphex(qtest_readl(dst, TH1520_USB_CORE_BASE +
                                TH1520_USB_GCTL), ==,
                    changed_gctl);
    g_assert_cmphex(qtest_readl(dst, TH1520_USB_CORE_BASE +
                                TH1520_USB_DCTL), ==,
                    TH1520_USB_DCTL_RUN_STOP);
    g_assert_cmphex(qtest_readl(dst, TH1520_USB_CORE_BASE +
                                TH1520_USB_XHCI_DNCTRL), ==,
                    0x5aa5);
    g_assert_cmphex(qtest_readl(dst, TH1520_USB_CORE_BASE +
                                TH1520_USB_XHCI_CONFIG), ==, 2);

    qtest_system_reset(dst);
    g_assert_cmphex(qtest_readl(dst, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_USB_SWRST), ==, 1);
    g_assert_cmphex(qtest_readl(dst, TH1520_USB_DRD_BASE + 0x50), ==, 0);
    g_assert_cmphex(qtest_readl(dst, TH1520_USB_CORE_BASE +
                                TH1520_USB_GCTL), ==,
                    TH1520_USB_GCTL_RESET);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_xgene_rtc_registers_timing_irq(void)
{
    const uint32_t run = XGENE_RTC_EN | XGENE_RTC_PSCLR_EN;
    QTestState *qts = qtest_init(
        "-machine beaglev-ahead -bios none "
        "-rtc base=2000-01-01,clock=vm");
    uint32_t retained;

    g_assert_cmpint(qtest_clock_set(qts, 0), ==, 0);
    qtest_irq_intercept_out_named(qts, C900_PLIC_QOM_PATH, "sext");
    qtest_writel(qts, C900_PLIC_PRIORITY(TH1520_RTC_IRQ), 5);
    c900_plic_set_enable(qts, 1, TH1520_RTC_IRQ, true);

    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, XGENE_RTC_TEST_EPOCH);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CMR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CLR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_STAT),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_RSTAT),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_EOI),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_VER),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CPSR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CPCVR),
                    ==, 0);

    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CPSR,
                  XGENE_RTC_PRESCALER);
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CCR, run);
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CLR, 100);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CLR),
                    ==, 100);

    /* CLR becomes visible on the next prescaled counter update. */
    qtest_clock_step(qts, XGENE_RTC_SECOND_NS - 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, XGENE_RTC_TEST_EPOCH);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CPCVR),
                    ==, XGENE_RTC_PRESCALER - 1);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, 100);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CPCVR),
                    ==, 0);

    qtest_clock_step(qts, XGENE_RTC_SECOND_NS / 2);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, 100);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CPCVR),
                    ==, XGENE_RTC_PRESCALER / 2);
    qtest_clock_step(qts, XGENE_RTC_SECOND_NS / 2);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, 101);

    /* A match while interrupt generation is disabled does not latch raw. */
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CMR, 102);
    qtest_clock_step(qts, XGENE_RTC_SECOND_NS);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_RSTAT),
                    ==, 0);
    g_assert_false(c900_plic_pending(qts, TH1520_RTC_IRQ));

    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CMR, 104);
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CCR,
                  run | XGENE_RTC_IE);
    qtest_clock_step(qts, 2 * XGENE_RTC_SECOND_NS - 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_RSTAT),
                    ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_STAT),
                    ==, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_RSTAT),
                    ==, 1);
    g_assert_true(c900_plic_pending(qts, TH1520_RTC_IRQ));
    assert_only_irq(qts, 0);
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    TH1520_RTC_IRQ);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_EOI),
                    ==, 1);
    qtest_writel(qts, C900_PLIC_CLAIM(1), TH1520_RTC_IRQ);
    g_assert_false(c900_plic_pending(qts, TH1520_RTC_IRQ));

    /* MASK hides STAT/IRQ but not RSTAT; unmasking exposes the latch. */
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CMR, 105);
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CCR,
                  run | XGENE_RTC_IE | XGENE_RTC_MASK);
    qtest_clock_step(qts, XGENE_RTC_SECOND_NS);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_RSTAT),
                    ==, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_STAT),
                    ==, 0);
    g_assert_false(c900_plic_pending(qts, TH1520_RTC_IRQ));
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CCR,
                  run | XGENE_RTC_IE);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_STAT),
                    ==, 1);
    g_assert_true(c900_plic_pending(qts, TH1520_RTC_IRQ));
    g_assert_cmphex(qtest_readl(qts, C900_PLIC_CLAIM(1)), ==,
                    TH1520_RTC_IRQ);
    qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_EOI);
    qtest_writel(qts, C900_PLIC_CLAIM(1), TH1520_RTC_IRQ);

    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CCR, 0);
    retained = qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR);
    qtest_clock_step(qts, 2 * XGENE_RTC_SECOND_NS);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, retained);

    /* WEN forces the counter to zero on the match clock. */
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CPSR,
                  XGENE_RTC_PRESCALER);
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CMR, 12);
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CCR,
                  run | XGENE_RTC_IE | XGENE_RTC_WEN);
    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CLR, 10);
    qtest_clock_step(qts, XGENE_RTC_SECOND_NS);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, 10);
    qtest_clock_step(qts, 2 * XGENE_RTC_SECOND_NS - 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, 11);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_RSTAT),
                    ==, 1);
    qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_EOI);

    retained = qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, retained);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CMR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CLR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CPSR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_RSTAT),
                    ==, 0);

    qtest_writel(qts, TH1520_RTC_BASE + XGENE_RTC_CCR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TH1520_RTC_BASE + XGENE_RTC_CCR),
                    ==, 0x1f);
    qtest_quit(qts);
}

static void test_xgene_rtc_migration(void)
{
    const uint32_t run = XGENE_RTC_EN | XGENE_RTC_PSCLR_EN |
                         XGENE_RTC_IE;
    const char *rtc_args =
        "-machine beaglev-ahead -bios none "
        "-rtc base=2000-01-01,clock=vm";
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    g_autofree char *incoming_args = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-rtc-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    incoming_args = g_strdup_printf("%s -incoming defer", rtc_args);

    src = qtest_init(rtc_args);
    dst = qtest_init(incoming_args);
    g_assert_cmpint(qtest_clock_set(src, 0), ==, 0);
    qtest_writel(src, C900_PLIC_PRIORITY(TH1520_RTC_IRQ), 5);
    c900_plic_set_enable(src, 1, TH1520_RTC_IRQ, true);

    qtest_writel(src, TH1520_RTC_BASE + XGENE_RTC_CPSR,
                  XGENE_RTC_PRESCALER);
    qtest_writel(src, TH1520_RTC_BASE + XGENE_RTC_CCR,
                  XGENE_RTC_EN | XGENE_RTC_PSCLR_EN);
    qtest_writel(src, TH1520_RTC_BASE + XGENE_RTC_CLR, 100);
    qtest_clock_step(src, XGENE_RTC_SECOND_NS);
    qtest_writel(src, TH1520_RTC_BASE + XGENE_RTC_CMR, 103);
    qtest_writel(src, TH1520_RTC_BASE + XGENE_RTC_CCR, run);
    qtest_clock_step(src, XGENE_RTC_SECOND_NS);
    g_assert_cmphex(qtest_readl(src, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, 101);
    qtest_clock_step(src, XGENE_RTC_SECOND_NS / 2);
    g_assert_cmphex(qtest_readl(src, TH1520_RTC_BASE + XGENE_RTC_CPCVR),
                    ==, XGENE_RTC_PRESCALER / 2);

    g_assert_cmpint(qtest_clock_set(dst, 5 * XGENE_RTC_SECOND_NS / 2), ==,
                    5 * XGENE_RTC_SECOND_NS / 2);
    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, 101);
    g_assert_cmphex(qtest_readl(dst, TH1520_RTC_BASE + XGENE_RTC_CMR),
                    ==, 103);
    g_assert_cmphex(qtest_readl(dst, TH1520_RTC_BASE + XGENE_RTC_CCR),
                    ==, run);
    g_assert_cmphex(qtest_readl(dst, TH1520_RTC_BASE + XGENE_RTC_CPSR),
                    ==, XGENE_RTC_PRESCALER);
    g_assert_cmphex(qtest_readl(dst, TH1520_RTC_BASE + XGENE_RTC_CPCVR),
                    ==, XGENE_RTC_PRESCALER / 2);
    g_assert_cmphex(qtest_readl(dst, TH1520_RTC_BASE + XGENE_RTC_RSTAT),
                    ==, 0);

    qtest_clock_step(dst, 3 * XGENE_RTC_SECOND_NS / 2 - 1);
    g_assert_cmphex(qtest_readl(dst, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, 102);
    g_assert_cmphex(qtest_readl(dst, TH1520_RTC_BASE + XGENE_RTC_RSTAT),
                    ==, 0);
    qtest_clock_step(dst, 1);
    g_assert_cmphex(qtest_readl(dst, TH1520_RTC_BASE + XGENE_RTC_CCVR),
                    ==, 103);
    g_assert_cmphex(qtest_readl(dst, TH1520_RTC_BASE + XGENE_RTC_STAT),
                    ==, 1);
    g_assert_true(c900_plic_pending(dst, TH1520_RTC_IRQ));

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
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

static void test_dw_timer_toggle_migration(void)
{
    const uint64_t base = TH1520_TIMER0_3_BASE;
    const uint64_t timer = base;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-timer-toggle-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_irq_intercept_out_named(src, TH1520_TIMER0_3_QOM_PATH,
                                  "toggle");
    qtest_irq_intercept_out_named(dst, TH1520_TIMER0_3_QOM_PATH,
                                  "toggle");
    g_assert_cmpint(qtest_clock_set(src, 0), ==, 0);

    qtest_writel(src, timer + DW_TIMER_LOAD_COUNT, 3);
    qtest_writel(src, base + DW_TIMER_LOAD_COUNT2(0), 5);
    qtest_writel(src, timer + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_PERIODIC | DW_TIMER_PWM);
    qtest_clock_step(src, 3 * TH1520_TIMER_TICK_NS);
    g_assert_true(qtest_get_irq(src, 0));
    qtest_readl(src, timer + DW_TIMER_EOI);
    qtest_clock_step(src, 2 * TH1520_TIMER_TICK_NS);
    g_assert_true(qtest_get_irq(src, 0));

    g_assert_cmpint(qtest_clock_set(dst, 5 * TH1520_TIMER_TICK_NS), ==,
                    5 * TH1520_TIMER_TICK_NS);
    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, timer + DW_TIMER_CONTROL), ==,
                    DW_TIMER_ENABLE | DW_TIMER_PERIODIC | DW_TIMER_PWM);
    g_assert_cmphex(qtest_readl(dst, timer + DW_TIMER_LOAD_COUNT), ==, 3);
    g_assert_cmphex(qtest_readl(dst, base + DW_TIMER_LOAD_COUNT2(0)), ==,
                    5);
    g_assert_true(qtest_get_irq(dst, 0));

    /* The high-half deadline and the following low half both continue. */
    qtest_clock_step(dst, 4 * TH1520_TIMER_TICK_NS - 1);
    g_assert_true(qtest_get_irq(dst, 0));
    qtest_clock_step(dst, 1);
    g_assert_false(qtest_get_irq(dst, 0));
    qtest_readl(dst, timer + DW_TIMER_EOI);
    qtest_clock_step(dst, 4 * TH1520_TIMER_TICK_NS - 1);
    g_assert_false(qtest_get_irq(dst, 0));
    qtest_clock_step(dst, 1);
    g_assert_true(qtest_get_irq(dst, 0));

    qtest_system_reset(dst);
    g_assert_false(qtest_get_irq(dst, 0));
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_dw_wdt_migration(void)
{
    const TH1520WDT *pending = &th1520_wdts[0];
    const TH1520WDT *running = &th1520_wdts[1];
    const uint64_t half_period = DW_WDT_TOP0_NS / 2;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    QDict *event;
    QDict *data;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-wdt-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init(
        "-machine beaglev-ahead -bios none -watchdog-action none");
    dst = qtest_init(
        "-machine beaglev-ahead -bios none -watchdog-action none "
        "-incoming defer");
    g_assert_cmpint(qtest_clock_set(src, 0), ==, 0);

    qtest_writel(src, pending->base + DW_WDT_TORR, 0);
    qtest_writel(src, pending->base + DW_WDT_CR,
                  DW_WDT_RMOD | DW_WDT_ENABLE);
    qtest_clock_step(src, DW_WDT_TOP0_NS);
    g_assert_cmphex(qtest_readl(src, pending->base + DW_WDT_STAT), ==, 1);

    qtest_writel(src, running->base + DW_WDT_TORR, 0);
    qtest_writel(src, running->base + DW_WDT_CR,
                  DW_WDT_RMOD | DW_WDT_ENABLE);
    qtest_clock_step(src, half_period);
    /* At an exact tick boundary ptimer reports the not-yet-decremented tick. */
    g_assert_cmphex(qtest_readl(src, pending->base + DW_WDT_CCVR), ==,
                    DW_WDT_TOP0_COUNT / 2 + 1);
    g_assert_cmphex(qtest_readl(src, running->base + DW_WDT_CCVR), ==,
                    DW_WDT_TOP0_COUNT / 2 + 1);

    /* Both ptimers retain their absolute virtual deadlines and stage. */
    g_assert_cmpint(qtest_clock_set(dst, DW_WDT_TOP0_NS + half_period), ==,
                    DW_WDT_TOP0_NS + half_period);
    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, pending->base + DW_WDT_STAT), ==, 1);
    g_assert_cmphex(qtest_readl(dst, running->base + DW_WDT_STAT), ==, 0);
    g_assert_cmphex(qtest_readl(dst, pending->base + DW_WDT_CCVR), ==,
                    DW_WDT_TOP0_COUNT / 2 + 1);
    g_assert_cmphex(qtest_readl(dst, running->base + DW_WDT_CCVR), ==,
                    DW_WDT_TOP0_COUNT / 2 + 1);
    g_assert_true(c900_plic_pending(dst, pending->irq));
    g_assert_false(c900_plic_pending(dst, running->irq));

    qtest_clock_step(dst, half_period + 1);
    event = qtest_qmp_eventwait_ref(dst, "WATCHDOG");
    data = qdict_get_qdict(event, "data");
    g_assert_cmpstr(qdict_get_str(data, "action"), ==, "none");
    qobject_unref(event);
    g_assert_cmphex(qtest_readl(dst, pending->base + DW_WDT_STAT), ==, 1);
    g_assert_cmphex(qtest_readl(dst, pending->base + DW_WDT_CCVR), ==, 0);
    g_assert_cmphex(qtest_readl(dst, running->base + DW_WDT_STAT), ==, 1);
    g_assert_cmphex(qtest_readl(dst, running->base + DW_WDT_CCVR), ==,
                    DW_WDT_TOP0_COUNT);

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

static void test_th1520_pwm_gated_migration(void)
{
    const TH1520ClockGateTestOutput *pwm_gate =
        &th1520_ap_clock_gate_test_outputs[TH1520_AP_CLOCK_GATE_PWM];
    const uint32_t ctrl = TH1520_PWM_CONTINUOUS | TH1520_PWM_FPOUT;
    const uint64_t enabled_period = CLOCK_PERIOD_FROM_HZ(125000000);
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-pwm-gated-XXXXXX", &path, NULL);
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
    th1520_set_ap_clock_gate(src, pwm_gate->offset, pwm_gate->mask, false);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmpuint(qtest_qom_clock_period(
                         dst, TH1520_AP_CLOCK_QOM_PATH "/"
                              TH1520_AP_CLOCK_PWM_OUTPUT), ==, 0);
    g_assert_true(qtest_get_irq(dst, 0));
    qtest_clock_step(dst, 100 * TH1520_PWM_TICK_NS);
    g_assert_true(qtest_get_irq(dst, 0));

    th1520_set_ap_clock_gate(dst, pwm_gate->offset, pwm_gate->mask, true);
    g_assert_cmpuint(qtest_qom_clock_period(
                         dst, TH1520_AP_CLOCK_QOM_PATH "/"
                              TH1520_AP_CLOCK_PWM_OUTPUT), ==,
                     enabled_period);
    qtest_clock_step(dst, 2 * TH1520_PWM_TICK_NS - 1);
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
    static const struct {
        unsigned int output;
        size_t controller;
        size_t neighbor;
    } pad_resets[] = {
        { TH1520_AP_RESET_PADCTRL0, 2, 1 },
        { TH1520_AP_RESET_PADCTRL1, 1, 2 },
    };
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

    for (size_t i = 0; i < ARRAY_SIZE(th1520_uart_controllers); i++) {
        const TH1520ResetTestOutput *reset =
            &th1520_ap_reset_test_outputs[TH1520_AP_RESET_UART0 + i];
        size_t neighbor = (i + 1) % ARRAY_SIZE(th1520_uart_controllers);
        uint64_t base = th1520_uart_controllers[i].base;
        uint64_t neighbor_base = th1520_uart_controllers[neighbor].base;

        qtest_writel(qts, base + DW_UART_SCR_OFFSET, 0x40 + i);
        qtest_writel(qts, neighbor_base + DW_UART_SCR_OFFSET, 0xa0 + i);
        qtest_writel(qts, TH1520_AP_RESET_BASE + reset->offset,
                      reset->deasserted - 1);
        g_assert_cmphex(qtest_readl(qts, base + DW_UART_SCR_OFFSET), ==, 0);
        g_assert_cmphex(qtest_readl(qts,
                                    neighbor_base + DW_UART_SCR_OFFSET), ==,
                        0xa0 + i);
        qtest_writel(qts, TH1520_AP_RESET_BASE + reset->offset,
                      reset->deasserted);
    }

    for (size_t i = 0; i < ARRAY_SIZE(th1520_i2c_controllers); i++) {
        const TH1520ResetTestOutput *reset =
            &th1520_ap_reset_test_outputs[TH1520_AP_RESET_I2C0 + i];
        size_t neighbor = (i + 1) % ARRAY_SIZE(th1520_i2c_controllers);
        uint64_t base = th1520_i2c_controllers[i].base;
        uint64_t neighbor_base = th1520_i2c_controllers[neighbor].base;

        qtest_writel(qts, base + DW_I2C_SDA_SETUP, 0x20 + i);
        qtest_writel(qts, neighbor_base + DW_I2C_SDA_SETUP, 0x80 + i);
        qtest_writel(qts, TH1520_AP_RESET_BASE + reset->offset,
                      reset->deasserted - 1);
        assert_dw_i2c_reset_state(qts, base);
        g_assert_cmphex(qtest_readl(qts,
                                    neighbor_base + DW_I2C_SDA_SETUP), ==,
                        0x80 + i);
        qtest_writel(qts, TH1520_AP_RESET_BASE + reset->offset,
                      reset->deasserted);
    }

    qtest_writel(qts, th1520_spi0.base + DW_SSI_CTRLR0,
                  DW_SSI_CTRLR0_DFS_8 | DW_SSI_CTRLR0_SRL);
    qtest_writel(qts, TH1520_I2C0_BASE + DW_I2C_SDA_SETUP, 0x55);
    qtest_writel(qts, TH1520_AP_RESET_BASE +
                       th1520_ap_reset_test_outputs[
                           TH1520_AP_RESET_SPI0].offset,
                  0x2);
    assert_dw_spi_reset_state(qts);
    g_assert_cmphex(qtest_readl(qts,
                                TH1520_I2C0_BASE + DW_I2C_SDA_SETUP), ==,
                    0x55);
    qtest_writel(qts, TH1520_AP_RESET_BASE +
                       th1520_ap_reset_test_outputs[
                           TH1520_AP_RESET_SPI0].offset,
                  0x3);

    for (size_t i = 0; i < 4; i++) {
        const TH1520ResetTestOutput *reset =
            &th1520_ap_reset_test_outputs[TH1520_AP_RESET_GPIO0 + i];
        const TH1520GPIOController *controller =
            &th1520_gpio_controllers[i];
        size_t neighbor = (i + 1) % 4;
        uint64_t neighbor_base = th1520_gpio_controllers[neighbor].base;

        qtest_writel(qts, controller->base + DW_GPIO_SWPORTA_DR, BIT(0));
        qtest_writel(qts, controller->base + DW_GPIO_SWPORTA_DDR, BIT(0));
        qtest_writel(qts, neighbor_base + DW_GPIO_SWPORTA_DR, BIT(1));
        qtest_writel(qts, TH1520_AP_RESET_BASE + reset->offset,
                      reset->deasserted - 1);
        assert_dw_gpio_reset_state(qts, controller);
        g_assert_cmphex(qtest_readl(qts,
                                    neighbor_base + DW_GPIO_SWPORTA_DR), ==,
                        BIT(1));
        qtest_writel(qts, TH1520_AP_RESET_BASE + reset->offset,
                      reset->deasserted);
    }

    for (size_t i = 0; i < ARRAY_SIZE(pad_resets); i++) {
        const TH1520ResetTestOutput *reset =
            &th1520_ap_reset_test_outputs[pad_resets[i].output];
        const TH1520PadCtrl *controller =
            &th1520_padctrls[pad_resets[i].controller];
        const TH1520PadCtrl *neighbor =
            &th1520_padctrls[pad_resets[i].neighbor];

        qtest_writel(qts, controller->base + 0x400, 0x01234567);
        qtest_writel(qts, neighbor->base + 0x400, 0x07654321);
        qtest_writel(qts, TH1520_AP_RESET_BASE + reset->offset, 0);
        assert_padctrl_reset_state(qts, controller);
        g_assert_cmphex(qtest_readl(qts, neighbor->base + 0x400), ==,
                        0x07654321);
        qtest_writel(qts, TH1520_AP_RESET_BASE + reset->offset, 1);
    }

    qtest_writeq(qts, TH1520_DMAC0_BASE + DMAC_CH_SAR(0),
                  0x1122334455667788ULL);
    qtest_writel(qts, th1520_spi0.base + DW_SSI_CTRLR0,
                  DW_SSI_CTRLR0_DFS_8 | DW_SSI_CTRLR0_SRL);
    qtest_writel(qts, TH1520_AP_RESET_BASE +
                       th1520_ap_reset_test_outputs[
                           TH1520_AP_RESET_DMAC0].offset,
                  0x2);
    assert_dmac_reset_state(qts);
    g_assert_cmphex(qtest_readl(qts, th1520_spi0.base + DW_SSI_CTRLR0), ==,
                    DW_SSI_CTRLR0_DFS_8 | DW_SSI_CTRLR0_SRL);
    qtest_writel(qts, TH1520_AP_RESET_BASE +
                       th1520_ap_reset_test_outputs[
                           TH1520_AP_RESET_DMAC0].offset,
                  0x3);

    for (size_t i = 0; i < ARRAY_SIZE(th1520_gmac_controllers); i++) {
        const TH1520ResetTestOutput *reset =
            &th1520_ap_reset_test_outputs[TH1520_AP_RESET_GMAC0 + i];
        const TH1520GMACController *controller =
            &th1520_gmac_controllers[i];
        size_t neighbor = (i + 1) % ARRAY_SIZE(th1520_gmac_controllers);
        const TH1520GMACController *other =
            &th1520_gmac_controllers[neighbor];

        qtest_writel(qts, controller->base + DWMAC_DMA_BUS_MODE,
                      0x00020180);
        qtest_writel(qts, controller->apb_base + GMAC_APB_CLK_EN, 0x55 + i);
        gmac_mdio_write(qts, controller->base, TH1520_GMAC_PHY_ADDR,
                        MII_BMCR, MII_BMCR_FD | MII_BMCR_SPEED100);
        qtest_writel(qts, other->base + DWMAC_DMA_BUS_MODE, 0x00020180);
        qtest_writel(qts, other->apb_base + GMAC_APB_CLK_EN, 0xa0 + i);
        gmac_mdio_write(qts, other->base, TH1520_GMAC_PHY_ADDR, MII_BMCR,
                        MII_BMCR_AUTOEN | MII_BMCR_SPEED100);
        qtest_writel(qts, TH1520_AP_RESET_BASE + reset->offset,
                      reset->deasserted - 1);
        assert_gmac_reset_state(qts, controller);
        g_assert_cmphex(qtest_readl(qts,
                                    other->base + DWMAC_DMA_BUS_MODE), ==,
                        0x00020180);
        g_assert_cmphex(qtest_readl(qts,
                                    other->apb_base + GMAC_APB_CLK_EN), ==,
                        0xa0 + i);
        g_assert_cmphex(gmac_mdio_read(qts, other->base,
                                      TH1520_GMAC_PHY_ADDR, MII_BMCR), ==,
                        MII_BMCR_AUTOEN | MII_BMCR_SPEED100);
        qtest_writel(qts, TH1520_AP_RESET_BASE + reset->offset,
                      reset->deasserted);
    }

    for (size_t i = 0; i < ARRAY_SIZE(th1520_gmac_controllers); i++) {
        qtest_writel(qts,
                      th1520_gmac_controllers[i].base + DWMAC_DMA_BUS_MODE,
                      0x00020180);
        qtest_writel(qts,
                      th1520_gmac_controllers[i].apb_base + GMAC_APB_CLK_EN,
                      0x66 + i);
        gmac_mdio_write(qts, th1520_gmac_controllers[i].base,
                        TH1520_GMAC_PHY_ADDR, MII_BMCR,
                        MII_BMCR_FD | MII_BMCR_SPEED100);
    }
    qtest_writel(qts, TH1520_AP_RESET_BASE +
                       th1520_ap_reset_test_outputs[
                           TH1520_AP_RESET_GMAC_SHARED].offset,
                  0x2);
    for (size_t i = 0; i < ARRAY_SIZE(th1520_gmac_controllers); i++) {
        assert_gmac_reset_state(qts, &th1520_gmac_controllers[i]);
    }
    qtest_writel(qts, TH1520_AP_RESET_BASE +
                       th1520_ap_reset_test_outputs[
                           TH1520_AP_RESET_GMAC_SHARED].offset,
                  0x3);

    qtest_quit(qts);
}

static void test_dw_i2c_migration(void)
{
    static const uint8_t contents[] = { 0xa5, 0x5a };
    uint64_t base = TH1520_I2C0_BASE;
    uint64_t aon_base = TH1520_AON_I2C_BASE;
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
    qtest_writel(src, aon_base + DW_I2C_SDA_SETUP, 0x22);
    qtest_writel(src, aon_base + DW_I2C_INTR_MASK, DW_I2C_INTR_TX_ABRT);

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
    g_assert_cmphex(qtest_readl(dst, aon_base + DW_I2C_SDA_SETUP), ==, 0x22);
    g_assert_cmphex(qtest_readl(dst, aon_base + DW_I2C_INTR_MASK), ==,
                    DW_I2C_INTR_TX_ABRT);
    dw_i2c_disable(dst, base);

    /* The EEPROM data and its post-read address counter both migrate. */
    g_assert_cmphex(dw_i2c_eeprom_current_read(dst), ==, contents[1]);
    qtest_system_reset(dst);
    g_assert_cmphex(dw_i2c_eeprom_read(dst, 0x124), ==, contents[1]);
    assert_dw_i2c_reset_state(dst, aon_base);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_aon_i2c_pmic_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-pmic-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    dw_i2c_pmic_write(src, DA9063_REG_VBCORE1_A, 0x32);
    dw_i2c_pmic_write(src, DA9063_REG_VBCORE1_B, 0x12);
    dw_i2c_pmic_write(src, DA9063_REG_DVC_1, 0x03);
    dw_i2c_pmic_write(src, DA9063_REG_DVC_2, 0xff);
    dw_i2c_enable(src, TH1520_AON_I2C_BASE, BEAGLEV_AHEAD_PMIC_ADDR, 0);
    qtest_writel(src, TH1520_AON_I2C_BASE + DW_I2C_DATA_CMD,
                  DA9063_REG_VBCORE1_B);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    /* A pending source-compatible FIFO-drain stop also migrates. */
    g_assert_cmphex(qtest_readl(dst, TH1520_AON_I2C_BASE + DW_I2C_TXFLR),
                    ==, 0);
    g_assert_true(qtest_readl(dst, TH1520_AON_I2C_BASE +
                              DW_I2C_RAW_INTR_STAT) & DW_I2C_INTR_STOP_DET);
    dw_i2c_disable(dst, TH1520_AON_I2C_BASE);

    /* Pointer and register values persist across the stopped transfer. */
    g_assert_cmphex(dw_i2c_pmic_current_read(dst), ==, 0x12);
    g_assert_cmphex(dw_i2c_pmic_read(dst, DA9063_REG_VBCORE1_A), ==, 0x32);
    g_assert_cmphex(dw_i2c_pmic_read(dst, DA9063_REG_DVC_1), ==, 0x03);
    g_assert_cmphex(dw_i2c_pmic_read(dst, DA9063_REG_DVC_2), ==, 0x81);

    qtest_system_reset(dst);
    g_assert_cmphex(dw_i2c_pmic_read(dst, DA9063_REG_VBCORE1_A), ==, 0x00);
    g_assert_cmphex(dw_i2c_pmic_read(dst, DA9063_REG_VBCORE1_B), ==, 0x00);
    g_assert_cmphex(dw_i2c_pmic_read(dst, DA9063_REG_DVC_1), ==, 0x00);
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

static void ap6203bm_drive_controls(QTestState *qts, uint32_t level)
{
    qtest_writel(qts, TH1520_GPIO2_BASE + DW_GPIO_SWPORTA_DR, level);
    qtest_writel(qts, TH1520_GPIO2_BASE + DW_GPIO_SWPORTA_DDR,
                  AP6203BM_CONTROL_MASK);
}

static void ap6203bm_set_host_wakes(QTestState *qts, bool wl, bool bt)
{
    qtest_set_irq_in(qts, TH1520_AP6203BM_QOM_PATH, "wl-host-wake-in", 0,
                     wl);
    qtest_set_irq_in(qts, TH1520_AP6203BM_QOM_PATH, "bt-host-wake-in", 0,
                     bt);
}

static void assert_ap6203bm_controls(QTestState *qts, bool wl, bool bt,
                                      bool bt_wake)
{
    g_assert_cmpint(qtest_get_irq(qts, TH1520_AP6203BM_WL_REG_ON), ==, wl);
    g_assert_cmpint(qtest_get_irq(qts, TH1520_AP6203BM_BT_REG_ON), ==, bt);
    g_assert_cmpint(qtest_get_irq(qts, TH1520_AP6203BM_BT_WAKE_HOST), ==,
                    bt_wake);
}

static void assert_ap6203bm_host_wakes(QTestState *qts, uint32_t expected)
{
    g_assert_cmphex(qtest_readl(qts, TH1520_GPIO2_BASE + DW_GPIO_EXT_PORTA) &
                    AP6203BM_HOST_WAKE_MASK, ==, expected);
}

static void test_ap6203bm_control_wake(void)
{
    QTestState *qts = qtest_init("-machine beaglev-ahead -bios none");

    qtest_irq_intercept_out_named(qts, TH1520_AP6203BM_QOM_PATH, "control");
    assert_ap6203bm_controls(qts, false, false, false);

    ap6203bm_drive_controls(qts, AP6203BM_CONTROL_MASK);
    assert_ap6203bm_controls(qts, true, true, true);
    ap6203bm_set_host_wakes(qts, true, false);
    assert_ap6203bm_host_wakes(qts, BIT(AP6203BM_WL_HOST_WAKE_GPIO));

    ap6203bm_drive_controls(qts, BIT(AP6203BM_BT_REG_ON_GPIO) |
                            BIT(AP6203BM_BT_WAKE_HOST_GPIO));
    assert_ap6203bm_controls(qts, false, true, true);
    ap6203bm_set_host_wakes(qts, false, true);
    assert_ap6203bm_host_wakes(qts, BIT(AP6203BM_BT_HOST_WAKE_GPIO));

    qtest_system_reset(qts);
    assert_ap6203bm_controls(qts, false, false, false);
    assert_ap6203bm_host_wakes(qts, 0);
    qtest_quit(qts);
}

static void test_ap6203bm_migration(void)
{
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-ap6203bm-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_irq_intercept_out_named(src, TH1520_AP6203BM_QOM_PATH, "control");
    qtest_irq_intercept_out_named(dst, TH1520_AP6203BM_QOM_PATH, "control");

    ap6203bm_drive_controls(src, BIT(AP6203BM_WL_REG_ON_GPIO) |
                            BIT(AP6203BM_BT_REG_ON_GPIO));
    ap6203bm_set_host_wakes(src, true, false);
    assert_ap6203bm_controls(src, true, true, false);
    assert_ap6203bm_host_wakes(src, BIT(AP6203BM_WL_HOST_WAKE_GPIO));

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    assert_ap6203bm_controls(dst, true, true, false);
    assert_ap6203bm_host_wakes(dst, BIT(AP6203BM_WL_HOST_WAKE_GPIO));
    ap6203bm_set_host_wakes(dst, false, true);
    assert_ap6203bm_host_wakes(dst, BIT(AP6203BM_BT_HOST_WAKE_GPIO));

    qtest_system_reset(dst);
    assert_ap6203bm_controls(dst, false, false, false);
    assert_ap6203bm_host_wakes(dst, 0);
    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static int64_t board_led_intensity(QTestState *qts, const char *name)
{
    g_autofree char *path = g_strdup_printf("/machine/%s", name);
    QDict *response = qtest_qmp(
        qts, "{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
        "'property': 'intensity-percent' } }", path);
    int64_t value;

    g_assert_nonnull(response);
    g_assert_true(qdict_haskey(response, "return"));
    value = qdict_get_int(response, "return");
    qobject_unref(response);
    return value;
}

static void assert_board_led_metadata(QTestState *qts, const char *name,
                                      const char *color)
{
    g_autofree char *path = g_strdup_printf("/machine/%s", name);
    QDict *response = qtest_qmp(
        qts, "{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
        "'property': 'color' } }", path);

    g_assert_nonnull(response);
    g_assert_cmpstr(qdict_get_str(response, "return"), ==, color);
    qobject_unref(response);

    response = qtest_qmp(
        qts, "{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
        "'property': 'gpio-active-high' } }", path);
    g_assert_nonnull(response);
    g_assert_true(qdict_get_bool(response, "return"));
    qobject_unref(response);
}

static void assert_user_led_pattern(QTestState *qts, uint32_t pattern)
{
    for (unsigned int i = 0; i < 5; i++) {
        g_autofree char *name = g_strdup_printf("usr%u", i);

        g_assert_cmpint(board_led_intensity(qts, name), ==,
                        pattern & BIT(i) ? 100 : 0);
    }
}

static void test_board_leds(void)
{
    const uint32_t user_mask = 0x1f00;
    const uint32_t source_pattern = BIT(8) | BIT(10) | BIT(12);
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-leds-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    for (unsigned int i = 0; i < 5; i++) {
        g_autofree char *name = g_strdup_printf("usr%u", i);

        assert_board_led_metadata(src, name, "blue");
    }
    assert_board_led_metadata(src, "power", "green");
    assert_user_led_pattern(src, 0);
    g_assert_cmpint(board_led_intensity(src, "power"), ==, 100);

    /* Data does not reach an LED until its GPIO is configured as output. */
    qtest_writel(src, TH1520_GPIO4_BASE + DW_GPIO_SWPORTA_DR, user_mask);
    assert_user_led_pattern(src, 0);
    qtest_writel(src, TH1520_GPIO4_BASE + DW_GPIO_SWPORTA_DDR, user_mask);
    assert_user_led_pattern(src, 0x1f);

    qtest_writel(src, TH1520_GPIO4_BASE + DW_GPIO_SWPORTA_DR,
                  source_pattern);
    assert_user_led_pattern(src, BIT(0) | BIT(2) | BIT(4));

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_GPIO4_BASE +
                                DW_GPIO_SWPORTA_DR), ==, source_pattern);
    g_assert_cmphex(qtest_readl(dst, TH1520_GPIO4_BASE +
                                DW_GPIO_SWPORTA_DDR), ==, user_mask);
    assert_user_led_pattern(dst, BIT(0) | BIT(2) | BIT(4));
    g_assert_cmpint(board_led_intensity(dst, "power"), ==, 100);

    qtest_system_reset(dst);
    assert_user_led_pattern(dst, 0);
    g_assert_cmpint(board_led_intensity(dst, "power"), ==, 100);

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
    qtest_writel(src, TH1520_VENDOR_UBOOT_AP_RESET_NPU_BASE, 1);
    qtest_writel(src, TH1520_AP_RESET_BASE + 0x0c0, 0x2);
    qtest_writel(src, TH1520_AP_RESET_BASE + 0x084, 0x2);
    qtest_writel(src, TH1520_AP_RESET_BASE + 0x208, 0x2);

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
    g_assert_cmphex(qtest_readl(dst, TH1520_VENDOR_UBOOT_AP_RESET_NPU_BASE),
                    ==, 1);
    g_assert_cmphex(qtest_readl(dst, TH1520_AP_RESET_BASE + 0x0c0), ==, 2);
    g_assert_cmphex(qtest_readl(dst, TH1520_AP_RESET_BASE + 0x084), ==, 2);
    g_assert_cmphex(qtest_readl(dst, TH1520_AP_RESET_BASE + 0x208), ==, 2);
    for (size_t line = 0;
         line < ARRAY_SIZE(th1520_ap_reset_test_outputs); line++) {
        bool expected = line == TH1520_AP_RESET_PWM ||
                        line == TH1520_AP_RESET_UART5 ||
                        line == TH1520_AP_RESET_GMAC_SHARED;

        g_assert_cmpint(qtest_get_irq(dst, line), ==, expected);
    }

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

static void test_ap_clock_gate_migration(void)
{
    const TH1520ClockGateTestOutput *timer_gate =
        &th1520_ap_clock_gate_test_outputs[TH1520_AP_CLOCK_GATE_TIMER0];
    const TH1520ClockGateTestOutput *wdt_gate =
        &th1520_ap_clock_gate_test_outputs[TH1520_AP_CLOCK_GATE_WDT0];
    const uint64_t enabled_period = CLOCK_PERIOD_FROM_HZ(125000000);
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    uint32_t timer_frozen;
    uint32_t wdt_frozen;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-clock-gate-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init(
        "-machine beaglev-ahead -bios none -watchdog-action none");
    dst = qtest_init(
        "-machine beaglev-ahead -bios none -watchdog-action none "
        "-incoming defer");
    qtest_irq_intercept_out_named(dst, TH1520_AP_CLOCK_QOM_PATH,
                                  "peripheral-clock-enable");
    g_assert_cmpint(qtest_clock_set(src, 0), ==, 0);

    qtest_writel(src, TH1520_TIMER0_3_BASE + DW_TIMER_LOAD_COUNT, 10);
    qtest_writel(src, TH1520_TIMER0_3_BASE + DW_TIMER_CONTROL,
                  DW_TIMER_ENABLE | DW_TIMER_PERIODIC);
    qtest_writel(src, TH1520_WDT0_BASE + DW_WDT_TORR, 0);
    qtest_writel(src, TH1520_WDT0_BASE + DW_WDT_CR,
                  DW_WDT_RMOD | DW_WDT_ENABLE);
    qtest_clock_step(src, 2 * TH1520_TIMER_TICK_NS + 1);
    th1520_set_ap_clock_gate(src, timer_gate->offset, timer_gate->mask,
                             false);
    th1520_set_ap_clock_gate(src, wdt_gate->offset, wdt_gate->mask, false);
    timer_frozen = qtest_readl(
        src, TH1520_TIMER0_3_BASE + DW_TIMER_CURRENT_VALUE);
    wdt_frozen = qtest_readl(src, TH1520_WDT0_BASE + DW_WDT_CCVR);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    for (size_t line = 0;
         line < ARRAY_SIZE(th1520_ap_clock_gate_test_outputs); line++) {
        bool expected = line != TH1520_AP_CLOCK_GATE_TIMER0 &&
                        line != TH1520_AP_CLOCK_GATE_WDT0;

        g_assert_cmpint(qtest_get_irq(dst, line), ==, expected);
    }
    g_assert_cmpuint(qtest_qom_clock_period(
                         dst, TH1520_AP_CLOCK_QOM_PATH "/"
                              TH1520_AP_CLOCK_TIMER0_OUTPUT), ==, 0);
    g_assert_cmpuint(qtest_qom_clock_period(
                         dst, TH1520_AP_CLOCK_QOM_PATH "/"
                              TH1520_AP_CLOCK_WDT0_OUTPUT), ==, 0);
    g_assert_cmphex(qtest_readl(
                        dst, TH1520_TIMER0_3_BASE + DW_TIMER_CURRENT_VALUE),
                    ==, timer_frozen);
    g_assert_cmphex(qtest_readl(dst, TH1520_WDT0_BASE + DW_WDT_CCVR), ==,
                    wdt_frozen);
    qtest_clock_step(dst, 100 * TH1520_TIMER_TICK_NS);
    g_assert_cmphex(qtest_readl(
                        dst, TH1520_TIMER0_3_BASE + DW_TIMER_CURRENT_VALUE),
                    ==, timer_frozen);
    g_assert_cmphex(qtest_readl(dst, TH1520_WDT0_BASE + DW_WDT_CCVR), ==,
                    wdt_frozen);

    th1520_set_ap_clock_gate(dst, timer_gate->offset, timer_gate->mask, true);
    th1520_set_ap_clock_gate(dst, wdt_gate->offset, wdt_gate->mask, true);
    g_assert_cmpuint(qtest_qom_clock_period(
                         dst, TH1520_AP_CLOCK_QOM_PATH "/"
                              TH1520_AP_CLOCK_TIMER0_OUTPUT), ==,
                     enabled_period);
    g_assert_cmpuint(qtest_qom_clock_period(
                         dst, TH1520_AP_CLOCK_QOM_PATH "/"
                              TH1520_AP_CLOCK_WDT0_OUTPUT), ==,
                     enabled_period);
    qtest_clock_step(dst, TH1520_TIMER_TICK_NS + 1);
    g_assert_cmphex(qtest_readl(
                        dst, TH1520_TIMER0_3_BASE + DW_TIMER_CURRENT_VALUE),
                    <, timer_frozen);
    g_assert_cmphex(qtest_readl(dst, TH1520_WDT0_BASE + DW_WDT_CCVR), <,
                    wdt_frozen);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
}

static void test_miscsys_clock_migration(void)
{
    static const bool expected[TH1520_MISCSYS_CLOCK_COUNT] = {
        [TH1520_MISCSYS_CLOCK_BUS] = false,
        [TH1520_MISCSYS_CLOCK_USB0] = true,
        [TH1520_MISCSYS_CLOCK_USB1] = false,
        [TH1520_MISCSYS_CLOCK_USB2] = true,
        [TH1520_MISCSYS_CLOCK_USB3] = false,
        [TH1520_MISCSYS_CLOCK_EMMC] = false,
        [TH1520_MISCSYS_CLOCK_SDIO0] = true,
        [TH1520_MISCSYS_CLOCK_SDIO1] = false,
    };
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-miscsys-clock-XXXXXX", &path,
                         NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");
    qtest_irq_intercept_out_named(dst, TH1520_MISCSYS_QOM_PATH,
                                  "clock-enable");

    qtest_writel(src, TH1520_MISCSYS_BASE + TH1520_MISCSYS_BUS_CLK, 0);
    qtest_writel(src, TH1520_MISCSYS_BASE + TH1520_MISCSYS_USB_CLK, 5);
    qtest_writel(src, TH1520_MISCSYS_BASE + TH1520_MISCSYS_EMMC_CLK, 0);
    qtest_writel(src, TH1520_MISCSYS_BASE + TH1520_MISCSYS_SDIO0_CLK, 1);
    qtest_writel(src, TH1520_MISCSYS_BASE + TH1520_MISCSYS_SDIO1_CLK, 0);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_BUS_CLK), ==, 0);
    g_assert_cmphex(qtest_readl(dst, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_USB_CLK), ==, 5);
    g_assert_cmphex(qtest_readl(dst, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_EMMC_CLK), ==, 0);
    g_assert_cmphex(qtest_readl(dst, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_SDIO0_CLK), ==, 1);
    g_assert_cmphex(qtest_readl(dst, TH1520_MISCSYS_BASE +
                                TH1520_MISCSYS_SDIO1_CLK), ==, 0);
    for (size_t line = 0; line < ARRAY_SIZE(expected); line++) {
        g_assert_cmpint(qtest_get_irq(dst, line), ==, expected[line]);
    }

    qtest_system_reset(dst);
    for (size_t line = 0; line < ARRAY_SIZE(expected); line++) {
        g_assert_true(qtest_get_irq(dst, line));
    }

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

#define AHEAD_VM_SECTION_FULL              0x04
#define AHEAD_VM_SECTION_FOOTER            0x7e
#define AHEAD_CURRENT_PLIC_VMSTATE         "thead.c900-plic"
#define AHEAD_LEGACY_PLIC_VMSTATE          "riscv_sifive_plic"
#define AHEAD_CURRENT_CLINT_VMSTATE        "thead.c900-clint"
#define AHEAD_LEGACY_CLINT_VMSTATE         "riscv_mtimer"
#define AHEAD_CURRENT_UART_VMSTATE         "dw-apb-uart"
#define AHEAD_LEGACY_UART_VMSTATE          "serial"
#define AHEAD_CURRENT_CPU_VMSTATE          "cpu"
#define AHEAD_CURRENT_CPU_VMSTATE_VERSION  12

#define AHEAD_PLIC_SOURCES                 241
#define AHEAD_PLIC_ENABLE_WORDS            (C900_PLIC_CONTEXTS * \
                                            C900_PLIC_WORDS)
#define AHEAD_LEGACY_PLIC_PAYLOAD_SIZE     \
    ((AHEAD_PLIC_SOURCES + C900_PLIC_CONTEXTS + 2 * C900_PLIC_WORDS + \
      AHEAD_PLIC_ENABLE_WORDS) * sizeof(uint32_t))
#define AHEAD_CURRENT_PLIC_PAYLOAD_SIZE    \
    (sizeof(uint32_t) + AHEAD_LEGACY_PLIC_PAYLOAD_SIZE + \
     2 * C900_PLIC_WORDS * sizeof(uint32_t))
#define AHEAD_CURRENT_CLINT_PAYLOAD_SIZE   \
    (sizeof(uint64_t) + 2 * C910_HARTS * sizeof(uint32_t) + \
     2 * C910_HARTS * sizeof(uint64_t))
#define AHEAD_LEGACY_CLINT_PAYLOAD_SIZE    \
    (sizeof(uint64_t) + 2 * C910_HARTS * sizeof(uint64_t))
#define AHEAD_CURRENT_UART_PAYLOAD_SIZE    24
#define AHEAD_LEGACY_UART_PAYLOAD_SIZE     11

/* env.mip in the unchanged RISC-V CPU v11/v12 parent payload. */
#define AHEAD_RISCV_CPU_MIP_OFFSET         \
    (2 * 32 * sizeof(uint64_t) + 2 * 64 * sizeof(uint8_t) + \
     3 * sizeof(uint64_t) + sizeof(uint8_t) + 2 * sizeof(uint64_t) + \
     6 * sizeof(uint32_t) + 2 * sizeof(uint8_t) + \
     3 * sizeof(uint64_t))

#define AHEAD_LEGACY_PLIC_CONTEXT          1
#define AHEAD_LEGACY_PLIC_ACTIVE_IRQ       120
#define AHEAD_LEGACY_PLIC_PENDING_IRQ      124
#define AHEAD_LEGACY_PLIC_EDGE_IRQ         130

typedef struct AheadVMStateFullSection {
    gsize offset;
    gsize payload_offset;
    uint32_t section_id;
} AheadVMStateFullSection;

static bool ahead_vmstate_full_header_at(const uint8_t *data, gsize size,
                                         gsize offset, const char *name,
                                         uint32_t instance_id,
                                         uint32_t version,
                                         gsize minimum_payload_size)
{
    gsize name_len = strlen(name);
    gsize header_size = 1 + sizeof(uint32_t) + 1 + name_len +
                        2 * sizeof(uint32_t);
    gsize instance_offset;
    gsize version_offset;

    if (name_len > UINT8_MAX || offset > size ||
        header_size > size - offset ||
        minimum_payload_size > size - offset - header_size) {
        return false;
    }
    instance_offset = offset + 1 + sizeof(uint32_t) + 1 + name_len;
    version_offset = instance_offset + sizeof(uint32_t);

    return data[offset] == AHEAD_VM_SECTION_FULL &&
           data[offset + 1 + sizeof(uint32_t)] == name_len &&
           !memcmp(data + offset + 1 + sizeof(uint32_t) + 1,
                   name, name_len) &&
           ldl_be_p(data + instance_offset) == instance_id &&
           ldl_be_p(data + version_offset) == version;
}

static bool ahead_vmstate_full_fixed_at(const uint8_t *data, gsize size,
                                        gsize offset, const char *name,
                                        uint32_t instance_id,
                                        uint32_t version, gsize payload_size)
{
    gsize header_size = 1 + sizeof(uint32_t) + 1 + strlen(name) +
                        2 * sizeof(uint32_t);
    gsize footer_offset;

    if (!ahead_vmstate_full_header_at(data, size, offset, name, instance_id,
                                      version, payload_size)) {
        return false;
    }
    footer_offset = offset + header_size + payload_size;
    if (sizeof(uint8_t) + sizeof(uint32_t) > size - footer_offset) {
        return false;
    }
    return data[footer_offset] == AHEAD_VM_SECTION_FOOTER &&
           ldl_be_p(data + footer_offset + 1) == ldl_be_p(data + offset + 1);
}

static guint ahead_vmstate_full_count(const GByteArray *stream,
                                      const char *name,
                                      uint32_t instance_id,
                                      uint32_t version, gsize payload_size)
{
    guint count = 0;

    for (gsize offset = 0; offset < stream->len; offset++) {
        count += ahead_vmstate_full_fixed_at(stream->data, stream->len,
                                             offset, name, instance_id,
                                             version, payload_size);
    }
    return count;
}

static AheadVMStateFullSection
ahead_vmstate_find_full(const GByteArray *stream, const char *name,
                        uint32_t instance_id, uint32_t version,
                        gsize payload_size, bool fixed_payload)
{
    AheadVMStateFullSection section = { 0 };
    gsize name_len = strlen(name);
    guint count = 0;

    for (gsize offset = 0; offset < stream->len; offset++) {
        bool match = fixed_payload ?
            ahead_vmstate_full_fixed_at(stream->data, stream->len, offset,
                                        name, instance_id, version,
                                        payload_size) :
            ahead_vmstate_full_header_at(stream->data, stream->len, offset,
                                         name, instance_id, version,
                                         payload_size);

        if (!match) {
            continue;
        }
        section = (AheadVMStateFullSection) {
            .offset = offset,
            .payload_offset = offset + 1 + sizeof(uint32_t) + 1 +
                              name_len + 2 * sizeof(uint32_t),
            .section_id = ldl_be_p(stream->data + offset + 1),
        };
        count++;
    }
    g_assert_cmpuint(count, ==, 1);
    return section;
}

static GByteArray *ahead_vmstate_read(const char *path)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    gsize size;

    g_assert_true(g_file_get_contents(path, &contents, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(size, >=, 8);
    g_assert_cmpuint(size, <=, G_MAXUINT);
    g_assert_cmpmem(contents, 4, "QEVM", 4);
    g_assert_cmpuint(ldl_be_p(contents + 4), ==, 3);
    return g_byte_array_new_take((uint8_t *)g_steal_pointer(&contents), size);
}

static void ahead_vmstate_append_be32(GByteArray *stream, uint32_t value)
{
    uint8_t encoded[sizeof(value)];

    stl_be_p(encoded, value);
    g_byte_array_append(stream, encoded, sizeof(encoded));
}

static void ahead_vmstate_replace_full(GByteArray **stream_ptr,
                                       const char *current_name,
                                       uint32_t current_instance,
                                       uint32_t current_version,
                                       gsize current_payload_size,
                                       const char *legacy_name,
                                       uint32_t legacy_instance,
                                       uint32_t legacy_version,
                                       const uint8_t *legacy_payload,
                                       gsize legacy_payload_size)
{
    GByteArray *stream = *stream_ptr;
    AheadVMStateFullSection section =
        ahead_vmstate_find_full(stream, current_name, current_instance,
                                current_version, current_payload_size, true);
    gsize old_end = section.payload_offset + current_payload_size;
    gsize legacy_name_len = strlen(legacy_name);
    g_autoptr(GByteArray) replacement = g_byte_array_new();
    g_autoptr(GByteArray) updated = g_byte_array_new();
    uint8_t marker = AHEAD_VM_SECTION_FULL;
    uint8_t name_len;

    g_assert_cmpuint(legacy_name_len, <=, UINT8_MAX);
    name_len = legacy_name_len;
    g_assert_cmpuint(old_end + 1 + sizeof(uint32_t), <=, stream->len);
    g_assert_cmpuint(stream->data[old_end], ==,
                     AHEAD_VM_SECTION_FOOTER);
    g_assert_cmpuint(ldl_be_p(stream->data + old_end + 1), ==,
                     section.section_id);

    g_byte_array_append(replacement, &marker, sizeof(marker));
    ahead_vmstate_append_be32(replacement, section.section_id);
    g_byte_array_append(replacement, &name_len, sizeof(name_len));
    g_byte_array_append(replacement, (const uint8_t *)legacy_name,
                        name_len);
    ahead_vmstate_append_be32(replacement, legacy_instance);
    ahead_vmstate_append_be32(replacement, legacy_version);
    g_byte_array_append(replacement, legacy_payload, legacy_payload_size);

    g_byte_array_append(updated, stream->data, section.offset);
    g_byte_array_append(updated, replacement->data, replacement->len);
    g_byte_array_append(updated, stream->data + old_end,
                        stream->len - old_end);
    g_byte_array_unref(stream);
    *stream_ptr = g_steal_pointer(&updated);
}

static void ahead_assert_device_vmstate_identities(const GByteArray *stream,
                                                   bool legacy)
{
    g_assert_cmpuint(ahead_vmstate_full_count(
                         stream, AHEAD_CURRENT_PLIC_VMSTATE, 0, 1,
                         AHEAD_CURRENT_PLIC_PAYLOAD_SIZE), ==,
                     legacy ? 0 : 1);
    g_assert_cmpuint(ahead_vmstate_full_count(
                         stream, AHEAD_CURRENT_CLINT_VMSTATE, 0, 1,
                         AHEAD_CURRENT_CLINT_PAYLOAD_SIZE), ==,
                     legacy ? 0 : 1);
    g_assert_cmpuint(ahead_vmstate_full_count(
                         stream, AHEAD_CURRENT_UART_VMSTATE, 0, 1,
                         AHEAD_CURRENT_UART_PAYLOAD_SIZE), ==,
                     legacy ? 0 : 1);
    g_assert_cmpuint(ahead_vmstate_full_count(
                         stream, AHEAD_LEGACY_PLIC_VMSTATE, 0, 1,
                         AHEAD_LEGACY_PLIC_PAYLOAD_SIZE), ==,
                     legacy ? 1 : 0);
    g_assert_cmpuint(ahead_vmstate_full_count(
                         stream, AHEAD_LEGACY_CLINT_VMSTATE, 0, 3,
                         AHEAD_LEGACY_CLINT_PAYLOAD_SIZE), ==,
                     legacy ? 1 : 0);
    g_assert_cmpuint(ahead_vmstate_full_count(
                         stream, AHEAD_LEGACY_UART_VMSTATE, 0, 3,
                         AHEAD_LEGACY_UART_PAYLOAD_SIZE), ==,
                     legacy ? 1 : 0);
}

/*
 * Recast current fixed-size sections into the exact layouts emitted by the
 * first Ahead machine: SiFive PLIC v1, ACLINT MTIMER v3, and serial v3.  The
 * stream-local section IDs do not carry device identity and are preserved.
 */
static void ahead_downgrade_legacy_device_vmstate(const char *path)
{
    g_autoptr(GByteArray) stream = ahead_vmstate_read(path);
    AheadVMStateFullSection section;
    uint8_t legacy_plic[AHEAD_LEGACY_PLIC_PAYLOAD_SIZE];
    uint8_t legacy_clint[AHEAD_LEGACY_CLINT_PAYLOAD_SIZE] = { 0 };
    uint8_t legacy_uart[AHEAD_LEGACY_UART_PAYLOAD_SIZE];
    g_autoptr(GError) error = NULL;
    uint8_t *payload;
    gsize priority_offset = sizeof(uint32_t);
    gsize threshold_offset = priority_offset +
                             AHEAD_PLIC_SOURCES * sizeof(uint32_t);
    gsize pending_offset = threshold_offset +
                           C900_PLIC_CONTEXTS * sizeof(uint32_t);
    gsize active_offset = pending_offset +
                          C900_PLIC_WORDS * sizeof(uint32_t);
    gsize enable_offset = active_offset +
                          C900_PLIC_WORDS * sizeof(uint32_t);
    gsize source_level_offset = enable_offset +
                                AHEAD_PLIC_ENABLE_WORDS * sizeof(uint32_t);
    gsize edge_trigger_offset = source_level_offset +
                                C900_PLIC_WORDS * sizeof(uint32_t);
    uint32_t word;
    uint32_t mask;

    ahead_assert_device_vmstate_identities(stream, false);

    /*
     * The old ACLINT SWI had no section of its own.  Its pending state was
     * carried in each CPU's mip field, from which the compatibility loader
     * reconstructs the C900 MSIP and SSIP banks.
     */
    for (uint32_t hart = 0; hart < C910_HARTS; hart++) {
        uint64_t mip;

        section = ahead_vmstate_find_full(
            stream, AHEAD_CURRENT_CPU_VMSTATE, hart,
            AHEAD_CURRENT_CPU_VMSTATE_VERSION,
            AHEAD_RISCV_CPU_MIP_OFFSET + sizeof(uint64_t), false);
        payload = stream->data + section.payload_offset;
        mip = ldq_be_p(payload + AHEAD_RISCV_CPU_MIP_OFFSET);
        mip &= ~(MIP_MSIP | MIP_SSIP);
        if (hart == 1) {
            mip |= MIP_MSIP;
        } else if (hart == 2) {
            mip |= MIP_SSIP;
        }
        stq_be_p(payload + AHEAD_RISCV_CPU_MIP_OFFSET, mip);
    }

    section = ahead_vmstate_find_full(stream, AHEAD_CURRENT_PLIC_VMSTATE,
                                      0, 1,
                                      AHEAD_CURRENT_PLIC_PAYLOAD_SIZE, true);
    payload = stream->data + section.payload_offset;
    g_assert_cmphex(ldl_be_p(payload), ==, 0);
    g_assert_cmphex(ldl_be_p(payload + priority_offset +
                            AHEAD_LEGACY_PLIC_ACTIVE_IRQ *
                            sizeof(uint32_t)), ==, 6);
    g_assert_cmphex(ldl_be_p(payload + threshold_offset +
                            AHEAD_LEGACY_PLIC_CONTEXT *
                            sizeof(uint32_t)), ==, 2);
    word = AHEAD_LEGACY_PLIC_PENDING_IRQ >> 5;
    mask = BIT(AHEAD_LEGACY_PLIC_PENDING_IRQ & 31);
    g_assert_true(ldl_be_p(payload + pending_offset +
                           word * sizeof(uint32_t)) & mask);
    word = AHEAD_LEGACY_PLIC_ACTIVE_IRQ >> 5;
    mask = BIT(AHEAD_LEGACY_PLIC_ACTIVE_IRQ & 31);
    g_assert_true(ldl_be_p(payload + active_offset +
                           word * sizeof(uint32_t)) & mask);
    g_assert_true(ldl_be_p(payload + enable_offset +
                           (AHEAD_LEGACY_PLIC_CONTEXT * C900_PLIC_WORDS +
                            word) * sizeof(uint32_t)) & mask);
    g_assert_true(ldl_be_p(payload + source_level_offset +
                           word * sizeof(uint32_t)) & mask);
    word = AHEAD_LEGACY_PLIC_EDGE_IRQ >> 5;
    mask = BIT(AHEAD_LEGACY_PLIC_EDGE_IRQ & 31);
    g_assert_true(ldl_be_p(payload + edge_trigger_offset +
                           word * sizeof(uint32_t)) & mask);
    memcpy(legacy_plic, payload + sizeof(uint32_t),
           sizeof(legacy_plic));
    ahead_vmstate_replace_full(&stream, AHEAD_CURRENT_PLIC_VMSTATE, 0, 1,
                               AHEAD_CURRENT_PLIC_PAYLOAD_SIZE,
                               AHEAD_LEGACY_PLIC_VMSTATE, 0, 1,
                               legacy_plic, sizeof(legacy_plic));

    section = ahead_vmstate_find_full(stream, AHEAD_CURRENT_CLINT_VMSTATE,
                                      0, 1,
                                      AHEAD_CURRENT_CLINT_PAYLOAD_SIZE, true);
    payload = stream->data + section.payload_offset;
    g_assert_cmphex(ldq_be_p(payload), ==, 3);
    g_assert_cmphex(ldl_be_p(payload + sizeof(uint64_t) +
                            sizeof(uint32_t)), ==, 1);
    g_assert_cmphex(ldl_be_p(payload + sizeof(uint64_t) +
                            C910_HARTS * sizeof(uint32_t) +
                            2 * sizeof(uint32_t)), ==, 1);
    g_assert_cmphex(ldq_be_p(payload + sizeof(uint64_t) +
                            2 * C910_HARTS * sizeof(uint32_t) +
                            3 * sizeof(uint64_t)), ==, 12);
    g_assert_cmphex(ldq_be_p(payload + sizeof(uint64_t) +
                            2 * C910_HARTS * sizeof(uint32_t) +
                            C910_HARTS * sizeof(uint64_t)), ==, 17);
    stq_be_p(legacy_clint, 3);
    memcpy(legacy_clint + sizeof(uint64_t),
           payload + sizeof(uint64_t) +
           2 * C910_HARTS * sizeof(uint32_t),
           C910_HARTS * sizeof(uint64_t));
    memset(legacy_clint + sizeof(uint64_t) +
           C910_HARTS * sizeof(uint64_t), 0xff,
           C910_HARTS * sizeof(uint64_t));
    stq_be_p(legacy_clint + sizeof(uint64_t) +
             C910_HARTS * sizeof(uint64_t) +
             3 * sizeof(uint64_t), 4000);
    ahead_vmstate_replace_full(&stream, AHEAD_CURRENT_CLINT_VMSTATE, 0, 1,
                               AHEAD_CURRENT_CLINT_PAYLOAD_SIZE,
                               AHEAD_LEGACY_CLINT_VMSTATE, 0, 3,
                               legacy_clint, sizeof(legacy_clint));

    section = ahead_vmstate_find_full(stream, AHEAD_CURRENT_UART_VMSTATE,
                                      0, 1,
                                      AHEAD_CURRENT_UART_PAYLOAD_SIZE, true);
    payload = stream->data + section.payload_offset;
    g_assert_cmphex(lduw_be_p(payload), ==, 0x1234);
    g_assert_cmphex(payload[5], ==, 3);
    g_assert_cmphex(payload[9], ==, 0x5a);
    g_assert_cmphex(ldl_be_p(payload + AHEAD_LEGACY_UART_PAYLOAD_SIZE),
                    ==, 0xb);
    memcpy(legacy_uart, payload, sizeof(legacy_uart));
    ahead_vmstate_replace_full(&stream, AHEAD_CURRENT_UART_VMSTATE, 0, 1,
                               AHEAD_CURRENT_UART_PAYLOAD_SIZE,
                               AHEAD_LEGACY_UART_VMSTATE, 0, 3,
                               legacy_uart, sizeof(legacy_uart));

    ahead_assert_device_vmstate_identities(stream, true);
    g_assert_true(g_file_set_contents(path, (const char *)stream->data,
                                      stream->len, &error));
    g_assert_no_error(error);
}

static void test_ahead_legacy_device_vmstate(void)
{
    const char *args =
        "-machine beaglev-ahead,suppress-vmdesc=on -bios none "
        "-global dw-apb-uart.dlf-width=4";
    g_autofree char *legacy_path = NULL;
    g_autofree char *legacy_uri = NULL;
    g_autofree char *modern_path = NULL;
    g_autofree char *modern_uri = NULL;
    g_autoptr(GByteArray) modern_stream = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-legacy-vmstate-XXXXXX",
                         &legacy_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    legacy_uri = g_strdup_printf("file:%s", legacy_path);
    fd = g_file_open_tmp("beaglev-ahead-modern-vmstate-XXXXXX",
                         &modern_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    modern_uri = g_strdup_printf("file:%s", modern_path);

    src = qtest_init(args);
    dst = qtest_initf("%s -incoming defer", args);
    qtest_irq_intercept_out_named(dst, C900_CLINT_QOM_PATH, "mtimer");

    qtest_writel(src, C900_PLIC_PRIORITY(AHEAD_LEGACY_PLIC_ACTIVE_IRQ), 6);
    qtest_writel(src, C900_PLIC_PRIORITY(AHEAD_LEGACY_PLIC_PENDING_IRQ), 7);
    qtest_writel(src, C900_PLIC_THRESHOLD(AHEAD_LEGACY_PLIC_CONTEXT), 2);
    c900_plic_set_enable(src, AHEAD_LEGACY_PLIC_CONTEXT,
                         AHEAD_LEGACY_PLIC_ACTIVE_IRQ, true);
    c900_plic_set_enable(src, AHEAD_LEGACY_PLIC_CONTEXT,
                         AHEAD_LEGACY_PLIC_PENDING_IRQ, true);
    c900_plic_set_input(src, "source", AHEAD_LEGACY_PLIC_ACTIVE_IRQ, 1);
    g_assert_cmphex(qtest_readl(
                        src, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT)),
                    ==, AHEAD_LEGACY_PLIC_ACTIVE_IRQ);
    c900_plic_set_pending(src, AHEAD_LEGACY_PLIC_PENDING_IRQ, true);
    c900_plic_set_input(src, "edge-trigger", AHEAD_LEGACY_PLIC_EDGE_IRQ,
                        1);
    g_assert_cmphex(qtest_readl(src, C900_PLIC_CONTROL), ==, 0);

    g_assert_cmpint(qtest_clock_set(src, 1000), ==, 1000);
    qtest_writel(src, C900_MSIP(1), 1);
    qtest_writel(src, C900_SSIP(2), 1);
    write_compare(src, C900_MTIMECMP(3), 12);
    write_compare(src, C900_STIMECMP(0), 17);

    qtest_writel(src, DW_UART_LCR, UART_LCR_DLAB | 3);
    qtest_writel(src, DW_UART_RBR_THR_DLL, 0x34);
    qtest_writel(src, DW_UART_IER_DLH, 0x12);
    qtest_writel(src, DW_UART_DLF, 0xb);
    qtest_writel(src, DW_UART_LCR, 3);
    qtest_writel(src, DW_UART_SCR, 0x5a);

    /* Poison fields absent from the old layouts before invoking pre_load. */
    c900_plic_set_input(dst, "source", AHEAD_LEGACY_PLIC_ACTIVE_IRQ, 1);
    c900_plic_set_input(dst, "edge-trigger", AHEAD_LEGACY_PLIC_EDGE_IRQ,
                        1);
    qtest_writel(dst, C900_MSIP(0), 1);
    qtest_writel(dst, C900_SSIP(3), 1);
    write_compare(dst, C900_STIMECMP(0), 42);
    qtest_writel(dst, DW_UART_DLF, 7);
    qtest_writel(dst, DW_UART_SCR, 0xa5);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", legacy_uri);
    wait_for_migration_complete(src);
    ahead_downgrade_legacy_device_vmstate(legacy_path);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        legacy_uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst, C900_PLIC_CONTROL), ==, 1);
    g_assert_cmphex(qtest_readl(dst,
                                C900_PLIC_PRIORITY(
                                    AHEAD_LEGACY_PLIC_ACTIVE_IRQ)), ==, 6);
    g_assert_cmphex(qtest_readl(dst,
                                C900_PLIC_THRESHOLD(
                                    AHEAD_LEGACY_PLIC_CONTEXT)), ==, 2);
    g_assert_true(qtest_readl(
        dst, C900_PLIC_ENABLE(AHEAD_LEGACY_PLIC_CONTEXT,
                              AHEAD_LEGACY_PLIC_ACTIVE_IRQ >> 5)) &
                  BIT(AHEAD_LEGACY_PLIC_ACTIVE_IRQ & 31));
    g_assert_true(c900_plic_pending(dst, AHEAD_LEGACY_PLIC_PENDING_IRQ));
    g_assert_cmphex(qtest_readl(
                        dst, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT)),
                    ==, AHEAD_LEGACY_PLIC_PENDING_IRQ);
    qtest_writel(dst, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT),
                  AHEAD_LEGACY_PLIC_PENDING_IRQ);

    /* The active bitmap maps, while the absent source-level bitmap clears. */
    c900_plic_set_pending(dst, AHEAD_LEGACY_PLIC_ACTIVE_IRQ, true);
    g_assert_cmphex(qtest_readl(
                        dst, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT)),
                    ==, 0);
    qtest_writel(dst, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT),
                  AHEAD_LEGACY_PLIC_ACTIVE_IRQ);
    g_assert_cmphex(qtest_readl(
                        dst, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT)),
                    ==, AHEAD_LEGACY_PLIC_ACTIVE_IRQ);
    qtest_writel(dst, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT),
                  AHEAD_LEGACY_PLIC_ACTIVE_IRQ);
    g_assert_false(c900_plic_pending(dst, AHEAD_LEGACY_PLIC_ACTIVE_IRQ));

    /* The absent edge bitmap defaults to level-triggered operation. */
    qtest_writel(dst, C900_PLIC_PRIORITY(AHEAD_LEGACY_PLIC_EDGE_IRQ), 5);
    c900_plic_set_enable(dst, AHEAD_LEGACY_PLIC_CONTEXT,
                         AHEAD_LEGACY_PLIC_EDGE_IRQ, true);
    c900_plic_set_input(dst, "source", AHEAD_LEGACY_PLIC_EDGE_IRQ, 1);
    g_assert_cmphex(qtest_readl(
                        dst, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT)),
                    ==, AHEAD_LEGACY_PLIC_EDGE_IRQ);
    qtest_writel(dst, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT),
                  AHEAD_LEGACY_PLIC_EDGE_IRQ);
    g_assert_cmphex(qtest_readl(
                        dst, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT)),
                    ==, AHEAD_LEGACY_PLIC_EDGE_IRQ);
    c900_plic_set_input(dst, "source", AHEAD_LEGACY_PLIC_EDGE_IRQ, 0);
    qtest_writel(dst, C900_PLIC_CLAIM(AHEAD_LEGACY_PLIC_CONTEXT),
                  AHEAD_LEGACY_PLIC_EDGE_IRQ);
    g_assert_false(c900_plic_pending(dst, AHEAD_LEGACY_PLIC_EDGE_IRQ));

    g_assert_cmphex(get_csr(dst, 0, CSR_TIME), ==, 3);
    g_assert_cmphex(qtest_readl(dst, C900_MSIP(0)), ==, 0);
    g_assert_cmphex(qtest_readl(dst, C900_MSIP(1)), ==, 1);
    g_assert_cmphex(qtest_readl(dst, C900_SSIP(2)), ==, 1);
    g_assert_cmphex(qtest_readl(dst, C900_SSIP(3)), ==, 0);
    g_assert_cmphex(qtest_readl(dst, C900_MTIMECMP(3)), ==, 12);
    g_assert_cmphex(qtest_readl(dst, C900_MTIMECMP(3) + 4), ==, 0);
    g_assert_cmphex(qtest_readl(dst, C900_STIMECMP(0)), ==, UINT32_MAX);
    g_assert_cmphex(qtest_readl(dst, C900_STIMECMP(0) + 4), ==,
                    UINT32_MAX);
    assert_no_irq(dst);
    qtest_clock_step(dst, 2999);
    g_assert_cmphex(get_csr(dst, 0, CSR_TIME), ==, 11);
    assert_no_irq(dst);
    qtest_clock_step(dst, 1);
    g_assert_cmphex(get_csr(dst, 0, CSR_TIME), ==, 12);
    assert_only_irq(dst, 3);

    g_assert_cmphex(qtest_readl(dst, DW_UART_LCR), ==, 3);
    g_assert_cmphex(qtest_readl(dst, DW_UART_SCR), ==, 0x5a);
    g_assert_cmphex(qtest_readl(dst, DW_UART_DLF), ==, 0);
    qtest_writel(dst, DW_UART_LCR, UART_LCR_DLAB | 3);
    g_assert_cmphex(qtest_readl(dst, DW_UART_RBR_THR_DLL), ==, 0x34);
    g_assert_cmphex(qtest_readl(dst, DW_UART_IER_DLH), ==, 0x12);
    qtest_writel(dst, DW_UART_LCR, 3);

    /* Loading old identities must not make a subsequent save emit them. */
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", modern_uri);
    wait_for_migration_complete(dst);
    modern_stream = ahead_vmstate_read(modern_path);
    ahead_assert_device_vmstate_identities(modern_stream, false);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(legacy_path), ==, 0);
    g_assert_cmpint(g_unlink(modern_path), ==, 0);
}

static void test_dwcmshc_emmc_tuning_migration(void)
{
    const uint64_t base = TH1520_EMMC_BASE;
    const uint32_t transfer_state = SDHC_CMD_INHIBIT |
                                    SDHC_DATA_INHIBIT |
                                    SDHC_DAT_LINE_ACTIVE |
                                    SDHC_DOING_READ |
                                    SDHC_DOING_WRITE |
                                    SDHC_SPACE_AVAILABLE |
                                    SDHC_DATA_AVAILABLE;
    g_autofree char *src_image = dwcmshc_create_image(NULL, 0);
    g_autofree char *mid_image = dwcmshc_create_image(NULL, 0);
    g_autofree char *dst_image = dwcmshc_create_image(NULL, 0);
    g_autofree char *pending_image = dwcmshc_create_image(NULL, 0);
    g_autofree char *final_image = dwcmshc_create_image(NULL, 0);
    g_autofree char *first_path = NULL;
    g_autofree char *second_path = NULL;
    g_autofree char *third_path = NULL;
    g_autofree char *fourth_path = NULL;
    g_autofree char *first_uri = NULL;
    g_autofree char *second_uri = NULL;
    g_autofree char *third_uri = NULL;
    g_autofree char *fourth_uri = NULL;
    QTestState *src;
    QTestState *mid;
    QTestState *dst;
    QTestState *pending;
    QTestState *final;
    uint8_t ext_csd[DWCMSHC_BLOCK_SIZE];
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-emmc-tuning-1-XXXXXX",
                         &first_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    fd = g_file_open_tmp("beaglev-ahead-emmc-tuning-2-XXXXXX",
                         &second_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    fd = g_file_open_tmp("beaglev-ahead-emmc-tuning-3-XXXXXX",
                         &third_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    fd = g_file_open_tmp("beaglev-ahead-emmc-tuning-4-XXXXXX",
                         &fourth_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    first_uri = g_strdup_printf("file:%s", first_path);
    second_uri = g_strdup_printf("file:%s", second_path);
    third_uri = g_strdup_printf("file:%s", third_path);
    fourth_uri = g_strdup_printf("file:%s", fourth_path);

    src = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=0,file=%s,format=raw,auto-read-only=off",
        src_image);
    mid = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=0,file=%s,format=raw,auto-read-only=off "
        "-incoming defer",
        mid_image);

    dwcmshc_init_emmc(src, base);
    dwcmshc_emmc_enter_hs200(src, base);
    qtest_writew(src, base + SDHC_HOSTCTL2, DWCMSHC_UHS_MODE_HS200);

    /* Migrate with one byte consumed from the controller's 128-byte FIFO. */
    dwcmshc_issue_emmc_tuning(src, base, DWCMSHC_EMMC_TUNING_SIZE);
    g_assert_cmphex(qtest_readb(src, base + SDHC_BDATA), ==,
                    dwcmshc_emmc_tuning_pattern[0]);
    g_assert_true(qtest_readl(src, base + SDHC_PRNSTS) &
                  SDHC_DATA_AVAILABLE);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", first_uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(mid,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        first_uri);
    wait_for_migration_complete(mid);

    g_assert_cmphex(qtest_readb(mid, base + SDHC_HOSTCTL) &
                    SDHC_CTRL_8BITBUS, ==, SDHC_CTRL_8BITBUS);
    g_assert_cmphex(qtest_readw(mid, base + SDHC_HOSTCTL2) &
                    R_SDHC_HOSTCTL2_UHS_MODE_SEL_MASK, ==,
                    DWCMSHC_UHS_MODE_HS200);
    for (size_t i = 1; i < sizeof(dwcmshc_emmc_tuning_pattern); i++) {
        g_assert_cmphex(qtest_readb(mid, base + SDHC_BDATA + (i & 3)), ==,
                        dwcmshc_emmc_tuning_pattern[i]);
    }
    g_assert_cmphex(qtest_readl(mid, base + SDHC_PRNSTS) &
                    (SDHC_DATA_AVAILABLE | SDHC_DATA_INHIBIT |
                     SDHC_DAT_LINE_ACTIVE | SDHC_DOING_READ), ==, 0);
    g_assert_cmphex(qtest_readw(mid, base + SDHC_BLKCNT), ==, 0);

    /* The migrated card must accept and complete a fresh fixed-size CMD21. */
    dwcmshc_read_emmc_tuning(mid, base);

    /* Migrate an armed Execute Tuning request before CMD21 is issued. */
    qtest_writew(mid, base + SDHC_NORINTSTS, UINT16_MAX);
    qtest_writew(mid, base + SDHC_NORINTSTSEN, SDHC_NISEN_RBUFRDY);
    qtest_writew(mid, base + SDHC_NORINTSIGEN, SDHC_NIS_RBUFRDY);
    qtest_writew(mid, base + SDHC_HOSTCTL2,
                 DWCMSHC_UHS_MODE_HS200 |
                 R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK);

    qtest_quit(src);
    dst = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=0,file=%s,format=raw,auto-read-only=off "
        "-incoming defer",
        dst_image);

    qtest_qmp_assert_success(mid,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", second_uri);
    wait_for_migration_complete(mid);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        second_uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readw(dst, base + SDHC_HOSTCTL2) &
                    (R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK |
                     R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK), ==,
                    R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK);
    qtest_irq_intercept_out_named(dst, TH1520_EMMC_QOM_PATH,
                                  "sysbus-irq");
    dwcmshc_issue_emmc_tuning(dst, base, DWCMSHC_EMMC_TUNING_SIZE);
    g_assert_cmphex(qtest_readw(dst, base + SDHC_NORINTSTS) &
                    (SDHC_NIS_CMDCMP | SDHC_NIS_TRSCMP |
                     SDHC_NIS_RBUFRDY), ==, SDHC_NIS_RBUFRDY);
    g_assert_cmphex(qtest_readw(dst, base + SDHC_HOSTCTL2) &
                    (R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK |
                     R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK), ==,
                    R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK);
    assert_only_irq(dst, 0);

    /* Preserve the completed tuning interrupt and reconstruct its IRQ. */
    qtest_quit(mid);
    pending = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=0,file=%s,format=raw,auto-read-only=off "
        "-incoming defer",
        pending_image);
    qtest_irq_intercept_out_named(pending, TH1520_EMMC_QOM_PATH,
                                  "sysbus-irq");
    assert_no_irq(pending);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", third_uri);
    wait_for_migration_complete(dst);
    qtest_qmp_assert_success(pending,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        third_uri);
    wait_for_migration_complete(pending);

    g_assert_cmphex(qtest_readw(pending, base + SDHC_NORINTSTS) &
                    (SDHC_NIS_CMDCMP | SDHC_NIS_TRSCMP |
                     SDHC_NIS_RBUFRDY), ==, SDHC_NIS_RBUFRDY);
    g_assert_cmphex(qtest_readw(pending, base + SDHC_NORINTSTSEN) &
                    SDHC_NISEN_RBUFRDY, ==, SDHC_NISEN_RBUFRDY);
    g_assert_cmphex(qtest_readw(pending, base + SDHC_NORINTSIGEN) &
                    SDHC_NIS_RBUFRDY, ==, SDHC_NIS_RBUFRDY);
    g_assert_cmphex(qtest_readw(pending, base + SDHC_HOSTCTL2) &
                    (R_SDHC_HOSTCTL2_EXECUTE_TUNING_MASK |
                     R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK), ==,
                    R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK);
    g_assert_cmphex(qtest_readw(pending, base + SDHC_HOSTCTL2) &
                    R_SDHC_HOSTCTL2_UHS_MODE_SEL_MASK, ==,
                    DWCMSHC_UHS_MODE_HS200);
    g_assert_cmphex(qtest_readl(pending, base + SDHC_PRNSTS) &
                    transfer_state, ==, 0);
    g_assert_cmphex(qtest_readw(pending, base + SDHC_BLKCNT), ==, 0);
    g_assert_cmphex(qtest_readw(pending, base + SDHC_ERRINTSTS), ==, 0);
    assert_only_irq(pending, 0);

    qtest_writeb(pending, base + SDHC_SWRST, SDHC_RESET_DATA);
    g_assert_cmphex(qtest_readw(pending, base + SDHC_NORINTSTS) &
                    SDHC_NIS_RBUFRDY, ==, 0);
    g_assert_cmphex(qtest_readw(pending, base + SDHC_HOSTCTL2) &
                    R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK, ==,
                    R_SDHC_HOSTCTL2_SAMPLING_CLKSEL_MASK);
    assert_no_irq(pending);

    dwcmshc_emmc_switch_ok(pending, base, EMMC_EXT_CSD_HS_TIMING,
                           EMMC_HS_TIMING_HS);
    dwcmshc_emmc_switch_ok(pending, base, EMMC_EXT_CSD_BUS_WIDTH,
                           EMMC_BUS_WIDTH_DDR_8);
    dwcmshc_emmc_switch_ok(pending, base, EMMC_EXT_CSD_HS_TIMING,
                           EMMC_HS_TIMING_HS400);
    qtest_writew(pending, base + SDHC_HOSTCTL2, DWCMSHC_UHS_MODE_HS400);
    dwcmshc_assert_emmc_mode(pending, base, EMMC_BUS_WIDTH_DDR_8,
                             EMMC_HS_TIMING_HS400);

    qtest_quit(dst);
    final = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=0,file=%s,format=raw,auto-read-only=off "
        "-incoming defer",
        final_image);
    qtest_qmp_assert_success(pending,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", fourth_uri);
    wait_for_migration_complete(pending);
    qtest_qmp_assert_success(final,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        fourth_uri);
    wait_for_migration_complete(final);

    g_assert_cmphex(qtest_readb(final, base + SDHC_HOSTCTL) &
                    SDHC_CTRL_8BITBUS, ==, SDHC_CTRL_8BITBUS);
    g_assert_cmphex(qtest_readw(final, base + SDHC_HOSTCTL2) &
                    R_SDHC_HOSTCTL2_UHS_MODE_SEL_MASK, ==,
                    DWCMSHC_UHS_MODE_HS400);
    dwcmshc_read_ext_csd(final, base, ext_csd);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_BUS_WIDTH], ==,
                    EMMC_BUS_WIDTH_DDR_8);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_HS_TIMING], ==,
                    EMMC_HS_TIMING_HS400);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_REV], ==, 8);
    g_assert_cmphex(ext_csd[EMMC_EXT_CSD_CARD_TYPE], ==, 0x57);

    qtest_quit(final);
    qtest_quit(pending);
    g_assert_cmpint(g_unlink(first_path), ==, 0);
    g_assert_cmpint(g_unlink(second_path), ==, 0);
    g_assert_cmpint(g_unlink(third_path), ==, 0);
    g_assert_cmpint(g_unlink(fourth_path), ==, 0);
    g_assert_cmpint(g_unlink(src_image), ==, 0);
    g_assert_cmpint(g_unlink(mid_image), ==, 0);
    g_assert_cmpint(g_unlink(dst_image), ==, 0);
    g_assert_cmpint(g_unlink(pending_image), ==, 0);
    g_assert_cmpint(g_unlink(final_image), ==, 0);
}

static void test_dwcmshc_adma_migration(void)
{
    enum {
        BLOCKS = 6,
        DESCS_PER_DELAY = 5,
        DESCRIPTOR_SIZE = 16,
        TRANSFER_DELAY_NS = 100,
    };
    const uint64_t base = TH1520_SDIO0_BASE;
    uint8_t expected[BLOCKS * DWCMSHC_BLOCK_SIZE];
    uint8_t actual[sizeof(expected)];
    uint8_t descriptors[BLOCKS * DESCRIPTOR_SIZE] = {};
    g_autofree char *src_image = NULL;
    g_autofree char *dst_image = NULL;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    int fd;

    for (unsigned block = 0; block < BLOCKS; block++) {
        memset(expected + block * DWCMSHC_BLOCK_SIZE, 0x10 + block,
               DWCMSHC_BLOCK_SIZE);
        descriptors[block * DESCRIPTOR_SIZE] =
            SDHC_ADMA_ATTR_VALID | SDHC_ADMA_ATTR_ACT_TRAN |
            (block == BLOCKS - 1 ? SDHC_ADMA_ATTR_END : 0);
        stw_le_p(descriptors + block * DESCRIPTOR_SIZE + 2,
                 DWCMSHC_BLOCK_SIZE);
        stq_le_p(descriptors + block * DESCRIPTOR_SIZE + 4,
                 DWCMSHC_ADMA_DATA_ADDR + block * DWCMSHC_BLOCK_SIZE);
    }
    src_image = dwcmshc_create_image(expected, sizeof(expected));
    dst_image = dwcmshc_create_image(expected, sizeof(expected));
    fd = g_file_open_tmp("beaglev-ahead-adma-migration-XXXXXX", &path,
                         NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=1,file=%s,format=raw,auto-read-only=off",
        src_image);
    dst = qtest_initf(
        "-machine beaglev-ahead -bios none "
        "-drive if=sd,index=1,file=%s,format=raw,auto-read-only=off "
        "-incoming defer",
        dst_image);

    dwcmshc_init_sd(src, base);

    qtest_memwrite(src, DWCMSHC_ADMA_DESC_ADDR, descriptors,
                   sizeof(descriptors));
    qtest_memset(src, DWCMSHC_ADMA_DATA_ADDR, 0xa5, sizeof(expected));
    qtest_writeb(src, base + SDHC_HOSTCTL, SDHC_CTRL_ADMA2_32);
    qtest_writew(src, base + SDHC_HOSTCTL2,
                  R_SDHC_HOSTCTL2_VERSION4_MASK |
                  R_SDHC_HOSTCTL2_ADDRESSING_MASK |
                  R_SDHC_HOSTCTL2_CMD23_ENA_MASK);
    qtest_writew(src, base + SDHC_NORINTSTS, UINT16_MAX);
    qtest_writew(src, base + SDHC_ERRINTSTS, UINT16_MAX);
    qtest_writew(src, base + SDHC_NORINTSTSEN,
                  SDHC_NISEN_CMDCMP | SDHC_NISEN_TRSCMP);
    qtest_writew(src, base + SDHC_NORINTSIGEN, 0);
    dwcmshc_write_adma_address(src, base, DWCMSHC_ADMA_DESC_ADDR);

    /*
     * SDHCI processes five descriptors synchronously, then arms its
     * 100 ns transfer timer for the sixth.  Do not touch its MMIO space
     * between this command and migration: an MMIO access resumes the timer.
     */
    sdhci_cmd_regs(src, base, DWCMSHC_BLOCK_SIZE, BLOCKS, 0,
                   SDHC_TRNS_DMA | SDHC_TRNS_BLK_CNT_EN |
                   SDHC_TRNS_ACMD_AUTO | SDHC_TRNS_READ | SDHC_TRNS_MULTI,
                   (18 << 8) | SDHC_CMD_RESPONSE | SDHC_CMD_DATA_PRESENT);
    qtest_memread(src, DWCMSHC_ADMA_DATA_ADDR, actual, sizeof(actual));
    g_assert_cmpmem(actual, DESCS_PER_DELAY * DWCMSHC_BLOCK_SIZE,
                    expected, DESCS_PER_DELAY * DWCMSHC_BLOCK_SIZE);
    for (size_t i = DESCS_PER_DELAY * DWCMSHC_BLOCK_SIZE;
         i < sizeof(actual); i++) {
        g_assert_cmphex(actual[i], ==, 0xa5);
    }
    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    qtest_clock_step(dst, TRANSFER_DELAY_NS - 1);
    qtest_memread(dst, DWCMSHC_ADMA_DATA_ADDR, actual, sizeof(actual));
    g_assert_cmpmem(actual, DESCS_PER_DELAY * DWCMSHC_BLOCK_SIZE,
                    expected, DESCS_PER_DELAY * DWCMSHC_BLOCK_SIZE);
    for (size_t i = DESCS_PER_DELAY * DWCMSHC_BLOCK_SIZE;
         i < sizeof(actual); i++) {
        g_assert_cmphex(actual[i], ==, 0xa5);
    }

    qtest_clock_step(dst, 1);
    qtest_memread(dst, DWCMSHC_ADMA_DATA_ADDR, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    g_assert_cmphex(dwcmshc_read_adma_address(dst, base), ==,
                    DWCMSHC_ADMA_DESC_ADDR + sizeof(descriptors));
    g_assert_cmphex(qtest_readw(dst, base + SDHC_BLKCNT), ==, 0);
    g_assert_true(qtest_readw(dst, base + SDHC_NORINTSTS) & SDHC_NIS_TRSCMP);

    qtest_quit(dst);
    qtest_quit(src);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_unlink(src_image), ==, 0);
    g_assert_cmpint(g_unlink(dst_image), ==, 0);
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
    qtest_irq_intercept_out_named(dst, TH1520_MISCSYS_QOM_PATH,
                                  "storage-reset");

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
    qtest_writel(src, TH1520_MISCSYS_BASE + 0x00c, 0);

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
    g_assert_cmphex(qtest_readl(dst, TH1520_MISCSYS_BASE + 0x00c), ==, 0);
    g_assert_false(qtest_get_irq(dst, TH1520_MISCSYS_STORAGE_EMMC));
    g_assert_true(qtest_get_irq(dst, TH1520_MISCSYS_STORAGE_SDIO0));
    g_assert_false(qtest_get_irq(dst, TH1520_MISCSYS_STORAGE_SDIO1));
    assert_dwcmshc_reset_state(dst, TH1520_SDIO0_BASE);

    qtest_system_reset(dst);
    for (size_t i = 0; i < ARRAY_SIZE(dwcmshc_controllers); i++) {
        assert_dwcmshc_reset_state(dst, dwcmshc_controllers[i].base);
        g_assert_false(qtest_get_irq(dst, i));
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

static void test_mr75203_migration(void)
{
    const int32_t temperature = -22222;
    const int32_t voltage = 1111;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    QTestState *src;
    QTestState *dst;
    uint16_t raw;
    int fd;

    fd = g_file_open_tmp("beaglev-ahead-pvt-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);

    src = qtest_init("-machine beaglev-ahead -bios none");
    dst = qtest_init("-machine beaglev-ahead -bios none -incoming defer");

    mr75203_qom_set(src, "temperature[0]", temperature);
    mr75203_qom_set(src, "voltage[7]", voltage);
    mr75203_program_ts(src);
    mr75203_program_vm(src);
    qtest_readl(src, TH1520_PVT_TS_BASE + MR75203_SDIF_DATA(0));
    qtest_readl(src, TH1520_PVT_VM_BASE + MR75203_VM_DATA(0, 7));
    qtest_writel(src, TH1520_PVT_COMMON_BASE + MR75203_ID_NUM, 0xa5a55a5a);
    qtest_writel(src, TH1520_PVT_COMMON_BASE + MR75203_SCRATCH, 0x11223344);
    qtest_writel(src, TH1520_PVT_COMMON_BASE + MR75203_REG_LOCK, 0x55);

    qtest_qmp_assert_success(src,
        "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    wait_for_migration_complete(src);
    qtest_qmp_assert_success(dst,
        "{ 'execute': 'migrate-incoming', 'arguments': { 'uri': %s } }",
        uri);
    wait_for_migration_complete(dst);

    g_assert_cmphex(qtest_readl(dst,
                                TH1520_PVT_COMMON_BASE + MR75203_ID_NUM),
                    ==, 0xa5a55a5a);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_PVT_COMMON_BASE + MR75203_SCRATCH),
                    ==, 0x11223344);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_PVT_COMMON_BASE + MR75203_REG_LOCK),
                    ==, 0x55);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_PVT_COMMON_BASE +
                                MR75203_LOCK_STATUS), ==, 1);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_PVT_TS_BASE + MR75203_CLK_SYNTH),
                    ==, MR75203_CLK_SYNTH_VALUE);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_PVT_TS_BASE + MR75203_SDIF_STATUS),
                    ==, MR75203_SDIF_LOCK);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_PVT_VM_BASE + MR75203_CLK_SYNTH),
                    ==, MR75203_CLK_SYNTH_VALUE);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_PVT_VM_BASE + MR75203_SDIF_STATUS),
                    ==, MR75203_SDIF_LOCK);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_PVT_TS_BASE +
                                MR75203_SAMPLE_COUNT), ==, 1);
    g_assert_cmphex(qtest_readl(dst,
                                TH1520_PVT_VM_BASE +
                                MR75203_SAMPLE_COUNT), ==, 1);
    g_assert_cmpint(mr75203_qom_get(dst, "temperature[0]"), ==, temperature);
    g_assert_cmpint(mr75203_qom_get(dst, "voltage[7]"), ==, voltage);

    raw = qtest_readl(dst,
                      TH1520_PVT_TS_BASE + MR75203_SDIF_DATA(0));
    g_assert_cmpint(llabs(mr75203_temperature_from_raw(raw) - temperature),
                    <=, 55);
    raw = qtest_readl(dst,
                      TH1520_PVT_VM_BASE + MR75203_VM_DATA(0, 7));
    g_assert_cmpint(mr75203_voltage_from_raw(raw), ==, voltage);

    qtest_system_reset(dst);
    assert_mr75203_reset_state(dst);
    g_assert_cmpint(mr75203_qom_get(dst, "temperature[0]"), ==, temperature);
    g_assert_cmpint(mr75203_qom_get(dst, "voltage[7]"), ==, voltage);

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
    set_csr(src, 1, CSR_MSTATUS,
            get_csr(src, 1, CSR_MSTATUS) | BIT_ULL(13));
    set_csr(src, 1, CSR_TH_FXCR, UINT64_MAX);
    set_csr(src, 1, CSR_FCSR, 0x45);
    g_assert_cmphex(get_csr(src, 1, CSR_TH_FXCR), ==, 0x02800025);
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
    g_assert_cmphex(get_csr(dst, 1, CSR_TH_FXCR), ==, 0x02800025);
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
        qtest_add_func("/beaglev-ahead/boot/mask-rom-contract",
                       test_mask_rom_contract);
        qtest_add_func("/beaglev-ahead/boot/mask-rom-execution-reset",
                       test_mask_rom_execution_reset);
        qtest_add_func("/beaglev-ahead/boot/mask-rom-errors",
                       test_mask_rom_errors);
        qtest_add_func("/beaglev-ahead/boot/external-dtb",
                       test_external_dtb);
        qtest_add_func("/beaglev-ahead/cpr/clock-registers",
                       test_ap_clock_registers);
        qtest_add_func("/beaglev-ahead/cpr/pll-poll-execution",
                       test_ap_clock_poll_execution);
        qtest_add_func("/beaglev-ahead/cpr/reset-registers",
                       test_ap_reset_registers);
        qtest_add_func("/beaglev-ahead/cpr/reset-outputs",
                       test_ap_reset_outputs);
        qtest_add_func("/beaglev-ahead/cpr/migration",
                       test_ap_cpr_migration);
        qtest_add_func("/beaglev-ahead/cpr/clock-gate-outputs",
                       test_ap_clock_gate_outputs);
        qtest_add_func("/beaglev-ahead/cpr/timed-clock-gates",
                       test_ap_clock_timed_gates);
        qtest_add_func("/beaglev-ahead/cpr/clock-gate-migration",
                       test_ap_clock_gate_migration);
        qtest_add_func("/beaglev-ahead/ddr-pll/registers",
                       test_th1520_ddr_pll_registers);
        qtest_add_func("/beaglev-ahead/ddr-pll/migration",
                       test_th1520_ddr_pll_migration);
        qtest_add_func("/beaglev-ahead/ddr-control/registers",
                       test_th1520_ddr_control_registers);
        qtest_add_func("/beaglev-ahead/ddr-control/migration",
                       test_th1520_ddr_control_migration);
        qtest_add_func("/beaglev-ahead/ddr/registers",
                       test_th1520_ddr_registers);
        qtest_add_func("/beaglev-ahead/ddr/migration",
                       test_th1520_ddr_migration);
        qtest_add_func("/beaglev-ahead/miscsys/clock-outputs",
                       test_miscsys_clock_outputs);
        qtest_add_func("/beaglev-ahead/miscsys/clock-migration",
                       test_miscsys_clock_migration);
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
        qtest_add_func("/beaglev-ahead/ap6203bm/control-wake",
                       test_ap6203bm_control_wake);
        qtest_add_func("/beaglev-ahead/ap6203bm/migration",
                       test_ap6203bm_migration);
        qtest_add_func("/beaglev-ahead/board/leds", test_board_leds);
        qtest_add_func("/beaglev-ahead/dw-i2c/registers",
                       test_dw_i2c_registers);
        qtest_add_func("/beaglev-ahead/dw-i2c/eeprom",
                       test_dw_i2c_eeprom);
        qtest_add_func("/beaglev-ahead/dw-i2c/eeprom-backing",
                       test_dw_i2c_eeprom_backing);
        qtest_add_func("/beaglev-ahead/dw-i2c/pmic",
                       test_aon_i2c_pmic);
        qtest_add_func("/beaglev-ahead/dw-i2c/aon-implicit-stop",
                       test_aon_i2c_implicit_stop);
        qtest_add_func("/beaglev-ahead/dw-i2c/interrupts",
                       test_dw_i2c_interrupts);
        qtest_add_func("/beaglev-ahead/dw-i2c/migration",
                       test_dw_i2c_migration);
        qtest_add_func("/beaglev-ahead/dw-i2c/pmic-migration",
                       test_aon_i2c_pmic_migration);
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
        qtest_add_func("/beaglev-ahead/th1520-pwm/gated-migration",
                       test_th1520_pwm_gated_migration);
        qtest_add_func("/beaglev-ahead/cpr/peripheral-resets",
                       test_ap_reset_peripherals);
        qtest_add_func("/beaglev-ahead/th1520-mbox/registers",
                       test_th1520_mbox_registers);
        qtest_add_func("/beaglev-ahead/th1520-mbox/migration",
                       test_th1520_mbox_migration);
        qtest_add_func("/beaglev-ahead/th1520-iopmp/registers",
                       test_th1520_iopmp_registers);
        qtest_add_func("/beaglev-ahead/th1520-iopmp/migration",
                       test_th1520_iopmp_migration);
        qtest_add_func("/beaglev-ahead/th1520-video-sysreg/registers",
                       test_th1520_video_sysreg_registers);
        qtest_add_func("/beaglev-ahead/th1520-video-sysreg/migration",
                       test_th1520_video_sysreg_migration);
        qtest_add_func("/beaglev-ahead/th1520-iso7816-config/registers",
                       test_th1520_iso7816_config_registers);
        qtest_add_func("/beaglev-ahead/th1520-iso7816-config/migration",
                       test_th1520_iso7816_config_migration);
        qtest_add_func("/beaglev-ahead/th1520-pmp-portal/registers",
                       test_th1520_pmp_portal_registers);
        qtest_add_func("/beaglev-ahead/th1520-pmp-portal/migration",
                       test_th1520_pmp_portal_migration);
        qtest_add_func("/beaglev-ahead/th1520-bootsel/registers",
                       test_th1520_bootsel_registers);
        qtest_add_func("/beaglev-ahead/th1520-bootsel/migration",
                       test_th1520_bootsel_migration);
        qtest_add_func("/beaglev-ahead/th1520-tee-miscsys-clock/registers",
                       test_th1520_tee_miscsys_clock_registers);
        qtest_add_func("/beaglev-ahead/th1520-tee-miscsys-clock/migration",
                       test_th1520_tee_miscsys_clock_migration);
        qtest_add_func("/beaglev-ahead/th1520-tee-dsp-reset/registers",
                       test_th1520_tee_dsp_reset_registers);
        qtest_add_func("/beaglev-ahead/th1520-tee-dsp-reset/migration",
                       test_th1520_tee_dsp_reset_migration);
        qtest_add_func("/beaglev-ahead/th1520-tee-vosys-dpu-reset/registers",
                       test_th1520_tee_vosys_dpu_reset_registers);
        qtest_add_func("/beaglev-ahead/th1520-tee-vosys-dpu-reset/migration",
                       test_th1520_tee_vosys_dpu_reset_migration);
        qtest_add_func("/beaglev-ahead/th1520-aon-audio-reset/registers",
                       test_th1520_aon_audio_reset_registers);
        qtest_add_func("/beaglev-ahead/th1520-aon-audio-reset/migration",
                       test_th1520_aon_audio_reset_migration);
        qtest_add_func("/beaglev-ahead/mr75203/registers",
                       test_mr75203_registers);
        qtest_add_func("/beaglev-ahead/mr75203/migration",
                       test_mr75203_migration);
        qtest_add_func("/beaglev-ahead/usb/registers",
                       test_th1520_usb_registers);
        qtest_add_func("/beaglev-ahead/usb/vendor-uboot-clock-alias",
                       test_th1520_vendor_uboot_usb_clock_alias);
        qtest_add_func("/beaglev-ahead/usb/reset-outputs",
                       test_th1520_usb_reset_outputs);
        qtest_add_func("/beaglev-ahead/usb/host-dma-irq",
                       test_th1520_usb_host_dma_irq);
        qtest_add_func("/beaglev-ahead/usb/pending-irq-migration",
                       test_th1520_usb_pending_irq_migration);
        if (qtest_has_device("usb-kbd")) {
            qtest_add_func("/beaglev-ahead/usb/hid-hotplug",
                           test_th1520_usb_hid_hotplug);
        }
        qtest_add_func("/beaglev-ahead/usb/migration",
                       test_th1520_usb_migration);
        qtest_add_func("/beaglev-ahead/xgene-rtc/registers-timing-irq",
                       test_xgene_rtc_registers_timing_irq);
        qtest_add_func("/beaglev-ahead/xgene-rtc/migration",
                       test_xgene_rtc_migration);
        qtest_add_func("/beaglev-ahead/dw-timer/registers",
                       test_dw_timer_registers);
        qtest_add_func("/beaglev-ahead/dw-timer/timing",
                       test_dw_timer_timing);
        qtest_add_func("/beaglev-ahead/dw-timer/toggle-pwm",
                       test_dw_timer_toggle_pwm);
        qtest_add_func("/beaglev-ahead/dw-timer/interrupt-routes",
                       test_dw_timer_interrupt_routes);
        qtest_add_func("/beaglev-ahead/dw-timer/migration",
                       test_dw_timer_migration);
        qtest_add_func("/beaglev-ahead/dw-timer/toggle-migration",
                       test_dw_timer_toggle_migration);
        qtest_add_func("/beaglev-ahead/dw-wdt/registers",
                       test_dw_wdt_registers);
        qtest_add_func("/beaglev-ahead/dw-wdt/timing",
                       test_dw_wdt_timing);
        for (size_t i = 0; i < ARRAY_SIZE(th1520_wdts); i++) {
            g_autofree char *name =
                g_strdup_printf("/beaglev-ahead/dw-wdt/%s-interrupt",
                                th1520_wdts[i].name);

            qtest_add_data_func(name, &th1520_wdts[i],
                                test_dw_wdt_interrupt_route);
        }
        qtest_add_func("/beaglev-ahead/dw-wdt/reset-outputs",
                       test_dw_wdt_reset_outputs);
        qtest_add_func("/beaglev-ahead/dw-wdt/action-reset",
                       test_dw_wdt_action_reset);
        qtest_add_func("/beaglev-ahead/dw-wdt/migration",
                       test_dw_wdt_migration);
        qtest_add_func("/beaglev-ahead/dmac/registers",
                       test_dmac_registers);
        qtest_add_func("/beaglev-ahead/dmac/direct-transfer",
                       test_dmac_direct_transfer);
        qtest_add_func("/beaglev-ahead/dmac/width-fixed-address",
                       test_dmac_width_and_fixed_address);
        qtest_add_func("/beaglev-ahead/dmac/advertised-widths",
                       test_dmac_advertised_widths);
        qtest_add_func("/beaglev-ahead/dmac/linked-list",
                       test_dmac_linked_list);
        qtest_add_func("/beaglev-ahead/dmac/migration",
                       test_dmac_migration);
        qtest_add_func("/beaglev-ahead/migration/whole-machine",
                       test_whole_machine_migration);
        qtest_add_func("/beaglev-ahead/migration/legacy-device-vmstate",
                       test_ahead_legacy_device_vmstate);
        qtest_add_func("/beaglev-ahead/gmac/registers",
                       test_gmac_registers);
        qtest_add_func("/beaglev-ahead/gmac/phy-gpio",
                       test_gmac_phy_gpio);
        qtest_add_func("/beaglev-ahead/gmac/phy-gpio-migration",
                       test_gmac_phy_gpio_migration);
        for (size_t i = 0; i < ARRAY_SIZE(th1520_gmac_controllers); i++) {
            g_autofree char *name =
                g_strdup_printf("/beaglev-ahead/gmac/%s-interrupt",
                                th1520_gmac_controllers[i].name);

            qtest_add_data_func(name, &th1520_gmac_controllers[i],
                                test_gmac_interrupt);
        }
#ifndef _WIN32
        qtest_add_func("/beaglev-ahead/gmac/rx-filter-perfect",
                       test_gmac_rx_filter_perfect);
        qtest_add_func("/beaglev-ahead/gmac/rx-filter-matrix",
                       test_gmac_rx_filter_matrix);
        qtest_add_func("/beaglev-ahead/gmac/rx-filter-migration",
                       test_gmac_rx_filter_migration);
        qtest_add_func("/beaglev-ahead/gmac/rx-ring-full-backpressure",
                       test_gmac_rx_ring_full_backpressure);
        qtest_add_func("/beaglev-ahead/gmac/rx-suspend-midframe",
                       test_gmac_rx_suspend_midframe);
        qtest_add_func("/beaglev-ahead/gmac/rx-fifo-overflow",
                       test_gmac_rx_fifo_overflow);
        qtest_add_func("/beaglev-ahead/gmac/rx-fifo-order",
                       test_gmac_rx_fifo_order);
        qtest_add_func("/beaglev-ahead/gmac/rx-fifo-migration",
                       test_gmac_rx_fifo_migration);
        qtest_add_func("/beaglev-ahead/gmac/tx-suspend-midframe",
                       test_gmac_tx_suspend_midframe);
        qtest_add_func("/beaglev-ahead/gmac/tx-suspend-migration",
                       test_gmac_tx_suspend_migration);
        qtest_add_func("/beaglev-ahead/gmac/rx-checksum-type2",
                       test_gmac_rx_checksum_type2);
        qtest_add_func("/beaglev-ahead/gmac/rx-checksum-type2-split",
                       test_gmac_rx_checksum_type2_split);
        qtest_add_func("/beaglev-ahead/gmac/tx-checksum",
                       test_gmac_tx_checksum);
        qtest_add_func("/beaglev-ahead/gmac/enhanced-descriptors",
                       test_gmac_enhanced_descriptors);
        qtest_add_func("/beaglev-ahead/gmac/rx-interrupt-watchdog",
                       test_gmac_rx_interrupt_watchdog);
        qtest_add_func("/beaglev-ahead/gmac/rx-watchdog-migration",
                       test_gmac_rx_watchdog_migration);
#endif
        qtest_add_func("/beaglev-ahead/dwcmshc/registers",
                       test_dwcmshc_registers);
        qtest_add_func("/beaglev-ahead/dwcmshc/reset-outputs",
                       test_storage_reset_outputs);
        qtest_add_func("/beaglev-ahead/dwcmshc/peripheral-resets",
                       test_storage_reset_peripherals);
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
        qtest_add_func("/beaglev-ahead/dwcmshc/sd-cmd19-tuning",
                       test_dwcmshc_sd_tuning);
        qtest_add_func("/beaglev-ahead/dwcmshc/emmc-hs400-profile",
                       test_dwcmshc_emmc_hs400_profile);
        qtest_add_func("/beaglev-ahead/dwcmshc/emmc-v18",
                       test_dwcmshc_emmc_v18);
        qtest_add_func("/beaglev-ahead/dwcmshc/emmc-tuning-migration",
                       test_dwcmshc_emmc_tuning_migration);
        qtest_add_func("/beaglev-ahead/dwcmshc/v4-64bit-adma",
                       test_dwcmshc_v4_adma);
        qtest_add_func("/beaglev-ahead/dwcmshc/adma-migration",
                       test_dwcmshc_adma_migration);
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
