# BeagleV Ahead / TH1520 physical PMA design and probe plan

Status: blocked on TH1520 integration evidence or physical-board results,
2026-08-24

This document defines how QEMU should model the C910 physical system map used
when address translation is disabled or when ``MXSTATUS.MAEE`` is clear.  It
also records what is still unknown so that an implementation does not turn a
generic OpenC910 example or a QEMU ``MemoryRegion`` type into an undocumented
TH1520 hardware claim.

The broader status and hardware rules are in
[the emulation plan](beaglev-ahead-emulation-plan.md) and
[hardware-validation ledger](beaglev-ahead-hardware-validation.md), especially
``CPU-004``.  The upstream-report classification is in
[the bug handoff](beaglev-ahead-upstream-bugs.md), local finding ``UQ-L005``.

## Established architectural contract

Official OpenC910 RTL commit
``b91c90914c19f114d35c8f6b73408eb241ed847c`` establishes the following:

* ``ct_mmu_sysmap.v`` divides the physical address space into eight contiguous
  regions.  Each region has an upper-exclusive, 4 KiB-aligned boundary and a
  five-bit SO/C/B/SH/SEC attribute.
* Region zero begins at physical address zero.  Every later region begins at
  the preceding upper boundary.  The generated implementation has a default
  attribute for an address outside all eight configured ranges.
* ``ct_mmu_ptw.v`` selects PTE[63:59] only while ``MXSTATUS.MAEE`` is set.
  With MAEE clear it selects the physical system-map attribute; its page-fault
  expression does not reinterpret the five PTE bits as standard reserved bits.
* the data and instruction TLB paths select the physical system-map attribute
  when translation is disabled as well.
* the LSU consumes at least SO and C architecturally: SO changes scalar
  alignment/fetch behavior and rejects vector accesses, while C=0 rejects AMO
  read-modify-write operations without rejecting LR/SC.

The generated ``sysmap.h`` in that repository is not a TH1520 source.  Its
boundaries describe low physical ranges and a different memory map, whereas
TH1520 places the C910-visible SRAM, controllers, PLIC, CLINT and mask ROM in
the high 40-bit ``0xff...`` region.  Those constants must never be copied into
the BeagleV Ahead machine.

## Pinned TH1520 software audit

The following public software was checked as negative as well as positive
evidence:

| Source | Pin | Relevant result |
| --- | --- | --- |
| RevyOS TH1520 vendor U-Boot, branch ``th1520`` | ``a13e24ed9ed773d4a07f079576a2fd654af6bfbb`` | C9xx setup writes ``CSR_MXSTATUS = 0x638000``, enabling MAEE.  No TH1520 ``SYSMAP_BASE_ADDR*`` or physical-attribute table is present. |
| RevyOS TH1520 vendor Linux, branch ``th1520-lts`` | ``a092d55649279e1c9bcda2769b8f6b4370fa2c94`` | Linux detects MAEE and emits T-Head normal/non-cacheable/IO attributes in PTE[63:59].  It does not describe the hardwired physical fallback map. |
| Mainline Linux evidence baseline | ``2709dd5ae32f0828f386327c76bba9f39f63a1c6`` | Establishes the software-visible TH1520 address map used elsewhere in this project, but not the C910 sysmap boundaries or five-bit attributes. |

The current vendor branches postdate some distributed board images, so their
absence of a table is not proof that no older integration source contains it.
Before hardware probing, repeat the search against the exact U-Boot, OpenSBI,
SPL, trusted firmware and device-tree commits identified from the owner's
stock image under ledger item ``DOC-002``.

## Facts still required

For each silicon stepping supported by the machine, obtain:

1. all eight upper-exclusive physical page boundaries;
2. all eight SO/C/B/SH/SEC values and the out-of-range default;
3. whether every C910 hart has the same synthesis;
4. the treatment of DRAM, SRAM, mask ROM, mapped devices, address-space holes,
   secure aliases and any DDR aperture above the board's fitted 4 GiB;
5. cold, warm and per-hart reset invariance; and
6. software-visible consequences of B, SH and SEC, including the corresponding
   AXI attributes and security checks.

The board's address map alone is insufficient.  A reserved hole can have a
normal PMA, a mapped ROM can be cacheable, and one sysmap region can cover a
mixture of implemented and unimplemented bus targets.

## QEMU implementation shape

Implement the hardware table as C910 integration data, not by asking QEMU
whether an address currently resolves to RAM or MMIO:

```c
typedef struct RISCVTHeadPMARegion {
    uint64_t upper_page; /* upper-exclusive physical address >> 12 */
    uint8_t attributes;  /* SO/C/B/SH/SEC in bits 4:0 */
} RISCVTHeadPMARegion;
```

The C910 configuration should contain eight entries, a default attribute and
an explicit validity flag.  The TH1520 SoC must supply the values before CPU
realization.  A generic ``thead-c910`` CPU must not silently inherit either
the OpenC910 fixture or the TH1520 table; integrations can differ.

The lookup and permission application belong in one shared helper in the
target's address-translation path.  Every successful first-stage translation
path must pass through it rather than duplicating the selection around the
page-table walker:

* M-mode, MMU-disabled and ``satp.MODE=Bare`` returns must attach the physical
  attribute before returning from ``get_physical_address()``;
* a translated first-stage access with MAEE clear must look up the final
  physical address after the leaf PPN and page offset are known;
* a translated first-stage access with MAEE set continues to use PTE[63:59],
  which takes precedence over the physical table;
* second-stage translation must not acquire C910 first-stage semantics;
* the full TLB entry must retain the selected attribute so AMO/vector checks,
  scalar alignment and instruction permission use one result; and
* a table boundary is page-aligned, so a normal target-page TLB entry cannot
  straddle two physical attributes.  Validate this assumption if QEMU is built
  with a target page larger than 4 KiB.

The helper must apply SO instruction permission for both PTE-selected and
physical attributes.  In particular, the existing direct/Bare returns must
not install executable mappings before the physical attribute is considered.
Likewise, ``riscv_thead_maee_check()`` currently returns success immediately
when MAEE is clear.  That gate must be replaced by the selected full-TLB
attribute's validity: physical C=0 AMO and physical SO vector restrictions are
active precisely when the physical table supplied that entry, even though
MAEE is clear.  Scalar alignment already consumes the retained TLB attribute
and should remain on the same authority.

An invalid/unconfigured table should preserve today's explicit incomplete
behavior: PTE bits are ignored with MAEE clear, but no physical attribute is
marked valid.  It must emit no invented SO/C/B/SH/SEC behavior.  Once TH1520
values are supplied, add a machine-version compatibility rule before changing
them in a released machine.

Do not make the table a mutable guest property.  OpenC910's generated C910
sysmap is synthesis input rather than an architected CSR bank.  A test-only
QOM override may be useful for unit coverage, but it must be clearly named and
must not become part of the BeagleV Ahead guest ABI.

## Regression matrix once values are known

Add a freestanding payload distinct from the existing PTE-attribute test.  It
must run with ``satp.MODE=Bare`` and again with Sv39 plus MAEE clear, and verify
that both paths classify the same physical sample addresses.

For disposable RAM and SRAM samples, cover:

* aligned and misaligned scalar byte/half/word/double loads and stores with
  MXSTATUS.MM set and clear;
* AMO.W/D versus LR/SC.W/D;
* XTheadVector e8/e16/e32/e64 unit, stride, index, segment and
  fault-only-first operations;
* instruction fetch only from a purpose-built executable scratch page; and
* addresses immediately below and at every established sysmap boundary.

For MMIO, ROM and reserved samples, start with aligned, documented,
side-effect-free reads.  Do not use AMOs, vector accesses, instruction fetch,
misaligned accesses or writes merely to classify a region.  Add them only for
a specifically reviewed scratch target with trap recovery and a proven reset
path.  A bus access fault identifies the target response, not the PMA, and
must not be used to infer an attribute bit.

Qtests should configure a synthetic eight-region table and cover realization,
the lookup algorithm, every boundary, the default, reset invariance and
post-migration TLB refill.  TCG tests should then prove the observable SO/C
rules in M/S/U modes, MAEE on/off, Bare/Sv39, first-miss and cached paths.
B/SH/SEC require a separate transport or ``MemTxAttrs`` test only after their
TH1520 meaning is established.

## Physical-board capture procedure

The first board session is inventory-only: preserve stock boot logs, exact
firmware hashes, DTBs, silicon/PCB markings and the reset ``MXSTATUS`` value.
Do not disable MAEE in a running stock kernel because its page tables contain
T-Head attributes and the resulting physical-map substitution can change
live memory semantics.

Use a dedicated recovery-aware M-mode image loaded from removable or otherwise
non-destructive media.  Park the other harts, disable interrupts and DMA, use
only firmware-declared disposable memory, install precise trap handlers, and
write each result to a reserved RAM transcript before printing it.  Restore
MAEE and all modified state before handing control to another stage.

Behavioral classification can prove only part of each five-bit value:

| Observation with MAEE clear/Bare | Inference |
| --- | --- |
| naturally aligned RAM AMO succeeds | C is likely one; confirm against trap cause, memory result and every hart |
| AMO raises the C910 zero-``tval`` store/AMO access fault while LR/SC succeeds | C is zero under the pinned LSU rule |
| ordinary scalar misalignment traps only for this physical range while MM is set | SO is likely one; exclude target bus-width and access-fault causes |
| vector access raises the C910 zero-``tval`` access fault on reviewed scratch memory | SO is one under the pinned LSU rule |
| executable scratch fetch raises an instruction access fault | SO is likely one; use only memory containing the probe's own known code |

No CPU-only software observation in the current model uniquely identifies B,
SH or SEC.  Resolve those from legal integration documentation, an approved
RTL/netlist configuration, or non-invasive interconnect tracing.  Never infer
them merely because Linux normally maps the address as IO or normal memory.

Store each capture with board serial redacted, PCB revision, silicon marking,
hart ID, cold/warm state, firmware hashes, exact probe hash, address, operation,
MXSTATUS/SATP, cause, ``tval``, register result and memory-before/after.  Keep
raw captures outside the source tree and add only reviewed expectations here.

## Acceptance gate

Physical PMA emulation is ready to merge only when:

* every boundary and five-bit value has authoritative provenance or repeatable
  results on the owner's identified stepping;
* the QEMU table reproduces Bare and MAEE-disabled Sv39 results on all harts;
* MAEE-enabled PTE attributes still override the physical table;
* focused normal, dependency-minimal and sanitizer tests pass together with
  the full RISC-V TCG and BeagleV Ahead qtest gates; and
* the hardware ledger records any stepping difference, unobservable B/SH/SEC
  behavior and source-provenance limitation without claiming equivalence.
