/*
 * T-Head TH1520 mailbox controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_MBOX_H
#define HW_MISC_TH1520_MBOX_H

#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_MBOX "th1520-mbox"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520MboxState, TH1520_MBOX)

#define TH1520_MBOX_CHANNELS         4
#define TH1520_MBOX_REMOTE_CHANNELS  (TH1520_MBOX_CHANNELS - 1)
#define TH1520_MBOX_INFO_WORDS       8
#define TH1520_MBOX_CHANNEL_SIZE     0x1000
#define TH1520_MBOX_LOCAL_MMIO_SIZE   0x6000
#define TH1520_MBOX_REMOTE0_MMIO_SIZE 0x6000
#define TH1520_MBOX_REMOTE1_MMIO_SIZE 0x2000
#define TH1520_MBOX_REMOTE2_MMIO_SIZE 0x2000

typedef struct TH1520MboxWindow {
    MemoryRegion iomem;
    TH1520MboxState *parent;
    uint16_t register_offset;
    uint8_t channel;
    bool local;
} TH1520MboxWindow;

struct TH1520MboxState {
    SysBusDevice parent_obj;

    TH1520MboxWindow local;
    TH1520MboxWindow remote[TH1520_MBOX_REMOTE_CHANNELS];
    qemu_irq irq;
    uint32_t info[TH1520_MBOX_CHANNELS][TH1520_MBOX_INFO_WORDS];
    uint8_t generate[TH1520_MBOX_CHANNELS];
    uint8_t status;
    uint8_t mask;
};

#endif /* HW_MISC_TH1520_MBOX_H */
