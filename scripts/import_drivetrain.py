"""Import the drivetrain kit's meshes written by scripts/export_drivetrain.py into /Game/Vehicles/Octavia/Drivetrain.
Run via scripts/Invoke-EditorPython.ps1 -Script scripts/import_drivetrain.py -PassMarker DRIVE_IMPORT_PASS (editor closed).

- Textures (Art/Vehicles/Octavia/Drivetrain/textures): base colour sRGB, the rest linear; the normal maps are OpenGL
  (+Y up), so their green channel is flipped for Unreal.
- Materials: one instance per surface, MI_Drive_<surface>, on the suspension's M_Susp (the four maps, the model's Grime
  vertex colour, a Tint and the bend; scripts/import_suspension_v2.py makes it), so a bent propshaft or a dented
  exhaust can show it the same way.
- Meshes, replaced each run: each slot gets its surface's instance; sockets Fixing0, Fixing1 ... where the game's fixings
  go (X along the way they back out, Tag "<kind> <tool>"); the kit brings its own hulls.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Art/Vehicles/Octavia/Drivetrain"
EXPORT = SOURCE / "Export"
DEST = "/Game/Vehicles/Octavia/Drivetrain"
TEX_DEST = DEST + "/Textures"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
data = json.loads((EXPORT / "drivetrain.json").read_text(encoding="utf-8"))
SURFACES = sorted({p.stem.rsplit("_", 1)[0] for p in (SOURCE / "textures").glob("*_basecolor.png")})
MAPS = ("basecolor", "normal", "roughness", "metallic")


def import_file(filename, destination, name):
    task = unreal.AssetImportTask()
    task.filename = str(filename)
    task.destination_path = destination
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])
    return unreal.load_asset(destination + "/" + name)


master = unreal.load_asset("/Game/Vehicles/Octavia/Suspension/M_Susp")
if master is None:
    raise RuntimeError("M_Susp is missing: run scripts/import_suspension_v2.py first")
instances = {}
for surface in SURFACES:
    mi_path = DEST + "/MI_Drive_" + surface
    mi = unreal.load_asset(mi_path) if assets.does_asset_exist(mi_path) else tools.create_asset(
        "MI_Drive_" + surface, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    lib.set_material_instance_parent(mi, master)
    for param, kind in (("BaseColor", "basecolor"), ("Normal", "normal"), ("Roughness", "roughness"), ("Metallic", "metallic")):
        path = SOURCE / "textures" / ("%s_%s.png" % (surface, kind))
        if not path.exists():
            continue
        tex = import_file(path, TEX_DEST, "T_Drive_%s_%s" % (surface, kind))
        if kind == "normal":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("flip_green_channel", True)
            tex.set_editor_property("srgb", False)
        elif kind != "basecolor":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        assets.save_loaded_asset(tex)
        lib.set_material_instance_texture_parameter_value(mi, param, tex)
    lib.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    instances[surface] = mi

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX False")
problems = []
for group in data["groups"]:
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    sm = options.static_mesh_import_data
    for name, value in {"combine_meshes": True, "auto_generate_collision": False, "one_convex_hull_per_ucx": True,
                        "build_nanite": False, "convert_scene": True, "convert_scene_unit": True, "force_front_x_axis": False,
                        "transform_vertex_to_absolute": True, "import_uniform_scale": 1.0,
                        "vertex_color_import_option": unreal.VertexColorImportOption.REPLACE}.items():
        sm.set_editor_property(name, value)
    task = unreal.AssetImportTask()
    task.filename = str(EXPORT / group["file"])
    task.destination_path = DEST
    task.destination_name = group["name"]
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    task.factory = unreal.FbxFactory()
    tools.import_asset_tasks([task])
    mesh = unreal.load_asset(DEST + "/" + group["name"])
    if not isinstance(mesh, unreal.StaticMesh):
        problems.append(group["name"] + " missing")
        continue
    box = mesh.get_bounding_box()
    size = [box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z]
    if any(abs(a - b) > 0.5 for a, b in zip(size, group["dimensions_cm"])):
        problems.append("%s size %s vs %s" % (group["name"], [round(v, 1) for v in size], group["dimensions_cm"]))
    for index, slot in enumerate(mesh.static_materials):
        surface = str(slot.get_editor_property("imported_material_slot_name"))
        if surface in instances:
            mesh.set_material(index, instances[surface])
        else:
            problems.append("%s slot %s has no surface" % (group["name"], surface))
    for index in range(32):
        old = mesh.find_socket("Fixing%d" % index)
        if old:
            mesh.remove_socket(old)
    for entry in group["sockets"]:
        socket = unreal.StaticMeshSocket(mesh)
        socket.set_editor_property("socket_name", entry["name"])
        socket.set_editor_property("relative_location", unreal.Vector(*entry["location_cm"]))
        socket.set_editor_property("relative_rotation", unreal.MathLibrary.make_rot_from_x(unreal.Vector(*entry["facing"])))
        socket.set_editor_property("tag", entry["tag"])
        mesh.add_socket(socket)
    assets.save_loaded_asset(mesh)
    unreal.log("DRIVE %s slots %d hulls %d sockets %d" % (group["name"], len(mesh.static_materials),
        unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_convex_collision_count(mesh), len(group["sockets"])))
if problems:
    raise RuntimeError("; ".join(problems))
unreal.log("DRIVE_IMPORT_PASS %d meshes" % len(data["groups"]))
