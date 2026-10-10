"""Create an organized, non-destructive Octavia working copy from the inspection blend."""
import bpy
import json
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Art/Vehicles/Octavia/Working"
OUT.mkdir(parents=True, exist_ok=True)
TARGET = OUT / "Octavia_Liftback_Working.blend"
if TARGET.exists():
    raise RuntimeError("Working file exists. Preserve any edits before generating another copy.")
bpy.ops.wm.open_mainfile(filepath=str(ROOT / "Art/Vehicles/Octavia/Inspection/Octavia_Source_Inspection.blend"))
source_objects = list(bpy.data.objects)
source_transforms = {o.name: o.matrix_world.copy() for o in source_objects}
source_polygons = sum(len(o.data.polygons) for o in source_objects if o.type == "MESH")

# Explicit provisional configuration; all unselected parts are preserved.
parts = {
"01_Body": """
body door_FL door_FR door_RL door_RR windshield doorglass_FL doorglass_FR
doorglass_RL doorglass_RR trunk trunkglass roof_a spoiler antenna_limo
fender_FL fender_FR bumperbar_F bumperbar_R
FL_bumper_F_a FL_bumper_R_a FL_hood FL_grille
FL_taillight_frame_L FL_taillight_frame_R FL_taillight_glass_L FL_taillight_glass_R
FL_taillight_L FL_taillight_R
FLheadlight_frame_L FLheadlight_frame_R FLheadlight_glass_L FLheadlight_glass_R
FLheadlight_L FLheadlight_R FLheadlight_foglight_L FLheadlight_foglight_R
signal_frame_L signal_frame_R signal_glass_L signal_glass_R signal_L signal_R
mirror_L mirror_R logo_R lettering_R lettering_vRS_F lettering_vRS_R wipers
""".split(),
"02_Interior": """
intcarpet seat_R seat_FL seat_FR doorpanel_FL doorpanel_FR doorpanel_RL doorpanel_RR
doortrim_FL doortrim_FR doortrim_RL doortrim_RR footrest pedal_brake pedal_clutch pedal_gas
wiperstalk signalstalk steering_wheel_c mirror_int shelf dashboard handbrake
shifter_base_M shifter_knob_M armrest inttrimupper inttrimlower interiorlight
klima_a radio_a gauges_a FL_gaugesdecals benzingaugesvrs_metric
needlevrs_speedo needlevrs_tacho needlevrs_temp needlevrs_fuel
""".split(),
"03_Engine": """
engine_i4_turbo engine_i4_turbo_chargepipe engine_i4_turbo_manifold engine_i4_vacline
intercooler enginecover_18T engine_i4_radhose radiator radiator_fan_L radiator_fan_R
transmission engbay airfilter_turbo engine_i4_intake_turbo engine_i4_commonparts
engine_i4_turbo_downpipe exhaust_mid standard_exhaust_muffler_vRS
standard_exhaust_heatshield fueltank
""".split(),
"04_Suspension": """
subframe_F lowerarm_FL lowerarm_FR hub_FL hub_FR torsionbeam hub_R_torsionbeam
shock_R_torsionbeam spring_R_torsionbeam strut_FL strut_FR swaybar_droplink_FL
swaybar_droplink_FR swaybar_F swaybar_F_mount steeringrack tierod_F halfshaft_L halfshaft_R
""".split()
}
collections = {}
for label in ["00_Roots", *parts, "90_Alternatives_HIDDEN", "91_LineHelpers_HIDDEN", "99_Preview"]:
    collection = bpy.data.collections.new(label)
    bpy.context.scene.collection.children.link(collection)
    collections[label] = collection

def move(obj, destination):
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    collections[destination].objects.link(obj)

groups = {}
for category, names in parts.items():
    for name in names:
        full = "octavia1u_" + name
        if bpy.data.objects.get(full) is None:
            raise RuntimeError("Configuration references missing group: " + full)
        groups[full] = category

manifest = []
for obj in source_objects:
    old_name = obj.name
    if obj.type == "CAMERA":
        category = "99_Preview"
        part_name = old_name
    elif old_name in ("Sketchfab_model", "Collada visual scene group"):
        category = "00_Roots"
        part_name = old_name
    else:
        group = obj
        while group.parent and group.parent.name not in ("Sketchfab_model", "Collada visual scene group"):
            group = group.parent
        part_name = group.name
        category = groups.get(part_name, "90_Alternatives_HIDDEN")
        if obj.type == "MESH" and not obj.data.polygons:
            category = "91_LineHelpers_HIDDEN"
        if obj.type == "MESH":
            obj.name = part_name + "__" + old_name
    obj["source_object_name"] = old_name
    obj["source_part_group"] = part_name
    move(obj, category)
    manifest.append({"source": old_name, "working_name": obj.name, "part": part_name, "collection": category})
    if any(abs(obj.matrix_world[r][c] - source_transforms[old_name][r][c]) > 0.00001 for r in range(4) for c in range(4)):
        raise RuntimeError("Unexpected transform change: " + old_name)

for name in ("90_Alternatives_HIDDEN", "91_LineHelpers_HIDDEN"):
    collections[name].hide_render = True
    collections[name].hide_viewport = True

# Material preview only: source material data is preserved, object viewport colours distinguish parts.
for obj in source_objects:
    if obj.type != "MESH":
        continue
    group = obj.get("source_part_group", "").lower()
    category = obj.users_collection[0].name
    obj.color = (0.52, 0.55, 0.53, 1)
    if category == "03_Engine":
        obj.color = (0.24, 0.26, 0.28, 1)
    elif category == "04_Suspension":
        obj.color = (0.17, 0.19, 0.21, 1)
    elif category == "02_Interior":
        obj.color = (0.12, 0.13, 0.14, 1)
    elif "glass" in group or "windshield" in group:
        obj.color = (0.08, 0.14, 0.18, 1)

visible = [o for o in source_objects if o.type == "MESH" and o.data.polygons and o.users_collection[0].name in parts]
pts = [o.matrix_world @ Vector(c) for o in visible for c in o.bound_box]
low = Vector(tuple(min(p[i] for p in pts) for i in range(3)))
high = Vector(tuple(max(p[i] for p in pts) for i in range(3)))
centre = (low + high) / 2
scene = bpy.context.scene
scene.display.shading.color_type = "OBJECT"
cam = scene.camera
cam.data.ortho_scale = max(high-low) * 1.4
for name, direction in [("liftback-front", Vector((6,-8,3.6))), ("liftback-rear", Vector((-6,8,3.6)))]:
    cam.location = centre + direction
    cam.rotation_euler = (centre-cam.location).to_track_quat("-Z","Y").to_euler()
    scene.render.filepath = str(OUT / (name+".png"))
    bpy.ops.render.render(write_still=True)

for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == "VIEW_3D":
            area.spaces.active.shading.color_type = "OBJECT"
            area.spaces.active.region_3d.view_location = centre
            area.spaces.active.region_3d.view_distance = max(high-low) * 1.5
            area.spaces.active.region_3d.view_rotation = cam.rotation_euler.to_quaternion()
        elif area.type == "OUTLINER":
            area.spaces.active.show_restrict_column_viewport = True
            area.spaces.active.show_restrict_column_render = True
# Do not leave hundreds of mesh objects selected.
for obj in bpy.context.selected_objects:
    obj.select_set(False)

assert len(bpy.data.objects) == len(source_objects), "Objects lost during organization."
assert sum(len(o.data.polygons) for o in source_objects if o.type=="MESH") == source_polygons, "Geometry changed."
summary = {
 "source_objects_preserved": len(source_objects),
 "source_polygons_preserved": source_polygons,
 "visible_mesh_objects": len(visible),
 "visible_triangles": sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in visible),
 "visible_bounds_m": list(high-low),
 "configuration": parts, "objects": manifest
}
(OUT / "organization-manifest.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
readme = bpy.data.texts.new("START_HERE")
readme.write("""OCTAVIA WORKING COPY
01 Body / 02 Interior / 03 Engine / 04 Suspension contain the provisional liftback selection.
90 Alternatives and 91 LineHelpers preserve unused source parts. Enable their monitor/camera
icons in the Outliner only when inspecting a replacement; hide the current equivalent first.
Nothing was deleted or joined. Original parent transforms remain unchanged.
Select a named part group (empty) and its children to manipulate the complete part.
Viewport colours are inspection aids, not final game materials.
No wheels, rig, collision or Unreal physics have been added yet.
""")
# Save only a new working file; never overwrite the source or an existing working copy.
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(TARGET))
print("ORGANIZED " + json.dumps({k:v for k,v in summary.items() if k not in ("configuration","objects")}))
