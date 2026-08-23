/*
 * T-Head TH1520 pad controllers
 *
 * The three TH1520 pad groups share the same basic programming layout:
 * PADCFG words start at offset 0 and MUXCFG words start at offset 0x400.
 * Most PADCFG words contain two 10-bit pin fields, while each MUXCFG word
 * contains eight 4-bit selectors.  Group 1 also contains a few special pads
 * and holes.
 *
 * This model preserves the digital reset values and reserved-bit behavior.
 * It does not yet turn mux, pull, drive-strength or input-enable changes into
 * electrical signal routing.  Physical pin behavior remains tracked in the
 * BeagleV Ahead hardware-validation ledger.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/misc/th1520_pinctrl.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TH1520_PADCFG_LAST_GROUP2 0x07c
#define TH1520_PADCFG_LAST_GROUP3 0x06c
#define TH1520_MUXCFG_BASE        0x400
#define TH1520_MUXCFG_LAST_GROUP2 0x41c
#define TH1520_MUXCFG_LAST_GROUP3 0x418

#define TH1520_PADCFG_ONE_MASK    0x000003ff
#define TH1520_PADCFG_TWO_MASK    0x03ff03ff
#define TH1520_PADCFG_RESET       0x02080208

static bool th1520_padctrl_pad_info(uint8_t group, unsigned int index,
                                    uint32_t *reset, uint32_t *write_mask)
{
    switch (group) {
    case 1:
        switch (index) {
        case 0:
            *reset = 0x00000125;
            *write_mask = 0x000001ff;
            return true;
        case 1:
            *reset = 0x001a0000;
            *write_mask = 0x001f0000;
            return true;
        case 4:
            *reset = 0x02380000;
            *write_mask = 0x03ff0000;
            return true;
        case 5:
        case 6:
            *reset = 0x02080238;
            break;
        case 7:
            *reset = 0x02380208;
            break;
        case 8:
            *reset = 0x02080218;
            break;
        case 9:
            *reset = 0x02180208;
            break;
        case 10 ... 22:
            *reset = TH1520_PADCFG_RESET;
            break;
        case 23:
            *reset = 0x00000208;
            *write_mask = TH1520_PADCFG_ONE_MASK;
            return true;
        default:
            return false;
        }
        *write_mask = TH1520_PADCFG_TWO_MASK;
        return true;
    case 2:
        if (index > TH1520_PADCFG_LAST_GROUP2 / 4) {
            return false;
        }
        *reset = TH1520_PADCFG_RESET;
        if (index == 3 || index == 4 || index == 14) {
            *reset = 0x02380238;
        } else if (index == 13) {
            *reset = 0x02380208;
        } else if (index == TH1520_PADCFG_LAST_GROUP2 / 4) {
            *reset = 0x00000208;
            *write_mask = TH1520_PADCFG_ONE_MASK;
            return true;
        }
        *write_mask = TH1520_PADCFG_TWO_MASK;
        return true;
    case 3:
        if (index > TH1520_PADCFG_LAST_GROUP3 / 4) {
            return false;
        }
        if (index == TH1520_PADCFG_LAST_GROUP3 / 4) {
            *reset = 0x00000208;
            *write_mask = TH1520_PADCFG_ONE_MASK;
            return true;
        }
        *reset = TH1520_PADCFG_RESET;
        *write_mask = TH1520_PADCFG_TWO_MASK;
        return true;
    default:
        return false;
    }
}

static bool th1520_padctrl_mux_info(uint8_t group, unsigned int index,
                                    uint32_t *reset, uint32_t *write_mask)
{
    static const uint32_t group1_masks[] = {
        0xf0000000, 0xfffffff0, 0xffffffff,
        0xffffffff, 0xffffffff, 0x0fffffff,
    };
    unsigned int count;

    *reset = 0;
    switch (group) {
    case 1:
        count = ARRAY_SIZE(group1_masks);
        if (index >= count) {
            return false;
        }
        *write_mask = group1_masks[index];
        return true;
    case 2:
        count = 1 + (TH1520_MUXCFG_LAST_GROUP2 -
                     TH1520_MUXCFG_BASE) / 4;
        break;
    case 3:
        count = 1 + (TH1520_MUXCFG_LAST_GROUP3 -
                     TH1520_MUXCFG_BASE) / 4;
        break;
    default:
        return false;
    }

    if (index >= count) {
        return false;
    }
    *write_mask = index == count - 1 ? 0x0fffffff : UINT32_MAX;
    return true;
}

static bool th1520_padctrl_reg_info(TH1520PadCtrlState *s, hwaddr offset,
                                    uint32_t *reset, uint32_t *write_mask)
{
    if (offset & 3) {
        return false;
    }

    if (offset < TH1520_MUXCFG_BASE) {
        return th1520_padctrl_pad_info(s->pad_group, offset / 4,
                                       reset, write_mask);
    }
    if (offset < TH1520_PADCTRL_REG_END) {
        return th1520_padctrl_mux_info(s->pad_group,
                                       (offset - TH1520_MUXCFG_BASE) / 4,
                                       reset, write_mask);
    }
    return false;
}

static uint64_t th1520_padctrl_read(void *opaque, hwaddr offset,
                                    unsigned int size)
{
    TH1520PadCtrlState *s = opaque;
    uint32_t reset;
    uint32_t write_mask;

    if (!th1520_padctrl_reg_info(s, offset, &reset, &write_mask)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented group %u register at 0x%03"
                      HWADDR_PRIx "\n", TYPE_TH1520_PADCTRL,
                      s->pad_group, offset);
        return 0;
    }

    return s->regs[offset / 4];
}

static void th1520_padctrl_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned int size)
{
    TH1520PadCtrlState *s = opaque;
    uint32_t reset;
    uint32_t write_mask;
    uint32_t old;

    if (!th1520_padctrl_reg_info(s, offset, &reset, &write_mask)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented group %u register at 0x%03"
                      HWADDR_PRIx "\n", TYPE_TH1520_PADCTRL,
                      s->pad_group, offset);
        return;
    }

    old = s->regs[offset / 4];
    s->regs[offset / 4] = (old & ~write_mask) |
                          ((uint32_t)value & write_mask);
}

static const MemoryRegionOps th1520_padctrl_ops = {
    .read = th1520_padctrl_read,
    .write = th1520_padctrl_write,
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

static void th1520_padctrl_reset(DeviceState *dev)
{
    TH1520PadCtrlState *s = TH1520_PADCTRL(dev);

    memset(s->regs, 0, sizeof(s->regs));
    for (hwaddr offset = 0; offset < TH1520_PADCTRL_REG_END; offset += 4) {
        uint32_t reset;
        uint32_t write_mask;

        if (th1520_padctrl_reg_info(s, offset, &reset, &write_mask)) {
            s->regs[offset / 4] = reset;
        }
    }
}

static const VMStateDescription vmstate_th1520_padctrl = {
    .name = TYPE_TH1520_PADCTRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, TH1520PadCtrlState,
                             TH1520_PADCTRL_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_padctrl_realize(DeviceState *dev, Error **errp)
{
    TH1520PadCtrlState *s = TH1520_PADCTRL(dev);

    if (s->pad_group < 1 || s->pad_group > 3) {
        error_setg(errp, "pad-group must be between 1 and 3");
        return;
    }

    memory_region_set_size(&s->iomem,
                           s->pad_group == 1 ? 0x2000 : 0x1000);
}

static const Property th1520_padctrl_properties[] = {
    DEFINE_PROP_UINT8("pad-group", TH1520PadCtrlState, pad_group, 0),
};

static void th1520_padctrl_init(Object *obj)
{
    TH1520PadCtrlState *s = TH1520_PADCTRL(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_padctrl_ops, s,
                          TYPE_TH1520_PADCTRL,
                          TH1520_PADCTRL_MAX_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void th1520_padctrl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 pad controller";
    dc->realize = th1520_padctrl_realize;
    dc->vmsd = &vmstate_th1520_padctrl;
    device_class_set_legacy_reset(dc, th1520_padctrl_reset);
    device_class_set_props(dc, th1520_padctrl_properties);
}

static const TypeInfo th1520_padctrl_info = {
    .name = TYPE_TH1520_PADCTRL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520PadCtrlState),
    .instance_init = th1520_padctrl_init,
    .class_init = th1520_padctrl_class_init,
};

static void th1520_padctrl_register_types(void)
{
    type_register_static(&th1520_padctrl_info);
}

type_init(th1520_padctrl_register_types)
