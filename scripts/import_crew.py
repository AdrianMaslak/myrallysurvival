"""Import the original rally-suit derivative and matching first-person arms, no source asset changes."""
import unreal, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"Art/Characters/Crew"
DEST="/Game/Characters/Crew"
tools=unreal.AssetToolsHelpers.get_asset_tools()
assets=unreal.EditorAssetLibrary
lib=unreal.MaterialEditingLibrary
def import_one(file,name,options=None):
    t=unreal.AssetImportTask()
    t.filename=str(file);t.destination_path=DEST;t.destination_name=name
    t.automated=True;t.replace_existing=True;t.save=True
    if options:
        t.options=options
        t.factory=unreal.FbxFactory()
    tools.import_asset_tasks([t])
    obj=unreal.load_asset(DEST+"/"+name)
    assert obj, name
    return obj
tex=import_one(SRC/"character_256.png","T_CrewFace")
tex.set_editor_property("filter",unreal.TextureFilter.TF_NEAREST)
assets.save_loaded_asset(tex)
colors=json.loads((SRC/"materials.json").read_text())
materials={}
for name,color in list(colors.items())+[("Face",None),("Glass",(.07,.08,.08))]:
    path=DEST+"/M_Crew_"+name
    m=unreal.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        "M_Crew_"+name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    # New assets only; reruns keep their graph.
    if not lib.get_num_material_expressions(m):
        if color is None:
            n=lib.create_material_expression(m,unreal.MaterialExpressionTextureSample, -300,0)
            n.set_editor_property("texture",tex)
            lib.connect_material_property(n,"RGB",unreal.MaterialProperty.MP_BASE_COLOR)
        else:
            n=lib.create_material_expression(m,unreal.MaterialExpressionVectorParameter,-300,0)
            n.set_editor_property("parameter_name","SuitTint")
            n.set_editor_property("default_value",unreal.LinearColor(*color,1))
            lib.connect_material_property(n,"RGB",unreal.MaterialProperty.MP_BASE_COLOR)
        rough=lib.create_material_expression(m,unreal.MaterialExpressionConstant,-300,180)
        rough.set_editor_property("r",.9)
        lib.connect_material_property(rough,"",unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property("two_sided",True)
    m.set_editor_property("used_with_skeletal_mesh",True)
    lib.recompile_material(m);assets.save_loaded_asset(m);materials[name]=m
skeleton=None
for name in ("SK_Crew","SK_CrewArms"):
    opt=unreal.FbxImportUI()
    opt.set_editor_property("import_mesh",True)
    opt.set_editor_property("import_as_skeletal",True)
    opt.set_editor_property("mesh_type_to_import",unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    opt.set_editor_property("automated_import_should_detect_type",False)
    opt.set_editor_property("import_materials",False)
    opt.set_editor_property("import_textures",False)
    opt.set_editor_property("import_animations",False)
    opt.set_editor_property("create_physics_asset",False)
    if skeleton: opt.set_editor_property("skeleton",skeleton)
    data=opt.get_editor_property("skeletal_mesh_import_data")
    data.set_editor_property("force_front_x_axis",True)
    data.set_editor_property("convert_scene",True)
    data.set_editor_property("convert_scene_unit",True)
    mesh=import_one(SRC/(name+".fbx"),name,opt)
    skeleton=mesh.get_editor_property("skeleton")
    assert skeleton, "Skeletal mesh has no skeleton"
    assets.save_loaded_asset(skeleton,only_if_is_dirty=False)
    slots=mesh.get_editor_property("materials")
    for slot in slots:
        label=str(slot.get_editor_property("material_slot_name"))
        key=label.replace("M_Crew_","")
        if key not in materials: key="Glass" if "glass" in label.lower() else "Face"
        slot.set_editor_property("material_interface",materials[key])
        unreal.log("CREW_SLOT "+name+" "+label+" "+key)
    mesh.set_editor_property("materials",slots)
    assets.save_loaded_asset(mesh)
for kind in ("Ratchet","Screwdriver","Pliers","Hammer"):
    name="SM_Crew_"+kind
    if not (SRC/(name+".fbx")).exists(): continue
    opt=unreal.FbxImportUI()
    opt.set_editor_property("import_as_skeletal",False)
    opt.set_editor_property("mesh_type_to_import",unreal.FBXImportType.FBXIT_STATIC_MESH)
    opt.set_editor_property("automated_import_should_detect_type",False)
    opt.set_editor_property("import_materials",False);opt.set_editor_property("import_textures",False)
    opt.static_mesh_import_data.set_editor_property("force_front_x_axis",True)
    mesh=import_one(SRC/(name+".fbx"),name,opt)
    for i,slot in enumerate(mesh.get_editor_property("static_materials")):
        key=str(slot.material_slot_name).replace("M_Crew_","")
        mesh.set_material(i,materials.get(key,materials["Cream"]))
    assets.save_loaded_asset(mesh)
assets.save_directory(DEST,only_if_is_dirty=False,recursive=True)
unreal.log("CREW_IMPORT_PASS")
