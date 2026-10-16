# strrva.py <exe> <string>...: RVA(s) of NUL-terminated ASCII strings in the exe (exact match), for xrefs.exe.
import struct
import sys


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    n = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    off = pe + 24 + opt
    out = []
    for i in range(n):
        name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", data, off + i * 40)
        out.append((name.rstrip(b"\0").decode(), va, vsize, raw, rsize))
    return out


def main():
    data = open(sys.argv[1], "rb").read()
    secs = sections(data)
    for s in sys.argv[2:]:
        needle = b"\0" + s.encode() + b"\0"
        pos = 0
        while (pos := data.find(needle, pos)) >= 0:
            fo = pos + 1
            for name, va, vsize, raw, rsize in secs:
                if raw <= fo < raw + rsize:
                    print(f"{s}: {name} rva={fo - raw + va:x}")
            pos += 1


if __name__ == "__main__":
    main()
