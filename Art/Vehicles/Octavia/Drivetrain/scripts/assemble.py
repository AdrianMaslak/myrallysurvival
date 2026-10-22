"""one assembly scene in the car frame: body Chassis etc, suspension v2 (real scale), engine set (x0.88 at 0.16,1.507,0.381).
World matrices are read with each source file open (so parenting is evaluated), then the objects are loaded unparented."""
import bpy
from mathutils import Matrix
skipf = lambda n: not any(k in n for k in ("_Bolt_", "_Nut_", "_Screw_", "_Clip_", "BoltPoint", "UCX_"))
T_ENG = Matrix.Translation((0.16, 1.507, 0.381)) @ Matrix.Scale(0.88, 4)
SRC = [("/home/claude/octavia18t/export/Octavia1U_1.8T_20V.blend", "ENGINE", T_ENG, lambda o: o.type == 'MESH' and skipf(o.name)),
       ("/home/claude/octavia_susp/export/Octavia1U_Suspension.blend", "SUSP", Matrix.Identity(4), lambda o: o.type == 'MESH' and skipf(o.name)),
       ("/home/claude/octavia_body/export/Octavia1U_Body_v2.blend", "BODY", Matrix.Identity(4),
        lambda o: o.name in ("Chassis", "FrontBumper", "RearBumper", "Fender_FL", "Fender_FR"))]
mats = {}
for path, coll, T, keep in SRC:
    bpy.ops.wm.open_mainfile(filepath=path)
    mats[coll] = {o.name: T @ o.matrix_world for o in bpy.data.objects if keep(o)}
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
for path, coll, T, keep in SRC:
    names = set(mats[coll])
    with bpy.data.libraries.load(path, link=False) as (src, dst):
        dst.objects = [n for n in src.objects if n in names]
    c = bpy.data.collections.new(coll); sc.collection.children.link(c)
    for o in dst.objects:
        if o is None: continue
        c.objects.link(o); o.parent = None; o.constraints.clear(); o.matrix_world = mats[coll][o.name]
        o.animation_data_clear()
    print(coll, len(c.objects))
bpy.ops.wm.save_as_mainfile(filepath="/home/claude/octavia_drive/work/assembly.blend")
