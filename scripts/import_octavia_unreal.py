"""Import the prototype FBXs and assemble a static preview in the gravel lab."""
import json
import runpy
from pathlib import Path
import unreal

ROOT=Path(__file__).resolve().parents[1]
EXPORT=ROOT/"Art/Vehicles/Octavia/Prototype/Export"
DEST="/Game/Vehicles/Octavia/Prototype"
MAP="/Game/Maps/L_GravelLab"
manifest=json.loads((EXPORT/"assembly.json").read_text(encoding="utf-8"))
# Expected sizes come from assembly.json (written by export_octavia_parts.py), in Unreal cm.
expected={p["name"]:p for p in manifest["parts"]}
tools=unreal.AssetToolsHelpers.get_asset_tools()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
# Use the legacy FBX factory explicitly so the pivot/unit options below are honored.
unreal.SystemLibrary.execute_console_command(world,"Interchange.FeatureFlags.Import.FBX False")
results=[]
assets={}
for part in manifest["parts"]:
    path=DEST+"/"+part["name"]
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        options=unreal.FbxImportUI()
        options.set_editor_property("import_mesh",True)
        options.set_editor_property("import_as_skeletal",False)
        options.set_editor_property("import_materials",True)
        options.set_editor_property("import_textures",False)
        options.set_editor_property("import_animations",False)
        options.set_editor_property("automated_import_should_detect_type",False)
        options.set_editor_property("mesh_type_to_import",unreal.FBXImportType.FBXIT_STATIC_MESH)
        data=options.static_mesh_import_data
        for name,value in {
            "combine_meshes":True, "auto_generate_collision":False,
            "build_nanite":False, "convert_scene":True, "convert_scene_unit":True,
            "force_front_x_axis":False, "transform_vertex_to_absolute":True,
            "import_uniform_scale":1.0
        }.items():data.set_editor_property(name,value)
        task=unreal.AssetImportTask()
        task.filename=str(EXPORT/part["file"])
        task.destination_path=DEST
        task.destination_name=part["name"]
        task.automated=True
        task.replace_existing=False
        task.save=True
        task.options=options
        task.factory=unreal.FbxFactory()
        tools.import_asset_tasks([task])
    mesh=unreal.load_asset(path)
    if not isinstance(mesh,unreal.StaticMesh):
        raise RuntimeError("Static mesh import failed: "+path)
    box=mesh.get_bounding_box()
    size=box.max-box.min
    dimensions=[size.x,size.y,size.z]
    expected_cm=expected[part["name"]]["dimensions_cm"]
    if any(abs(a-b)>0.5 for a,b in zip(dimensions,expected_cm)):
        raise RuntimeError(f"Imported units/orientation mismatch for {part['name']}: {dimensions} vs {expected_cm}")
    if "_Wheel_" in part["name"]:
        centre=(box.min+box.max)*0.5
        if max(abs(centre.x),abs(centre.y),abs(centre.z))>0.05:
            raise RuntimeError("Wheel pivot shifted in Unreal import.")
    assets[part["name"]]=mesh
    results.append({"asset":path,"dimensions_cm":dimensions})

# LevelEditorSubsystem dereferences null in commandlets (-run=pythonscript crashed here).
if "-run=" in unreal.SystemLibrary.get_command_line().lower():
    raise RuntimeError("Meshes imported. Map assembly needs the full editor: "
                       "scripts/Invoke-EditorPython.ps1 -Script scripts/import_octavia_unreal.py")
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
    raise RuntimeError("Save the open level before assembling the preview.")
if not unreal.EditorAssetLibrary.does_asset_exist(MAP):
    runpy.run_path(str(ROOT/"scripts/create_gravel_lab.py"),run_name="__main__")
elif not levels.load_level(MAP):
    raise RuntimeError("Could not load gravel lab.")
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if any(a.get_actor_label().startswith("OctaviaPreview_") for a in actors.get_all_level_actors()):
    raise RuntimeError("Octavia preview already exists; preserving it rather than spawning duplicates.")
offset=unreal.Vector(1500,-1600,6)
for part in manifest["parts"]:
    location=offset+unreal.Vector(*part["unreal_expected_pivot_cm"])
    actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,location)
    if actor is None:raise RuntimeError("Failed to spawn preview part.")
    actor.set_actor_label("OctaviaPreview_"+part["name"])
    actor.set_folder_path("Octavia_StaticPreview")
    actor.static_mesh_component.set_static_mesh(assets[part["name"]])
    actor.static_mesh_component.set_collision_profile_name("NoCollision")
if not levels.save_current_level():raise RuntimeError("Could not save assembled preview.")
report={"static_mesh_import_verified":True,"wheel_scale_and_pivots_verified":True,
        "preview_map":MAP,"drivable":False,"parts":results}
(EXPORT/"unreal-import-validation.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
unreal.log("OCTAVIA_IMPORT_PASS: 12 meshes, wheel dimensions/pivots checked; static lab preview saved.")
