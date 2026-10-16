# snapdump.py <pid> <out.dmp>: small minidump (threads + stacks + module list) of a running/hung process, for
# reading thread call stacks offline (tools/sigtools/dumpstack.py).
import ctypes
import ctypes.wintypes as wt
import sys

MiniDumpNormal = 0x0
MiniDumpWithThreadInfo = 0x1000
MiniDumpWithIndirectlyReferencedMemory = 0x40
PROCESS_ALL = 0x0400 | 0x0010 | 0x0040  # QUERY_INFORMATION | VM_READ | DUP_HANDLE


def main():
    pid, out = int(sys.argv[1]), sys.argv[2]
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    dbg = ctypes.WinDLL("dbghelp", use_last_error=True)
    k32.OpenProcess.restype = wt.HANDLE
    h = k32.OpenProcess(PROCESS_ALL, False, pid)
    if not h:
        print("OpenProcess failed", ctypes.get_last_error())
        return 1
    k32.CreateFileW.restype = wt.HANDLE
    f = k32.CreateFileW(out, 0x40000000, 0, None, 2, 0x80, None)
    ok = dbg.MiniDumpWriteDump(h, pid, f, MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory, None, None, None)
    err = ctypes.get_last_error()
    k32.CloseHandle(f)
    k32.CloseHandle(h)
    print("ok" if ok else f"MiniDumpWriteDump failed {err:#x}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
