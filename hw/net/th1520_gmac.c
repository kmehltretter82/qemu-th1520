/*
 * T-Head TH1520 GMAC APB clock glue
 *
 * This models the digital register contract only.  Clock phase/delay fields
 * are retained for software visibility but do not alter virtual packet
 * timing.  See docs/devel/beaglev-ahead-hardware-validation.md, GMAC-001.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/net/th1520_gmac.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

enum {
    TH1520_GMAC_CLK_EN,
    TH1520_GMAC_RXCLK_DELAY,
    TH1520_GMAC_TXCLK_DELAY,
    TH1520_GMAC_PLLCLK_DIV,
    TH1520_GMAC_EPHY_DIV,
    TH1520_GMAC_PTPCLK_DIV,
    TH1520_GMAC_GTXCLK_SEL,
    TH1520_GMAC_INTF_CTRL,
    TH1520_GMAC_TXCLK_OEN,
};

static const uint32_t th1520_gmac_apb_reset_values[] = {
    [TH1520_GMAC_CLK_EN]       = 0x00000008,
    [TH1520_GMAC_RXCLK_DELAY]  = 0x00008000,
    [TH1520_GMAC_TXCLK_DELAY]  = 0x00008000,
    [TH1520_GMAC_PLLCLK_DIV]   = 0x00000004,
    [TH1520_GMAC_EPHY_DIV]     = 0x00000014,
    [TH1520_GMAC_PTPCLK_DIV]   = 0x00000002,
    [TH1520_GMAC_GTXCLK_SEL]   = 0x00000001,
    [TH1520_GMAC_INTF_CTRL]    = 0x00000000,
    [TH1520_GMAC_TXCLK_OEN]    = 0x00000001,
};

static const uint32_t th1520_gmac_apb_write_masks[] = {
    [TH1520_GMAC_CLK_EN]       = 0x000000ff,
    [TH1520_GMAC_RXCLK_DELAY]  = 0x0000c01f,
    [TH1520_GMAC_TXCLK_DELAY]  = 0x0000c01f,
    [TH1520_GMAC_PLLCLK_DIV]   = 0x800000ff,
    [TH1520_GMAC_EPHY_DIV]     = 0x800000ff,
    [TH1520_GMAC_PTPCLK_DIV]   = 0x8000000f,
    [TH1520_GMAC_GTXCLK_SEL]   = 0x00000001,
    [TH1520_GMAC_INTF_CTRL]    = 0x00000001,
    [TH1520_GMAC_TXCLK_OEN]    = 0x00000001,
};

static uint64_t th1520_gmac_apb_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    TH1520GMACAPBState *s = opaque;
    unsigned reg = offset / sizeof(uint32_t);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from reserved offset 0x%" HWADDR_PRIx "\n",
                      TYPE_TH1520_GMAC_APB, offset);
        return 0;
    }

    return s->regs[reg];
}

static void th1520_gmac_apb_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size)
{
    TH1520GMACAPBState *s = opaque;
    unsigned reg = offset / sizeof(uint32_t);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to reserved offset 0x%" HWADDR_PRIx "\n",
                      TYPE_TH1520_GMAC_APB, offset);
        return;
    }

    s->regs[reg] = value & th1520_gmac_apb_write_masks[reg];
}

static const MemoryRegionOps th1520_gmac_apb_ops = {
    .read = th1520_gmac_apb_read,
    .write = th1520_gmac_apb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void th1520_gmac_apb_reset(DeviceState *dev)
{
    TH1520GMACAPBState *s = TH1520_GMAC_APB(dev);

    memcpy(s->regs, th1520_gmac_apb_reset_values, sizeof(s->regs));
}

static const VMStateDescription vmstate_th1520_gmac_apb = {
    .name = TYPE_TH1520_GMAC_APB,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, TH1520GMACAPBState,
                             TH1520_GMAC_APB_NR_REGS),
        VMSTATE_END_OF_LIST()
    },
};

static void th1520_gmac_apb_init(Object *obj)
{
    TH1520GMACAPBState *s = TH1520_GMAC_APB(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_gmac_apb_ops, s,
                          TYPE_TH1520_GMAC_APB, TH1520_GMAC_APB_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void th1520_gmac_apb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 GMAC APB clock glue";
    device_class_set_legacy_reset(dc, th1520_gmac_apb_reset);
    dc->vmsd = &vmstate_th1520_gmac_apb;
}

static const TypeInfo th1520_gmac_apb_type_info = {
    .name = TYPE_TH1520_GMAC_APB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520GMACAPBState),
    .instance_init = th1520_gmac_apb_init,
    .class_init = th1520_gmac_apb_class_init,
};

static void th1520_gmac_apb_register_types(void)
{
    type_register_static(&th1520_gmac_apb_type_info);
}

type_init(th1520_gmac_apb_register_types)
