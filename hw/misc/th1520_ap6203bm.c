/*
 * BeagleV Ahead AP6203BM control/wake peer
 *
 * The public board schematic identifies the AP6203BM module and routes its
 * control/wake pins to GPIO2.  This is only that electrical peer: it forwards
 * five digital control/wake levels and has no SDIO function, WLAN/BT firmware,
 * RF, power, clock or timing model.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_ap6203bm.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "system/reset.h"

static void th1520_ap6203bm_update_outputs(TH1520AP6203BMState *s)
{
    qemu_set_irq(s->control_out[TH1520_AP6203BM_WL_REG_ON], s->wl_reg_on);
    qemu_set_irq(s->control_out[TH1520_AP6203BM_BT_REG_ON], s->bt_reg_on);
    qemu_set_irq(s->control_out[TH1520_AP6203BM_BT_WAKE_HOST],
                 s->bt_wake_host);
    qemu_set_irq(s->host_wake_out[TH1520_AP6203BM_WL_HOST_WAKE],
                 s->wl_host_wake);
    qemu_set_irq(s->host_wake_out[TH1520_AP6203BM_BT_HOST_WAKE],
                 s->bt_host_wake);
}

static void th1520_ap6203bm_set_wl_reg_on(void *opaque, int n, int level)
{
    TH1520AP6203BMState *s = opaque;

    g_assert(n == 0);
    s->wl_reg_on = !!level;
    qemu_set_irq(s->control_out[TH1520_AP6203BM_WL_REG_ON], s->wl_reg_on);
}

static void th1520_ap6203bm_set_bt_reg_on(void *opaque, int n, int level)
{
    TH1520AP6203BMState *s = opaque;

    g_assert(n == 0);
    s->bt_reg_on = !!level;
    qemu_set_irq(s->control_out[TH1520_AP6203BM_BT_REG_ON], s->bt_reg_on);
}

static void th1520_ap6203bm_set_bt_wake_host(void *opaque, int n, int level)
{
    TH1520AP6203BMState *s = opaque;

    g_assert(n == 0);
    s->bt_wake_host = !!level;
    qemu_set_irq(s->control_out[TH1520_AP6203BM_BT_WAKE_HOST],
                 s->bt_wake_host);
}

static void th1520_ap6203bm_set_wl_host_wake(void *opaque, int n, int level)
{
    TH1520AP6203BMState *s = opaque;

    g_assert(n == 0);
    s->wl_host_wake = !!level;
    qemu_set_irq(s->host_wake_out[TH1520_AP6203BM_WL_HOST_WAKE],
                 s->wl_host_wake);
}

static void th1520_ap6203bm_set_bt_host_wake(void *opaque, int n, int level)
{
    TH1520AP6203BMState *s = opaque;

    g_assert(n == 0);
    s->bt_host_wake = !!level;
    qemu_set_irq(s->host_wake_out[TH1520_AP6203BM_BT_HOST_WAKE],
                 s->bt_host_wake);
}

static void th1520_ap6203bm_reset(DeviceState *dev)
{
    TH1520AP6203BMState *s = TH1520_AP6203BM(dev);

    /* Module reset polarity and sequencing need hardware validation. */
    s->wl_reg_on = false;
    s->bt_reg_on = false;
    s->bt_wake_host = false;
    s->wl_host_wake = false;
    s->bt_host_wake = false;
    th1520_ap6203bm_update_outputs(s);
}

static void th1520_ap6203bm_realize(DeviceState *dev, Error **errp)
{
    TH1520AP6203BMState *s = TH1520_AP6203BM(dev);

    /*
     * This board-private peer is a QOM child, not a device on a qbus.
     * Register it explicitly so whole-machine resets reach it.
     */
    qemu_register_resettable(OBJECT(s));

    /* Board children are created after the SoC reset pass. */
    th1520_ap6203bm_update_outputs(s);
}

static void th1520_ap6203bm_unrealize(DeviceState *dev)
{
    qemu_unregister_resettable(OBJECT(dev));
}

static int th1520_ap6203bm_post_load(void *opaque, int version_id)
{
    TH1520AP6203BMState *s = opaque;

    th1520_ap6203bm_update_outputs(s);
    return 0;
}

static const VMStateDescription vmstate_th1520_ap6203bm = {
    .name = TYPE_TH1520_AP6203BM,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = th1520_ap6203bm_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(wl_reg_on, TH1520AP6203BMState),
        VMSTATE_BOOL(bt_reg_on, TH1520AP6203BMState),
        VMSTATE_BOOL(bt_wake_host, TH1520AP6203BMState),
        VMSTATE_BOOL(wl_host_wake, TH1520AP6203BMState),
        VMSTATE_BOOL(bt_host_wake, TH1520AP6203BMState),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_ap6203bm_init(Object *obj)
{
    TH1520AP6203BMState *s = TH1520_AP6203BM(obj);

    qdev_init_gpio_in_named(DEVICE(s), th1520_ap6203bm_set_wl_reg_on,
                            "wl-reg-on-in", 1);
    qdev_init_gpio_in_named(DEVICE(s), th1520_ap6203bm_set_bt_reg_on,
                            "bt-reg-on-in", 1);
    qdev_init_gpio_in_named(DEVICE(s), th1520_ap6203bm_set_bt_wake_host,
                            "bt-wake-host-in", 1);
    qdev_init_gpio_in_named(DEVICE(s), th1520_ap6203bm_set_wl_host_wake,
                            "wl-host-wake-in", 1);
    qdev_init_gpio_in_named(DEVICE(s), th1520_ap6203bm_set_bt_host_wake,
                            "bt-host-wake-in", 1);

    qdev_init_gpio_out_named(DEVICE(s), s->control_out, "control",
                             TH1520_AP6203BM_CONTROL_COUNT);
    qdev_init_gpio_out_named(DEVICE(s), s->host_wake_out, "host-wake",
                             TH1520_AP6203BM_HOST_WAKE_COUNT);
}

static void th1520_ap6203bm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "BeagleV Ahead AP6203BM control/wake peer";
    dc->realize = th1520_ap6203bm_realize;
    dc->unrealize = th1520_ap6203bm_unrealize;
    dc->user_creatable = false;
    dc->vmsd = &vmstate_th1520_ap6203bm;
    device_class_set_legacy_reset(dc, th1520_ap6203bm_reset);
}

static const TypeInfo th1520_ap6203bm_info = {
    .name = TYPE_TH1520_AP6203BM,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(TH1520AP6203BMState),
    .instance_init = th1520_ap6203bm_init,
    .class_init = th1520_ap6203bm_class_init,
};

static void th1520_ap6203bm_register_types(void)
{
    type_register_static(&th1520_ap6203bm_info);
}

type_init(th1520_ap6203bm_register_types)
