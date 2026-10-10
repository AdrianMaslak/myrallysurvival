"""Add separate placeholder wheels and export a static-mesh assembly; run with Blender."""
import bpy, json, math
from pathlib import Path
from mathutils import Vector, Matrix

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"Art/Vehicles/Octavia/Prototype"
EXPORT=OUT/"Export"
TARGET=OUT/"Octavia_With_Wheels.blend"
if TARGET.exists():
    raise RuntimeError("Prototype exists; preserve user edits before generating another copy.")
EXPORT.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(ROOT/"Art/Vehicles/Octavia/Working/Octavia_Liftback_Working.blend"))
source_count=len(bpy.data.objects)
source_geometry=sum(len(o.data.polygons) for o in bpy.data.objects if o.type=="MESH")
wheel_collection=bpy.data.collections.new("05_Placeholder_Wheels")
bpy.context.scene.collection.children.link(wheel_collection)

def material(name,color,metallic=0.0):
    m=bpy.data.materials.new(name); m.diffuse_color=(*color,1)
    m.use_nodes=True
    bsdf=m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value=(*color,1)
    bsdf.inputs["Metallic"].default_value=metallic
    bsdf.inputs["Roughness"].default_value=0.7 if not metallic else 0.4
    return m

rubber=material("M_Placeholder_Rubber",(0.025,0.03,0.035))
alloy=material("M_Placeholder_Rim",(0.38,0.4,0.42),0.65)
dark=material("M_Placeholder_Hub",(0.07,0.08,0.09),0.3)

def bounds(objects, side=0):
    pts=[o.matrix_world@v.co for o in objects for v in o.data.vertices
         if not side or (o.matrix_world@v.co).x*side>0]
    lo=Vector(tuple(min(p[i] for p in pts) for i in range(3)))
    hi=Vector(tuple(max(p[i] for p in pts) for i in range(3)))
    return lo,hi

def group_meshes(name):
    return [o for o in bpy.data.objects["octavia1u_"+name].children_recursive if o.type=="MESH" and o.data.polygons]

fl=bounds(group_meshes("hub_FL"));fr=bounds(group_meshes("hub_FR"))
front_y=(fl[0].y+fl[1].y+fr[0].y+fr[1].y)/4
front_z=(fl[0].z+fl[1].z+fr[0].z+fr[1].z)/4
rear_left=bounds(group_meshes("hub_R_torsionbeam"),1)
rear_right=bounds(group_meshes("hub_R_torsionbeam"),-1)
rear_y=(rear_left[0].y+rear_left[1].y+rear_right[0].y+rear_right[1].y)/4
rear_z=(rear_left[0].z+rear_left[1].z+rear_right[0].z+rear_right[1].z)/4
# Approximate tyre centreline offset from measured outer hub faces.
front_x=(fl[1].x-fr[0].x)/2-0.015
rear_x=(rear_left[1].x-rear_right[0].x)/2-0.015
mounts={"FL":(front_x,front_y,front_z),"FR":(-front_x,front_y,front_z),
        "RL":(rear_x,rear_y,rear_z),"RR":(-rear_x,rear_y,rear_z)}
radius=0.31
width=0.205

# Surface of revolution around local X. Each wheel is a single mesh with three material slots.
def build_wheel(name,location):
    vertices=[];faces=[];material_indices=[]
    def profile(points,mat,segments=48):
        start=len(vertices)
        for x,r in points:
            for j in range(segments):
                a=2*math.pi*j/segments
                vertices.append((x,r*math.cos(a),r*math.sin(a)))
        for i in range(len(points)):
            for j in range(segments):
                faces.append((start+i*segments+j,start+i*segments+(j+1)%segments,
                              start+((i+1)%len(points))*segments+(j+1)%segments,
                              start+((i+1)%len(points))*segments+j))
                material_indices.append(mat)
    half=width/2
    profile([(-half,.205),(-half,.265),(-.085,.305),(-.065,radius),
             (.065,radius),(.085,.305),(half,.265),(half,.205)],0)
    profile([(-.08,.065),(-.08,.205),(.08,.205),(.08,.065)],1)
    profile([(-.09,.001),(-.09,.065),(.09,.065),(.09,.001)],2)
    mesh=bpy.data.meshes.new(name+"_Mesh")
    mesh.from_pydata(vertices,[],faces);mesh.update()
    obj=bpy.data.objects.new(name,mesh);wheel_collection.objects.link(obj)
    obj.location=location
    for m in (rubber,alloy,dark):mesh.materials.append(m)
    for p,index in zip(mesh.polygons,material_indices):p.material_index=index;p.use_smooth=True
    obj.color=rubber.diffuse_color
    obj["wheel_radius_m"]=radius;obj["wheel_width_m"]=width
    obj["pivot_note"]="Centre of wheel; axle along source-model X."
    return obj

wheels=[build_wheel("Wheel_"+label,point) for label,point in mounts.items()]
bpy.context.view_layer.update()
for wheel in wheels:
    lo,hi=bounds([wheel])
    if ((lo+hi)/2-wheel.location).length>0.00001:
        raise RuntimeError("Wheel pivot is not centred.")

# Real simple preview materials on chosen surfaces, unique to this new file.
bodymat=material("M_Prototype_Paint",(0.39,0.43,0.40),0.25)
glassmat=material("M_Prototype_Glass_SolidPreview",(0.07,0.13,0.17),0.2)
interiormat=material("M_Prototype_Interior",(0.065,0.07,0.075))
metalmat=material("M_Prototype_Mechanical",(0.17,0.19,0.21),0.5)
lampmat=material("M_Prototype_Headlamp",(0.6,0.65,0.65),0.2)
tailmat=material("M_Prototype_Taillamp",(0.35,0.025,0.018))
categories={"01_Body","02_Interior","03_Engine","04_Suspension"}
source_visible=[o for o in bpy.data.objects if o.type=="MESH" and o.data.polygons and any(c.name in categories for c in o.users_collection)]
for o in source_visible:
    g=o.get("source_part_group","").lower()
    category=o.users_collection[0].name
    mat=bodymat
    if category=="02_Interior":mat=interiormat
    elif category in ("03_Engine","04_Suspension"):mat=metalmat
    elif "headlight" in g:mat=lampmat
    elif "taillight" in g:mat=tailmat
    elif "glass" in g or "windshield" in g:mat=glassmat
    o.data.materials.clear();o.data.materials.append(mat)
    for p in o.data.polygons:p.material_index=0

scene=bpy.context.scene
scene.display.shading.color_type="MATERIAL"
cam=scene.camera
centre=Vector((0,0,0.8))
cam.data.ortho_scale=6.3
for name,direction in [("with-wheels-front",Vector((6,-8,3.3))),("with-wheels-rear",Vector((-6,8,3.3))),("with-wheels-side",Vector((8,0,0.8)))]:
    cam.location=centre+direction
    cam.rotation_euler=(centre-cam.location).to_track_quat("-Z","Y").to_euler()
    scene.render.filepath=str(OUT/(name+".png"))
    bpy.ops.render.render(write_still=True)
cam.location=centre+Vector((6,-8,3.3))
cam.rotation_euler=(centre-cam.location).to_track_quat("-Z","Y").to_euler()
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=="VIEW_3D":
            area.spaces.active.shading.color_type="MATERIAL"
            area.spaces.active.region_3d.view_location=centre
            area.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion()
            area.spaces.active.region_3d.view_distance=7
assert len(bpy.data.objects)==source_count+4
assert sum(len(o.data.polygons) for o in bpy.data.objects if o.type=="MESH" and o not in wheels)==source_geometry
# Original shape data and hierarchy are retained; only materials and four new wheels differ.
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(TARGET))

# Export copies only. Static chassis is consolidated; interactive panels remain separate.
panel_groups={
 "Door_FL":{"door_FL","doorglass_FL","doorpanel_FL","doortrim_FL"},
 "Door_FR":{"door_FR","doorglass_FR","doorpanel_FR","doortrim_FR"},
 "Door_RL":{"door_RL","doorglass_RL","doorpanel_RL","doortrim_RL"},
 "Door_RR":{"door_RR","doorglass_RR","doorpanel_RR","doortrim_RR"},
 "Bonnet":{"FL_hood"},
 "Trunk":{"trunk","trunkglass","spoiler","lettering_R","lettering_vRS_R","logo_R"},
 "Radiator":{"radiator","radiator_fan_L","radiator_fan_R"},
}
buckets={"Chassis":[]}
for name in panel_groups:buckets[name]=[]
for o in source_visible:
    group=o.get("source_part_group","").removeprefix("octavia1u_")
    bucket=next((k for k,v in panel_groups.items() if group in v),"Chassis")
    buckets[bucket].append(o)
for wheel in wheels:buckets[wheel.name]=[wheel]

export_collection=bpy.data.collections.new("TEMP_Export")
scene.collection.children.link(export_collection)
# Bake source -Y forward into +X forward for the export mesh coordinates.
align=Matrix.Rotation(math.pi/2,4,"Z")
manifest={"wheel_radius_cm":radius*100,"wheel_width_cm":width*100,
 "estimated_wheelbase_cm":(rear_y-front_y)*100,
 "note":"Static mesh exports, not a physics-ready vehicle. Mount positions inferred from hub bounds.",
 "axis_note":"Blender export meshes +X forward, +Z up; FBX conversion must be checked in Unreal.",
 "parts":[]}
for name,objects in buckets.items():
    if not objects:raise RuntimeError("Empty export part: "+name)
    if name.startswith("Wheel_"):pivot=objects[0].location.copy()
    elif name=="Chassis":pivot=Vector((0,0,0))
    else:
        lo,hi=bounds(objects);pivot=(lo+hi)/2
    copies=[]
    for original in objects:
        mesh=original.data.copy()
        mesh.transform(align @ Matrix.Translation(-pivot) @ original.matrix_world)
        copy=bpy.data.objects.new("EXPORT_"+name,mesh)
        export_collection.objects.link(copy);copies.append(copy)
    bpy.ops.object.select_all(action="DESELECT")
    for copy in copies:copy.select_set(True)
    bpy.context.view_layer.objects.active=copies[0]
    if len(copies)>1: bpy.ops.object.join()
    export_obj=bpy.context.view_layer.objects.active;export_obj.name="SM_Octavia_"+name
    # Recalculate winding/normals on copies only.
    bpy.ops.object.mode_set(mode="EDIT");bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False);bpy.ops.object.mode_set(mode="OBJECT")
    filename=export_obj.name+".fbx"
    bpy.ops.export_scene.fbx(filepath=str(EXPORT/filename),use_selection=True,
        object_types={"MESH"},axis_forward="-Y",axis_up="Z",apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL",bake_anim=False,use_mesh_modifiers=True,
        mesh_smooth_type="FACE",use_triangles=True,add_leaf_bones=False)
    export_pivot=align@pivot
    manifest["parts"].append({"name":export_obj.name,"file":filename,
        "source_pivot_m":list(pivot),"blender_export_pivot_cm":[v*100 for v in export_pivot],
        "unreal_expected_pivot_cm":[-pivot.y*100,-pivot.x*100,pivot.z*100],
        "pivot_type":"wheel centre" if name.startswith("Wheel_") else ("vehicle origin" if name=="Chassis" else "part bounds centre; hinges not authored"),
        "triangles":sum(len(p.vertices)-2 for p in export_obj.data.polygons)})
    bpy.data.objects.remove(export_obj,do_unlink=True)
(EXPORT/"assembly.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
print("PROTOTYPE "+json.dumps({"wheels":mounts,"exports":len(manifest["parts"]),"estimated_wheelbase_cm":manifest["estimated_wheelbase_cm"]}))
