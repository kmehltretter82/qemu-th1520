/*
 * T-Head TH1520 mailbox controller
 *
 * Linux describes four resources: one 24 KiB local aperture and three
 * remote-ICU apertures.  Its driver uses four 4 KiB local channel windows;
 * remote ICU 0 starts 16 KiB into its resource, while remote ICUs 1 and 2
 * start at their resource bases.  This model implements the CPU-visible
 * registers exercised by that driver: status/clear/mask in the local CPU0
 * window, and generate plus eight INFO words in every used channel window.
 *
 * The remote E902, C906 and C910R endpoints are not yet modeled.  Their
 * register windows are retained so a guest can send messages without an
 * invented firmware response.  The three ``remote-event`` GPIO inputs let a
 * later endpoint model inject a local event after populating the matching
 * local channel window.  They deliberately do not define an AON firmware or
 * remote-ICU protocol.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_mbox.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TH1520_MBOX_STATUS            0x000
#define TH1520_MBOX_CLEAR             0x004
#define TH1520_MBOX_MASK              0x00c
#define TH1520_MBOX_GENERATE          0x010
#define TH1520_MBOX_INFO0             0x014
#define TH1520_MBOX_INFO_LAST          0x030

#define TH1520_MBOX_REMOTE0_OFFSET     0x4000

#define TH1520_MBOX_CHANNEL_MASK       \
    MAKE_64BIT_MASK(0, TH1520_MBOX_REMOTE_CHANNELS)
#define TH1520_MBOX_GENERATE_MASK      0xff

static void th1520_mbox_update_irq(TH1520MboxState *s)
{
    qemu_set_irq(s->irq, s->status & s->mask);
}

static bool th1520_mbox_window_channel(TH1520MboxWindow *window,
                                        hwaddr *offset,
                                        unsigned int *channel)
{
    if (window->local) {
        if (*offset >= TH1520_MBOX_CHANNELS * TH1520_MBOX_CHANNEL_SIZE) {
            return false;
        }
        *channel = *offset / TH1520_MBOX_CHANNEL_SIZE;
        *offset %= TH1520_MBOX_CHANNEL_SIZE;
        return true;
    }

    if (*offset < window->register_offset ||
        *offset >= window->register_offset + TH1520_MBOX_CHANNEL_SIZE) {
        return false;
    }
    *offset -= window->register_offset;
    *channel = window->channel;
    return true;
}

static uint64_t th1520_mbox_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    TH1520MboxWindow *window = opaque;
    TH1520MboxState *s = window->parent;
    unsigned int channel;

    if (!th1520_mbox_window_channel(window, &offset, &channel)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%" HWADDR_PRIx
                      "\n", TYPE_TH1520_MBOX, offset);
        return 0;
    }

    switch (offset) {
    case TH1520_MBOX_STATUS:
        return channel ? 0 : s->status;
    case TH1520_MBOX_CLEAR:
        return 0;
    case TH1520_MBOX_MASK:
        return channel ? 0 : s->mask;
    case TH1520_MBOX_GENERATE:
        return s->generate[channel];
    case TH1520_MBOX_INFO0 ... TH1520_MBOX_INFO_LAST:
        return s->info[channel][(offset - TH1520_MBOX_INFO0) / 4];
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_MBOX, offset);
        return 0;
    }
}

static void th1520_mbox_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned int size)
{
    TH1520MboxWindow *window = opaque;
    TH1520MboxState *s = window->parent;
    unsigned int channel;

    if (!th1520_mbox_window_channel(window, &offset, &channel)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%" HWADDR_PRIx
                      "\n", TYPE_TH1520_MBOX, offset);
        return;
    }

    switch (offset) {
    case TH1520_MBOX_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to status register\n", TYPE_TH1520_MBOX);
        return;
    case TH1520_MBOX_CLEAR:
        if (!channel) {
            s->status &= ~value;
            th1520_mbox_update_irq(s);
        }
        return;
    case TH1520_MBOX_MASK:
        if (!channel) {
            s->mask = value & TH1520_MBOX_CHANNEL_MASK;
            th1520_mbox_update_irq(s);
        }
        return;
    case TH1520_MBOX_GENERATE:
        s->generate[channel] = value & TH1520_MBOX_GENERATE_MASK;
        return;
    case TH1520_MBOX_INFO0 ... TH1520_MBOX_INFO_LAST:
        s->info[channel][(offset - TH1520_MBOX_INFO0) / 4] = value;
        return;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register at 0x%03" HWADDR_PRIx
                      "\n", TYPE_TH1520_MBOX, offset);
    }
}

static const MemoryRegionOps th1520_mbox_ops = {
    .read = th1520_mbox_read,
    .write = th1520_mbox_write,
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

static void th1520_mbox_remote_event(void *opaque, int n, int level)
{
    TH1520MboxState *s = opaque;

    if (level) {
        s->status |= BIT(n);
        th1520_mbox_update_irq(s);
    }
}

static void th1520_mbox_reset(DeviceState *dev)
{
    TH1520MboxState *s = TH1520_MBOX(dev);

    memset(s->info, 0, sizeof(s->info));
    memset(s->generate, 0, sizeof(s->generate));
    s->status = 0;
    s->mask = 0;
    th1520_mbox_update_irq(s);
}

static int th1520_mbox_post_load(void *opaque, int version_id)
{
    TH1520MboxState *s = opaque;

    th1520_mbox_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_th1520_mbox = {
    .name = TYPE_TH1520_MBOX,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = th1520_mbox_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_2DARRAY(info, TH1520MboxState,
                               TH1520_MBOX_CHANNELS,
                               TH1520_MBOX_INFO_WORDS),
        VMSTATE_UINT8_ARRAY(generate, TH1520MboxState,
                            TH1520_MBOX_CHANNELS),
        VMSTATE_UINT8(status, TH1520MboxState),
        VMSTATE_UINT8(mask, TH1520MboxState),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_mbox_init_window(TH1520MboxWindow *window,
                                    TH1520MboxState *s, const char *name,
                                    uint32_t size, bool local,
                                    uint8_t channel, uint16_t register_offset)
{
    window->parent = s;
    window->local = local;
    window->channel = channel;
    window->register_offset = register_offset;
    memory_region_init_io(&window->iomem, OBJECT(s), &th1520_mbox_ops,
                          window, name, size);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &window->iomem);
}

static void th1520_mbox_init(Object *obj)
{
    TH1520MboxState *s = TH1520_MBOX(obj);

    th1520_mbox_init_window(&s->local, s, "th1520-mbox.local",
                            TH1520_MBOX_LOCAL_MMIO_SIZE, true, 0, 0);
    th1520_mbox_init_window(&s->remote[0], s, "th1520-mbox.remote-icu0",
                            TH1520_MBOX_REMOTE0_MMIO_SIZE, false, 1,
                            TH1520_MBOX_REMOTE0_OFFSET);
    th1520_mbox_init_window(&s->remote[1], s, "th1520-mbox.remote-icu1",
                            TH1520_MBOX_REMOTE1_MMIO_SIZE, false, 2, 0);
    th1520_mbox_init_window(&s->remote[2], s, "th1520-mbox.remote-icu2",
                            TH1520_MBOX_REMOTE2_MMIO_SIZE, false, 3, 0);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(s), th1520_mbox_remote_event,
                            "remote-event", TH1520_MBOX_REMOTE_CHANNELS);
}

static void th1520_mbox_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 mailbox controller";
    dc->vmsd = &vmstate_th1520_mbox;
    device_class_set_legacy_reset(dc, th1520_mbox_reset);
}

static const TypeInfo th1520_mbox_info = {
    .name = TYPE_TH1520_MBOX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520MboxState),
    .instance_init = th1520_mbox_init,
    .class_init = th1520_mbox_class_init,
};

static void th1520_mbox_register_types(void)
{
    type_register_static(&th1520_mbox_info);
}

type_init(th1520_mbox_register_types)
