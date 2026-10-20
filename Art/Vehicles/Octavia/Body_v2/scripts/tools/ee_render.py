"""EEVEE material renders. usage: -- file.blend outprefix view=cx,cy,cz,tx,ty,tz[;...] names hide=prefixes"""
import bpy, sys, os
from mathutils import Vector
a = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.open_mainfile(filepath=a[0]); prefix = a[1]
views = [v.split(":") for v in a[2].split(";")]
hide = a[3].split(",") if len(a) > 3 and a[3] else []
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE' if 'BLENDER_EEVEE' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE_NEXT'
sc.render.resolution_x, sc.render.resolution_y = 1100, 700
w = bpy.data.worlds.new("w"); sc.world = w
try: w.use_nodes = True
except Exception: pass
bg = w.node_tree.nodes.get("Background")
if bg: bg.inputs[0].default_value = (0.6, 0.63, 0.67, 1); bg.inputs[1].default_value = 1.0
L = bpy.data.lights.new("sun", 'SUN'); L.energy = 3.5; lo = bpy.data.objects.new("sun", L); lo.rotation_euler = (0.8, 0.2, 0.9); sc.collection.objects.link(lo)
for o in bpy.data.objects:
    ref = any(c.name.startswith("REFERENCE") for c in o.users_collection)
    o.hide_render = o.type == 'EMPTY' or o.name.startswith("UCX_") or (ref and not o.name.startswith("REF_SUSP_Tyre") and not o.name.startswith("REF_SUSP_Rim")) or any(o.name.startswith(h) for h in hide)
    if ref and o.type == 'MESH': o.display_type = 'SOLID'
cd = bpy.data.cameras.new("c"); cam = bpy.data.objects.new("c", cd); sc.collection.objects.link(cam); sc.camera = cam; cd.lens = 40; cd.clip_start = 0.01
for v in views:
    name, nums = v[0], [float(x) for x in v[1].split(",")]
    cam.location = nums[:3]; cam.rotation_euler = (Vector(nums[3:6]) - Vector(nums[:3])).to_track_quat('-Z', 'Y').to_euler()
    if len(nums) > 6: cd.lens = nums[6]
    sc.render.filepath = f"{prefix}_{name}.png"; bpy.ops.render.render(write_still=True)
print("done")
