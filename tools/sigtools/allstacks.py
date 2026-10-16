# allstacks.py <minidump> [filter]: heuristic stack scan for every thread (return addresses into known modules).
# Prints threads whose stack mentions `filter` (module name substring), or all threads without a filter.
import sys
from minidump.minidumpfile import MinidumpFile


def main(path, flt=None):
    md = MinidumpFile.parse(path)
    reader = md.get_reader()
    mods = [(m.baseaddress, m.baseaddress + m.size, m.name.split("\\")[-1]) for m in md.modules.modules]

    def where(addr):
        for lo, hi, name in mods:
            if lo <= addr < hi:
                return f"{name}+{addr - lo:x}"
        return None

    for t in md.threads.threads:
        ctx = t.ContextObject
        rip = getattr(ctx, "Rip", 0) if ctx else 0
        rsp = getattr(ctx, "Rsp", 0) if ctx else 0
        frames = [where(rip) or hex(rip)]
        try:
            data = b""
            for off in range(0, 0x80000, 0x100):   # chunked: stacks are captured only up to their committed end
                try:
                    data += reader.read(rsp + off, 0x100)
                except Exception:
                    break
            for i in range(0, len(data) - 8, 8):
                v = int.from_bytes(data[i:i + 8], "little")
                w = where(v)
                if w and (w.startswith("Spider-Man.exe") or w.startswith("EndlessMode") or w.startswith("ntdll")
                          or w.startswith("KERNELBASE") or w.startswith("ucrt") or w.startswith("winmm")):
                    frames.append(w)
                if len(frames) > 400:
                    break
        except Exception as ex:
            frames.append(f"<stack unreadable: {ex}>")
        text = " | ".join(frames)
        if flt and flt.lower() not in text.lower():
            continue
        print(f"thread {t.ThreadId}: {text}\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
