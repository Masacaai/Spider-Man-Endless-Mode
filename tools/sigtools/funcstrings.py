# funcstrings.py <rva hex>... [-e exe]: for each address, the ASCII strings referenced (lea reg,[rip+x]) in the 0x400 bytes
# before it. A cheap way to name an unknown function from a stack trace: logging/profiling strings such as
# "TargetQuerySystem::ProcessJob" or "SharedMain" often sit right next to the code. Needs pefile + capstone.
import re
import sys

import capstone
import pefile

DEFAULT_EXE = r"C:\Program Files (x86)\Steam\steamapps\common\Marvel's Spider-Man Remastered\Spider-Man.exe"


def main(addrs, exe):
    pe = pefile.PE(exe, fast_load=True)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for a in addrs:
        data = pe.get_data(a - 0x400, 0x400)
        found = []
        for ins in md.disasm(data, a - 0x400):
            if ins.mnemonic == "lea" and "rip" in ins.op_str:
                m = re.search(r"rip ([+-]) (0x[0-9a-f]+)", ins.op_str)
                target = ins.address + ins.size + (1 if m.group(1) == "+" else -1) * int(m.group(2), 16)
                try:
                    s = pe.get_data(target, 40).split(b"\0")[0]
                except Exception:
                    continue
                if len(s) >= 5 and all(32 <= c < 127 for c in s):
                    found.append(s.decode())
        print(hex(a), found[-6:])


if __name__ == "__main__":
    args = sys.argv[1:]
    exe = DEFAULT_EXE
    if "-e" in args:
        i = args.index("-e")
        exe = args[i + 1]
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    main([int(x, 16) for x in args], exe)
