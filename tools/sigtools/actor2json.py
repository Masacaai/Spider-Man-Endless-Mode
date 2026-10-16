# actor2json.py <file.actor> [component name filter]: dumps an .actor's component data (Insomniac serialized
# objects, same encoding as .config content) as JSON, using ALERT's parsers.
import json
import struct
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ALERT"))   # clone Tkachov/ALERT into tools/ALERT
import dat1lib
import dat1lib.types.sections as sections
from dat1lib.types.sections.actor.guessed import ComponentDefinitionsSection, ComponentsDataSection


def main():
    with open(sys.argv[1], "rb") as f:
        actor = dat1lib.read(f)
    flt = sys.argv[2] if len(sys.argv) > 2 else None
    dat1 = actor.dat1
    defs = dat1.get_section(ComponentDefinitionsSection.TAG)
    raw = actor._raw_dat1   # component offsets are relative to the DAT1 start
    out = {}
    for e in defs.entries:
        name = dat1.get_string(e[2])
        if flt and flt.lower() not in name.lower():
            continue
        off, size = e[5], e[6]
        try:
            s = sections.SerializedSection(raw[off:off + size], dat1)
            out[name] = s.root
        except Exception as ex:
            out[name] = f"<parse error: {ex}>"
    print(json.dumps(out, indent=2, default=str))


if __name__ == "__main__":
    main()
