# Draft confidential report: NPCM GMAC receive FCS over-read

This is a local review draft.  **Do not submit it** until a human reviewer
has checked the reproduction, repeated it on a clean current-master build,
and explicitly authorized a confidential QEMU GitLab work item.  The report
must say at the beginning that an AI/LLM coding agent assisted discovery and
analysis, followed by human review.

## Summary

`hw/net/npcm_gmac.c:gmac_receive()` adds `ETH_FCS_LEN` to the received packet
length, but the network callback buffer contains only the original packet.
When an RX packet was queued because DMA was stopped, the queue allocation is
exactly the packet length.  Enabling DMA flushes that queue and causes
`gmac_rx_transfer_frame_to_buffer()` to read four bytes past the allocation
while copying the frame into guest RAM.

The observed impact is a guest-reachable host heap out-of-bounds read and an
incorrect FCS.  Whether the four bytes disclose security-sensitive host data
and which QEMU security boundary applies require maintainer triage.  This
draft makes no CVE, exploitability, or arbitrary-code-execution claim.

## Affected revision and code

* QEMU master: `bde2492aace2b5acb755a5b057013e915163a77f` (2026-08-24 audit and rerun)
* `hw/net/npcm_gmac.c:gmac_receive()` and
  `gmac_rx_transfer_frame_to_buffer()`
* `net/queue.c:qemu_net_queue_append()` and
  `qemu_net_queue_flush()`

The affected source paths are unchanged in the disposable sanitizer build
used for the trace, `eea8fe61b8be8f3016e522e6af24924a0266ca95`.

A fresh disposable ASan/UBSan `aarch64-softmmu` binary was also built from
the pinned current-master tree at `bde2492aace2b5acb755a5b057013e915163a77f`
on 2026-08-24.  The exact sequence below reproduced the failure there; QEMU
aborted with return code `-6` before replying to the final qtest command.

## Minimal reproduction

Build an isolated sanitizer binary from a clean upstream checkout:

```sh
./configure --target-list=aarch64-softmmu --enable-asan --enable-ubsan \
    --disable-docs --disable-tools --disable-guest-agent --disable-werror
ninja qemu-system-aarch64
```

Run QEMU with a qtest controller and a TCP loopback socket backend.  The
controller supplies `PORT` by listening on `127.0.0.1:PORT`, then starts:

```sh
qemu-system-aarch64 -M npcm845-evb -display none -monitor none \
    -qtest stdio \
    -nic socket,connect=127.0.0.1:PORT,model=npcm-gmac
```

The controller sends these qtest commands, accepting `OK` after each one:

```text
endianness
write 0x100000 0x10 0x00000080ff0700000000110000000000
writel 0xF080300C 0x00100000
writel 0xF080301C 0x00010040
writel 0xF0802000 0x00000004
```

The descriptor at `0x00100000` has DMA ownership, a 2047-byte receive
buffer, and guest buffer address `0x00110000`.  The MAC accepts RX, but DMA RX
is intentionally still stopped, so the incoming packet is placed in QEMU's
network queue.  Send one 64-byte Ethernet payload through the socket stream:

```text
4-byte network-order length (0x00000040), followed by 64 payload bytes
```

Finally send:

```text
writel 0xF0803018 0x00000002
```

This enables DMA RX and invokes `qemu_flush_queued_packets()`.  The sanitizer
process aborts before returning `OK` for that command.

## Observed sanitizer result

The relevant output from ASan/UBSan is:

```text
AddressSanitizer: heap-buffer-overflow
READ of size 68
qemu_ram_move ... system/physmem.c:3163
gmac_rx_transfer_frame_to_buffer ... hw/net/npcm_gmac.c:293
gmac_receive ... hw/net/npcm_gmac.c:390
allocation: qemu_net_queue_append ... net/queue.c:105
allocated region: 104 bytes; read ends at its boundary
```

The 104-byte allocation is the queue object plus the 64-byte packet.  The
receive helper requests 68 bytes, so the final four bytes are outside the
packet allocation.  The stack-buffer direct-delivery path may not trip ASan:
QEMU's socket backend uses a large `NET_BUFSIZE` stack buffer, and normal QEMU
build flags initialize that buffer.  The queued path is therefore the
required reproducer.

## Duplicate and disclosure checks

The closest public report, [QEMU #3202](https://gitlab.com/qemu-project/qemu/-/work_items/3202),
is a distinct NPCM GMAC **transmit** allocation-size truncation.  This draft
concerns the receive path and the FCS length handling.  Repeat a human search
of GitLab and qemu-devel immediately before submission.

Because this is a possible host-memory-safety issue, submit initially as a
confidential work item according to QEMU's
[security process](https://www.qemu.org/contribute/security-process/), and
include no private board data, credentials, proprietary manuals, or local
filesystem paths.

## Local disposition

* Status: confidential-triage-ready; not submitted.
* Local fix/reference: branch commit `95af4a301b` allocates a bounded
  `len + ETH_FCS_LEN` frame and computes the CRC32.
* Current-master ASan/UBSan rerun: completed on 2026-08-24 at the pinned
  revision; the trace above is the result.
* Required before submission: human review, a fresh duplicate search, and
  user authorization for the external disclosure.
