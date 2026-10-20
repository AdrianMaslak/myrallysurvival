"""Re-import every Octavia part FBX over the existing static meshes, keeping asset references.
Run after export_octavia_parts.py, via scripts/Invoke-EditorPython.ps1. New parts are created.
Materials are matched by name to the existing M_* assets; the script fails if import made new ones.
"""
import json
import os
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
EXPORT = ROOT / "Art/Vehicles/Octavia/Prototype/Export"
DEST = "/Game/Vehicles/Octavia/Prototype"
manifest = json.loads((EXPORT / "assembly.json").read_text(encoding="utf-8"))
tools = unreal.AssetToolsHelpers.get_asset_tools()
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
# Legacy FBX factory, so the pivot and unit options below are honoured.
unreal.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX False")


def materials_in_dest():
    return {a for a in unreal.EditorAssetLibrary.list_assets(DEST, recursive=False)
            if isinstance(unreal.load_asset(a), unreal.MaterialInterface)}


before = materials_in_dest()
# OCTAVIA_ONLY=Chassis,Roof (environment, the parts' short names) re-imports just those.
ONLY = {"SM_Octavia_" + n for n in os.environ.get("OCTAVIA_ONLY", "").split(",") if n}
if ONLY:
    manifest["parts"] = [p for p in manifest["parts"] if p["name"] in ONLY]
# The shell, panels, lamps, glass, interior and steering wheel come from the v2 body (scripts/import_body_v2.py);
# the prototype's versions would undo it.
V2 = ROOT / "Art/Vehicles/Octavia/Body_v2/Export/Game/game_parts.json"
if V2.exists():
    v2_names = {"SM_Octavia_" + p["name"] for p in json.loads(V2.read_text(encoding="utf-8"))["parts"]}
    manifest["parts"] = [p for p in manifest["parts"] if p["name"] not in v2_names]
    unreal.log("Skipping the v2 body's meshes: " + ", ".join(sorted(v2_names)))
tasks = []
for part in manifest["parts"]:
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    options.texture_import_data.set_editor_property("material_search_location", unreal.MaterialSearchLocation.LOCAL)
    data = options.static_mesh_import_data
    for name, value in {
        "combine_meshes": True, "auto_generate_collision": False, "build_nanite": False,
        "convert_scene": True, "convert_scene_unit": True, "force_front_x_axis": False,
        "transform_vertex_to_absolute": True, "import_uniform_scale": 1.0,
        # Keep the exported face normals and smoothing as authored.
        "normal_import_method": unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
    }.items():
        data.set_editor_property(name, value)
    task = unreal.AssetImportTask()
    task.filename = str(EXPORT / part["file"])
    task.destination_path = DEST
    task.destination_name = part["name"]
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    task.factory = unreal.FbxFactory()
    tasks.append(task)
tools.import_asset_tasks(tasks)

problems = []
for part in manifest["parts"]:
    mesh = unreal.load_asset(f"{DEST}/{part['name']}")
    if not isinstance(mesh, unreal.StaticMesh):
        problems.append(part["name"] + " missing")
        continue
    box = mesh.get_bounding_box()
    size = box.max - box.min
    if any(abs(a - b) > 0.5 for a, b in zip([size.x, size.y, size.z], part["dimensions_cm"])):
        problems.append(f"{part['name']} size {[size.x, size.y, size.z]} vs {part['dimensions_cm']}")
    for slot in mesh.static_materials:
        if slot.material_interface is None:
            problems.append(f"{part['name']} slot {slot.material_slot_name} empty")
new_materials = materials_in_dest() - before
if new_materials:
    problems.append(f"import created new materials {sorted(new_materials)}")
if problems:
    raise RuntimeError("; ".join(problems))
unreal.log(f"OCTAVIA_REIMPORT_PASS: {len(manifest['parts'])} meshes replaced, sizes and materials checked.")
