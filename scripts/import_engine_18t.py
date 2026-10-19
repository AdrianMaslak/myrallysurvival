"""Import the 1.8T engine bay parts written by scripts/export_engine_18t.py into /Game/Vehicles/Octavia/Engine18T.
Run via scripts/Invoke-EditorPython.ps1 -Script scripts/import_engine_18t.py -PassMarker ENGINE18T_IMPORT_PASS.

- Textures (Art/Vehicles/Octavia/Engine_18T/textures): base colour in sRGB, the rest linear; the normal maps are
  OpenGL (+Y up), so their green channel is flipped for Unreal.
- M_Engine18T: the four maps, times the model's Grime vertex colour (white is clean), and the leak look the car
  sets per car (URallyLeakComponent): Wet (0..1) below WetHeight (cm, the mesh's own space, soft over WetBlend)
  turns it WetTint and shiny. One instance per surface, MI_Engine18T_<surface>, with that surface's maps.
- Meshes, replaced each run: each group's material slots get their surface's instance; modules get sockets
  Fixing0, Fixing1 ... where the game's fixings go (X along the way they back out, Tag "<kind> <tool>",
  from which the game takes how many there are and what turns each); a module without
  collision hulls of its own gets one convex hull (it is aimed at and carried by it).
The material graph is made once and kept (delete_all_material_expressions asserts in 5.8.3).
"""
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Art/Vehicles/Octavia/Engine_18T"
EXPORT = SOURCE / "Export"
DEST = "/Game/Vehicles/Octavia/Engine18T"
TEX_DEST = DEST + "/Textures"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
data = json.loads((EXPORT / "engine_18t.json").read_text(encoding="utf-8"))
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
    asset = unreal.load_asset(destination + "/" + name)
    if not asset:
        raise RuntimeError("Import failed: " + str(filename))
    return asset


# Textures.
textures = {}
for surface in SURFACES:
    for kind in MAPS:
        name = "T_Engine18T_%s_%s" % (surface, kind)
        tex = import_file(SOURCE / "textures" / ("%s_%s.png" % (surface, kind)), TEX_DEST, name)
        if kind == "normal":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("flip_green_channel", True)
        elif kind != "basecolor":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        assets.save_loaded_asset(tex)
        textures[(surface, kind)] = tex
unreal.log("ENGINE18T textures %d" % len(textures))


def node(m, kind, x, y):
    return lib.create_material_expression(m, kind, x, y)


def scalar(m, name, value, x, y):
    p = node(m, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", value)
    return p


def binary(m, kind, a, b, x, y, pa="", pb=""):
    n = node(m, kind, x, y)
    lib.connect_material_expressions(a, pa, n, "A")
    lib.connect_material_expressions(b, pb, n, "B")
    return n


def lerp(m, a, b, alpha, x, y, pa=""):
    n = node(m, unreal.MaterialExpressionLinearInterpolate, x, y)
    lib.connect_material_expressions(a, pa, n, "A")
    lib.connect_material_expressions(b, "", n, "B")
    lib.connect_material_expressions(alpha, "", n, "Alpha")
    return n


def sampler(m, name, tex, sampler_type, x, y):
    s = node(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    s.set_editor_property("parameter_name", name)
    s.set_editor_property("texture", tex)
    s.set_editor_property("sampler_type", sampler_type)
    return s


path = DEST + "/M_Engine18T"
if assets.does_asset_exist(path):
    master = unreal.load_asset(path)
    unreal.log("M_Engine18T exists; its graph is kept")
else:
    master = tools.create_asset("M_Engine18T", DEST, unreal.Material, unreal.MaterialFactoryNew())
    m = master
    base = sampler(m, "BaseColor", textures[("cast_alu", "basecolor")], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1400, -400)
    normal = sampler(m, "Normal", textures[("cast_alu", "normal")], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1400, -100)
    rough = sampler(m, "Roughness", textures[("cast_alu", "roughness")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1400, 200)
    metal = sampler(m, "Metallic", textures[("cast_alu", "metallic")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1400, 500)
    grime = node(m, unreal.MaterialExpressionVertexColor, -1400, -650)
    grimy = binary(m, unreal.MaterialExpressionMultiply, base, grime, -1100, -450, "RGB", "RGB")
    # Wet below WetHeight in the mesh's own space.
    world = node(m, unreal.MaterialExpressionWorldPosition, -1800, 900)
    world.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    local = node(m, unreal.MaterialExpressionTransformPosition, -1600, 900)
    local.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    lib.connect_material_expressions(world, "", local, "")
    height = node(m, unreal.MaterialExpressionComponentMask, -1400, 900)
    for c in "rgba":
        height.set_editor_property(c, c == "b")
    lib.connect_material_expressions(local, "", height, "")
    wet = scalar(m, "Wet", 0.0, -1400, 1050)
    wet_height = scalar(m, "WetHeight", -1000.0, -1400, 1150)
    wet_blend = scalar(m, "WetBlend", 6.0, -1400, 1250)
    below = binary(m, unreal.MaterialExpressionSubtract, wet_height, height, -1200, 1000)
    share = binary(m, unreal.MaterialExpressionDivide, below, wet_blend, -1050, 1000)
    below_mask = node(m, unreal.MaterialExpressionSaturate, -900, 1000)
    lib.connect_material_expressions(share, "", below_mask, "")
    wet_mask = binary(m, unreal.MaterialExpressionMultiply, below_mask, wet, -750, 1000)
    tint = node(m, unreal.MaterialExpressionVectorParameter, -750, 1150)
    tint.set_editor_property("parameter_name", "WetTint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.008, 0.006, 0.004, 1.0))
    colour = lerp(m, grimy, tint, wet_mask, -500, -400)
    lib.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    shiny = node(m, unreal.MaterialExpressionConstant, -750, 300)
    shiny.set_editor_property("r", 0.08)
    roughness = lerp(m, rough, shiny, wet_mask, -500, 200, "R")
    lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    zero = node(m, unreal.MaterialExpressionConstant, -750, 600)
    metallic = lerp(m, metal, zero, wet_mask, -500, 500, "R")
    lib.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    lib.recompile_material(m)
    assets.save_loaded_asset(m)
    unreal.log("M_Engine18T created")

instances = {}
for surface in SURFACES:
    mi_path = DEST + "/MI_Engine18T_" + surface
    mi = unreal.load_asset(mi_path) if assets.does_asset_exist(mi_path) else tools.create_asset(
        "MI_Engine18T_" + surface, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    lib.set_material_instance_parent(mi, master)
    for param, kind in (("BaseColor", "basecolor"), ("Normal", "normal"), ("Roughness", "roughness"), ("Metallic", "metallic")):
        lib.set_material_instance_texture_parameter_value(mi, param, textures[(surface, kind)])
    lib.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    instances[surface] = mi

# Meshes.
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX False")
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
        raise RuntimeError("Mesh import failed: " + group["name"])
    box = mesh.get_bounding_box()
    size = [box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z]
    if any(abs(a - b) > 0.5 for a, b in zip(size, group["dimensions_cm"])):
        raise RuntimeError("Size or orientation off for %s: %s vs %s" % (group["name"], size, group["dimensions_cm"]))
    # Each slot to its surface's instance (slots are named after the model's materials).
    for index, slot in enumerate(mesh.static_materials):
        surface = str(slot.material_slot_name).split(".")[0]
        if surface in instances:
            mesh.set_material(index, instances[surface])
        else:
            unreal.log_warning("ENGINE18T no surface for slot %s on %s" % (slot.material_slot_name, group["name"]))
    for index in range(32):
        old = mesh.find_socket("Fixing%d" % index)
        if old:
            mesh.remove_socket(old)
    for entry in group.get("sockets", []):
        socket = unreal.StaticMeshSocket(mesh)
        socket.set_editor_property("socket_name", entry["name"])
        socket.set_editor_property("relative_location", unreal.Vector(*entry["location_cm"]))
        socket.set_editor_property("relative_rotation", unreal.MathLibrary.make_rot_from_x(unreal.Vector(*entry["facing"])))
        # What it is and what turns it, in the game's terms ("Nut Spanner13"; URallyPanelComponent::BuildFixings).
        socket.set_editor_property("tag", entry.get("tag", ""))
        mesh.add_socket(socket)
    # Fixing heads are drawn, never aimed at or carried: no hull.
    if group["name"] not in ("SM_Engine18T_Engine", "SM_Engine18T_Pulley", "SM_Engine18T_Bay") and not group["name"].startswith("SM_Engine18T_Fixing")             and mesh_editor.get_convex_collision_count(mesh) == 0:
        if not mesh_editor.set_convex_decomposition_collisions(mesh, 1, 32, 100000):
            raise RuntimeError("Convex collision failed for " + group["name"])
    assets.save_loaded_asset(mesh)
    unreal.log("ENGINE18T %s slots %d hulls %d sockets %d" % (group["name"], len(mesh.static_materials),
        mesh_editor.get_convex_collision_count(mesh), len(group.get("sockets", []))))
unreal.log("ENGINE18T_IMPORT_PASS")
