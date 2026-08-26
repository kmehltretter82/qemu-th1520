/*
 * T-Head TH1520 DDR controller and PHY state
 *
 * The public vendor SPL for Light/TH1520 configures two DesignWare DDR PHYs
 * with 16-bit CSR writes and a DesignWare uMCTL2 controller with 32-bit
 * writes.  It loads the PHY instruction/data memories, starts each firmware
 * training pass through CSR 0xd0099, consumes a mailbox completion value of
 * 0x7, then asks the controller to begin DFI initialization and polls its
 * DFI and normal-mode status words.
 *
 * This model retains the source-visible controller and PHY register state
 * exercised by that path.  A completed training trigger produces the final
 * mailbox result immediately, and two completed PHYs make the controller's
 * DFI/normal-mode polling transitions visible.  These are firmware
 * compatibility conventions, not an implementation of PHY firmware, LPDDR4
 * training, DFI traffic, DRAM timing, analog calibration, or physical reset
 * values.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/th1520_ddr.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

#define TH1520_DDR_CTRL_DFI_INIT_COMPLETE_EN  0x00000001
#define TH1520_DDR_CTRL_DFI_INIT_START        0x00000020
#define TH1520_DDR_CTRL_SW_DONE               0x00000001
#define TH1520_DDR_CTRL_NORMAL_MODE           0x00000001

static bool th1520_ddr_controller_training_complete(
    TH1520DDRControllerState *s)
{
    return s->phy[0] && s->phy[1] && s->phy[0]->training_complete &&
           s->phy[1]->training_complete;
}

static bool th1520_ddr_controller_dfi_complete(TH1520DDRControllerState *s)
{
    return s->dfi_initialized ||
           (th1520_ddr_controller_training_complete(s) &&
           (s->regs[TH1520_DDR_CTRL_DFIMISC / sizeof(uint32_t)] &
            TH1520_DDR_CTRL_DFI_INIT_START));
}

static bool th1520_ddr_controller_normal_mode(TH1520DDRControllerState *s)
{
    return th1520_ddr_controller_dfi_complete(s) &&
           (s->regs[TH1520_DDR_CTRL_DFIMISC / sizeof(uint32_t)] &
            TH1520_DDR_CTRL_DFI_INIT_COMPLETE_EN) &&
           (s->regs[TH1520_DDR_CTRL_SWCTL / sizeof(uint32_t)] &
            TH1520_DDR_CTRL_SW_DONE);
}

static uint64_t th1520_ddr_controller_read(void *opaque, hwaddr offset,
                                           unsigned int size)
{
    TH1520DDRControllerState *s = opaque;

    switch (offset) {
    case TH1520_DDR_CTRL_STAT:
    case TH1520_DDR_CTRL_DCH1_STAT:
        return th1520_ddr_controller_normal_mode(s) ?
               TH1520_DDR_CTRL_NORMAL_MODE : 0;
    case TH1520_DDR_CTRL_DFISTAT:
    case TH1520_DDR_CTRL_DCH1_DFISTAT:
        return th1520_ddr_controller_dfi_complete(s) ? 1 : 0;
    case TH1520_DDR_CTRL_SWSTAT:
        return s->regs[TH1520_DDR_CTRL_SWCTL / sizeof(uint32_t)] &
               TH1520_DDR_CTRL_SW_DONE;
    default:
        return s->regs[offset / sizeof(uint32_t)];
    }
}

static void th1520_ddr_controller_write(void *opaque, hwaddr offset,
                                        uint64_t value, unsigned int size)
{
    TH1520DDRControllerState *s = opaque;

    if (offset == TH1520_DDR_CTRL_DFIMISC &&
        th1520_ddr_controller_dfi_complete(s)) {
        s->dfi_initialized = true;
    }

    switch (offset) {
    case TH1520_DDR_CTRL_STAT:
    case TH1520_DDR_CTRL_DCH1_STAT:
    case TH1520_DDR_CTRL_DFISTAT:
    case TH1520_DDR_CTRL_DCH1_DFISTAT:
    case TH1520_DDR_CTRL_SWSTAT:
        return;
    default:
        s->regs[offset / sizeof(uint32_t)] = value;
        return;
    }
}

static uint64_t th1520_ddr_phy_read(void *opaque, hwaddr offset,
                                    unsigned int size)
{
    TH1520DDRPhyState *s = opaque;

    switch (offset) {
    case TH1520_DDR_PHY_MAILBOX_STATUS:
        return s->mailbox_pending ? 0 : 1;
    case TH1520_DDR_PHY_MAILBOX_MSG0:
        return s->mailbox_pending ? 0x7 :
               s->regs[offset / sizeof(uint16_t)];
    case TH1520_DDR_PHY_MAILBOX_MSG1:
        return s->mailbox_pending ? 0 :
               s->regs[offset / sizeof(uint16_t)];
    default:
        return s->regs[offset / sizeof(uint16_t)];
    }
}

static void th1520_ddr_phy_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned int size)
{
    TH1520DDRPhyState *s = opaque;
    uint16_t val = value;

    switch (offset) {
    case TH1520_DDR_PHY_MAILBOX_ACK:
        s->regs[offset / sizeof(uint16_t)] = val;
        if (val == 0) {
            s->mailbox_pending = false;
        }
        return;
    case TH1520_DDR_PHY_TRAINING_TRIGGER:
        s->regs[offset / sizeof(uint16_t)] = val;
        if (val) {
            s->training_trigger_seen = true;
        } else if (s->training_trigger_seen) {
            s->training_trigger_seen = false;
            s->training_complete = true;
            s->mailbox_pending = true;
        }
        return;
    default:
        s->regs[offset / sizeof(uint16_t)] = val;
        return;
    }
}

static const MemoryRegionOps th1520_ddr_controller_ops = {
    .read = th1520_ddr_controller_read,
    .write = th1520_ddr_controller_write,
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

static const MemoryRegionOps th1520_ddr_phy_ops = {
    .read = th1520_ddr_phy_read,
    .write = th1520_ddr_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
};

static void th1520_ddr_controller_reset(DeviceState *dev)
{
    TH1520DDRControllerState *s = TH1520_DDR_CONTROLLER(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->dfi_initialized = false;
}

static void th1520_ddr_phy_reset(DeviceState *dev)
{
    TH1520DDRPhyState *s = TH1520_DDR_PHY(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->training_trigger_seen = false;
    s->training_complete = false;
    s->mailbox_pending = false;
}

static const VMStateDescription vmstate_th1520_ddr_controller = {
    .name = TYPE_TH1520_DDR_CONTROLLER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, TH1520DDRControllerState,
                             TH1520_DDR_CONTROLLER_REG_COUNT),
        VMSTATE_BOOL(dfi_initialized, TH1520DDRControllerState),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription vmstate_th1520_ddr_phy = {
    .name = TYPE_TH1520_DDR_PHY,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16_ARRAY(regs, TH1520DDRPhyState,
                             TH1520_DDR_PHY_REG_COUNT),
        VMSTATE_BOOL(training_trigger_seen, TH1520DDRPhyState),
        VMSTATE_BOOL(training_complete, TH1520DDRPhyState),
        VMSTATE_BOOL(mailbox_pending, TH1520DDRPhyState),
        VMSTATE_END_OF_LIST(),
    },
};

static void th1520_ddr_controller_init(Object *obj)
{
    TH1520DDRControllerState *s = TH1520_DDR_CONTROLLER(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_ddr_controller_ops, s,
                          TYPE_TH1520_DDR_CONTROLLER,
                          TH1520_DDR_CONTROLLER_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void th1520_ddr_phy_init(Object *obj)
{
    TH1520DDRPhyState *s = TH1520_DDR_PHY(obj);

    memory_region_init_io(&s->iomem, obj, &th1520_ddr_phy_ops, s,
                          TYPE_TH1520_DDR_PHY, TH1520_DDR_PHY_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void th1520_ddr_controller_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 DDR controller state (virtual training)";
    dc->user_creatable = false;
    dc->vmsd = &vmstate_th1520_ddr_controller;
    device_class_set_legacy_reset(dc, th1520_ddr_controller_reset);
}

static void th1520_ddr_phy_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "T-Head TH1520 DDR PHY state (virtual training)";
    dc->user_creatable = false;
    dc->vmsd = &vmstate_th1520_ddr_phy;
    device_class_set_legacy_reset(dc, th1520_ddr_phy_reset);
}

static const TypeInfo th1520_ddr_controller_info = {
    .name = TYPE_TH1520_DDR_CONTROLLER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520DDRControllerState),
    .instance_init = th1520_ddr_controller_init,
    .class_init = th1520_ddr_controller_class_init,
};

static const TypeInfo th1520_ddr_phy_info = {
    .name = TYPE_TH1520_DDR_PHY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TH1520DDRPhyState),
    .instance_init = th1520_ddr_phy_init,
    .class_init = th1520_ddr_phy_class_init,
};

static void th1520_ddr_register_types(void)
{
    type_register_static(&th1520_ddr_controller_info);
    type_register_static(&th1520_ddr_phy_info);
}

type_init(th1520_ddr_register_types)
