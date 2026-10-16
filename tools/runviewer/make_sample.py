# make_sample.py: writes sample_run.json, a synthetic run in the EndlessMode_runs format, so the viewer can be
# previewed before a real run exists. Clearly not real data ("mode": "SAMPLE").
import json
import math
import os
import random

random.seed(7)
waves, enemies, hits, samples, events = [], [], [], [], []
t = 5.0
hp, max_hp = 208.0, 208.0
x, z = 1255.0, -815.0
kinds = ["skirmish", "assault", "full force", "boss"]
factions = ["street", "street", "street", "street", "fisk", "fisk", "fisk"]
types = {
    "street": ["enm_meleethug", "enm_baton_thug", "enm_pistolthug", "enm_riflethug", "enm_rocket_thug", "enm_slayer"],
    "fisk": ["enm_thug_kingpin_melee", "enm_thug_kingpin_baton", "enm_thug_kingpin_pistol", "enm_thug_kingpin_shield",
             "enm_rocket_kingpin", "enm_swat_kingpin_rifle"],
}
kills = 0
for w in range(1, 8):
    fac = factions[w - 1]
    kind = kinds[(w - 1) % 4]
    count = 0 if kind == "boss" else 6 + 2 * w
    bosses = ["Bounty Hunter"] if kind == "boss" else []
    start = t
    wk, wh, wd = 0, 0, 0.0
    for i in range(count + len(bosses)):
        boss = i >= count
        typ = "enm_baton_thug_bounty" if boss else random.choice(types[fac])
        a = random.uniform(0, 2 * math.pi)
        r = random.uniform(10, 35)
        st = start + random.uniform(0, 25)
        dt = st + random.uniform(8, 40)
        enemies.append({"type": typ, "wave": w, "point": random.randint(0, 138), "boss": boss,
                        "x": round(1255 + r * math.cos(a), 2), "y": 1.0, "z": round(-815 + r * math.sin(a), 2),
                        "start_health": 400 if boss else 170, "spawn_t": round(st, 2), "death_t": round(dt, 2),
                        "stuck": False, "hits_on_hero": 0, "damage_to_hero": 0})
        wk += 1
    end = start + 40 + 6 * w
    tt = start
    while tt < end:
        x = min(1300, max(1205, x + random.uniform(-3, 3)))
        z = min(-745, max(-880, z + random.uniform(-3, 3)))
        if random.random() < 0.08 + 0.01 * w:
            e = random.choice([e for e in enemies if e["wave"] == w])
            dmg = random.uniform(8, 25)
            before = hp
            hp = max(1.0, hp - dmg)
            e["hits_on_hero"] += 1
            e["damage_to_hero"] = round(e["damage_to_hero"] + before - hp, 2)
            hits.append({"t": round(tt, 2), "wave": w, "by": e["type"], "amount": round(dmg, 1), "before": round(before, 1),
                         "after": round(hp, 1), "x": round(x, 2), "y": 1.0, "z": round(z, 2)})
            wh += 1
            wd += before - hp
        else:
            hp = min(max_hp, hp + 0.6)
        alive = sum(1 for e in enemies if e["spawn_t"] <= tt < e["death_t"])
        samples.append([round(tt, 2), round(x, 2), 1.0, round(z, 2), round(hp, 1), max_hp, alive])
        tt += 0.5
    kills += wk
    waves.append({"n": w, "faction": fac, "kind": kind, "bosses": bosses, "count": count, "max_alive": 8 + w,
                  "health_mult": round(1 + 0.025 * (w - 1), 3), "start_t": round(start, 2),
                  "clear_t": round(end, 2) if w < 7 else -1, "kills": wk, "hits_taken": wh, "damage_taken": round(wd, 1)})
    t = end + 7
events.append({"t": 61.2, "type": "blacklist", "detail": "54"})
events.append({"t": 61.2, "type": "stuck", "detail": "54"})
run = {
    "version": 1, "started": "2026-10-13T20:00:00", "duration_s": round(t, 2), "end_reason": "hero_down",
    "mode": {"arena": "rykers", "mode": "SAMPLE (synthetic data)", "seed": 7, "engaged": True,
             "faction_order": ["street", "fisk", "demons", "prisoners", "sable", "hammerhead", "tombstone", "taskmaster"]},
    "summary": {"waves_started": len(waves), "waves_cleared": len(waves) - 1, "kills": kills, "hits_taken": len(hits),
                "damage_taken": round(sum(h["before"] - h["after"] for h in hits), 1), "hits_dealt": kills * 6,
                "bosses_defeated": 1, "enemies_spawned": len(enemies), "stuck_removed": 0},
    "waves": waves, "enemies": enemies, "hits_taken": hits, "samples": samples, "events": events,
    "blacklisted_points": [6, 54],
}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sample_run.json")
json.dump(run, open(out, "w"), indent=1)
print("wrote", out, len(samples), "samples", len(hits), "hits")
