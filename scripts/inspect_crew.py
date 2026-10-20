import bpy, json
from pathlib import Path
root = Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(root / "Simple_Character_PSX/simple-character-psx.blend"))
out = {"objects": [], "actions": [a.name for a in bpy.data.actions]}
for o in bpy.context.scene.objects:
    d = {"name":o.name,"type":o.type,"location":list(o.location),"rotation":list(o.rotation_euler),"scale":list(o.scale),"dimensions":list(o.dimensions)}
    if o.type == "ARMATURE":
        d["bones"] = [{"name":b.name,"parent":b.parent.name if b.parent else None,"head":list(b.head_local),"tail":list(b.tail_local)} for b in o.data.bones]
    if o.type == "MESH":
        d["materials"] = [m.name if m else None for m in o.data.materials]
        d["groups"] = [g.name for g in o.vertex_groups]
        d["bounds"] = [list(v) for v in o.bound_box]
    out["objects"].append(d)
dest = root / "Art/Characters/Crew"
dest.mkdir(parents=True,exist_ok=True)
(dest / "inspection.json").write_text(json.dumps(out,indent=2))
print("CREW_INSPECT_PASS", json.dumps(out))
