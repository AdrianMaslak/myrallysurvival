# render one object, one material at a time, from several views
import bpy, sys, bmesh
from mathutils import Vector
a = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.open_mainfile(filepath=a[0]); objname = a[1]; views = a[2].split(","); prefix = a[3]
sc = bpy.context.scene; sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading; sh.light = 'STUDIO'; sh.color_type = 'SINGLE'; sh.show_cavity = True
sc.render.resolution_x, sc.render.resolution_y = 900, 600
src = bpy.data.objects[objname]
for o in bpy.data.objects: o.hide_render = True
V = {"side": ((-6.5, 0, 0.7), (0, 0, 0.7)), "under": ((-1.5, 0.5, -5.5), (0, 0, 0.3)), "fl34": ((-4.2, 5.2, 2.4), (0, 0.2, 0.6)),
     "rr34": ((4.2, -5.4, 2.4), (0, -0.2, 0.6)), "top": ((0, 0, 8), (0, 0, 0)), "inside": ((0.6, -0.3, 1.0), (-0.8, 0.3, 0.5))}
cam_d = bpy.data.cameras.new("c"); cam = bpy.data.objects.new("c", cam_d); sc.collection.objects.link(cam); sc.camera = cam; cam_d.lens = 35
for i, m in enumerate(src.data.materials):
    me = src.data.copy(); bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index != i], context='FACES'); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(f"part{i}", me); sc.collection.objects.link(ob); ob.matrix_world = src.matrix_world
    for vn in views:
        loc, tgt = V[vn]; cam.location = loc; cam.rotation_euler = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = f"{prefix}_{i}_{m.name}_{vn}.png"; bpy.ops.render.render(write_still=True)
    ob.hide_render = True
