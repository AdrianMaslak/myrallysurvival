import bpy, bmesh, json
from mathutils import Vector
from mathutils.bvhtree import BVHTree
bpy.ops.wm.open_mainfile(filepath="/home/claude/octavia_drive/work/assembly.blend")
ch=bpy.data.objects["Chassis"]; bm=bmesh.new(); bm.from_mesh(ch.data); bm.transform(ch.matrix_world); t=BVHTree.FromBMesh(bm)
xs=[round(-0.80+0.01*i,3) for i in range(161)]; ys=[round(1.30-0.01*j,3) for j in range(371)]
Z=[[None]*len(xs) for _ in ys]
for j,y in enumerate(ys):
    for i,x in enumerate(xs):
        h=t.ray_cast(Vector((x,y,-0.2)),Vector((0,0,1)),2.0); Z[j][i]=round(h[0].z,4) if h[0] else None
json.dump(dict(xs=xs,ys=ys,z=Z),open("/home/claude/octavia_drive/work/underside.json","w"))
print("ok")
