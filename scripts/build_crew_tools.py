"""Small original workshop tools. Grip at origin, working end along Blender -Y (UE +Y; the game turns it forward)."""
import bpy, math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/"Art/Characters/Crew"
bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name,rgb,metal=0):
    m=bpy.data.materials.new(name);m.diffuse_color=(*rgb,1);m.use_nodes=True
    p=m.node_tree.nodes.get("Principled BSDF");p.inputs["Base Color"].default_value=(*rgb,1)
    p.inputs["Metallic"].default_value=metal;p.inputs["Roughness"].default_value=.6
    return m
steel=mat("M_Crew_Cream",(.35,.37,.36),.7)
grip=mat("M_Crew_Red",(.25,.035,.025))
parts=[]
def box(loc,size,material):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc)
    o=bpy.context.object;o.scale=size;o.data.materials.append(material);parts.append(o)
def cylinder(loc,radius,length,material,rot=(0,0,0)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=10,radius=radius,depth=length,location=loc,rotation=rot)
    o=bpy.context.object;o.data.materials.append(material);parts.append(o)
for kind in ("Ratchet","Screwdriver","Pliers","Hammer"):
    parts=[]
    if kind=="Ratchet":
        box((0,-.065,0),(.015,.21,.009),steel)
        box((0,.015,0),(.022,.065,.018),grip)
        cylinder((0,-.17,0),.021,.013,steel)
        cylinder((0,-.17,-.035),.011,.06,steel)
    elif kind=="Screwdriver":
        cylinder((0,0,0),.016,.09,grip,(math.pi/2,0,0))
        cylinder((0,-.11,0),.004,.14,steel,(math.pi/2,0,0))
        box((0,-.184,0),(.009,.014,.002),steel)
    elif kind=="Pliers":
        for s in (-1,1):
            box((s*.012,.015,0),(.014,.10,.012),grip)
            box((s*.008,-.06,0),(.012,.055,.014),steel)
        cylinder((0,-.04,0),.018,.018,steel)
    else:
        box((0,-.035,0),(.023,.20,.023),grip)
        box((0,-.14,0),(.11,.035,.035),steel)
    bpy.ops.object.select_all(action="DESELECT")
    for o in parts:o.select_set(True)
    bpy.context.view_layer.objects.active=parts[0]
    bpy.ops.object.join()
    o=bpy.context.object;o.name="SM_Crew_"+kind
    bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    bpy.ops.export_scene.fbx(filepath=str(DEST/(o.name+".fbx")),use_selection=True,object_types={"MESH"},
        axis_forward="-Y",axis_up="Z",bake_anim=False)
    bpy.data.objects.remove(o,do_unlink=True)
print("CREW_TOOLS_PASS")
