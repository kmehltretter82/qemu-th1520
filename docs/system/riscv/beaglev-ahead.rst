BeagleV Ahead (``beaglev-ahead``)
================================

The ``beaglev-ahead`` machine models the BeagleV Ahead single-board computer,
which is built around the T-Head TH1520 SoC.  The initial implementation is a
boot-critical subset intended for direct firmware and kernel development.  It
does not yet run an unmodified production board image.

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
  SSIP, and STIMECMP banks and a 3 MHz architectural timer; and
* six DesignWare APB UARTs at their TH1520 addresses, including the 16550
  register banks, DesignWare status/reset/probe registers, busy detection,
  and PLIC interrupts 36 through 41.  UART0 at ``0xffe7014000`` is the board
  console and UART1 through UART5 are disabled in the board device tree;
* six DesignWare I2C controllers at ``0xffe7f20000``, ``0xffe7f24000``,
  ``0xffec00c000``, ``0xffec014000``, ``0xffe7f28000``, and
  ``0xfff7f2c000``, connected to PLIC sources 44 through 49.  I2C0 is enabled
  for the board's 4 KiB FT24C32A-compatible EEPROM at address ``0x50``; I2C1
  through I2C5 are present but board-disabled;
* two four-counter DesignWare APB timer components at ``0xffefc32000`` and
  ``0xffffc33000``.  Timers 0 through 7 count at 125 MHz and connect to PLIC
  sources 16 through 23.  All eight individual timer nodes remain disabled in
  the board device tree, matching upstream Linux;
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
  and migration are modeled;
* two DesignWare GMAC 3.x cores at ``0xffe7070000`` and ``0xffe7060000``
  with TH1520 APB glue, descriptor DMA, normal/enhanced descriptors, FCS,
  checksum status, Clause 22 MDIO/PHY state, PLIC sources 66/67 and migration.
  Board GMAC0 can use a QEMU network backend; GMAC1 is board-disabled; and
* a four-channel DesignWare AXI DMAC 1.01a at ``0xffefc00000`` on PLIC source
  27, with direct and linked-list memory-to-memory copies, 64-bit addresses,
  descriptor writeback, errors, interrupt aggregation, reset and migration.

The generated device tree uses the same board, CPU, PLIC, CLINT, UART, I2C,
APB timer, GPIO, pinctrl, storage, memory, and cache topology bindings as
upstream Linux's TH1520 device tree, augmented with the
schematic-established board EEPROM.  It advertises ``xtheadvector`` and
``thead,vlenb = <16>`` for every C910 hart.

Boot options
------------

Direct boot uses QEMU's bundled generic OpenSBI by default.  For example:

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

Passing ``-dtb file.dtb`` replaces the generated tree and passes that external
tree through the same firmware handoff.  This is useful for controlled driver
experiments; the supplied tree remains responsible for describing the real
machine topology correctly.

All four C910 harts currently enter that trampoline.  The FW_DYNAMIC handoff
selects hart 0 for relocation, and an OpenSBI configuration node restricts its
subsequent cold-boot lottery to hart 0.  OpenSBI consumes that node before the
next boot stage.  This makes direct boots deterministic while leaving the
other harts available for SBI HSM startup; a four-hart M-mode test checks the
ordered UART transcript from harts 0 through 3.

Storage unit 0 is attached as an eMMC device and unit 1 as a removable SD card.
Supplying either image makes it accessible to firmware and the operating
system, but does not select it as the reset boot source.  The reset trampoline
and OpenSBI convention are not an emulation of the TH1520 mask ROM or reset
controller.  The physical initial hart states, Core0 TEE mode and secondary
release sequence remain hardware-validation items.  Boot straps, the
USB/UART downloader, and mask-ROM booting from eMMC, SD, or QSPI are later
milestones.

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
debug register file, reset, and migration state.  Current regression coverage
is an architectural smoke test rather than the exhaustive and differential
coverage needed to claim silicon equivalence.  Likewise, MAEE PTE attribute
bits are accepted but their cacheability and ordering effects are not modeled;
some custom CSRs remain placeholders.  Fixed counters, TLB-miss events and the
C9xx overflow protocol are implemented, but cache, branch, pipeline and other
microarchitectural performance events are not yet hardware-accurate.  The
TH1520 integration exposes no writable PMP entries,
matching public physical-board boot captures, although generic C910
documentation describes optional PMP configurations.  These uncertainties
are itemized in the hardware validation ledger.

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
recovery, DMA handshakes, SMBus behavior and gate/reset coupling are absent.
Only the first 4 KiB of each 16 KiB described aperture is modeled.  High-speed
mode master-code/count defaults, the register-timeout value, reserved-aperture
responses and instance differences remain owner-hardware validation items.

I2C0 contains a 4 KiB ``atmel,24c32``-compatible device at address ``0x50``
with a 32-byte page size in the generated device tree.  EEPROM data and its
current-address pointer migrate, and an optional raw backing image persists
writes.  The pinned Linux DesignWare and AT24 drivers bind and read the entire
4096-byte erased image.  The EEPROM model does not yet reproduce page-wrap,
write-cycle busy time, endurance, power-loss or write-protect behavior.
Factory contents and layout, the fitted board revision and the schematic's
GPIO2_22-related write-protect network must be checked on the owner's board
before those behaviors are modeled.

The two APB timer components use a reusable four-counter DesignWare model.
Each counter has load, current-value, control, EOI and interrupt-status
registers at the 0x14-byte hardware stride.  Periodic and free-running
countdown, interrupt masking, raw and masked aggregate status, per-counter and
aggregate EOI, the component-version register, reset and migration are
modeled.  The TH1520 integration uses a common fixed 125 MHz input, component
version ``0x3231322a`` and eight independent level-high PLIC routes.

The model retains the four second-load and protection registers and preserves
the PWM control bit, but it does not infer cascade wiring or drive a physical
PWM output.  The second-load value consequently has no waveform effect.
Per-counter synthesized clocks, AP clock-gate/reset coupling, pulse-versus-
level synthesis choices, exact enable/reload/zero-count edges, wider bus
transactions and cold/warm reset-domain behavior remain hardware-validation
items.  Only aligned 32-bit accesses are currently accepted.

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

The six GPIO controllers use a reusable one-port DesignWare APB model.  The
model implements software data and direction, external pin sampling, combined
edge/level interrupt generation, polarity, enable/mask, edge EOI, synchronous
sampling selection, reset and migration.  Linux commit
``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` binds all six generated nodes.
Focused qtests cover all bank widths, addresses, PLIC sources, AP clock IDs,
DT aliases, five LED descriptions, pin input/output, interrupt modes and a
pending edge across migration.

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
modeled.  GPIO connections to the Wi-Fi module, Ethernet PHY, card detect,
buttons and expansion headers remain unwired until their polarity, safe power
sequence and physical routing are validated.

The three storage controllers use a reusable DesignWare MSHC wrapper around
QEMU's SDHCI engine.  The model exposes the TH1520's 64 KiB apertures, vendor
area pointers, host-version and capability registers, software-visible PHY
configuration, deterministic PHY power-good/DLL-lock behavior, tuning control,
and per-instance interrupt routing.  Linux commit
``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` probes all three instances and
uses 64-bit ADMA; with a 64 MiB image on unit 0 it enumerates a high-speed eMMC
block device and reaches the requested root-device wait.

This is not yet complete storage fidelity.  QEMU's eMMC card currently models
an eMMC 4.3-era command set rather than the board's 5.1-class, HS400-capable
part.  Command Queue Engine and ADMA3 execution, the mask-ROM boot datapath,
analog tuning failures, card-detect/write-protect GPIO wiring, eMMC boot/RPMB
details, and the CYW43012 SDIO function are not implemented.  Synthesis version
IDs default to zero and can be overridden for testing; capability voltage bits
and several reset values remain hardware-validation items.

Both GMAC cores use a reusable DWMAC 3.x functional model.  GMAC0 has the
board's RGMII/RTL8211F-facing DT connection and accepts a normal QEMU network
backend; GMAC1 is present at the SoC level but disabled in the board DT because
the board routes no second PHY.  Mainline Linux binds GMAC0 as DWMAC1000 and
observes the same user/version identity and advertised checksum/extended-
descriptor features as a public physical boot capture.  Programmable
MAC/VLAN/hash filtering, complete checksum corner cases, PTP/MMC/WOL/EEE,
flow-control timing and RTL8211F vendor pages, delays, GPIO reset and interrupt
behavior remain incomplete.

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
all 4096 bytes of the synthetic erased board EEPROM.  With no block device
attached, the expected endpoint is a
missing-root-filesystem panic; this is a bring-up test, not a claim that a
production image is supported.

Six repeated boots with each of the full and dependency-minimal QEMU builds
selected OpenSBI boot hart 0 and brought up all four CPUs.  This validates the
emulator's deterministic direct-boot convention only; it does not establish
the silicon reset sequence.

A whole-machine migration regression moves DRAM, SRAM, per-hart architectural
and C910-specific CSR state, the rotating CPUID cursor, architectural time,
CLINT, PLIC, all six UARTs, all six I2C controllers, board EEPROM, both APB
timer components, all six GPIO controllers, all three pad controllers, storage
and GMAC state in one stream.  Focused migration tests additionally preserve
an in-flight I2C read and EEPROM address pointer, a running APB timer with a
latched interrupt, plus completed AXI-DMAC data/register/interrupt state.
Together with the focused device tests, the complete 62-test board gate runs
in the full, dependency-minimal and ASan/UBSan builds.  The
instrumented C910 vector/PMU, CLINT, PLIC, UART and four-hart payloads pass
without sanitizer findings.  A bounded instrumented Linux run reaches the
C900 PLIC probe after bringing up all four CPUs; the normal builds separately
cover the later native UART handoff and expected missing-root panic.  ASan's
warning that it does not fully support QEMU's ``makecontext``/``swapcontext``
coroutines is expected and is not counted as a clean sanitizer finding.

SPI and timer PWM outputs, RTC/watchdog, USB, PCIe, display, audio, camera,
video codecs, GPU, NPU, the C906 and E902 auxiliary cores, DSPs, security
blocks, the secure DMA
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

   mkdir build-beaglev-ahead
   cd build-beaglev-ahead
   ../configure --target-list=riscv64-softmmu \
       --without-default-devices \
       --with-devices-riscv64=beaglev-ahead \
       --disable-docs
   ninja qemu-system-riscv64

Source deletion is deferred until the machine's complete dependency closure is
covered by tests.  This keeps reusable QEMU devices available while the board
model is still growing.
