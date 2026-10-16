# disasm.py <rva hex> <length hex> [exe]: disassemble Spider-Man.exe at an RVA (needs: pip install pefile capstone).
# Example: python disasm.py 1bbf3b0 50      Prints "rva mnemonic operands". Calls print their absolute RVA target.
# Use it to read the code around an address from a crash dump or a hang stack (see docs/REVERSE_ENGINEERING.md).
import sys

import capstone
import pefile

DEFAULT_EXE = r"C:\Program Files (x86)\Steam\steamapps\common\Marvel's Spider-Man Remastered\Spider-Man.exe"


def main(rva, length, exe):
    pe = pefile.PE(exe, fast_load=True)
    data = pe.get_data(rva, length)
    for ins in capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64).disasm(data, rva):
        print("%x %s %s" % (ins.address, ins.mnemonic, ins.op_str))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(int(sys.argv[1], 16), int(sys.argv[2], 16), sys.argv[3] if len(sys.argv) > 3 else DEFAULT_EXE)
