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
#include "qemu/error-report.h"
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
REG32(DWMAC_DMA_RX_WATCHDOG, 0x1024)
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
#define DWMAC_DMA_RX_WATCHDOG_RIWT_MASK      0xff
#define DWMAC_DMA_RX_WATCHDOG_CYCLE_SCALE    256
#define DWMAC_DMA_HW_FEATURE_TX_COE           BIT(16)
#define DWMAC_DMA_HW_FEATURE_RX_COE_TYPE2     BIT(18)
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
#define DW_GMAC_MAC_CONFIG_IPC                 BIT(10)
#define DW_GMAC_FLOW_CTRL_UP                   BIT(3)
#define DW_GMAC_FLOW_CTRL_RFE                  BIT(2)

#define DW_GMAC_CONTROL_ETHERTYPE              0x8808
#define DW_GMAC_PAUSE_OPCODE                   0x0001
#define DW_GMAC_IP_PROTO_ICMP                  1
#define DW_GMAC_IP_PROTO_ICMPV6                58

typedef struct DWGMACRxFilterResult {
    bool accept;
    bool da_fail;
    bool sa_fail;
    bool vlan_tag;
} DWGMACRxFilterResult;

typedef struct DWGMACRxCOEStatus {
    bool available;
    uint32_t rdes4;
} DWGMACRxCOEStatus;

/*
 * One frame held in the receive FIFO: its length including FCS, the RDES0
 * status bits the MAC decided on arrival, and the extended status word.
 */
typedef struct DWGMACRxFifoEntry {
    uint32_t len;
    uint32_t rdes0;
    uint32_t rdes4;
} DWGMACRxFifoEntry;

static bool gmac_rx_enabled(const DWGMACState *gmac);
static void gmac_rx_fifo_clear(DWGMACState *gmac);

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

static bool gmac_rx_coe_type2_active(const DWGMACState *gmac)
{
    return gmac->rx_coe_type2 && gmac_uses_enhanced_desc(gmac) &&
           (gmac->regs[R_DWMAC_DMA_BUS_MODE] & DWMAC_DMA_BUS_MODE_ATDS) &&
           (gmac->regs[R_DW_GMAC_MAC_CONFIG] & DW_GMAC_MAC_CONFIG_IPC);
}

static bool gmac_rx_is_type_frame(const uint8_t *buf, size_t len)
{
    return len >= sizeof(struct eth_header) &&
           lduw_be_p(buf + offsetof(struct eth_header, h_proto)) >= 0x600;
}

static bool gmac_l3_protocol(const DWGMACState *gmac,
                             const uint8_t *buf, size_t len,
                             uint16_t *protocol, size_t *offset)
{
    if (len < sizeof(struct eth_header)) {
        return false;
    }

    *protocol = lduw_be_p(buf + offsetof(struct eth_header, h_proto));
    *offset = sizeof(struct eth_header);
    if (*protocol == ETH_P_VLAN ||
        (*protocol == ETH_P_DVLAN &&
         (gmac->regs[R_DW_GMAC_VLAN_FLAG] &
          DW_GMAC_VLAN_TAG_ESVL_MASK))) {
        if (len - *offset < sizeof(struct vlan_header)) {
            return false;
        }
        *protocol = lduw_be_p(buf + *offset +
                              offsetof(struct vlan_header, h_proto));
        *offset += sizeof(struct vlan_header);
    }
    return true;
}

static bool gmac_rx_checksum_valid(uint32_t initial, const uint8_t *buf,
                                   size_t len)
{
    g_assert(len <= UINT16_MAX);
    return net_checksum_finish(initial +
                               net_checksum_add(len, (uint8_t *)buf)) == 0;
}

static uint32_t gmac_ip4_pseudo_checksum(const uint8_t *ip, size_t l4_len,
                                         uint8_t protocol)
{
    uint8_t pseudo_tail[4] = {
        0, protocol, l4_len >> 8, l4_len,
    };
    uint32_t sum = net_checksum_add(8, (uint8_t *)ip + 12);

    sum += net_checksum_add(sizeof(pseudo_tail), pseudo_tail);
    return sum;
}

static uint32_t gmac_ip6_pseudo_checksum(const uint8_t *ip, size_t l4_len,
                                         uint8_t protocol)
{
    uint8_t pseudo_tail[8] = { 0 };
    uint32_t sum = net_checksum_add(32, (uint8_t *)ip + 8);

    stl_be_p(pseudo_tail, l4_len);
    pseudo_tail[7] = protocol;
    sum += net_checksum_add(sizeof(pseudo_tail), pseudo_tail);
    return sum;
}

static bool gmac_rx_ip4_l4_checksum_valid(const uint8_t *ip,
                                          const uint8_t *l4,
                                          size_t l4_len, uint8_t protocol)
{
    return gmac_rx_checksum_valid(
        gmac_ip4_pseudo_checksum(ip, l4_len, protocol), l4, l4_len);
}

static bool gmac_rx_ip6_l4_checksum_valid(const uint8_t *ip,
                                          const uint8_t *l4,
                                          size_t l4_len, uint8_t protocol)
{
    return gmac_rx_checksum_valid(
        gmac_ip6_pseudo_checksum(ip, l4_len, protocol), l4, l4_len);
}

static uint32_t gmac_rx_coe_l4_status(const uint8_t *ip, bool ipv6,
                                      const uint8_t *l4, size_t l4_len,
                                      uint8_t protocol)
{
    uint32_t status;
    bool valid;

    switch (protocol) {
    case IP_PROTO_UDP:
        status = RX_DESC_RDES4_PAYLOAD_UDP;
        if (l4_len < sizeof(udp_header) ||
            lduw_be_p(l4 + offsetof(udp_header, uh_ulen)) != l4_len) {
            return status | RX_DESC_RDES4_IP_PAYLOAD_ERR;
        }
        if (!lduw_be_p(l4 + offsetof(udp_header, uh_sum))) {
            return ipv6 ? status | RX_DESC_RDES4_IP_PAYLOAD_ERR : status;
        }
        valid = ipv6 ?
            gmac_rx_ip6_l4_checksum_valid(ip, l4, l4_len, protocol) :
            gmac_rx_ip4_l4_checksum_valid(ip, l4, l4_len, protocol);
        return status | (valid ? 0 : RX_DESC_RDES4_IP_PAYLOAD_ERR);

    case IP_PROTO_TCP:
        status = RX_DESC_RDES4_PAYLOAD_TCP;
        if (l4_len < sizeof(tcp_header)) {
            return status | RX_DESC_RDES4_IP_PAYLOAD_ERR;
        }
        valid = ipv6 ?
            gmac_rx_ip6_l4_checksum_valid(ip, l4, l4_len, protocol) :
            gmac_rx_ip4_l4_checksum_valid(ip, l4, l4_len, protocol);
        return status | (valid ? 0 : RX_DESC_RDES4_IP_PAYLOAD_ERR);

    case DW_GMAC_IP_PROTO_ICMP:
        if (ipv6) {
            break;
        }
        status = RX_DESC_RDES4_PAYLOAD_ICMP;
        valid = l4_len >= 4 && gmac_rx_checksum_valid(0, l4, l4_len);
        return status | (valid ? 0 : RX_DESC_RDES4_IP_PAYLOAD_ERR);

    case DW_GMAC_IP_PROTO_ICMPV6:
        if (!ipv6) {
            break;
        }
        status = RX_DESC_RDES4_PAYLOAD_ICMP;
        valid = l4_len >= 4 &&
                gmac_rx_ip6_l4_checksum_valid(ip, l4, l4_len, protocol);
        return status | (valid ? 0 : RX_DESC_RDES4_IP_PAYLOAD_ERR);
    }

    return RX_DESC_RDES4_PAYLOAD_UNKNOWN |
           RX_DESC_RDES4_IP_CSUM_BYPASSED;
}

static uint32_t gmac_rx_coe_ip4_status(const uint8_t *buf, size_t len,
                                       size_t offset)
{
    const uint8_t *ip = buf + offset;
    size_t available = len - offset;
    size_t header_len;
    size_t total_len;
    uint32_t status = RX_DESC_RDES4_IPV4_PACKET;

    if (available < sizeof(struct ip_header) ||
        IP_HEADER_VERSION((struct ip_header *)ip) != IP_HEADER_VERSION_4) {
        return status | RX_DESC_RDES4_IP_HEADER_ERR;
    }

    header_len = IP_HDR_GET_LEN(ip);
    total_len = lduw_be_p(ip + offsetof(struct ip_header, ip_len));
    if (header_len < sizeof(struct ip_header) || header_len > available ||
        total_len < header_len || total_len > available ||
        !gmac_rx_checksum_valid(0, ip, header_len)) {
        return status | RX_DESC_RDES4_IP_HEADER_ERR;
    }
    if (lduw_be_p(ip + offsetof(struct ip_header, ip_off)) &
        (IP_OFFMASK | IP_MF)) {
        return status | RX_DESC_RDES4_IP_CSUM_BYPASSED;
    }

    return status |
           gmac_rx_coe_l4_status(ip, false, ip + header_len,
                                 total_len - header_len,
                                 ip[offsetof(struct ip_header, ip_p)]);
}

static uint32_t gmac_rx_coe_ip6_status(const uint8_t *buf, size_t len,
                                       size_t offset)
{
    const uint8_t *ip = buf + offset;
    size_t available = len - offset;
    size_t payload_len;
    size_t cursor;
    size_t end;
    uint8_t protocol;
    uint32_t status = RX_DESC_RDES4_IPV6_PACKET;

    if (available < sizeof(struct ip6_header) || (ip[0] >> 4) != 6) {
        return status | RX_DESC_RDES4_IP_HEADER_ERR;
    }

    payload_len = lduw_be_p(ip + 4);
    if (payload_len > available - sizeof(struct ip6_header)) {
        return status | RX_DESC_RDES4_IP_HEADER_ERR;
    }
    cursor = sizeof(struct ip6_header);
    end = cursor + payload_len;
    protocol = ip[6];

    for (;;) {
        size_t extension_len;

        switch (protocol) {
        case IP6_ROUTING:
            if (end - cursor < sizeof(struct ip6_ext_hdr)) {
                return status | RX_DESC_RDES4_IP_HEADER_ERR;
            }
            extension_len = ((size_t)ip[cursor + 1] + 1) *
                            IP6_EXT_GRANULARITY;
            if (extension_len > end - cursor) {
                return status | RX_DESC_RDES4_IP_HEADER_ERR;
            }
            return status | RX_DESC_RDES4_IP_CSUM_BYPASSED;
        case IP6_FRAGMENT:
            if (end - cursor < IP6_EXT_GRANULARITY) {
                return status | RX_DESC_RDES4_IP_HEADER_ERR;
            }
            return status | RX_DESC_RDES4_IP_CSUM_BYPASSED;
        case IP6_AUTHENTICATION:
            if (end - cursor < sizeof(struct ip6_ext_hdr)) {
                return status | RX_DESC_RDES4_IP_HEADER_ERR;
            }
            extension_len = ((size_t)ip[cursor + 1] + 2) * 4;
            if (extension_len > end - cursor) {
                return status | RX_DESC_RDES4_IP_HEADER_ERR;
            }
            return status | RX_DESC_RDES4_IP_CSUM_BYPASSED;
        case IP6_ESP:
            if (end - cursor < IP6_EXT_GRANULARITY) {
                return status | RX_DESC_RDES4_IP_HEADER_ERR;
            }
            return status | RX_DESC_RDES4_IP_CSUM_BYPASSED;
        case IP6_HOP_BY_HOP:
        case IP6_DESTINATON:
        case IP6_MOBILITY:
            if (end - cursor < sizeof(struct ip6_ext_hdr)) {
                return status | RX_DESC_RDES4_IP_HEADER_ERR;
            }
            extension_len = ((size_t)ip[cursor + 1] + 1) *
                            IP6_EXT_GRANULARITY;
            if (extension_len > end - cursor) {
                return status | RX_DESC_RDES4_IP_HEADER_ERR;
            }
            protocol = ip[cursor];
            cursor += extension_len;
            break;
        default:
            return status |
                   gmac_rx_coe_l4_status(ip, true, ip + cursor,
                                         end - cursor, protocol);
        }
    }
}

static DWGMACRxCOEStatus gmac_rx_coe_status(const DWGMACState *gmac,
                                            const uint8_t *buf, size_t len)
{
    DWGMACRxCOEStatus result = {
        .available = gmac_rx_coe_type2_active(gmac),
    };
    uint16_t protocol;
    size_t offset;

    if (!result.available) {
        return result;
    }
    if (!gmac_l3_protocol(gmac, buf, len, &protocol, &offset)) {
        result.rdes4 = RX_DESC_RDES4_IP_CSUM_BYPASSED;
    } else if (protocol == ETH_P_IP) {
        result.rdes4 = gmac_rx_coe_ip4_status(buf, len, offset);
    } else if (protocol == ETH_P_IPV6) {
        result.rdes4 = gmac_rx_coe_ip6_status(buf, len, offset);
    } else {
        result.rdes4 = RX_DESC_RDES4_IP_CSUM_BYPASSED;
    }
    return result;
}

static bool gmac_rx_coe_has_error(const DWGMACRxCOEStatus *status)
{
    return status->available &&
           (status->rdes4 & (RX_DESC_RDES4_IP_HEADER_ERR |
                             RX_DESC_RDES4_IP_PAYLOAD_ERR));
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
    if (gmac->rx_watchdog_timer) {
        timer_del(gmac->rx_watchdog_timer);
    }
    memcpy(gmac->regs, dw_gmac_cold_reset_values,
           DW_GMAC_NR_REGS * sizeof(uint32_t));
    for (unsigned int index = 1; index < gmac->num_mac_addrs; index++) {
        gmac->regs[gmac_mac_addr_reg(index, true) / 4] = 0x0000ffff;
        gmac->regs[gmac_mac_addr_reg(index, false) / 4] = 0xffffffff;
    }
    gmac_sanitize_filter_regs(gmac);
    gmac_rx_fifo_clear(gmac);
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

static void gmac_update_phy_link(DWGMACState *gmac)
{
    bool active = gmac->nic && !qemu_get_queue(gmac->nic)->link_down &&
                  !gmac->phy_reset_asserted;

    gmac_phy_set_link(gmac, active);
}

static void dw_gmac_reset_phy(DWGMACState *gmac)
{
    memset(gmac->phy_regs, 0xff, sizeof(gmac->phy_regs));
    memcpy(gmac->phy_regs[gmac->phy_addr], phy_reg_init,
           sizeof(phy_reg_init));
    gmac->phy_regs[gmac->phy_addr][MII_PHYID1] = gmac->phy_id1;
    gmac->phy_regs[gmac->phy_addr][MII_PHYID2] = gmac->phy_id2;
}

static void dw_gmac_set_phy_irq(DWGMACState *gmac, bool asserted)
{
    if (gmac->phy_irq_n) {
        qemu_set_irq(gmac->phy_irq_n, !asserted);
    }
}

static void dw_gmac_phy_reset_input(void *opaque, int n, int level)
{
    DWGMACState *gmac = opaque;
    bool asserted = !level;

    if (gmac->phy_reset_asserted == asserted) {
        return;
    }

    gmac->phy_reset_asserted = asserted;
    if (asserted) {
        dw_gmac_reset_phy(gmac);
    }
    gmac_update_phy_link(gmac);
}

/*
 * Frames keep arriving while the ring is full: silicon holds them in the
 * receive FIFO and re-polls the descriptor on the next one, which is the only
 * resume path a driver that never writes the poll-demand register has.
 */
static bool gmac_can_receive(NetClientState *nc)
{
    return gmac_rx_enabled(DW_GMAC(qemu_get_nic_opaque(nc)));
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

static void gmac_rx_watchdog_expired(void *opaque)
{
    DWGMACState *gmac = opaque;

    gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_RI;
    gmac_update_irq(gmac);
}

static void gmac_rx_watchdog_start(DWGMACState *gmac)
{
    uint64_t cycles;
    int64_t delay_ns;
    uint32_t riwt = gmac->regs[R_DWMAC_DMA_RX_WATCHDOG] &
                    DWMAC_DMA_RX_WATCHDOG_RIWT_MASK;

    if (!riwt || !gmac->rx_watchdog_clock_hz ||
        timer_pending(gmac->rx_watchdog_timer)) {
        return;
    }

    cycles = (uint64_t)riwt * DWMAC_DMA_RX_WATCHDOG_CYCLE_SCALE;
    delay_ns = DIV_ROUND_UP(cycles * NANOSECONDS_PER_SECOND,
                            gmac->rx_watchdog_clock_hz);
    timer_mod_ns(gmac->rx_watchdog_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + MAX(delay_ns, 1));
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

static int gmac_write_rx_ext_status(dma_addr_t addr, uint32_t status)
{
    uint32_t le_status = cpu_to_le32(status);

    addr += sizeof(struct DWGMACRxDesc);
    if (dma_memory_write(&address_space_memory, addr, &le_status,
                         sizeof(le_status), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to write descriptor status @ 0x%"
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

static bool gmac_rx_enabled(const DWGMACState *gmac)
{
    return (gmac->regs[R_DW_GMAC_MAC_CONFIG] & DW_GMAC_MAC_CONFIG_RX_EN) &&
           (gmac->regs[R_DWMAC_DMA_CONTROL] & DWMAC_DMA_CONTROL_START_STOP_RX);
}

static uint32_t gmac_rx_fifo_data_bytes(const DWGMACState *gmac)
{
    uint32_t offset = 0, total = 0;

    while (offset < gmac->rx_fifo_used) {
        DWGMACRxFifoEntry entry;

        memcpy(&entry, gmac->rx_fifo + offset, sizeof(entry));
        total += entry.len;
        offset += sizeof(entry) + entry.len;
    }
    return total;
}

static bool gmac_rx_fifo_push(DWGMACState *gmac, const uint8_t *frame,
                              uint32_t len, uint32_t rdes0, uint32_t rdes4)
{
    DWGMACRxFifoEntry entry = { .len = len, .rdes0 = rdes0, .rdes4 = rdes4 };
    uint32_t needed = gmac->rx_fifo_used + sizeof(entry) + len;

    if (gmac_rx_fifo_data_bytes(gmac) + len > gmac->rx_fifo_size) {
        return false;
    }
    if (needed > gmac->rx_fifo_capacity) {
        gmac->rx_fifo = g_realloc(gmac->rx_fifo, needed);
        gmac->rx_fifo_capacity = needed;
    }
    memcpy(gmac->rx_fifo + gmac->rx_fifo_used, &entry, sizeof(entry));
    memcpy(gmac->rx_fifo + gmac->rx_fifo_used + sizeof(entry), frame, len);
    gmac->rx_fifo_used = needed;
    return true;
}

static bool gmac_rx_fifo_peek(const DWGMACState *gmac, DWGMACRxFifoEntry *entry,
                              const uint8_t **frame)
{
    if (!gmac->rx_fifo_used) {
        return false;
    }
    memcpy(entry, gmac->rx_fifo, sizeof(*entry));
    *frame = gmac->rx_fifo + sizeof(*entry);
    return true;
}

static void gmac_rx_fifo_pop(DWGMACState *gmac)
{
    DWGMACRxFifoEntry entry;
    uint32_t size;

    memcpy(&entry, gmac->rx_fifo, sizeof(entry));
    size = sizeof(entry) + entry.len;
    memmove(gmac->rx_fifo, gmac->rx_fifo + size, gmac->rx_fifo_used - size);
    gmac->rx_fifo_used -= size;
}

static void gmac_rx_fifo_clear(DWGMACState *gmac)
{
    gmac->rx_fifo_used = 0;
}

/* Validate a FIFO image, typically one that arrived through migration. */
static bool gmac_rx_fifo_valid(const DWGMACState *gmac)
{
    uint32_t offset = 0;

    while (offset < gmac->rx_fifo_used) {
        DWGMACRxFifoEntry entry;

        if (gmac->rx_fifo_used - offset < sizeof(entry)) {
            return false;
        }
        memcpy(&entry, gmac->rx_fifo + offset, sizeof(entry));
        if (entry.len < ETH_FCS_LEN ||
            entry.len > gmac->rx_fifo_used - offset - sizeof(entry)) {
            return false;
        }
        offset += sizeof(entry) + entry.len;
    }
    return offset == gmac->rx_fifo_used &&
           gmac_rx_fifo_data_bytes(gmac) <= gmac->rx_fifo_size;
}

static uint32_t gmac_rx_current_desc(const DWGMACState *gmac)
{
    uint32_t current = gmac->regs[R_DWMAC_DMA_HOST_RX_DESC];

    if (!current) {
        current = gmac->regs[R_DWMAC_DMA_RX_BASE_ADDR];
    }
    return DWMAC_DMA_HOST_RX_DESC_MASK(current);
}

typedef enum {
    GMAC_RX_FITS,        /* enough DMA-owned buffers from the current descriptor */
    GMAC_RX_FITS_LATER,  /* blocked by a software-owned descriptor */
    GMAC_RX_FITS_NEVER,  /* every descriptor is DMA-owned and still too small */
} GMACRxFit;

/*
 * Decide whether a frame can be placed before anything is written back, so a
 * frame that cannot be completed is never half-delivered.  Silicon closes
 * each descriptor as its buffer fills and would leave the first part of a
 * blocked frame with FD set and no LD; QEMU instead holds the whole frame
 * until it fits.  The outcome for the frame is identical.
 */
static GMACRxFit gmac_rx_frame_fits(DWGMACState *gmac, uint32_t len)
{
    uint32_t start = gmac_rx_current_desc(gmac);
    uint32_t addr = start;
    uint64_t available = 0;
    uint32_t descriptors = 0;

    while (available < len) {
        struct DWGMACRxDesc desc;

        if (++descriptors > 65536) {
            return GMAC_RX_FITS_NEVER;
        }
        if (gmac_read_rx_desc(addr, &desc)) {
            /* Let delivery report the bus error rather than stalling. */
            return GMAC_RX_FITS;
        }
        if (!(desc.rdes0 & RX_DESC_RDES0_OWN)) {
            return GMAC_RX_FITS_LATER;
        }
        available += gmac_rx_buffer1_size(gmac, &desc);
        if (!gmac_rx_is_chained(gmac, &desc)) {
            available += gmac_rx_buffer2_size(gmac, &desc);
        }
        addr = gmac_rx_next_desc(gmac, addr, &desc);
        if (addr == start) {
            break;
        }
    }
    return available >= len ? GMAC_RX_FITS : GMAC_RX_FITS_NEVER;
}

/* Enter Suspended once; a re-poll that fails again is not a new RU event. */
static void gmac_rx_suspend(DWGMACState *gmac)
{
    uint32_t state = extract32(gmac->regs[R_DWMAC_DMA_STATUS],
                               DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT, 3);

    if (state != DWMAC_DMA_STATUS_RX_SUSPENDED_STATE) {
        gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_RU;
        gmac_dma_set_state(gmac, DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT,
                           DWMAC_DMA_STATUS_RX_SUSPENDED_STATE);
        gmac_update_irq(gmac);
    }
}

/*
 * Missed Frame and Buffer Overflow Counter: bits 15:0 count frames the DMA
 * missed for lack of a receive buffer, bits 27:17 frames the MAC dropped on
 * receive FIFO overflow; bits 16 and 28 latch counter overflow.
 */
static void gmac_rx_count_missed(DWGMACState *gmac, bool fifo_overflow)
{
    uint32_t *ctr = &gmac->regs[R_DWMAC_DMA_MISSED_FRAME_CTR];

    if (fifo_overflow) {
        uint32_t count = extract32(*ctr, 17, 11);

        *ctr = count == 0x7ff ? *ctr | BIT(28) : deposit32(*ctr, 17, 11,
                                                             count + 1);
        gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_OVF;
    } else {
        uint32_t count = extract32(*ctr, 0, 16);

        *ctr = count == 0xffff ? *ctr | BIT(16) : deposit32(*ctr, 0, 16,
                                                              count + 1);
    }
    gmac_update_irq(gmac);
}

/*
 * Write one frame into the descriptor ring.  Returns false only if the very
 * first descriptor turned out to be software-owned before anything was
 * written, so the caller can hold the frame; every other outcome consumes it.
 */
static bool gmac_rx_deliver(DWGMACState *gmac, const uint8_t *frame,
                            uint32_t len, uint32_t rdes0_flags, uint32_t rdes4)
{
    const uint8_t *frame_ptr = frame;
    uint32_t left_frame = len;
    uint32_t desc_addr;
    uint32_t next_desc_addr;
    struct DWGMACRxDesc rx_desc;
    uint32_t transferred = 0;
    uint32_t descriptors = 0;
    bool first_desc = true;

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
            gmac_rx_suspend(gmac);
            return true;
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
            return true;
        }

        trace_dw_gmac_debug_desc_data(DEVICE(gmac)->canonical_path, &rx_desc,
                                      rx_desc.rdes0, rx_desc.rdes1,
                                      rx_desc.rdes2, rx_desc.rdes3);
        if (!(rx_desc.rdes0 & RX_DESC_RDES0_OWN)) {
            if (first_desc) {
                return false;
            }
            /* Unreachable after the fit check unless the guest raced the DMA. */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: RX descriptor @ 0x%x reclaimed mid-frame\n",
                          DEVICE(gmac)->canonical_path, desc_addr);
            gmac_rx_suspend(gmac);
            return true;
        }

        next_desc_addr = gmac_rx_next_desc(gmac, desc_addr, &rx_desc);
        rx_desc.rdes0 = rdes0_flags & RX_DESC_RDES0_FRM_TYPE_MASK;
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
            return true;
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
                return true;
            }
            trace_dw_gmac_packet_receiving_buffer(
                DEVICE(gmac)->canonical_path, rx_buf_len, rx_buf_addr);
        }

        if (eof_transferred) {
            rx_desc.rdes0 |= rdes0_flags | RX_DESC_RDES0_LAST_DESC_MASK;
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
        if (eof_transferred &&
            (rdes0_flags & RX_DESC_RDES0_EXT_STATUS_AVAIL_MASK) &&
            gmac_write_rx_ext_status(desc_addr, rdes4)) {
            gmac_dma_bus_error(gmac,
                               DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT);
            return true;
        }
        if (gmac_write_rx_desc(desc_addr, &rx_desc)) {
            gmac_dma_bus_error(gmac,
                               DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT);
            return true;
        }

        gmac->regs[R_DWMAC_DMA_HOST_RX_DESC] = next_desc_addr;
        if (eof_transferred) {
            if (!gmac_rx_irq_disabled(&rx_desc)) {
                timer_del(gmac->rx_watchdog_timer);
                gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_RI;
            } else if (!(gmac->regs[R_DWMAC_DMA_STATUS] &
                         DWMAC_DMA_STATUS_RI)) {
                gmac_rx_watchdog_start(gmac);
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
    return true;
}

/*
 * Re-poll the ring and place as many held frames as now fit, in order.  This
 * is what a resumed receive process does on the next frame, a receive poll
 * demand, or a DMA start.
 */
static void gmac_rx_drain(DWGMACState *gmac)
{
    DWGMACRxFifoEntry entry;
    const uint8_t *frame;

    if (!gmac_rx_enabled(gmac)) {
        return;
    }
    while (gmac_rx_fifo_peek(gmac, &entry, &frame)) {
        switch (gmac_rx_frame_fits(gmac, entry.len)) {
        case GMAC_RX_FITS:
            if (!gmac_rx_deliver(gmac, frame, entry.len, entry.rdes0,
                                 entry.rdes4)) {
                gmac_rx_suspend(gmac);
                return;
            }
            gmac_rx_fifo_pop(gmac);
            break;
        case GMAC_RX_FITS_LATER:
            gmac_rx_suspend(gmac);
            return;
        case GMAC_RX_FITS_NEVER:
            gmac_rx_fifo_pop(gmac);
            gmac_rx_count_missed(gmac, false);
            break;
        }
    }
}

static ssize_t gmac_receive(NetClientState *nc, const uint8_t *buf, size_t len)
{
    DWGMACState *gmac = DW_GMAC(qemu_get_nic_opaque(nc));
    g_autofree uint8_t *frame = NULL;
    uint32_t frame_len;
    DWGMACRxFilterResult filter_result = { .accept = true };
    DWGMACRxCOEStatus coe_status;
    uint32_t rdes0_flags;

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

    /* The MAC decides frame status on arrival, before anything is queued. */
    rdes0_flags = !gmac->rx_coe_type2 || gmac_rx_is_type_frame(buf, len) ?
                  RX_DESC_RDES0_FRM_TYPE_MASK : 0;
    coe_status = gmac_rx_coe_status(gmac, buf, len);
    if (coe_status.available) {
        rdes0_flags |= RX_DESC_RDES0_EXT_STATUS_AVAIL_MASK;
        if (gmac_rx_coe_has_error(&coe_status)) {
            rdes0_flags |= RX_DESC_RDES0_ERR_SUMM_MASK;
        }
    }
    if (filter_result.da_fail) {
        rdes0_flags |= RX_DESC_RDES0_DEST_ADDR_FILT_FAIL;
    }
    if (filter_result.sa_fail) {
        rdes0_flags |= RX_DESC_RDES0_SRC_ADDR_FILT_FAIL_MASK;
    }
    if (filter_result.vlan_tag) {
        rdes0_flags |= RX_DESC_RDES0_VLAN_TAG_MASK;
    }

    /* QEMU network backends omit the FCS, but DWMAC DMA includes it. */
    frame_len = len + ETH_FCS_LEN;
    frame = g_malloc(frame_len);
    memcpy(frame, buf, len);
    uint32_t fcs = cpu_to_le32(crc32(0, buf, len));
    memcpy(frame + len, &fcs, sizeof(fcs));

    /* A new frame resumes a suspended receive process: re-poll first. */
    gmac_rx_drain(gmac);

    if (!gmac->rx_fifo_used) {
        switch (gmac_rx_frame_fits(gmac, frame_len)) {
        case GMAC_RX_FITS:
            if (gmac_rx_deliver(gmac, frame, frame_len, rdes0_flags,
                                coe_status.rdes4)) {
                return len;
            }
            break;
        case GMAC_RX_FITS_NEVER:
            gmac_rx_count_missed(gmac, false);
            return len;
        case GMAC_RX_FITS_LATER:
            break;
        }
    }

    /* Hold the frame behind anything already waiting, or overflow. */
    if (!gmac_rx_fifo_push(gmac, frame, frame_len, rdes0_flags,
                           coe_status.rdes4)) {
        gmac_rx_count_missed(gmac, true);
        return len;
    }
    gmac_rx_suspend(gmac);
    return len;
}

static uint32_t gmac_tx_coe_error(const DWGMACState *gmac, uint32_t status)
{
    if (gmac_uses_enhanced_desc(gmac)) {
        status |= TX_DESC_TDES0_ERR_SUMM_MASK;
    }
    return status;
}

static uint32_t gmac_tx_coe_insert_l4(uint8_t *ip, bool ipv6,
                                      uint8_t *l4, size_t available,
                                      size_t declared, uint8_t protocol,
                                      unsigned int cic)
{
    size_t checksum_offset;
    size_t minimum;
    size_t checksum_len;
    uint32_t status = 0;
    uint32_t sum;
    uint16_t checksum;

    switch (protocol) {
    case IP_PROTO_TCP:
        minimum = sizeof(tcp_header);
        checksum_offset = offsetof(tcp_header, th_sum);
        break;
    case IP_PROTO_UDP:
        minimum = sizeof(udp_header);
        checksum_offset = offsetof(udp_header, uh_sum);
        break;
    case DW_GMAC_IP_PROTO_ICMP:
        if (ipv6) {
            return 0;
        }
        minimum = 4;
        checksum_offset = 2;
        break;
    case DW_GMAC_IP_PROTO_ICMPV6:
        if (!ipv6) {
            return 0;
        }
        minimum = 4;
        checksum_offset = 2;
        break;
    default:
        return 0;
    }

    if (available < declared) {
        status |= TX_DESC_TDES0_PYLD_CHKSM_ERR_MASK;
    }
    if (declared < minimum || available < minimum) {
        return status | TX_DESC_TDES0_PYLD_CHKSM_ERR_MASK;
    }
    checksum_len = MIN(available, declared);
    if (cic == TX_DESC_CIC_PAYLOAD_FULL) {
        stw_be_p(l4 + checksum_offset, 0);
        if (ipv6) {
            sum = gmac_ip6_pseudo_checksum(ip, declared, protocol);
        } else if (protocol == DW_GMAC_IP_PROTO_ICMP) {
            sum = 0;
        } else {
            sum = gmac_ip4_pseudo_checksum(ip, declared, protocol);
        }
    } else {
        /* CIC2 includes the pseudo-header seed supplied in this field. */
        sum = 0;
    }
    sum += net_checksum_add(checksum_len, l4);
    checksum = net_checksum_finish(sum);
    if (protocol == IP_PROTO_UDP && checksum == 0) {
        checksum = UINT16_MAX;
    }
    stw_be_p(l4 + checksum_offset, checksum);
    return status;
}

static uint32_t gmac_tx_coe_insert_ip4(uint8_t *buf, size_t len,
                                       size_t offset, unsigned int cic)
{
    uint8_t *ip = buf + offset;
    size_t available = len - offset;
    size_t header_len;
    size_t total_len;
    uint32_t status = 0;

    if (available < sizeof(struct ip_header) ||
        IP_HEADER_VERSION((struct ip_header *)ip) != IP_HEADER_VERSION_4) {
        return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
    }

    header_len = IP_HDR_GET_LEN(ip);
    if (header_len < sizeof(struct ip_header) || header_len > available) {
        return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
    }

    /* The IPv4 header field is ignored and replaced for every nonzero CIC. */
    eth_fix_ip4_checksum(ip, header_len);
    total_len = lduw_be_p(ip + offsetof(struct ip_header, ip_len));
    if (total_len < header_len) {
        status |= TX_DESC_TDES0_IP_HEAD_ERR_MASK;
    }
    if (cic < TX_DESC_CIC_PAYLOAD_PARTIAL ||
        status ||
        (lduw_be_p(ip + offsetof(struct ip_header, ip_off)) &
         (IP_OFFMASK | IP_MF))) {
        return status;
    }

    status |= gmac_tx_coe_insert_l4(
        ip, false, ip + header_len, available - header_len,
        total_len - header_len,
        ip[offsetof(struct ip_header, ip_p)], cic);
    return status;
}

static uint32_t gmac_tx_coe_insert_ip6(uint8_t *buf, size_t len,
                                       size_t offset, unsigned int cic)
{
    uint8_t *ip = buf + offset;
    size_t available = len - offset;
    size_t declared_end;
    size_t actual_end;
    size_t cursor;
    uint8_t protocol;

    if (available < sizeof(struct ip6_header) || (ip[0] >> 4) != 6) {
        return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
    }
    if (cic == TX_DESC_CIC_IP_HEADER) {
        return 0;
    }

    declared_end = sizeof(struct ip6_header) + lduw_be_p(ip + 4);
    actual_end = MIN(available, declared_end);
    cursor = sizeof(struct ip6_header);
    protocol = ip[6];

    for (;;) {
        size_t extension_len;

        switch (protocol) {
        case IP6_HOP_BY_HOP:
        case IP6_DESTINATON:
            if (declared_end - cursor < sizeof(struct ip6_ext_hdr) ||
                actual_end - cursor < sizeof(struct ip6_ext_hdr)) {
                return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
            }
            extension_len = ((size_t)ip[cursor + 1] + 1) *
                            IP6_EXT_GRANULARITY;
            if (extension_len > declared_end - cursor ||
                extension_len > actual_end - cursor) {
                return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
            }
            protocol = ip[cursor];
            cursor += extension_len;
            break;
        case IP6_ROUTING:
        case IP6_MOBILITY:
            if (declared_end - cursor < sizeof(struct ip6_ext_hdr) ||
                actual_end - cursor < sizeof(struct ip6_ext_hdr)) {
                return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
            }
            extension_len = ((size_t)ip[cursor + 1] + 1) *
                            IP6_EXT_GRANULARITY;
            if (extension_len > declared_end - cursor ||
                extension_len > actual_end - cursor) {
                return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
            }
            return 0;
        case IP6_FRAGMENT:
            if (declared_end - cursor < IP6_EXT_GRANULARITY ||
                actual_end - cursor < IP6_EXT_GRANULARITY) {
                return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
            }
            return 0;
        case IP6_AUTHENTICATION:
            if (declared_end - cursor < sizeof(struct ip6_ext_hdr) ||
                actual_end - cursor < sizeof(struct ip6_ext_hdr)) {
                return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
            }
            extension_len = ((size_t)ip[cursor + 1] + 2) * 4;
            if (extension_len > declared_end - cursor ||
                extension_len > actual_end - cursor) {
                return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
            }
            return 0;
        case IP6_ESP:
            if (declared_end - cursor < IP6_EXT_GRANULARITY ||
                actual_end - cursor < IP6_EXT_GRANULARITY) {
                return TX_DESC_TDES0_IP_HEAD_ERR_MASK;
            }
            return 0;
        case IP6_NONE:
            return 0;
        default:
            return gmac_tx_coe_insert_l4(
                ip, true, ip + cursor, actual_end - cursor,
                declared_end - cursor, protocol, cic);
        }
    }
}

static uint32_t gmac_tx_coe_insert(DWGMACState *gmac, uint8_t *buf,
                                   size_t len, unsigned int cic)
{
    uint16_t protocol;
    size_t offset;
    uint32_t status;

    if (cic == TX_DESC_CIC_BYPASS ||
        !(gmac->hw_feature & DWMAC_DMA_HW_FEATURE_TX_COE) ||
        !(gmac->regs[R_DWMAC_DMA_CONTROL] &
          DWMAC_DMA_CONTROL_TX_STORE_FORWARD) ||
        !gmac_l3_protocol(gmac, buf, len, &protocol, &offset)) {
        return 0;
    }

    if (protocol == ETH_P_IP) {
        status = gmac_tx_coe_insert_ip4(buf, len, offset, cic);
    } else if (protocol == ETH_P_IPV6) {
        status = gmac_tx_coe_insert_ip6(buf, len, offset, cic);
    } else {
        return 0;
    }

    return status ? gmac_tx_coe_error(gmac, status) : 0;
}

static void gmac_tx_frame_reserve(DWGMACState *gmac, uint32_t needed)
{
    if (needed > gmac->tx_frame_capacity) {
        gmac->tx_frame = g_realloc(gmac->tx_frame, needed);
        gmac->tx_frame_capacity = needed;
    }
}

/* Abandon a partially assembled frame without disturbing the descriptor ring. */
static void gmac_tx_frame_discard(DWGMACState *gmac)
{
    gmac->tx_frame_len = 0;
    gmac->tx_frame_cic = TX_DESC_CIC_BYPASS;
}

static void gmac_try_send_next_packet(DWGMACState *gmac)
{
    uint32_t desc_addr;
    struct DWGMACTxDesc tx_desc;
    uint32_t tx_buf_addr, tx_buf_len;
    uint32_t descriptors = 0;

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
            /* A first segment starts a new frame and drops any stale one. */
            gmac->tx_frame_len = 0;
            gmac->tx_frame_cic = gmac_tx_checksum_control(gmac, &tx_desc);
        }
        /* step 4 */
        tx_buf_addr = tx_desc.tdes2;
        gmac->regs[R_DWMAC_DMA_CUR_TX_BUF_ADDR] = tx_buf_addr;
        tx_buf_len = gmac_tx_buffer1_size(gmac, &tx_desc);

        if ((uint64_t)gmac->tx_frame_len + tx_buf_len > UINT16_MAX) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: TX frame exceeds 65535 bytes\n",
                          DEVICE(gmac)->canonical_path);
            gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_UNF;
            gmac_tx_frame_discard(gmac);
            gmac_update_irq(gmac);
            return;
        }
        gmac_tx_frame_reserve(gmac, gmac->tx_frame_len + tx_buf_len);

        /* step 5 */
        if (tx_buf_len &&
            dma_memory_read(&address_space_memory, tx_buf_addr,
                            gmac->tx_frame + gmac->tx_frame_len, tx_buf_len,
                            MEMTXATTRS_UNSPECIFIED)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read packet @ 0x%x\n",
                        __func__, tx_buf_addr);
            gmac_tx_frame_discard(gmac);
            gmac_dma_bus_error(gmac,
                               DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT);
            return;
        }
        gmac->tx_frame_len += tx_buf_len;

        /* If not chained we'll have a second buffer. */
        if (!gmac_tx_is_chained(gmac, &tx_desc)) {
            tx_buf_addr = tx_desc.tdes3;
            gmac->regs[R_DWMAC_DMA_CUR_TX_BUF_ADDR] = tx_buf_addr;
            tx_buf_len = gmac_tx_buffer2_size(gmac, &tx_desc);

            if ((uint64_t)gmac->tx_frame_len + tx_buf_len > UINT16_MAX) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: TX frame exceeds 65535 bytes\n",
                              DEVICE(gmac)->canonical_path);
                gmac->regs[R_DWMAC_DMA_STATUS] |= DWMAC_DMA_STATUS_UNF;
                gmac_tx_frame_discard(gmac);
                gmac_update_irq(gmac);
                return;
            }
            gmac_tx_frame_reserve(gmac, gmac->tx_frame_len + tx_buf_len);

            if (tx_buf_len &&
                dma_memory_read(&address_space_memory, tx_buf_addr,
                                gmac->tx_frame + gmac->tx_frame_len,
                                tx_buf_len, MEMTXATTRS_UNSPECIFIED)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Failed to read packet @ 0x%x\n",
                              __func__, tx_buf_addr);
                gmac_tx_frame_discard(gmac);
                gmac_dma_bus_error(gmac,
                                   DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT);
                return;
            }
            gmac->tx_frame_len += tx_buf_len;
        }
        tx_desc.tdes0 &= ~(TX_DESC_TDES0_IP_HEAD_ERR_MASK |
                           TX_DESC_TDES0_PYLD_CHKSM_ERR_MASK |
                           TX_DESC_TDES0_ERR_SUMM_MASK);
        if (gmac_tx_is_last(gmac, &tx_desc)) {
            uint16_t length = gmac->tx_frame_len;

            tx_desc.tdes0 |= gmac_tx_coe_insert(gmac, gmac->tx_frame,
                                                length, gmac->tx_frame_cic);
            qemu_send_packet(qemu_get_queue(gmac->nic), gmac->tx_frame, length);
            trace_dw_gmac_packet_sent(DEVICE(gmac)->canonical_path, length);
            gmac_tx_frame_discard(gmac);
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
    gmac_update_phy_link(gmac);
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
        gmac_rx_drain(gmac);
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
            gmac_rx_drain(gmac);
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

    case A_DWMAC_DMA_RX_WATCHDOG:
        gmac->regs[offset / sizeof(uint32_t)] =
            v & DWMAC_DMA_RX_WATCHDOG_RIWT_MASK;
        if (!gmac->regs[offset / sizeof(uint32_t)]) {
            timer_del(gmac->rx_watchdog_timer);
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
    dw_gmac_reset_phy(gmac);
    gmac_update_phy_link(gmac);
    dw_gmac_set_phy_irq(gmac, false);
    gmac_tx_frame_discard(gmac);
    gmac_update_irq(gmac);

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
    if (gmac->rx_coe_type2 &&
        (gmac->hw_feature & (DWMAC_DMA_HW_FEATURE_RX_COE_TYPE2 |
                             DWMAC_DMA_HW_FEATURE_ENH_DESC)) !=
        (DWMAC_DMA_HW_FEATURE_RX_COE_TYPE2 |
         DWMAC_DMA_HW_FEATURE_ENH_DESC)) {
        error_setg(errp, "rx-coe-type2 requires Type-2 RX checksum and "
                   "enhanced-descriptor hardware features");
        return;
    }

    gmac->rx_watchdog_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                            gmac_rx_watchdog_expired, gmac);

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
    dw_gmac_set_phy_irq(gmac, false);
}

static void dw_gmac_unrealize(DeviceState *dev)
{
    DWGMACState *gmac = DW_GMAC(dev);

    qemu_del_nic(gmac->nic);
    timer_free(gmac->rx_watchdog_timer);
    gmac->rx_watchdog_timer = NULL;
    g_free(gmac->tx_frame);
    gmac->tx_frame = NULL;
    gmac->tx_frame_capacity = 0;
    g_free(gmac->rx_fifo);
    gmac->rx_fifo = NULL;
    gmac->rx_fifo_capacity = 0;
}

static int dw_gmac_post_load(void *opaque, int version_id)
{
    DWGMACState *gmac = opaque;

    gmac->regs[R_DWMAC_DMA_RX_WATCHDOG] &=
        DWMAC_DMA_RX_WATCHDOG_RIWT_MASK;
    if (version_id < 2) {
        timer_del(gmac->rx_watchdog_timer);
    }
    if (version_id < 3) {
        gmac->phy_reset_asserted = false;
    }
    dw_gmac_set_phy_irq(gmac, false);
    gmac_sync_conf_mac(gmac);
    gmac_update_irq(gmac);
    return 0;
}

static bool dw_gmac_tx_frame_needed(void *opaque)
{
    DWGMACState *gmac = opaque;

    return gmac->tx_frame_len != 0;
}

static int dw_gmac_tx_frame_post_load(void *opaque, int version_id)
{
    DWGMACState *gmac = opaque;

    /*
     * VMSTATE_VBUFFER_ALLOC_UINT32 allocates exactly the migrated length, so
     * the host-side capacity has to match what was just allocated.
     */
    gmac->tx_frame_capacity = gmac->tx_frame_len;
    return 0;
}

static const VMStateField dw_gmac_tx_frame_fields[] = {
    VMSTATE_UINT32(tx_frame_len, DWGMACState),
    VMSTATE_UINT8(tx_frame_cic, DWGMACState),
    VMSTATE_VBUFFER_ALLOC_UINT32(tx_frame, DWGMACState, 0, NULL,
                                 tx_frame_len),
    VMSTATE_END_OF_LIST()
};

/*
 * Only a frame suspended between its first and terminal segment needs this,
 * which is why it is a subsection: an idle or mid-quiescent GMAC emits
 * nothing extra, and a stream without it simply has no partial frame.
 *
 * savevm requires a subsection name to be prefixed by its parent's name, and
 * the reusable core is registered under two identities, so each identity
 * carries its own description over the shared field table.
 */
static const VMStateDescription vmstate_dw_gmac_tx_frame = {
    .name = TYPE_DW_GMAC "/tx-frame",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = dw_gmac_tx_frame_needed,
    .post_load = dw_gmac_tx_frame_post_load,
    .fields = dw_gmac_tx_frame_fields,
};

static const VMStateDescription vmstate_npcm_gmac_tx_frame = {
    .name = TYPE_NPCM_GMAC "/tx-frame",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = dw_gmac_tx_frame_needed,
    .post_load = dw_gmac_tx_frame_post_load,
    .fields = dw_gmac_tx_frame_fields,
};

static bool dw_gmac_rx_fifo_needed(void *opaque)
{
    DWGMACState *gmac = opaque;

    return gmac->rx_fifo_used != 0;
}

static int dw_gmac_rx_fifo_post_load(void *opaque, int version_id)
{
    DWGMACState *gmac = opaque;

    gmac->rx_fifo_capacity = gmac->rx_fifo_used;
    if (!gmac_rx_fifo_valid(gmac)) {
        error_report("%s: corrupt receive FIFO image",
                     DEVICE(gmac)->canonical_path);
        return -EINVAL;
    }
    return 0;
}

static const VMStateField dw_gmac_rx_fifo_fields[] = {
    VMSTATE_UINT32(rx_fifo_used, DWGMACState),
    VMSTATE_VBUFFER_ALLOC_UINT32(rx_fifo, DWGMACState, 0, NULL,
                                 rx_fifo_used),
    VMSTATE_END_OF_LIST()
};

/* Emitted only while the DMA is holding frames it could not place yet. */
static const VMStateDescription vmstate_dw_gmac_rx_fifo = {
    .name = TYPE_DW_GMAC "/rx-fifo",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = dw_gmac_rx_fifo_needed,
    .post_load = dw_gmac_rx_fifo_post_load,
    .fields = dw_gmac_rx_fifo_fields,
};

static const VMStateDescription vmstate_npcm_gmac_rx_fifo = {
    .name = TYPE_NPCM_GMAC "/rx-fifo",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = dw_gmac_rx_fifo_needed,
    .post_load = dw_gmac_rx_fifo_post_load,
    .fields = dw_gmac_rx_fifo_fields,
};

static const VMStateDescription * const dw_gmac_vmstate_subsections[] = {
    &vmstate_dw_gmac_tx_frame,
    &vmstate_dw_gmac_rx_fifo,
    NULL
};

static const VMStateDescription * const npcm_gmac_vmstate_subsections[] = {
    &vmstate_npcm_gmac_tx_frame,
    &vmstate_npcm_gmac_rx_fifo,
    NULL
};

static const VMStateField dw_gmac_vmstate_fields[] = {
    VMSTATE_UINT32_ARRAY(regs, DWGMACState, DW_GMAC_NR_REGS),
    VMSTATE_UINT16_2DARRAY_V(phy_regs, DWGMACState, DW_GMAC_MAX_PHYS,
                             DW_GMAC_MAX_PHY_REGS, 1),
    VMSTATE_TIMER_PTR_V(rx_watchdog_timer, DWGMACState, 2),
    VMSTATE_BOOL_V(phy_reset_asserted, DWGMACState, 3),
    VMSTATE_END_OF_LIST()
};

static const VMStateDescription vmstate_dw_gmac = {
    .name = TYPE_DW_GMAC,
    .version_id = 3,
    .minimum_version_id = 0,
    .post_load = dw_gmac_post_load,
    .fields = dw_gmac_vmstate_fields,
    .subsections = dw_gmac_vmstate_subsections,
};

static const VMStateDescription vmstate_npcm_gmac = {
    .name = TYPE_NPCM_GMAC,
    .version_id = 1,
    .minimum_version_id = 0,
    .post_load = dw_gmac_post_load,
    .fields = dw_gmac_vmstate_fields,
    .subsections = npcm_gmac_vmstate_subsections,
};

static const Property dw_gmac_properties[] = {
    DEFINE_NIC_PROPERTIES(DWGMACState, conf),
    DEFINE_PROP_UINT32("version", DWGMACState, version, 0x1032),
    DEFINE_PROP_UINT32("hw-feature", DWGMACState, hw_feature, 0x100d4f37),
    DEFINE_PROP_UINT64("riwt-clock-frequency", DWGMACState,
                       rx_watchdog_clock_hz, 0),
    /* Preserve legacy users' accept-all behavior unless explicitly enabled. */
    DEFINE_PROP_BOOL("rx-filtering", DWGMACState, rx_filtering, false),
    /* Preserve legacy descriptor status unless explicitly enabled. */
    DEFINE_PROP_BOOL("rx-coe-type2", DWGMACState, rx_coe_type2, false),
    /* Receive FIFO depth in frame bytes; the TH1520 synthesis value is unknown. */
    DEFINE_PROP_UINT32("rx-fifo-size", DWGMACState, rx_fifo_size, 16384),
    DEFINE_PROP_UINT16("hash-bins", DWGMACState, hash_bins, 64),
    DEFINE_PROP_UINT8("num-mac-addresses", DWGMACState, num_mac_addrs, 4),
    DEFINE_PROP_UINT8("phy-addr", DWGMACState, phy_addr, 0),
    DEFINE_PROP_UINT16("phy-id1", DWGMACState, phy_id1, 0x0362),
    DEFINE_PROP_UINT16("phy-id2", DWGMACState, phy_id2, 0x5e6a),
};

static void dw_gmac_init(Object *obj)
{
    DWGMACState *gmac = DW_GMAC(obj);

    qdev_init_gpio_in_named(DEVICE(gmac), dw_gmac_phy_reset_input,
                            "phy-reset-n", 1);
    qdev_init_gpio_out_named(DEVICE(gmac), &gmac->phy_irq_n,
                             "phy-irq-n", 1);
}

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
        .instance_init = dw_gmac_init,
        .class_init = dw_gmac_class_init,
    },
    {
        .name = TYPE_NPCM_GMAC,
        .parent = TYPE_DW_GMAC,
        .class_init = npcm_gmac_class_init,
    },
};
DEFINE_TYPES(dw_gmac_types)
