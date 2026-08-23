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
* the C910 Privileged ISA 1.10 identity, Sv39 MMU, 40-bit physical addresses,
  and T-Head custom CSR aperture;
* 4 GiB RAM at ``0x0000000000``;
* 1.5 MiB SRAM at ``0xffe0000000``;
* the 1 MiB mask-ROM aperture at ``0xffffd00000``;
* a 240-source PLIC at ``0xffd8000000``;
* CLINT software interrupts and a 3 MHz timer at ``0xffdc000000``; and
* the UART0 16550 register subset at ``0xffe7014000`` and PLIC interrupt 36.

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

This reset trampoline is not an emulation of the TH1520 mask ROM.  Boot straps,
the USB/UART downloader, and booting from eMMC, SD, or QSPI are later
milestones.

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
some custom CSRs remain placeholders and performance events are not
hardware-accurate.  The TH1520 integration exposes no writable PMP entries,
matching public physical-board boot captures, although generic C910
documentation describes optional PMP configurations.  These uncertainties
are itemized in the hardware validation ledger.

Peripheral limitations
----------------------

UART0 currently uses QEMU's generic ``serial-mm`` 16550 subset.  Reads and
writes to DesignWare-specific registers are accepted by a low-priority
unimplemented aperture but do not have hardware behavior.

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
