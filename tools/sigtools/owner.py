# owner.py <exe> <rva hex>...: for a code address, finds the containing function's start (int3/ret padding scan),
# then the vtables that point at it (and the RTTI class of each), plus direct call sites. Heuristic.
import re
import struct
import sys


def main():
    exe = sys.argv[1]
    d = open(exe, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    base = struct.unpack_from("<Q", d, pe + 24 + 24)[0]
    n = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    secs = [struct.unpack_from("<8sIIII", d, pe + 24 + opt + i * 40) for i in range(n)]

    def off(rva):
        for _, vs, v, rs, r in secs:
            if v <= rva < v + max(vs, rs) and rva - v < rs:
                return rva - v + r
        return None

    def rva_of(o):
        for _, vs, v, rs, r in secs:
            if r <= o < r + rs:
                return o - r + v
        return None

    def cls(vt_rva):
        o = off(vt_rva - 8)
        if o is None:
            return "?"
        col = struct.unpack_from("<Q", d, o)[0] - base
        oc = off(col)
        if oc is None or struct.unpack_from("<I", d, oc)[0] != 1:
            return "?"
        td = struct.unpack_from("<I", d, oc + 12)[0]
        ot = off(td + 0x10)
        name = d[ot:d.find(b"\0", ot)].decode(errors="replace")
        return name[4:].split("@@")[0]

    for a in sys.argv[2:]:
        rva = int(a, 16)
        o = off(rva)
        # function start: walk back to a CC CC padding run followed by code
        s = o
        while s > 0 and not (d[s - 1] == 0xCC and d[s - 2] == 0xCC):
            s -= 1
        start = rva_of(s)
        print(f"exe+{rva:x}: function starts at exe+{start:x}")
        target = struct.pack("<Q", base + start)
        for m in re.finditer(re.escape(target), d):
            r = rva_of(m.start())
            # find vtable start: walk back over qwords that look like code pointers until one doesn't
            v = m.start()
            while True:
                prev = struct.unpack_from("<Q", d, v - 8)[0]
                if base + 0x1000 <= prev < base + 0x3800000:
                    v -= 8
                else:
                    break
            vt = rva_of(v)
            print(f"   in vtable exe+{vt:x} slot {(m.start() - v) // 8}: class {cls(vt)}")
        text = [x for x in secs if x[0].startswith(b".text")][0]
        _, vs, va, rs, raw = text
        for i in range(raw, raw + rs - 5):
            if d[i] == 0xE8 and (i - raw + va) + 5 + struct.unpack_from("<i", d, i + 1)[0] == start:
                print(f"   called from exe+{i - raw + va:x}")


if __name__ == "__main__":
    main()
