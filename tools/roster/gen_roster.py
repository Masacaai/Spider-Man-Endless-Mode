# gen_roster.py: curated factions/bosses -> EndlessMode/src/roster_data.inc
# Names are actor file names; full paths come from enemy_paths.txt so every path is known to exist in the toc.
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "EndlessMode", "src", "roster_data.inc")

# Bosses: (id/name in the HUD, actor name). Base health comes from enemies.json.
BOSSES = [
    # 2026-10-13: the user picked the six bosses that aren't bound to their story setpieces; each fights at Ryker's
    # with their faction. Rhino, Scorpion, Shocker, Mecha-Hammerhead and Doc Ock are out.
    ("Kingpin", "enm_kingpin_boss"),
    ("Mister Negative", "enm_mrnegative_swordsman"),
    ("Tombstone", "enm_tombstone"),
    ("Taskmaster", "npc_taskmaster"),
    ("Hammerhead", "enm_hammerhead_boss_rough"),
    ("Silver Sable", "enm_silver_sable_ctns"),
    # Harder variant (phase 2 of his story fight): only from the second cycle on.
    ("Tombstone (Chained)", "enm_tombstone_chain"),
]
MIN_CYCLE = {"Tombstone (Chained)": 1}
# Health gates: outside their missions some bosses stop taking damage at a fraction of max health (waiting for a
# scripted phase change). The director finishes them when they reach it. Default: the 1 HP floor.
GATE = {"Hammerhead": 0.55}            # 715/1300 measured 2026-10-13
# Extra health so the fight up to the gate is a full-length fight.
HEALTH_BONUS = {"Hammerhead": 1.8}

# Factions in campaign order. Roles: Melee Baton Pistol Rifle Shield Rocket Sniper Heavy Special Elite
FACTIONS = [
    ("street", "Street Thugs", [
        ("enm_meleethug", "Melee"), ("enm_baton_thug", "Baton"), ("enm_pistolthug", "Pistol"),
        ("enm_riflethug", "Rifle"), ("enm_rocket_thug", "Rocket"), # enm_grenadierthug removed 2026-10-13: its GrenadierBot class (GrenadierBotAiming) overflowed the stack when spawned engaged
        ("enm_slayer", "Heavy"), ("enm_heavy_maggia", "Heavy"),
    ], []),
    ("fisk", "Fisk's Men", [
        ("enm_thug_kingpin_melee", "Melee"), ("enm_thug_kingpin_baton", "Baton"),
        ("enm_thug_kingpin_pistol", "Pistol"), ("enm_thug_kingpin_ranged", "Rifle"),
        ("enm_thug_kingpin_shield", "Shield"), ("enm_rocket_kingpin", "Rocket"),
        ("enm_thug_kingpin_sniper", "Sniper"), ("enm_slayer", "Heavy"),
        ("enm_swat_kingpin_melee", "Elite"), ("enm_swat_kingpin_baton", "Elite"),
        ("enm_swat_kingpin_rifle", "Elite"), ("enm_swat_kingpin_shield", "Elite"),
    ], ["Kingpin"]),
    ("demons", "Inner Demons", [
        ("enm_melee_innerdemon", "Melee"), ("enm_baton_innerdemon", "Baton"),
        ("enm_pistol_innerdemon", "Pistol"), ("enm_rifle_innerdemon", "Rifle"),
        ("enm_shield_innerdemon", "Shield"), ("enm_rocket_innerdemon", "Rocket"),
        ("enm_sniper_innerdemon", "Sniper"), ("enm_slayer_innerdemon", "Heavy"),
        ("enm_swordsman", "Special"),
    ], ["Mister Negative"]),
    ("prisoners", "Ryker's Inmates", [
        ("enm_prison_meleethug", "Melee"), ("enm_prison_baton_thug", "Baton"),
        ("enm_prison_pistolthug", "Pistol"), ("enm_thug_prison_rifle", "Rifle"),
        ("enm_prison_shield_thug", "Shield"), ("enm_thug_prison_rpg", "Rocket"),
        ("enm_sniper_prison", "Sniper"), ("enm_thug_prison_heavy", "Heavy"),
        ("enm_prison_whip", "Special"),
        ("enm_prison_super_meleethug", "Elite"), ("enm_prison_super_baton_thug", "Elite"),
        ("enm_prison_super_shield_thug", "Elite"), ("enm_thug_prison_super_heavy", "Elite"),
        ("enm_thug_super_prison_rpg", "Elite"), ("enm_thug_super_prison_rifle", "Elite"),
    ], []),
    ("sable", "Sable International", [
        ("enm_meleethug_sableagent", "Melee"), ("enm_baton_sableagent", "Baton"),
        ("enm_pistol_sableagent", "Pistol"), ("enm_rifle_sableagent", "Rifle"),
        ("enm_shield_sableagent", "Shield"), ("enm_rocket_sableagent", "Rocket"),
        ("enm_sniper_sableagent", "Sniper"), ("enm_jetpack_sableagent", "Special"),
        ("enm_taskmaster_heavy", "Heavy"),
    ], ["Silver Sable"]),
    ("hammerhead", "Hammerhead's Maggia", [
        ("enm_thug_hammerhead_melee", "Melee"), ("enm_thug_hammerhead_baton", "Baton"),
        ("enm_thug_hammerhead_pistol", "Pistol"), ("enm_thug_hammerhead_ranged", "Rifle"),
        ("enm_thug_hammerhead_shield", "Shield"), ("enm_rocket_hammerhead", "Rocket"),
        ("enm_sniper_hammerhead", "Sniper"), ("enm_thug_hammerhead_heavy", "Heavy"),
        ("enm_thug_hammerhead_molotov", "Special"), ("enm_thug_hammerhead_heavy_minigun", "Elite"),
    ], ["Hammerhead"]),
    # "Olympus": Hammerhead's goons with Sable tech from Project Olympus (the DLC's last faction; tiers 02/03).
    # Tier 02 ("hammerhead02") uses the same suit models as base Hammerhead goons; only tier 03 wears the Sable
    # tech ("aug" models), so Olympus is all tier 03 (user, 2026-10-13: Olympus came out in regular clothes).
    ("olympus", "Project Olympus", [
        ("enm_thug_hammerhead03_melee", "Melee"), ("enm_thug_hammerhead03_baton", "Baton"),
        ("enm_thug_hammerhead03_pistol", "Pistol"), ("enm_thug_hammerhead03_ranged", "Rifle"),
        ("enm_thug_hammerhead03_shieldrush", "Shield"), ("enm_rocket_hammerhead03", "Rocket"),
        ("enm_sniper_hammerhead03", "Sniper"), ("enm_thug_hammerhead03_heavy", "Heavy"),
        ("enm_whip_hammerhead03", "Special"), ("enm_jetpack_hammerhead03", "Special"),
        ("enm_thug_hammerhead03_heavy_minigun", "Elite"), ("enm_jetpack_hammerhead03", "Elite"),
    ], ["Hammerhead"]),
    ("tombstone", "Tombstone's Crew", [
        ("enm_thug_tombstone_melee", "Melee"), ("enm_thug_tombstone_dosed_melee", "Baton"),
        ("enm_thug_tombstone_pistol", "Pistol"), ("enm_thug_tombstone_rifle", "Rifle"),
        ("enm_shield_maggia", "Shield"), ("enm_rocket_maggia", "Rocket"),
        ("enm_heavy_maggia", "Heavy"), ("enm_thug_maggia_heavy_minigun", "Elite"),
    ], ["Tombstone", "Tombstone (Chained)"]),
    ("taskmaster", "Taskmaster's Mercs", [
        ("enm_taskmaster_meleethug", "Melee"), ("enm_taskmaster_baton", "Baton"),
        ("enm_taskmaster_pistol", "Pistol"), ("enm_taskmaster_rifle", "Rifle"),
        ("enm_taskmaster_shield", "Shield"), ("enm_taskmaster_rocket", "Rocket"),
        ("enm_taskmaster_sniper", "Sniper"), ("enm_taskmaster_heavy", "Heavy"),
    ], ["Taskmaster"]),
]


# Bot classes that crash when spawned engaged (null MoverStandard in their behaviours, 2026-10-13: enm_heavy_maggia,
# Tombstone). They spawn calm and join the fight when they notice the hero. See docs/BUGS.md.
CALM_CLASSES = {"SlayerBot", "SlayerRangedBot"}


def main():
    paths = {}
    for p in open(os.path.join(HERE, "enemy_paths.txt")):
        p = p.strip()
        if p:
            paths[os.path.basename(p)[:-6]] = p
    info = {r["name"]: r for r in json.load(open(os.path.join(HERE, "enemies.json"))) if "name" in r}
    errors = []

    def path_of(name):
        if name not in paths:
            errors.append(name)
            return "MISSING"
        return paths[name].replace("\\", "\\\\")

    def head_of(name):
        # HeadActor reference inside the actor file (bosses keep their head as a separate actor that must be loaded too)
        import subprocess
        fn = os.path.join(HERE, "actors", paths[name].replace("\\", "_").replace("/", "_"))
        try:
            out = subprocess.run([sys.executable, "-I", os.path.join(HERE, "..", "sigtools", "actor2json.py"), fn],
                                 capture_output=True, text=True).stdout
            import re
            m = re.search(r'"HeadActor": "([^"]+\.actor)"', out)
            return m.group(1).replace("/", "\\\\") if m else ""
        except Exception:
            return ""

    lines = ["// Generated by tools/roster/gen_roster.py; edit that file, not this one.", ""]
    lines.append("static const Boss kBosses[] = {")
    for disp, name in BOSSES:
        hp = (info.get(name) or {}).get("health") or 0
        calm = "true" if (info.get(name) or {}).get("bot_class") in CALM_CLASSES else "false"
        head = head_of(name)
        lines.append(f'    {{ "{path_of(name)}", "{disp}", {hp}, false, {calm}, "{head}", {MIN_CYCLE.get(disp, 0)}, '
                     f'{GATE.get(disp, 0.0)}f, {HEALTH_BONUS.get(disp, 1.0)}f }},')
    lines.append("};")
    lines.append("")
    lines.append("static const struct { const char* id; const char* name; std::vector<Unit> units; std::vector<const char*> bosses; } kFactions[] = {")
    for fid, fname, units, bosses in FACTIONS:
        lines.append(f'    {{ "{fid}", "{fname}", {{')
        for name, role in units:
            calm = "true" if (info.get(name) or {}).get("bot_class") in CALM_CLASSES else "false"
            lines.append(f'        {{ "{path_of(name)}", Role::{role}, {calm} }},')
        blist = ", ".join(f'"{b}"' for b in bosses)
        lines.append(f"    }}, {{ {blist} }} }},")
    lines.append("};")
    if errors:
        print("missing actors:", errors)
        sys.exit(1)
    open(OUT, "w").write("\n".join(lines) + "\n")
    print(f"wrote {OUT}: {len(BOSSES)} bosses, {len(FACTIONS)} factions, "
          f"{sum(len(f[2]) for f in FACTIONS)} unit entries")


if __name__ == "__main__":
    main()
