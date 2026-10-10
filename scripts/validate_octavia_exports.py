import bpy,json,hashlib,array
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"Art/Vehicles/Octavia"
def signatures():
    result={}
    for o in bpy.data.objects:
        if o.type=="CAMERA":continue
        digest=hashlib.sha256()
        if o.type=="MESH":
            coords=array.array("f",[0.0])*(len(o.data.vertices)*3)
            o.data.vertices.foreach_get("co",coords)
            digest.update(coords.tobytes())
            indices=array.array("i",[0])*len(o.data.loops)
            o.data.loops.foreach_get("vertex_index",indices)
            digest.update(indices.tobytes())
        result[o.name]=(digest.hexdigest(),tuple(v for row in o.matrix_world for v in row))
    return result
bpy.ops.wm.open_mainfile(filepath=str(BASE/"Working/Octavia_Liftback_Working.blend"))
original=signatures()
bpy.ops.wm.open_mainfile(filepath=str(BASE/"Prototype/Octavia_With_Wheels.blend"))
current=signatures()
for name,state in original.items():
    if current.get(name)!=state:raise RuntimeError("Changed source geometry/transform: "+name)
extra=set(current)-set(original)
if extra!={"Wheel_FL","Wheel_FR","Wheel_RL","Wheel_RR"}:raise RuntimeError("Unexpected new objects: "+str(extra))
export=BASE/"Prototype/Export"
manifest=json.loads((export/"assembly.json").read_text(encoding="utf-8"))
results=[]
for part in manifest["parts"]:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(export/part["file"]))
    meshes=[o for o in bpy.context.scene.objects if o.type=="MESH"]
    if len(meshes)!=1:raise RuntimeError("Expected one mesh per export: "+part["name"])
    o=meshes[0]
    pts=[o.matrix_world@v.co for v in o.data.vertices]
    lo=Vector(tuple(min(p[i] for p in pts) for i in range(3)))
    hi=Vector(tuple(max(p[i] for p in pts) for i in range(3)))
    dims=hi-lo
    if part["name"].startswith("SM_Octavia_Wheel"):
        if ((lo+hi)/2).length>0.00001:raise RuntimeError("Wheel pivot moved on FBX round trip.")
        if any(abs(a-b)>0.0001 for a,b in zip(dims,(.62,.205,.62))):
            raise RuntimeError("Wheel dimensions/orientation changed: "+str(dims))
    if part["name"]=="SM_Octavia_Chassis" and not (4<dims.x<5 and 1.5<dims.y<2.2):
        raise RuntimeError("Chassis units or forward axis are wrong.")
    results.append({"name":part["name"],"dimensions_m":list(dims),"pivot_location_m":list(o.location)})
report={"source_geometry_and_transforms_preserved":True,"new_wheels":4,
        "fbx_roundtrip_meshes":len(results),"wheel_pivots_and_dimensions_pass":True,
        "unreal_import_verified":False,"parts":results}
(export/"validation.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
print("VALIDATION "+json.dumps({k:v for k,v in report.items() if k!="parts"}))
