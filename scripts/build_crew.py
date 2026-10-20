"""Style the supplied JashiPSX character, preserving the source; export body and matching arms."""
import bpy, bmesh, math, json, shutil
from pathlib import Path
from mathutils import Matrix, Vector
ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "Art/Characters/Crew"
DEST.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(ROOT / "Simple_Character_PSX/simple-character-psx.blend"))
rig = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
# Rename to stable engine-friendly identifiers.
names = {"spine":"pelvis", "spine.001":"spine_01", "spine.002":"spine_02", "spine.003":"chest",
         "spine.004":"neck", "spine.005":"head"}
for b in rig.data.bones:
    old = b.name
    new = names.get(old, old.replace(".", "_").lower())
    b.name = new
    for o in meshes:
        if old in o.vertex_groups: o.vertex_groups[old].name = new
# The source is 2.05 m. A 1.76 m crew member fits the existing character/seat measurements.
scale = 1.76 / max((o.matrix_world @ v.co).z for o in meshes for v in o.data.vertices)
for o in meshes:
    world = o.matrix_world.copy()
    for v in o.data.vertices: v.co = (world @ v.co) * scale
    o.parent = None
    o.matrix_world.identity()
# All bones in one transform. Scaling edit-bone heads and tails one at a time drags each connected
# neighbour along, so joints down a chain were scaled again and again (the knee ended up at 25 cm).
expected = {b.name: [scale * (rig.matrix_world @ p) for p in (b.head_local, b.tail_local)] for b in rig.data.bones}
rig.data.transform(Matrix.Scale(scale,4) @ rig.matrix_world)
rig.matrix_world.identity()
assert all((b.head_local-expected[b.name][0]).length < 1e-4 and (b.tail_local-expected[b.name][1]).length < 1e-4
           for b in rig.data.bones), "rig no longer matches the scaled mesh"
rig.name = "CrewRig"
for o in meshes:
    for m in o.modifiers:
        if m.type == "ARMATURE": m.object = rig
    o.parent = rig

def material(name, color):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    bsdf.inputs["Roughness"].default_value = .88
    return m

colors = {
    "SuitBlue": (.075,.13,.19), "Cream":(.64,.61,.49), "Red":(.40,.065,.05),
    "Boots":(.035,.032,.029), "Skin":(.52,.34,.24), "Seam":(.025,.038,.05)
}
mats = {k:material("M_Crew_"+k,v) for k,v in colors.items()}
for o in meshes:
    if o.name == "head":
        for m in o.data.materials:
            if m and m.use_nodes:
                for n in m.node_tree.nodes:
                    if n.type == "TEX_IMAGE" and n.image:
                        n.image.filepath = str(ROOT/"Simple_Character_PSX/texture/character_256.png")
                        n.image.reload()
        continue
    # Cut clean cloth panels instead of colouring whole triangular faces. Preserve skin weights.
    if o.name == "body":
        for v in o.data.vertices:
            if .80 < v.co.z < 1.04:
                v.co.x *= .78 + .22 * min(1, abs(v.co.z-.88)/.16)
            if .14 < v.co.z < .48:
                side = 1 if v.co.x > 0 else -1
                v.co.x = side*.13 + (v.co.x-side*.13)*1.25
                v.co.y *= 1.15
    bm=bmesh.new(); bm.from_mesh(o.data)
    cuts = [(Vector((0,0,z)),Vector((0,0,1))) for z in (.13,.82,.90,1.19,1.30,1.32)] if o.name=="body" else [
        (Vector((x,0,0)),Vector((1,0,0))) for x in (-.68,-.35,-.29,-.28,.28,.29,.35,.68)]
    for point,normal in cuts:
        bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
            dist=.00001,plane_co=point,plane_no=normal)
    bm.to_mesh(o.data); bm.free(); o.data.update()
    o.data.materials.clear()
    for m in mats.values(): o.data.materials.append(m)
    keys = list(mats)
    for p in o.data.polygons:
        c = sum((o.data.vertices[i].co for i in p.vertices),Vector()) / len(p.vertices)
        key = "SuitBlue"
        if o.name == "body":
            if c.z < .13: key = "Boots"
            elif .82 < c.z < .90: key = "Seam"
            elif c.z > 1.32: key = "Cream"
            elif 1.19 < c.z < 1.30: key = "Red"
        else:
            if abs(c.x) > .68: key = "Skin"
            elif abs(c.x) < .28: key = "Cream"
            elif .29 < abs(c.x) < .35: key = "Red"
        p.material_index = keys.index(key)

# Replace the source model's shorts and exposed calves with continuous suit trousers.
body=next(o for o in meshes if o.name=="body")
bm=bmesh.new();bm.from_mesh(body.data)
remove=[f for f in bm.faces if .13 < f.calc_center_median().z < .86]
bmesh.ops.delete(bm,geom=remove,context="FACES")
bm.to_mesh(body.data);bm.free()
vertices=[];faces=[];weights=[]
for side,suffix in ((1,"l"),(-1,"r")):
    start=len(vertices)
    # The waist ring sits inside the jacket hem (x ±.19 at most), so the trousers do not flare out below it.
    for z,rx,ry,cx in ((.92,.085,.09,.10),(.69,.09,.105,.125),(.48,.075,.085,.125),(.30,.065,.075,.125),(.13,.055,.07,.125)):
        for i in range(8):
            a=2*math.pi*i/8
            vertices.append((side*cx+rx*math.cos(a),.005+ry*math.sin(a),z))
            # Waist ring on the pelvis; below it thigh into shin, half and half at the knee (z .40).
            weights.append([("pelvis",1)] if z>.9 else [("thigh_"+suffix,1-max(0,min(1,(.50-z)/.2))),
                ("shin_"+suffix,max(0,min(1,(.50-z)/.2)))])
    for ring in range(4):
        for i in range(8):
            faces.append((start+ring*8+i,start+ring*8+(i+1)%8,start+(ring+1)*8+(i+1)%8,start+(ring+1)*8+i))
data=bpy.data.meshes.new("SuitTrousers")
data.from_pydata(vertices,[],faces);data.materials.append(mats["SuitBlue"])
trousers=bpy.data.objects.new("trousers",data);bpy.context.collection.objects.link(trousers)
for name in ("pelvis","thigh_l","shin_l","thigh_r","shin_r"): trousers.vertex_groups.new(name=name)
for i,groups in enumerate(weights):
    for name,w in groups:
        if w>0: trousers.vertex_groups[name].add([i],w,"REPLACE")
for p in data.polygons:p.use_smooth=True
trousers.modifiers.new("CrewSkin","ARMATURE").object=rig;trousers.parent=rig
meshes.append(trousers)

# Small, flat original team patch and zipper, skinned to the torso. No licensed sponsor logos.
def patch(name, center, size, mat, bone):
    x,y,z = center
    w,h = size
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([(x-w/2,y,z-h/2),(x+w/2,y,z-h/2),(x+w/2,y,z+h/2),(x-w/2,y,z+h/2)],[],[(0,1,2,3)])
    ob = bpy.data.objects.new(name,mesh)
    bpy.context.collection.objects.link(ob)
    ob.data.materials.append(mats[mat])
    ob.vertex_groups.new(name=bone).add(list(range(4)),1,"REPLACE")
    ob.modifiers.new("CrewSkin","ARMATURE").object=rig
    ob.parent=rig
    meshes.append(ob)
    return ob
patch("Zipper",(0,-.135,1.12),(.012,.30),"Seam","spine_02")
patch("TeamPatch",(-.095,-.14,1.30),(.12,.055),"Cream","chest")
# A simple two-colour stitched team badge, intentionally readable at PSX detail.
patch("TeamPatchRed",(-.095,-.142,1.307),(.09,.013),"Red","chest")
patch("TeamPatchBlue",(-.095,-.143,1.287),(.09,.013),"SuitBlue","chest")
shutil.copyfile(ROOT/"Simple_Character_PSX/texture/character_256.png", DEST/"character_256.png")
shutil.copyfile(ROOT/"Simple_Character_PSX/README.txt", DEST/"SOURCE-LICENSE.txt")

def export(name, selected):
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    for o in selected: o.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.fbx(filepath=str(DEST/(name+".fbx")),use_selection=True,
        object_types={"ARMATURE","MESH"},add_leaf_bones=False,bake_anim=False,
        axis_forward="-Y",axis_up="Z",apply_unit_scale=True,use_armature_deform_only=True,mesh_smooth_type="FACE")
# FBX shares its node namespace between meshes and bones.
next(o for o in meshes if o.name=="head").name="CrewHeadMesh"
# The source faces -Y; Unreal's crew solver (RallyCrewMesh.cpp) wants the character facing +X with its
# left at -Y. A quarter turn about Z for the export only; the saved .blend keeps the source orientation.
# First-person arms start at the sleeve's red band: the cream shoulders sat just under the camera
# and filled the lower half of the view.
arms=next(o for o in meshes if o.name=="arms")
fp=arms.copy(); fp.data=arms.data.copy(); fp.name="arms_fp"; bpy.context.collection.objects.link(fp)
bm=bmesh.new(); bm.from_mesh(fp.data)
bmesh.ops.delete(bm,geom=[f for f in bm.faces if abs(f.calc_center_median().x)<.29],context="FACES")
bm.to_mesh(fp.data); bm.free()
def turn(m):
    for o in meshes+[fp]: o.data.transform(m)
    rig.data.transform(m)
turn(Matrix.Rotation(math.pi/2,4,"Z"))
export("SK_Crew",meshes)
export("SK_CrewArms",[fp])
turn(Matrix.Rotation(-math.pi/2,4,"Z"))
bpy.data.objects.remove(fp)
(DEST/"materials.json").write_text(json.dumps(colors,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(DEST/"Crew_Rally96.blend"))

# Preview without changing the delivered rest-pose source.
world=bpy.context.scene.world or bpy.data.worlds.new("World")
bpy.context.scene.world=world
world.use_nodes=True
world.node_tree.nodes["Background"].inputs[0].default_value=(.13,.15,.18,1)
world.node_tree.nodes["Background"].inputs[1].default_value=.6
bpy.ops.object.camera_add(location=(2.6,-4.4,2.3))
camera=bpy.context.object
camera.rotation_euler=(Vector((0,0,.95))-camera.location).to_track_quat("-Z","Y").to_euler()
camera.data.type="ORTHO"; camera.data.ortho_scale=2.8
bpy.context.scene.camera=camera
for pos,power,size in [((2,-3,4),450,4),((-3,-1,2),220,3)]:
    bpy.ops.object.light_add(type="AREA",location=pos)
    bpy.context.object.data.energy=power; bpy.context.object.data.shape="DISK"; bpy.context.object.data.size=size
    bpy.context.object.rotation_euler=(Vector((0,0,1))-bpy.context.object.location).to_track_quat("-Z","Y").to_euler()
scene=bpy.context.scene
scene.render.engine="CYCLES"; scene.cycles.samples=16
scene.render.resolution_x=900;scene.render.resolution_y=900;scene.render.resolution_percentage=100
scene.render.filepath=str(DEST/"Crew_Rally96_preview.png")
bpy.ops.render.render(write_still=True)
print("CREW_BUILD_PASS")
