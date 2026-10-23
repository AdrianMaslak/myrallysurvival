"""Quick workbench renders for geometry checks.
usage: python3 wb_render.py -- file.blend outprefix view[,view...] [hide=prefix1,prefix2] [show_ref=1] [xray=1]"""
import bpy, sys, math, os
from mathutils import Vector
a = sys.argv[sys.argv.index("--") + 1:]
blend, prefix, views = a[0], a[1], a[2].split(",")
opts = dict(x.split("=", 1) for x in a[3:])
bpy.ops.wm.open_mainfile(filepath=blend)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'STUDIO'; sh.color_type = opts.get("color", 'MATERIAL'); sh.show_cavity = True; sh.cavity_type = 'BOTH'
sh.show_object_outline = False; sh.show_xray = opts.get("xray") == "1"; sh.xray_alpha = 0.5
sc.render.resolution_x, sc.render.resolution_y = int(opts.get("w", 1200)), int(opts.get("h", 800))
V = {
 "fl34": ((-4.2, 5.2, 2.4), (0, 0.2, 0.6)), "rr34": ((4.2, -5.4, 2.4), (0, -0.2, 0.6)),
 "side": ((-6.5, 0.0, 0.7), (0, 0.0, 0.7)), "under": ((-1.5, 0.5, -5.5), (0, 0, 0.3)),
 "top": ((0, 0.0, 8.0), (0, 0, 0.0)), "front": ((0, 6.5, 0.8), (0, 0, 0.6)), "rear": ((0, -6.5, 0.9), (0, 0, 0.6)),
 "bay": ((-1.6, 3.2, 2.2), (0, 1.35, 0.5)), "rearunder": ((-2.0, -0.6, -1.4), (0, -1.3, 0.35)),
 "farch": ((-2.4, 1.9, 0.3), (-0.7, 1.26, 0.35)), "rarch": ((-2.4, -0.6, 0.3), (-0.7, -1.3, 0.35)),
 "cabin": ((-0.386, -0.05, 1.12), (-0.386, 1.0, 0.85)), "cabin_side": ((-2.2, -0.2, 1.0), (0, 0.0, 0.7)),
 "floor_in": ((0.0, 0.4, 1.6), (0, -1.0, 0.35)),
}
for k, v in list(V.items()): pass
hide = [h for h in opts.get("hide", "").split(",") if h]
show_ref = opts.get("show_ref", "1") == "1"
for o in bpy.data.objects:
    inref = any(c.name.startswith("REFERENCE") for c in o.users_collection)
    o.hide_render = (inref and not show_ref) or any(o.name.startswith(h) for h in hide) or o.name.startswith("UCX_") or o.type == 'EMPTY'
cam_d = bpy.data.cameras.new("qcam"); cam = bpy.data.objects.new("qcam", cam_d); sc.collection.objects.link(cam); sc.camera = cam
for vn in views:
    if vn.startswith("custom:"):
        nums = [float(x) for x in vn[7:].split(":")]; loc, tgt = nums[:3], nums[3:6]; name = opts.get("name", "custom")
    else: (loc, tgt), name = V[vn], vn
    cam.location = loc; d = Vector(tgt) - Vector(loc); cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    cam_d.lens = float(opts.get("lens", 18 if vn == "cabin" else 40)); cam_d.clip_start = 0.01
    sc.render.filepath = f"{prefix}_{name}.png"; bpy.ops.render.render(write_still=True)
print("ok")
