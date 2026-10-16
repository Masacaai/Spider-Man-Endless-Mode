# asinvokes.py <exe>: every call to the ActionScript Invoke (exe+1d1cee0) with the method name loaded into r8 just
# before it (lea r8,[rip+x]). Prints "callsite rva  method". Static, heuristic.
import re
import struct
import sys

INVOKE = 0x1d1cee0


def sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    n = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    off = pe + 24 + opt
    return [struct.unpack_from("<8sIIII", d, off + i * 40) for i in range(n)]


def main():
    d = open(sys.argv[1], "rb").read()
    secs = sections(d)
    text = [s for s in secs if s[0].startswith(b".text")][0]
    _, vsize, va, rsize, raw = text

    def rva_to_off(rva):
        for name, vs, v, rs, r in secs:
            if v <= rva < v + max(vs, rs):
                return rva - v + r
        return None

    out = []
    for i in range(raw, raw + rsize - 5):
        if d[i] != 0xE8:
            continue
        rel = struct.unpack_from("<i", d, i + 1)[0]
        site = i - raw + va
        if site + 5 + rel != INVOKE:
            continue
        name = "?"
        # look back up to 0x60 bytes for lea r8,[rip+disp] (4C 8D 05 disp32)
        for j in range(i - 7, i - 0x60, -1):
            if d[j] == 0x4C and d[j + 1] == 0x8D and d[j + 2] == 0x05:
                disp = struct.unpack_from("<i", d, j + 3)[0]
                target = (j - raw + va) + 7 + disp
                o = rva_to_off(target)
                if o is not None:
                    end = d.find(b"\0", o, o + 80)
                    s = d[o:end]
                    if s and all(32 <= c < 127 for c in s):
                        name = s.decode()
                break
        out.append((site, name))
    for site, name in out:
        print(f"exe+{site:x}  {name}")
    print(f"{len(out)} call sites", file=sys.stderr)


if __name__ == "__main__":
    main()
