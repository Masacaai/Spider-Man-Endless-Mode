# dumpstack.py <minidump> : exception info + heuristic stack walk (return addresses into known modules).
# Prints frames as module+offset. For Spider-Man.exe, offsets are RVAs comparable with recon notes.
import sys
from minidump.minidumpfile import MinidumpFile

def main(path):
    md = MinidumpFile.parse(path)
    reader = md.get_reader()
    mods = [(m.baseaddress, m.baseaddress + m.size, m.name.split('\\')[-1]) for m in md.modules.modules]

    def where(addr):
        for lo, hi, name in mods:
            if lo <= addr < hi:
                return f"{name}+{addr - lo:x}"
        return None

    exc = md.exception.exception_records[0]
    rec = exc.ExceptionRecord
    tid = exc.ThreadId
    print(f"exception code={rec.ExceptionCode} addr={rec.ExceptionAddress:#x} ({where(rec.ExceptionAddress)}) thread={tid}")
    if rec.NumberParameters >= 2:
        print(f"  access type={rec.ExceptionInformation[0]} target={rec.ExceptionInformation[1]:#x}")

    ctx = None
    for t in md.threads.threads:
        if t.ThreadId == tid:
            ctx = t.ContextObject
            stack = t.Stack
            break
    if ctx is None:
        print("thread context not found")
        return
    for r in ("Rip", "Rsp", "Rax", "Rbx", "Rcx", "Rdx", "Rsi", "Rdi", "R8", "R9"):
        v = getattr(ctx, r, None)
        if v is not None:
            print(f"  {r}={v:#x} {where(v) or ''}")

    rsp = ctx.Rsp
    print("stack return-address candidates:")
    count = 0
    for off in range(0, 0x2000, 8):
        try:
            data = reader.read(rsp + off, 8)
        except Exception:
            break
        v = int.from_bytes(data, "little")
        w = where(v)
        if w and ("Spider-Man.exe" in w or "EndlessMode" in w or "winmm" in w):
            print(f"  [rsp+{off:#06x}] {v:#x} {w}")
            count += 1
            if count >= 40:
                break

if __name__ == "__main__":
    main(sys.argv[1])
