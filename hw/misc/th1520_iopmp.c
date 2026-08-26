/*
 * T-Head TH1520 I/O physical memory protection configuration window
 *
 * Vendor U-Boot programs the default attribute at 31 listed controller IDs
 * (30 unique 4 KiB apertures) before board initialization.  The vendor Linux
 * driver also establishes the visible default/region/dummy/bypass registers
 * and their sticky page-lock bits.  This models that software-visible
 * configuration contract only.  It intentionally has no downstream DMA
 * filtering, translation, violation interrupt, TEE, or secure-boot effect.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_iopmp.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

static bool th1520_iopmp_bypass_enabled(const TH1520IOPMPState *s)
{
    return s->misc_ctrl & TH1520_IOPMP_CTRL_BYPASS;
}

static bool th1520_iopmp_region_locked(const TH1520IOPMPState *s,
                                       unsigned int region)
{
    return s->page_lock0 & (1U << region);
}

static uint64_t th1520_iopmp_read(void *opaque, hwaddr offset,
                                  unsigned int size)
{
    TH1520IOPMPState *s = opaque;
    unsigned int region;

    switch (offset) {
    case TH1520_IOPMP_MISC_CTRL:
        return s->misc_ctrl;
    case TH1520_IOPMP_DUMMY_ADDR:
        return s->dummy_addr;
    case TH1520_IOPMP_PAGE_LOCK0:
        return s->page_lock0;
    case TH1520_IOPMP_DEFAULT_ATTR_CFG:
        return s->default_attr_cfg;
    default:
        break;
    }

    if (offset >= TH1520_IOPMP_ATTR_CFG0 &&
        offset < TH1520_IOPMP_ATTR_CFG0 +
                 sizeof(s->attr_cfg)) {
        return s->attr_cfg[(offset - TH1520_IOPMP_ATTR_CFG0) / 4];
    }

    if (offset >= TH1520_IOPMP_REGION0_SADDR &&
        offset < TH1520_IOPMP_REGION0_SADDR +
                 TH1520_IOPMP_REGION_COUNT * 8) {
        region = (offset - TH1520_IOPMP_REGION0_SADDR) / 8;
        if (offset & 4) {
            return s->region_end[region];
        }
        return s->region_start[region];
    }

    qemu_log_mask(LOG_UNIMP,
                  "%s: unimplemented register at 0x%03" HWADDR_PRIx "\n",
                  TYPE_TH1520_IOPMP, offset);
    return 0;
}

static void th1520_iopmp_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned int size)
{
    TH1520IOPMPState *s = opaque;
    unsigned int region;

    switch (offset) {
    case TH1520_IOPMP_MISC_CTRL:
        if (!(s->page_lock0 & TH1520_IOPMP_PAGE_LOCK_BYPASS_EN)) {
            s->misc_ctrl = value & TH1520_IOPMP_CTRL_BYPASS;
        }
        return;
    case TH1520_IOPMP_DUMMY_ADDR:
        if (!(s->page_lock0 & TH1520_IOPMP_PAGE_LOCK_DUMMY_ADDR) &&
            !th1520_iopmp_bypass_enabled(s)) {
            s->dummy_addr = value;
        }
        return;
    case TH1520_IOPMP_PAGE_LOCK0:
        s->page_lock0 |= value & TH1520_IOPMP_PAGE_LOCK_MASK;
        return;
    case TH1520_IOPMP_DEFAULT_ATTR_CFG:
        if (!(s->page_lock0 & TH1520_IOPMP_PAGE_LOCK_DEFAULT_CFG) &&
            !th1520_iopmp_bypass_enabled(s)) {
            s->default_attr_cfg = value;
        }
        return;
    default:
        break;
    }

    if (offset >= TH1520_IOPMP_ATTR_CFG0 &&
        offset < TH1520_IOPMP_ATTR_CFG0 +
                 sizeof(s->attr_cfg)) {
        region = (offset - TH1520_IOPMP_ATTR_CFG0) / 4;
        if (!th1520_iopmp_region_locked(s, region) &&
            !th1520_iopmp_bypass_enabled(s)) {
            s->attr_cfg[region] = value;
        }
        return;
    }

    if (offset >= TH1520_IOPMP_REGION0_SADDR &&
        offset < TH1520_IOPMP_REGION0_SADDR +
                 TH1520_IOPMP_REGION_COUNT * 8) {
        region = (offset - TH1520_IOPMP_REGION0_SADDR) / 8;
        if (!th1520_iopmp_region_locked(s, region) &&
            !th1520_iopmp_bypass_enabled(s)) {
            if (offset & 4) {
                s->region_end[region] = value;
            } else {
                s->region_start[region] = value;
            }
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "%s: unimplemented register at 0x%03" HWADDR_PRIx "\n",
                  TYPE_TH1520_IOPMP, offset);
}

static const MemoryRegionOps th1520_iopmp_ops = {
    .read = th1520_iopmp_read,
    .write = th1520_iopmp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void th1520_iopmp_reset(DeviceState *dev)
{
    TH1520IOPMPState *s = TH1520_IOPMP(dev);

    s->misc_ctrl = 0;
    s->dummy_addr = 0;
    s->page_lock0 = 0;
    memset(s->attr_cfg, 0, sizeof(s->attr_cfg));
    s->default_attr_cfg = 0;
    memset(s->region_start, 0, sizeof(s->region_start));
    memset(s->region_end, 0, sizeof(s->region_end));
}

static const VMStateDescription vmstate_th1520_iopmp = {
    .name = TYPE_TH1520_IOPMP,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(misc_ctrl, TH1520IOPMPState),
        VMSTATE_UINT32(dummy_addr, TH1520IOPMPState),
        VMSTATE_UINT32(page_lock0, TH1520IOPMPState),
        VMSTATE_UINT32_ARRAY(attr_cfg, TH1520IOPMPState,
                             TH1520_IOPMP_REGION_COUNT),
        VMSTATE_UINT32(default_attr_cfg, TH1520IOPMPState),
        VMSTATE_UINT32_ARRAY(region_start, TH1520IOPMPState,
                             TH1520_IOPMP_REGION_COUNT),
        VMSTATE_UINT32_ARRAY(region_end, TH1520IOPMPState,
                             TH1520_IOPMP_REGION_COUNT),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_iopmp_init(Object *obj)
{
    TH1520IOPMPState *s = TH1520_IOPMP(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_iopmp_ops, s,
                          TYPE_TH1520_IOPMP, TH1520_IOPMP_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void th1520_iopmp_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 IOPMP configuration window";
    dc->vmsd = &vmstate_th1520_iopmp;
    device_class_set_legacy_reset(dc, th1520_iopmp_reset);
}

static const TypeInfo th1520_iopmp_info = {
    .name = TYPE_TH1520_IOPMP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520IOPMPState),
    .instance_init = th1520_iopmp_init,
    .class_init = th1520_iopmp_class_init,
};

static void th1520_iopmp_register_types(void)
{
    type_register_static(&th1520_iopmp_info);
}

type_init(th1520_iopmp_register_types)
