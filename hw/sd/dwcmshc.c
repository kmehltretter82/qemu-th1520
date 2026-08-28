/*
 * Synopsys DesignWare Cores Mobile Storage Host Controller
 *
 * This models the SDHCI-compatible command and DMA engine through QEMU's
 * generic SDHCI device and the DWC MSHC vendor/PHY integration used by the
 * T-Head TH1520.  CQE and the mask-ROM boot datapath are deliberately not
 * advertised to a guest device tree until their execution engines exist.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/sd/dwcmshc.h"
#include "migration/vmstate.h"
#include "hw/core/qdev-clock.h"

#define DWCMSHC_VENDOR_POINTER          0x0e8
#define DWCMSHC_VENDOR_WINDOW           0x100

#define DWCMSHC_PHY_BASE                0x300
#define DWCMSHC_PHY_CNFG                0x300
#define DWCMSHC_PHY_DLL_CTRL            0x324
#define DWCMSHC_PHY_DLL_STATUS          0x32e
#define DWCMSHC_PHY_DLLDBG_MLKDC        0x330
#define DWCMSHC_PHY_DLLDBG_SLKDC        0x332

#define DWCMSHC_MSHC_VER_ID             0x500
#define DWCMSHC_MSHC_VER_TYPE           0x504
#define DWCMSHC_MSHC_CTRL               0x508
#define DWCMSHC_MBIU_CTRL               0x510
#define DWCMSHC_EMMC_CTRL               0x52c
#define DWCMSHC_BOOT_CTRL               0x52e
#define DWCMSHC_AT_CTRL                 0x540
#define DWCMSHC_AT_STAT                 0x544
#define DWCMSHC_EMBEDDED_CTRL           0xf6c

#define DWCMSHC_PHY_RSTN                BIT(0)
#define DWCMSHC_PHY_PWRGOOD             BIT(1)
#define DWCMSHC_DLL_ENABLE              BIT(0)
#define DWCMSHC_DLL_UPDATE              BIT(2)
#define DWCMSHC_DLL_LOCK                BIT(0)

/*
 * Defaults reconstructed from the publicly hosted TH1520 manual.  The
 * synthesis version strings are redacted there and remain zero by default;
 * the capability voltage bits still require a physical comparison.  Keep
 * those uncertainties tied to ledger item SD-001 rather than implying that
 * they were measured on the board.
 */
#define DWCMSHC_CAPABILITIES_RESET       0x080081773f6dc881ULL
#define DWCMSHC_MAX_CURRENT_RESET        0x0000000000191919ULL
#define DWCMSHC_HOST_VERSION_420         0x0005
#define DWCMSHC_AT_CTRL_RESET            0x03000005
#define DWCMSHC_AT_STAT_RESET            0x00000006
#define DWCMSHC_EMBEDDED_CTRL_WR_MASK    0x7f770000

static const uint8_t dwcmshc_phy_write_mask[DWCMSHC_PHY_SIZE] = {
    [0x00] = 0x01,
    [0x02] = 0xff,
    [0x04] = 0xff, [0x05] = 0x1f,
    [0x06] = 0xff, [0x07] = 0x1f,
    [0x08] = 0xff, [0x09] = 0x1f,
    [0x0a] = 0xff, [0x0b] = 0x1f,
    [0x0c] = 0xff, [0x0d] = 0x1f,
    [0x0e] = 0xf1, [0x0f] = 0x03,
    [0x10] = 0x3f,
    [0x18] = 0xff, [0x19] = 0xff,
    [0x1a] = 0x03,
    [0x1c] = 0xff,
    [0x1d] = 0x1f,
    [0x1e] = 0x7f,
    [0x20] = 0x1f,
    [0x21] = 0x0f,
    [0x24] = 0x07,
    [0x25] = 0x37,
    [0x26] = 0x7f,
    [0x28] = 0xff,
    [0x29] = 0x7f,
    [0x2a] = 0x7f,
    [0x2c] = 0xff, [0x2d] = 0xff,
};

static uint8_t dwcmshc_read_byte(DWCMSHCState *s, hwaddr addr,
                                 bool *implemented)
{
    *implemented = true;

    if (addr >= DWCMSHC_PHY_BASE &&
        addr < DWCMSHC_PHY_BASE + DWCMSHC_PHY_SIZE) {
        return s->phy[addr - DWCMSHC_PHY_BASE];
    }

    if (addr >= DWCMSHC_MSHC_VER_ID && addr < DWCMSHC_MSHC_VER_ID + 4) {
        return extract32(s->version_id,
                         (addr - DWCMSHC_MSHC_VER_ID) * 8, 8);
    }
    if (addr >= DWCMSHC_MSHC_VER_TYPE &&
        addr < DWCMSHC_MSHC_VER_TYPE + 4) {
        return extract32(s->version_type,
                         (addr - DWCMSHC_MSHC_VER_TYPE) * 8, 8);
    }

    switch (addr) {
    case DWCMSHC_MSHC_CTRL:
        return s->mshc_ctrl;
    case DWCMSHC_MBIU_CTRL:
        return s->mbiu_ctrl;
    case DWCMSHC_EMMC_CTRL ... DWCMSHC_EMMC_CTRL + 1:
        return extract16(s->emmc_ctrl, (addr - DWCMSHC_EMMC_CTRL) * 8, 8);
    case DWCMSHC_BOOT_CTRL ... DWCMSHC_BOOT_CTRL + 1:
        return extract16(s->boot_ctrl, (addr - DWCMSHC_BOOT_CTRL) * 8, 8);
    case DWCMSHC_AT_CTRL ... DWCMSHC_AT_CTRL + 3:
        return extract32(s->at_ctrl, (addr - DWCMSHC_AT_CTRL) * 8, 8);
    case DWCMSHC_AT_STAT ... DWCMSHC_AT_STAT + 3:
        return extract32(s->at_stat, (addr - DWCMSHC_AT_STAT) * 8, 8);
    case DWCMSHC_EMBEDDED_CTRL ... DWCMSHC_EMBEDDED_CTRL + 3:
        return extract32(s->embedded_ctrl,
                         (addr - DWCMSHC_EMBEDDED_CTRL) * 8, 8);
    default:
        *implemented = false;
        return 0;
    }
}

static void dwcmshc_update_dll(DWCMSHCState *s)
{
    bool enabled = (s->phy[DWCMSHC_PHY_DLL_CTRL - DWCMSHC_PHY_BASE] &
                    DWCMSHC_DLL_ENABLE) &&
                   (s->phy[DWCMSHC_PHY_CNFG - DWCMSHC_PHY_BASE] &
                    DWCMSHC_PHY_PWRGOOD);

    s->phy[DWCMSHC_PHY_DLL_STATUS - DWCMSHC_PHY_BASE] =
        enabled ? DWCMSHC_DLL_LOCK : 0;
    s->phy[DWCMSHC_PHY_DLLDBG_MLKDC - DWCMSHC_PHY_BASE] =
        enabled ? s->phy[0x1e] & 0x7f : 0;
    s->phy[DWCMSHC_PHY_DLLDBG_SLKDC - DWCMSHC_PHY_BASE] =
        enabled ? s->phy[0x1e] & 0x7f : 0;
}

static bool dwcmshc_write_phy_byte(DWCMSHCState *s, hwaddr addr,
                                   uint8_t value)
{
    unsigned int offset = addr - DWCMSHC_PHY_BASE;
    uint8_t mask;

    if (offset >= DWCMSHC_PHY_SIZE) {
        return false;
    }

    mask = dwcmshc_phy_write_mask[offset];
    if (!mask) {
        return true;
    }

    s->phy[offset] = (s->phy[offset] & ~mask) | (value & mask);

    if (addr == DWCMSHC_PHY_CNFG) {
        if (s->phy[offset] & DWCMSHC_PHY_RSTN) {
            s->phy[offset] |= DWCMSHC_PHY_PWRGOOD;
        } else {
            s->phy[offset] &= ~DWCMSHC_PHY_PWRGOOD;
        }
        dwcmshc_update_dll(s);
    } else if (addr == DWCMSHC_PHY_DLL_CTRL) {
        /* There is no analog delay in the model, so update completes now. */
        s->phy[offset] &= ~DWCMSHC_DLL_UPDATE;
        dwcmshc_update_dll(s);
    }

    return true;
}

static bool dwcmshc_write_byte(DWCMSHCState *s, hwaddr addr, uint8_t value)
{
    uint8_t shift;

    if (addr >= DWCMSHC_PHY_BASE &&
        addr < DWCMSHC_PHY_BASE + DWCMSHC_PHY_SIZE) {
        return dwcmshc_write_phy_byte(s, addr, value);
    }

    switch (addr) {
    case DWCMSHC_MSHC_CTRL:
        s->mshc_ctrl = value & (BIT(4) | BIT(0));
        return true;
    case DWCMSHC_MBIU_CTRL:
        s->mbiu_ctrl = value & 0x0f;
        return true;
    case DWCMSHC_EMMC_CTRL ... DWCMSHC_EMMC_CTRL + 1:
        shift = (addr - DWCMSHC_EMMC_CTRL) * 8;
        s->emmc_ctrl = deposit32(s->emmc_ctrl, shift, 8,
                                 value & (shift ? 0x07 : 0x0f));
        return true;
    case DWCMSHC_BOOT_CTRL:
        /*
         * VALIDATE_BOOT is a strobe and MAN_BOOT_EN changes only when that
         * strobe accompanies it.  The mask-ROM boot engine is not modeled,
         * so fail loud and complete the strobe without inventing active boot
         * state.  A bare MAN_BOOT_EN write is specified to be ignored.
         */
        if ((value & BIT(7)) && (value & BIT(0))) {
            qemu_log_mask(LOG_UNIMP,
                          "DWC MSHC mandatory boot is not implemented\n");
        }
        return true;
    case DWCMSHC_BOOT_CTRL + 1:
        s->boot_ctrl = deposit32(s->boot_ctrl, 8, 8, value & 0xf1);
        return true;
    case DWCMSHC_AT_CTRL ... DWCMSHC_AT_CTRL + 3: {
        const uint32_t mask = 0x7f1f0f1f;

        shift = (addr - DWCMSHC_AT_CTRL) * 8;
        s->at_ctrl = deposit32(s->at_ctrl, shift, 8,
                               value & extract32(mask, shift, 8));
        return true;
    }
    case DWCMSHC_AT_STAT:
        /* CENTER_PH_CODE is writable only during software-managed tuning. */
        if (s->at_ctrl & BIT(4)) {
            s->at_stat = deposit32(s->at_stat, 0, 8, value);
        }
        return true;
    case DWCMSHC_AT_STAT + 1 ... DWCMSHC_AT_STAT + 3:
        return true;
    case DWCMSHC_EMBEDDED_CTRL ... DWCMSHC_EMBEDDED_CTRL + 3:
        shift = (addr - DWCMSHC_EMBEDDED_CTRL) * 8;
        s->embedded_ctrl = deposit32(
            s->embedded_ctrl, shift, 8,
            value & extract32(DWCMSHC_EMBEDDED_CTRL_WR_MASK, shift, 8));
        return true;
    case DWCMSHC_MSHC_VER_ID ... DWCMSHC_MSHC_VER_ID + 3:
    case DWCMSHC_MSHC_VER_TYPE ... DWCMSHC_MSHC_VER_TYPE + 3:
        return true;
    default:
        return false;
    }
}

static uint64_t dwcmshc_pointer_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    DWCMSHCState *s = opaque;
    uint32_t pointers = s->vendor_area1 | (s->vendor_area2 << 16);

    return extract32(pointers, addr * 8, size * 8);
}

static void dwcmshc_pointer_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned int size)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "DWC MSHC write to read-only vendor pointer +0x%02"
                  HWADDR_PRIx "\n", addr);
}

static const MemoryRegionOps dwcmshc_pointer_ops = {
    .read = dwcmshc_pointer_read,
    .write = dwcmshc_pointer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static uint64_t dwcmshc_vendor_read(void *opaque, hwaddr offset,
                                    unsigned int size)
{
    DWCMSHCState *s = opaque;
    hwaddr addr = offset + DWCMSHC_VENDOR_WINDOW;
    uint64_t value = 0;
    bool all_implemented = true;

    for (unsigned int i = 0; i < size; i++) {
        bool implemented;
        uint8_t byte = dwcmshc_read_byte(s, addr + i, &implemented);

        value |= (uint64_t)byte << (i * 8);
        all_implemented &= implemented;
    }

    if (!all_implemented) {
        qemu_log_mask(LOG_UNIMP,
                      "DWC MSHC read_%u at 0x%04" HWADDR_PRIx
                      " is not implemented\n", size * 8, addr);
    }

    return value;
}

static void dwcmshc_vendor_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned int size)
{
    DWCMSHCState *s = opaque;
    hwaddr addr = offset + DWCMSHC_VENDOR_WINDOW;
    bool all_implemented = true;

    for (unsigned int i = 0; i < size; i++) {
        all_implemented &= dwcmshc_write_byte(s, addr + i,
                                              value >> (i * 8));
    }

    if (!all_implemented) {
        qemu_log_mask(LOG_UNIMP,
                      "DWC MSHC write_%u at 0x%04" HWADDR_PRIx
                      " is not implemented\n", size * 8, addr);
    }
}

static const MemoryRegionOps dwcmshc_vendor_ops = {
    .read = dwcmshc_vendor_read,
    .write = dwcmshc_vendor_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void dwcmshc_reset(DeviceState *dev)
{
    DWCMSHCState *s = DWC_MSHC(dev);

    device_cold_reset(DEVICE(&s->sdhci));

    s->mshc_ctrl = BIT(0);
    s->mbiu_ctrl = 0x0f;
    s->emmc_ctrl = BIT(3) | BIT(2);
    s->boot_ctrl = 0;
    s->at_ctrl = DWCMSHC_AT_CTRL_RESET;
    s->at_stat = DWCMSHC_AT_STAT_RESET;
    s->embedded_ctrl = 0;

    memset(s->phy, 0, sizeof(s->phy));
    stw_le_p(&s->phy[0x04], 0x0440);
    stw_le_p(&s->phy[0x06], 0x0440);
    stw_le_p(&s->phy[0x08], 0x0440);
    stw_le_p(&s->phy[0x0a], 0x0440);
    stw_le_p(&s->phy[0x0c], 0x0440);
    stw_le_p(&s->phy[0x18], 0xffff);
    s->phy[0x20] = 0x0e;
}

static void dwcmshc_realize(DeviceState *dev, Error **errp)
{
    DWCMSHCState *s = DWC_MSHC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusDevice *sdhci_sbd = SYS_BUS_DEVICE(&s->sdhci);

    if (s->vendor_area1 > 0x0fff || s->vendor_area2 > 0x0fff) {
        error_setg(errp, "DWC MSHC vendor pointers must fit in 12 bits");
        return;
    }

    qdev_prop_set_uint8(DEVICE(&s->sdhci), "sd-spec-version", 4);
    qdev_prop_set_uint8(DEVICE(&s->sdhci), "uhs", UHS_I);
    qdev_prop_set_uint64(DEVICE(&s->sdhci), "capareg", s->capareg);
    qdev_prop_set_uint64(DEVICE(&s->sdhci), "maxcurr", s->maxcurr);

    if (!sysbus_realize(sdhci_sbd, errp)) {
        return;
    }
    s->sdhci.version = s->host_version;

    memory_region_init(&s->container, OBJECT(s), "dwcmshc.container",
                       DWCMSHC_REG_SIZE);
    sysbus_init_mmio(sbd, &s->container);
    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(sdhci_sbd, 0));

    memory_region_init_io(&s->pointer_iomem, OBJECT(s),
                          &dwcmshc_pointer_ops, s,
                          "dwcmshc.vendor-pointer", 4);
    memory_region_add_subregion_overlap(&s->container,
                                        DWCMSHC_VENDOR_POINTER,
                                        &s->pointer_iomem, 1);

    memory_region_init_io(&s->vendor_iomem, OBJECT(s), &dwcmshc_vendor_ops,
                          s, "dwcmshc.vendor",
                          DWCMSHC_REG_SIZE - DWCMSHC_VENDOR_WINDOW);
    memory_region_add_subregion(&s->container, DWCMSHC_VENDOR_WINDOW,
                                &s->vendor_iomem);

    sysbus_pass_irq(sbd, sdhci_sbd);
    s->bus = qdev_get_child_bus(DEVICE(sdhci_sbd), "sd-bus");
}

static bool dwcmshc_core_clock_needed(void *opaque)
{
    DWCMSHCState *s = opaque;

    return clock_has_source(s->core_clk);
}

/* Emitted only by an integration that connects the core clock. */
static const VMStateDescription vmstate_dwcmshc_core_clock = {
    .name = TYPE_DWC_MSHC "/core-clock",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = dwcmshc_core_clock_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(core_clk, DWCMSHCState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_dwcmshc = {
    .name = TYPE_DWC_MSHC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(mshc_ctrl, DWCMSHCState),
        VMSTATE_UINT8(mbiu_ctrl, DWCMSHCState),
        VMSTATE_UINT16(emmc_ctrl, DWCMSHCState),
        VMSTATE_UINT16(boot_ctrl, DWCMSHCState),
        VMSTATE_UINT32(at_ctrl, DWCMSHCState),
        VMSTATE_UINT32(at_stat, DWCMSHCState),
        VMSTATE_UINT32(embedded_ctrl, DWCMSHCState),
        VMSTATE_UINT8_ARRAY(phy, DWCMSHCState, DWCMSHC_PHY_SIZE),
        VMSTATE_END_OF_LIST(),
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_dwcmshc_core_clock,
        NULL
    },
};

static const Property dwcmshc_properties[] = {
    DEFINE_PROP_UINT64("capareg", DWCMSHCState, capareg,
                       DWCMSHC_CAPABILITIES_RESET),
    DEFINE_PROP_UINT64("maxcurr", DWCMSHCState, maxcurr,
                       DWCMSHC_MAX_CURRENT_RESET),
    DEFINE_PROP_UINT32("mshc-version-id", DWCMSHCState, version_id, 0),
    DEFINE_PROP_UINT32("mshc-version-type", DWCMSHCState, version_type, 0),
    DEFINE_PROP_UINT16("host-version", DWCMSHCState, host_version,
                       DWCMSHC_HOST_VERSION_420),
    DEFINE_PROP_UINT16("vendor-area1", DWCMSHCState, vendor_area1, 0x500),
    DEFINE_PROP_UINT16("vendor-area2", DWCMSHCState, vendor_area2, 0x180),
};

static void dwcmshc_core_clock_update(void *opaque, ClockEvent event)
{
    DWCMSHCState *s = opaque;

    if (event == ClockUpdate && clock_get_hz(s->core_clk)) {
        sdhci_core_clock_resumed(&s->sdhci);
    }
}

static void dwcmshc_init(Object *obj)
{
    DWCMSHCState *s = DWC_MSHC(obj);

    object_initialize_child(obj, "sdhci", &s->sdhci, TYPE_SYSBUS_SDHCI);
    /* Created at init so the board can connect it before realize. */
    s->core_clk = qdev_init_clock_in(DEVICE(obj), "core",
                                     dwcmshc_core_clock_update, s,
                                     ClockUpdate);
    s->sdhci.core_clk = s->core_clk;
}

static void dwcmshc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "Synopsys DesignWare Cores Mobile Storage Host Controller";
    dc->realize = dwcmshc_realize;
    device_class_set_legacy_reset(dc, dwcmshc_reset);
    device_class_set_props(dc, dwcmshc_properties);
    dc->vmsd = &vmstate_dwcmshc;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo dwcmshc_info = {
    .name = TYPE_DWC_MSHC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWCMSHCState),
    .instance_init = dwcmshc_init,
    .class_init = dwcmshc_class_init,
};

static void dwcmshc_register_types(void)
{
    type_register_static(&dwcmshc_info);
}

type_init(dwcmshc_register_types)
