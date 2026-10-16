# vt2name.py <exe> <vtable rva hex>...: MSVC RTTI class name for each vtable (vtable-8 -> COL -> TypeDescriptor).
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
        secs.append((va, max(vsize, rsize), raw))

    def off(rva):
        for va, size, raw in secs:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError(hex(rva))

    for a in sys.argv[2:]:
        vt = int(a, 16)
        try:
            col = struct.unpack_from("<Q", data, off(vt - 8))[0] - base
            td = struct.unpack_from("<I", data, off(col + 12))[0]
            name = data[off(td + 16):off(td + 16) + 200].split(b"\0")[0].decode(errors="replace")
        except Exception as e:
            name = f"? ({e})"
        print(f"exe+{vt:x}  {name}")


if __name__ == "__main__":
    main()
