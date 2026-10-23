"""Render objects with their wireframe (for topology checks). usage: -- file.blend out.png obj1,obj2 camx,camy,camz,tx,ty,tz [lens]"""
import bpy, sys
from mathutils import Vector
a = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.open_mainfile(filepath=a[0]); out = a[1]; names = a[2].split(","); cv = [float(x) for x in a[3].split(",")]
lens = float(a[4]) if len(a) > 4 else 50
sc = bpy.context.scene; sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading; sh.light = 'STUDIO'; sh.color_type = 'OBJECT'; sh.show_cavity = False
sc.render.resolution_x, sc.render.resolution_y = 1400, 900
for o in bpy.data.objects: o.hide_render = o.name not in names
for n in names:
    o = bpy.data.objects[n]; o.color = (0.75, 0.75, 0.78, 1)
    w = o.copy(); w.data = o.data; sc.collection.objects.link(w); w.color = (0.05, 0.05, 0.1, 1); w.hide_render = False
    m = w.modifiers.new("wf", 'WIREFRAME'); m.thickness = 0.0025; m.use_relative_offset = False; m.use_even_offset = False
cam_d = bpy.data.cameras.new("c"); cam = bpy.data.objects.new("c", cam_d); sc.collection.objects.link(cam); sc.camera = cam; cam_d.lens = lens
cam.location = cv[:3]; cam.rotation_euler = (Vector(cv[3:6]) - Vector(cv[:3])).to_track_quat('-Z', 'Y').to_euler()
sc.render.filepath = out; bpy.ops.render.render(write_still=True)
