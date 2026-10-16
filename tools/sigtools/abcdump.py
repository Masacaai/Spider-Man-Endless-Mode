# abcdump.py <file.gfx|file.swf> [method-name-substring ...]
# Disassembles the ActionScript 3 (ABC) inside a Scaleform .gfx / .swf (as extracted by tools/assets/extract.py from
# ui/export/HUD/*.gfx). With no filter it lists every class and its methods; with filters it prints the bytecode of every method
# whose "Class.method" name contains one of them. Used to read how the game's HUD panels decide what to show
# (docs/GAME_INTERNALS.md, "UI (Scaleform)"). Output is a plain opcode listing, not decompiled source.
import struct
import sys
import zlib

OPS = {
    0x01: "bkpt", 0x02: "nop", 0x03: "throw", 0x04: "getsuper:u", 0x05: "setsuper:u", 0x06: "dxns:u", 0x07: "dxnslate", 0x08: "kill:u",
    0x09: "label", 0x0c: "ifnlt:j", 0x0d: "ifnle:j", 0x0e: "ifngt:j", 0x0f: "ifnge:j", 0x10: "jump:j", 0x11: "iftrue:j", 0x12: "iffalse:j",
    0x13: "ifeq:j", 0x14: "ifne:j", 0x15: "iflt:j", 0x16: "ifle:j", 0x17: "ifgt:j", 0x18: "ifge:j", 0x19: "ifstricteq:j", 0x1a: "ifstrictne:j",
    0x1b: "lookupswitch", 0x1c: "pushwith", 0x1d: "popscope", 0x1e: "nextname", 0x1f: "hasnext", 0x20: "pushnull", 0x21: "pushundefined",
    0x23: "nextvalue", 0x24: "pushbyte:b", 0x25: "pushshort:u", 0x26: "pushtrue", 0x27: "pushfalse", 0x28: "pushnan", 0x29: "pop", 0x2a: "dup",
    0x2b: "swap", 0x2c: "pushstring:s", 0x2d: "pushint:i", 0x2e: "pushuint:ui", 0x2f: "pushdouble:d", 0x30: "pushscope", 0x31: "pushnamespace:u",
    0x32: "hasnext2:uu", 0x40: "newfunction:m", 0x41: "call:u", 0x42: "construct:u", 0x43: "callmethod:uu", 0x44: "callstatic:uu",
    0x45: "callsuper:nu", 0x46: "callproperty:nu", 0x47: "returnvoid", 0x48: "returnvalue", 0x49: "constructsuper:u", 0x4a: "constructprop:nu",
    0x4c: "callproplex:nu", 0x4e: "callsupervoid:nu", 0x4f: "callpropvoid:nu", 0x50: "sxi1", 0x51: "sxi8", 0x52: "sxi16", 0x53: "applytype:u",
    0x55: "newobject:u", 0x56: "newarray:u", 0x57: "newactivation", 0x58: "newclass:u", 0x59: "getdescendants:n", 0x5a: "newcatch:u",
    0x5d: "findpropstrict:n", 0x5e: "findproperty:n", 0x5f: "finddef:n", 0x60: "getlex:n", 0x61: "setproperty:n", 0x62: "getlocal:u",
    0x63: "setlocal:u", 0x64: "getglobalscope", 0x65: "getscopeobject:b", 0x66: "getproperty:n", 0x68: "initproperty:n", 0x6a: "deleteproperty:n",
    0x6c: "getslot:u", 0x6d: "setslot:u", 0x6e: "getglobalslot:u", 0x6f: "setglobalslot:u", 0x70: "convert_s", 0x71: "esc_xelem", 0x72: "esc_xattr",
    0x73: "convert_i", 0x74: "convert_u", 0x75: "convert_d", 0x76: "convert_b", 0x77: "convert_o", 0x78: "checkfilter", 0x80: "coerce:n",
    0x82: "coerce_a", 0x85: "coerce_s", 0x86: "astype:n", 0x87: "astypelate", 0x90: "negate", 0x91: "increment", 0x92: "inclocal:u", 0x93: "decrement",
    0x94: "declocal:u", 0x95: "typeof", 0x96: "not", 0x97: "bitnot", 0xa0: "add", 0xa1: "subtract", 0xa2: "multiply", 0xa3: "divide", 0xa4: "modulo",
    0xa5: "lshift", 0xa6: "rshift", 0xa7: "urshift", 0xa8: "bitand", 0xa9: "bitor", 0xaa: "bitxor", 0xab: "equals", 0xac: "strictequals",
    0xad: "lessthan", 0xae: "lessequals", 0xaf: "greaterthan", 0xb0: "greaterequals", 0xb1: "instanceof", 0xb2: "istype:n", 0xb3: "istypelate",
    0xb4: "in", 0xc0: "increment_i", 0xc1: "decrement_i", 0xc2: "inclocal_i:u", 0xc3: "declocal_i:u", 0xc4: "negate_i", 0xc5: "add_i",
    0xc6: "subtract_i", 0xc7: "multiply_i", 0xd0: "getlocal0", 0xd1: "getlocal1", 0xd2: "getlocal2", 0xd3: "getlocal3", 0xd4: "setlocal0",
    0xd5: "setlocal1", 0xd6: "setlocal2", 0xd7: "setlocal3", 0xef: "debug", 0xf0: "debugline:u", 0xf1: "debugfile:s", 0xf2: "bkptline:u", 0xf3: "timestamp",
}


class R:
    def __init__(self, d, p=0):
        self.d, self.p = d, p

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u30(self):
        v, sh = 0, 0
        while True:
            b = self.u8()
            v |= (b & 0x7f) << sh
            sh += 7
            if not b & 0x80:
                return v & 0xffffffff

    def s32(self):
        v = self.u30()
        return v - (1 << 32) if v & 0x80000000 else v

    def s24(self):
        v = self.d[self.p] | self.d[self.p + 1] << 8 | self.d[self.p + 2] << 16
        self.p += 3
        return v - (1 << 24) if v & 0x800000 else v

    def bytes(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v


def swf_abc_blocks(path):
    d = open(path, "rb").read()
    sig = d[:3]
    if sig in (b"CWS", b"CFX"):
        d = d[:3].replace(b"C", b"F") + d[3:8] + zlib.decompress(d[8:])
    nbits = d[8] >> 3
    pos = 8 + (5 + 4 * nbits + 7) // 8 + 4
    out = []
    while pos < len(d):
        h = struct.unpack("<H", d[pos:pos + 2])[0]
        pos += 2
        t, l = h >> 6, h & 0x3f
        if l == 0x3f:
            l = struct.unpack("<I", d[pos:pos + 4])[0]
            pos += 4
        if t in (72, 82):
            body = d[pos:pos + l]
            if t == 82:
                q = 4
                while body[q] != 0:
                    q += 1
                body = body[q + 1:]
            out.append(body)
        pos += l
    return out


class Abc:
    def __init__(self, data):
        r = R(data)
        r.u8(); r.u8(); r.u8(); r.u8()   # minor, major
        self.ints = [0] + [r.s32() for _ in range(max(r.u30(), 1) - 1)]
        self.uints = [0] + [r.u30() for _ in range(max(r.u30(), 1) - 1)]
        self.doubles = [0.0] + [struct.unpack("<d", r.bytes(8))[0] for _ in range(max(r.u30(), 1) - 1)]
        n = r.u30()
        self.strings = [""] + [r.bytes(r.u30()).decode("utf-8", "replace") for _ in range(max(n, 1) - 1)]
        n = r.u30()
        self.ns = [("", "")]
        for _ in range(max(n, 1) - 1):
            k = r.u8()
            self.ns.append((k, self.strings[r.u30()]))
        n = r.u30()
        self.nssets = [[]]
        for _ in range(max(n, 1) - 1):
            self.nssets.append([r.u30() for _ in range(r.u30())])
        n = r.u30()
        self.mn = [None]
        for _ in range(max(n, 1) - 1):
            k = r.u8()
            if k in (0x07, 0x0d):
                ns = r.u30(); name = r.u30(); self.mn.append(("Q", k, ns, name))
            elif k in (0x0f, 0x10):
                self.mn.append(("RTQ", k, r.u30()))
            elif k in (0x11, 0x12):
                self.mn.append(("RTQL", k))
            elif k in (0x09, 0x0e):
                name = r.u30(); ns = r.u30(); self.mn.append(("M", k, name, ns))
            elif k in (0x1b, 0x1c):
                self.mn.append(("ML", k, r.u30()))
            elif k == 0x1d:
                name = r.u30(); params = [r.u30() for _ in range(r.u30())]; self.mn.append(("T", k, name, params))
            else:
                raise ValueError("multiname kind %x" % k)
        n = r.u30()
        self.methods = []
        for _ in range(n):
            pc = r.u30(); ret = r.u30(); ptypes = [r.u30() for _ in range(pc)]; name = r.u30(); flags = r.u8()
            if flags & 0x08:
                for _ in range(r.u30()):
                    r.u30(); r.u8()
            if flags & 0x80:
                for _ in range(pc):
                    r.u30()
            self.methods.append({"name": name, "params": pc, "flags": flags, "owner": None})
        for _ in range(r.u30()):   # metadata
            r.u30()
            k = r.u30()
            for _ in range(2 * k):
                r.u30()
        ncls = r.u30()
        self.instances = []
        for _ in range(ncls):
            name = r.u30(); sup = r.u30(); flags = r.u8()
            if flags & 0x08:
                r.u30()
            for _ in range(r.u30()):
                r.u30()
            init = r.u30()
            traits = self.traits(r)
            self.instances.append({"name": name, "super": sup, "init": init, "traits": traits})
        self.classes = []
        for i in range(ncls):
            cinit = r.u30()
            self.classes.append({"cinit": cinit, "traits": self.traits(r)})
        for _ in range(r.u30()):
            r.u30()
            self.traits(r)
        self.bodies = {}
        for _ in range(r.u30()):
            m = r.u30(); r.u30(); r.u30(); r.u30(); r.u30()
            code = r.bytes(r.u30())
            for _ in range(r.u30()):
                for _ in range(5):
                    r.u30()
            self.traits(r)
            self.bodies[m] = code

    def traits(self, r):
        out = []
        for _ in range(r.u30()):
            name = r.u30(); kind = r.u8(); t = kind & 0x0f
            info = {"name": name, "kind": t}
            if t in (0, 6):
                r.u30(); r.u30()
                vi = r.u30()
                if vi:
                    r.u8()
            elif t == 4:
                r.u30(); r.u30()
            elif t == 5:
                r.u30(); info["method"] = r.u30()
            else:
                r.u30(); info["method"] = r.u30()
            if kind & 0x40:
                for _ in range(r.u30()):
                    r.u30()
            out.append(info)
        return out

    def mname(self, i):
        if i == 0:
            return "*"
        m = self.mn[i]
        if m[0] == "Q":
            return self.strings[m[3]]
        if m[0] == "M":
            return self.strings[m[2]]
        if m[0] == "T":
            return "%s.<%s>" % (self.mname(m[2]), ",".join(self.mname(p) for p in m[3]))
        return "<%s>" % m[0]

    def disasm(self, code):
        r = R(code)
        lines = []
        while r.p < len(code):
            at = r.p
            op = r.u8()
            spec = OPS.get(op)
            if spec is None:
                lines.append("%04x  ??? 0x%02x" % (at, op))
                continue
            name, _, args = spec.partition(":")
            txt = name
            if op == 0xef:
                txt += " %d %d %d %d" % (r.u8(), r.u30(), r.u8(), r.u30())
            elif op == 0x1b:
                default = r.s24(); n = r.u30() + 1
                txt += " default=%04x cases=%s" % (at + default, [("%04x" % (at + r.s24())) for _ in range(n)])
            else:
                parts = []
                for a in args:
                    if a == "u":
                        parts.append(str(r.u30()))
                    elif a == "b":
                        parts.append(str(r.u8()))
                    elif a == "j":
                        off = r.s24(); parts.append("%04x" % (r.p + off))
                    elif a == "s":
                        parts.append(repr(self.strings[r.u30()]))
                    elif a == "i":
                        parts.append(str(self.ints[r.u30()]))
                    elif a == "d":
                        parts.append(str(self.doubles[r.u30()]))
                    elif a == "n":
                        parts.append(self.mname(r.u30()))
                    elif a == "m":
                        parts.append("method#%d" % r.u30())
                txt += " " + " ".join(parts) if parts else ""
            lines.append("%04x  %s" % (at, txt))
        return lines


def main(path, filters):
    for blob in swf_abc_blocks(path):
        a = Abc(blob)
        owner = {}
        for ci, inst in enumerate(a.instances):
            cname = a.mname(inst["name"])
            owner[inst["init"]] = cname + ".<init>"
            for t in inst["traits"]:
                if "method" in t:
                    owner[t["method"]] = cname + "." + a.mname(t["name"]) + {1: "", 2: " [getter]", 3: " [setter]"}.get(t["kind"], "")
            owner[a.classes[ci]["cinit"]] = cname + ".<cinit>"
        if not filters:
            for ci, inst in enumerate(a.instances):
                cname = a.mname(inst["name"])
                names = [a.mname(t["name"]) for t in inst["traits"] if "method" in t]
                print(cname, "extends", a.mname(inst["super"]), "|", ", ".join(names))
            continue
        for m, code in sorted(a.bodies.items()):
            nm = owner.get(m, "method#%d" % m)
            if any(f.lower() in nm.lower() for f in filters):
                print("\n=== %s (method#%d)" % (nm, m))
                print("\n".join(a.disasm(code)))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2:])
