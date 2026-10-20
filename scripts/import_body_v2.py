"""Import the user's v2 body and interior (Art/Vehicles/Octavia/Body_v2) over the game's SM_Octavia_* shell meshes.
Run via scripts/Invoke-EditorPython.ps1 -Script scripts/import_body_v2.py -PassMarker BODYV2_IMPORT_PASS (editor
closed), after scripts/export_body_v2.py has written Export/Game (file paths in game_parts.json are relative to Export).

- The meshes keep their asset names in /Game/Vehicles/Octavia/Prototype, so the car, the panels, the dents and the
  loose pieces pick them up unchanged. Each was exported at the pivot the game already uses (game_parts.json).
- Textures (textures/<surface>_<map>.png): base colour and emissive sRGB, the rest linear; the normal maps are
  OpenGL (+Y up), so their green channel is flipped for Unreal.
- Vertex colour: R is the paint condition the dent code lowers (1 intact), G and B the model's Grime (white clean).
- M_Body: base colour times Grime, darkened by DamageDarken where R drops (scrapes, a smashed lamp); normal,
  roughness, metallic; emissive map times EmissiveStrength, 0 as delivered (the game sets it to light a lamp).
  One instance per surface, MI_Body_<surface>.
- M_Body_Paint: the livery (T_Octavia_Livery on UV 1, docs/PAINTING.md) times Grime, scraped to bare steel where R
  drops, as M_Prototype_Paint; the paint's own normal, roughness and metallic maps on UV 0.
- Glass keeps M_Prototype_Glass_SolidPreview, which already crazes on R (scripts/setup_damage_assets.py).
- M_Body_Dash for the gauge faces and warning lights: Backlight lights the dials, Warn0..Warn7 each warning lamp
  (URallyCabinComponent sets them). A lens's emissive is its own base colour, so it glows its colour when lit.
- Maps come from textures_game/ (scripts/build_game_textures.py: rougher worn paint, duller chrome, the interior's
  colours, the fixed speedometer) where there is one there, else from textures/.
- The wipers, gauge needles, pedals, levers, stalks, glove box and sun visors are their own meshes
  (SM_Octavia_Wiper_L and so on, at their pivots; game_parts.json has each one's rig).
- The dent-able meshes get Allow CPU Access; parts that come off and have no hulls of their own get one.
Material graphs are made once and kept (delete_all_material_expressions asserts in 5.8.3).
"""
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Art/Vehicles/Octavia/Body_v2"
EXPORT = SOURCE / "Export"
DEST = "/Game/Vehicles/Octavia/Prototype"
BODY_DEST = "/Game/Vehicles/Octavia/Body_v2"
TEX_DEST = BODY_DEST + "/Textures"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
data = json.loads((EXPORT / "Game/game_parts.json").read_text(encoding="utf-8"))
parts = data["parts"]
# The delivered surfaces and the game's own (dash_labels: scripts/build_game_textures.py).
SURFACES = sorted({p.stem.rsplit("_", 1)[0] for folder in ("textures", "textures_game")
                   for p in (SOURCE / folder).glob("*_basecolor.png")})
MAPS = ("basecolor", "normal", "roughness", "metallic", "emissive")
GLASS = {"glass_clear", "glass_tinted"}
PAINT = "body_paint"
# As scripts/setup_damage_assets.py: darker than the paint so scrapes read at a distance.
BARE_STEEL = (0.09, 0.09, 0.085)
# Dent-able (URallyBodyDeformComponent) and parts that come off (loose pieces need a hull).
DEFORM = {"Chassis", "Door_FL", "Door_FR", "Door_RL", "Door_RR", "Bonnet", "Trunk", "FrontBumper", "RearBumper",
          "Fender_FL", "Fender_FR", "Headlight_L", "Headlight_R", "Taillight_L", "Taillight_R", "Roof", "Windshield",
          "BumperBar_F", "BumperBar_R", "Interior"}
DETACHABLE = {"Door_FL", "Door_FR", "Door_RL", "Door_RR", "Bonnet", "Trunk", "FrontBumper", "RearBumper",
              "Fender_FL", "Fender_FR", "Headlight_L", "Headlight_R", "Taillight_L", "Taillight_R"}


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
        # The game's retuned maps (scripts/build_game_textures.py) where there is one, else the delivered map.
        path = SOURCE / "textures_game" / ("%s_%s.png" % (surface, kind))
        if not path.exists():
            path = SOURCE / "textures" / ("%s_%s.png" % (surface, kind))
        if not path.exists():
            continue
        tex = import_file(path, TEX_DEST, "T_Body_%s_%s" % (surface, kind))
        if kind == "normal":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("flip_green_channel", True)
            tex.set_editor_property("srgb", False)
        elif kind in ("roughness", "metallic"):
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        assets.save_loaded_asset(tex)
        textures[(surface, kind)] = tex
unreal.log("BODYV2 textures %d" % len(textures))


def node(m, kind, x, y):
    return lib.create_material_expression(m, kind, x, y)


def connect(a, pa, b, pb):
    lib.connect_material_expressions(a, pa, b, pb)


def scalar(m, name, value, x, y):
    p = node(m, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", value)
    return p


def constant(m, value, x, y):
    if isinstance(value, tuple):
        c = node(m, unreal.MaterialExpressionConstant3Vector, x, y)
        c.set_editor_property("constant", unreal.LinearColor(*value, 1.0))
    else:
        c = node(m, unreal.MaterialExpressionConstant, x, y)
        c.set_editor_property("r", value)
    return c


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


def lerp(m, damaged, intact, vc, x, y, pd="", pi=""):
    n = node(m, unreal.MaterialExpressionLinearInterpolate, x, y)
    connect(damaged, pd, n, "A")
    connect(intact, pi, n, "B")
    connect(vc, "R", n, "Alpha")
    return n


def new_material(name):
    path = BODY_DEST + "/" + name
    if assets.does_asset_exist(path):
        unreal.log(name + " exists; its graph is kept")
        return unreal.load_asset(path), False
    m = tools.create_asset(name, BODY_DEST, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("two_sided", True)
    return m, True


first = next(s for s in SURFACES if s != PAINT and s not in GLASS)
master, fresh = new_material("M_Body")
if fresh:
    m = master
    vc = node(m, unreal.MaterialExpressionVertexColor, -1500, -700)
    base = sampler(m, "BaseColor", textures[(first, "basecolor")], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1500, -450)
    grimy = binary(m, unreal.MaterialExpressionMultiply, base, vc, -1150, -500, "RGB", "G")
    darkened = binary(m, unreal.MaterialExpressionMultiply, grimy, scalar(m, "DamageDarken", 0.35, -1150, -300), -950, -350)
    lib.connect_material_property(lerp(m, darkened, grimy, vc, -750, -450), "", unreal.MaterialProperty.MP_BASE_COLOR)
    normal = sampler(m, "Normal", textures[(first, "normal")], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1500, -150)
    lib.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    rough = sampler(m, "Roughness", textures[(first, "roughness")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1500, 150)
    lib.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    metal = sampler(m, "Metallic", textures[(first, "metallic")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1500, 450)
    lib.connect_material_property(metal, "R", unreal.MaterialProperty.MP_METALLIC)
    glow = sampler(m, "Emissive", textures[(first, "basecolor")], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1500, 750)
    lit = binary(m, unreal.MaterialExpressionMultiply, glow, scalar(m, "EmissiveStrength", 0.0, -1150, 900), -950, 800, "RGB")
    lib.connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(m)
    assets.save_loaded_asset(m)
    unreal.log("M_Body created")

paint, fresh = new_material("M_Body_Paint")
if fresh:
    m = paint
    vc = node(m, unreal.MaterialExpressionVertexColor, -1500, -700)
    uv1 = node(m, unreal.MaterialExpressionTextureCoordinate, -1700, -450)
    uv1.set_editor_property("coordinate_index", 1)
    livery = sampler(m, "Livery", unreal.load_asset(DEST + "/T_Octavia_Livery"), unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1500, -450)
    connect(uv1, "", livery, "UVs")
    grimy = binary(m, unreal.MaterialExpressionMultiply, livery, vc, -1150, -500, "RGB", "G")
    lib.connect_material_property(lerp(m, constant(m, BARE_STEEL, -950, -650), grimy, vc, -750, -450),
                                  "", unreal.MaterialProperty.MP_BASE_COLOR)
    normal = sampler(m, "Normal", textures[(PAINT, "normal")], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1500, -150)
    lib.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    rough = sampler(m, "Roughness", textures[(PAINT, "roughness")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1500, 150)
    lib.connect_material_property(lerp(m, constant(m, 0.45, -950, 100), rough, vc, -750, 150, "", "R"),
                                  "", unreal.MaterialProperty.MP_ROUGHNESS)
    metal = sampler(m, "Metallic", textures[(PAINT, "metallic")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1500, 450)
    lib.connect_material_property(lerp(m, constant(m, 1.0, -950, 400), metal, vc, -750, 450, "", "R"),
                                  "", unreal.MaterialProperty.MP_METALLIC)
    lib.recompile_material(m)
    assets.save_loaded_asset(m)
    unreal.log("M_Body_Paint created")

dash, fresh = new_material("M_Body_Dash")
if fresh:
    # The instrument cluster (URallyCabinComponent): M_Body's maps, lit by Backlight (the dial lamps, with the
    # headlamps) on the gauge faces; on the warning-light strip (IsWarning 1) each of its eight cells by its own
    # Warn0..Warn7 (OIL, BAT, BRAKE, ABS, left, right, HI, BELT), picked by the cell the UV falls in. The strip's
    # cells start at U 0.5388 of the atlas, 0.05621 apart (textures/gauge_faces_emissive.png).
    m = dash
    m.set_editor_property("two_sided", False)
    base = sampler(m, "BaseColor", textures[("gauge_faces", "basecolor")], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1500, -450)
    lib.connect_material_property(base, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = sampler(m, "Roughness", textures[("gauge_faces", "roughness")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1500, 150)
    lib.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    glow = sampler(m, "Emissive", textures[("gauge_faces", "emissive")], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1500, 750)
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -2600, 1100)
    u = node(m, unreal.MaterialExpressionComponentMask, -2450, 1100)
    for c in ("r", "g", "b", "a"):
        u.set_editor_property(c, c == "r")
    connect(uv, "", u, "")
    shifted = binary(m, unreal.MaterialExpressionSubtract, u, constant(m, 0.5388, -2450, 1200), -2300, 1100)
    scaled = binary(m, unreal.MaterialExpressionDivide, shifted, constant(m, 0.05621, -2300, 1200), -2150, 1100)
    cell = node(m, unreal.MaterialExpressionFloor, -2000, 1100)
    connect(scaled, "", cell, "")
    mask = None
    for i in range(8):
        y = 1300 + i * 120
        offset = binary(m, unreal.MaterialExpressionSubtract, cell, constant(m, float(i), -2000, y), -1850, y)
        far = node(m, unreal.MaterialExpressionAbs, -1700, y)
        connect(offset, "", far, "")
        near = node(m, unreal.MaterialExpressionOneMinus, -1600, y)
        connect(far, "", near, "")
        inside = node(m, unreal.MaterialExpressionSaturate, -1500, y)
        connect(near, "", inside, "")
        term = binary(m, unreal.MaterialExpressionMultiply, inside, scalar(m, "Warn%d" % i, 0.0, -1500, y + 60), -1350, y)
        mask = term if mask is None else binary(m, unreal.MaterialExpressionAdd, mask, term, -1200, y)
    which = node(m, unreal.MaterialExpressionLinearInterpolate, -1050, 1000)
    connect(scalar(m, "Backlight", 0.0, -1200, 950), "", which, "A")
    connect(mask, "", which, "B")
    connect(scalar(m, "IsWarning", 0.0, -1200, 1050), "", which, "Alpha")
    lamps = binary(m, unreal.MaterialExpressionMultiply, glow, which, -900, 800, "RGB")
    lit = binary(m, unreal.MaterialExpressionMultiply, lamps, scalar(m, "EmissiveStrength", 3.0, -900, 950), -750, 800)
    lib.connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(m)
    assets.save_loaded_asset(m)
    unreal.log("M_Body_Dash created")

glass = unreal.load_asset(DEST + "/M_Prototype_Glass_SolidPreview")
instances = {PAINT: paint}
for surface in SURFACES:
    if surface in GLASS:
        instances[surface] = glass
        continue
    if surface == PAINT:
        continue
    path = BODY_DEST + "/MI_Body_" + surface
    mi = unreal.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        "MI_Body_" + surface, BODY_DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    gauge = surface in ("gauge_faces", "gauge_warning")
    lib.set_material_instance_parent(mi, dash if gauge else master)
    for param, kind in (("BaseColor", "basecolor"), ("Normal", "normal"), ("Roughness", "roughness"),
                        ("Metallic", "metallic"), ("Emissive", "emissive")):
        if (surface, kind) in textures:
            lib.set_material_instance_texture_parameter_value(mi, param, textures[(surface, kind)])
    # A lens glows in its own colour over the lamp behind it (the car sets its EmissiveStrength).
    if surface.startswith("lens_") and (surface, "emissive") not in textures:
        lib.set_material_instance_texture_parameter_value(mi, "Emissive", textures[(surface, "basecolor")])
    if gauge:
        lib.set_material_instance_scalar_parameter_value(mi, "IsWarning", 1.0 if surface == "gauge_warning" else 0.0)
    # A smashed lamp goes nearly black; scraped plastic, rubber and trim only darken.
    if not gauge:
        lib.set_material_instance_scalar_parameter_value(mi, "DamageDarken", 0.12 if surface.startswith(("lamp_", "lens_", "reflector")) else 0.35)
    lib.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    instances[surface] = mi

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX False")
problems = []
for part in parts:
    short = part["name"]
    asset = "SM_Octavia_" + short
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    sm = options.static_mesh_import_data
    for key, value in {"combine_meshes": True, "auto_generate_collision": False, "one_convex_hull_per_ucx": True,
                       "build_nanite": False, "convert_scene": True, "convert_scene_unit": True, "force_front_x_axis": False,
                       "transform_vertex_to_absolute": True, "import_uniform_scale": 1.0,
                       "normal_import_method": unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
                       "vertex_color_import_option": unreal.VertexColorImportOption.REPLACE}.items():
        sm.set_editor_property(key, value)
    task = unreal.AssetImportTask()
    task.filename = str(EXPORT / part["file"])
    task.destination_path = DEST
    task.destination_name = asset
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    task.factory = unreal.FbxFactory()
    tools.import_asset_tasks([task])
    mesh = unreal.load_asset(DEST + "/" + asset)
    if not isinstance(mesh, unreal.StaticMesh):
        problems.append(asset + " missing")
        continue
    box = mesh.get_bounding_box()
    size = [box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z]
    expected = part.get("dimensions_cm")
    if expected and any(abs(a - b) > 0.5 for a, b in zip(size, expected)):
        problems.append("%s size %s vs %s" % (asset, [round(v, 1) for v in size], expected))
    # Re-import over the old mesh keeps its slots (and their names where it matched them by index), so the
    # surface comes from each section's imported name; slots no section uses are the old mesh's, left empty.
    used = {mesh_editor.get_lod_material_slot(mesh, 0, s) for s in range(mesh.get_num_sections(0))}
    surfaces = set()
    for index, slot in enumerate(mesh.static_materials):
        if index not in used:
            mesh.set_material(index, None)
            continue
        surface = str(slot.get_editor_property("imported_material_slot_name"))
        surface = surface[2:] if surface.startswith("M_") else surface
        surfaces.add(surface)
        if surface in instances:
            mesh.set_material(index, instances[surface])
        else:
            problems.append("%s slot %s (%s) has no surface" % (asset, slot.material_slot_name, surface))
    expected_surfaces = {s[2:] for s in part["material_slots"]}
    if not surfaces <= expected_surfaces:
        problems.append("%s surfaces %s vs %s" % (asset, sorted(surfaces), sorted(expected_surfaces)))
    elif surfaces != expected_surfaces:
        # The Trunk's plate-lamp lens builds to no section (its faces are degenerate at Unreal's tolerance).
        unreal.log_warning("BODYV2 %s has no section for %s" % (asset, sorted(expected_surfaces - surfaces)))
    if short in DEFORM:
        mesh.set_editor_property("allow_cpu_access", True)
    if short in DETACHABLE and mesh_editor.get_convex_collision_count(mesh) == 0:
        if not mesh_editor.set_convex_decomposition_collisions(mesh, 1, 32, 100000):
            problems.append("convex collision failed for " + asset)
    assets.save_loaded_asset(mesh)
    unreal.log("BODYV2 %s tris %d slots %d hulls %d size %s" % (asset, mesh.get_num_triangles(0), len(mesh.static_materials),
               mesh_editor.get_convex_collision_count(mesh), [round(v, 1) for v in size]))
if problems:
    raise RuntimeError("; ".join(problems))
unreal.log("BODYV2_IMPORT_PASS %d meshes" % len(parts))
