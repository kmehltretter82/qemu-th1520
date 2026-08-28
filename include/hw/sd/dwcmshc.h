/*
 * Synopsys DesignWare Cores Mobile Storage Host Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SD_DWCMSHC_H
#define HW_SD_DWCMSHC_H

#include "hw/sd/sdhci.h"
#include "hw/core/clock.h"

#define TYPE_DWC_MSHC "dwcmshc"
OBJECT_DECLARE_SIMPLE_TYPE(DWCMSHCState, DWC_MSHC)

#define DWCMSHC_REG_SIZE 0x10000
#define DWCMSHC_PHY_SIZE 0x34

struct DWCMSHCState {
    SysBusDevice parent_obj;

    MemoryRegion container;
    MemoryRegion pointer_iomem;
    MemoryRegion vendor_iomem;
    BusState *bus;

    SDHCIState sdhci;
    /* Controller core clock; a zero rate stalls the SDHCI data engine. */
    Clock *core_clk;

    /* Read-only integration parameters. */
    uint64_t capareg;
    uint64_t maxcurr;
    uint32_t version_id;
    uint32_t version_type;
    uint16_t host_version;
    uint16_t vendor_area1;
    uint16_t vendor_area2;

    /* Vendor and PHY register state. */
    uint8_t mshc_ctrl;
    uint8_t mbiu_ctrl;
    uint16_t emmc_ctrl;
    uint16_t boot_ctrl;
    uint32_t at_ctrl;
    uint32_t at_stat;
    uint32_t embedded_ctrl;
    uint8_t phy[DWCMSHC_PHY_SIZE];
};

#endif /* HW_SD_DWCMSHC_H */
