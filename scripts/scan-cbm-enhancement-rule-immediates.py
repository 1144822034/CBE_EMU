"""Read-only Thumb scan for item-controller rule-table offset constants.

This intentionally scans every halfword boundary so it can find candidate code
without relying on an IDA database.  Hits are candidates only; nearby bytes may
be literal data and must be confirmed in a disassembler before being cited.
"""

from pathlib import Path
import re
import sys

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs


TARGETS = (0x580, 0x584, 0x58C)
CBM_CODE_OFFSET = 0x9A


def main() -> None:
    roots = [Path(arg) for arg in sys.argv[1:]]
    if not roots:
        roots = [Path("bin/JHOnlineData")]

    files: list[Path] = []
    for root in roots:
        files.extend(sorted(root.glob("*.cbm")) if root.is_dir() else [root])

    decoder = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    for path in files:
        data = path.read_bytes()
        hits = 0
        split_hits: set[int] = set()
        for file_offset in range(CBM_CODE_OFFSET, len(data) - 4, 2):
            address = file_offset - CBM_CODE_OFFSET
            instructions = list(
                decoder.disasm(data[file_offset : file_offset + 4], address, count=1)
            )
            if not instructions:
                continue
            instruction = instructions[0]
            operands = instruction.op_str.lower()
            if instruction.mnemonic == "movs" and operands.endswith(", #0xb"):
                window = list(
                    decoder.disasm(
                        data[file_offset : file_offset + 32], address, count=12
                    )
                )
                register = operands.split(",", 1)[0].strip()
                if any(
                    candidate.mnemonic == "lsls"
                    and candidate.op_str.lower().startswith(f"{register}, {register},")
                    and candidate.op_str.lower().endswith("#7")
                    for candidate in window[1:]
                ):
                    split_hits.add(address)
                    rendered = "; ".join(
                        f"{candidate.mnemonic} {candidate.op_str}"
                        for candidate in window[:8]
                    )
                    print(
                        f"{path.name}:0x{address:08x}: split-0x580 {rendered}"
                    )
            immediates = {
                int(value, 16)
                for value in re.findall(r"#0x([0-9a-f]+)\b", operands)
            }
            if immediates.isdisjoint(TARGETS):
                continue
            hits += 1
            print(
                f"{path.name}:0x{address:08x}: "
                f"{instruction.mnemonic} {instruction.op_str}"
            )
        print(
            f"{path.name}: direct_candidates={hits} "
            f"split_0x580_candidates={len(split_hits)}"
        )


if __name__ == "__main__":
    main()
