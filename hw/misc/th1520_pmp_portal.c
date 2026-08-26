/*
 * T-Head TH1520 firmware PMP portal
 *
 * The public vendor DTS names a 4 KiB pmp@ffdc020000 portal.  Public SPL
 * code clears and, in another vendor revision, restores exactly five 32-bit
 * words at offsets 0x000 and 0x100--0x10c.  Keep only those words so the
 * source-backed firmware sequence is accessible.  This is not CPU PMP, a
 * security/enforcement model, or a claim about unknown portal readback,
 * locks, reset values or unrepresented offsets.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_pmp_portal.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static uint64_t th1520_pmp_portal_read(void *opaque, hwaddr offset,
                                       unsigned int size)
{
    TH1520PMPPortalReg *reg = opaque;

    return reg->portal->words[reg->index];
}

static void th1520_pmp_portal_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned int size)
{
    TH1520PMPPortalReg *reg = opaque;

    reg->portal->words[reg->index] = value;
}

static const MemoryRegionOps th1520_pmp_portal_ops = {
    .read = th1520_pmp_portal_read,
    .write = th1520_pmp_portal_write,
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

static const char *const th1520_pmp_portal_names[
    TH1520_PMP_PORTAL_REG_COUNT] = {
    "th1520-pmp-portal-config",
    "th1520-pmp-portal-word-100",
    "th1520-pmp-portal-word-104",
    "th1520-pmp-portal-word-108",
    "th1520-pmp-portal-word-10c",
};

static void th1520_pmp_portal_reset(DeviceState *dev)
{
    TH1520PMPPortalState *s = TH1520_PMP_PORTAL(dev);

    /* Physical reset state is not public; zero is the QEMU convention. */
    memset(s->words, 0, sizeof(s->words));
}

static const VMStateDescription vmstate_th1520_pmp_portal = {
    .name = TYPE_TH1520_PMP_PORTAL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(words, TH1520PMPPortalState,
                             TH1520_PMP_PORTAL_REG_COUNT),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_pmp_portal_init(Object *obj)
{
    TH1520PMPPortalState *s = TH1520_PMP_PORTAL(obj);

    for (int i = 0; i < TH1520_PMP_PORTAL_REG_COUNT; i++) {
        TH1520PMPPortalReg *reg = &s->regs[i];

        reg->portal = s;
        reg->index = i;
        memory_region_init_io(&reg->iomem, obj, &th1520_pmp_portal_ops, reg,
                              th1520_pmp_portal_names[i], sizeof(uint32_t));
        sysbus_init_mmio(SYS_BUS_DEVICE(s), &reg->iomem);
    }
}

static void th1520_pmp_portal_class_init(ObjectClass *klass,
                                         const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 firmware PMP portal words";
    dc->vmsd = &vmstate_th1520_pmp_portal;
    device_class_set_legacy_reset(dc, th1520_pmp_portal_reset);
}

static const TypeInfo th1520_pmp_portal_info = {
    .name = TYPE_TH1520_PMP_PORTAL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520PMPPortalState),
    .instance_init = th1520_pmp_portal_init,
    .class_init = th1520_pmp_portal_class_init,
};

static void th1520_pmp_portal_register_types(void)
{
    type_register_static(&th1520_pmp_portal_info);
}

type_init(th1520_pmp_portal_register_types)
