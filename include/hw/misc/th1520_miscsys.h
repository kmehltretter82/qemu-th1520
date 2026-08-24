/*
 * T-Head TH1520 miscellaneous subsystem clock/reset registers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_MISCSYS_H
#define HW_MISC_TH1520_MISCSYS_H

#include "hw/core/irq.h"
#include "hw/core/sysbus.h"

#define TYPE_TH1520_MISCSYS "th1520-miscsys"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520MiscSysState, TH1520_MISCSYS)

#define TH1520_MISCSYS_MMIO_SIZE 0x1000
#define TH1520_MISCSYS_REGS \
    (TH1520_MISCSYS_MMIO_SIZE / sizeof(uint32_t))

enum {
    TH1520_MISCSYS_USB_PRST,
    TH1520_MISCSYS_USB_PHYRST,
    TH1520_MISCSYS_USB_VCCRST,
    TH1520_MISCSYS_USB_RESET_COUNT,
};

enum {
    TH1520_MISCSYS_STORAGE_EMMC,
    TH1520_MISCSYS_STORAGE_SDIO0,
    TH1520_MISCSYS_STORAGE_SDIO1,
    TH1520_MISCSYS_STORAGE_RESET_COUNT,
};

struct TH1520MiscSysState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq usb_reset[TH1520_MISCSYS_USB_RESET_COUNT];
    qemu_irq storage_reset[TH1520_MISCSYS_STORAGE_RESET_COUNT];
    uint32_t regs[TH1520_MISCSYS_REGS];
    bool usb_reset_asserted[TH1520_MISCSYS_USB_RESET_COUNT];
    bool storage_reset_asserted[TH1520_MISCSYS_STORAGE_RESET_COUNT];
};

#endif /* HW_MISC_TH1520_MISCSYS_H */
