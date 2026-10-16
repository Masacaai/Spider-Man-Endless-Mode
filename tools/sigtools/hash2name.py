# hash2name.py <strings.txt> <hex>... : looks up CRC32 name hashes (ALERT's Insomniac CRC32, both variants)
# against every string extracted from the exe (strings.txt lines: "<offset> <string>").
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ALERT"))   # clone Tkachov/ALERT into tools/ALERT
import dat1lib.crc32 as crc

def main():
    path, wanted = sys.argv[1], {int(h, 16) for h in sys.argv[2:]}
    found = {}
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            parts = line.rstrip("\n").split(" ", 1)
            if len(parts) != 2:
                continue
            s = parts[1]
            if len(s) > 120:
                continue
            for variant in (False, True):
                try:
                    h = crc.hash(s, variant)
                except Exception:
                    continue
                if h in wanted:
                    found.setdefault(h, set()).add(s)
    for h in sorted(wanted):
        print(f"{h:08X}  {', '.join(sorted(found.get(h, {'?'})))}")

if __name__ == "__main__":
    main()
