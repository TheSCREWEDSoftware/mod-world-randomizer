#!/usr/bin/env python3
"""
Rebuild the randomizer's zone list from your aowow zone extract.

    python tools/generate_from_aowow.py <path to aowow/Stuff/zone-extract>

Writes:
  data/sql/db-world/02_world_randomizer_zones.sql   zones, original level ranges, territory, neighbours
  web/zones.js                                      the same zones plus their map outlines, for web/map.html

Neighbours (for Randomizer.Brackets.Adjacent) are zones whose outlines touch on the continent map; an island with
no touching zone is linked to the closest one, and EXTRA_LINKS adds borders the outlines miss (water gaps, boats).
Needs numpy, scipy and Pillow:  pip install numpy scipy pillow

Rows: every outdoor zone that has a world map. Cities are left out; zones with no level range
(the Scarlet Enclave) and Wintergrasp are listed but disabled.
"""

import json
import sys
from pathlib import Path

CONTINENTS = {"Eastern Kingdoms": 0, "Kalimdor": 1, "Outland": 2, "Northrend": 3}
CONTINENT_MAPS = {0: "-3", 1: "-6", 2: "-2", 3: "-5"}  # aowow continent map ids
MAP_WIDTH, MAP_HEIGHT = 1002, 668
TOUCH_PIXELS = 4

# Real borders the highlight outlines leave a gap in, and boat links between islands and the mainland
EXTRA_LINKS = [
    (3537, 65),    # Borean Tundra - Dragonblight
    (495, 65),     # Howling Fjord - Dragonblight
    (394, 65),     # Grizzly Hills - Dragonblight
    (3525, 148),   # Bloodmyst Isle - Darkshore (boat)
]


def compute_neighbours(shapes, zone_ids):
    import numpy as np
    from PIL import Image, ImageDraw
    from scipy import ndimage

    links = {}

    def link(a, b):
        links.setdefault(a, set()).add(b)
        links.setdefault(b, set()).add(a)

    for map_id in CONTINENT_MAPS.values():
        masks = {}
        for zone_id, entry in shapes.get(map_id, {}).items():
            zone_id = int(zone_id)
            if zone_id not in zone_ids:
                continue
            image = Image.new("1", (MAP_WIDTH, MAP_HEIGHT), 0)
            draw = ImageDraw.Draw(image)
            for ring in entry["p"]:
                points = [(ring[i] * MAP_WIDTH / 100, ring[i + 1] * MAP_HEIGHT / 100) for i in range(0, len(ring) - 1, 2)]
                if len(points) > 2:
                    draw.polygon(points, fill=1)
            masks[zone_id] = np.array(image, dtype=bool)

        distances = {z: ndimage.distance_transform_edt(~m) for z, m in masks.items()}
        ids = sorted(masks)
        for a in ids:
            near = sorted((distances[a][masks[b]].min(), b) for b in ids if b != a and masks[b].any())
            touching = [b for d, b in near if d <= TOUCH_PIXELS] or [near[0][1]] if near else []
            for b in touching:
                link(a, b)

    for a, b in EXTRA_LINKS:
        if a in zone_ids and b in zone_ids:
            link(a, b)

    return {a: sorted(v) for a, v in links.items()}


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    extract = Path(sys.argv[1])
    module = Path(__file__).resolve().parent.parent
    zones = json.loads((extract / "zones.json").read_text(encoding="utf-8"))
    shapes = json.loads((extract / "zone_shapes.json").read_text(encoding="utf-8"))["maps"]

    top = [z for z in zones if z["type"] == 0 and z["isTopLevelZone"] and z["hasWorldMap"]]
    rows = []
    for z in sorted(top, key=lambda z: z["zoneId"]):
        if z["isCity"]:
            continue
        enabled = 0 if (z["levelMin"] == 0 or z["territoryName"] == "world_pvp") else 1
        rows.append({
            "id": z["zoneId"],
            "map": z["realMapId"],
            "continent": CONTINENTS.get(z["continent"], 0),
            "expansion": z["expansion"],
            "min": z["levelMin"] or 1,
            "max": z["levelMax"] or 1,
            "territory": {"alliance": 0, "horde": 1}.get(z["territoryName"], 2),
            "enabled": enabled,
            "shuffle": 1,
            "name": z["name"],
        })

    neighbours = compute_neighbours(shapes, {r["id"] for r in rows})
    for r in rows:
        r["n"] = neighbours.get(r["id"], [])

    def sql(text: str) -> str:
        return text.replace("'", "''")

    lines = [
        "-- mod-world-randomizer: the zones to shuffle, with their original level ranges and territory.",
        "-- Taken from your aowow (Stuff/zone-extract/zones.json): every outdoor zone with a world map, cities left out.",
        "-- Wintergrasp and the Scarlet Enclave are listed but disabled. Set `shuffle` = 0 to keep a zone in its own bracket.",
        "DELETE FROM `world_randomizer_zone` WHERE `zone_id` IN (" + ", ".join(str(r["id"]) for r in rows) + ");",
        "INSERT INTO `world_randomizer_zone` (`zone_id`, `map_id`, `continent`, `expansion`, `level_min`, "
        "`level_max`, `territory`, `enabled`, `shuffle`, `name`) VALUES",
        ",\n".join(
            f"({r['id']}, {r['map']}, {r['continent']}, {r['expansion']}, {r['min']}, {r['max']}, "
            f"{r['territory']}, {r['enabled']}, {r['shuffle']}, '{sql(r['name'])}')" for r in rows) + ";",
        "",
        "-- Neighbouring zones, for Randomizer.Brackets.Adjacent (both directions are listed).",
        "DELETE FROM `world_randomizer_zone_link`;",
        "INSERT INTO `world_randomizer_zone_link` (`zone_id`, `neighbour_id`) VALUES",
        ",\n".join(f"({r['id']}, {n})" for r in rows for n in r["n"]) + ";",
    ]
    (module / "data/sql/db-world/02_world_randomizer_zones.sql").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # Outlines: every zone drawn on the four continent maps (cities too, drawn grey)
    outlines = {}
    for continent, map_id in CONTINENT_MAPS.items():
        outlines[continent] = {
            zone_id: {"p": entry["p"], "name": entry.get("name", ""), "l": entry.get("l")}
            for zone_id, entry in shapes.get(map_id, {}).items()
        }

    js = (
        "// Generated by tools/generate_from_aowow.py from your aowow zone extract. Do not edit by hand.\n"
        "window.RANDOMIZER_ZONES = " + json.dumps(rows, separators=(",", ":")) + ";\n"
        "window.RANDOMIZER_OUTLINES = " + json.dumps(outlines, separators=(",", ":")) + ";\n"
    )
    (module / "web/zones.js").write_text(js, encoding="utf-8")

    print(f"{len(rows)} zones ({sum(r['enabled'] for r in rows)} enabled), "
          f"{sum(len(r['n']) for r in rows) // 2} neighbour links, {sum(len(v) for v in outlines.values())} outlines.")


if __name__ == "__main__":
    main()
