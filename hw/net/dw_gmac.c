/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Synopsys DesignWare GMAC 3.x
 *
 * Copyright 2024 Google LLC
 * Authors:
 * Hao Wu <wuhaotsh@google.com>
 * Nabih Estefan <nabihestefan@google.com>
 *
 * Unsupported/unimplemented features:
 * - Only clause 22 MDIO transactions and a small generic PHY are implemented.
 * - Precision timestamp (PTP) is not implemented.
 */

#include "qemu/osdep.h"

#include <zlib.h>

#include "hw/core/registerfields.h"
#include "hw/net/mii.h"
#include "hw/net/dw_gmac.h"
#include "migration/vmstate.h"
#include "net/checksum.h"
#include "net/eth.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "system/dma.h"
#include "trace.h"

REG32(DWMAC_DMA_BUS_MODE, 0x1000)
REG32(DWMAC_DMA_XMT_POLL_DEMAND, 0x1004)
REG32(DWMAC_DMA_RCV_POLL_DEMAND, 0x1008)
REG32(DWMAC_DMA_RX_BASE_ADDR, 0x100c)
REG32(DWMAC_DMA_TX_BASE_ADDR, 0x1010)
REG32(DWMAC_DMA_STATUS, 0x1014)
REG32(DWMAC_DMA_CONTROL, 0x1018)
REG32(DWMAC_DMA_INTR_ENA, 0x101c)
REG32(DWMAC_DMA_MISSED_FRAME_CTR, 0x1020)
REG32(DWMAC_DMA_HOST_TX_DESC, 0x1048)
REG32(DWMAC_DMA_HOST_RX_DESC, 0x104c)
REG32(DWMAC_DMA_CUR_TX_BUF_ADDR, 0x1050)
REG32(DWMAC_DMA_CUR_RX_BUF_ADDR, 0x1054)
REG32(DWMAC_DMA_HW_FEATURE, 0x1058)

REG32(DW_GMAC_MAC_CONFIG, 0x0)
REG32(DW_GMAC_FRAME_FILTER, 0x4)
REG32(DW_GMAC_HASH_HIGH, 0x8)
REG32(DW_GMAC_HASH_LOW, 0xc)
REG32(DW_GMAC_MII_ADDR, 0x10)
REG32(DW_GMAC_MII_DATA, 0x14)
REG32(DW_GMAC_FLOW_CTRL, 0x18)
REG32(DW_GMAC_VLAN_FLAG, 0x1c)
REG32(DW_GMAC_VERSION, 0x20)
REG32(DW_GMAC_WAKEUP_FILTER, 0x28)
REG32(DW_GMAC_PMT, 0x2c)
REG32(DW_GMAC_LPI_CTRL, 0x30)
REG32(DW_GMAC_TIMER_CTRL, 0x34)
REG32(DW_GMAC_INT_STATUS, 0x38)
REG32(DW_GMAC_INT_MASK, 0x3c)
REG32(DW_GMAC_MAC0_ADDR_HI, 0x40)
REG32(DW_GMAC_MAC0_ADDR_LO, 0x44)
REG32(DW_GMAC_MAC1_ADDR_HI, 0x48)
REG32(DW_GMAC_MAC1_ADDR_LO, 0x4c)
REG32(DW_GMAC_MAC2_ADDR_HI, 0x50)
REG32(DW_GMAC_MAC2_ADDR_LO, 0x54)
REG32(DW_GMAC_MAC3_ADDR_HI, 0x58)
REG32(DW_GMAC_MAC3_ADDR_LO, 0x5c)
REG32(DW_GMAC_VLAN_HASH_TABLE, 0x588)
REG32(DW_GMAC_RGMII_STATUS, 0xd8)
REG32(DW_GMAC_WATCHDOG, 0xdc)
REG32(DW_GMAC_PTP_TCR, 0x700)
REG32(DW_GMAC_PTP_SSIR, 0x704)
REG32(DW_GMAC_PTP_STSR, 0x708)
REG32(DW_GMAC_PTP_STNSR, 0x70c)
REG32(DW_GMAC_PTP_STSUR, 0x710)
REG32(DW_GMAC_PTP_STNSUR, 0x714)
REG32(DW_GMAC_PTP_TAR, 0x718)
REG32(DW_GMAC_PTP_TTSR, 0x71c)

/* Register Fields */
#define DW_GMAC_MII_ADDR_BUSY             BIT(0)
#define DW_GMAC_MII_ADDR_WRITE            BIT(1)
#define DW_GMAC_MII_ADDR_GR(rv)           extract16((rv), 6, 5)
#define DW_GMAC_MII_ADDR_PA(rv)           extract16((rv), 11, 5)

#define DW_GMAC_INT_MASK_LPIIM            BIT(10)
#define DW_GMAC_INT_MASK_PMTM             BIT(3)
#define DW_GMAC_INT_MASK_RGIM             BIT(0)

#define DWMAC_DMA_BUS_MODE_SWR               BIT(0)
#define DWMAC_DMA_BUS_MODE_ATDS              BIT(7)
#define DWMAC_DMA_HW_FEATURE_ENH_DESC         BIT(24)

#define DW_GMAC_MAC_ADDR0_BASE                0x40
#define DW_GMAC_MAC_ADDR16_BASE               0x800
#define DW_GMAC_MAC_ADDR128_END               0xb80
#define DW_GMAC_MAC_ADDR_STRIDE               8
#define DW_GMAC_ARCH_MAC_ADDRS                 128
#define DW_GMAC_MAC_ADDR_HIGH_MASK             0xff00ffffu
#define DW_GMAC_MAC_ADDR_AE                    BIT(31)
#define DW_GMAC_MAC_ADDR_SA                    BIT(30)
#define DW_GMAC_MAC_ADDR_MBC(word)             extract32((word), 24, 6)
#define DW_GMAC_FRAME_FILTER_MASK               0x800107ffu
#define DW_GMAC_FRAME_FILTER_HASH_MASK \
    (DW_GMAC_FRAME_FILTER_HPF_MASK | DW_GMAC_FRAME_FILTER_HMC_MASK | \
     DW_GMAC_FRAME_FILTER_HUC_MASK)
#define DW_GMAC_VLAN_TAG_MASK                   0x0007ffffu

#define DW_GMAC_MAC_CONFIG_DM                  BIT(11)
#define DW_GMAC_FLOW_CTRL_UP                   BIT(3)
#define DW_GMAC_FLOW_CTRL_RFE                  BIT(2)

#define DW_GMAC_CONTROL_ETHERTYPE              0x8808
#define DW_GMAC_PAUSE_OPCODE                   0x0001

typedef struct DWGMACRxFilterResult {
    bool accept;
    bool da_fail;
    bool sa_fail;
    bool vlan_tag;
} DWGMACRxFilterResult;

static hwaddr gmac_mac_addr_reg(unsigned int index, bool high)
{
    hwaddr base;

    g_assert(index < DW_GMAC_ARCH_MAC_ADDRS);
    if (index < 16) {
        base = DW_GMAC_MAC_ADDR0_BASE + index * DW_GMAC_MAC_ADDR_STRIDE;
    } else {
        base = DW_GMAC_MAC_ADDR16_BASE +
               (index - 16) * DW_GMAC_MAC_ADDR_STRIDE;
    }
    return base + (high ? 0 : sizeof(uint32_t));
}

static bool gmac_decode_mac_addr_reg(hwaddr offset, unsigned int *index,
                                     bool *high)
{
    hwaddr relative;

    if (offset >= DW_GMAC_MAC_ADDR0_BASE &&
        offset < DW_GMAC_MAC_ADDR0_BASE + 16 * DW_GMAC_MAC_ADDR_STRIDE) {
        relative = offset - DW_GMAC_MAC_ADDR0_BASE;
        *index = relative / DW_GMAC_MAC_ADDR_STRIDE;
    } else if (offset >= DW_GMAC_MAC_ADDR16_BASE &&
               offset < DW_GMAC_MAC_ADDR128_END) {
        relative = offset - DW_GMAC_MAC_ADDR16_BASE;
        *index = 16 + relative / DW_GMAC_MAC_ADDR_STRIDE;
    } else {
        return false;
    }

    *high = !(relative & sizeof(uint32_t));
    return true;
}

static void gmac_sync_conf_mac(DWGMACState *gmac)
{
    uint32_t high = gmac->regs[
        gmac_mac_addr_reg(0, true) / sizeof(uint32_t)];
    uint32_t low = gmac->regs[
        gmac_mac_addr_reg(0, false) / sizeof(uint32_t)];

    gmac->conf.macaddr.a[0] = low;
    gmac->conf.macaddr.a[1] = low >> 8;
    gmac->conf.macaddr.a[2] = low >> 16;
    gmac->conf.macaddr.a[3] = low >> 24;
    gmac->conf.macaddr.a[4] = high;
    gmac->conf.macaddr.a[5] = high >> 8;
}

static void gmac_sanitize_filter_regs(DWGMACState *gmac)
{
    uint32_t frame_filter_mask = DW_GMAC_FRAME_FILTER_MASK;

    if (!gmac->hash_bins) {
        frame_filter_mask &= ~DW_GMAC_FRAME_FILTER_HASH_MASK;
        gmac->regs[R_DW_GMAC_HASH_HIGH] = 0;
        gmac->regs[R_DW_GMAC_HASH_LOW] = 0;
    }
    gmac->regs[R_DW_GMAC_FRAME_FILTER] &= frame_filter_mask;
    gmac->regs[R_DW_GMAC_VLAN_FLAG] &= DW_GMAC_VLAN_TAG_MASK;
    gmac->regs[R_DW_GMAC_VLAN_HASH_TABLE] = 0;

    gmac->regs[gmac_mac_addr_reg(0, true) / 4] =
        DW_GMAC_MAC_ADDR_AE |
        (gmac->regs[gmac_mac_addr_reg(0, true) / 4] & 0xffff);
    for (unsigned int index = 1; index < gmac->num_mac_addrs; index++) {
        gmac->regs[gmac_mac_addr_reg(index, true) / 4] &=
            DW_GMAC_MAC_ADDR_HIGH_MASK;
    }
    for (unsigned int index = gmac->num_mac_addrs;
         index < DW_GMAC_ARCH_MAC_ADDRS; index++) {
        gmac->regs[gmac_mac_addr_reg(index, true) / 4] = 0;
        gmac->regs[gmac_mac_addr_reg(index, false) / 4] = 0;
    }
}

static bool gmac_mac_addr_matches(DWGMACState *gmac, unsigned int index,
                                  const uint8_t *addr)
{
    uint32_t high = gmac->regs[gmac_mac_addr_reg(index, true) / 4];
    uint32_t low = gmac->regs[gmac_mac_addr_reg(index, false) / 4];
    uint8_t masked = index ? DW_GMAC_MAC_ADDR_MBC(high) : 0;

    for (unsigned int byte = 0; byte < ETH_ALEN; byte++) {
        uint8_t expected;

        if (masked & BIT(byte)) {
            continue;
        }
        expected = byte < 4 ? extract32(low, byte * 8, 8) :
                              extract32(high, (byte - 4) * 8, 8);
        if (addr[byte] != expected) {
            return false;
        }
    }
    return true;
}

static bool gmac_perfect_addr_match(DWGMACState *gmac, const uint8_t *addr,
                                    bool source, bool *filter_present)
{
    *filter_present = !source;

    for (unsigned int index = 0; index < gmac->num_mac_addrs; index++) {
        uint32_t high = gmac->regs[gmac_mac_addr_reg(index, true) / 4];

        if (index) {
            if (!(high & DW_GMAC_MAC_ADDR_AE) ||
                !!(high & DW_GMAC_MAC_ADDR_SA) != source) {
                continue;
            }
            *filter_present = true;
        } else if (source) {
            continue;
        }

        if (gmac_mac_addr_matches(gmac, index, addr)) {
            return true;
        }
    }
    return false;
}

static bool gmac_hash_addr_match(DWGMACState *gmac, const uint8_t *addr)
{
    unsigned int index = (~net_crc32(addr, ETH_ALEN)) >> 26;
    uint32_t table = gmac->regs[index < 32 ? R_DW_GMAC_HASH_LOW :
                                             R_DW_GMAC_HASH_HIGH];

    return table & BIT(index & 31);
}

static bool gmac_vlan_filter(DWGMACState *gmac, const uint8_t *buf,
                             size_t len, bool *tagged)
{
    uint32_t reg = gmac->regs[R_DW_GMAC_VLAN_FLAG];
    uint16_t protocol;
    uint16_t frame_tag;
    uint16_t wanted;
    bool match;

    *tagged = false;
    if (len < ETH_HLEN + sizeof(uint16_t)) {
        return true;
    }

    protocol = lduw_be_p(buf + 2 * ETH_ALEN);
    if (protocol != ETH_P_VLAN &&
        (protocol != ETH_P_DVLAN || !(reg & DW_GMAC_VLAN_TAG_ESVL_MASK))) {
        return true;
    }

    frame_tag = lduw_be_p(buf + ETH_HLEN);
    wanted = DW_GMAC_VLAN_TAG_VL_MASK(reg);
    if (reg & DW_GMAC_VLAN_TAG_ETV_MASK) {
        frame_tag &= VLAN_VID_MASK;
        wanted &= VLAN_VID_MASK;
    }

    if (!wanted) {
        match = true;
    } else {
        match = frame_tag == wanted;
    }

    if (reg & DW_GMAC_VLAN_TAG_VTIM_MASK) {
        match = !match;
    }
    *tagged = match;
    return !(gmac->regs[R_DW_GMAC_FRAME_FILTER] &
             DW_GMAC_FRAME_FILTER_VTFE_MASK) || match;
}

static bool gmac_pause_is_processed(DWGMACState *gmac, const uint8_t *buf,
                                    size_t len)
{
    static const uint8_t pause_multicast[ETH_ALEN] = {
        0x01, 0x80, 0xc2, 0x00, 0x00, 0x01,
    };
    bool own_address;

    if (len < ETH_HLEN + sizeof(uint16_t) ||
        lduw_be_p(buf + ETH_HLEN) != DW_GMAC_PAUSE_OPCODE ||
        !(gmac->regs[R_DW_GMAC_MAC_CONFIG] & DW_GMAC_MAC_CONFIG_DM) ||
        !(gmac->regs[R_DW_GMAC_FLOW_CTRL] & DW_GMAC_FLOW_CTRL_RFE)) {
        return false;
    }

    own_address = gmac_mac_addr_matches(gmac, 0, buf);
    return !memcmp(buf, pause_multicast, ETH_ALEN) ||
           ((gmac->regs[R_DW_GMAC_FLOW_CTRL] & DW_GMAC_FLOW_CTRL_UP) &&
            own_address);
}

static DWGMACRxFilterResult gmac_filter_packet(DWGMACState *gmac,
                                                const uint8_t *buf,
                                                size_t len)
{
    uint32_t filter = gmac->regs[R_DW_GMAC_FRAME_FILTER] &
        DW_GMAC_FRAME_FILTER_MASK &
        (gmac->hash_bins ? UINT32_MAX : ~DW_GMAC_FRAME_FILTER_HASH_MASK);
    DWGMACRxFilterResult result = { 0 };
    bool da_pass = false;
    bool sa_pass = true;
    bool da_perfect = false;
    bool da_filter_present;
    bool sa_match = false;
    bool sa_filter_present = false;
    bool address_pass;
    bool control = len >= ETH_HLEN &&
                   lduw_be_p(buf + 2 * ETH_ALEN) ==
                   DW_GMAC_CONTROL_ETHERTYPE;

    if (filter & DW_GMAC_FRAME_FILTER_PR_MASK) {
        da_pass = true;
    } else if (len >= ETH_ALEN) {
        if (is_broadcast_ether_addr(buf)) {
            da_pass = !(filter & DW_GMAC_FRAME_FILTER_DBF_MASK);
        } else if (is_multicast_ether_addr(buf) &&
                   (filter & DW_GMAC_FRAME_FILTER_PM_MASK)) {
            da_pass = true;
        } else {
            bool hash_enabled = filter &
                (is_multicast_ether_addr(buf) ?
                 DW_GMAC_FRAME_FILTER_HMC_MASK :
                 DW_GMAC_FRAME_FILTER_HUC_MASK);

            da_perfect = gmac_perfect_addr_match(gmac, buf, false,
                                                  &da_filter_present);
            if (hash_enabled) {
                da_pass = gmac_hash_addr_match(gmac, buf);
                if (filter & DW_GMAC_FRAME_FILTER_HPF_MASK) {
                    da_pass |= da_perfect;
                }
            } else {
                da_pass = da_perfect;
            }
            if (filter & DW_GMAC_FRAME_FILTER_DAIF_MASK) {
                da_pass = !da_pass;
            }
        }
    }

    if (!(filter & DW_GMAC_FRAME_FILTER_PR_MASK) && len >= 2 * ETH_ALEN) {
        sa_match = gmac_perfect_addr_match(gmac, buf + ETH_ALEN, true,
                                           &sa_filter_present);
        if (sa_filter_present || (filter & DW_GMAC_FRAME_FILTER_SAF_MASK)) {
            sa_pass = (filter & DW_GMAC_FRAME_FILTER_SAIF_MASK) ?
                      !sa_match : sa_match;
        }
    } else if (!(filter & DW_GMAC_FRAME_FILTER_PR_MASK) &&
               (filter & DW_GMAC_FRAME_FILTER_SAF_MASK)) {
        sa_pass = !!(filter & DW_GMAC_FRAME_FILTER_SAIF_MASK);
    }

    result.da_fail = !da_pass;
    result.sa_fail = !sa_pass;
    address_pass = da_pass &&
                   (!(filter & DW_GMAC_FRAME_FILTER_SAF_MASK) || sa_pass);
    if (filter & DW_GMAC_FRAME_FILTER_REC_ALL_MASK) {
        address_pass = true;
    }

    if (control) {
        switch (DW_GMAC_FRAME_FILTER_PCF_MASK(filter)) {
        case 0:
            result.accept = false;
            break;
        case 1:
            result.accept = !gmac_pause_is_processed(gmac, buf, len);
            break;
        case 2:
            result.accept = true;
            break;
        case 3:
            result.accept = address_pass;
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        result.accept = address_pass;
    }

    if (!gmac_vlan_filter(gmac, buf, len, &result.vlan_tag)) {
        result.accept = false;
    }
    if (len >= ETH_ALEN && is_broadcast_ether_addr(buf) &&
        (filter & DW_GMAC_FRAME_FILTER_DBF_MASK)) {
        result.accept = false;
    }
    return result;
}

static const uint32_t dw_gmac_cold_reset_values[DW_GMAC_NR_REGS] = {
    /* Reduce version to 3.2 so that the kernel can enable interrupt. */
    [R_DW_GMAC_VERSION]         = 0x00001032,
    [R_DW_GMAC_TIMER_CTRL]      = 0x03e80000,
    [R_DW_GMAC_MAC0_ADDR_HI]    = 0x8000ffff,
    [R_DW_GMAC_MAC0_ADDR_LO]    = 0xffffffff,
    [R_DW_GMAC_MAC1_ADDR_HI]    = 0x0000ffff,
    [R_DW_GMAC_MAC1_ADDR_LO]    = 0xffffffff,
    [R_DW_GMAC_MAC2_ADDR_HI]    = 0x0000ffff,
    [R_DW_GMAC_MAC2_ADDR_LO]    = 0xffffffff,
    [R_DW_GMAC_MAC3_ADDR_HI]    = 0x0000ffff,
    [R_DW_GMAC_MAC3_ADDR_LO]    = 0xffffffff,
    [R_DW_GMAC_PTP_TCR]         = 0x00002000,
    [R_DWMAC_DMA_BUS_MODE]         = 0x00020101,
    [R_DWMAC_DMA_HW_FEATURE]       = 0x100d4f37,
};

static const uint16_t phy_reg_init[] = {
    [MII_BMCR]      = MII_BMCR_AUTOEN | MII_BMCR_FD | MII_BMCR_SPEED1000,
    [MII_BMSR]      = MII_BMSR_100TX_FD | MII_BMSR_100TX_HD | MII_BMSR_10T_FD |
                      MII_BMSR_10T_HD | MII_BMSR_EXTSTAT | MII_BMSR_AUTONEG |
                      MII_BMSR_LINK_ST | MII_BMSR_EXTCAP,
    [MII_PHYID1]    = 0x0362,
    [MII_PHYID2]    = 0x5e6a,
    [MII_ANAR]      = MII_ANAR_TXFD | MII_ANAR_TX | MII_ANAR_10FD |
                      MII_ANAR_10 | MII_ANAR_CSMACD,
    [MII_ANLPAR]    = MII_ANLPAR_ACK | MII_ANLPAR_PAUSE |
                      MII_ANLPAR_TXFD | MII_ANLPAR_TX | MII_ANLPAR_10FD |
                      MII_ANLPAR_10 | MII_ANLPAR_CSMACD,
    [MII_ANER]      = 0x64 | MII_ANER_NWAY,
    [MII_ANNP]      = 0x2001,
    [MII_CTRL1000]  = MII_CTRL1000_FULL,
    [MII_STAT1000]  = MII_STAT1000_FULL,
    [MII_EXTSTAT]   = 0x3000, /* 1000BASTE_T full-duplex capable */
};

static bool gmac_uses_enhanced_desc(const DWGMACState *gmac)
{
    return gmac->hw_feature & DWMAC_DMA_HW_FEATURE_ENH_DESC;
}

static size_t gmac_desc_stride(const DWGMACState *gmac)
{
    return gmac->regs[R_DWMAC_DMA_BUS_MODE] & DWMAC_DMA_BUS_MODE_ATDS ?
           2 * sizeof(struct DWGMACRxDesc) : sizeof(struct DWGMACRxDesc);
}

static uint32_t gmac_rx_buffer1_size(const DWGMACState *gmac,
                                     const struct DWGMACRxDesc *desc)
{
    return gmac_uses_enhanced_desc(gmac) ? extract32(desc->rdes1, 0, 13) :
           RX_DESC_RDES1_BFFR1_SZ_MASK(desc->rdes1);
}

static uint32_t gmac_rx_buffer2_size(const DWGMACState *gmac,
                                     const struct DWGMACRxDesc *desc)
{
    return gmac_uses_enhanced_desc(gmac) ? extract32(desc->rdes1, 16, 13) :
           RX_DESC_RDES1_BFFR2_SZ_MASK(desc->rdes1);
}

static bool gmac_rx_is_chained(const DWGMACState *gmac,
                               const struct DWGMACRxDesc *desc)
{
    return desc->rdes1 & (gmac_uses_enhanced_desc(gmac) ? BIT(14) : BIT(24));
}

static bool gmac_rx_is_end_ring(const DWGMACState *gmac,
                                const struct DWGMACRxDesc *desc)
{
    return desc->rdes1 & (gmac_uses_enhanced_desc(gmac) ? BIT(15) : BIT(25));
}

static bool gmac_rx_irq_disabled(const struct DWGMACRxDesc *desc)
{
    return desc->rdes1 & BIT(31);
}

static bool gmac_tx_is_chained(const DWGMACState *gmac,
                               const struct DWGMACTxDesc *desc)
{
    return gmac_uses_enhanced_desc(gmac) ? desc->tdes0 & BIT(20) :
           desc->tdes1 & BIT(24);
}

static bool gmac_tx_is_end_ring(const DWGMACState *gmac,
                                const struct DWGMACTxDesc *desc)
{
    return gmac_uses_enhanced_desc(gmac) ? desc->tdes0 & BIT(21) :
           desc->tdes1 & BIT(25);
}

static bool gmac_tx_is_first(const DWGMACState *gmac,
                             const struct DWGMACTxDesc *desc)
{
    return gmac_uses_enhanced_desc(gmac) ? desc->tdes0 & BIT(28) :
           desc->tdes1 & BIT(29);
}

static bool gmac_tx_is_last(const DWGMACState *gmac,
                            const struct DWGMACTxDesc *desc)
{
    return gmac_uses_enhanced_desc(gmac) ? desc->tdes0 & BIT(29) :
           desc->tdes1 & BIT(30);
}

static bool gmac_tx_irq_requested(const DWGMACState *gmac,
                                  const struct DWGMACTxDesc *desc)
{
    return gmac_uses_enhanced_desc(gmac) ? desc->tdes0 & BIT(30) :
           desc->tdes1 & BIT(31);
}

static uint32_t gmac_tx_buffer1_size(const DWGMACState *gmac,
                                     const struct DWGMACTxDesc *desc)
{
    return gmac_uses_enhanced_desc(gmac) ? extract32(desc->tdes1, 0, 13) :
           TX_DESC_TDES1_BFFR1_SZ_MASK(desc->tdes1);
}

static uint32_t gmac_tx_buffer2_size(const DWGMACState *gmac,
                                     const struct DWGMACTxDesc *desc)
{
    return gmac_uses_enhanced_desc(gmac) ? extract32(desc->tdes1, 16, 13) :
           TX_DESC_TDES1_BFFR2_SZ_MASK(desc->tdes1);
}

static unsigned gmac_tx_checksum_control(const DWGMACState *gmac,
                                         const struct DWGMACTxDesc *desc)
{
    return gmac_uses_enhanced_desc(gmac) ? extract32(desc->tdes0, 22, 2) :
           TX_DESC_TDES1_CHKSM_INS_CTRL_MASK(desc->tdes1);
}

static void dw_gmac_soft_reset(DWGMACState *gmac)
{
    memcpy(gmac->regs, dw_gmac_cold_reset_values,
           DW_GMAC_NR_REGS * sizeof(uint32_t));
    for (unsigned int index = 1; index < gmac->num_mac_addrs; index++) {
        gmac->regs[gmac_mac_addr_reg(index, true) / 4] = 0x0000ffff;
        gmac->regs[gmac_mac_addr_reg(index, false) / 4] = 0xffffffff;
    }
    gmac_sanitize_filter_regs(gmac);
    gmac->regs[R_DW_GMAC_VERSION] = gmac->version;
    gmac->regs[R_DWMAC_DMA_HW_FEATURE] = gmac->hw_feature;
    /* Clear reset bits */
    gmac->regs[R_DWMAC_DMA_BUS_MODE] &= ~DWMAC_DMA_BUS_MODE_SWR;
}

static void gmac_phy_set_link(DWGMACState *gmac, bool active)
{
    /* Autonegotiation status mirrors link status.  */
    if (active) {
        gmac->phy_regs[gmac->phy_addr][MII_BMSR] |=
            MII_BMSR_LINK_ST | MII_BMSR_AN_COMP;
    } else {
        gmac->phy_regs[gmac->phy_addr][MII_BMSR] &=
            ~(MII_BMSR_LINK_ST | MII_BMSR_AN_COMP);
    }
}

static bool gmac_can_receive(NetClientState *nc)
{
    DWGMACState *gmac = DW_GMAC(qemu_get_nic_opaque(nc));

    /* If GMAC receive is disabled. */
    if (!(gmac->regs[R_DW_GMAC_MAC_CONFIG] & DW_GMAC_MAC_CONFIG_RX_EN)) {
        return false;
    }

    /* If GMAC DMA RX is stopped. */
    if (!(gmac->regs[R_DWMAC_DMA_CONTROL] & DWMAC_DMA_CONTROL_START_STOP_RX)) {
        return false;
    }
    return true;
}

/*
 * Function that updates the GMAC IRQ
 * It find the logical OR of the enabled bits for NIS (if enabled)
 * It find the logical OR of the enabled bits for AIS (if enabled)
 */
static void gmac_update_irq(DWGMACState *gmac)
{
    /*
     * Check if the normal interrupts summary is enabled
     * if so, add the bits for the summary that are enabled
     */
    if (gmac->regs[R_DWMAC_DMA_INTR_ENA] & gmac->regs[R_DWMAC_DMA_STATUS] &
        (DWMAC_DMA_INTR_ENAB_NIE_BITS)) {
        gmac->regs[R_DWMAC_DMA_STATUS] |=  DWMAC_DMA_STATUS_NIS;
    }
    /*
     * Check if the abnormal interrupts summary is enabled
     * if so, add the bits for the summary that are enabled
     */
    if (gmac->regs[R_DWMAC_DMA_INTR_ENA] & gmac->regs[R_DWMAC_DMA_STATUS] &
        (DWMAC_DMA_INTR_ENAB_AIE_BITS)) {
        gmac->regs[R_DWMAC_DMA_STATUS] |=  DWMAC_DMA_STATUS_AIS;
    }

    /* Get the logical OR of both normal and abnormal interrupts */
    int level = !!((gmac->regs[R_DWMAC_DMA_STATUS] &
                    gmac->regs[R_DWMAC_DMA_INTR_ENA] &
                    DWMAC_DMA_STATUS_NIS) |
                   (gmac->regs[R_DWMAC_DMA_STATUS] &
                   gmac->regs[R_DWMAC_DMA_INTR_ENA] &
                   DWMAC_DMA_STATUS_AIS));

    /* Set the IRQ */
    trace_dw_gmac_update_irq(DEVICE(gmac)->canonical_path,
                               gmac->regs[R_DWMAC_DMA_STATUS],
                               gmac->regs[R_DWMAC_DMA_INTR_ENA],
                               level);
    qemu_set_irq(gmac->irq, level);
}

static int gmac_read_rx_desc(dma_addr_t addr, struct DWGMACRxDesc *desc)
{
    if (dma_memory_read(&address_space_memory, addr, desc,
                        sizeof(*desc), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read descriptor @ 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return -1;
    }
    desc->rdes0 = le32_to_cpu(desc->rdes0);
    desc->rdes1 = le32_to_cpu(desc->rdes1);
    desc->rdes2 = le32_to_cpu(desc->rdes2);
    desc->rdes3 = le32_to_cpu(desc->rdes3);
    return 0;
}

static int gmac_write_rx_desc(dma_addr_t addr, struct DWGMACRxDesc *desc)
{
    struct DWGMACRxDesc le_desc;
    le_desc.rdes0 = cpu_to_le32(desc->rdes0);
    le_desc.rdes1 = cpu_to_le32(desc->rdes1);
    le_desc.rdes2 = cpu_to_le32(desc->rdes2);
    le_desc.rdes3 = cpu_to_le32(desc->rdes3);
    if (dma_memory_write(&address_space_memory, addr, &le_desc,
                        sizeof(le_desc), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to write descriptor @ 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return -1;
    }
    return 0;
}

static int gmac_read_tx_desc(dma_addr_t addr, struct DWGMACTxDesc *desc)
{
    if (dma_memory_read(&address_space_memory, addr, desc,
                        sizeof(*desc), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read descriptor @ 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return -1;
    }
    desc->tdes0 = le32_to_cpu(desc->tdes0);
    desc->tdes1 = le32_to_cpu(desc->tdes1);
    desc->tdes2 = le32_to_cpu(desc->tdes2);
    desc->tdes3 = le32_to_cpu(desc->tdes3);
    return 0;
}

static int gmac_write_tx_desc(dma_addr_t addr, struct DWGMACTxDesc *desc)
{
    struct DWGMACTxDesc le_desc;
    le_desc.tdes0 = cpu_to_le32(desc->tdes0);
    le_desc.tdes1 = cpu_to_le32(desc->tdes1);
    le_desc.tdes2 = cpu_to_le32(desc->tdes2);
    le_desc.tdes3 = cpu_to_le32(desc->tdes3);
    if (dma_memory_write(&address_space_memory, addr, &le_desc,
                        sizeof(le_desc), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to write descriptor @ 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return -1;
    }
    return 0;
}

static int gmac_rx_transfer_frame_to_buffer(uint32_t rx_buf_len,
                                            uint32_t *left_frame,
                                            uint32_t rx_buf_addr,
                                            bool *eof_transferred,
                                            const uint8_t **frame_ptr,
                                            uint32_t *transferred)
{
    uint32_t to_transfer;
    /*
     * Check that the buffer is bigger than the frame being transferred.
     * If bigger then transfer only whats left of frame
     * Else, fill frame with all the content possible
     */
    if (rx_buf_len >= *left_frame) {
        to_transfer = *left_frame;
        *eof_transferred = true;
    } else {
        to_transfer = rx_buf_len;
    }

    /* Write this part of the frame to guest memory. */
    if (to_transfer &&
        dma_memory_write(&address_space_memory, rx_buf_addr, *frame_ptr,
                         to_transfer, MEMTXATTRS_UNSPECIFIED)) {
        return -1;
    }

    /* update frame pointer and size of whats left of frame */
    *frame_ptr += to_transfer;
    *left_frame -= to_transfer;
    *transferred += to_transfer;

    return 0;
}

static void gmac_dma_set_state(DWGMACState *gmac, int shift, uint32_t state)
{
    gmac->regs[R_DWMAC_DMA_STATUS] = deposit32(gmac->regs[R_DWMAC_DMA_STATUS],
        shift, 3, state);
}

static uint32_t gmac_rx_next_desc(DWGMACState *gmac, uint32_t desc_addr,
                                  const struct DWGMACRxDesc *desc)
{
    if (gmac_rx_is_end_ring(gmac, desc)) {
        return DWMAC_DMA_HOST_RX_DESC_MASK(
            gmac->regs[R_DWMAC_DMA_RX_BASE_ADDR]);
    }
    if (gmac_rx_is_chained(gmac, desc)) {
        return DWMAC_DMA_HOST_RX_DESC_MASK(desc->rdes3);
    }
    return desc_addr + gmac_desc_stride(gmac);
}

static uint32_t gmac_tx_next_desc(DWGMACState *gmac, uint32_t desc_addr,
                                  const struct DWGMACTxDesc *desc)
{
    if (gmac_tx_is_end_ring(gmac, desc)) {
        return DWMAC_DMA_HOST_TX_DESC_MASK(
            gmac->regs[R_DWMAC_DMA_TX_BASE_ADDR]);
    }
    if (gmac_tx_is_chained(gmac, desc)) {
        return DWMAC_DMA_HOST_TX_DESC_MASK(desc->tdes3);
    }
    return desc_addr + gmac_desc_stride(gmac);
}

static void gmac_dma_bus_error(DWGMACState *gmac, int state_shift)
{
    gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_FBI;
    gmac_dma_set_state(gmac, state_shift,
                       state_shift == DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT ?
                       DWMAC_DMA_STATUS_RX_SUSPENDED_STATE :
                       DWMAC_DMA_STATUS_TX_SUSPENDED_STATE);
    gmac_update_irq(gmac);
}

static ssize_t gmac_receive(NetClientState *nc, const uint8_t *buf, size_t len)
{
    DWGMACState *gmac = DW_GMAC(qemu_get_nic_opaque(nc));
    g_autofree uint8_t *frame = NULL;
    const uint8_t *frame_ptr;
    uint32_t left_frame;
    uint32_t desc_addr;
    uint32_t next_desc_addr;
    struct DWGMACRxDesc rx_desc;
    uint32_t transferred = 0;
    uint32_t descriptors = 0;
    bool first_desc = true;
    DWGMACRxFilterResult filter_result = { .accept = true };

    trace_dw_gmac_packet_receive(DEVICE(gmac)->canonical_path, len);
    if (!gmac_can_receive(nc)) {
        qemu_log_mask(LOG_GUEST_ERROR, "GMAC is not able to receive\n");
        return -1;
    }

    if (len > UINT32_MAX - ETH_FCS_LEN) {
        return -1;
    }

    if (gmac->rx_filtering) {
        filter_result = gmac_filter_packet(gmac, buf, len);
        if (!filter_result.accept) {
            /* The MAC consumed and discarded the frame; do not retry it. */
            return len;
        }
    }

    /* QEMU network backends omit the FCS, but DWMAC DMA includes it. */
    frame = g_malloc(len + ETH_FCS_LEN);
    memcpy(frame, buf, len);
    uint32_t fcs = cpu_to_le32(crc32(0, buf, len));
    memcpy(frame + len, &fcs, sizeof(fcs));
    frame_ptr = frame;
    left_frame = len + ETH_FCS_LEN;

    if (!gmac->regs[R_DWMAC_DMA_HOST_RX_DESC]) {
        gmac->regs[R_DWMAC_DMA_HOST_RX_DESC] =
            DWMAC_DMA_HOST_RX_DESC_MASK(gmac->regs[R_DWMAC_DMA_RX_BASE_ADDR]);
    }
    desc_addr = DWMAC_DMA_HOST_RX_DESC_MASK(
        gmac->regs[R_DWMAC_DMA_HOST_RX_DESC]);

    while (left_frame) {
        uint32_t rx_buf_len;
        uint32_t rx_buf_addr;
        bool eof_transferred = false;

        if (++descriptors > 65536) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: RX descriptor ring made no progress\n",
                          DEVICE(gmac)->canonical_path);
            gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_RU;
            gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
                               DWMAC_DMA_STATUS_RX_SUSPENDED_STATE);
            gmac_update_irq(gmac);
            return len;
        }

        gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
                           DWMAC_DMA_STATUS_RX_RUNNING_FETCHING_STATE);
        trace_dw_gmac_packet_desc_read(DEVICE(gmac)->canonical_path,
                                       desc_addr);
        if (gmac_read_rx_desc(desc_addr, &rx_desc)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "RX descriptor @ 0x%x cannot be read\n", desc_addr);
            gmac_dma_bus_error(gmac,
                               DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT);
            return len;
        }

        trace_dw_gmac_debug_desc_data(DEVICE(gmac)->canonical_path, &rx_desc,
                                      rx_desc.rdes0, rx_desc.rdes1,
                                      rx_desc.rdes2, rx_desc.rdes3);
        if (!(rx_desc.rdes0 & RX_DESC_RDES0_OWN)) {
            gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_RU;
            gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
                               DWMAC_DMA_STATUS_RX_SUSPENDED_STATE);
            gmac_update_irq(gmac);
            return len;
        }

        next_desc_addr = gmac_rx_next_desc(gmac, desc_addr, &rx_desc);
        rx_desc.rdes0 = RX_DESC_RDES0_FRM_TYPE_MASK;
        if (first_desc) {
            rx_desc.rdes0 |= RX_DESC_RDES0_FIRST_DESC_MASK;
        }

        gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
                           DWMAC_DMA_STATUS_RX_RUNNING_TRANSFERRING_STATE);

        rx_buf_len = gmac_rx_buffer1_size(gmac, &rx_desc);
        rx_buf_addr = rx_desc.rdes2;
        gmac->regs[R_DWMAC_DMA_CUR_RX_BUF_ADDR] = rx_buf_addr;
        if (gmac_rx_transfer_frame_to_buffer(rx_buf_len, &left_frame,
                                             rx_buf_addr, &eof_transferred,
                                             &frame_ptr, &transferred)) {
            gmac_dma_bus_error(gmac,
                               DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT);
            return len;
        }
        trace_dw_gmac_packet_receiving_buffer(DEVICE(gmac)->canonical_path,
                                              rx_buf_len, rx_buf_addr);

        if (!eof_transferred && !gmac_rx_is_chained(gmac, &rx_desc)) {
            rx_buf_len = gmac_rx_buffer2_size(gmac, &rx_desc);
            rx_buf_addr = rx_desc.rdes3;
            gmac->regs[R_DWMAC_DMA_CUR_RX_BUF_ADDR] = rx_buf_addr;
            if (gmac_rx_transfer_frame_to_buffer(rx_buf_len, &left_frame,
                                                 rx_buf_addr,
                                                 &eof_transferred,
                                                 &frame_ptr, &transferred)) {
                gmac_dma_bus_error(gmac,
                                   DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT);
                return len;
            }
            trace_dw_gmac_packet_receiving_buffer(
                DEVICE(gmac)->canonical_path, rx_buf_len, rx_buf_addr);
        }

        if (eof_transferred) {
            rx_desc.rdes0 |= RX_DESC_RDES0_LAST_DESC_MASK;
            if (filter_result.da_fail) {
                rx_desc.rdes0 |= RX_DESC_RDES0_DEST_ADDR_FILT_FAIL;
            }
            if (filter_result.sa_fail) {
                rx_desc.rdes0 |= RX_DESC_RDES0_SRC_ADDR_FILT_FAIL_MASK;
            }
            if (filter_result.vlan_tag) {
                rx_desc.rdes0 |= RX_DESC_RDES0_VLAN_TAG_MASK;
            }
            rx_desc.rdes0 = deposit32(rx_desc.rdes0,
                                      RX_DESC_RDES0_FRAME_LEN_SHIFT, 14,
                                      MIN(transferred, 0x3fff));
            if (transferred > 0x3fff) {
                rx_desc.rdes0 |= RX_DESC_RDES0_LEN_ERR_MASK |
                                 RX_DESC_RDES0_ERR_SUMM_MASK;
            }
        }

        gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
                           DWMAC_DMA_STATUS_RX_RUNNING_CLOSING_STATE);
        if (gmac_write_rx_desc(desc_addr, &rx_desc)) {
            gmac_dma_bus_error(gmac,
                               DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT);
            return len;
        }

        gmac->regs[R_DWMAC_DMA_HOST_RX_DESC] = next_desc_addr;
        if (eof_transferred) {
            if (!gmac_rx_irq_disabled(&rx_desc)) {
                gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_RI;
            }
            break;
        }

        first_desc = false;
        desc_addr = next_desc_addr;
    }

    trace_dw_gmac_packet_received(DEVICE(gmac)->canonical_path, left_frame);
    gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
                       DWMAC_DMA_STATUS_RX_RUNNING_WAITING_STATE);
    gmac_update_irq(gmac);
    return len;
}

static int gmac_tx_get_csum(DWGMACState *gmac,
                            const struct DWGMACTxDesc *desc)
{
    unsigned mask = gmac_tx_checksum_control(gmac, desc);
    int csum = 0;

    if (likely(mask > 0)) {
        csum |= CSUM_IP;
    }
    if (likely(mask > 1)) {
        csum |= CSUM_TCP | CSUM_UDP;
    }

    return csum;
}

static void gmac_try_send_next_packet(DWGMACState *gmac)
{
    size_t tx_buffer_size = 2048;
    g_autofree uint8_t *tx_send_buffer = g_malloc(tx_buffer_size);
    uint32_t desc_addr;
    struct DWGMACTxDesc tx_desc;
    uint32_t tx_buf_addr, tx_buf_len;
    uint32_t prev_buf_size = 0;
    uint32_t descriptors = 0;
    int csum = 0;

    if (!(gmac->regs[R_DW_GMAC_MAC_CONFIG] & DW_GMAC_MAC_CONFIG_TX_EN) ||
        !(gmac->regs[R_DWMAC_DMA_CONTROL] &
          DWMAC_DMA_CONTROL_START_STOP_TX)) {
        return;
    }

    /* steps 1&2 */
    if (!gmac->regs[R_DWMAC_DMA_HOST_TX_DESC]) {
        gmac->regs[R_DWMAC_DMA_HOST_TX_DESC] =
            DWMAC_DMA_HOST_TX_DESC_MASK(gmac->regs[R_DWMAC_DMA_TX_BASE_ADDR]);
    }
    desc_addr = gmac->regs[R_DWMAC_DMA_HOST_TX_DESC];

    while (true) {
        uint32_t next_desc_addr;

        if (++descriptors > 65536) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: TX descriptor ring made no progress\n",
                          DEVICE(gmac)->canonical_path);
            gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_TU;
            gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT,
                               DWMAC_DMA_STATUS_TX_SUSPENDED_STATE);
            gmac_update_irq(gmac);
            return;
        }

        gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT,
            DWMAC_DMA_STATUS_TX_RUNNING_FETCHING_STATE);
        if (gmac_read_tx_desc(desc_addr, &tx_desc)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "TX Descriptor @ 0x%x can't be read\n",
                          desc_addr);
            gmac_dma_bus_error(gmac,
                               DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT);
            return;
        }
        /* step 3 */

        trace_dw_gmac_packet_desc_read(DEVICE(gmac)->canonical_path,
            desc_addr);
        trace_dw_gmac_debug_desc_data(DEVICE(gmac)->canonical_path, &tx_desc,
            tx_desc.tdes0, tx_desc.tdes1, tx_desc.tdes2, tx_desc.tdes3);

        /* 1 = DMA Owned, 0 = Software Owned */
        if (!(tx_desc.tdes0 & TX_DESC_TDES0_OWN)) {
            trace_dw_gmac_tx_desc_owner(DEVICE(gmac)->canonical_path,
                                          desc_addr);
            gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_TU;
            gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT,
                DWMAC_DMA_STATUS_TX_SUSPENDED_STATE);
            gmac_update_irq(gmac);
            return;
        }

        gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT,
            DWMAC_DMA_STATUS_TX_RUNNING_READ_STATE);
        next_desc_addr = gmac_tx_next_desc(gmac, desc_addr, &tx_desc);
        if (gmac_tx_is_first(gmac, &tx_desc)) {
            csum = gmac_tx_get_csum(gmac, &tx_desc);
        }
        /* step 4 */
        tx_buf_addr = tx_desc.tdes2;
        gmac->regs[R_DWMAC_DMA_CUR_TX_BUF_ADDR] = tx_buf_addr;
        tx_buf_len = gmac_tx_buffer1_size(gmac, &tx_desc);

        if ((uint64_t)prev_buf_size + tx_buf_len > UINT16_MAX) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: TX frame exceeds 65535 bytes\n",
                          DEVICE(gmac)->canonical_path);
            gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_UNF;
            gmac_update_irq(gmac);
            return;
        }
        if ((size_t)prev_buf_size + tx_buf_len > tx_buffer_size) {
            tx_buffer_size = prev_buf_size + tx_buf_len;
            tx_send_buffer = g_realloc(tx_send_buffer, tx_buffer_size);
        }

        /* step 5 */
        if (tx_buf_len &&
            dma_memory_read(&address_space_memory, tx_buf_addr,
                            tx_send_buffer + prev_buf_size, tx_buf_len,
                            MEMTXATTRS_UNSPECIFIED)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read packet @ 0x%x\n",
                        __func__, tx_buf_addr);
            gmac_dma_bus_error(gmac,
                               DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT);
            return;
        }
        prev_buf_size += tx_buf_len;

        /* If not chained we'll have a second buffer. */
        if (!gmac_tx_is_chained(gmac, &tx_desc)) {
            tx_buf_addr = tx_desc.tdes3;
            gmac->regs[R_DWMAC_DMA_CUR_TX_BUF_ADDR] = tx_buf_addr;
            tx_buf_len = gmac_tx_buffer2_size(gmac, &tx_desc);

            if ((uint64_t)prev_buf_size + tx_buf_len > UINT16_MAX) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: TX frame exceeds 65535 bytes\n",
                              DEVICE(gmac)->canonical_path);
                gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_UNF;
                gmac_update_irq(gmac);
                return;
            }
            if ((size_t)prev_buf_size + tx_buf_len > tx_buffer_size) {
                tx_buffer_size = prev_buf_size + tx_buf_len;
                tx_send_buffer = g_realloc(tx_send_buffer, tx_buffer_size);
            }

            if (tx_buf_len &&
                dma_memory_read(&address_space_memory, tx_buf_addr,
                                tx_send_buffer + prev_buf_size,
                                tx_buf_len, MEMTXATTRS_UNSPECIFIED)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Failed to read packet @ 0x%x\n",
                              __func__, tx_buf_addr);
                gmac_dma_bus_error(gmac,
                                   DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT);
                return;
            }
            prev_buf_size += tx_buf_len;
        }
        if (gmac_tx_is_last(gmac, &tx_desc)) {
            uint16_t length = prev_buf_size;

            net_checksum_calculate(tx_send_buffer, length, csum);
            qemu_send_packet(qemu_get_queue(gmac->nic), tx_send_buffer, length);
            trace_dw_gmac_packet_sent(DEVICE(gmac)->canonical_path, length);
            prev_buf_size = 0;
        }

        /* step 6 */
        gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT,
            DWMAC_DMA_STATUS_TX_RUNNING_CLOSING_STATE);
        tx_desc.tdes0 &= ~TX_DESC_TDES0_OWN;
        if (gmac_write_tx_desc(desc_addr, &tx_desc)) {
            gmac_dma_bus_error(gmac,
                               DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT);
            return;
        }
        desc_addr = next_desc_addr;
        gmac->regs[R_DWMAC_DMA_HOST_TX_DESC] = desc_addr;

        /* step 7 */
        if (gmac_tx_irq_requested(gmac, &tx_desc)) {
            gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_TI;
            gmac_update_irq(gmac);
        }
    }
}

static void gmac_cleanup(NetClientState *nc)
{
    /* Nothing to do yet. */
}

static void gmac_set_link(NetClientState *nc)
{
    DWGMACState *gmac = qemu_get_nic_opaque(nc);

    trace_dw_gmac_set_link(!nc->link_down);
    gmac_phy_set_link(gmac, !nc->link_down);
}

static void dw_gmac_mdio_access(DWGMACState *gmac, uint16_t v)
{
    bool busy = v & DW_GMAC_MII_ADDR_BUSY;
    uint8_t is_write;
    uint8_t pa, gr;
    uint16_t data;

    if (busy) {
        is_write = v & DW_GMAC_MII_ADDR_WRITE;
        pa = DW_GMAC_MII_ADDR_PA(v);
        gr = DW_GMAC_MII_ADDR_GR(v);
        /* Both pa and gr are 5 bits, so they are less than 32. */
        g_assert(pa < DW_GMAC_MAX_PHYS);
        g_assert(gr < DW_GMAC_MAX_PHY_REGS);


        if (pa != gmac->phy_addr) {
            data = 0xffff;
            if (!(v & DW_GMAC_MII_ADDR_WRITE)) {
                gmac->regs[R_DW_GMAC_MII_DATA] = data;
            }
        } else if (v & DW_GMAC_MII_ADDR_WRITE) {
            data = gmac->regs[R_DW_GMAC_MII_DATA];
            /* Clear reset bit for BMCR register */
            switch (gr) {
            case MII_BMCR:
                data &= ~MII_BMCR_RESET;
                /* Autonegotiation is a W1C bit*/
                if (data & MII_BMCR_ANRESTART) {
                    /* Tells autonegotiation to not restart again */
                    data &= ~MII_BMCR_ANRESTART;
                }
                if ((data & MII_BMCR_AUTOEN) &&
                    !(gmac->phy_regs[pa][MII_BMSR] & MII_BMSR_AN_COMP)) {
                    /* sets autonegotiation as complete */
                    gmac->phy_regs[pa][MII_BMSR] |= MII_BMSR_AN_COMP;
                    /* Resolve AN automatically->need to set this */
                    gmac->phy_regs[pa][MII_ANLPAR] = 0x0000;
                }
            }
            gmac->phy_regs[pa][gr] = data;
        } else {
            data = gmac->phy_regs[pa][gr];
            gmac->regs[R_DW_GMAC_MII_DATA] = data;
        }
        trace_dw_gmac_mdio_access(DEVICE(gmac)->canonical_path, is_write, pa,
                                        gr, data);
    }
    gmac->regs[R_DW_GMAC_MII_ADDR] = v & ~DW_GMAC_MII_ADDR_BUSY;
}

static uint64_t dw_gmac_read(void *opaque, hwaddr offset, unsigned size)
{
    DWGMACState *gmac = opaque;
    uint32_t v = 0;
    unsigned int mac_index;
    bool mac_high;

    if (offset >= DW_GMAC_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid register offset: 0x%04" HWADDR_PRIx"\n",
                      DEVICE(gmac)->canonical_path, offset);
        return v;
    }

    if (gmac_decode_mac_addr_reg(offset, &mac_index, &mac_high)) {
        if (mac_index < gmac->num_mac_addrs) {
            v = gmac->regs[offset / sizeof(uint32_t)];
            if (mac_high) {
                v = mac_index ? v & DW_GMAC_MAC_ADDR_HIGH_MASK :
                    DW_GMAC_MAC_ADDR_AE | (v & 0xffff);
            }
        }
        goto done;
    }

    switch (offset) {
    /* Write only registers */
    case A_DWMAC_DMA_XMT_POLL_DEMAND:
    case A_DWMAC_DMA_RCV_POLL_DEMAND:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Read of write-only reg: offset: 0x%04" HWADDR_PRIx
                      "\n", DEVICE(gmac)->canonical_path, offset);
        break;

    case A_DW_GMAC_FRAME_FILTER:
        v = gmac->regs[offset / sizeof(uint32_t)] &
            DW_GMAC_FRAME_FILTER_MASK &
            (gmac->hash_bins ? UINT32_MAX :
             ~DW_GMAC_FRAME_FILTER_HASH_MASK);
        break;

    case A_DW_GMAC_HASH_HIGH:
    case A_DW_GMAC_HASH_LOW:
        v = gmac->hash_bins ?
            gmac->regs[offset / sizeof(uint32_t)] : 0;
        break;

    case A_DW_GMAC_VLAN_FLAG:
        v = gmac->regs[offset / sizeof(uint32_t)] & DW_GMAC_VLAN_TAG_MASK;
        break;

    case A_DW_GMAC_VLAN_HASH_TABLE:
        break;

    default:
        v = gmac->regs[offset / sizeof(uint32_t)];
    }

done:
    trace_dw_gmac_reg_read(DEVICE(gmac)->canonical_path, offset, v);
    return v;
}

static void dw_gmac_write(void *opaque, hwaddr offset,
                              uint64_t v, unsigned size)
{
    DWGMACState *gmac = opaque;
    unsigned int mac_index;
    bool mac_high;

    trace_dw_gmac_reg_write(DEVICE(gmac)->canonical_path, offset, v);

    if (offset >= DW_GMAC_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid register offset: 0x%04" HWADDR_PRIx"\n",
                      DEVICE(gmac)->canonical_path, offset);
        return;
    }

    if (gmac_decode_mac_addr_reg(offset, &mac_index, &mac_high)) {
        if (mac_index < gmac->num_mac_addrs) {
            if (mac_high) {
                gmac->regs[offset / sizeof(uint32_t)] = mac_index ?
                    (v & DW_GMAC_MAC_ADDR_HIGH_MASK) :
                    (DW_GMAC_MAC_ADDR_AE | (v & 0xffff));
            } else {
                gmac->regs[offset / sizeof(uint32_t)] = v;
            }

            if (!mac_index) {
                gmac_sync_conf_mac(gmac);
            }
        }
        gmac_update_irq(gmac);
        return;
    }

    switch (offset) {
    /* Read only registers */
    case A_DW_GMAC_VERSION:
    case A_DW_GMAC_INT_STATUS:
    case A_DW_GMAC_RGMII_STATUS:
    case A_DW_GMAC_PTP_STSR:
    case A_DW_GMAC_PTP_STNSR:
    case A_DWMAC_DMA_MISSED_FRAME_CTR:
    case A_DWMAC_DMA_HOST_TX_DESC:
    case A_DWMAC_DMA_HOST_RX_DESC:
    case A_DWMAC_DMA_CUR_TX_BUF_ADDR:
    case A_DWMAC_DMA_CUR_RX_BUF_ADDR:
    case A_DWMAC_DMA_HW_FEATURE:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write of read-only reg: offset: 0x%04" HWADDR_PRIx
                      ", value: 0x%04" PRIx64 "\n",
                      DEVICE(gmac)->canonical_path, offset, v);
        break;

    case A_DW_GMAC_MAC_CONFIG:
        gmac->regs[offset / sizeof(uint32_t)] = v;
        break;

    case A_DW_GMAC_FRAME_FILTER:
        gmac->regs[offset / sizeof(uint32_t)] =
            v & DW_GMAC_FRAME_FILTER_MASK &
            (gmac->hash_bins ? UINT32_MAX :
             ~DW_GMAC_FRAME_FILTER_HASH_MASK);
        break;

    case A_DW_GMAC_HASH_HIGH:
    case A_DW_GMAC_HASH_LOW:
        gmac->regs[offset / sizeof(uint32_t)] = gmac->hash_bins ? v : 0;
        break;

    case A_DW_GMAC_MII_ADDR:
        dw_gmac_mdio_access(gmac, v);
        break;

    case A_DW_GMAC_VLAN_FLAG:
        gmac->regs[offset / sizeof(uint32_t)] = v & DW_GMAC_VLAN_TAG_MASK;
        break;

    case A_DW_GMAC_VLAN_HASH_TABLE:
        gmac->regs[offset / sizeof(uint32_t)] = 0;
        break;

    case A_DWMAC_DMA_BUS_MODE:
        gmac->regs[offset / sizeof(uint32_t)] = v;
        if (v & DWMAC_DMA_BUS_MODE_SWR) {
            dw_gmac_soft_reset(gmac);
        }
        break;

    case A_DWMAC_DMA_RCV_POLL_DEMAND:
        /* The written value is not significant. */
        gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
            DWMAC_DMA_STATUS_RX_RUNNING_WAITING_STATE);
        break;

    case A_DWMAC_DMA_XMT_POLL_DEMAND:
        /* The written value is not significant. */
        gmac_try_send_next_packet(gmac);
        break;

    case A_DWMAC_DMA_CONTROL:
        gmac->regs[offset / sizeof(uint32_t)] = v;
        if (v & DWMAC_DMA_CONTROL_START_STOP_TX) {
            gmac_try_send_next_packet(gmac);
        } else {
            gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT,
                DWMAC_DMA_STATUS_TX_STOPPED_STATE);
        }
        if (v & DWMAC_DMA_CONTROL_START_STOP_RX) {
            gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
                DWMAC_DMA_STATUS_RX_RUNNING_WAITING_STATE);
            qemu_flush_queued_packets(qemu_get_queue(gmac->nic));
        } else {
            gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
                DWMAC_DMA_STATUS_RX_STOPPED_STATE);
        }
        break;

    case A_DWMAC_DMA_STATUS:
        /* Check that RO bits are not written to */
        if (DWMAC_DMA_STATUS_RO_MASK(v)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Write of read-only bits of reg: offset: 0x%04"
                           HWADDR_PRIx ", value: 0x%04" PRIx64 "\n",
                           DEVICE(gmac)->canonical_path, offset, v);
        }
        /* for W1C bits, implement W1C */
        gmac->regs[offset / sizeof(uint32_t)] &= ~DWMAC_DMA_STATUS_W1C_MASK(v);
        if (v & DWMAC_DMA_STATUS_RU) {
            /* Clearing RU bit indicates descriptor is owned by DMA again. */
            gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
                DWMAC_DMA_STATUS_RX_RUNNING_WAITING_STATE);
            qemu_flush_queued_packets(qemu_get_queue(gmac->nic));
        }
        break;

    default:
        gmac->regs[offset / sizeof(uint32_t)] = v;
        break;
    }

    gmac_update_irq(gmac);
}

static void dw_gmac_reset(DeviceState *dev)
{
    DWGMACState *gmac = DW_GMAC(dev);

    dw_gmac_soft_reset(gmac);
    memset(gmac->phy_regs, 0xff, sizeof(gmac->phy_regs));
    memcpy(gmac->phy_regs[gmac->phy_addr], phy_reg_init,
           sizeof(phy_reg_init));
    gmac->phy_regs[gmac->phy_addr][MII_PHYID1] = gmac->phy_id1;
    gmac->phy_regs[gmac->phy_addr][MII_PHYID2] = gmac->phy_id2;
    if (gmac->nic) {
        gmac_phy_set_link(gmac, !qemu_get_queue(gmac->nic)->link_down);
    }

    trace_dw_gmac_reset(DEVICE(gmac)->canonical_path,
                        gmac->phy_regs[gmac->phy_addr][MII_BMSR]);
}

static NetClientInfo net_dw_gmac_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = gmac_can_receive,
    .receive = gmac_receive,
    .cleanup = gmac_cleanup,
    .link_status_changed = gmac_set_link,
};

static const struct MemoryRegionOps dw_gmac_ops = {
    .read = dw_gmac_read,
    .write = dw_gmac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void dw_gmac_realize(DeviceState *dev, Error **errp)
{
    DWGMACState *gmac = DW_GMAC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (gmac->phy_addr >= DW_GMAC_MAX_PHYS) {
        error_setg(errp, "phy-addr must be below %u", DW_GMAC_MAX_PHYS);
        return;
    }
    if (!gmac->num_mac_addrs ||
        gmac->num_mac_addrs > DW_GMAC_MAX_MAC_ADDRS) {
        error_setg(errp, "num-mac-addresses must be between 1 and %u",
                   DW_GMAC_MAX_MAC_ADDRS);
        return;
    }
    if (gmac->hash_bins != 0 && gmac->hash_bins != 64) {
        error_setg(errp, "hash-bins must be either 0 or 64");
        return;
    }

    memory_region_init_io(&gmac->iomem, OBJECT(gmac), &dw_gmac_ops, gmac,
                          object_get_typename(OBJECT(dev)), 8 * KiB);
    sysbus_init_mmio(sbd, &gmac->iomem);
    sysbus_init_irq(sbd, &gmac->irq);

    qemu_macaddr_default_if_unset(&gmac->conf.macaddr);

    gmac->nic = qemu_new_nic(&net_dw_gmac_info, &gmac->conf,
                             object_get_typename(OBJECT(dev)), dev->id,
                             &dev->mem_reentrancy_guard, gmac);
    qemu_format_nic_info_str(qemu_get_queue(gmac->nic), gmac->conf.macaddr.a);
    gmac->regs[R_DW_GMAC_MAC0_ADDR_HI] = BIT(31) |
        (gmac->conf.macaddr.a[5] << 8) | gmac->conf.macaddr.a[4];
    gmac->regs[R_DW_GMAC_MAC0_ADDR_LO] =
        (gmac->conf.macaddr.a[3] << 24) |
        (gmac->conf.macaddr.a[2] << 16) |
        (gmac->conf.macaddr.a[1] << 8) | gmac->conf.macaddr.a[0];
}

static void dw_gmac_unrealize(DeviceState *dev)
{
    DWGMACState *gmac = DW_GMAC(dev);

    qemu_del_nic(gmac->nic);
}

static int dw_gmac_post_load(void *opaque, int version_id)
{
    DWGMACState *gmac = opaque;

    gmac_sync_conf_mac(gmac);
    gmac_update_irq(gmac);
    return 0;
}

static const VMStateField dw_gmac_vmstate_fields[] = {
    VMSTATE_UINT32_ARRAY(regs, DWGMACState, DW_GMAC_NR_REGS),
    VMSTATE_UINT16_2DARRAY_V(phy_regs, DWGMACState, DW_GMAC_MAX_PHYS,
                             DW_GMAC_MAX_PHY_REGS, 1),
    VMSTATE_END_OF_LIST()
};

static const VMStateDescription vmstate_dw_gmac = {
    .name = TYPE_DW_GMAC,
    .version_id = 1,
    .minimum_version_id = 0,
    .post_load = dw_gmac_post_load,
    .fields = dw_gmac_vmstate_fields,
};

static const VMStateDescription vmstate_npcm_gmac = {
    .name = TYPE_NPCM_GMAC,
    .version_id = 1,
    .minimum_version_id = 0,
    .post_load = dw_gmac_post_load,
    .fields = dw_gmac_vmstate_fields,
};

static const Property dw_gmac_properties[] = {
    DEFINE_NIC_PROPERTIES(DWGMACState, conf),
    DEFINE_PROP_UINT32("version", DWGMACState, version, 0x1032),
    DEFINE_PROP_UINT32("hw-feature", DWGMACState, hw_feature, 0x100d4f37),
    /* Preserve legacy users' accept-all behavior unless explicitly enabled. */
    DEFINE_PROP_BOOL("rx-filtering", DWGMACState, rx_filtering, false),
    DEFINE_PROP_UINT16("hash-bins", DWGMACState, hash_bins, 64),
    DEFINE_PROP_UINT8("num-mac-addresses", DWGMACState, num_mac_addrs, 4),
    DEFINE_PROP_UINT8("phy-addr", DWGMACState, phy_addr, 0),
    DEFINE_PROP_UINT16("phy-id1", DWGMACState, phy_id1, 0x0362),
    DEFINE_PROP_UINT16("phy-id2", DWGMACState, phy_id2, 0x5e6a),
};

static void dw_gmac_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->desc = "Synopsys DesignWare GMAC 3.x Controller";
    dc->realize = dw_gmac_realize;
    dc->unrealize = dw_gmac_unrealize;
    device_class_set_legacy_reset(dc, dw_gmac_reset);
    dc->vmsd = &vmstate_dw_gmac;
    device_class_set_props(dc, dw_gmac_properties);
}

static void npcm_gmac_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM GMAC Controller";
    dc->vmsd = &vmstate_npcm_gmac;
}

static const TypeInfo dw_gmac_types[] = {
    {
        .name = TYPE_DW_GMAC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(DWGMACState),
        .class_init = dw_gmac_class_init,
    },
    {
        .name = TYPE_NPCM_GMAC,
        .parent = TYPE_DW_GMAC,
        .class_init = npcm_gmac_class_init,
    },
};
DEFINE_TYPES(dw_gmac_types)
