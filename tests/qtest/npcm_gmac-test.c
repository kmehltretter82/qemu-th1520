/*
 * QTests for Nuvoton NPCM7xx/8xx GMAC Modules.
 *
 * Copyright 2024 Google LLC
 * Authors:
 * Hao Wu <wuhaotsh@google.com>
 * Nabih Estefan <nabihestefan@google.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"

#include "libqos/libqos.h"
#include "hw/net/mii.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/iov.h"

/* Name of the GMAC Device */
#define TYPE_NPCM_GMAC "npcm-gmac"

/* Address of the PCS Module */
#define PCS_BASE_ADDRESS 0xf0780000
#define NPCM_PCS_IND_AC_BA 0x1fe

typedef struct GMACModule {
    int irq;
    uint64_t base_addr;
} GMACModule;

typedef struct TestData {
    const GMACModule *module;
} TestData;

/* Values extracted from hw/arm/npcm8xx.c */
static const GMACModule gmac_module_list[] = {
    {
        .irq        = 14,
        .base_addr  = 0xf0802000
    },
    {
        .irq        = 15,
        .base_addr  = 0xf0804000
    },
    {
        .irq        = 16,
        .base_addr  = 0xf0806000
    },
    {
        .irq        = 17,
        .base_addr  = 0xf0808000
    }
};

/* Returns the index of the GMAC module. */
static int gmac_module_index(const GMACModule *mod)
{
    ptrdiff_t diff = mod - gmac_module_list;

    g_assert_true(diff >= 0 && diff < ARRAY_SIZE(gmac_module_list));

    return diff;
}

/* 32-bit register offsets. Taken from dw_gmac.c. */
typedef enum NPCMRegister {
    /* DMA Registers */
    NPCM_DMA_BUS_MODE = 0x1000,
    NPCM_DMA_XMT_POLL_DEMAND = 0x1004,
    NPCM_DMA_RCV_POLL_DEMAND = 0x1008,
    NPCM_DMA_RCV_BASE_ADDR = 0x100c,
    NPCM_DMA_TX_BASE_ADDR = 0x1010,
    NPCM_DMA_STATUS = 0x1014,
    NPCM_DMA_CONTROL = 0x1018,
    NPCM_DMA_INTR_ENA = 0x101c,
    NPCM_DMA_MISSED_FRAME_CTR = 0x1020,
    NPCM_DMA_HOST_TX_DESC = 0x1048,
    NPCM_DMA_HOST_RX_DESC = 0x104c,
    NPCM_DMA_CUR_TX_BUF_ADDR = 0x1050,
    NPCM_DMA_CUR_RX_BUF_ADDR = 0x1054,
    NPCM_DMA_HW_FEATURE = 0x1058,

    /* GMAC Registers */
    NPCM_GMAC_MAC_CONFIG = 0x0,
    NPCM_GMAC_FRAME_FILTER = 0x4,
    NPCM_GMAC_HASH_HIGH = 0x8,
    NPCM_GMAC_HASH_LOW = 0xc,
    NPCM_GMAC_MII_ADDR = 0x10,
    NPCM_GMAC_MII_DATA = 0x14,
    NPCM_GMAC_FLOW_CTRL = 0x18,
    NPCM_GMAC_VLAN_FLAG = 0x1c,
    NPCM_GMAC_VERSION = 0x20,
    NPCM_GMAC_WAKEUP_FILTER = 0x28,
    NPCM_GMAC_PMT = 0x2c,
    NPCM_GMAC_LPI_CTRL = 0x30,
    NPCM_GMAC_TIMER_CTRL = 0x34,
    NPCM_GMAC_INT_STATUS = 0x38,
    NPCM_GMAC_INT_MASK = 0x3c,
    NPCM_GMAC_MAC0_ADDR_HI = 0x40,
    NPCM_GMAC_MAC0_ADDR_LO = 0x44,
    NPCM_GMAC_MAC1_ADDR_HI = 0x48,
    NPCM_GMAC_MAC1_ADDR_LO = 0x4c,
    NPCM_GMAC_MAC2_ADDR_HI = 0x50,
    NPCM_GMAC_MAC2_ADDR_LO = 0x54,
    NPCM_GMAC_MAC3_ADDR_HI = 0x58,
    NPCM_GMAC_MAC3_ADDR_LO = 0x5c,
    NPCM_GMAC_RGMII_STATUS = 0xd8,
    NPCM_GMAC_WATCHDOG = 0xdc,
    NPCM_GMAC_PTP_TCR = 0x700,
    NPCM_GMAC_PTP_SSIR = 0x704,
    NPCM_GMAC_PTP_STSR = 0x708,
    NPCM_GMAC_PTP_STNSR = 0x70c,
    NPCM_GMAC_PTP_STSUR = 0x710,
    NPCM_GMAC_PTP_STNSUR = 0x714,
    NPCM_GMAC_PTP_TAR = 0x718,
    NPCM_GMAC_PTP_TTSR = 0x71c,

    /* PCS Registers */
    NPCM_PCS_SR_CTL_ID1 = 0x3c0008,
    NPCM_PCS_SR_CTL_ID2 = 0x3c000a,
    NPCM_PCS_SR_CTL_STS = 0x3c0010,

    NPCM_PCS_SR_MII_CTRL = 0x3e0000,
    NPCM_PCS_SR_MII_STS = 0x3e0002,
    NPCM_PCS_SR_MII_DEV_ID1 = 0x3e0004,
    NPCM_PCS_SR_MII_DEV_ID2 = 0x3e0006,
    NPCM_PCS_SR_MII_AN_ADV = 0x3e0008,
    NPCM_PCS_SR_MII_LP_BABL = 0x3e000a,
    NPCM_PCS_SR_MII_AN_EXPN = 0x3e000c,
    NPCM_PCS_SR_MII_EXT_STS = 0x3e001e,

    NPCM_PCS_SR_TIM_SYNC_ABL = 0x3e0e10,
    NPCM_PCS_SR_TIM_SYNC_TX_MAX_DLY_LWR = 0x3e0e12,
    NPCM_PCS_SR_TIM_SYNC_TX_MAX_DLY_UPR = 0x3e0e14,
    NPCM_PCS_SR_TIM_SYNC_TX_MIN_DLY_LWR = 0x3e0e16,
    NPCM_PCS_SR_TIM_SYNC_TX_MIN_DLY_UPR = 0x3e0e18,
    NPCM_PCS_SR_TIM_SYNC_RX_MAX_DLY_LWR = 0x3e0e1a,
    NPCM_PCS_SR_TIM_SYNC_RX_MAX_DLY_UPR = 0x3e0e1c,
    NPCM_PCS_SR_TIM_SYNC_RX_MIN_DLY_LWR = 0x3e0e1e,
    NPCM_PCS_SR_TIM_SYNC_RX_MIN_DLY_UPR = 0x3e0e20,

    NPCM_PCS_VR_MII_MMD_DIG_CTRL1 = 0x3f0000,
    NPCM_PCS_VR_MII_AN_CTRL = 0x3f0002,
    NPCM_PCS_VR_MII_AN_INTR_STS = 0x3f0004,
    NPCM_PCS_VR_MII_TC = 0x3f0006,
    NPCM_PCS_VR_MII_DBG_CTRL = 0x3f000a,
    NPCM_PCS_VR_MII_EEE_MCTRL0 = 0x3f000c,
    NPCM_PCS_VR_MII_EEE_TXTIMER = 0x3f0010,
    NPCM_PCS_VR_MII_EEE_RXTIMER = 0x3f0012,
    NPCM_PCS_VR_MII_LINK_TIMER_CTRL = 0x3f0014,
    NPCM_PCS_VR_MII_EEE_MCTRL1 = 0x3f0016,
    NPCM_PCS_VR_MII_DIG_STS = 0x3f0020,
    NPCM_PCS_VR_MII_ICG_ERRCNT1 = 0x3f0022,
    NPCM_PCS_VR_MII_MISC_STS = 0x3f0030,
    NPCM_PCS_VR_MII_RX_LSTS = 0x3f0040,
    NPCM_PCS_VR_MII_MP_TX_BSTCTRL0 = 0x3f0070,
    NPCM_PCS_VR_MII_MP_TX_LVLCTRL0 = 0x3f0074,
    NPCM_PCS_VR_MII_MP_TX_GENCTRL0 = 0x3f007a,
    NPCM_PCS_VR_MII_MP_TX_GENCTRL1 = 0x3f007c,
    NPCM_PCS_VR_MII_MP_TX_STS = 0x3f0090,
    NPCM_PCS_VR_MII_MP_RX_GENCTRL0 = 0x3f00b0,
    NPCM_PCS_VR_MII_MP_RX_GENCTRL1 = 0x3f00b2,
    NPCM_PCS_VR_MII_MP_RX_LOS_CTRL0 = 0x3f00ba,
    NPCM_PCS_VR_MII_MP_MPLL_CTRL0 = 0x3f00f0,
    NPCM_PCS_VR_MII_MP_MPLL_CTRL1 = 0x3f00f2,
    NPCM_PCS_VR_MII_MP_MPLL_STS = 0x3f0110,
    NPCM_PCS_VR_MII_MP_MISC_CTRL2 = 0x3f0126,
    NPCM_PCS_VR_MII_MP_LVL_CTRL = 0x3f0130,
    NPCM_PCS_VR_MII_MP_MISC_CTRL0 = 0x3f0132,
    NPCM_PCS_VR_MII_MP_MISC_CTRL1 = 0x3f0134,
    NPCM_PCS_VR_MII_DIG_CTRL2 = 0x3f01c2,
    NPCM_PCS_VR_MII_DIG_ERRCNT_SEL = 0x3f01c4,
} NPCMRegister;

static uint32_t gmac_read(QTestState *qts, const GMACModule *mod,
                          NPCMRegister regno)
{
    return qtest_readl(qts, mod->base_addr + regno);
}

static void gmac_write(QTestState *qts, const GMACModule *mod,
                       NPCMRegister regno, uint32_t value)
{
    qtest_writel(qts, mod->base_addr + regno, value);
}

static uint16_t pcs_read(QTestState *qts, const GMACModule *mod,
                          NPCMRegister regno)
{
    uint32_t write_value = (regno & 0x3ffe00) >> 9;
    qtest_writel(qts, PCS_BASE_ADDRESS + NPCM_PCS_IND_AC_BA, write_value);
    uint32_t read_offset = regno & 0x1ff;
    return qtest_readl(qts, PCS_BASE_ADDRESS + read_offset);
}

/* Check that GMAC registers are reset to default value */
static void test_init(gconstpointer test_data)
{
    const TestData *td = test_data;
    const GMACModule *mod = td->module;
    QTestState *qts = qtest_init("-machine npcm845-evb");

#define CHECK_REG32(regno, value) \
    do { \
        g_assert_cmphex(gmac_read(qts, mod, (regno)), ==, (value)); \
    } while (0)

#define CHECK_REG_PCS(regno, value) \
    do { \
        g_assert_cmphex(pcs_read(qts, mod, (regno)), ==, (value)); \
    } while (0)

    CHECK_REG32(NPCM_DMA_BUS_MODE, 0x00020100);
    CHECK_REG32(NPCM_DMA_XMT_POLL_DEMAND, 0);
    CHECK_REG32(NPCM_DMA_RCV_POLL_DEMAND, 0);
    CHECK_REG32(NPCM_DMA_RCV_BASE_ADDR, 0);
    CHECK_REG32(NPCM_DMA_TX_BASE_ADDR, 0);
    CHECK_REG32(NPCM_DMA_STATUS, 0);
    CHECK_REG32(NPCM_DMA_CONTROL, 0);
    CHECK_REG32(NPCM_DMA_INTR_ENA, 0);
    CHECK_REG32(NPCM_DMA_MISSED_FRAME_CTR, 0);
    CHECK_REG32(NPCM_DMA_HOST_TX_DESC, 0);
    CHECK_REG32(NPCM_DMA_HOST_RX_DESC, 0);
    CHECK_REG32(NPCM_DMA_CUR_TX_BUF_ADDR, 0);
    CHECK_REG32(NPCM_DMA_CUR_RX_BUF_ADDR, 0);
    CHECK_REG32(NPCM_DMA_HW_FEATURE, 0x100d4f37);

    CHECK_REG32(NPCM_GMAC_MAC_CONFIG, 0);
    CHECK_REG32(NPCM_GMAC_FRAME_FILTER, 0);
    CHECK_REG32(NPCM_GMAC_HASH_HIGH, 0);
    CHECK_REG32(NPCM_GMAC_HASH_LOW, 0);
    CHECK_REG32(NPCM_GMAC_MII_ADDR, 0);
    CHECK_REG32(NPCM_GMAC_MII_DATA, 0);
    CHECK_REG32(NPCM_GMAC_FLOW_CTRL, 0);
    CHECK_REG32(NPCM_GMAC_VLAN_FLAG, 0);
    CHECK_REG32(NPCM_GMAC_VERSION, 0x00001032);
    CHECK_REG32(NPCM_GMAC_WAKEUP_FILTER, 0);
    CHECK_REG32(NPCM_GMAC_PMT, 0);
    CHECK_REG32(NPCM_GMAC_LPI_CTRL, 0);
    CHECK_REG32(NPCM_GMAC_TIMER_CTRL, 0x03e80000);
    CHECK_REG32(NPCM_GMAC_INT_STATUS, 0);
    CHECK_REG32(NPCM_GMAC_INT_MASK, 0);
    CHECK_REG32(NPCM_GMAC_MAC0_ADDR_HI, 0x8000ffff);
    CHECK_REG32(NPCM_GMAC_MAC0_ADDR_LO, 0xffffffff);
    CHECK_REG32(NPCM_GMAC_MAC1_ADDR_HI, 0x0000ffff);
    CHECK_REG32(NPCM_GMAC_MAC1_ADDR_LO, 0xffffffff);
    CHECK_REG32(NPCM_GMAC_MAC2_ADDR_HI, 0x0000ffff);
    CHECK_REG32(NPCM_GMAC_MAC2_ADDR_LO, 0xffffffff);
    CHECK_REG32(NPCM_GMAC_MAC3_ADDR_HI, 0x0000ffff);
    CHECK_REG32(NPCM_GMAC_MAC3_ADDR_LO, 0xffffffff);
    CHECK_REG32(NPCM_GMAC_RGMII_STATUS, 0);
    CHECK_REG32(NPCM_GMAC_WATCHDOG, 0);
    CHECK_REG32(NPCM_GMAC_PTP_TCR, 0x00002000);
    CHECK_REG32(NPCM_GMAC_PTP_SSIR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_STSR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_STNSR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_STSUR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_STNSUR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_TAR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_TTSR, 0);

    /* The compatibility type retains its PHY at address 0. */
    gmac_write(qts, mod, NPCM_GMAC_MII_ADDR,
               BIT(0) | (MII_PHYID1 << 6));
    CHECK_REG32(NPCM_GMAC_MII_DATA, 0x0362);
    gmac_write(qts, mod, NPCM_GMAC_MII_ADDR,
               BIT(0) | (1 << 11) | (MII_PHYID1 << 6));
    CHECK_REG32(NPCM_GMAC_MII_DATA, 0xffff);

    if (mod->base_addr == 0xf0802000) {
        CHECK_REG_PCS(NPCM_PCS_SR_CTL_ID1, 0x699e);
        CHECK_REG_PCS(NPCM_PCS_SR_CTL_ID2, 0);
        CHECK_REG_PCS(NPCM_PCS_SR_CTL_STS, 0x8000);

        CHECK_REG_PCS(NPCM_PCS_SR_MII_CTRL, 0x1140);
        CHECK_REG_PCS(NPCM_PCS_SR_MII_STS, 0x0109);
        CHECK_REG_PCS(NPCM_PCS_SR_MII_DEV_ID1, 0x699e);
        CHECK_REG_PCS(NPCM_PCS_SR_MII_DEV_ID2, 0x0ced0);
        CHECK_REG_PCS(NPCM_PCS_SR_MII_AN_ADV, 0x0020);
        CHECK_REG_PCS(NPCM_PCS_SR_MII_LP_BABL, 0);
        CHECK_REG_PCS(NPCM_PCS_SR_MII_AN_EXPN, 0);
        CHECK_REG_PCS(NPCM_PCS_SR_MII_EXT_STS, 0xc000);

        CHECK_REG_PCS(NPCM_PCS_SR_TIM_SYNC_ABL, 0x0003);
        CHECK_REG_PCS(NPCM_PCS_SR_TIM_SYNC_TX_MAX_DLY_LWR, 0x0038);
        CHECK_REG_PCS(NPCM_PCS_SR_TIM_SYNC_TX_MAX_DLY_UPR, 0);
        CHECK_REG_PCS(NPCM_PCS_SR_TIM_SYNC_TX_MIN_DLY_LWR, 0x0038);
        CHECK_REG_PCS(NPCM_PCS_SR_TIM_SYNC_TX_MIN_DLY_UPR, 0);
        CHECK_REG_PCS(NPCM_PCS_SR_TIM_SYNC_RX_MAX_DLY_LWR, 0x0058);
        CHECK_REG_PCS(NPCM_PCS_SR_TIM_SYNC_RX_MAX_DLY_UPR, 0);
        CHECK_REG_PCS(NPCM_PCS_SR_TIM_SYNC_RX_MIN_DLY_LWR, 0x0048);
        CHECK_REG_PCS(NPCM_PCS_SR_TIM_SYNC_RX_MIN_DLY_UPR, 0);

        CHECK_REG_PCS(NPCM_PCS_VR_MII_MMD_DIG_CTRL1, 0x2400);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_AN_CTRL, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_AN_INTR_STS, 0x000a);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_TC, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_DBG_CTRL, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_EEE_MCTRL0, 0x899c);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_EEE_TXTIMER, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_EEE_RXTIMER, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_LINK_TIMER_CTRL, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_EEE_MCTRL1, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_DIG_STS, 0x0010);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_ICG_ERRCNT1, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MISC_STS, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_RX_LSTS, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_TX_BSTCTRL0, 0x00a);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_TX_LVLCTRL0, 0x007f);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_TX_GENCTRL0, 0x0001);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_TX_GENCTRL1, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_TX_STS, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_RX_GENCTRL0, 0x0100);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_RX_GENCTRL1, 0x1100);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_RX_LOS_CTRL0, 0x000e);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_MPLL_CTRL0, 0x0100);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_MPLL_CTRL1, 0x0032);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_MPLL_STS, 0x0001);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_MISC_CTRL2, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_LVL_CTRL, 0x0019);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_MISC_CTRL0, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_MP_MISC_CTRL1, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_DIG_CTRL2, 0);
        CHECK_REG_PCS(NPCM_PCS_VR_MII_DIG_ERRCNT_SEL, 0);
    }

    qtest_quit(qts);
}

#ifndef _WIN32

#define GMAC_TEST_DESC_ADDR 0x00100000
#define GMAC_TEST_DATA_ADDR 0x00110000
#define GMAC_TEST_TIMEOUT_S 5

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

static bool gmac_wait_socket_readable(int fd)
{
    fd_set read_fds;
    struct timeval tv = { .tv_sec = GMAC_TEST_TIMEOUT_S };

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    return select(fd + 1, &read_fds, NULL, NULL, &tv) == 1;
}

static bool gmac_wait_status(QTestState *qts, const GMACModule *mod,
                             uint32_t mask)
{
    gint64 deadline = g_get_monotonic_time() +
                      GMAC_TEST_TIMEOUT_S * G_TIME_SPAN_SECOND;

    do {
        if (gmac_read(qts, mod, NPCM_DMA_STATUS) & mask) {
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
    qts = qtest_initf("-machine npcm845-evb "
                      "-nic socket,fd=%d,model=npcm-gmac",
                      sockets[1]);
    close(sockets[1]);
    return qts;
}

static void test_normal_descriptors(gconstpointer test_data)
{
    const TestData *td = test_data;
    const GMACModule *mod = td->module;
    static const uint8_t packet[64] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0x65, 0x43, 0x21,
        0x08, 0x00, 0x45, 0x00,
    };
    GMACDesc desc;
    QTestState *qts;
    int sockets[2];
    uint32_t wire_len;
    uint8_t received[sizeof(packet)];

    qts = gmac_packet_test_init(sockets);

    qtest_memwrite(qts, GMAC_TEST_DATA_ADDR, packet, sizeof(packet));
    desc = (GMACDesc) {
        .des0 = BIT(31),
        .des1 = BIT(31) | BIT(30) | BIT(29) | sizeof(packet),
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    desc = (GMACDesc) { 0 };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + sizeof(desc), &desc);

    gmac_write(qts, mod, NPCM_DMA_TX_BASE_ADDR, GMAC_TEST_DESC_ADDR);
    gmac_write(qts, mod, NPCM_DMA_INTR_ENA, BIT(16) | BIT(0));
    gmac_write(qts, mod, NPCM_GMAC_MAC_CONFIG, BIT(3));
    gmac_write(qts, mod, NPCM_DMA_CONTROL, BIT(13));

    g_assert_true(gmac_wait_socket_readable(sockets[0]));
    g_assert_cmpint(recv(sockets[0], &wire_len, sizeof(wire_len), MSG_WAITALL),
                    ==, sizeof(wire_len));
    g_assert_cmpuint(ntohl(wire_len), ==, sizeof(packet));
    g_assert_cmpint(recv(sockets[0], received, sizeof(received), MSG_WAITALL),
                    ==, sizeof(received));
    g_assert_cmpmem(received, sizeof(received), packet, sizeof(packet));
    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0 & BIT(31), ==, 0);
    g_assert_true(gmac_wait_status(qts, mod, BIT(0)));

    qtest_quit(qts);
    close(sockets[0]);

    qts = gmac_packet_test_init(sockets);
    desc = (GMACDesc) {
        .des0 = BIT(31),
        .des1 = 2047,
        .des2 = GMAC_TEST_DATA_ADDR,
    };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    desc = (GMACDesc) { 0 };
    gmac_write_desc(qts, GMAC_TEST_DESC_ADDR + sizeof(desc), &desc);

    gmac_write(qts, mod, NPCM_DMA_RCV_BASE_ADDR, GMAC_TEST_DESC_ADDR);
    gmac_write(qts, mod, NPCM_DMA_INTR_ENA, BIT(16) | BIT(6));
    gmac_write(qts, mod, NPCM_GMAC_MAC_CONFIG, BIT(2));
    gmac_write(qts, mod, NPCM_DMA_CONTROL, BIT(1));

    wire_len = htonl(sizeof(packet));
    const struct iovec iov[] = {
        { .iov_base = &wire_len, .iov_len = sizeof(wire_len) },
        { .iov_base = (void *)packet, .iov_len = sizeof(packet) },
    };
    g_assert_cmpint(iov_send(sockets[0], iov, ARRAY_SIZE(iov), 0,
                             sizeof(wire_len) + sizeof(packet)),
                    ==, sizeof(wire_len) + sizeof(packet));
    g_assert_true(gmac_wait_status(qts, mod, BIT(6)));

    gmac_read_desc(qts, GMAC_TEST_DESC_ADDR, &desc);
    g_assert_cmphex(desc.des0 & BIT(31), ==, 0);
    g_assert_cmphex(desc.des0 & (BIT(9) | BIT(8)), ==, BIT(9) | BIT(8));
    g_assert_cmpuint(extract32(desc.des0, 16, 14), ==,
                     sizeof(packet) + sizeof(uint32_t));
    g_assert_cmphex(gmac_read(qts, mod, NPCM_DMA_HOST_RX_DESC), ==,
                    GMAC_TEST_DESC_ADDR + sizeof(desc));

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

static void gmac_add_test(const char *name, const TestData* td,
                          GTestDataFunc fn)
{
    g_autofree char *full_name = g_strdup_printf(
            "npcm8xx_gmac/gmac[%d]/%s", gmac_module_index(td->module), name);
    qtest_add_data_func(full_name, td, fn);
}

int main(int argc, char **argv)
{
    TestData test_data_list[ARRAY_SIZE(gmac_module_list)];

    g_test_init(&argc, &argv, NULL);

    for (int i = 0; i < ARRAY_SIZE(gmac_module_list); ++i) {
        TestData *td = &test_data_list[i];

        td->module = &gmac_module_list[i];

        gmac_add_test("init", td, test_init);
    }

#ifndef _WIN32
    gmac_add_test("normal-descriptors", &test_data_list[0],
                  test_normal_descriptors);
#endif

    return g_test_run();
}
