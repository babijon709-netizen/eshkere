#!/usr/bin/env python3
"""Verify magic_farm_profile.h against a matching libil2cpp.so dump.

Usage: python3 tools/offsets/verify_magic_profile.py /path/to/libil2cpp.so
"""
from pathlib import Path
import re
import struct
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip())
        return 2
    so = Path(sys.argv[1])
    data = so.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        print("expected ELF64 little-endian libil2cpp.so")
        return 1
    e_phoff = struct.unpack_from("<Q", data, 32)[0]
    e_phentsize = struct.unpack_from("<H", data, 54)[0]
    e_phnum = struct.unpack_from("<H", data, 56)[0]
    loads = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", data, off)
        p_offset, p_vaddr, _, p_filesz, p_memsz, _ = struct.unpack_from("<QQQQQQ", data, off + 8)
        if p_type == 1:
            loads.append((p_vaddr, p_filesz, p_offset, p_flags))

    def read_rva(rva: int, size: int) -> bytes | None:
        for vaddr, filesz, offset, _flags in loads:
            if vaddr <= rva and rva + size <= vaddr + filesz:
                start = offset + rva - vaddr
                return data[start:start + size]
        return None

    header = Path(__file__).resolve().parents[2] / "jni/src/magic_farm_profile.h"
    text = header.read_text()
    errors = []
    checked = 0
    for name, value in re.findall(r"inline constexpr std::uint64_t (\w+) = (0x[0-9A-Fa-f]+);", text):
        rva = int(value, 16)
        match = re.search(rf"inline constexpr unsigned char {name}Bytes\[16\] = \{{([^}}]+)\}};", text)
        if match:
            expected = bytes(int(x, 16) for x in match.group(1).split(","))
            actual = read_rva(rva, 16)
            checked += 1
            if actual != expected:
                errors.append(f"{name}: bytes mismatch at {rva:#x}")
        elif name == "ReturnTrap":
            actual = read_rva(rva, 4)
            insn = re.search(r"inline constexpr std::uint32_t ReturnTrapInsn = (0x[0-9A-Fa-f]+);", text)
            checked += 1
            if not insn or actual != struct.pack("<I", int(insn.group(1), 16)):
                errors.append(f"ReturnTrap: instruction mismatch at {rva:#x}")
        else:
            checked += 1
            if read_rva(rva, 8) is None:
                errors.append(f"{name}: RVA outside file-backed PT_LOAD at {rva:#x}")
    if errors:
        print("\n".join(errors))
        return 1
    print(f"magic profile: {checked} RVAs/byte fingerprints verified against {so}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
