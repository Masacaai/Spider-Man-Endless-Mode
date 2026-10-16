# dumpq.py <exe> <rva hex> <count>: dumps qwords at an RVA; pointers into the image are shown as RVAs,
# with the C string they point to (if printable) or the RTTI class of a vtable.
import struct
import sys


def main():
    data = open(sys.argv[1], "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + opt + 40 * i
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((va, vsize, rsize, raw))

    def off(rva):
        for va, vsize, rsize, raw in secs:
            if va <= rva < va + min(vsize, rsize):
                return raw + rva - va
        return None

    start, count = int(sys.argv[2], 16), int(sys.argv[3])
    for i in range(count):
        r = start + i * 8
        o = off(r)
        if o is None:
            print(f"exe+{r:x}: (no file data)")
            continue
        q = struct.unpack_from("<Q", data, o)[0]
        note = ""
        if base <= q < base + 0x10000000:
            t = q - base
            to = off(t)
            note = f"-> exe+{t:x}"
            if to is not None:
                s = data[to:to + 80].split(b"\0")[0]
                if len(s) >= 3 and all(32 <= c < 127 for c in s):
                    note += f'  "{s.decode()}"'
        print(f"exe+{r:x}: {q:016x} {note}")


if __name__ == "__main__":
    main()
