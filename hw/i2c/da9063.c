/*
 * Dialog DA9063 PMIC
 *
 * This deliberately models only the page-0 register state used by the
 * BeagleV Ahead vendor SPL to select its CPU voltage rails.  It does not
 * model regulator outputs, interrupts, RTC, watchdog, GPIO, or other PMIC
 * functions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/da9063.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

#define DA9063_REG_CONTROL_D       0x11
#define DA9063_REG_DVC_1           0x32
#define DA9063_REG_DVC_2           0x33
#define DA9063_REG_VBCORE2_A       0xa3
#define DA9063_REG_VBCORE1_A       0xa4
#define DA9063_REG_VBIO_A          0xa7
#define DA9063_REG_VBCORE2_B       0xb4
#define DA9063_REG_VBCORE1_B       0xb5
#define DA9063_REG_VBIO_B          0xb8

struct DA9063State {
    I2CSlave parent_obj;

    uint8_t regs[0x100];
    uint8_t pointer;
    bool expect_pointer;
};

static uint8_t da9063_writable_mask(uint8_t reg)
{
    switch (reg) {
    case DA9063_REG_CONTROL_D:
    case DA9063_REG_DVC_1:
    case DA9063_REG_VBCORE2_A:
    case DA9063_REG_VBCORE1_A:
    case DA9063_REG_VBIO_A:
    case DA9063_REG_VBCORE2_B:
    case DA9063_REG_VBCORE1_B:
    case DA9063_REG_VBIO_B:
        return UINT8_MAX;
    case DA9063_REG_DVC_2:
        return 0x81;
    default:
        return 0;
    }
}

static int da9063_event(I2CSlave *slave, enum i2c_event event)
{
    DA9063State *s = DA9063(slave);

    switch (event) {
    case I2C_START_SEND:
        s->expect_pointer = true;
        break;
    case I2C_START_RECV:
    case I2C_FINISH:
    case I2C_NACK:
        break;
    default:
        return -1;
    }

    return 0;
}

static int da9063_send(I2CSlave *slave, uint8_t data)
{
    DA9063State *s = DA9063(slave);
    uint8_t writable_mask;

    if (s->expect_pointer) {
        s->pointer = data;
        s->expect_pointer = false;
        return 0;
    }

    writable_mask = da9063_writable_mask(s->pointer);
    s->regs[s->pointer] = (s->regs[s->pointer] & ~writable_mask) |
                        (data & writable_mask);
    s->pointer++;

    return 0;
}

static uint8_t da9063_recv(I2CSlave *slave)
{
    DA9063State *s = DA9063(slave);
    uint8_t value = s->regs[s->pointer];

    s->pointer++;
    return value;
}

static void da9063_reset(DeviceState *dev)
{
    DA9063State *s = DA9063(dev);

    /* This is a virtual-device reset convention, not a hardware claim. */
    memset(s->regs, 0, sizeof(s->regs));
    s->pointer = 0;
    s->expect_pointer = true;
}

static const VMStateDescription vmstate_da9063 = {
    .name = TYPE_DA9063,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, DA9063State),
        VMSTATE_UINT8_ARRAY(regs, DA9063State, 0x100),
        VMSTATE_UINT8(pointer, DA9063State),
        VMSTATE_BOOL(expect_pointer, DA9063State),
        VMSTATE_END_OF_LIST()
    }
};

static void da9063_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    dc->desc = "Dialog DA9063 PMIC (partial)";
    dc->vmsd = &vmstate_da9063;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, da9063_reset);
    sc->event = da9063_event;
    sc->send = da9063_send;
    sc->recv = da9063_recv;
}

static const TypeInfo da9063_type_info = {
    .name = TYPE_DA9063,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DA9063State),
    .class_size = sizeof(I2CSlaveClass),
    .class_init = da9063_class_init,
};

static void da9063_register_types(void)
{
    type_register_static(&da9063_type_info);
}

type_init(da9063_register_types)
