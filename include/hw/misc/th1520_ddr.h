/*
 * T-Head TH1520 DDR controller and PHY state
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_DDR_H
#define HW_MISC_TH1520_DDR_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

/* The public SPL uses controller offsets through the FREQ1 register bank. */
#define TH1520_DDR_CONTROLLER_MMIO_SIZE       0x00003000
#define TH1520_DDR_CONTROLLER_REG_COUNT       \
    (TH1520_DDR_CONTROLLER_MMIO_SIZE / sizeof(uint32_t))

/*
 * The selected public training path reaches CSR 0xd0099 at a two-byte stride.
 * This bound covers that path; it is not a statement of the physical PHY
 * aperture.
 */
#define TH1520_DDR_PHY_MMIO_SIZE              0x00200000
#define TH1520_DDR_PHY_REG_COUNT              \
    (TH1520_DDR_PHY_MMIO_SIZE / sizeof(uint16_t))

/* Source-defined DWC uMCTL2 controller words used for virtual completion. */
#define TH1520_DDR_CTRL_STAT                  0x0004
#define TH1520_DDR_CTRL_DFIMISC               0x01b0
#define TH1520_DDR_CTRL_DFISTAT               0x01bc
#define TH1520_DDR_CTRL_SWCTL                 0x0320
#define TH1520_DDR_CTRL_SWSTAT                0x0324
#define TH1520_DDR_CTRL_DCH1_STAT             0x1b04
#define TH1520_DDR_CTRL_DCH1_DFISTAT          0x1cbc

/* Source-defined DWC DDR PHY CSR indexes at the SPL's two-byte stride. */
#define TH1520_DDR_PHY_MICRO_CONT_MUX         0x1a0000
#define TH1520_DDR_PHY_MAILBOX_STATUS         0x1a0008
#define TH1520_DDR_PHY_MAILBOX_ACK            0x1a0062
#define TH1520_DDR_PHY_MAILBOX_MSG0           0x1a0064
#define TH1520_DDR_PHY_MAILBOX_MSG1           0x1a0068
#define TH1520_DDR_PHY_TRAINING_TRIGGER       0x1a0132

#define TYPE_TH1520_DDR_CONTROLLER "th1520-ddr-controller"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520DDRControllerState, TH1520_DDR_CONTROLLER)

#define TYPE_TH1520_DDR_PHY "th1520-ddr-phy"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520DDRPhyState, TH1520_DDR_PHY)

struct TH1520DDRPhyState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint16_t regs[TH1520_DDR_PHY_REG_COUNT];
    bool training_trigger_seen;
    bool training_complete;
    bool mailbox_pending;
};

struct TH1520DDRControllerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    TH1520DDRPhyState *phy[2];
    uint32_t regs[TH1520_DDR_CONTROLLER_REG_COUNT];
    bool dfi_initialized;
};

#endif /* HW_MISC_TH1520_DDR_H */
