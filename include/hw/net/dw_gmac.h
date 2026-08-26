/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Synopsys DesignWare GMAC 3.x
 *
 * Copyright 2024 Google LLC
 * Authors:
 * Hao Wu <wuhaotsh@google.com>
 * Nabih Estefan <nabihestefan@google.com>
 */

#ifndef DW_GMAC_H
#define DW_GMAC_H

#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "net/net.h"
#include "qemu/timer.h"

#define DW_GMAC_REG_SIZE 0x1060
#define DW_GMAC_NR_REGS (DW_GMAC_REG_SIZE / sizeof(uint32_t))

#define DW_GMAC_MAX_PHYS 32
#define DW_GMAC_MAX_PHY_REGS 32
#define DW_GMAC_MAX_MAC_ADDRS 32

struct DWGMACRxDesc {
    uint32_t rdes0;
    uint32_t rdes1;
    uint32_t rdes2;
    uint32_t rdes3;
};

/* DWGMACRxDesc.flags values */
/* RDES2 and RDES3 are buffer addresses */
/* Owner: 0 = software, 1 = dma */
#define RX_DESC_RDES0_OWN BIT(31)
/* Destination Address Filter Fail */
#define RX_DESC_RDES0_DEST_ADDR_FILT_FAIL BIT(30)
/* Frame length */
#define RX_DESC_RDES0_FRAME_LEN_MASK(word) extract32(word, 16, 14)
/* Frame length Shift*/
#define RX_DESC_RDES0_FRAME_LEN_SHIFT 16
/* Error Summary */
#define RX_DESC_RDES0_ERR_SUMM_MASK BIT(15)
/* Descriptor Error */
#define RX_DESC_RDES0_DESC_ERR_MASK BIT(14)
/* Source Address Filter Fail */
#define RX_DESC_RDES0_SRC_ADDR_FILT_FAIL_MASK BIT(13)
/* Length Error */
#define RX_DESC_RDES0_LEN_ERR_MASK BIT(12)
/* Overflow Error */
#define RX_DESC_RDES0_OVRFLW_ERR_MASK BIT(11)
/* VLAN Tag */
#define RX_DESC_RDES0_VLAN_TAG_MASK BIT(10)
/* First Descriptor */
#define RX_DESC_RDES0_FIRST_DESC_MASK BIT(9)
/* Last Descriptor */
#define RX_DESC_RDES0_LAST_DESC_MASK BIT(8)
/* IPC Checksum Error/Giant Frame */
#define RX_DESC_RDES0_IPC_CHKSM_ERR_GNT_FRM_MASK BIT(7)
/* Late Collision */
#define RX_DESC_RDES0_LT_COLL_MASK BIT(6)
/* Frame Type */
#define RX_DESC_RDES0_FRM_TYPE_MASK BIT(5)
/* Receive Watchdog Timeout */
#define RX_DESC_RDES0_REC_WTCHDG_TMT_MASK BIT(4)
/* Receive Error */
#define RX_DESC_RDES0_RCV_ERR_MASK BIT(3)
/* Dribble Bit Error */
#define RX_DESC_RDES0_DRBL_BIT_ERR_MASK BIT(2)
/* Cyclcic Redundancy Check Error */
#define RX_DESC_RDES0_CRC_ERR_MASK BIT(1)
/* Extended Status Available */
#define RX_DESC_RDES0_EXT_STATUS_AVAIL_MASK BIT(0)

/* Enhanced receive descriptor word 4 */
#define RX_DESC_RDES4_PAYLOAD_UNKNOWN 0
#define RX_DESC_RDES4_PAYLOAD_UDP 1
#define RX_DESC_RDES4_PAYLOAD_TCP 2
#define RX_DESC_RDES4_PAYLOAD_ICMP 3
#define RX_DESC_RDES4_IP_HEADER_ERR BIT(3)
#define RX_DESC_RDES4_IP_PAYLOAD_ERR BIT(4)
#define RX_DESC_RDES4_IP_CSUM_BYPASSED BIT(5)
#define RX_DESC_RDES4_IPV4_PACKET BIT(6)
#define RX_DESC_RDES4_IPV6_PACKET BIT(7)

/* Disable Interrupt on Completion */
#define RX_DESC_RDES1_DIS_INTR_COMP_MASK BIT(31)
/* Receive end of ring */
#define RX_DESC_RDES1_RC_END_RING_MASK BIT(25)
/* Second Address Chained */
#define RX_DESC_RDES1_SEC_ADDR_CHND_MASK BIT(24)
/* Receive Buffer 2 Size */
#define RX_DESC_RDES1_BFFR2_SZ_SHIFT 11
#define RX_DESC_RDES1_BFFR2_SZ_MASK(word) extract32(word, \
    RX_DESC_RDES1_BFFR2_SZ_SHIFT, 11)
/* Receive Buffer 1 Size */
#define RX_DESC_RDES1_BFFR1_SZ_MASK(word) extract32(word, 0, 11)


struct DWGMACTxDesc {
    uint32_t tdes0;
    uint32_t tdes1;
    uint32_t tdes2;
    uint32_t tdes3;
};

/* DWGMACTxDesc.flags values */
/* TDES2 and TDES3 are buffer addresses */
/* Owner: 0 = software, 1 = gmac */
#define TX_DESC_TDES0_OWN BIT(31)
/* Tx Time Stamp Status */
#define TX_DESC_TDES0_TTSS_MASK BIT(17)
/* IP Header Error */
#define TX_DESC_TDES0_IP_HEAD_ERR_MASK BIT(16)
/* Error Summary */
#define TX_DESC_TDES0_ERR_SUMM_MASK BIT(15)
/* Jabber Timeout */
#define TX_DESC_TDES0_JBBR_TMT_MASK BIT(14)
/* Frame Flushed */
#define TX_DESC_TDES0_FRM_FLSHD_MASK BIT(13)
/* Payload Checksum Error */
#define TX_DESC_TDES0_PYLD_CHKSM_ERR_MASK BIT(12)
/* Loss of Carrier */
#define TX_DESC_TDES0_LSS_CARR_MASK BIT(11)
/* No Carrier */
#define TX_DESC_TDES0_NO_CARR_MASK BIT(10)
/* Late Collision */
#define TX_DESC_TDES0_LATE_COLL_MASK BIT(9)
/* Excessive Collision */
#define TX_DESC_TDES0_EXCS_COLL_MASK BIT(8)
/* VLAN Frame */
#define TX_DESC_TDES0_VLAN_FRM_MASK BIT(7)
/* Collision Count */
#define TX_DESC_TDES0_COLL_CNT_MASK(word) extract32(word, 3, 4)
/* Excessive Deferral */
#define TX_DESC_TDES0_EXCS_DEF_MASK BIT(2)
/* Underflow Error */
#define TX_DESC_TDES0_UNDRFLW_ERR_MASK BIT(1)
/* Deferred Bit */
#define TX_DESC_TDES0_DFRD_BIT_MASK BIT(0)

/* Interrupt of Completion */
#define TX_DESC_TDES1_INTERR_COMP_MASK BIT(31)
/* Last Segment */
#define TX_DESC_TDES1_LAST_SEG_MASK BIT(30)
/* First Segment */
#define TX_DESC_TDES1_FIRST_SEG_MASK BIT(29)
/* Checksum Insertion Control */
#define TX_DESC_TDES1_CHKSM_INS_CTRL_MASK(word) extract32(word, 27, 2)
#define TX_DESC_CIC_BYPASS 0
#define TX_DESC_CIC_IP_HEADER 1
#define TX_DESC_CIC_PAYLOAD_PARTIAL 2
#define TX_DESC_CIC_PAYLOAD_FULL 3
/* Disable Cyclic Redundancy Check */
#define TX_DESC_TDES1_DIS_CDC_MASK BIT(26)
/* Transmit End of Ring */
#define TX_DESC_TDES1_TX_END_RING_MASK BIT(25)
/* Secondary Address Chained */
#define TX_DESC_TDES1_SEC_ADDR_CHND_MASK BIT(24)
/* Transmit Buffer 2 Size */
#define TX_DESC_TDES1_BFFR2_SZ_MASK(word) extract32(word, 11, 11)
/* Transmit Buffer 1 Size */
#define TX_DESC_TDES1_BFFR1_SZ_MASK(word) extract32(word, 0, 11)

typedef struct DWGMACState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;
    /* Optional external PHY interrupt; its electrical level is active-low. */
    qemu_irq phy_irq_n;

    NICState *nic;
    NICConf conf;

    uint32_t regs[DW_GMAC_NR_REGS];
    uint16_t phy_regs[DW_GMAC_MAX_PHYS][DW_GMAC_MAX_PHY_REGS];
    QEMUTimer *rx_watchdog_timer;
    /* Optional external PHY reset input; its electrical level is active-low. */
    bool phy_reset_asserted;

    uint32_t version;
    uint32_t hw_feature;
    uint64_t rx_watchdog_clock_hz;
    bool rx_filtering;
    bool rx_coe_type2;
    uint16_t hash_bins;
    uint8_t num_mac_addrs;
    uint8_t phy_addr;
    uint16_t phy_id1;
    uint16_t phy_id2;
} DWGMACState;

#define TYPE_DW_GMAC "dw-gmac"
OBJECT_DECLARE_SIMPLE_TYPE(DWGMACState, DW_GMAC)

/* Compatibility type used by the Nuvoton NPCM7xx/NPCM8xx machines. */
#define TYPE_NPCM_GMAC "npcm-gmac"
typedef DWGMACState NPCMGMACState;

/* Mask for RO bits in Status */
#define DWMAC_DMA_STATUS_RO_MASK(word) (word & 0xfffe0000)
/* Mask for RO bits in Status */
#define DWMAC_DMA_STATUS_W1C_MASK(word) (word & 0x1e7ff)

/* Transmit Process State */
#define DWMAC_DMA_STATUS_TX_PROCESS_STATE_SHIFT 20
/* Transmit States */
#define DWMAC_DMA_STATUS_TX_STOPPED_STATE \
    (0b000)
#define DWMAC_DMA_STATUS_TX_RUNNING_FETCHING_STATE \
    (0b001)
#define DWMAC_DMA_STATUS_TX_RUNNING_WAITING_STATE \
    (0b010)
#define DWMAC_DMA_STATUS_TX_RUNNING_READ_STATE \
    (0b011)
#define DWMAC_DMA_STATUS_TX_SUSPENDED_STATE \
    (0b110)
#define DWMAC_DMA_STATUS_TX_RUNNING_CLOSING_STATE \
    (0b111)
/* Transmit Process State */
#define DWMAC_DMA_STATUS_RX_PROCESS_STATE_SHIFT 17
/* Receive States */
#define DWMAC_DMA_STATUS_RX_STOPPED_STATE \
    (0b000)
#define DWMAC_DMA_STATUS_RX_RUNNING_FETCHING_STATE \
    (0b001)
#define DWMAC_DMA_STATUS_RX_RUNNING_WAITING_STATE \
    (0b011)
#define DWMAC_DMA_STATUS_RX_SUSPENDED_STATE \
    (0b100)
#define DWMAC_DMA_STATUS_RX_RUNNING_CLOSING_STATE \
    (0b101)
#define DWMAC_DMA_STATUS_RX_RUNNING_TRANSFERRING_STATE \
    (0b111)


/* Early Receive Interrupt */
#define DWMAC_DMA_STATUS_ERI BIT(14)
/* Fatal Bus Error Interrupt */
#define DWMAC_DMA_STATUS_FBI BIT(13)
/* Early transmit Interrupt */
#define DWMAC_DMA_STATUS_ETI BIT(10)
/* Receive Watchdog Timeout */
#define DWMAC_DMA_STATUS_RWT BIT(9)
/* Receive Process Stopped */
#define DWMAC_DMA_STATUS_RPS BIT(8)
/* Receive Buffer Unavailable */
#define DWMAC_DMA_STATUS_RU BIT(7)
/* Receive Interrupt */
#define DWMAC_DMA_STATUS_RI BIT(6)
/* Transmit Underflow */
#define DWMAC_DMA_STATUS_UNF BIT(5)
/* Receive Overflow */
#define DWMAC_DMA_STATUS_OVF BIT(4)
/* Transmit Jabber Timeout */
#define DWMAC_DMA_STATUS_TJT BIT(3)
/* Transmit Buffer Unavailable */
#define DWMAC_DMA_STATUS_TU BIT(2)
/* Transmit Process Stopped */
#define DWMAC_DMA_STATUS_TPS BIT(1)
/* Transmit Interrupt */
#define DWMAC_DMA_STATUS_TI BIT(0)

/* Normal Interrupt Summary */
#define DWMAC_DMA_STATUS_NIS BIT(16)
/* Interrupts enabled by NIE */
#define DWMAC_DMA_STATUS_NIS_BITS (DWMAC_DMA_STATUS_TI | \
                                  DWMAC_DMA_STATUS_TU | \
                                  DWMAC_DMA_STATUS_RI | \
                                  DWMAC_DMA_STATUS_ERI)
/* Abnormal Interrupt Summary */
#define DWMAC_DMA_STATUS_AIS BIT(15)
/* Interrupts enabled by AIE */
#define DWMAC_DMA_STATUS_AIS_BITS (DWMAC_DMA_STATUS_TPS | \
                                  DWMAC_DMA_STATUS_TJT | \
                                  DWMAC_DMA_STATUS_OVF | \
                                  DWMAC_DMA_STATUS_UNF | \
                                  DWMAC_DMA_STATUS_RU  | \
                                  DWMAC_DMA_STATUS_RPS | \
                                  DWMAC_DMA_STATUS_RWT | \
                                  DWMAC_DMA_STATUS_ETI | \
                                  DWMAC_DMA_STATUS_FBI)

/* Early Receive Interrupt Enable */
#define DWMAC_DMA_INTR_ENAB_ERE BIT(14)
/* Fatal Bus Error Interrupt Enable */
#define DWMAC_DMA_INTR_ENAB_FBE BIT(13)
/* Early transmit Interrupt Enable */
#define DWMAC_DMA_INTR_ENAB_ETE BIT(10)
/* Receive Watchdog Timout Enable */
#define DWMAC_DMA_INTR_ENAB_RWE BIT(9)
/* Receive Process Stopped Enable */
#define DWMAC_DMA_INTR_ENAB_RSE BIT(8)
/* Receive Buffer Unavailable Enable */
#define DWMAC_DMA_INTR_ENAB_RUE BIT(7)
/* Receive Interrupt Enable */
#define DWMAC_DMA_INTR_ENAB_RIE BIT(6)
/* Transmit Underflow Enable */
#define DWMAC_DMA_INTR_ENAB_UNE BIT(5)
/* Receive Overflow Enable */
#define DWMAC_DMA_INTR_ENAB_OVE BIT(4)
/* Transmit Jabber Timeout Enable */
#define DWMAC_DMA_INTR_ENAB_TJE BIT(3)
/* Transmit Buffer Unavailable Enable */
#define DWMAC_DMA_INTR_ENAB_TUE BIT(2)
/* Transmit Process Stopped Enable */
#define DWMAC_DMA_INTR_ENAB_TSE BIT(1)
/* Transmit Interrupt Enable */
#define DWMAC_DMA_INTR_ENAB_TIE BIT(0)

/* Normal Interrupt Summary Enable */
#define DWMAC_DMA_INTR_ENAB_NIE BIT(16)
/* Interrupts enabled by NIE Enable */
#define DWMAC_DMA_INTR_ENAB_NIE_BITS (DWMAC_DMA_INTR_ENAB_TIE | \
                                     DWMAC_DMA_INTR_ENAB_TUE | \
                                     DWMAC_DMA_INTR_ENAB_RIE | \
                                     DWMAC_DMA_INTR_ENAB_ERE)
/* Abnormal Interrupt Summary Enable */
#define DWMAC_DMA_INTR_ENAB_AIE BIT(15)
/* Interrupts enabled by AIE Enable */
#define DWMAC_DMA_INTR_ENAB_AIE_BITS (DWMAC_DMA_INTR_ENAB_TSE | \
                                     DWMAC_DMA_INTR_ENAB_TJE | \
                                     DWMAC_DMA_INTR_ENAB_OVE | \
                                     DWMAC_DMA_INTR_ENAB_UNE | \
                                     DWMAC_DMA_INTR_ENAB_RUE | \
                                     DWMAC_DMA_INTR_ENAB_RSE | \
                                     DWMAC_DMA_INTR_ENAB_RWE | \
                                     DWMAC_DMA_INTR_ENAB_ETE | \
                                     DWMAC_DMA_INTR_ENAB_FBE)

/* Flushing Disabled */
#define DWMAC_DMA_CONTROL_FLUSH_MASK BIT(24)
/* Transmit store and forward */
#define DWMAC_DMA_CONTROL_TX_STORE_FORWARD BIT(21)
/* Start/stop Transmit */
#define DWMAC_DMA_CONTROL_START_STOP_TX BIT(13)
/* Start/stop Receive */
#define DWMAC_DMA_CONTROL_START_STOP_RX BIT(1)
/* Next receive descriptor start address */
#define DWMAC_DMA_HOST_RX_DESC_MASK(word) ((uint32_t) (word) & ~3u)
/* Next transmit descriptor start address */
#define DWMAC_DMA_HOST_TX_DESC_MASK(word) ((uint32_t) (word) & ~3u)

/* Receive enable */
#define DW_GMAC_MAC_CONFIG_RX_EN BIT(2)
/* Transmit enable */
#define DW_GMAC_MAC_CONFIG_TX_EN BIT(3)

/* Frame Receive All */
#define DW_GMAC_FRAME_FILTER_REC_ALL_MASK BIT(31)
/* VLAN Tag Filter Enable */
#define DW_GMAC_FRAME_FILTER_VTFE_MASK BIT(16)
/* Frame HPF Filter*/
#define DW_GMAC_FRAME_FILTER_HPF_MASK BIT(10)
/* Frame SAF Filter*/
#define DW_GMAC_FRAME_FILTER_SAF_MASK BIT(9)
/* Frame SAIF Filter*/
#define DW_GMAC_FRAME_FILTER_SAIF_MASK BIT(8)
/* Frame PCF Filter*/
#define DW_GMAC_FRAME_FILTER_PCF_MASK(word) extract32((word), 6, 2)
/* Frame DBF Filter*/
#define DW_GMAC_FRAME_FILTER_DBF_MASK BIT(5)
/* Frame PM Filter*/
#define DW_GMAC_FRAME_FILTER_PM_MASK BIT(4)
/* Frame DAIF Filter*/
#define DW_GMAC_FRAME_FILTER_DAIF_MASK BIT(3)
/* Frame HMC Filter*/
#define DW_GMAC_FRAME_FILTER_HMC_MASK BIT(2)
/* Frame HUC Filter*/
#define DW_GMAC_FRAME_FILTER_HUC_MASK BIT(1)
/* Frame PR Filter*/
#define DW_GMAC_FRAME_FILTER_PR_MASK BIT(0)

/* VLAN Tag register */
#define DW_GMAC_VLAN_TAG_ESVL_MASK BIT(18)
#define DW_GMAC_VLAN_TAG_VTIM_MASK BIT(17)
#define DW_GMAC_VLAN_TAG_ETV_MASK BIT(16)
#define DW_GMAC_VLAN_TAG_VL_MASK(word) extract32((word), 0, 16)

#endif /* DW_GMAC_H */
