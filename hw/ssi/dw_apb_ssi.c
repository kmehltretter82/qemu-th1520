/*
 * Synopsys DesignWare APB SSI
 *
 * This is a functional master-mode model of the common APB SSI register
 * interface.  It intentionally has no clock-accurate serialiser: transfers
 * advance synchronously when the engine is enabled and a native chip select
 * is asserted.  Board models supply the physical SSI peripheral wiring.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/ssi/dw_apb_ssi.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define DW_APB_SSI_MMIO_SIZE             0x1000

#define DW_APB_SSI_CTRLR0                0x00
#define DW_APB_SSI_CTRLR1                0x04
#define DW_APB_SSI_SSIENR                0x08
#define DW_APB_SSI_MWCR                  0x0c
#define DW_APB_SSI_SER                   0x10
#define DW_APB_SSI_BAUDR                 0x14
#define DW_APB_SSI_TXFTLR                0x18
#define DW_APB_SSI_RXFTLR                0x1c
#define DW_APB_SSI_TXFLR                 0x20
#define DW_APB_SSI_RXFLR                 0x24
#define DW_APB_SSI_SR                    0x28
#define DW_APB_SSI_IMR                   0x2c
#define DW_APB_SSI_ISR                   0x30
#define DW_APB_SSI_RISR                  0x34
#define DW_APB_SSI_TXOICR                0x38
#define DW_APB_SSI_RXOICR                0x3c
#define DW_APB_SSI_RXUICR                0x40
#define DW_APB_SSI_MSTICR                0x44
#define DW_APB_SSI_ICR                   0x48
#define DW_APB_SSI_DMACR                 0x4c
#define DW_APB_SSI_DMATDLR               0x50
#define DW_APB_SSI_DMARDLR               0x54
#define DW_APB_SSI_IDR                   0x58
#define DW_APB_SSI_VERSION               0x5c
#define DW_APB_SSI_DR                    0x60
#define DW_APB_SSI_DR_END                0xf0
#define DW_APB_SSI_RX_SAMPLE_DLY         0xf0
#define DW_APB_SSI_SPI_CTRLR0            0xf4

#define DW_APB_SSI_CTRLR0_DFS_MASK       0x0f
#define DW_APB_SSI_CTRLR0_SCPHA          BIT(6)
#define DW_APB_SSI_CTRLR0_SCPOL          BIT(7)
#define DW_APB_SSI_CTRLR0_TMOD_MASK      (0x3 << 8)
#define DW_APB_SSI_CTRLR0_SRL            BIT(11)
#define DW_APB_SSI_CTRLR0_VALID          \
    (DW_APB_SSI_CTRLR0_DFS_MASK | DW_APB_SSI_CTRLR0_SCPHA | \
     DW_APB_SSI_CTRLR0_SCPOL | DW_APB_SSI_CTRLR0_TMOD_MASK | \
     DW_APB_SSI_CTRLR0_SRL)

#define DW_APB_SSI_TMOD_TR               0
#define DW_APB_SSI_TMOD_TO               1
#define DW_APB_SSI_TMOD_RO               2
#define DW_APB_SSI_TMOD_EEPROM_READ      3

#define DW_APB_SSI_SR_BUSY                BIT(0)
#define DW_APB_SSI_SR_TF_NOT_FULL         BIT(1)
#define DW_APB_SSI_SR_TF_EMPTY            BIT(2)
#define DW_APB_SSI_SR_RF_NOT_EMPTY        BIT(3)
#define DW_APB_SSI_SR_RF_FULL             BIT(4)
#define DW_APB_SSI_SR_TX_ERROR            BIT(5)

#define DW_APB_SSI_INT_TXEI               BIT(0)
#define DW_APB_SSI_INT_TXOI               BIT(1)
#define DW_APB_SSI_INT_RXUI               BIT(2)
#define DW_APB_SSI_INT_RXOI               BIT(3)
#define DW_APB_SSI_INT_RXFI               BIT(4)
#define DW_APB_SSI_INT_MSTI               BIT(5)
#define DW_APB_SSI_INT_MASK               0x3f

#define DW_APB_SSI_DMACR_VALID            0x03
#define DW_APB_SSI_DMA_TDLR_VALID          0xff
#define DW_APB_SSI_DMA_RDLR_VALID          0xff
#define DW_APB_SSI_RX_SAMPLE_DLY_VALID     0xff

static uint32_t dw_apb_ssi_cs_mask(const DWAPBSSIState *s)
{
    return s->num_cs == DW_APB_SSI_MAX_CS ? UINT16_MAX :
           MAKE_64BIT_MASK(0, s->num_cs);
}

static bool dw_apb_ssi_engine_active(const DWAPBSSIState *s)
{
    return s->ssienr && s->ser;
}

static unsigned int dw_apb_ssi_tmod(const DWAPBSSIState *s)
{
    return extract32(s->ctrlr0, 8, 2);
}

static uint32_t dw_apb_ssi_frame_mask(const DWAPBSSIState *s)
{
    unsigned int bits = (s->ctrlr0 & DW_APB_SSI_CTRLR0_DFS_MASK) + 1;

    return MAKE_64BIT_MASK(0, bits);
}

static void dw_apb_ssi_update_cs(DWAPBSSIState *s)
{
    for (unsigned int i = 0; i < s->num_cs; i++) {
        bool selected = dw_apb_ssi_engine_active(s) && (s->ser & BIT(i));

        qemu_set_irq(s->cs[i], !selected);
    }
}

static uint32_t dw_apb_ssi_raw_interrupt_status(DWAPBSSIState *s)
{
    uint32_t status = 0;

    if (s->ssienr && fifo32_num_used(&s->tx_fifo) <= s->txftlr) {
        status |= DW_APB_SSI_INT_TXEI;
    }
    if (s->tx_overflow) {
        status |= DW_APB_SSI_INT_TXOI;
    }
    if (s->rx_underflow) {
        status |= DW_APB_SSI_INT_RXUI;
    }
    if (s->rx_overflow) {
        status |= DW_APB_SSI_INT_RXOI;
    }
    if (s->ssienr && fifo32_num_used(&s->rx_fifo) > s->rxftlr) {
        status |= DW_APB_SSI_INT_RXFI;
    }
    if (s->mst_error) {
        status |= DW_APB_SSI_INT_MSTI;
    }

    return status;
}

static void dw_apb_ssi_update_irq(DWAPBSSIState *s)
{
    qemu_set_irq(s->irq, !!(dw_apb_ssi_raw_interrupt_status(s) & s->imr));
}

static uint32_t dw_apb_ssi_status(DWAPBSSIState *s)
{
    uint32_t status = 0;
    uint32_t tx_used = fifo32_num_used(&s->tx_fifo);
    uint32_t rx_used = fifo32_num_used(&s->rx_fifo);

    if (dw_apb_ssi_engine_active(s) &&
        (tx_used || s->auto_rx_remaining)) {
        status |= DW_APB_SSI_SR_BUSY;
    }
    if (tx_used < s->fifo_depth) {
        status |= DW_APB_SSI_SR_TF_NOT_FULL;
    }
    if (!tx_used) {
        status |= DW_APB_SSI_SR_TF_EMPTY;
    }
    if (rx_used) {
        status |= DW_APB_SSI_SR_RF_NOT_EMPTY;
    }
    if (rx_used == s->fifo_depth) {
        status |= DW_APB_SSI_SR_RF_FULL;
    }
    if (s->tx_overflow) {
        status |= DW_APB_SSI_SR_TX_ERROR;
    }

    return status;
}

static void dw_apb_ssi_push_rx(DWAPBSSIState *s, uint32_t value)
{
    if (fifo32_is_full(&s->rx_fifo)) {
        s->rx_overflow = 1;
        return;
    }

    fifo32_push(&s->rx_fifo, value & dw_apb_ssi_frame_mask(s));
}

static uint32_t dw_apb_ssi_transfer(DWAPBSSIState *s, uint32_t value)
{
    if (s->ctrlr0 & DW_APB_SSI_CTRLR0_SRL) {
        return value & dw_apb_ssi_frame_mask(s);
    }

    return ssi_transfer(s->spi, value & dw_apb_ssi_frame_mask(s));
}

static void dw_apb_ssi_generate_receive(DWAPBSSIState *s)
{
    while (dw_apb_ssi_engine_active(s) && s->auto_rx_remaining &&
           !fifo32_is_full(&s->rx_fifo)) {
        dw_apb_ssi_push_rx(s, dw_apb_ssi_transfer(s, 0));
        s->auto_rx_remaining--;
    }
}

static void dw_apb_ssi_flush_tx(DWAPBSSIState *s)
{
    unsigned int tmod = dw_apb_ssi_tmod(s);

    if (!dw_apb_ssi_engine_active(s)) {
        return;
    }

    while (!fifo32_is_empty(&s->tx_fifo)) {
        uint32_t value = fifo32_pop(&s->tx_fifo);
        bool capture = tmod == DW_APB_SSI_TMOD_TR;

        /* EEPROM-read mode discards command/address response frames. */
        if (capture) {
            dw_apb_ssi_push_rx(s, dw_apb_ssi_transfer(s, value));
        } else {
            dw_apb_ssi_transfer(s, value);
        }
    }
}

static void dw_apb_ssi_start_transfer(DWAPBSSIState *s, bool start_auto)
{
    unsigned int tmod;

    if (!dw_apb_ssi_engine_active(s)) {
        return;
    }

    dw_apb_ssi_flush_tx(s);
    tmod = dw_apb_ssi_tmod(s);
    if (start_auto && !s->auto_rx_remaining &&
        (tmod == DW_APB_SSI_TMOD_RO ||
         tmod == DW_APB_SSI_TMOD_EEPROM_READ)) {
        s->auto_rx_remaining = (s->ctrlr1 & UINT16_MAX) + 1;
    }
    dw_apb_ssi_generate_receive(s);
}

static uint32_t dw_apb_ssi_read_data(DWAPBSSIState *s)
{
    uint32_t value;

    if (fifo32_is_empty(&s->rx_fifo)) {
        s->rx_underflow = 1;
        value = 0;
    } else {
        value = fifo32_pop(&s->rx_fifo);
        dw_apb_ssi_generate_receive(s);
    }
    dw_apb_ssi_update_irq(s);

    return value;
}

static void dw_apb_ssi_clear_interrupts(DWAPBSSIState *s, uint32_t mask)
{
    if (mask & DW_APB_SSI_INT_TXOI) {
        s->tx_overflow = 0;
    }
    if (mask & DW_APB_SSI_INT_RXUI) {
        s->rx_underflow = 0;
    }
    if (mask & DW_APB_SSI_INT_RXOI) {
        s->rx_overflow = 0;
    }
    if (mask & DW_APB_SSI_INT_MSTI) {
        s->mst_error = 0;
    }
    dw_apb_ssi_update_irq(s);
}

static uint64_t dw_apb_ssi_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    DWAPBSSIState *s = opaque;
    uint32_t value;

    if (offset >= DW_APB_SSI_DR && offset < DW_APB_SSI_DR_END) {
        return dw_apb_ssi_read_data(s);
    }

    switch (offset) {
    case DW_APB_SSI_CTRLR0:
        value = s->ctrlr0;
        break;
    case DW_APB_SSI_CTRLR1:
        value = s->ctrlr1;
        break;
    case DW_APB_SSI_SSIENR:
        value = s->ssienr;
        break;
    case DW_APB_SSI_MWCR:
        value = s->mwcr;
        break;
    case DW_APB_SSI_SER:
        value = s->ser;
        break;
    case DW_APB_SSI_BAUDR:
        value = s->baudr;
        break;
    case DW_APB_SSI_TXFTLR:
        value = s->txftlr;
        break;
    case DW_APB_SSI_RXFTLR:
        value = s->rxftlr;
        break;
    case DW_APB_SSI_TXFLR:
        value = fifo32_num_used(&s->tx_fifo);
        break;
    case DW_APB_SSI_RXFLR:
        value = fifo32_num_used(&s->rx_fifo);
        break;
    case DW_APB_SSI_SR:
        value = dw_apb_ssi_status(s);
        break;
    case DW_APB_SSI_IMR:
        value = s->imr;
        break;
    case DW_APB_SSI_ISR:
        value = dw_apb_ssi_raw_interrupt_status(s) & s->imr;
        break;
    case DW_APB_SSI_RISR:
        value = dw_apb_ssi_raw_interrupt_status(s);
        break;
    case DW_APB_SSI_TXOICR:
        dw_apb_ssi_clear_interrupts(s, DW_APB_SSI_INT_TXOI);
        value = 0;
        break;
    case DW_APB_SSI_RXOICR:
        dw_apb_ssi_clear_interrupts(s, DW_APB_SSI_INT_RXOI);
        value = 0;
        break;
    case DW_APB_SSI_RXUICR:
        dw_apb_ssi_clear_interrupts(s, DW_APB_SSI_INT_RXUI);
        value = 0;
        break;
    case DW_APB_SSI_MSTICR:
        dw_apb_ssi_clear_interrupts(s, DW_APB_SSI_INT_MSTI);
        value = 0;
        break;
    case DW_APB_SSI_ICR:
        dw_apb_ssi_clear_interrupts(s, DW_APB_SSI_INT_MASK);
        value = 0;
        break;
    case DW_APB_SSI_DMACR:
        value = s->dmacr;
        break;
    case DW_APB_SSI_DMATDLR:
        value = s->dmatdlr;
        break;
    case DW_APB_SSI_DMARDLR:
        value = s->dmardlr;
        break;
    case DW_APB_SSI_IDR:
        value = s->component_id;
        break;
    case DW_APB_SSI_VERSION:
        value = s->component_version;
        break;
    case DW_APB_SSI_RX_SAMPLE_DLY:
        value = s->rx_sample_dly;
        break;
    case DW_APB_SSI_SPI_CTRLR0:
        value = s->spi_ctrlr0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from reserved register 0x%03" HWADDR_PRIx
                      "\n", TYPE_DW_APB_SSI, offset);
        value = 0;
        break;
    }

    return value;
}

static bool dw_apb_ssi_config_write(DWAPBSSIState *s, hwaddr offset)
{
    if (!s->ssienr) {
        return true;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: write to configuration register 0x%03" HWADDR_PRIx
                  " while enabled\n", TYPE_DW_APB_SSI, offset);
    return false;
}

static void dw_apb_ssi_write_data(DWAPBSSIState *s, uint32_t value)
{
    if (fifo32_is_full(&s->tx_fifo)) {
        s->tx_overflow = 1;
    } else {
        fifo32_push(&s->tx_fifo, value & dw_apb_ssi_frame_mask(s));
        dw_apb_ssi_start_transfer(s, false);
    }
    dw_apb_ssi_update_irq(s);
}

static void dw_apb_ssi_disable(DWAPBSSIState *s)
{
    s->ssienr = 0;
    s->auto_rx_remaining = 0;
    fifo32_reset(&s->tx_fifo);
    fifo32_reset(&s->rx_fifo);
    dw_apb_ssi_update_cs(s);
    dw_apb_ssi_update_irq(s);
}

static void dw_apb_ssi_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned int size)
{
    DWAPBSSIState *s = opaque;
    uint32_t value = data;

    if (offset >= DW_APB_SSI_DR && offset < DW_APB_SSI_DR_END) {
        dw_apb_ssi_write_data(s, value);
        return;
    }

    switch (offset) {
    case DW_APB_SSI_CTRLR0:
        if (dw_apb_ssi_config_write(s, offset)) {
            s->ctrlr0 = value & DW_APB_SSI_CTRLR0_VALID;
        }
        break;
    case DW_APB_SSI_CTRLR1:
        if (dw_apb_ssi_config_write(s, offset)) {
            s->ctrlr1 = value & UINT16_MAX;
        }
        break;
    case DW_APB_SSI_SSIENR:
        if (value & BIT(0)) {
            bool was_enabled = s->ssienr;

            s->ssienr = 1;
            dw_apb_ssi_update_cs(s);
            dw_apb_ssi_start_transfer(s, !was_enabled);
            dw_apb_ssi_update_irq(s);
        } else {
            dw_apb_ssi_disable(s);
        }
        break;
    case DW_APB_SSI_MWCR:
        if (dw_apb_ssi_config_write(s, offset)) {
            s->mwcr = value;
        }
        break;
    case DW_APB_SSI_SER:
    {
        uint32_t old_ser = s->ser;
        bool was_active = dw_apb_ssi_engine_active(s);

        s->ser = value & dw_apb_ssi_cs_mask(s);
        if (!s->ser) {
            s->auto_rx_remaining = 0;
        }
        dw_apb_ssi_update_cs(s);
        dw_apb_ssi_start_transfer(s, !was_active || old_ser != s->ser);
        dw_apb_ssi_update_irq(s);
        break;
    }
    case DW_APB_SSI_BAUDR:
        if (dw_apb_ssi_config_write(s, offset)) {
            s->baudr = value & UINT16_MAX;
        }
        break;
    case DW_APB_SSI_TXFTLR:
        s->txftlr = MIN(value, s->fifo_depth - 1);
        dw_apb_ssi_update_irq(s);
        break;
    case DW_APB_SSI_RXFTLR:
        s->rxftlr = MIN(value, s->fifo_depth - 1);
        dw_apb_ssi_update_irq(s);
        break;
    case DW_APB_SSI_IMR:
        s->imr = value & DW_APB_SSI_INT_MASK;
        dw_apb_ssi_update_irq(s);
        break;
    case DW_APB_SSI_DMACR:
        s->dmacr = value & DW_APB_SSI_DMACR_VALID;
        break;
    case DW_APB_SSI_DMATDLR:
        s->dmatdlr = value & DW_APB_SSI_DMA_TDLR_VALID;
        break;
    case DW_APB_SSI_DMARDLR:
        s->dmardlr = value & DW_APB_SSI_DMA_RDLR_VALID;
        break;
    case DW_APB_SSI_RX_SAMPLE_DLY:
        s->rx_sample_dly = value & DW_APB_SSI_RX_SAMPLE_DLY_VALID;
        break;
    case DW_APB_SSI_SPI_CTRLR0:
        if (dw_apb_ssi_config_write(s, offset)) {
            s->spi_ctrlr0 = value;
        }
        break;
    case DW_APB_SSI_TXFLR:
    case DW_APB_SSI_RXFLR:
    case DW_APB_SSI_SR:
    case DW_APB_SSI_ISR:
    case DW_APB_SSI_RISR:
    case DW_APB_SSI_TXOICR:
    case DW_APB_SSI_RXOICR:
    case DW_APB_SSI_RXUICR:
    case DW_APB_SSI_MSTICR:
    case DW_APB_SSI_ICR:
    case DW_APB_SSI_IDR:
    case DW_APB_SSI_VERSION:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register 0x%03" HWADDR_PRIx
                      "\n", TYPE_DW_APB_SSI, offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to reserved register 0x%03" HWADDR_PRIx
                      "\n", TYPE_DW_APB_SSI, offset);
        break;
    }
}

static bool dw_apb_ssi_access_valid(void *opaque, hwaddr offset,
                                    unsigned int size, bool is_write,
                                    MemTxAttrs attrs)
{
    return size == 4 && !(offset & 3);
}

static const MemoryRegionOps dw_apb_ssi_ops = {
    .read = dw_apb_ssi_read,
    .write = dw_apb_ssi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = dw_apb_ssi_access_valid,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void dw_apb_ssi_reset(DeviceState *dev)
{
    DWAPBSSIState *s = DW_APB_SSI(dev);

    s->ctrlr0 = 7;
    s->ctrlr1 = 0;
    s->ssienr = 0;
    s->mwcr = 0;
    s->ser = 0;
    s->baudr = 0;
    s->txftlr = 0;
    s->rxftlr = 0;
    s->imr = 0;
    s->dmacr = 0;
    s->dmatdlr = 0;
    s->dmardlr = 0;
    s->rx_sample_dly = 0;
    s->spi_ctrlr0 = 0;
    s->tx_overflow = 0;
    s->rx_underflow = 0;
    s->rx_overflow = 0;
    s->mst_error = 0;
    s->auto_rx_remaining = 0;
    fifo32_reset(&s->tx_fifo);
    fifo32_reset(&s->rx_fifo);
    dw_apb_ssi_update_cs(s);
    dw_apb_ssi_update_irq(s);
}

static int dw_apb_ssi_post_load(void *opaque, int version_id)
{
    DWAPBSSIState *s = opaque;

    if ((s->ctrlr0 & ~DW_APB_SSI_CTRLR0_VALID) ||
        (s->ctrlr1 & ~UINT16_MAX) || s->ssienr > 1 ||
        (s->ser & ~dw_apb_ssi_cs_mask(s)) ||
        s->txftlr >= s->fifo_depth || s->rxftlr >= s->fifo_depth ||
        (s->imr & ~DW_APB_SSI_INT_MASK) ||
        (s->dmacr & ~DW_APB_SSI_DMACR_VALID) ||
        (s->dmatdlr & ~DW_APB_SSI_DMA_TDLR_VALID) ||
        (s->dmardlr & ~DW_APB_SSI_DMA_RDLR_VALID) ||
        (s->rx_sample_dly & ~DW_APB_SSI_RX_SAMPLE_DLY_VALID) ||
        s->tx_overflow > 1 || s->rx_underflow > 1 ||
        s->rx_overflow > 1 || s->mst_error > 1) {
        return -EINVAL;
    }

    dw_apb_ssi_update_cs(s);
    dw_apb_ssi_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_dw_apb_ssi = {
    .name = TYPE_DW_APB_SSI,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = dw_apb_ssi_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_FIFO32(tx_fifo, DWAPBSSIState),
        VMSTATE_FIFO32(rx_fifo, DWAPBSSIState),
        VMSTATE_UINT32(ctrlr0, DWAPBSSIState),
        VMSTATE_UINT32(ctrlr1, DWAPBSSIState),
        VMSTATE_UINT32(ssienr, DWAPBSSIState),
        VMSTATE_UINT32(mwcr, DWAPBSSIState),
        VMSTATE_UINT32(ser, DWAPBSSIState),
        VMSTATE_UINT32(baudr, DWAPBSSIState),
        VMSTATE_UINT32(txftlr, DWAPBSSIState),
        VMSTATE_UINT32(rxftlr, DWAPBSSIState),
        VMSTATE_UINT32(imr, DWAPBSSIState),
        VMSTATE_UINT32(dmacr, DWAPBSSIState),
        VMSTATE_UINT32(dmatdlr, DWAPBSSIState),
        VMSTATE_UINT32(dmardlr, DWAPBSSIState),
        VMSTATE_UINT32(rx_sample_dly, DWAPBSSIState),
        VMSTATE_UINT32(spi_ctrlr0, DWAPBSSIState),
        VMSTATE_UINT32(tx_overflow, DWAPBSSIState),
        VMSTATE_UINT32(rx_underflow, DWAPBSSIState),
        VMSTATE_UINT32(rx_overflow, DWAPBSSIState),
        VMSTATE_UINT32(mst_error, DWAPBSSIState),
        VMSTATE_UINT32(auto_rx_remaining, DWAPBSSIState),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property dw_apb_ssi_properties[] = {
    DEFINE_PROP_UINT32("component-id", DWAPBSSIState, component_id, 0),
    DEFINE_PROP_UINT32("component-version", DWAPBSSIState,
                       component_version, 0),
    DEFINE_PROP_UINT16("fifo-depth", DWAPBSSIState, fifo_depth, 16),
    DEFINE_PROP_UINT8("num-cs", DWAPBSSIState, num_cs, 1),
};

static void dw_apb_ssi_init(Object *obj)
{
    DWAPBSSIState *s = DW_APB_SSI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &dw_apb_ssi_ops, s,
                          TYPE_DW_APB_SSI, DW_APB_SSI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void dw_apb_ssi_realize(DeviceState *dev, Error **errp)
{
    DWAPBSSIState *s = DW_APB_SSI(dev);

    if (!s->num_cs || s->num_cs > DW_APB_SSI_MAX_CS) {
        error_setg(errp, "%s: num-cs must be between 1 and %d",
                   TYPE_DW_APB_SSI, DW_APB_SSI_MAX_CS);
        return;
    }
    if (s->fifo_depth < 2 || s->fifo_depth > 256) {
        error_setg(errp, "%s: fifo-depth must be between 2 and 256",
                   TYPE_DW_APB_SSI);
        return;
    }

    s->spi = ssi_create_bus(dev, "spi");
    qdev_init_gpio_out_named(dev, s->cs, "cs", s->num_cs);
    fifo32_create(&s->tx_fifo, s->fifo_depth);
    fifo32_create(&s->rx_fifo, s->fifo_depth);
}

static void dw_apb_ssi_unrealize(DeviceState *dev)
{
    DWAPBSSIState *s = DW_APB_SSI(dev);

    fifo32_destroy(&s->tx_fifo);
    fifo32_destroy(&s->rx_fifo);
}

static void dw_apb_ssi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Synopsys DesignWare APB SSI";
    dc->realize = dw_apb_ssi_realize;
    dc->unrealize = dw_apb_ssi_unrealize;
    dc->vmsd = &vmstate_dw_apb_ssi;
    device_class_set_legacy_reset(dc, dw_apb_ssi_reset);
    device_class_set_props(dc, dw_apb_ssi_properties);
}

static const TypeInfo dw_apb_ssi_info = {
    .name = TYPE_DW_APB_SSI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWAPBSSIState),
    .instance_init = dw_apb_ssi_init,
    .class_init = dw_apb_ssi_class_init,
};

static void dw_apb_ssi_register_types(void)
{
    type_register_static(&dw_apb_ssi_info);
}

type_init(dw_apb_ssi_register_types)
