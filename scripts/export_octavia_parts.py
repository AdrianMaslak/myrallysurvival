"""Export the wheeled Octavia prototype to FBX parts with the model's own face normals.
Run: blender --background --factory-startup --python scripts/export_octavia_parts.py

Replaces the export step of prepare_octavia_prototype.py, which recalculated normals "outside" on
joined copies and flipped the headliner, door cards, much of the body and the dashboard (Unreal culls
back faces, so they vanished from inside). The .blend file is read, never saved.
Also exports the steering wheel as its own part with its column axis, so it can turn in game.
Writes Art/Vehicles/Octavia/Prototype/Export/*.fbx and assembly.json (Unreal frame: X forward, Y right,
Z up, cm, origin at ground below the car centre).
"""
import json
import math
from pathlib import Path
import bpy
import numpy as np
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Art/Vehicles/Octavia/Prototype/Octavia_With_Wheels.blend"
EXPORT = ROOT / "Art/Vehicles/Octavia/Prototype/Export"
CATEGORIES = {"01_Body", "02_Interior", "03_Engine", "04_Suspension"}
PANEL_GROUPS = {
    "Door_FL": {"door_FL", "doorglass_FL", "doorpanel_FL", "doortrim_FL"},
    "Door_FR": {"door_FR", "doorglass_FR", "doorpanel_FR", "doortrim_FR"},
    "Door_RL": {"door_RL", "doorglass_RL", "doorpanel_RL", "doortrim_RL"},
    "Door_RR": {"door_RR", "doorglass_RR", "doorpanel_RR", "doortrim_RR"},
    "Bonnet": {"FL_hood"},
    "Trunk": {"trunk", "trunkglass", "spoiler", "lettering_R", "lettering_vRS_R", "logo_R"},
    "Radiator": {"radiator", "radiator_fan_L", "radiator_fan_R"},
    "SteeringWheel": {"steering_wheel_c"},
}
# Source model is -Y forward; exports are +X forward.
ALIGN = Matrix.Rotation(math.pi / 2, 4, "Z")

bpy.ops.wm.open_mainfile(filepath=str(SOURCE))


def to_unreal(v):
    return [-v.y * 100, -v.x * 100, v.z * 100]


def world_points(objects):
    return [o.matrix_world @ v.co for o in objects for v in o.data.vertices]


def bounds(objects):
    pts = world_points(objects)
    lo = Vector(tuple(min(p[i] for p in pts) for i in range(3)))
    hi = Vector(tuple(max(p[i] for p in pts) for i in range(3)))
    return lo, hi


visible = [o for o in bpy.data.objects
           if o.type == "MESH" and o.data.polygons and any(c.name in CATEGORIES for c in o.users_collection)]
wheels = sorted((o for o in bpy.data.objects if o.name.startswith("Wheel_") and o.type == "MESH"), key=lambda o: o.name)
if len(wheels) != 4:
    raise RuntimeError("Expected four placeholder wheels.")

buckets = {"Chassis": []}
for name in PANEL_GROUPS:
    buckets[name] = []
for o in visible:
    group = o.get("source_part_group", "").removeprefix("octavia1u_")
    buckets[next((k for k, v in PANEL_GROUPS.items() if group in v), "Chassis")].append(o)
for wheel in wheels:
    buckets[wheel.name] = [wheel]

scene = bpy.context.scene
export_collection = bpy.data.collections.new("TEMP_Export")
scene.collection.children.link(export_collection)
manifest = {
    "note": "Static mesh parts with source face normals; tyre and steering data for the game pawn.",
    "frame": "Unreal cm: X forward, Y right, Z up, origin at ground below the car centre.",
    "parts": [],
}

for name, objects in buckets.items():
    if not objects:
        raise RuntimeError("Empty export part: " + name)
    if name.startswith("Wheel_"):
        pivot = objects[0].location.copy()
    elif name == "Chassis":
        pivot = Vector((0, 0, 0))
    else:
        lo, hi = bounds(objects)
        pivot = (lo + hi) / 2
    copies = []
    for original in objects:
        mesh = original.data.copy()
        matrix = ALIGN @ Matrix.Translation(-pivot) @ original.matrix_world
        mesh.transform(matrix)
        # Baking a mirrored transform reverses winding; restore the source facing.
        if matrix.determinant() < 0:
            mesh.flip_normals()
        copy = bpy.data.objects.new("EXPORT_" + name, mesh)
        export_collection.objects.link(copy)
        copies.append(copy)
    bpy.ops.object.select_all(action="DESELECT")
    for copy in copies:
        copy.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    if len(copies) > 1:
        bpy.ops.object.join()
    export_obj = bpy.context.view_layer.objects.active
    export_obj.name = "SM_Octavia_" + name
    filename = export_obj.name + ".fbx"
    bpy.ops.export_scene.fbx(filepath=str(EXPORT / filename), use_selection=True,
        object_types={"MESH"}, axis_forward="-Y", axis_up="Z", apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL", bake_anim=False, use_mesh_modifiers=True,
        mesh_smooth_type="FACE", use_triangles=True, add_leaf_bones=False)
    lo, hi = bounds(objects)
    size = hi - lo
    entry = {
        "name": export_obj.name, "file": filename,
        "pivot_cm": to_unreal(pivot),
        "unreal_expected_pivot_cm": to_unreal(pivot),
        # Unreal axes: X = source Y extent, Y = source X extent.
        "dimensions_cm": [size.y * 100, size.x * 100, size.z * 100],
        "triangles": sum(len(p.vertices) - 2 for p in export_obj.data.polygons),
    }
    if name == "SteeringWheel":
        # Column axis = direction of least spread of the wheel's vertices, pointing at the driver.
        pts = np.array([to_unreal(p) for p in world_points(objects)])
        _, _, axes = np.linalg.svd(pts - pts.mean(axis=0), full_matrices=False)
        axis = axes[2] / np.linalg.norm(axes[2])
        if axis[0] > 0:
            axis = -axis
        entry["column_axis"] = [float(a) for a in axis]
    manifest["parts"].append(entry)
    bpy.data.objects.remove(export_obj, do_unlink=True)

(EXPORT / "assembly.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
for part in manifest["parts"]:
    print("EXPORT", part["name"], [round(v, 1) for v in part["pivot_cm"]], [round(v, 1) for v in part["dimensions_cm"]], part.get("column_axis", ""))
print(f"EXPORT_DONE {len(manifest['parts'])} parts")
