"""Reject images that are not standalone STM32U585 flash images."""
import struct
import sys
from elftools.elf.elffile import ELFFile

with open(sys.argv[1], "rb") as stream:
    elf = ELFFile(stream)
    assert elf["e_machine"] == "EM_ARM"
    segments = [s for s in elf.iter_segments()
                if s["p_type"] == "PT_LOAD" and s["p_filesz"]]
    assert segments and min(s["p_paddr"] for s in segments) == 0x08000000
    for segment in segments:
        assert 0x08000000 <= segment["p_paddr"]
        assert segment["p_paddr"] + segment["p_filesz"] <= 0x08200000
    first = next(s for s in segments if s["p_paddr"] == 0x08000000)
    sp, reset = struct.unpack("<II", first.data()[:8])
    assert 0x20000000 < sp <= 0x200C0000, hex(sp)
    assert reset & 1 and 0x08000000 <= reset < 0x08200000, hex(reset)
    print(f"Standalone image validated: SP={sp:#x}, reset={reset:#x}")
