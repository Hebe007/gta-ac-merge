"""Read interior (MLO) placements from CodeWalker's YMAP XML export.

An interior's collision (.ybn) is stored in the interior's own coordinates and named after the interior archetype
(e.g. vbca_tunnel1). The map places it with a CMloInstanceDef: archetypeName, position, rotation. CodeWalker uses an
MLO instance's rotation as-is (ordinary CEntityDef rotations are stored inverted)."""
from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path

V3 = tuple[float, float, float]
Q = tuple[float, float, float, float]


def read_mlo_instances(path: Path) -> list[tuple[str, V3, Q]]:
    out = []
    for item in ET.parse(path).getroot().iter("Item"):
        if item.get("type") != "CMloInstanceDef":
            continue
        name = (item.findtext("archetypeName") or "").strip().lower()
        p, r = item.find("position"), item.find("rotation")
        if not name or p is None or r is None:
            continue
        pos = (float(p.get("x")), float(p.get("y")), float(p.get("z")))
        rot = (float(r.get("x")), float(r.get("y")), float(r.get("z")), float(r.get("w")))
        out.append((name, pos, rot))
    return out
