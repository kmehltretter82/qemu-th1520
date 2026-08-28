# QEMU with T-Head TH1520 / BeagleV Ahead emulation

This is a downstream fork of [QEMU](https://www.qemu.org/) that adds emulation
of the T-Head TH1520 SoC and the BeagleV Ahead single-board computer, together
with the XuanTie C910 CPU model those parts are built around.

Upstream QEMU is unmodified except where this work required it.  Everything
specific to this fork lives behind the `beaglev-ahead` machine, the
`thead-c910` CPU and the reusable IP models listed below; generic QEMU users
and other boards keep their existing behaviour.

* Upstream baseline: `2be159078ea26feac4c9c9902acf8906f1a05c2a`
* Working branch: `beaglev-ahead`
* QEMU version at that baseline: 11.1.50

## What this adds

CPU and ISA:

* a `thead-c910` RV64 CPU model with the T-Head `mvendorid` 0x5b7, zero
  `marchid`/`mimpid`, Sv39, 40-bit physical addresses and the T-Head custom
  CSR aperture;
* **XTheadVector**, the C910's Vector 0.7.1-derived vector ISA, which upstream
  QEMU does not implement — 128-bit vector registers, the six T-Head vector
  CSRs, and a frozen v0.7.1-derived execution engine;
* MAEE page-attribute ownership, strong-order and non-cacheable memory
  semantics, and the C910 FXCR floating-point control contract;
* 16 programmable performance counters with the C9xx overflow CSRs.

SoC and board:

* a `beaglev-ahead` machine with four C910 harts, the physical RAM/SRAM/ROM
  map, a 240-source C900 PLIC, a C900 CLINT and a generated device tree;
* TH1520 clock, reset, pinctrl, PMIC, mailbox and system-control blocks;
* reusable DesignWare IP models — APB UART, I2C, GPIO, timers, watchdog, SSI,
  AXI DMAC, MSHC/SDHCI and GMAC — wired at TH1520 addresses and interrupts;
* eMMC with an Ahead-only synthetic 5.1 speed profile (HS200/HS400 at 1.8 V),
  two GMAC 3.x cores with descriptor DMA and Clause 22 MDIO, a DWC3/xHCI USB
  host, an X-Gene-compatible RTC, PWM, PVT and the board's user LEDs.

## Status

Honest summary: **this boots Linux, and it is not a finished board model.**

What works today:

* mainline Linux and the vendor RevyOS `th1520-lts` kernel both boot to a
  controlled root shell, mount an ext2/ext4 root from the emulated eMMC over
  HS400, and survive a write/sync/hash/remount/reopen integrity cycle;
* networking through GMAC0: DHCP, ping and a 1 MiB HTTP transfer verified by
  SHA-256;
* one- and four-hart configurations, migration coverage for every modeled
  stateful device, and reset coverage for the modeled reset groups.

What does not work yet:

* **no unmodified official board image boots.** The stock-image gate is open;
  see `DOC-002a` in the validation ledger.
* vendor U-Boot reaches eMMC environment loading but does not hand off to an
  OS. The auxiliary firmware and vendor partition layout it needs come from an
  official image.
* no display/GPU, camera, media, NPU, Wi-Fi or Bluetooth data path; no
  auxiliary C906/E902/DSP cores; USB device/OTG and Fastboot are absent.
* nothing here has been compared against physical hardware. Every value marked
  provisional in the ledger is a software-visible convention, not a measured
  silicon fact.

The project tracks its own uncertainty deliberately: no unknown is quietly
turned into a guessed hardware behaviour.

## Documentation

* [`docs/system/riscv/beaglev-ahead.rst`](docs/system/riscv/beaglev-ahead.rst)
  — user-facing machine documentation and command lines
* [`docs/devel/beaglev-ahead-emulation-plan.md`](docs/devel/beaglev-ahead-emulation-plan.md)
  — the phased plan, acceptance gates and progress assessment
* [`docs/devel/beaglev-ahead-hardware-validation.md`](docs/devel/beaglev-ahead-hardware-validation.md)
  — the uncertainty ledger: every fact not yet proved, and how to prove it
* [`docs/devel/beaglev-ahead-local-bugs.md`](docs/devel/beaglev-ahead-local-bugs.md)
  — branch-only defects and current test evidence
* [`docs/devel/beaglev-ahead-upstream-bugs.md`](docs/devel/beaglev-ahead-upstream-bugs.md)
  — defects found in pre-existing upstream QEMU code

## Building

```bash
mkdir build && cd build
../configure --target-list=riscv64-softmmu --disable-docs
make -j"$(nproc)"
```

A dependency-minimal build with only this board is also supported:

```bash
../configure --target-list=riscv64-softmmu \
    --without-default-devices --with-devices-riscv64=beaglev-ahead
```

## Running

```bash
qemu-system-riscv64 -M beaglev-ahead \
    -kernel Image \
    -drive if=sd,index=0,file=emmc.img,format=raw \
    -append "console=ttyS0,115200 earlycon" \
    -nographic
```

See the machine documentation for mask-ROM boot, bare M-mode ELF payloads and
the other supported modes.

## Tests

The board gate runs from a build directory:

```bash
./pyvenv/bin/meson test --print-errorlogs 'qtest-riscv64/beaglev-ahead-test'
```

Note that `make check-tcg` cannot drive the dependency-minimal or sanitizer
builds, because they provide only the `beaglev-ahead` machine and the
`virt`-based payloads abort; the Ahead-machine targets have to be named
explicitly.

## Licence and provenance

QEMU is GPL-2.0-or-later, and so is this fork.  See `LICENSE` and `COPYING`.

The XTheadVector implementation was ported from the GPL-2.0-or-later
Alibaba/XuanTie QEMU fork at `3287d345c7f5d60d5c8774d90752f5f710744f85` and
reconciled with upstream's older RVV 0.7.1 code at
`e523773040ed914b60c8b68c25a96c88b2bb112a`.  File copyright and licence
notices are retained.

TH1520 register behaviour was implemented from publicly hosted vendor
documentation and public driver sources.  No vendor PDF and no substantial
quotation from one is present in this tree.

This fork is not affiliated with or endorsed by the QEMU project,
BeagleBoard.org, T-Head or Alibaba.
