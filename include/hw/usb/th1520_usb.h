/*
 * T-Head TH1520 USB3 DRD wrapper
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_USB_TH1520_USB_H
#define HW_USB_TH1520_USB_H

#include "hw/core/sysbus.h"
#include "hw/usb/hcd-dwc3.h"

#define TYPE_TH1520_USB "th1520-usb"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520USBState, TH1520_USB)

#define TH1520_USB_DRD_MMIO_SIZE 0x1000
#define TH1520_USB_DRD_REGS (0x60 / sizeof(uint32_t))

struct TH1520USBState {
    SysBusDevice parent_obj;

    MemoryRegion drd_iomem;
    MemoryRegion dwc3_alias;
    USBDWC3 dwc3;
    uint32_t drd_regs[TH1520_USB_DRD_REGS];
    bool reset_asserted[3];
};

#endif /* HW_USB_TH1520_USB_H */
