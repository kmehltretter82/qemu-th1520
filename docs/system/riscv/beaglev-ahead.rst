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
* a DesignWare APB UART0 at ``0xffe7014000``, including the 16550 register
  bank, DesignWare status/reset/probe registers, busy detection, and PLIC
  interrupt 36.

The generated device tree uses the same board, CPU, PLIC, CLINT, UART, memory,
and cache topology bindings as upstream Linux's BeagleV Ahead device tree.  It
advertises ``xtheadvector`` and ``thead,vlenb = <16>`` for every C910 hart.

Boot options
------------

Direct boot uses QEMU's bundled generic OpenSBI by default.  For example:

.. code-block:: bash

   qemu-system-riscv64 -M beaglev-ahead \
       -kernel Image \
       -initrd rootfs.cpio.gz \
       -append "console=ttyS0,115200 earlycon" \
       -nographic

QEMU loads firmware at the beginning of RAM, places a supplied Linux kernel
after it, generates the device tree, and installs a small reset trampoline in
the mask-ROM aperture.  ``-bios none`` is also accepted for low-level tests
that load all code explicitly.

All four C910 harts currently enter that trampoline.  The FW_DYNAMIC handoff
selects hart 0 for relocation, and an OpenSBI configuration node restricts its
subsequent cold-boot lottery to hart 0.  OpenSBI consumes that node before the
next boot stage.  This makes direct boots deterministic while leaving the
other harts available for SBI HSM startup; a four-hart M-mode test checks the
ordered UART transcript from harts 0 through 3.

This reset trampoline and OpenSBI convention are not an emulation of the
TH1520 mask ROM or reset controller.  The physical initial hart states, Core0
TEE mode and secondary release sequence remain hardware-validation items.
Boot straps, the USB/UART downloader, and booting from eMMC, SD, or QSPI are
later milestones.

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

UART0 uses a reusable DesignWare APB wrapper around QEMU's 16550 core.  It
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

An upstream Linux image built from commit
``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` has been exercised with both
the full and dependency-minimal QEMU builds.  OpenSBI passes the C910 identity
to S-mode, Linux activates the T-Head noncoherent cache-maintenance path,
brings up four harts, uses earlycon, and binds the DesignWare UART as
``ttyS0``.  With no block device attached, the expected endpoint is a
missing-root-filesystem panic; this is a bring-up test, not a claim that a
production image is supported.

Six repeated boots with each of the full and dependency-minimal QEMU builds
selected OpenSBI boot hart 0 and brought up all four CPUs.  This validates the
emulator's deterministic direct-boot convention only; it does not establish
the silicon reset sequence.

A whole-machine migration regression moves DRAM, SRAM, per-hart architectural
and C910-specific CSR state, the rotating CPUID cursor, architectural time,
CLINT, PLIC and UART state in one stream.  Together with the focused device
tests, it runs in the full, dependency-minimal and ASan/UBSan builds.  The
instrumented C910 vector/PMU, CLINT, PLIC, UART and four-hart payloads pass
without sanitizer findings.  A bounded instrumented Linux run reaches the
C900 PLIC probe after bringing up all four CPUs; the normal builds separately
cover the later native UART handoff and expected missing-root panic.  ASan's
warning that it does not fully support QEMU's ``makecontext``/``swapcontext``
coroutines is expected and is not counted as a clean sanitizer finding.

Storage, Ethernet, DMA, GPIO/pinctrl, I2C/SPI/PWM, RTC/watchdog, USB, PCIe,
display, audio, camera, video codecs, GPU, NPU, the C906 and E902 auxiliary
cores, DSPs, security blocks, and board Wi-Fi/Bluetooth are not modeled yet.
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
