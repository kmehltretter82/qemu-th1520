# BeagleV Ahead / TH1520 physical PMA design and probe plan

Status: physical-PMA plumbing and synthetic validation implemented; exact
TH1520 table blocked on integration evidence or physical-board results,
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

## QEMU implementation

The branch implements the hardware table as C910 integration data.  It does
not ask QEMU whether an address currently resolves to RAM or MMIO:

```c
typedef struct RISCVTHeadPMARegion {
    uint64_t upper_page; /* upper-exclusive physical address >> 12 */
    uint8_t attributes;  /* SO/C/B/SH/SEC in bits 4:0 */
} RISCVTHeadPMARegion;
```

The CPU configuration contains eight entries, a default attribute and an
explicit validity flag.  A generic ``thead-c910`` CPU and the BeagleV Ahead
machine both leave it invalid, so neither silently inherits the OpenC910
fixture or guessed TH1520 data.  Once established, the TH1520 SoC must supply
its values before CPU realization because integrations can differ.

One shared helper now performs lookup and permission application in the
target's address-translation path.  Every successful first-stage translation
path passes through it rather than duplicating selection around the page-table
walker:

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

The helper applies SO instruction permission for both PTE-selected and
physical attributes, so direct/Bare returns cannot install an executable
mapping before considering the physical attribute.  The AMO/vector check no
longer returns early merely because MAEE is clear: the selected full-TLB
attribute's validity is authoritative.  Physical C=0 AMO and physical SO
vector restrictions are therefore active precisely when the physical table
supplied that entry.  Scalar alignment consumes the same retained attribute.

An invalid/unconfigured table should preserve today's explicit incomplete
behavior: PTE bits are ignored with MAEE clear, but no physical attribute is
marked valid.  It must emit no invented SO/C/B/SH/SEC behavior.  Once TH1520
values are supplied, add a machine-version compatibility rule before changing
them in a released machine.

The table is not a mutable guest property.  OpenC910's generated C910 sysmap
is synthesis input rather than an architected CSR bank.  The experimental TCG
CPU option used by tests has this form:

``x-thead-pma=<upper>:<attr>/.../default:<attr>``

It requires exactly eight strictly increasing, 4 KiB-aligned physical upper
addresses within the CPU's physical-address width and a five-bit attribute
for every region and the default.  It is rejected without XTheadMaee or TCG.
The option exists to validate integration plumbing and is not set by the
BeagleV Ahead machine or part of its guest ABI.

## Synthetic regression and remaining hardware matrix

``test-thead-c910-physical-pma`` is a freestanding payload distinct from the
PTE-attribute test.  Its linker places pages immediately below and at all
eight synthetic upper bounds, followed by a default-region page.  It checks
38 exact traps and successful counterparts across:

* M-mode direct access and S-mode ``satp.MODE=Bare`` with MAEE set and clear;
* S/U Sv39 with MAEE set, where deliberately contrary PTE attributes win;
* S/U Sv39 with MAEE clear, where the physical table wins;
* normal, strong-order and C=0 non-cacheable regions, plus the default;
* strong-order scalar alignment, vector load and instruction-fetch faults;
* C=0 AMO.W access faults while LR.W/SC.W remains permitted; and
* first-fill and cached full-TLB paths with exact causes and ``tval`` values.

The configuration is intentionally synthetic and proves QEMU plumbing only.
It is not a candidate TH1520 table.  The committed focused test passes normal,
dependency-minimal and ASan/UBSan builds; ASan prints its expected
``makecontext`` warning.

Once real values are known, adapt a hardware-safe version to classify the
same physical sample addresses in Bare mode and Sv39 with MAEE clear.

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

The TCG test covers realization, lookup, every boundary, the default and the
observable SO/C rules in M/S/U modes, MAEE on/off, Bare/Sv39, first-miss and
cached paths.  Reset invariance and post-migration TLB refill remain to be
added.  B/SH/SEC require a separate transport or ``MemTxAttrs`` test only
after their TH1520 meaning is established.

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
