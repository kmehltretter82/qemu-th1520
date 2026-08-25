#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Require full barriers and TB exits in every XTheadSync translation."""

import re
import sys
from pathlib import Path


EXPECTED = {"th.sync", "th.sync.s", "th.sync.i", "th.sync.is"}


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} QEMU_TCG_LOG", file=sys.stderr)
        return 2

    text = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
    verified = set()
    for block in text.split("----------------"):
        if "OP:" not in block:
            continue
        disassembly, operations = block.split("OP:", 1)
        mnemonics = re.findall(
            r"\b(th\.sync(?:\.is|\.i|\.s)?)\s*$",
            disassembly,
            re.MULTILINE,
        )
        insns = [line.strip() for line in disassembly.splitlines()
                 if line.strip().startswith("0x")]
        if (len(mnemonics) != 1 or not insns or
                not insns[-1].endswith(mnemonics[0])):
            continue

        main_path = operations.split("set_label $L0", 1)[0]
        ops = [line.strip() for line in main_path.splitlines() if line.strip()]
        sequence = ["mb seq:all", "add_i64 pc,pc,$0x4", "exit_tb $0x0"]
        if any(ops[i:i + len(sequence)] == sequence
               for i in range(len(ops) - len(sequence) + 1)):
            verified.add(mnemonics[0])

    missing = EXPECTED - verified
    if missing:
        print("missing ordered XTheadSync IR: " + ", ".join(sorted(missing)),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
