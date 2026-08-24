/* SPDX-License-Identifier: GPL-2.0-or-later */

ENTRY(_start)

SECTIONS
{
    . = 0x80000000;
    .text : {
        *(.text)
    }
    .rodata : {
        *(.rodata)
    }

    . = ALIGN(1 << 21);
    .data : {
        *(.data)
    }
    .bss : {
        *(.bss)
    }

    ASSERT(. <= 0x80400000, "ordinary test image overlaps PMA pages")

    . = 0x80400000;
    .pma_normal0 (NOLOAD) : { *(.pma_normal0) }
    . = 0x80401000;
    .pma_strong0 (NOLOAD) : { *(.pma_strong0) }
    . = 0x80402000;
    .pma_noncache0 (NOLOAD) : { *(.pma_noncache0) }
    . = 0x80403000;
    .pma_normal1 (NOLOAD) : { *(.pma_normal1) }
    . = 0x80404000;
    .pma_strong1 (NOLOAD) : { *(.pma_strong1) }
    . = 0x80405000;
    .pma_noncache1 (NOLOAD) : { *(.pma_noncache1) }
    . = 0x80406000;
    .pma_normal2 (NOLOAD) : { *(.pma_normal2) }
    . = 0x80407000;
    .pma_strong2 (NOLOAD) : { *(.pma_strong2) }
    . = 0x80408000;
    .pma_default (NOLOAD) : { *(.pma_default) }
}
