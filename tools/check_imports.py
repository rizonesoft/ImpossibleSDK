#!/usr/bin/env python3
"""Fail when a program needs more of Windows than its tier allows. Stdlib only."""
import argparse
import json
import struct
import sys
from pathlib import Path

TIERS = Path(__file__).resolve().parents[1] / "abi" / "tiers.json"
IMPORT, TLS, BOUND_IMPORT, DELAY_IMPORT = 1, 9, 11, 13
ORDINAL, BOUND, NOTHING = "imported by ordinal", "has bound imports", "is loaded for nothing"


def allowed(tier):
    table = {}
    known = json.loads(TIERS.read_text(encoding="utf-8"))["tiers"]
    if tier not in [entry["tier"] for entry in known]:
        raise ValueError(f"tier {tier} is not in {TIERS.name}")
    for entry in known:
        if entry["tier"] <= tier:
            for dll, names in entry["functions"].items():
                table.setdefault(dll, set()).update(names)
    return table


class Image:
    """The program laid out in memory as the loader maps it, so every address is read the way Windows reads it."""

    def __init__(self, data):
        if data[:2] != b"MZ":
            raise ValueError("not a PE program")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("not a PE program")
        machine, count = struct.unpack_from("<HH", data, pe + 4)
        size = struct.unpack_from("<H", data, pe + 20)[0]
        optional = pe + 24
        if machine != 0x8664 or struct.unpack_from("<H", data, optional)[0] != 0x20B:
            raise ValueError("not a 64-bit x86 program")
        image_size, headers = struct.unpack_from("<II", data, optional + 56)
        entries = struct.unpack_from("<I", data, optional + 108)[0]
        self.directory = [struct.unpack_from("<II", data, optional + 112 + 8 * i) for i in range(entries)]
        self.memory = bytearray(image_size)
        self.place(0, data[:headers])
        for i in range(count):
            virtual_size, address, raw_size, raw = struct.unpack_from("<IIII", data, optional + size + 40 * i + 8)
            if raw_size and raw + raw_size > len(data):
                raise ValueError("the file is shorter than its sections")
            pages = (virtual_size or raw_size) + 0xFFF & ~0xFFF
            self.place(address, data[raw:raw + min(raw_size, pages)])

    def place(self, address, part):
        if address + len(part) > len(self.memory):
            raise ValueError("a section lies outside the image")
        self.memory[address:address + len(part)] = part

    def text(self, rva):
        return self.memory[rva:self.memory.index(b"\0", rva)].decode("ascii")

    def rva(self, index):
        return self.directory[index][0] if index < len(self.directory) else 0

    def imports(self):
        """Yield (dll, name) pairs; the name is ORDINAL, BOUND or NOTHING when it is not a plain import by name."""
        table = self.rva(IMPORT)
        while table:
            lookup, bound, _, name, address = struct.unpack_from("<IIIII", self.memory, table)
            if not name:
                return
            dll = self.text(name).lower()
            if bound:
                yield dll, BOUND
            entry = lookup or address
            if not struct.unpack_from("<Q", self.memory, entry)[0]:
                yield dll, NOTHING  # Windows loads it all the same
            while True:
                value = struct.unpack_from("<Q", self.memory, entry)[0]
                if not value:
                    break
                yield dll, ORDINAL if value >> 63 else self.text(value + 2)
                entry += 8
            table += 20


def problems(data, tier):
    image, table, found = Image(data), allowed(tier), []
    for dll, name in image.imports():
        if name in (ORDINAL, BOUND, NOTHING):
            found.append(f"{dll}: {name}")
        elif name not in table.get(dll, ()):
            found.append(f"{dll}: {name} is not in tier {tier}")
    if image.rva(BOUND_IMPORT):
        found.append("has bound imports")
    if image.rva(DELAY_IMPORT):
        found.append("uses delay-loaded DLLs")
    if image.rva(TLS):
        found.append("uses thread-local storage")
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    parser.add_argument("--tier", type=int, required=True)
    args = parser.parse_args()
    try:
        found = problems(args.program.read_bytes(), args.tier)
    except (OSError, ValueError, struct.error) as error:
        found = [str(error)]
    for line in found:
        print(f"{args.program.name}: {line}", file=sys.stderr)
    return 1 if found else 0


if __name__ == "__main__":
    raise SystemExit(main())
