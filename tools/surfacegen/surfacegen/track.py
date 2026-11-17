"""Write an Assetto Corsa multi-layout track folder around a generated kn5."""
from __future__ import annotations

import json
import math
from pathlib import Path

from .kn5 import start_matrix, write_kn5
from .surfaces import Mesh

# Starting values, tuned in spike B. WALL has no grip and never counts as track.
SURFACES = {
    "ROAD":   dict(FRICTION=0.99, DAMPING=0, WAV="", WAV_PITCH=0, FF_EFFECT="NULL", DIRT_ADDITIVE=0, IS_VALID_TRACK=1,
                   SIN_HEIGHT=0, SIN_LENGTH=0, VIBRATION_GAIN=0, VIBRATION_LENGTH=0),
    "KERB":   dict(FRICTION=0.92, DAMPING=0, WAV="kerb.wav", WAV_PITCH=1.3, FF_EFFECT=1, DIRT_ADDITIVE=0, IS_VALID_TRACK=1,
                   SIN_HEIGHT=0, SIN_LENGTH=0, VIBRATION_GAIN=1.0, VIBRATION_LENGTH=1.5),
    "GRASS":  dict(FRICTION=0.60, DAMPING=0.1, WAV="grass.wav", WAV_PITCH=0, FF_EFFECT="NULL", DIRT_ADDITIVE=1, IS_VALID_TRACK=0,
                   SIN_HEIGHT=0.03, SIN_LENGTH=0.5, VIBRATION_GAIN=0.2, VIBRATION_LENGTH=0.5),
    "SAND":   dict(FRICTION=0.80, DAMPING=0.1, WAV="sand.wav", WAV_PITCH=0, FF_EFFECT="NULL", DIRT_ADDITIVE=1, IS_VALID_TRACK=0,
                   SIN_HEIGHT=0, SIN_LENGTH=0, VIBRATION_GAIN=0.2, VIBRATION_LENGTH=0.5),
    "GRAVEL": dict(FRICTION=0.70, DAMPING=0.05, WAV="gravel.wav", WAV_PITCH=0, FF_EFFECT="NULL", DIRT_ADDITIVE=1, IS_VALID_TRACK=0,
                   SIN_HEIGHT=0.02, SIN_LENGTH=0.4, VIBRATION_GAIN=0.3, VIBRATION_LENGTH=0.5),
    "WALL":   dict(FRICTION=0, DAMPING=0, WAV="", WAV_PITCH=0, FF_EFFECT="NULL", DIRT_ADDITIVE=0, IS_VALID_TRACK=0,
                   SIN_HEIGHT=0, SIN_LENGTH=0, VIBRATION_GAIN=0, VIBRATION_LENGTH=0),
}
GATE_HALF_WIDTH = 8.0


def write_track(out: Path, layout: str, meshes: dict[str, Mesh], start_gta: tuple[float, float, float, float]) -> None:
    out = Path(out)
    track = out.name
    kn5_name = f"{track}_{layout}.kn5"
    out.mkdir(parents=True, exist_ok=True)
    x, y, z, heading = start_gta
    start = start_matrix((x, y, z), heading)
    # time gates either side of the start, perpendicular to the heading (GTA space)
    h = math.radians(heading)
    side = (math.cos(h), math.sin(h))
    left = start_matrix((x - side[0] * GATE_HALF_WIDTH, y - side[1] * GATE_HALF_WIDTH, z), heading)
    right = start_matrix((x + side[0] * GATE_HALF_WIDTH, y + side[1] * GATE_HALF_WIDTH, z), heading)
    dummies = {"AC_START_0": start, "AC_PIT_0": start, "AC_HOTLAP_START_0": start,
               "AC_TIME_0_L": left, "AC_TIME_0_R": right}
    write_kn5(out / kn5_name, meshes, dummies)

    (out / f"models_{layout}.ini").write_text(f"[MODEL_0]\nFILE={kn5_name}\nPOSITION=0,0,0\nROTATION=0,0,0\n")

    data = out / layout / "data"
    data.mkdir(parents=True, exist_ok=True)
    sections = []
    for i, key in enumerate(k for k in SURFACES if k in meshes):
        lines = [f"[SURFACE_{i}]", f"KEY={key}"] + [f"{k}={v}" for k, v in SURFACES[key].items()]
        lines += ["BLACK_FLAG_TIME=0", "IS_PITLANE=0"]
        sections.append("\n".join(lines))
    (data / "surfaces.ini").write_text("\n\n".join(sections) + "\n")

    ui = out / "ui" / layout
    ui.mkdir(parents=True, exist_ok=True)
    (ui / "ui_track.json").write_text(json.dumps({
        "name": f"GTA V physics ({layout})",
        "description": "Invisible driving surface generated from your own GTA V collision files. For the GTA link only.",
        "tags": ["gta-link"], "country": "", "city": "Los Santos", "length": "", "width": "",
        "pitboxes": "1", "run": "clockwise"}, indent=2))
