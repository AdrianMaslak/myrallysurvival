import bpy, json
from mathutils import Vector
from pathlib import Path
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(root/"Art/Vehicles/Octavia/Working/Octavia_Liftback_Working.blend"))
for group_name in ("hub_FL","hub_FR","hub_R_torsionbeam","strut_FL","strut_FR"):
    g=bpy.data.objects["octavia1u_"+group_name]
    for o in g.children_recursive:
        if o.type!="MESH" or not o.data.polygons: continue
        pts=[o.matrix_world@Vector(c) for c in o.bound_box]
        lo=[min(v[i] for v in pts) for i in range(3)]
        hi=[max(v[i] for v in pts) for i in range(3)]
        print("HUB "+json.dumps({"group":group_name,"name":o.name,"min":lo,"max":hi,"materials":[m.name if m else "" for m in o.data.materials]}))
