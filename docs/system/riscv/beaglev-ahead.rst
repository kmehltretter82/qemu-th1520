BeagleV Ahead (``beaglev-ahead``)
================================

The ``beaglev-ahead`` machine models the BeagleV Ahead single-board computer,
which is built around the T-Head TH1520 SoC.  The initial implementation is a
boot-critical subset intended for direct firmware and kernel development.  It
does not yet run an unmodified production board image.

The machine is TCG-only.  QEMU rejects a non-TCG machine accelerator, and KVM
CPU realization rejects ``thead-c910`` because its custom state has no RISC-V
KVM synchronization contract.

Supported hardware
------------------

The machine currently provides:

* four T-Head C910 RV64 harts at hart IDs 0 through 3;
* the scalar RV64IMAFDC, Zfh, and implemented scalar XThead instructions;
* XTheadVector 1.0, derived from Vector 0.7.1, with 128-bit vector
  registers and the six T-Head vector CSRs;
* the C910 Privileged ISA 1.10 identity, T-Head ``mvendorid`` 0x5b7, zero
  ``marchid``/``mimpid``, Sv39 MMU, 40-bit physical addresses, and T-Head
  custom CSR aperture;
* 16 C910 programmable performance counters, C9xx counter overflow CSRs and
  local interrupt 17, plus the board's Linux/OpenSBI PMU event mappings;
* 4 GiB RAM at ``0x0000000000``;
* 1.5 MiB SRAM at ``0xffe0000000``;
* the 1 MiB mask-ROM aperture at ``0xffffd00000``;
* a 240-source C900 PLIC at ``0xffd8000000``, with eight M/S contexts,
  T-Head privilege delegation, writable pending state, and per-source
  edge/level inputs;
* a C900 CLINT at ``0xffdc000000``, with four-hart MSIP, MTIMECMP,
  SSIP, and STIMECMP banks and a 3 MHz architectural timer;
* the TH1520 application clock and reset providers at ``0xffef010000`` and
  ``0xffef014000``.  The reset provider connects all 28 mainline-described
  groups for currently modeled AP peripherals: watchdogs, PWM, timer groups,
  UART0-5, I2C0-5, SPI0, GPIO0-3, both application pad controllers, DMAC0,
  both GMACs and their shared AXI domain.  Output state is derived from the
  active-low register members and reconstructed after migration.  The clock
  provider exports 33 active-high leaf-gate levels for modeled AP consumers;
  PWM, timer0/1 and WDT0/1 also receive 125 MHz while enabled and zero while
  gated;
* six DesignWare APB UARTs at their TH1520 addresses, including the 16550
  register banks, DesignWare status/reset/probe registers, busy detection,
  and PLIC interrupts 36 through 41.  UART0 at ``0xffe7014000`` is the board
  console and UART1 through UART5 are disabled in the board device tree;
* six DesignWare I2C controllers at ``0xffe7f20000``, ``0xffe7f24000``,
  ``0xffec00c000``, ``0xffec014000``, ``0xffe7f28000``, and
  ``0xfff7f2c000``, connected to PLIC sources 44 through 49.  I2C0 is enabled
  for the board's 4 KiB FT24C32A-compatible EEPROM at address ``0x50``; I2C1
  through I2C5 are present but board-disabled;
* the TH1520 miscellaneous clock/reset bank at ``0xffec02c000`` and USB DRD
  wrapper at ``0xffec03f000``.  Their modeled reset values, writable masks,
  three active-low storage outputs, three active-low USB outputs, reset and
  migration state follow the recorded TH1520 software contract.  Its eight
  leaf-gate bits have observable, migratable outputs but are not yet connected
  to storage or USB engine behavior;
* one DWC3/xHCI USB 3 host controller at ``0xffe7040000`` on PLIC source 68,
  with one paired USB2/USB3 connector.  Host DMA, interrupts, a USB keyboard,
  hotplug, system reset and migration are exercised; the generated generic
  core node remains disabled pending a mainline TH1520 glue binding;
* one DesignWare APB SSI controller at ``0xffe700c000``, connected to PLIC
  source 54.  The ``spi0`` node is disabled to match upstream Linux and no
  flash or other board peripheral is attached;
* one six-channel TH1520 PWM controller at ``0xffec01c000`` with the upstream
  ``thead,th1520-pwm`` binding, AP clock ID 51 and ``#pwm-cells = <3>``.  It
  models the aligned 32-bit control/period/falling-point register subset used
  by Linux, continuous normal/inverted waveforms, staged period-boundary
  updates, reset and migration.  Its AP gate supplies a provisional 125 MHz
  QEMU input or stops it at zero; a gated channel holds its phase/output and
  resumes on re-enable.  Six QOM output lines are test facilities; no board pin
  or consumer is wired;
* two four-counter DesignWare APB timer components at ``0xffefc32000`` and
  ``0xffffc33000``.  Timers 0 through 7 count at 125 MHz and connect to PLIC
  sources 16 through 23.  User-defined PWM mode alternates the low interval
  from ``LoadCount`` and the high interval from ``LoadCount2`` on four named
  toggle outputs per component.  Their AP leaf gates freeze and resume the
  enabled count and output phase.  The toggle outputs are QEMU test
  facilities, not routed board pins.  All eight individual timer nodes remain
  disabled in the board device tree, matching upstream Linux;
* two Synopsys DesignWare APB watchdogs at ``0xffefc30000`` and
  ``0xffefc31000``.  They count at 125 MHz, connect to PLIC sources 24 and 25,
  and use AP clock IDs 76/77 and reset IDs 3/4.  Each AP leaf gate freezes and
  resumes its enabled countdown.  Both generated nodes remain board-disabled
  because current upstream Linux has not added them to the TH1520 device tree;
* one X-Gene-compatible DesignWare APB RTC at ``0xfffff40000`` with a
  32.768 kHz input, programmable prescaler and level-high PLIC source 74.
  Counter, load/match, interrupt, wrap, reset and migration behavior are
  modeled.  Its generated node remains disabled because mainline Linux does
  not yet program the TH1520 prescaler;
* one TH1520 mailbox controller with its 24 KiB local aperture at
  ``0xffffc38000`` and remote-ICU resources at ``0xffffc40000``,
  ``0xffffc4c000`` and ``0xffffc54000``.  Its generated binding uses AP clock
  IDs 72 through 75 and level-high PLIC source 28.  Only the C910-visible
  local-side register contract is modeled; no remote CPU or firmware endpoint
  is attached;
* one MR75203 PVT controller in four apertures beginning at
  ``0xfffff4e000``.  Its TH1520 synthesis has two temperature sensors, 11
  process detectors and one 16-channel voltage monitor.  The Linux SDIF
  programming and sample paths, deterministic QOM environment inputs, reset
  and migration are modeled;
* six DesignWare APB GPIO controllers at ``0xffec005000``, ``0xffec006000``,
  ``0xffe7f34000``, ``0xffe7f38000``, ``0xfffff52000``, and
  ``0xfffff41000``.  Their 157 Linux-described GPIO lines support input,
  output, edge/level interrupts, masking, reset and migration.  The generated
  board device tree describes its five GPIO4 LEDs;
* three TH1520 pad controllers at ``0xfffff4a000``, ``0xffe7f3c000``, and
  ``0xffec007000``.  Their PADCFG and MUXCFG register state, digital reset
  values, reserved-bit masks, system reset and migration are modeled.  The
  generated board device tree includes the exact Linux GPIO ranges and the
  board's LED, GMAC0, UART0 and Wi-Fi pin groups;
* three DesignWare Mobile Storage Host Controllers at ``0xffe7080000``
  (eMMC), ``0xffe7090000`` (microSD/SDIO0), and ``0xffe70a0000``
  (on-board Wi-Fi/SDIO1), connected to PLIC sources 62, 64, and 71.  The
  SDHCI v4.20 register interface, TH1520 vendor/PHY registers, programmed I/O,
  SDMA, ADMA2 including v4 64-bit descriptors, Auto CMD23, reset, interrupts,
  and migration are modeled.  Unit 0 opts its card into an Ahead-specific,
  synthetic eMMC 5.1 speed profile with HS200 and HS400 at 1.8 V, eMMC CMD21
  tuning blocks and validated CMD6 timing/bus-width transitions.  Other QEMU
  eMMC users retain the generic card profile;
* a board-private AP6203BM control/wake peer.  It routes GPIO2_31,
  GPIO2_28 and GPIO2_30 to ``WL_REG_ON``, ``BT_REG_ON`` and
  ``BT_WAKE_HOST``, and routes its ``WL_HW_OOB`` and ``HOST_WAKE_BT`` outputs
  to GPIO2_25 and GPIO2_29.  The five digital levels reset and migrate.  It
  intentionally does not implement an SDIO function, CYW43012 firmware,
  WLAN, Bluetooth, RF, power, clock or timing behavior; and
* two DesignWare GMAC 3.x cores at ``0xffe7070000`` and ``0xffe7060000``
  with TH1520 APB glue, descriptor DMA, normal/enhanced descriptors, FCS,
  checksum status, Clause 22 MDIO/PHY state, DMA receive-interrupt-watchdog
  timing, PLIC sources 66/67 and migration including an armed watchdog timer.
  Board GMAC0 can use a QEMU network backend; GMAC1 is board-disabled; and
* a four-channel DesignWare AXI DMAC 1.01a at ``0xffefc00000`` on PLIC source
  27, with direct and linked-list memory-to-memory copies, 64-bit addresses,
  descriptor writeback, errors, interrupt aggregation, reset and migration.

The generated device tree uses the same board, CPU, PLIC, CLINT, UART, I2C,
SPI0, PWM, APB timer, PVT, GPIO, pinctrl, storage, memory, and cache topology
bindings as upstream Linux's TH1520 device tree, augmented with the
schematic-established board EEPROM, two disabled generic DesignWare watchdog
nodes, a disabled vendor-established X-Gene RTC node, both USB syscon
apertures, and a disabled generic DWC3 node.  It advertises
``xtheadvector`` and ``thead,vlenb = <16>`` for every C910 hart.

Boot options
------------

``boot-mode=direct`` is the default and uses QEMU's bundled generic OpenSBI.
For example:

.. code-block:: bash

   qemu-system-riscv64 -M beaglev-ahead \
       -kernel Image \
       -drive if=sd,index=0,file=emmc.img,format=raw \
       -drive if=sd,index=1,file=microsd.img,format=raw \
       -initrd rootfs.cpio.gz \
       -append "console=ttyS0,115200 earlycon" \
       -nographic

QEMU loads firmware at the beginning of RAM, places a supplied Linux kernel
after it, generates the device tree, and installs a small reset trampoline in
the mask-ROM aperture.  ``-bios none`` is also accepted for low-level tests
that load all code explicitly.

A bare M-mode ELF should instead be supplied as firmware so the reset
trampoline jumps to its ELF entry point.  For example:

.. code-block:: bash

   qemu-system-riscv64 -M beaglev-ahead \
       -bios test.elf -display none -semihosting

An opt-in mode can execute a privately supplied raw mask-ROM image directly
from the 1 MiB ROM aperture:

.. code-block:: bash

   qemu-system-riscv64 \
       -M beaglev-ahead,boot-mode=mask-rom \
       -bios th1520-mask-rom.bin -nographic

The image must be non-empty and no larger than 1 MiB.  In this mode QEMU does
not load firmware or a kernel into RAM, inject a device tree, or install its
direct-boot trampoline.  Consequently ``-kernel``, ``-initrd``, ``-append``
and ``-dtb`` are rejected rather than silently ignored.  The ROM is read-only
and is restored on reset.

The supported migration configuration uses ``boot-mode=mask-rom`` at both
ends.  A mask-ROM destination must start with a syntactically valid, non-empty
image no larger than 1 MiB because machine validation precedes incoming
migration.  The migration stream then carries the source ROM RAMBlock bytes:
a focused test starts with different destination bytes, verifies that the
source bytes replace them, and verifies that those bytes survive reset.  A
private ROM is therefore sensitive migration data as well as a sensitive host
file; keep it private unless its redistribution rights are known.

This option is an execution bridge, not a claim that the TH1520 boot process
is modeled.  QEMU does not interpret the image, select eMMC/SD/QSPI, reproduce
authentication or fallback, enter a measured TEE state, or release harts as
silicon does.  All four emulated C910 harts still start at the common ROM
address.  A synthetic test ROM parks three harts and proves hart 0 can emit a
UART marker before and after QEMU system reset.  A real ROM may stop at its
first access to an early clock, reset, power, security, SRAM/DDR or mailbox
interface that QEMU does not yet model faithfully.

``-bios none -kernel test.elf`` is not equivalent: without firmware the reset
trampoline jumps to the start of DRAM and the ``-kernel`` entry is an SBI
next-stage address.

Passing ``-dtb file.dtb`` replaces the generated tree and passes that external
tree through the same firmware handoff.  This is useful for controlled driver
experiments; the supplied tree remains responsible for describing the real
machine topology correctly.

For a USB host experiment whose external device tree enables and sequences the
TH1520 glue and DWC3 core, a keyboard can be attached with:

.. code-block:: bash

   -device usb-kbd,bus=usb-bus.0,port=1

The generated tree deliberately does not enable the core by itself.

In direct mode, all four C910 harts currently enter that trampoline.  The
FW_DYNAMIC handoff selects hart 0 for relocation, and an OpenSBI configuration
node restricts its subsequent cold-boot lottery to hart 0.  OpenSBI consumes
that node before the next boot stage.  This makes direct boots deterministic
while leaving the other harts available for SBI HSM startup; a four-hart
M-mode test checks the ordered UART transcript from harts 0 through 3.

Storage unit 0 is attached as an eMMC device and unit 1 as a removable SD card.
Supplying either image makes it accessible to firmware and the operating
system, but does not select it as the reset boot source.  The reset trampoline
and OpenSBI convention are not an emulation of the TH1520 mask ROM or reset
controller.  The physical initial hart states, Core0 TEE mode and secondary
release sequence remain hardware-validation items.  Boot straps, the USB/UART
downloader, and mask-ROM selection, parsing and boot from eMMC, SD, or QSPI
are later milestones.

SDIO1 represents the soldered radio route, not a generic removable card.  Its
AP6203BM control/wake GPIO topology is modeled, but there is no emulated
CYW43012 SDIO function or radio backend yet.

The board EEPROM defaults to 4096 bytes of ``0xff`` because its factory image
can contain board-unique data that QEMU must not invent.  A captured 4096-byte
image can instead be made writable and persistent with:

.. code-block:: bash

   -drive if=none,id=board-eeprom,format=raw,file=ahead-eeprom.bin \
   -global at24c-eeprom.drive=board-eeprom

Keep any factory dump private unless its contents have been reviewed and
redacted.  The image must be exactly 4096 bytes.

CPU limitation
--------------

The real C910 implements T-Head's pre-ratification XTheadVector ISA, derived
from RISC-V Vector 0.7.1 with a 128-bit vector length.  Its encodings and some
semantics conflict with ratified RVV 1.0.  QEMU therefore uses a separate
``xtheadvector`` feature and decoder; it reports the identifying ``misa.V``
bit without enabling the ratified RVV 1.0 decoder.  ``XTheadZvamo`` is treated
as a distinct optional extension and is conservatively disabled for C910
pending physical-board confirmation.

The XTheadVector engine covers the frozen instruction set, CSR/status layout,
debug register file, reset, and migration state.  Architectural guests cover
illegal ``th.vsetvl`` WARL and source preservation, ``vstart`` prestart and
early-exit behavior, mask-undisturbed and tail-zero results, sticky saturation
and all four fixed-point rounding modes.  The state guest also requires
``th.vmfirst.m`` to trap at nonzero ``vstart`` without changing its scalar
destination or ``vstart``, and checks its legal first-set and no-set results.
A floating-point state guest covers
FS-Off legality across every decoder family, VS-Off reduction legality,
unsupported reduction widths and exception-driven ``fflags``/FS-Dirty updates
through all six helper-loop families; it also proves an exact operation does
not spuriously dirty FS.  Physical C910 NaN, exception and stepping behavior
has not yet been compared.  This is still not the exhaustive, randomized
differential coverage needed to claim silicon equivalence.
Reduction-boundary coverage also requires integer and floating-point
reductions to leave their complete destination register unchanged at ``vl=0``
and to trap at nonzero ``vstart``.  It covers e64 widening rejection before
helper dispatch, valid LMUL=8 scalar reduction operands, permitted
source/mask overlap and inactive-mask/tail behavior.  Those are frozen-spec
rules; physical C910 confirmation remains outstanding.  A fifth guest covers
the implicit ``v0`` overlap boundary for masked LMUL=2 integer and
floating-point comparisons and all three mask-prefix forms, while retaining
legal LMUL=1, unmasked and non-``v0`` controls.  A sixth guest uses independent
scalar RV64 oracles for ``th.vslidedown.vx`` and ``th.vrgather.vx``.  It checks
valid, VLMAX, ``UINT64_MAX`` and ``1ULL << 32`` scalar indices, a slide that
partly crosses VLMAX, in-place slide, helper and optimized gather paths, and
mask/prestart/tail/``vstart`` state.  This permutation gate dynamically covers
RV64 e8,m1 scalar indexing.  Checkpoint ``78ad4d6e56`` also checks e64,m1
``th.vrgather.vv`` with an index above 32 bits and requires exact traps for all
three slide-down forms when masked ``vd=v0`` at LMUL=1, including first-trap
``vstart``/destination preservation and legal unmasked/non-``v0`` controls.
Other SEW/LMUL and XLEN combinations plus physical C910 results remain
outstanding.  MAEE
PTE bits 63:59 are carried
through translation while MAEE is enabled.  When MAEE is clear, C910 ignores
those PTE bits and obtains attributes from its physical system map; QEMU
matches the ignore behavior but does not yet model the unknown TH1520 physical
PMA ranges.  A strong-order page enforces post-translation natural alignment
for ordinary scalar accesses and rejects instruction fetches; non-cacheable
instruction fetch remains valid.  AMO read-modify-write operations require a
cacheable mapping, while LR/SC remains valid there.  XTheadVector loads and
stores reject strong-order mappings.  Tests cover all four element widths and
unit-stride, strided, indexed and two-field segment forms; segment
fault-only-first loads trap on a denied field in element zero and shorten
``vl`` for a later denied segment.
Ratified-RVV translation is gated off on this XTheadVector-only CPU even where
the instruction encodings overlap.  All four XTheadSync variants provide a
full inter-vCPU TCG memory barrier and retain a translation-block exit for the
pipeline boundary.  Cacheability, buffering, shareability, security-bus,
cache-operation completion, timing and a physical instruction-refetch
pipeline are not modeled.  Some custom CSRs remain placeholders.  Fixed
counters, TLB-miss events and the C9xx overflow
protocol are implemented, but cache, branch, pipeline and other
microarchitectural performance events are not yet hardware-accurate.  The
TH1520 integration exposes no writable PMP entries,
matching public physical-board boot captures, although generic C910
documentation describes optional PMP configurations.  These uncertainties
are itemized in the hardware validation ledger.

The C910 ``FXCR`` user CSR at 0x800 aliases ``frm`` and ``fflags`` and adds
the DQNaN propagation control plus sticky exception-event flag FE.  QEMU
models the pinned openC910 mask, FS access rules, NaN/event behavior, zero
state after QEMU system reset, same-version migration, and exposes it as
``th.c910.fxcr`` in ``info registers -a``.  Migration tests inject one local
invalid SoftFloat event through a qtest-only hook while the incoming destination
remains stopped.  The first resumed FXCR read must discard that non-migrated
event and report the source state, then the guest proves DQNaN payload
propagation and that a new already-sticky exception event sets FE.  The
retained-RAM reset phase then proves FS Off and the first exception-producing FP
operation after QEMU system reset.  An actual older-version FXCR-bearing stream
remains open.  The physical TH1520 reset value and exact CPU stepping are also
unmeasured, so this is a QEMU/openC910 contract rather than silicon evidence.

Peripheral limitations
----------------------

The PLIC follows the public C900 RTL rather than QEMU's generic SiFive model.
It implements five-bit priority and threshold fields, the machine-only
supervisor-delegation register at offset ``0x1ffffc``, writable pending words,
shared M/S arbitration, completion-qualified active state, and level
re-pending.  Its reset, all eight contexts, priority and tie-breaking rules,
edge/level inputs, privilege faults, CPU interrupt delivery, and migration
state have focused tests.  Linux establishes the TH1520 source count and
context topology, but the openC910 integration is not the TH1520 synthesis.
Consequently, the physical priority width, complete edge/level map,
security/AMP configuration, simultaneous-event ordering, and reset-domain
behavior remain hardware-validation items.  SoC inputs without a proved
trigger type currently default to level-sensitive.

The CLINT follows the public openC910 register layout: it has separate
machine and supervisor software/compare banks, 32-bit registers, and no
memory-mapped ``mtime`` register.  Machine banks reject supervisor accesses,
and supervisor banks are accessible from M- and S-mode.  Reset, all four
interrupt outputs, timer frequency, privilege faults, and migration state are
modeled.  Physical-board validation is still needed for timer rollover and
latching, system-bus handling of wider CPU accesses, oscillator stability,
and reset-domain behavior.

For each connected AP or storage reset group, clearing any represented
active-low member immediately cold-resets the corresponding whole QEMU device.
Releasing the group has no additional modeled effect, and MMIO remains
accessible while reset is held.  This intentionally conservative convention
does not distinguish APB, core, counter or AXI members.  Individual and shared
GMAC resets currently cover both the core/APB wrapper and the reusable model's
PHY; the eMMC pair similarly collapses to one storage-controller reset.  Raw
line, device-effect, neighbor-isolation and asserted-line migration qtests
cover all 28 AP outputs and all three storage outputs.  Physical pulse versus
level behavior, split-member scope, minimum assertion/release ordering,
retention and held-reset bus behavior remain unverified.  The C910 and mailbox
reset words remain register-only pending authentic hart-release and
per-channel-retention evidence; other reset/power domains are absent.  AP
clock-gate writes stop only the PWM, timer and watchdog timed engines described
below; the other 28 AP leaves and all eight miscellaneous leaves currently
export raw state without changing child engines or MMIO access.

The six UARTs use a reusable DesignWare APB wrapper around QEMU's 16550 core.
It
implements aligned 32-bit register accesses, USR FIFO/busy status, SRR reset,
busy-detect interrupts, the fractional divisor, synthesis probe registers,
configurable FIFO depth, reset, and migration.  UART RX, TX, THRE and busy
interrupts are connected through C900 PLIC source 36 and have both qtest and
guest-executed coverage.

The exact TH1520 UART synthesis is not publicly established.  The board model
therefore uses conservative defaults: a functional 16-byte FIFO while optional
CPR, UCV, DLF, and FIFO-statistics features report unavailable unless
configured explicitly.  Unproved shadow, DMA-extra, and RS-485 registers read
zero and ignore writes; partial implementations are intentionally not exposed.
The architectural component-type register reports the DesignWare
identification value.  Exact FIFO depth, optional-feature presence,
version/parameter values, subword and wider system-bus behavior, and
reset-domain details remain physical-hardware validation items.

The six I2C instances use QEMU's reusable DesignWare master model.  Their
addresses, 16 KiB device-tree apertures, PLIC sources 44-49 and AP clock IDs
64-69 match pinned upstream Linux.  The TH1520 configuration reports component
parameters ``0x000f0fee``, component version ``0x3230322a``, component type
``0x44570140``, 16-entry RX/TX FIFOs, interrupt-mask reset ``0x48ff``, spike
lengths of one clock, and all-ones SCL/SDA stuck-low timeout resets.  Polling
and interrupt-driven master reads/writes, repeated starts, NACK aborts,
register reset and migration are functional.  Focused tests cover all six
register/PLIC instances and migrate an in-flight receive FIFO.

This is not yet a cycle-accurate I2C implementation.  Commands execute as they
are written rather than through a timed TX FIFO; slave mode, multi-master
arbitration, bus clock timing, clock stretching, stuck-line detection and
recovery, DMA handshakes, SMBus behavior and clock-gate coupling are absent.
Each controller's mainline-described APB/core reset pair drives the
whole-device reset convention described above; the physical split remains
unverified.
Only the first 4 KiB of each 16 KiB described aperture is modeled.  High-speed
mode master-code/count defaults, the register-timeout value, reserved-aperture
responses and instance differences remain owner-hardware validation items.

I2C0 contains a 4 KiB ``atmel,24c32``-compatible device at address ``0x50``
with a 32-byte page size in the generated device tree.  EEPROM data and its
current-address pointer migrate, and an optional raw backing image persists
writes.  The pinned Linux DesignWare and AT24 drivers bind and read the entire
4096-byte erased image.  Multi-byte writes wrap within the selected 32-byte
page.  The EEPROM model does not yet reproduce write-cycle busy time,
endurance, power-loss or write-protect behavior.
Factory contents and layout, the fitted board revision and the schematic's
GPIO2_22-related write-protect network must be checked on the owner's board
before those behaviors are modeled.

The USB model combines QEMU's host-only DWC3/xHCI core with TH1520
miscellaneous-system and DRD wrapper registers at their Linux/vendor addresses.
It exposes one paired USB2/USB3 connector, routes the xHCI interrupt to PLIC
source 68, gives xHCI DMA access to guest memory, and translates all three
active-low miscellaneous reset bits into conservative whole-core resets.  The
wrapper and core register state migrate.  Focused tests program an xHCI event
and command ring, complete a no-op command through DMA, check PLIC delivery,
hotplug a keyboard, exercise both ERSTBA half-write orders, and migrate
miscellaneous, wrapper, DWC3 and xHCI state.

Linux commit ``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` was also booted with
its DWC3/xHCI/HID host drivers built in and a test-only TH1520 glue module.  It
enumerated QEMU's ``0627:0001`` USB keyboard through this controller.  That
module is validation scaffolding derived from the public vendor-driver
sequence, not a proposed production Linux driver.  Mainline Linux has no
TH1520 USB parent binding or glue driver, so QEMU emits both syscon nodes but
keeps the generic child disabled rather than bypassing clock/reset sequencing.

This milestone is host-functional, not complete DRD or PHY emulation.  Device
mode, OTG role detection/switching, VBUS and ID sensing, Fastboot/BootROM
recovery, suspend/resume, analog USB2/USB3 PHY behavior, link training, timing,
error injection, dynamic port-disable/topology overrides and clock-gate effects
are absent.  DWC3 revision/HWPARAM values are QEMU synthesis defaults, not
measured TH1520 values.  The three silicon reset domains are collapsed to a
full digital-core reset because the reusable core exposes no finer boundary.
All of these distinctions remain in the hardware-validation ledger.

SPI0 uses a reusable DesignWare APB SSI master model.  Its address, level-high
PLIC source 54, AP clock ID 54, ``spi0`` alias and disabled
``thead,th1520-spi``/``snps,dw-apb-ssi`` device-tree node match the pinned
upstream Linux description.  The model supports aligned 32-bit FIFO access,
standard, transmit-only, receive-only and EEPROM-read transfers, interrupt
thresholds and sticky overrun/underrun status, serial-loopback mode, reset and
migration.  A board model may attach an SSI peripheral to its ``spi`` bus and
native active-low ``cs`` outputs; the BeagleV Ahead machine deliberately does
not attach one.  Vendor U-Boot's ``light-beagle.dts`` describes a ``jedec,spi-nor``
child on SPI0 CS GPIO2_15 and a separate ``spi-nand`` child on ``qspi1``, but
the V1 board BOM contains neither NOR nor NAND.  The design data records the
corresponding boot selections but does not establish a fitted device or its
routing.  QEMU therefore does not turn either firmware description into a
default peripheral; the conflict is retained in the validation ledger.

The generic model defaults to a 16-frame FIFO and one native chip select and
reports zero component ID/version, because the TH1520 synthesis values have
not been measured.  Transfers are synchronous rather than clock accurate.
DMA, enhanced/dual/quad framing, clock-gate coupling, pinmux/electrical
routing, board SPI peripherals, QSPI0/1, XIP and boot-flash behavior are not
modeled.  SPI0's APB/core reset pair drives a whole-device reset, with the
physical split still unverified.  The disabled device-tree status means this model is not yet evidence
that an unmodified mainline Linux SPI driver binds on this board.

The separate TH1520 PWM controller follows the software contract of the
in-tree Linux driver: six channels at a 0x20-byte stride, CTRL/PERIOD/FP at
``+0x00/+0x08/+0x0c``, START and CFG_UPDATE strobes, continuous-mode output
and FPOUT phase selection.  Period and falling-point writes are shadowed until
the next period boundary.  The model drives six QOM ``pwm`` outputs so qtests
can check normal and inverted phases, staged updates and migration; there is
no virtual header/pad connection.  AP clock offset ``0x204`` bit 18 supplies a
provisional 125 MHz input while set and a zero-frequency gate while clear.
Gating preserves the remaining edge delay and holds the current output level;
re-enabling resumes that phase, including across migration.  This deterministic
rule is not yet a claim about silicon.  Only the first 0xb0 bytes used by Linux
are mapped, even though the DT aperture is 16 KiB.

The documented active-low AP reset pair at ``0xffef0140c0`` resets the PWM
model immediately when either of its APB/counter bits is clear.  This is a
QEMU software contract, not a claim about physical pulse width, held-reset
accesses, retention or ordering.  The exact reset/readback and reserved-
register behavior, physical clock rate and gate phase/output behavior,
one-shot/inactive-output semantics,
physical pinmux/header routing and electrical effects are deliberately not
claimed.  The generated node has no ``status`` property, matching upstream
Linux, but the board DTS has no PWM consumer and this documentation does not
claim a real output pin.

The two APB timer components use a reusable four-counter DesignWare model.
Each counter has load, current-value, control, EOI and interrupt-status
registers at the 0x14-byte hardware stride.  Periodic and free-running
countdown, interrupt masking, raw and masked aggregate status, per-counter and
aggregate EOI, the component-version register, reset and migration are
modeled.  The TH1520 integration uses separate timer-group AP leaves at clock
offset ``0x208`` bits 1/0.  Each supplies 125 MHz while set and zero while
clear, freezing enabled counters and resuming them when restored.  Component
version ``0x3231322a`` and eight independent level-high PLIC routes are used.

The model retains the four second-load and protection registers and preserves
the PWM control bit.  Every counter exports a named ``toggle`` line.  In
user-defined PWM mode, reaching zero alternates reloads from ``LoadCount2``
for the high half and ``LoadCount`` for the low half; outside PWM mode the
toggle line still changes at each normal timer expiry.  Toggle level, the
active half-cycle and its deadline survive migration, and a disabled counter
or reset drives the line low.  Clock gating freezes that level and the
remaining half-cycle.  QEMU does not infer cascade wiring or connect a toggle
line to pinctrl, a header or another counter.  Initial-enable, zero-count,
optional 0%/100% mode and physical routing remain hardware-validation items.

The documented active-low APB/core pairs at ``0xffef01403c`` and
``0xffef014040`` immediately reset timer components 0-3 and 4-7 respectively
when either bit is clear.  This is likewise a QEMU software contract rather
than a measured reset waveform.  Per-counter synthesized clocks, physical
clock-gate phase/reset effects, pulse-versus-level synthesis choices, exact
enable/reload/zero-count edges, wider bus transactions and cold/warm
reset-domain behavior remain hardware-validation items.  Only aligned 32-bit
accesses are currently accepted.

Upstream Linux leaves all eight TH1520 nodes disabled.  Its generic
``dw_apb_timer_of`` path also tries to create a PLIC-backed clockevent during
early ``time_init()``, before the RISC-V PLIC IRQ domain or the TH1520 clock
provider is available.  Enabling a node unchanged therefore is not a supported
mainline configuration.  A controlled Linux 7.2 validation build selected one
channel as a clocksource and supplied its known 125 MHz rate directly; the
standard DW APB clocksource code registered ``timer`` and booted through a
freestanding init.  Interrupt delivery is independently exercised by a
bare-metal payload through PLIC source 16.  These test-only Linux changes are
not part of QEMU or a proposed Linux fix.

The two application-domain watchdogs use a reusable Synopsys DesignWare APB
model.  It implements aligned 32-bit control, timeout-range, current-count,
restart, interrupt-status and read-to-clear EOI registers, plus configurable
component-parameter, version and type registers.  Enable is sticky until
reset.  Fixed timeout ranges count exact input-clock ticks; ``TOP_INIT`` is
used only for the first period after enable, while later stages and valid
``0x76`` restart writes use ``TOP``.  Reset mode invokes QEMU's configured
watchdog action on the first expiry.  Interrupt mode asserts the PLIC on the
first expiry and invokes that action after a second uncleared period.  Reading
EOI clears the interrupt without restarting the counter.  Register, timer,
IRQ and stage state migrates.

Both TH1520 instances use independent AP leaves at clock offset ``0x208`` bits
3/2, supplying a provisional 125 MHz while set and zero while clear.  Gating
freezes an enabled count and re-enabling resumes it, including across
migration.  This is a deterministic QEMU convention pending physical
comparison.  They use level-high PLIC sources 24/25 and the active-low AP reset
bits at ``0xffef014034`` and ``0xffef014038``.  Asserting either reset affects
only its corresponding watchdog in QEMU.  The generated nodes use
``snps,dw-wdt``, AP clock IDs 76/77 and reset IDs 3/4, but remain disabled
because the pinned upstream TH1520 DTS contains no watchdog nodes.  A
controlled external-DT test enabled both nodes
on Linux commit ``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` with
``CONFIG_DW_WATCHDOG`` and ``CONFIG_RESET_TH1520``.  Both watchdog-class
devices bound, started, reported positive time left, accepted a keepalive and
stopped through their reset controls.

The DesignWare component type ``0x44570120`` and fixed-TOP capability are
represented.  The TH1520 component version and the remaining synthesis
parameters have not been observed, so they deliberately read zero.  QEMU
currently selects ``0x0000ffff`` for the reset current-count value because
the available integration material conflicts with its field description.
Exact physical clock rate, gate phase/reset behavior, timeout edge convention,
reset pulse/scope and retention, component identities, access behavior outside
aligned 32-bit words, and the additional AO/audio watchdogs remain
owner-hardware validation items.

The RTC follows the DesignWare APB programming interface used by the Linux
``apm,xgene-rtc`` driver.  It implements the 32-bit counter, match and delayed
load registers; enable, interrupt-enable, interrupt-mask and wrap-enable
control; masked and raw status; read-to-clear EOI; component-version and the
optional prescaler registers.  The initial counter comes from QEMU's ``-rtc``
base.  A TH1520 guest can program ``CPSR = 0x8000`` and the prescaler-enable
bit to divide the 32.768 kHz input to one counter update per second.  Match
events route to PLIC source 74.  Counter, prescaler phase, delayed load, alarm,
register and IRQ state migrate.  QEMU conservatively retains the counter but
clears control, alarm and prescaler configuration on system reset.

The generated RTC node remains disabled.  Vendor kernel commit
``b9cf70c75d2b7482195a94e754d59f8cfc9dda2c`` adds the TH1520 prescaler
sequence to its X-Gene driver, while mainline Linux commit
``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` has neither that sequence nor a
TH1520 RTC node.  A controlled external-DT test used a test-only module derived
from the mainline driver plus the attributed vendor prescaler writes.  Linux
registered ``rtc0``, set and read time advancing at 1 Hz, and received an
alarm through PLIC source 74.

The exact component version and reset values, whether the physical divisor is
``CPSR`` or ``CPSR + 1``, prescaler-current direction, wrap edge, battery
domain, cold/warm/domain retention, clock calibration and suspend wake path
remain owner-hardware validation items.  QEMU currently reports zero for the
unknown component version and prescaler reset, treats ``CPSR`` as the exact
divisor, applies a load on the next prescaled counter update and does not
model low-power wake.

The TH1520 mailbox matches the public upstream Linux binding: one 24 KiB local
resource plus three remote-ICU resources, AP clock IDs 72 through 75 and
level-high PLIC source 28.  The Linux driver accesses four 4 KiB local channel
windows and maps remote ICU0 16 KiB into its declared resource; QEMU preserves
that sparse layout.  It implements aligned 32-bit INFO0 through INFO7 and
generate registers for all used channels, along with the C910-local
status/clear/mask registers.  A remote event input is available only for a
future endpoint model and qtests; it raises the matching local event after
that endpoint has populated the local channel window.  System reset and VM
migration preserve the modeled state and recompute the PLIC output.

The E902, C906 and C910R endpoints, their interrupt-controller behavior, AON
RPC firmware protocol, remote acknowledgment timing, clock-gate/reset effects,
access widths outside the driver-used 32-bit registers and all power/wake
semantics remain unmodeled.  The generated mailbox node therefore describes a
bounded CPU-visible transport, not a working AON or auxiliary-processor
service.

The MR75203 PVT controller matches the four-resource ``moortec,mr75203`` node
in pinned upstream Linux.  Its common, temperature, process and voltage
apertures are at ``0xfffff4e000``, ``0xfffff4e080``, ``0xfffff4e180`` and
``0xfffff4e800``.  QEMU reports the TH1520 component and synthesis identity,
uses the DT calibration coefficients and implements the SDIF command sequence
used by Linux.  A pinned kernel binds the generated node and reads both
temperature sensors and all 16 voltage-monitor channels.

The environment presented to the guest can be changed at runtime with monitor
``qom-set`` commands.  Temperatures use milli-degrees Celsius; voltages use
millivolts; process samples are unscaled 16-bit values.  The defaults are 25 C,
800 mV and zero process samples.

.. code-block:: none

   (qemu) qom-set /machine/soc/pvt temperature[0] 42000
   (qemu) qom-set /machine/soc/pvt voltage[3] 900
   (qemu) qom-set /machine/soc/pvt process-sample[0] 1234

Valid indices are ``temperature[0..1]``, ``voltage[0..15]`` and
``process-sample[0..10]``.  Environment inputs deliberately survive a guest
system reset and migrate with the VM, while guest-programmed PVT registers
reset normally.

This is a deterministic software-facing sensor model, not an analog or thermal
simulation.  Conversions configured for continuous operation become ready
without a modeled delay.  Alarm comparators and status/masks, the controller
timer, conversion latency, DONE clearing/rearming, interrupt aggregation and
the physical rail names are not modeled.  The mainline node has no interrupt
property, so QEMU does not invent a PLIC route.

The six GPIO controllers use a reusable one-port DesignWare APB model.  The
model implements software data and direction, external pin sampling, combined
edge/level interrupt generation, polarity, enable/mask, edge EOI, synchronous
sampling selection, reset and migration.  Linux commit
``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` binds all six generated nodes.
Focused qtests cover all bank widths, addresses, PLIC sources, AP clock IDs,
DT aliases, five LED descriptions, pin input/output, interrupt modes and a
pending edge across migration.

The board model connects five active-high blue user LEDs to GPIO4 pins 8-12
and represents the green power LED as continuously lit.  The schematic routes
LED1-LED5 through DMG1012T transistors from those five GPIOs, and the pinned BOM
identifies their fitted parts as blue while LED6 is green.  Their QOM paths are
``/machine/usr0`` through ``/machine/usr4`` and ``/machine/power``; each has a
read-only ``intensity-percent`` property.  GPIO data has no visible effect
until its direction bit selects output.  Guest reset extinguishes the five
user LEDs and leaves the power LED on, and user-LED intensity migrates with the
GPIO state.

Linux commit ``2709dd5ae32f0828f386327c76bba9f39f63a1c6``, rebuilt with its
standard ``gpio-leds`` driver, exposes ``led1`` through ``led5`` in sysfs.  A
freestanding initramfs turned all five on and then selected LEDs 1, 3 and 5;
the matching QEMU trace recorded blue ``USR0``-``USR4`` intensity changes.
This validates the software path but not the exact brightness or first-power-
up behavior of the owner's board.

Several GPIO details remain deliberately provisional.  QEMU follows the Linux
driver convention that a set direction bit means output, despite ambiguous
wording in the publicly hosted TH1520 manual.  Debounce selection is retained
but no temporal filter is applied, hardware-controlled port functions are not
present, and synthesis identification registers default to zero.  Undriven
inputs deterministically read low; this is not a claim about physical pulls.

The three pad-controller instances preserve software-visible PADCFG and
MUXCFG state, documented digital reset values and writable masks.  Their exact
apertures, 73.728 MHz always-on clock, AP clock IDs 45 and 47, GPIO ranges and
board LED, GMAC0, UART0 and Wi-Fi pin groups match pinned upstream Linux.  The
same kernel binds all three ``pinctrl-th1520`` devices.  Focused qtests cover
all reset words, representative writable and reserved masks, system reset, the
complete DT contract and migration of distinct state in all three instances.

Pad state does not yet produce electrical signal routing.  Mux selection does
not redirect GPIO or peripheral lines, and pull, voltage-domain, drive-current,
slew, Schmitt-trigger, output-enable, tri-state and contention behavior are not
modeled.  The separately source-backed GPIO3_21 PHY reset and GPIO3_22 PHY
interrupt wires are the only Ethernet exception; Wi-Fi, card-detect, buttons
and expansion-header connections remain unwired until their polarity, safe
power sequence and physical routing are validated.

The three storage controllers use a reusable DesignWare MSHC wrapper around
QEMU's SDHCI engine.  The model exposes the TH1520's 64 KiB apertures, vendor
area pointers, host-version and capability registers, software-visible PHY
configuration, deterministic PHY power-good/DLL-lock behavior, tuning control,
and per-instance interrupt routing.  Linux commit
``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` probes all three instances and
uses 64-bit ADMA; with a 64 MiB image on unit 0 it reports
``mmc1: new HS400 MMC card`` and reaches the requested root-device wait in
both the normal and dependency-minimal QEMU builds.  The Linux host number is
probe-order dependent and is not the QEMU drive unit.

The Ahead eMMC attachment selects a synthetic eMMC 5.1 speed profile.  Its
``EXT_CSD_REV`` is 8 and ``CARD_TYPE`` is ``0x57``, advertising HS26/HS52,
DDR52, HS200 and HS400 only at 1.8 V.  ``GENERIC_CMD6_TIME`` is 50, a
conservative 500 ms fallback rather than a measured property of the fitted
card.  ``STROBE_SUPPORT`` and ``DRIVER_STRENGTH`` remain zero, and only the
default Type 0 driver strength is accepted.  CMD21 returns the standard
64-byte four-bit or 128-byte eight-bit HS200 tuning block; SDHCI Execute
Tuning consumes the block internally and selects the tuned sampling clock.
The reference qtest workflow exercises HS200 and CMD21 before moving through
HS and DDR8 to HS400.  The card has no synthetic tuning-history latch and
requires only the immediate HS-plus-DDR8 predecessor state for HS400.  Invalid
direct transitions are rejected with ``SWITCH_ERROR``.  The opt-in is local
to this board, so the generic QEMU eMMC defaults are unchanged.

This profile is a guest-software contract, not identification of the physical
16 GiB device.  Three focused SD/eMMC profile, tuning, reset and migration
qtests pass, including an old-binary fail-before comparison.  At that profile
checkpoint, the complete normal, dependency-minimal and ASan/UBSan board gates
were respectively 113/113, 112/112 and 112/112.  The earlier 13-test storage
subset also passed
under ASan/UBSan.  A QEMU trace of the pinned Linux run records the CMD6
HS200-to-HS-to-DDR8-to-HS400 transitions but no CMD21.  This is expected from
that kernel: the TH1520 platform callback returns success without issuing
CMD21 while preparing HS400.  Linux therefore validates EXT_CSD negotiation,
CMD6 and HS400 enumeration, while the qtests validate CMD21 data and tuning
interrupt semantics.  The fitted part, CID, CSD, complete EXT_CSD contents,
voltage behavior and electrical/analog HS200/HS400 timing still require
owner-board validation.  Command Queue Engine
and ADMA3 execution, the mask-ROM boot datapath, analog tuning failures,
card-detect/write-protect GPIO wiring, eMMC boot/RPMB details, and the CYW43012
SDIO function are not implemented.  Synthesis version IDs default to zero and
can be overridden for testing; capability voltage bits and several reset
values remain hardware-validation items.

Both GMAC cores use a reusable DWMAC 3.x functional model.  GMAC0 has the
board's RGMII/RTL8211F-facing DT connection and accepts a normal QEMU network
backend; GMAC1 is present at the SoC level but disabled in the board DT because
the board routes no second PHY.  Mainline Linux binds GMAC0 as DWMAC1000 and
observes the same user/version identity and advertised checksum/extended-
descriptor features as a public physical boot capture.  Checkpoint
``1369cec4d9`` implements DMA RIWT[7:0] for Linux receive-interrupt mitigation.

The generated GMAC0 PHY node carries the mainline active-low GPIO3_21 reset
specifier, its 10 ms assertion and 50 ms recovery delays, and the active-low
GPIO3_22 interrupt specifier.  QEMU routes those digital GPIO lines: asserted
reset reinitializes the generic Clause-22 PHY state and clears its link status,
while the unimplemented PHY interrupt remains deasserted (high).  The delay
values are consumed by guest software; QEMU does not claim RTL8211F electrical
timing, straps, vendor registers, link training or interrupt-source behavior.
The reset state is migrated.

Each unit is 256 clock cycles; TH1520 currently supplies a fixed 500 MHz
reset/reference-rate assumption, so Linux's default ``0xa0`` expires after
exactly 81,920 ns.  A DIC-suppressed terminal receive completion arms the
one-shot; expiry raises RI/NIS and the PLIC line, while a non-DIC completion,
zero write or reset cancels it.  Current VMState version 2 preserves an armed
deadline.  Older streams retain the low register byte but load unarmed because
they contain no deadline.

The complete GMAC qtest group passes 14/14.  Four clean normal/minimal
one-/four-hart Linux runs pass DHCP, 3/3 gateway pings and a 1 MiB HTTP
SHA-256 download.  A retained contention run exposes a Linux masked-RI race:
RIWT expires while RIE is masked, then an unrelated TX interrupt W1C-clears RI
without scheduling RX and temporarily strands three completed descriptors.
QEMU does not hide that timing with a device-model workaround.  Programmable
MAC/VLAN/hash filtering, complete checksum corner cases, sustained contention
stress, dynamic RIWT clock/gate behavior, physical RIWT timing, PTP/MMC/WOL/
EEE, flow-control timing and RTL8211F vendor pages, electrical delays, straps,
link training and interrupt-source behavior remain incomplete.

The general AXI DMAC implements the four-channel software path used by the
mainline ``dw-axi-dmac`` driver.  Direct and 64-byte-LLI memory-to-memory
transfers support 64-bit addresses, programmable widths and increment/fixed
addresses, valid/last writeback, channel status, combined interrupts, reset
and migration.  A pinned mainline kernel probes all four channels; Linux
``dmatest`` completed 20 randomized copies up to 1 MiB on every channel with
zero failures.  Transfers complete synchronously in QEMU.  Peripheral
request/handshake wiring, the secure/TEE controller, contiguous/reload/shadow/
cyclic modes, dynamic LLI extension, detailed bus errors, mid-transfer timing,
arbitration/QoS and observable noncoherent-cache effects are not modeled.
Identification/version reset values are provisional pending owner-board reads.

An upstream Linux image built from commit
``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` has been exercised with both
the full and dependency-minimal QEMU builds.  OpenSBI passes the C910 identity
to S-mode, Linux activates the T-Head noncoherent cache-maintenance path,
brings up four harts, uses earlycon, and binds the DesignWare UART as
``ttyS0``.  A variant with ``CONFIG_EEPROM_AT24=y`` also binds I2C0 and reads
all 4096 bytes of the synthetic erased board EEPROM.  A separate variant with
``CONFIG_SENSORS_MR75203=y`` binds the PVT controller and reads two temperature
and 16 voltage hwmon channels.  The USB validation variant enumerates a QEMU
keyboard through the TH1520 DWC3/xHCI path using the test-only glue described
above.  With no block device
attached, the expected endpoint is a
missing-root-filesystem panic; this is a bring-up test, not a claim that a
production image is supported.

Six repeated boots with each of the full and dependency-minimal QEMU builds
selected OpenSBI boot hart 0 and brought up all four CPUs.  This validates the
emulator's deterministic direct-boot convention only; it does not establish
the silicon reset sequence.

A whole-machine migration regression moves DRAM, SRAM, per-hart architectural
and C910-specific CSR state including FXCR/FRM/FFLAGS, the rotating CPUID
cursor, architectural time,
CLINT, PLIC, all six UARTs, all six I2C controllers, board EEPROM, SPI0, both
APB timer components, both AP watchdogs, the RTC, TH1520 mailbox, MR75203 PVT,
all six GPIO controllers, five user-LED intensities, all three pad controllers,
storage, GMAC, the USB miscellaneous and DRD wrappers, DWC3 and xHCI state in
one stream.
Focused migration tests additionally preserve an in-flight I2C read and
EEPROM address pointer, two running watchdogs at different stages, a running
APB timer with a latched interrupt, a running APB timer high/low toggle phase,
a running TH1520 PWM phase with a pending update, AP and miscellaneous raw
clock-gate levels, gated timer/watchdog counts and gated output phases, a
mailbox event and remote-window data,
plus completed AXI-DMAC
data/register/interrupt state.  The PVT migration test preserves guest
registers, sample counters, temperature/voltage inputs and their resulting
conversions.  The focused RTC migration test preserves counter and prescaler
phase, match/control state and a future PLIC alarm.  Together with the focused
device tests, an armed DWC GMAC RIWT deadline now survives current-version
migration: a half-expired timer remains quiet through remaining time minus one
nanosecond and expires on the next.  Pre-version-2 GMAC streams load the masked
register value unarmed because their missing deadline cannot be reconstructed.
The board gate passes 153/153 normal, 152/152 dependency-minimal and
152/152 ASan/UBSan.  The complete normal
RISC-V TCG gate passes 38/38, and the explicitly enumerated Ahead-specific
minimal TCG gate passes
15/15, and all seven XTheadVector firmware payloads pass directly under
ASan/UBSan.  The preceding migration checkpoint's 114/114 board plus 14/14 CSR
normal, 113/113 plus 7/7 minimal and 112/112 sanitizer-board totals remain
historical evidence.  The 48-stage C910 FXCR guest passes in all three
configurations.  Generic SoftFloat passes
its 17/17 quick suite and the slow ``fp-test-mulAdd`` FMA test.  The only
conditional omission is the keyboard-hotplug test because the deliberately
minimal configurations exclude ``usb-kbd``; their
register/reset/DMA/IRQ/migration USB tests still run.  The instrumented C910
vector/PMU/MAEE, CLINT, PLIC, UART and four-hart payloads pass
without sanitizer findings.  A bounded instrumented Linux run reaches the
C900 PLIC probe after bringing up all four CPUs; the normal builds separately
cover the later native UART handoff and expected missing-root panic.  ASan's
warning that it does not fully support QEMU's ``makecontext``/``swapcontext``
coroutines is expected and was not accompanied by an ASan/UBSan finding.
Dedicated current, synthetic pre-version-3 and genuine historical C910 CSR
migration gates pass.  Their documented old-wire ambiguities, physical reset
behavior and silicon state remain open.  External backends and peers do not
migrate; queued packets, in-flight storage and GMAC DMA ownership, plus active
or attached USB migration also remain open.  The fixed 500 MHz RIWT conversion
does not follow dynamic clock-rate/gate changes and is not a measurement of the
owner's silicon.

QSPI/XIP, board SPI peripherals, timer cascade and physical toggle routing,
PVT alarm/timer/IRQ and analog timing fidelity, non-application watchdogs,
USB device/OTG, PHY and
recovery-mode behavior, display, audio, camera, video codecs, GPU, NPU, the
C906 and E902 auxiliary cores, DSPs, security blocks, the secure DMA
controller, board buttons, and Wi-Fi/Bluetooth are not modeled yet.
Electrical pad routing and GPIO-connected PHY/Wi-Fi/card-detect signals are
not wired yet.  The remaining storage, Ethernet and general-DMA gaps are
listed above.
The development plan and the hardware differential-validation ledger are in
``docs/devel/beaglev-ahead-emulation-plan.md`` and
``docs/devel/beaglev-ahead-hardware-validation.md``.

Minimal build
-------------

To exclude unrelated boards and devices at build time without deleting shared
source files:

.. code-block:: bash

   mkdir build-beaglev-ahead-minimal
   cd build-beaglev-ahead-minimal
   ../configure --target-list=riscv64-softmmu \
       --without-default-devices \
       --with-devices-riscv64=beaglev-ahead \
       --disable-docs
   ninja qemu-system-riscv64

Source deletion is deferred until the machine's complete dependency closure is
covered by tests.  This keeps reusable QEMU devices available while the board
model is still growing.
