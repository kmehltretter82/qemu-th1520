/*
 * T-Head TH1520 SoC and BeagleV Ahead board
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_TH1520_H
#define HW_RISCV_TH1520_H

#include "hw/core/boards.h"
#include "hw/riscv/riscv_hart.h"

#define TYPE_RISCV_TH1520_SOC "riscv.th1520.soc"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520SoCState, RISCV_TH1520_SOC)

struct TH1520SoCState {
    DeviceState parent_obj;

    RISCVHartArrayState c910_cpus;
    MemoryRegion sram;
    MemoryRegion brom;
    DeviceState *plic;
};

#define TYPE_BEAGLEV_AHEAD_MACHINE MACHINE_TYPE_NAME("beaglev-ahead")
OBJECT_DECLARE_SIMPLE_TYPE(BeagleVAheadState, BEAGLEV_AHEAD_MACHINE)

struct BeagleVAheadState {
    MachineState parent_obj;

    TH1520SoCState soc;
    Notifier machine_done;
};

enum {
    TH1520_DEV_DRAM,
    TH1520_DEV_PLIC,
    TH1520_DEV_CLINT,
    TH1520_DEV_SRAM,
    TH1520_DEV_UART0,
    TH1520_DEV_BROM,
};

#define TH1520_C910_HARTS 4
#define TH1520_C910_VLENB 16
#define TH1520_TIMEBASE_FREQ 3000000

/* riscv,ndev describes IDs 1..240; QEMU's PLIC count includes ID zero. */
#define TH1520_PLIC_NDEV 240
#define TH1520_PLIC_NUM_SOURCES (TH1520_PLIC_NDEV + 1)
#define TH1520_PLIC_NUM_PRIORITIES 7
#define TH1520_PLIC_PRIORITY_BASE 0x000000
#define TH1520_PLIC_PENDING_BASE 0x001000
#define TH1520_PLIC_ENABLE_BASE 0x002000
#define TH1520_PLIC_ENABLE_STRIDE 0x80
#define TH1520_PLIC_CONTEXT_BASE 0x200000
#define TH1520_PLIC_CONTEXT_STRIDE 0x1000

#define TH1520_UART0_IRQ 36
#define TH1520_UART_INPUT_FREQ 100000000

#endif /* HW_RISCV_TH1520_H */
