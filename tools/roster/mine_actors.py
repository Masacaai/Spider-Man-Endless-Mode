# mine_actors.py <actors dir> <paths.txt> <out.json>: for every extracted enemy .actor, finds the main bot component
# (the one with BotBaseData) and records its class, health, configs and BotData flags. Uses ALERT's parsers.
import json
import os
import sys
import io
import contextlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ALERT"))   # clone Tkachov/ALERT into tools/ALERT
import dat1lib
import dat1lib.types.sections as sections
from dat1lib.types.sections.actor.guessed import ComponentDefinitionsSection


def components(path):
    with contextlib.redirect_stdout(io.StringIO()):   # ALERT prints warnings for unknown sections
        with open(path, "rb") as f:
            actor = dat1lib.read(f)
        dat1 = actor.dat1
        defs = dat1.get_section(ComponentDefinitionsSection.TAG)
        raw = actor._raw_dat1
        out = {}
        for e in defs.entries:
            name = dat1.get_string(e[2])
            try:
                out[name] = sections.SerializedSection(raw[e[5]:e[5] + e[6]], dat1).root
            except Exception:
                out[name] = None
    return out


def main():
    actors_dir, paths_file, out_file = sys.argv[1:4]
    rows = []
    for p in open(paths_file):
        p = p.strip()
        if not p:
            continue
        fn = os.path.join(actors_dir, p.replace("\\", "_").replace("/", "_"))
        if not os.path.exists(fn):
            continue
        try:
            comps = components(fn)
        except Exception as ex:
            rows.append({"path": p, "error": str(ex)})
            continue
        row = {"path": p, "name": os.path.basename(p)[:-6], "components": sorted(comps)}
        for cname, data in comps.items():
            if isinstance(data, dict) and "BotBaseData" in data:
                base = data.get("BotBaseData") or {}
                row["bot_class"] = cname
                row["health"] = base.get("Health")
                row["bot_config"] = base.get("BotConfig")
                row["melee_config"] = base.get("MeleeConfig")
                row["awareness_config"] = base.get("AwarenessConfig")
                cfg = data.get("BotConfigData") or {}
                row["difficulty_config"] = cfg.get("DifficultyModConfig")
                row["emergent_vo"] = cfg.get("EmergentVOConfig")
                hb = data.get("HumanBotData") or {}
                row["human_bot_config"] = hb.get("HumanBotConfig")
                row["bot_data"] = data.get("BotData") or {}
        row["has_bot"] = "bot_class" in row
        rows.append(row)
    json.dump(rows, open(out_file, "w"), indent=1)
    bots = [r for r in rows if r.get("has_bot")]
    print(f"{len(rows)} actors, {len(bots)} with a bot component")
    for r in sorted(bots, key=lambda r: (r.get("health") or 0)):
        flags = ",".join(k for k, v in (r.get("bot_data") or {}).items() if v is True)
        print(f"{r.get('health')!s:>6}  {r['bot_class']:<28} {r['name']:<48} {flags}")


if __name__ == "__main__":
    main()
