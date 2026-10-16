# extract.py <out dir> <asset path | 16-hex-digit asset id>...: extracts assets by path from MSMR's asset_archive (via ALERT's dat1lib) and,
# for .config files, also writes <name>.json (ALERT's config2json). Paths are hashed the way the game does it.
import io
import contextlib
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ALERT"))   # clone Tkachov/ALERT into tools/ALERT
import dat1lib
import dat1lib.crc64 as crc64

ARCHIVE = os.environ.get("MSMR_ARCHIVE", r"C:\Program Files (x86)\Steam\steamapps\common\Marvel's Spider-Man Remastered\asset_archive")   # set MSMR_ARCHIVE if the game is elsewhere


def config_json(data):
    import dat1lib.types.sections.config.serialized as ser
    with contextlib.redirect_stdout(io.StringIO()):
        cfg = dat1lib.read(io.BytesIO(data))
    t = cfg.dat1.get_section(ser.ConfigTypeSection.TAG)
    c = cfg.dat1.get_section(ser.ConfigContentSection.TAG)
    return {"Type": t.root.get("Type") if t else None, "Content": c.root if c else None}


def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    with contextlib.redirect_stdout(io.StringIO()):
        with open(os.path.join(ARCHIVE, "toc"), "rb") as f:
            toc = dat1lib.read(f)
        toc.set_archives_dir(ARCHIVE)
    for path in sys.argv[2:]:
        norm = path.replace("/", "\\").lower()
        is_id = len(path) == 16 and all(c in "0123456789abcdefABCDEF" for c in path)
        aid = int(path, 16) if is_id else crc64.hash(path)
        if is_id:
            norm = path.upper() + ".config"   # ids passed here are configs (grepall hits)
        entries = toc.get_asset_entries_by_assetid(aid)
        if not entries:
            print(f"MISSING {aid:016X} {path}")
            continue
        data = toc.extract_asset(entries[0])
        fn = os.path.join(out_dir, norm.replace("\\", "_"))
        with open(fn, "wb") as f:
            f.write(data)
        print(f"OK {aid:016X} {len(data):7d} {path}")
        if norm.endswith(".config"):
            try:
                with open(fn + ".json", "w") as f:
                    json.dump(config_json(data), f, indent=1, default=str)
            except Exception as e:
                print(f"  (json failed: {e})")


if __name__ == "__main__":
    main()
