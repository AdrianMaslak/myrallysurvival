"""Import the v2 suspension meshes written by scripts/export_suspension_v2.py into /Game/Vehicles/Octavia/Suspension.
Run via scripts/Invoke-EditorPython.ps1 -Script scripts/import_suspension_v2.py -PassMarker SUSPV2_IMPORT_PASS.

- Textures (Art/Vehicles/Octavia/Suspension_v2/textures): base colour in sRGB, the rest linear; the normal maps are
  OpenGL (+Y up), so their green channel is flipped for Unreal.
- M_Susp: the four maps, times the model's Grime vertex colour (white is clean) and a Tint the car sets as a part
  wears (a leaking damper darkens, a scored disc goes brown; white as delivered), and the bend the car sets on a
  damaged part (a kink in the middle, both ends where they were: Bend cm, BendLength cm along BendAxis from the
  mesh origin, BendDir, all in the mesh's own space; as M_Prototype_Part, scripts/setup_part_materials.py). One
  instance per surface, MI_Susp_<surface>.
- Meshes, replaced each run: each slot gets its surface's instance; sockets Fixing0, Fixing1 ... where the game's
  fixings go (X along the way they back out, Tag "<kind> <tool>"); a mesh the game aims at or carries that has no
  hulls of its own gets one convex hull (the wheels, discs, brakes and rear dampers).
The material graph is made once and kept (delete_all_material_expressions asserts in 5.8.3).
"""
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Art/Vehicles/Octavia/Suspension_v2"
EXPORT = SOURCE / "Export"
DEST = "/Game/Vehicles/Octavia/Suspension"
TEX_DEST = DEST + "/Textures"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
data = json.loads((EXPORT / "suspension_v2.json").read_text(encoding="utf-8"))
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


textures = {}
for surface in SURFACES:
    for kind in MAPS:
        # The game's retuned map (scripts/build_game_textures.py) where there is one.
        path = SOURCE / "textures_game" / ("%s_%s.png" % (surface, kind))
        if not path.exists():
            path = SOURCE / "textures" / path.name
        if not path.exists():
            continue
        name = "T_Susp_%s_%s" % (surface, kind)
        tex = import_file(path, TEX_DEST, name)
        if kind == "normal":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("flip_green_channel", True)
            tex.set_editor_property("srgb", False)
        elif kind != "basecolor":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        assets.save_loaded_asset(tex)
        textures[(surface, kind)] = tex
unreal.log("SUSPV2 textures %d" % len(textures))


def node(m, kind, x, y):
    return lib.create_material_expression(m, kind, x, y)


def connect(a, pa, b, pb):
    lib.connect_material_expressions(a, pa, b, pb)


def scalar(m, name, value, x, y):
    p = node(m, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", value)
    return p


def vector(m, name, value, x, y):
    p = node(m, unreal.MaterialExpressionVectorParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
    return p


def rgb(m, source, x, y):
    mask = node(m, unreal.MaterialExpressionComponentMask, x, y)
    for c in "rgba":
        mask.set_editor_property(c, c != "a")
    connect(source, "", mask, "")
    return mask


def sampler(m, name, tex, sampler_type, x, y):
    s = node(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    s.set_editor_property("parameter_name", name)
    s.set_editor_property("texture", tex)
    s.set_editor_property("sampler_type", sampler_type)
    return s


def binary(m, kind, a, b, x, y, pa="", pb=""):
    n = node(m, kind, x, y)
    connect(a, pa, n, "A")
    connect(b, pb, n, "B")
    return n


first = SURFACES[0]
path = DEST + "/M_Susp"
if assets.does_asset_exist(path):
    master = unreal.load_asset(path)
    unreal.log("M_Susp exists; its graph is kept")
else:
    master = tools.create_asset("M_Susp", DEST, unreal.Material, unreal.MaterialFactoryNew())
    m = master
    base = sampler(m, "BaseColor", textures[(first, "basecolor")], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1400, -400)
    normal = sampler(m, "Normal", textures[(first, "normal")], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1400, -100)
    rough = sampler(m, "Roughness", textures[(first, "roughness")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1400, 200)
    metal = sampler(m, "Metallic", textures[(first, "metallic")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1400, 500)
    grime = node(m, unreal.MaterialExpressionVertexColor, -1400, -650)
    grimy = binary(m, unreal.MaterialExpressionMultiply, base, grime, -1100, -450, "RGB", "RGB")
    tint = rgb(m, vector(m, "Tint", (1.0, 1.0, 1.0), -1100, -650), -950, -650)
    tinted = binary(m, unreal.MaterialExpressionMultiply, grimy, tint, -800, -450)
    lib.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    lib.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(metal, "R", unreal.MaterialProperty.MP_METALLIC)
    # The bend (as M_Prototype_Part): this vertex's place along the part (0 at the mesh origin, 1 at BendLength)
    # gives sin(pi t) of the bend, pushed along BendDir in the mesh's space, then turned into world space.
    world = node(m, unreal.MaterialExpressionWorldPosition, -1600, 900)
    world.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    local = node(m, unreal.MaterialExpressionTransformPosition, -1400, 900)
    local.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, "", local, "")
    axis = rgb(m, vector(m, "BendAxis", (0.0, 1.0, 0.0), -1600, 1100), -1400, 1100)
    along = binary(m, unreal.MaterialExpressionDotProduct, local, axis, -1200, 950)
    length = scalar(m, "BendLength", 40.0, -1200, 1100)
    share = binary(m, unreal.MaterialExpressionDivide, along, length, -1000, 950)
    clamped = node(m, unreal.MaterialExpressionSaturate, -850, 950)
    connect(share, "", clamped, "")
    sine = node(m, unreal.MaterialExpressionSine, -700, 950)
    sine.set_editor_property("period", 2.0)
    connect(clamped, "", sine, "")
    bend = scalar(m, "Bend", 0.0, -700, 1100)
    amount = binary(m, unreal.MaterialExpressionMultiply, sine, bend, -500, 1000)
    direction = rgb(m, vector(m, "BendDir", (-1.0, 0.0, 0.0), -700, 1250), -500, 1250)
    offset = binary(m, unreal.MaterialExpressionMultiply, direction, amount, -300, 1100)
    to_world = node(m, unreal.MaterialExpressionTransform, -100, 1100)
    to_world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    to_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(offset, "", to_world, "")
    lib.connect_material_property(to_world, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    lib.recompile_material(m)
    assets.save_loaded_asset(m)
    unreal.log("M_Susp created")

instances = {}
for surface in SURFACES:
    mi_path = DEST + "/MI_Susp_" + surface
    mi = unreal.load_asset(mi_path) if assets.does_asset_exist(mi_path) else tools.create_asset(
        "MI_Susp_" + surface, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    lib.set_material_instance_parent(mi, master)
    for param, kind in (("BaseColor", "basecolor"), ("Normal", "normal"), ("Roughness", "roughness"), ("Metallic", "metallic")):
        if (surface, kind) in textures:
            lib.set_material_instance_texture_parameter_value(mi, param, textures[(surface, kind)])
    lib.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    instances[surface] = mi

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
    for index, slot in enumerate(mesh.static_materials):
        surface = str(slot.material_slot_name).split(".")[0]
        if surface in instances:
            mesh.set_material(index, instances[surface])
        else:
            unreal.log_warning("SUSPV2 no surface for slot %s on %s" % (slot.material_slot_name, group["name"]))
    for index in range(32):
        old = mesh.find_socket("Fixing%d" % index)
        if old:
            mesh.remove_socket(old)
    for entry in group.get("sockets", []):
        socket = unreal.StaticMeshSocket(mesh)
        socket.set_editor_property("socket_name", entry["name"])
        socket.set_editor_property("relative_location", unreal.Vector(*entry["location_cm"]))
        socket.set_editor_property("relative_rotation", unreal.MathLibrary.make_rot_from_x(unreal.Vector(*entry["facing"])))
        socket.set_editor_property("tag", entry.get("tag", ""))
        mesh.add_socket(socket)
    # The parts the game aims at or carries need a hull; the model's moving parts mostly bring their own.
    if group["role"] in ("wheel", "disc", "brake", "strut") and mesh_editor.get_convex_collision_count(mesh) == 0:
        if not mesh_editor.set_convex_decomposition_collisions(mesh, 1, 32, 100000):
            raise RuntimeError("Convex collision failed for " + group["name"])
    assets.save_loaded_asset(mesh)
    unreal.log("SUSPV2 %s slots %d hulls %d sockets %d" % (group["name"], len(mesh.static_materials),
        mesh_editor.get_convex_collision_count(mesh), len(group.get("sockets", []))))
unreal.log("SUSPV2_IMPORT_PASS")
