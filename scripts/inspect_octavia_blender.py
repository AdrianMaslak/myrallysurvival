"""Background inspection: blender --background --factory-startup --python scripts/inspect_octavia_blender.py"""
import bpy
import json
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "skoda_octavia_mk1_1u_1996-2010.glb"
OUT = ROOT / "Art" / "Vehicles" / "Octavia" / "Inspection"
OUT.mkdir(parents=True, exist_ok=True)
BLEND = OUT / "Octavia_Source_Inspection.blend"
if BLEND.exists():
    raise RuntimeError("Inspection blend already exists; refusing to replace it.")
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(SOURCE))
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH" and len(o.data.polygons)]
corners = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
low = Vector(tuple(min(c[i] for c in corners) for i in range(3)))
high = Vector(tuple(max(c[i] for c in corners) for i in range(3)))
centre = (low + high) / 2
report = {
    "source": SOURCE.name,
    "blender": bpy.app.version_string,
    "world_bounds_min": list(low), "world_bounds_max": list(high),
    "all_variants_dimensions_m": list(high - low),
    "mesh_objects_with_faces": len(meshes),
    "groups": [o.name for o in bpy.context.scene.objects if o.type == "EMPTY"],
    "note": "All source variants shown together. Bounds are not stock vehicle dimensions."
}
(OUT / "inspection.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
scene.render.engine = "BLENDER_WORKBENCH"
scene.render.resolution_x = 1200
scene.render.resolution_y = 800
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
shade = scene.display.shading
shade.light = "STUDIO"
shade.color_type = "MATERIAL"
shade.show_shadows = True
shade.show_cavity = True
shade.cavity_type = "BOTH"
shade.background_type = "WORLD"
scene.world = bpy.data.worlds.new("InspectionWorld")
scene.world.color = (0.055, 0.065, 0.08)

cam_data = bpy.data.cameras.new("InspectionCamera")
cam = bpy.data.objects.new("InspectionCamera", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
cam_data.type = "ORTHO"
cam_data.ortho_scale = max(high - low) * 1.45
for name, direction in [
    ("front-quarter", Vector((6, -8, 4))),
    ("rear-quarter", Vector((-6, 8, 4))),
]:
    cam.location = centre + direction
    cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = str(OUT / (name + ".png"))
    bpy.ops.render.render(write_still=True)
# Open at a useful solid viewport angle, with the full import preserved.
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == "VIEW_3D":
            area.spaces.active.region_3d.view_location = centre
            area.spaces.active.region_3d.view_distance = max(high - low) * 1.7
            area.spaces.active.region_3d.view_rotation = cam.rotation_euler.to_quaternion()
bpy.ops.wm.save_as_mainfile(filepath=str(BLEND))
print("INSPECTION_SUMMARY " + json.dumps({k:v for k,v in report.items() if k != "groups"}))
