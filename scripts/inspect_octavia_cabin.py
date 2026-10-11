"""Report cabin parts (seats, steering wheel, dashboard, pedals, glass) of the wheeled prototype.
Run: blender --background --factory-startup --python scripts/inspect_octavia_cabin.py
Writes Art/Vehicles/Octavia/Prototype/Export/cabin.json with bounds in Unreal cm (assembly frame:
X forward, Y right, Z up, origin at ground below the car centre) for placing the driver camera.
"""
import json
import re
from pathlib import Path
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Art/Vehicles/Octavia/Prototype/Octavia_With_Wheels.blend"
OUT = ROOT / "Art/Vehicles/Octavia/Prototype/Export/cabin.json"
PATTERN = re.compile(r"seat|steer|wheel_?s|dash|pedal|glass|window|windscreen|windshield|mirror|gear|lever|instrument|cluster|roll", re.I)

bpy.ops.wm.open_mainfile(filepath=str(SOURCE))


def visible(obj):
    if obj.hide_get() or obj.hide_render:
        return False
    return not any(c.hide_render or c.name.endswith("_HIDDEN") for c in obj.users_collection)


def to_unreal(v):
    # Matches assembly.json: Unreal X = -Blender Y, Unreal Y = -Blender X, metres to cm.
    return [round(-v.y * 100, 1), round(-v.x * 100, 1), round(v.z * 100, 1)]


parts = []
for obj in bpy.data.objects:
    if obj.type != "MESH" or not PATTERN.search(obj.name):
        continue
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    lo = Vector((min(c.x for c in corners), min(c.y for c in corners), min(c.z for c in corners)))
    hi = Vector((max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners)))
    a, b = to_unreal(lo), to_unreal(hi)
    parts.append({
        "name": obj.name,
        "visible": visible(obj),
        "collections": [c.name for c in obj.users_collection],
        "materials": [m.name for m in obj.data.materials if m],
        "min_cm": [min(a[i], b[i]) for i in range(3)],
        "max_cm": [max(a[i], b[i]) for i in range(3)],
        "centre_cm": to_unreal((lo + hi) * 0.5),
    })

parts.sort(key=lambda p: (not p["visible"], p["name"]))
OUT.write_text(json.dumps(parts, indent=1), encoding="utf-8")
for p in parts:
    if p["visible"]:
        print(f"CABIN {p['name'][:48]:48} centre {p['centre_cm']} min {p['min_cm']} max {p['max_cm']} mats {p['materials'][:3]}")
print(f"CABIN_DONE {sum(p['visible'] for p in parts)} visible of {len(parts)} matching parts -> {OUT}")
