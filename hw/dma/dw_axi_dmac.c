/*
 * Synopsys DesignWare AXI DMA Controller
 *
 * The register layout implemented here is the <= 8 channel map used by
 * DW_axi_dmac 1.01a.  Transfers are deliberately completed synchronously:
 * QEMU models the software-visible ordering and error/interrupt state, not
 * AXI arbitration latency.
 *
 * The initial functional subset executes direct and linked-list
 * memory-to-memory transfers.  Peripheral handshakes, contiguous/reload/
 * shadow multi-block modes, dynamic LLI extension, and mid-transfer
 * suspend/abort timing require SoC wiring or physical validation and are not
 * claimed here.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/dma/dw_axi_dmac.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "system/dma.h"

#define DW_AXI_DMAC_MMIO_SIZE          0x1000
#define DW_AXI_DMAC_COMMON_SIZE        0x100
#define DW_AXI_DMAC_CHANNEL_SIZE       0x100

#define DMAC_ID                        0x000
#define DMAC_COMPONENT_VERSION         0x008
#define DMAC_CFG                       0x010
#define DMAC_CHEN                      0x018
#define DMAC_INTSTATUS                 0x030
#define DMAC_COMMON_INTCLEAR           0x038
#define DMAC_COMMON_INTSTATUS_ENABLE   0x040
#define DMAC_COMMON_INTSIGNAL_ENABLE   0x048
#define DMAC_COMMON_INTSTATUS          0x050
#define DMAC_RESET                     0x058
#define DMAC_LOWPOWER_CFG              0x060

#define DMAC_CFG_ENABLE                BIT(0)
#define DMAC_CFG_INTERRUPT_ENABLE      BIT(1)

#define DMAC_CH_ENABLE_SHIFT           0
#define DMAC_CH_ENABLE_WE_SHIFT        8
#define DMAC_CH_SUSPEND_SHIFT          16
#define DMAC_CH_SUSPEND_WE_SHIFT       24
#define DMAC_CH_ABORT_SHIFT            32
#define DMAC_CH_ABORT_WE_SHIFT         40

#define CH_SAR                         0x00
#define CH_DAR                         0x08
#define CH_BLOCK_TS                    0x10
#define CH_CTL                         0x18
#define CH_CFG                         0x20
#define CH_LLP                         0x28
#define CH_STATUS                      0x30
#define CH_SW_HS_SRC                   0x38
#define CH_SW_HS_DST                   0x40
#define CH_BLOCK_TRANSFER_RESUME       0x48
#define CH_AXI_ID                      0x50
#define CH_AXI_QOS                     0x58
#define CH_SSTAT                       0x60
#define CH_DSTAT                       0x68
#define CH_SSTAT_ADDR                  0x70
#define CH_DSTAT_ADDR                  0x78
#define CH_INTSTATUS_ENABLE            0x80
#define CH_INTSTATUS                   0x88
#define CH_INTSIGNAL_ENABLE            0x90
#define CH_INTCLEAR                    0x98

#define CH_BLOCK_TS_MASK               MAKE_64BIT_MASK(0, 22)
#define CH_CTL_SRC_WIDTH_SHIFT         8
#define CH_CTL_DST_WIDTH_SHIFT         11
#define CH_CTL_SRC_INCREMENT_SHIFT     4
#define CH_CTL_DST_INCREMENT_SHIFT     6
#define CH_CTL_INCREMENT_MASK          0x3
#define CH_CTL_LLI_LAST                BIT_ULL(62)
#define CH_CTL_LLI_VALID               BIT_ULL(63)
#define CH_CFG_SRC_MULTIBLOCK_SHIFT    0
#define CH_CFG_DST_MULTIBLOCK_SHIFT    2
#define CH_CFG_MULTIBLOCK_MASK         0x3
#define CH_CFG_MULTIBLOCK_LINKED_LIST  0x3
#define CH_CFG_TT_FC_SHIFT             32
#define CH_CFG_TT_FC_MASK              0x7
#define CH_CFG_TT_FC_MEM_TO_MEM        0

#define CH_IRQ_BLOCK_TRANSFER          BIT(0)
#define CH_IRQ_DMA_TRANSFER            BIT(1)
#define CH_IRQ_SRC_DECODE_ERROR        BIT(5)
#define CH_IRQ_DST_DECODE_ERROR        BIT(6)
#define CH_IRQ_LLI_READ_DECODE_ERROR   BIT(9)
#define CH_IRQ_LLI_WRITE_DECODE_ERROR  BIT(10)
#define CH_IRQ_INVALID_ERROR           BIT(13)
#define CH_IRQ_MULTIBLOCK_ERROR        BIT(14)
#define CH_IRQ_SUSPENDED               BIT(29)
#define CH_IRQ_DISABLED                BIT(30)
#define CH_IRQ_ABORTED                 BIT(31)
#define CH_IRQ_VALID_MASK              0xf83f7ffbU

#define COMMON_IRQ_VALID_MASK          0x10fU
#define DW_AXI_DMAC_COPY_CHUNK         4096
#define DW_AXI_DMAC_MAX_LLI_BLOCKS     4096

typedef struct QEMU_PACKED DWAxiDMACLLI {
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
} DWAxiDMACLLI;

QEMU_BUILD_BUG_ON(sizeof(DWAxiDMACLLI) != 64);

static uint64_t dw_axi_dmac_lane_mask(hwaddr offset, unsigned size)
{
    return MAKE_64BIT_MASK((offset & 7) * 8, size * 8);
}

static uint64_t dw_axi_dmac_merge(uint64_t old, uint64_t value,
                                  hwaddr offset, unsigned size)
{
    unsigned shift = (offset & 7) * 8;
    uint64_t mask = dw_axi_dmac_lane_mask(offset, size);

    return (old & ~mask) | ((value << shift) & mask);
}

static uint64_t dw_axi_dmac_extract(uint64_t value, hwaddr offset,
                                    unsigned size)
{
    return extract64(value, (offset & 7) * 8, size * 8);
}

static uint32_t dw_axi_dmac_combined_status(DWAxiDMACState *s)
{
    uint32_t status = 0;

    for (unsigned i = 0; i < s->num_channels; i++) {
        DWAxiDMACChannel *ch = &s->channel[i];

        if (ch->int_status & ch->int_signal_enable) {
            status |= BIT(i);
        }
    }

    if (s->common_int_status & s->common_int_signal_enable) {
        status |= BIT(16);
    }

    return status;
}

static void dw_axi_dmac_update_irq(DWAxiDMACState *s)
{
    bool level = (s->cfg & DMAC_CFG_INTERRUPT_ENABLE) &&
                 dw_axi_dmac_combined_status(s);

    qemu_set_irq(s->irq, level);
}

static void dw_axi_dmac_raise_channel(DWAxiDMACState *s, unsigned channel,
                                      uint32_t events)
{
    DWAxiDMACChannel *ch = &s->channel[channel];

    ch->int_status |= events & ch->int_status_enable & CH_IRQ_VALID_MASK;
    dw_axi_dmac_update_irq(s);
}

static void dw_axi_dmac_reset_state(DWAxiDMACState *s)
{
    s->cfg = 0;
    s->low_power_cfg = 0;
    s->common_int_status_enable = 0;
    s->common_int_status = 0;
    s->common_int_signal_enable = 0;
    memset(s->channel, 0, sizeof(s->channel));
    qemu_set_irq(s->irq, 0);
}

static void dw_axi_dmac_reset(DeviceState *dev)
{
    dw_axi_dmac_reset_state(DW_AXI_DMAC(dev));
}

static uint32_t dw_axi_dmac_copy_block(DWAxiDMACState *s,
                                       DWAxiDMACChannel *ch)
{
    uint64_t transfers = (ch->block_ts & CH_BLOCK_TS_MASK) + 1;
    unsigned source_width_code =
        extract64(ch->ctl, CH_CTL_SRC_WIDTH_SHIFT, 3);
    unsigned destination_width_code =
        extract64(ch->ctl, CH_CTL_DST_WIDTH_SHIFT, 3);
    unsigned source_increment =
        extract64(ch->ctl, CH_CTL_SRC_INCREMENT_SHIFT, 2);
    unsigned destination_increment =
        extract64(ch->ctl, CH_CTL_DST_INCREMENT_SHIFT, 2);
    size_t source_width;
    size_t destination_width;
    uint64_t total_bytes;
    uint64_t source = ch->sar;
    uint64_t destination = ch->dar;
    uint8_t buffer[DW_AXI_DMAC_COPY_CHUNK];

    if (transfers > s->block_size ||
        source_width_code > s->data_width ||
        destination_width_code > s->data_width ||
        source_increment > 1 || destination_increment > 1) {
        return CH_IRQ_INVALID_ERROR;
    }

    source_width = 1U << source_width_code;
    destination_width = 1U << destination_width_code;
    if (transfers > UINT64_MAX / source_width) {
        return CH_IRQ_INVALID_ERROR;
    }
    total_bytes = transfers * source_width;

    if (total_bytes % destination_width) {
        return CH_IRQ_INVALID_ERROR;
    }

    if (!source_increment && !destination_increment &&
        source_width == destination_width) {
        uint64_t done = 0;

        while (done < total_bytes) {
            size_t length = MIN(total_bytes - done,
                                (uint64_t)sizeof(buffer));

            length -= length % source_width;
            if (dma_memory_read(&address_space_memory, source, buffer,
                                length, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
                return CH_IRQ_SRC_DECODE_ERROR;
            }
            if (dma_memory_write(&address_space_memory, destination, buffer,
                                 length, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
                return CH_IRQ_DST_DECODE_ERROR;
            }
            source += length;
            destination += length;
            done += length;
        }
    } else {
        uint8_t fifo[128];
        size_t fifo_bytes = 0;
        uint64_t remaining = total_bytes;

        while (remaining) {
            if (dma_memory_read(&address_space_memory, source,
                                fifo + fifo_bytes, source_width,
                                MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
                return CH_IRQ_SRC_DECODE_ERROR;
            }
            fifo_bytes += source_width;
            remaining -= source_width;
            if (!source_increment) {
                source += source_width;
            }

            while (fifo_bytes >= destination_width) {
                if (dma_memory_write(&address_space_memory, destination,
                                     fifo, destination_width,
                                     MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
                    return CH_IRQ_DST_DECODE_ERROR;
                }
                if (!destination_increment) {
                    destination += destination_width;
                }
                fifo_bytes -= destination_width;
                memmove(fifo, fifo + destination_width, fifo_bytes);
            }
        }
        g_assert(fifo_bytes == 0);
    }

    ch->sar = source;
    ch->dar = destination;
    ch->status = ch->block_ts & CH_BLOCK_TS_MASK;
    return 0;
}

static bool dw_axi_dmac_writeback_lli(uint64_t address, uint32_t ctl_high,
                                      uint32_t status)
{
    uint32_t value = cpu_to_le32(ctl_high & ~BIT(31));

    if (dma_memory_write(&address_space_memory,
                         address + offsetof(DWAxiDMACLLI, ctl_high),
                         &value, sizeof(value),
                         MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        return false;
    }

    value = cpu_to_le32(status);
    return dma_memory_write(&address_space_memory,
                            address + offsetof(DWAxiDMACLLI, status_low),
                            &value, sizeof(value),
                            MEMTXATTRS_UNSPECIFIED) == MEMTX_OK;
}

static void dw_axi_dmac_fail_channel(DWAxiDMACState *s, unsigned channel,
                                     uint32_t error)
{
    DWAxiDMACChannel *ch = &s->channel[channel];

    ch->enabled = false;
    ch->suspended = false;
    dw_axi_dmac_raise_channel(s, channel, error);
}

static void dw_axi_dmac_run_linked_list(DWAxiDMACState *s,
                                        unsigned channel)
{
    DWAxiDMACChannel *ch = &s->channel[channel];
    uint64_t next = ch->llp;

    for (unsigned block = 0; block < DW_AXI_DMAC_MAX_LLI_BLOCKS; block++) {
        DWAxiDMACLLI lli;
        uint64_t address;
        uint32_t ctl_high;
        uint32_t event;

        if (!next || (next & 0x3e)) {
            dw_axi_dmac_fail_channel(s, channel, CH_IRQ_INVALID_ERROR);
            return;
        }

        address = next & ~0x3fULL;
        if (dma_memory_read(&address_space_memory, address, &lli, sizeof(lli),
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            dw_axi_dmac_fail_channel(s, channel,
                                     CH_IRQ_LLI_READ_DECODE_ERROR);
            return;
        }

        ctl_high = le32_to_cpu(lli.ctl_high);
        if (!(ctl_high & BIT(31))) {
            dw_axi_dmac_fail_channel(s, channel, CH_IRQ_INVALID_ERROR);
            return;
        }

        ch->sar = le64_to_cpu(lli.sar);
        ch->dar = le64_to_cpu(lli.dar);
        ch->block_ts = le32_to_cpu(lli.block_ts_low) & CH_BLOCK_TS_MASK;
        ch->ctl = (uint64_t)ctl_high << 32 |
                  le32_to_cpu(lli.ctl_low);
        ch->llp = address;
        next = le64_to_cpu(lli.llp);

        event = dw_axi_dmac_copy_block(s, ch);
        if (event) {
            if (!dw_axi_dmac_writeback_lli(address, ctl_high, event)) {
                event |= CH_IRQ_LLI_WRITE_DECODE_ERROR;
            }
            dw_axi_dmac_fail_channel(s, channel, event);
            return;
        }

        event = CH_IRQ_BLOCK_TRANSFER;
        if (ch->ctl & CH_CTL_LLI_LAST) {
            event |= CH_IRQ_DMA_TRANSFER;
        }
        if (!dw_axi_dmac_writeback_lli(address, ctl_high, event)) {
            dw_axi_dmac_fail_channel(s, channel,
                                     CH_IRQ_LLI_WRITE_DECODE_ERROR);
            return;
        }

        ch->llp = next;
        if (event & CH_IRQ_DMA_TRANSFER) {
            ch->enabled = false;
            ch->suspended = false;
            dw_axi_dmac_raise_channel(s, channel, event);
            return;
        }

        dw_axi_dmac_raise_channel(s, channel, CH_IRQ_BLOCK_TRANSFER);
    }

    dw_axi_dmac_fail_channel(s, channel, CH_IRQ_MULTIBLOCK_ERROR);
}

static bool dw_axi_dmac_is_linked_list(DWAxiDMACChannel *ch)
{
    unsigned source_type = extract64(ch->cfg,
                                     CH_CFG_SRC_MULTIBLOCK_SHIFT, 2);
    unsigned destination_type = extract64(ch->cfg,
                                          CH_CFG_DST_MULTIBLOCK_SHIFT, 2);

    return source_type == CH_CFG_MULTIBLOCK_LINKED_LIST &&
           destination_type == CH_CFG_MULTIBLOCK_LINKED_LIST;
}

static void dw_axi_dmac_run_channel(DWAxiDMACState *s, unsigned channel)
{
    DWAxiDMACChannel *ch = &s->channel[channel];
    unsigned transfer_type = extract64(ch->cfg, CH_CFG_TT_FC_SHIFT, 3);
    uint32_t event;

    ch->status = 0;
    if (transfer_type != CH_CFG_TT_FC_MEM_TO_MEM) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: channel %u peripheral handshake transfer is not "
                      "connected\n", TYPE_DW_AXI_DMAC, channel);
        dw_axi_dmac_fail_channel(s, channel, CH_IRQ_INVALID_ERROR);
        return;
    }

    if (dw_axi_dmac_is_linked_list(ch)) {
        dw_axi_dmac_run_linked_list(s, channel);
        return;
    }

    event = dw_axi_dmac_copy_block(s, ch);
    if (event) {
        dw_axi_dmac_fail_channel(s, channel, event);
        return;
    }

    ch->enabled = false;
    ch->suspended = false;
    dw_axi_dmac_raise_channel(s, channel,
                              CH_IRQ_BLOCK_TRANSFER | CH_IRQ_DMA_TRANSFER);
}

static uint64_t dw_axi_dmac_channel_read(DWAxiDMACState *s,
                                         unsigned channel, hwaddr reg)
{
    DWAxiDMACChannel *ch = &s->channel[channel];

    switch (reg) {
    case CH_SAR:
        return ch->sar;
    case CH_DAR:
        return ch->dar;
    case CH_BLOCK_TS:
        return ch->block_ts;
    case CH_CTL:
        return ch->ctl;
    case CH_CFG:
        return ch->cfg;
    case CH_LLP:
        return ch->llp;
    case CH_STATUS:
        return ch->status;
    case CH_SW_HS_SRC:
        return ch->sw_hs_src;
    case CH_SW_HS_DST:
        return ch->sw_hs_dst;
    case CH_BLOCK_TRANSFER_RESUME:
        return 0;
    case CH_AXI_ID:
        return ch->axi_id;
    case CH_AXI_QOS:
        return ch->axi_qos;
    case CH_SSTAT:
    case CH_DSTAT:
        return 0;
    case CH_SSTAT_ADDR:
        return ch->sstat_addr;
    case CH_DSTAT_ADDR:
        return ch->dstat_addr;
    case CH_INTSTATUS_ENABLE:
        return ch->int_status_enable;
    case CH_INTSTATUS:
        return ch->int_status;
    case CH_INTSIGNAL_ENABLE:
        return ch->int_signal_enable;
    case CH_INTCLEAR:
        return 0;
    default:
        return 0;
    }
}

static void dw_axi_dmac_channel_write(DWAxiDMACState *s, unsigned channel,
                                      hwaddr reg, hwaddr offset,
                                      uint64_t value, unsigned size)
{
    DWAxiDMACChannel *ch = &s->channel[channel];

    switch (reg) {
    case CH_SAR:
        ch->sar = dw_axi_dmac_merge(ch->sar, value, offset, size);
        break;
    case CH_DAR:
        ch->dar = dw_axi_dmac_merge(ch->dar, value, offset, size);
        break;
    case CH_BLOCK_TS:
        ch->block_ts = dw_axi_dmac_merge(ch->block_ts, value, offset, size) &
                       CH_BLOCK_TS_MASK;
        break;
    case CH_CTL:
        ch->ctl = dw_axi_dmac_merge(ch->ctl, value, offset, size);
        break;
    case CH_CFG:
        ch->cfg = dw_axi_dmac_merge(ch->cfg, value, offset, size);
        break;
    case CH_LLP:
        ch->llp = dw_axi_dmac_merge(ch->llp, value, offset, size);
        break;
    case CH_SW_HS_SRC:
        ch->sw_hs_src = dw_axi_dmac_merge(ch->sw_hs_src, value, offset,
                                          size) & 0x3f;
        break;
    case CH_SW_HS_DST:
        ch->sw_hs_dst = dw_axi_dmac_merge(ch->sw_hs_dst, value, offset,
                                          size) & 0x3f;
        break;
    case CH_BLOCK_TRANSFER_RESUME:
        if (ch->enabled && !ch->suspended) {
            dw_axi_dmac_run_channel(s, channel);
        }
        break;
    case CH_AXI_ID:
        ch->axi_id = dw_axi_dmac_merge(ch->axi_id, value, offset, size);
        break;
    case CH_AXI_QOS:
        ch->axi_qos = dw_axi_dmac_merge(ch->axi_qos, value, offset, size) &
                      0xff;
        break;
    case CH_SSTAT_ADDR:
        ch->sstat_addr = dw_axi_dmac_merge(ch->sstat_addr, value, offset,
                                           size);
        break;
    case CH_DSTAT_ADDR:
        ch->dstat_addr = dw_axi_dmac_merge(ch->dstat_addr, value, offset,
                                           size);
        break;
    case CH_INTSTATUS_ENABLE:
        ch->int_status_enable =
            dw_axi_dmac_merge(ch->int_status_enable, value, offset, size) &
            CH_IRQ_VALID_MASK;
        ch->int_status &= ch->int_status_enable;
        dw_axi_dmac_update_irq(s);
        break;
    case CH_INTSIGNAL_ENABLE:
        ch->int_signal_enable =
            dw_axi_dmac_merge(ch->int_signal_enable, value, offset, size) &
            CH_IRQ_VALID_MASK;
        dw_axi_dmac_update_irq(s);
        break;
    case CH_INTCLEAR: {
        unsigned shift = (offset & 7) * 8;
        uint64_t written = value << shift;

        ch->int_status &= ~(written & CH_IRQ_VALID_MASK);
        dw_axi_dmac_update_irq(s);
        break;
    }
    case CH_STATUS:
    case CH_SSTAT:
    case CH_DSTAT:
    case CH_INTSTATUS:
        /* Read-only registers. */
        break;
    default:
        break;
    }
}

static uint64_t dw_axi_dmac_chen(DWAxiDMACState *s)
{
    uint64_t value = 0;

    for (unsigned i = 0; i < s->num_channels; i++) {
        if (s->channel[i].enabled) {
            value |= BIT_ULL(i + DMAC_CH_ENABLE_SHIFT);
        }
        if (s->channel[i].suspended) {
            value |= BIT_ULL(i + DMAC_CH_SUSPEND_SHIFT);
        }
    }
    return value;
}

static void dw_axi_dmac_write_chen(DWAxiDMACState *s, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    uint64_t written = value << ((offset & 7) * 8);
    bool start[DW_AXI_DMAC_MAX_CHANNELS] = {};

    if (!(s->cfg & DMAC_CFG_ENABLE)) {
        return;
    }

    for (unsigned i = 0; i < s->num_channels; i++) {
        DWAxiDMACChannel *ch = &s->channel[i];

        if (written & BIT_ULL(i + DMAC_CH_ABORT_WE_SHIFT)) {
            if (written & BIT_ULL(i + DMAC_CH_ABORT_SHIFT)) {
                bool was_enabled = ch->enabled;

                ch->enabled = false;
                ch->suspended = false;
                if (was_enabled) {
                    dw_axi_dmac_raise_channel(s, i, CH_IRQ_ABORTED);
                }
            }
        }

        if (written & BIT_ULL(i + DMAC_CH_SUSPEND_WE_SHIFT)) {
            bool suspend = written & BIT_ULL(i + DMAC_CH_SUSPEND_SHIFT);

            ch->suspended = suspend && ch->enabled;
            if (ch->suspended) {
                dw_axi_dmac_raise_channel(s, i, CH_IRQ_SUSPENDED);
            }
        }

        if (written & BIT_ULL(i + DMAC_CH_ENABLE_WE_SHIFT)) {
            bool enable = written & BIT_ULL(i + DMAC_CH_ENABLE_SHIFT);

            if (!enable) {
                bool was_enabled = ch->enabled;

                ch->enabled = false;
                ch->suspended = false;
                if (was_enabled) {
                    dw_axi_dmac_raise_channel(s, i, CH_IRQ_DISABLED);
                }
            } else if (!ch->enabled) {
                ch->enabled = true;
                ch->suspended = false;
                start[i] = true;
            }
        }
    }

    for (unsigned i = 0; i < s->num_channels; i++) {
        if (start[i]) {
            dw_axi_dmac_run_channel(s, i);
        }
    }
}

static uint64_t dw_axi_dmac_read(void *opaque, hwaddr offset, unsigned size)
{
    DWAxiDMACState *s = opaque;
    hwaddr reg = offset & ~7ULL;
    uint64_t value = 0;

    if (offset >= DW_AXI_DMAC_COMMON_SIZE) {
        unsigned channel = (offset - DW_AXI_DMAC_COMMON_SIZE) /
                           DW_AXI_DMAC_CHANNEL_SIZE;
        hwaddr channel_offset = (offset - DW_AXI_DMAC_COMMON_SIZE) %
                                DW_AXI_DMAC_CHANNEL_SIZE;

        if (channel >= s->num_channels) {
            return 0;
        }
        value = dw_axi_dmac_channel_read(s, channel, channel_offset & ~7ULL);
        return dw_axi_dmac_extract(value, channel_offset, size);
    }

    switch (reg) {
    case DMAC_ID:
        value = s->id;
        break;
    case DMAC_COMPONENT_VERSION:
        value = s->component_version;
        break;
    case DMAC_CFG:
        value = s->cfg;
        break;
    case DMAC_CHEN:
        value = dw_axi_dmac_chen(s);
        break;
    case DMAC_INTSTATUS:
        value = dw_axi_dmac_combined_status(s);
        break;
    case DMAC_COMMON_INTCLEAR:
        value = 0;
        break;
    case DMAC_COMMON_INTSTATUS_ENABLE:
        value = s->common_int_status_enable;
        break;
    case DMAC_COMMON_INTSIGNAL_ENABLE:
        value = s->common_int_signal_enable;
        break;
    case DMAC_COMMON_INTSTATUS:
        value = s->common_int_status;
        break;
    case DMAC_RESET:
        value = 0;
        break;
    case DMAC_LOWPOWER_CFG:
        value = s->low_power_cfg;
        break;
    default:
        break;
    }

    return dw_axi_dmac_extract(value, offset, size);
}

static void dw_axi_dmac_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    DWAxiDMACState *s = opaque;
    hwaddr reg = offset & ~7ULL;

    if (offset >= DW_AXI_DMAC_COMMON_SIZE) {
        unsigned channel = (offset - DW_AXI_DMAC_COMMON_SIZE) /
                           DW_AXI_DMAC_CHANNEL_SIZE;
        hwaddr channel_offset = (offset - DW_AXI_DMAC_COMMON_SIZE) %
                                DW_AXI_DMAC_CHANNEL_SIZE;

        if (channel < s->num_channels) {
            dw_axi_dmac_channel_write(s, channel, channel_offset & ~7ULL,
                                      channel_offset, value, size);
        }
        return;
    }

    switch (reg) {
    case DMAC_CFG: {
        uint64_t old = s->cfg;

        s->cfg = dw_axi_dmac_merge(s->cfg, value, offset, size) & 0x3;
        if ((old & DMAC_CFG_ENABLE) && !(s->cfg & DMAC_CFG_ENABLE)) {
            for (unsigned i = 0; i < s->num_channels; i++) {
                s->channel[i].enabled = false;
                s->channel[i].suspended = false;
            }
        }
        dw_axi_dmac_update_irq(s);
        break;
    }
    case DMAC_CHEN:
        dw_axi_dmac_write_chen(s, offset, value, size);
        break;
    case DMAC_COMMON_INTCLEAR: {
        uint64_t written = value << ((offset & 7) * 8);

        s->common_int_status &= ~(written & COMMON_IRQ_VALID_MASK);
        dw_axi_dmac_update_irq(s);
        break;
    }
    case DMAC_COMMON_INTSTATUS_ENABLE:
        s->common_int_status_enable =
            dw_axi_dmac_merge(s->common_int_status_enable, value, offset,
                              size) & COMMON_IRQ_VALID_MASK;
        s->common_int_status &= s->common_int_status_enable;
        dw_axi_dmac_update_irq(s);
        break;
    case DMAC_COMMON_INTSIGNAL_ENABLE:
        s->common_int_signal_enable =
            dw_axi_dmac_merge(s->common_int_signal_enable, value, offset,
                              size) & COMMON_IRQ_VALID_MASK;
        dw_axi_dmac_update_irq(s);
        break;
    case DMAC_RESET:
        if ((value << ((offset & 7) * 8)) & BIT(0)) {
            dw_axi_dmac_reset_state(s);
        }
        break;
    case DMAC_LOWPOWER_CFG:
        s->low_power_cfg = dw_axi_dmac_merge(s->low_power_cfg, value,
                                              offset, size);
        break;
    case DMAC_ID:
    case DMAC_COMPONENT_VERSION:
    case DMAC_INTSTATUS:
    case DMAC_COMMON_INTSTATUS:
        /* Read-only registers. */
        break;
    default:
        break;
    }
}

static const MemoryRegionOps dw_axi_dmac_ops = {
    .read = dw_axi_dmac_read,
    .write = dw_axi_dmac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
        .unaligned = false,
    },
};

static void dw_axi_dmac_realize(DeviceState *dev, Error **errp)
{
    DWAxiDMACState *s = DW_AXI_DMAC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (!s->num_channels || s->num_channels > DW_AXI_DMAC_MAX_CHANNELS) {
        error_setg(errp, "num-channels must be between 1 and %u",
                   DW_AXI_DMAC_MAX_CHANNELS);
        return;
    }
    if (!s->block_size || s->block_size > (CH_BLOCK_TS_MASK + 1)) {
        error_setg(errp, "block-size must be between 1 and 0x%" PRIx64,
                   (uint64_t)CH_BLOCK_TS_MASK + 1);
        return;
    }
    if (s->data_width > 6) {
        error_setg(errp, "data-width must be at most 6");
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &dw_axi_dmac_ops, s,
                          TYPE_DW_AXI_DMAC, DW_AXI_DMAC_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static int dw_axi_dmac_post_load(void *opaque, int version_id)
{
    DWAxiDMACState *s = opaque;

    dw_axi_dmac_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_dw_axi_dmac_channel = {
    .name = TYPE_DW_AXI_DMAC "/channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(sar, DWAxiDMACChannel),
        VMSTATE_UINT64(dar, DWAxiDMACChannel),
        VMSTATE_UINT64(block_ts, DWAxiDMACChannel),
        VMSTATE_UINT64(ctl, DWAxiDMACChannel),
        VMSTATE_UINT64(cfg, DWAxiDMACChannel),
        VMSTATE_UINT64(llp, DWAxiDMACChannel),
        VMSTATE_UINT64(status, DWAxiDMACChannel),
        VMSTATE_UINT64(sw_hs_src, DWAxiDMACChannel),
        VMSTATE_UINT64(sw_hs_dst, DWAxiDMACChannel),
        VMSTATE_UINT64(axi_id, DWAxiDMACChannel),
        VMSTATE_UINT64(axi_qos, DWAxiDMACChannel),
        VMSTATE_UINT64(sstat_addr, DWAxiDMACChannel),
        VMSTATE_UINT64(dstat_addr, DWAxiDMACChannel),
        VMSTATE_UINT32(int_status_enable, DWAxiDMACChannel),
        VMSTATE_UINT32(int_status, DWAxiDMACChannel),
        VMSTATE_UINT32(int_signal_enable, DWAxiDMACChannel),
        VMSTATE_BOOL(enabled, DWAxiDMACChannel),
        VMSTATE_BOOL(suspended, DWAxiDMACChannel),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_dw_axi_dmac = {
    .name = TYPE_DW_AXI_DMAC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = dw_axi_dmac_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(cfg, DWAxiDMACState),
        VMSTATE_UINT64(low_power_cfg, DWAxiDMACState),
        VMSTATE_UINT32(common_int_status_enable, DWAxiDMACState),
        VMSTATE_UINT32(common_int_status, DWAxiDMACState),
        VMSTATE_UINT32(common_int_signal_enable, DWAxiDMACState),
        VMSTATE_STRUCT_ARRAY(channel, DWAxiDMACState,
                             DW_AXI_DMAC_MAX_CHANNELS, 1,
                             vmstate_dw_axi_dmac_channel,
                             DWAxiDMACChannel),
        VMSTATE_END_OF_LIST()
    },
};

static const Property dw_axi_dmac_properties[] = {
    DEFINE_PROP_UINT64("id", DWAxiDMACState, id, 0),
    DEFINE_PROP_UINT64("component-version", DWAxiDMACState,
                       component_version, 0x3130312a),
    DEFINE_PROP_UINT32("num-channels", DWAxiDMACState, num_channels, 4),
    DEFINE_PROP_UINT32("block-size", DWAxiDMACState, block_size, 65536),
    DEFINE_PROP_UINT32("data-width", DWAxiDMACState, data_width, 4),
};

static void dw_axi_dmac_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Synopsys DesignWare AXI DMA Controller";
    dc->realize = dw_axi_dmac_realize;
    device_class_set_legacy_reset(dc, dw_axi_dmac_reset);
    dc->vmsd = &vmstate_dw_axi_dmac;
    device_class_set_props(dc, dw_axi_dmac_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo dw_axi_dmac_type_info = {
    .name = TYPE_DW_AXI_DMAC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWAxiDMACState),
    .class_init = dw_axi_dmac_class_init,
};

static void dw_axi_dmac_register_types(void)
{
    type_register_static(&dw_axi_dmac_type_info);
}

type_init(dw_axi_dmac_register_types)
