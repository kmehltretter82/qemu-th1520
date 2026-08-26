/*
 * T-Head TH1520 firmware PMP portal
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_TH1520_PMP_PORTAL_H
#define HW_MISC_TH1520_PMP_PORTAL_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TH1520_PMP_PORTAL "th1520-pmp-portal"
OBJECT_DECLARE_SIMPLE_TYPE(TH1520PMPPortalState, TH1520_PMP_PORTAL)

/* The DTS names a 4 KiB portal; only these public-SPL words are modeled. */
#define TH1520_PMP_PORTAL_APERTURE_SIZE 0x1000
#define TH1520_PMP_PORTAL_REG_COUNT     5
#define TH1520_PMP_PORTAL_CONFIG         0x000
#define TH1520_PMP_PORTAL_WORD_100       0x100
#define TH1520_PMP_PORTAL_WORD_104       0x104
#define TH1520_PMP_PORTAL_WORD_108       0x108
#define TH1520_PMP_PORTAL_WORD_10C       0x10c

typedef struct TH1520PMPPortalReg {
    MemoryRegion iomem;
    struct TH1520PMPPortalState *portal;
    uint8_t index;
} TH1520PMPPortalReg;

struct TH1520PMPPortalState {
    SysBusDevice parent_obj;

    TH1520PMPPortalReg regs[TH1520_PMP_PORTAL_REG_COUNT];
    uint32_t words[TH1520_PMP_PORTAL_REG_COUNT];
};

#endif /* HW_MISC_TH1520_PMP_PORTAL_H */
