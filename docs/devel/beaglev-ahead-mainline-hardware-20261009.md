# BeagleV Ahead mainline hardware boot — 2026-10-09

The same unpatched mainline kernel used in the
[QEMU boot checkpoint](beaglev-ahead-mainline-20261009.md) booted on the
owner's physical BeagleV Ahead through a temporary kexec handover. All four
CPUs came online, the unchanged scalar/atomic/floating-point workload matched
the QEMU hashes on every hart, timer checks passed, and the runtime UART
accepted output. The test ran entirely from an initramfs with no storage
controllers enabled in its device tree.

Factory recovery verification is pending the owner's RESET-button press.
The test's software reboot reached `Restarting system` without returning to
factory firmware. No eMMC image, boot file or saved boot environment was changed.

## Kernel, firmware and RAM payload

* Linux commit: `af32da41b0327b9c6a37856ba82b6760d6c8d10e`, fetched from
  Torvalds mainline on 2026-10-09; source tree clean, no patches.
* Release: `7.3.0-rc6-beagle-qemu-20261009+`; clang/LLD 22.1.8. The
  [resolved configuration](beaglev-ahead-mainline-20261009.config) is unchanged
  from the preceding two QEMU boot lanes.
* Raw Image SHA-256:
  `e154599b4b81505364f96337bcd9fee0eb924c33b0709760eb0030391687c54c`.
* The factory resident firmware remains OpenSBI implementation version `0x9`,
  reporting SBI specification 0.3 and TIME, IPI, RFENCE, SRST and HSM extensions.
  No firmware was replaced during the handover.
* The upstream Ahead DT was adapted to advertise the stock kernel's usable
  RAM, `0x40200000..0xffffffff`, and disable all three MMC controllers. The
  lower memory range was excluded because the stock Linux kernel does not
  use it; this is not a full-4-GiB memory qualification.
* Command line:
  `console=ttyS0,115200 earlycon clk_ignore_unused rdinit=/init panic=10`.
  Unused-clock gating was deliberately suppressed for this first handover.
* Initramfs: static RV64GC BusyBox plus the existing, unchanged CPU and timer
  test ELFs and per-hart affinity helper. PID 1 mounts only devtmpfs, proc,
  sysfs and tmpfs, then runs the tests and attempts a reboot after 15 seconds.

The factory ELF-only loader initially required an external ELF container for
the raw Image. Its single segment preserves every Image byte and reserves the
Image header's 6,017,024-byte footprint. The container SHA-256 is
`25780a480860d90d0265e39cd4cc790e0e9e8d2aedef55805336576428290547`;
the adapted DTB is
`ac92140eb60bcb047cdf6822f882b14ed108d336a8e64477b4d06ac137e783cc`;
the initramfs is
`9e759014816622c74eda0c4fe2f258e26d6771e3198f04fb91088e0e0fb26f61`.
That exact payload first completed the RAM tests in QEMU. Its software reboot
also stalled; QEMU was subsequently terminated by the host harness. A zero
QEMU exit status in this capture does not demonstrate automatic reset.

## Loader and handover

The factory `kexec-tools 2.0.20.git` loader reads memory regions from the stock
DT and chooses an ELF base of `0x400000`, despite `--mem-min=0x40200000`.
Its segment checks then reject the load. It also rejects the `--cmdline`
spelling advertised by its own help; `--append` works. Both attempts stopped
before executing a kernel or changing the installed system.

Unmodified upstream
[kexec-tools 2.0.32](https://www.kernel.org/pub/linux/utils/kernel/kexec/kexec-tools-2.0.32.tar.xz)
was cross-built as a static RV64GC executable with Alpine's musl and compiler
runtime. The temporary binary was copied to `/tmp`, not installed over the
factory tool. Its SHA-256 is
`e69dc7be3980dcd9c3c1c0404b5234e7bce8fdc4663c37501c5791c06f3e0b64`.
It used `/proc/iomem`, accepted the load through the classic `kexec_load`
syscall, and selected these non-overlapping RAM segments:

| Segment | Address | Reserved size |
| --- | --- | --- |
| Kernel and entry | `0x41e00000` | `0x5bd000` |
| Updated DTB | `0xfff52000` | `0x6000` |
| Initramfs | `0xfff58000` | `0xa8000` |

After address and payload-hash verification, a passive 115200 8N1 UART capture
was started and `systemctl kexec` requested normal service shutdown and
filesystem unmounting. The console records the stock kernel stopping CPUs
1–3 and jumping from hart 0 to the new kernel. Wi-Fi was intentionally absent
from the minimal mainline configuration; UART provided the observation path.

## Hardware results

| Check | Physical mainline RAM boot |
| --- | --- |
| Kernel and initramfs userspace | Passed |
| CPUs online | `0-3` |
| Scalar, atomic and floating-point workload | All four hart hashes match QEMU and stock hardware |
| Timer reads | No backwards reads on any hart |
| 32 sleeps per hart | All returned 0; no early wakes |
| TIME/CLOCK_MONOTONIC median ratio | 2,999,507–2,999,670 Hz; within 5% of 3 MHz |
| Runtime `ttyS0` write | Passed; PLIC source 36 recorded |
| Ghostwrite mitigation | Enabled; status reports `xtheadvector disabled` |
| Software reset | Stalled at `Restarting system`; manual reset needed |

Each scalar output is 880,336 bytes / 15,720 records, with SHA-256
`cf891df2c8b7af547b3c9574a0801b9d68976828dd42ebf51f53556cb80a2c11`.
For this RAM boot the digest and byte count were captured over UART, rather
than transferring the full scalar files. All four 1,552-byte timer outputs
were transferred as base64 and decoded and checked on the host. The clock
ratio checks kernel time conversion, not an independent oscillator reference.
Legacy-vector workloads were not run in this physical mainline boot.

The immutable mainline console checkpoint is 33,470 bytes, SHA-256
`61d3bc964a68e14ea915d9f50f41b548fa35313c4ab94128f55d530507af1d23`.
Private source/helper snapshots, payloads, build and load logs, the console,
decoded timer bytes and machine-readable results are retained under
`beagle/captures/20261009-mainline-hardware/` in the owner's workspace.
Raw board/network identifiers are not included in this public report.

This proves a bounded mainline RAM boot under the factory resident firmware.
It does not qualify mainline Wi-Fi, Ethernet, eMMC, microSD, USB, display,
camera, analog behavior or the complete cold-boot firmware chain. No QEMU
hardware-behavior defect is established, and no uncertainty-ledger item is
closed by this checkpoint. The stock-loader placement failure and the software
reset stall are separate compatibility boundaries requiring further diagnosis.
