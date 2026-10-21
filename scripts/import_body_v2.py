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
- M_Body_PaintMud on the paint where textures_game has the mud mask: M_Body_Paint plus mud the car sets.
- M_Body_Mirror for the mirrors' glass (surface mirror_glass, split from the door and interior plastic at export): the
  picture URallyCabinComponent's scene captures draw into a render target ("Mirror"), U flipped as a mirror shows it.
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

# The windscreen: tinted see-through glass with rain on it, the drops' mask and normal (textures_game/windscreen_drops_*)
# showing as far as Wet (0 dry .. 1 streaming), which URallyCabinComponent raises in the rain and the wipers clear;
# hit, it cracks (textures_game/windscreen_cracks_mask, spreading as R drops) and crazes.
drops = {}
for kind in ("mask", "normal"):
    path = SOURCE / "textures_game" / ("windscreen_drops_%s.png" % kind)
    if path.exists():
        tex = import_file(path, TEX_DEST, "T_Windscreen_Drops_" + kind)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP if kind == "normal"
                                else unreal.TextureCompressionSettings.TC_MASKS)
        if kind == "normal":
            tex.set_editor_property("flip_green_channel", True)
        assets.save_loaded_asset(tex)
        drops[kind] = tex
windscreen = None
crack_path = SOURCE / "textures_game" / "windscreen_cracks_mask.png"
if len(drops) == 2 and crack_path.exists():
    cracks_tex = import_file(crack_path, TEX_DEST, "T_Windscreen_Cracks")
    cracks_tex.set_editor_property("srgb", False)
    cracks_tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    assets.save_loaded_asset(cracks_tex)
    # _v2: the first M_Windscreen had no cracks, and a material's graph is made once and kept.
    windscreen, fresh = new_material("M_Windscreen_v2")
    if fresh:
        m = windscreen
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
        vc = node(m, unreal.MaterialExpressionVertexColor, -1700, -700)
        mask = sampler(m, "DropsMask", drops["mask"], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1700, 300)
        # How much of each drop shows: none dry, the biggest first as it starts, all of them streaming.
        wet = scalar(m, "Wet", 0.0, -1700, 550)
        lifted = binary(m, unreal.MaterialExpressionAdd, mask, wet, -1450, 350, "R")
        less = binary(m, unreal.MaterialExpressionSubtract, lifted, constant(m, 1.0, -1450, 500), -1300, 350)
        gain = binary(m, unreal.MaterialExpressionMultiply, less, constant(m, 3.0, -1300, 500), -1150, 350)
        drop = node(m, unreal.MaterialExpressionSaturate, -1000, 350)
        connect(gain, "", drop, "")
        # Cracks: as the glass is hit (R dropping), the lines of the crack map show, the earliest first; past
        # half damage it is crazed all over (pale, nearly opaque).
        cracks = sampler(m, "CracksMask", cracks_tex, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1700, -1100)
        damage = node(m, unreal.MaterialExpressionOneMinus, -1500, -850)
        connect(vc, "R", damage, "")
        reached = binary(m, unreal.MaterialExpressionAdd, cracks,
                         binary(m, unreal.MaterialExpressionMultiply, damage, constant(m, 2.5, -1500, -750), -1350, -800),
                         -1200, -1000, "R")
        over = binary(m, unreal.MaterialExpressionSubtract, reached, constant(m, 1.0, -1200, -900), -1050, -1000)
        sharp = binary(m, unreal.MaterialExpressionMultiply, over, constant(m, 8.0, -1050, -900), -900, -1000)
        line = node(m, unreal.MaterialExpressionSaturate, -750, -1000)
        connect(sharp, "", line, "")
        tint = binary(m, unreal.MaterialExpressionAdd, constant(m, (0.02, 0.03, 0.035), -1150, -500),
                      binary(m, unreal.MaterialExpressionMultiply, drop, constant(m, (0.08, 0.09, 0.1), -1150, -350), -1000, -400), -850, -450)
        crazed = lerp(m, constant(m, (0.42, 0.45, 0.46), -850, -650), tint, vc, -650, -500)
        cracked = node(m, unreal.MaterialExpressionLinearInterpolate, -450, -600)
        connect(crazed, "", cracked, "A")
        connect(constant(m, (0.75, 0.78, 0.8), -650, -750), "", cracked, "B")
        connect(line, "", cracked, "Alpha")
        lib.connect_material_property(cracked, "", unreal.MaterialProperty.MP_BASE_COLOR)
        clear = binary(m, unreal.MaterialExpressionAdd, constant(m, 0.18, -1000, 150), binary(m, unreal.MaterialExpressionMultiply,
                       drop, constant(m, 0.3, -1000, 250), -850, 200), -700, 150)
        hazed = lerp(m, constant(m, 0.85, -700, 50), clear, vc, -550, 100)
        seen = node(m, unreal.MaterialExpressionLinearInterpolate, -400, 100)
        connect(hazed, "", seen, "A")
        connect(constant(m, 0.9, -550, 0), "", seen, "B")
        connect(line, "", seen, "Alpha")
        lib.connect_material_property(seen, "", unreal.MaterialProperty.MP_OPACITY)
        lib.connect_material_property(constant(m, 0.05, -350, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
        lib.connect_material_property(constant(m, 0.9, -350, 400), "", unreal.MaterialProperty.MP_SPECULAR)
        normal = sampler(m, "DropsNormal", drops["normal"], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1700, 800)
        flat = constant(m, (0.0, 0.0, 1.0), -1450, 950)
        bent = node(m, unreal.MaterialExpressionLinearInterpolate, -650, 850)
        connect(flat, "", bent, "A")
        connect(normal, "RGB", bent, "B")
        connect(drop, "", bent, "Alpha")
        lib.connect_material_property(bent, "", unreal.MaterialProperty.MP_NORMAL)
        lib.recompile_material(m)
        assets.save_loaded_asset(m)
        unreal.log("M_Windscreen_v2 created")

# M_Body_PaintMud: M_Body_Paint with mud on it. Mud (0..1, URallyCabinComponent) climbs the car from the ground: the
# mud line is 15 + 55 * Mud cm above CarOrigin (the ground under the car's middle) along CarUp, softening over 30 cm,
# broken up by textures_game/mud_splatter_mask (splashes stick first), dark brown and matte. Tuned in Blender renders.
mud_path = SOURCE / "textures_game" / "mud_splatter_mask.png"
paint_mud = None
if mud_path.exists():
    mud_tex = import_file(mud_path, TEX_DEST, "T_Body_MudSplatter")
    mud_tex.set_editor_property("srgb", False)
    mud_tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    assets.save_loaded_asset(mud_tex)
    paint_mud, fresh = new_material("M_Body_PaintMud")
    if fresh:
        m = paint_mud
        vc = node(m, unreal.MaterialExpressionVertexColor, -1700, -700)
        uv1 = node(m, unreal.MaterialExpressionTextureCoordinate, -1900, -450)
        uv1.set_editor_property("coordinate_index", 1)
        livery = sampler(m, "Livery", unreal.load_asset(DEST + "/T_Octavia_Livery"), unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1700, -450)
        connect(uv1, "", livery, "UVs")
        grimy = binary(m, unreal.MaterialExpressionMultiply, livery, vc, -1350, -500, "RGB", "G")
        # PaintTint (white as painted): RallyPaint in the console tries another colour over the livery.
        tint = node(m, unreal.MaterialExpressionVectorParameter, -1500, -300)
        tint.set_editor_property("parameter_name", "PaintTint")
        tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
        tinted = binary(m, unreal.MaterialExpressionMultiply, grimy, tint, -1200, -400)
        painted = lerp(m, constant(m, BARE_STEEL, -1150, -650), tinted, vc, -950, -500)
        rough = sampler(m, "Roughness", textures[(PAINT, "roughness")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1700, 150)
        paint_rough = lerp(m, constant(m, 0.45, -1150, 100), rough, vc, -950, 150, "", "R")
        metal = sampler(m, "Metallic", textures[(PAINT, "metallic")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1700, 450)
        paint_metal = lerp(m, constant(m, 1.0, -1150, 400), metal, vc, -950, 450, "", "R")
        normal = sampler(m, "Normal", textures[(PAINT, "normal")], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1700, -150)
        lib.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
        # Height above the ground under the car's middle.
        world = node(m, unreal.MaterialExpressionWorldPosition, -2300, 900)
        world.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
        origin = node(m, unreal.MaterialExpressionVectorParameter, -2300, 1000)
        origin.set_editor_property("parameter_name", "CarOrigin")
        origin.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        up = node(m, unreal.MaterialExpressionVectorParameter, -2300, 1150)
        up.set_editor_property("parameter_name", "CarUp")
        up.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
        rel = binary(m, unreal.MaterialExpressionSubtract, world, origin, -2100, 950)
        height = binary(m, unreal.MaterialExpressionDotProduct, rel, up, -1950, 1000)
        amount_param = scalar(m, "Mud", 0.0, -2300, 1300)
        line = binary(m, unreal.MaterialExpressionAdd, constant(m, 15.0, -2100, 1350),
                      binary(m, unreal.MaterialExpressionMultiply, amount_param, constant(m, 55.0, -2100, 1450), -1950, 1400), -1800, 1350)
        below = binary(m, unreal.MaterialExpressionSubtract, line, height, -1650, 1100)
        soft = binary(m, unreal.MaterialExpressionDivide, below, constant(m, 30.0, -1650, 1200), -1500, 1100)
        rise = node(m, unreal.MaterialExpressionSaturate, -1350, 1100)
        connect(soft, "", rise, "")
        splat = sampler(m, "MudMask", mud_tex, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1700, 1600)
        dry = node(m, unreal.MaterialExpressionOneMinus, -1450, 1600)
        connect(splat, "R", dry, "")
        stick = binary(m, unreal.MaterialExpressionSubtract,
                       binary(m, unreal.MaterialExpressionMultiply, rise, constant(m, 1.5, -1350, 1250), -1200, 1150),
                       binary(m, unreal.MaterialExpressionMultiply, dry, constant(m, 1.1, -1350, 1700), -1200, 1650), -1050, 1300)
        stuck = node(m, unreal.MaterialExpressionSaturate, -900, 1300)
        connect(stick, "", stuck, "")
        enough = binary(m, unreal.MaterialExpressionMultiply, amount_param, constant(m, 3.0, -1200, 1450), -1050, 1450)
        some = node(m, unreal.MaterialExpressionSaturate, -900, 1450)
        connect(enough, "", some, "")
        caked = binary(m, unreal.MaterialExpressionMultiply, stuck, some, -750, 1350)
        shade = binary(m, unreal.MaterialExpressionAdd, constant(m, 0.7, -1200, 1850),
                       binary(m, unreal.MaterialExpressionMultiply, splat, constant(m, 0.6, -1350, 1900), -1200, 1950, "R"), -1050, 1850)
        mud_colour = binary(m, unreal.MaterialExpressionMultiply, constant(m, (0.08, 0.06, 0.04), -1050, 2000), shade, -900, 1900)
        for prop, a, b, y in ((unreal.MaterialProperty.MP_BASE_COLOR, painted, mud_colour, -500),
                              (unreal.MaterialProperty.MP_ROUGHNESS, paint_rough, constant(m, 0.95, -750, 200), 150),
                              (unreal.MaterialProperty.MP_METALLIC, paint_metal, constant(m, 0.0, -750, 500), 450)):
            mix = node(m, unreal.MaterialExpressionLinearInterpolate, -550, y)
            connect(a, "", mix, "A")
            connect(b, "", mix, "B")
            connect(caked, "", mix, "Alpha")
            lib.connect_material_property(mix, "", prop)
        lib.recompile_material(m)
        assets.save_loaded_asset(m)
        unreal.log("M_Body_PaintMud created")

# M_Body_Lens: the lamps' lenses as see-through glass, so the reflector and bulbs show behind them, with little shine
# (roughness from the map, specular 0.35); Opacity per lens (clear 0.12, red and amber tinted glass, 0.42 and 0.38); a smashed lens (R dropping)
# goes dark and nearly opaque; the emissive map times EmissiveStrength when its lamp is lit (URallyCabinComponent).
LENS_OPACITY = {"lens_clear": 0.12, "lens_red": 0.42, "lens_amber": 0.38}
lens_master, fresh = new_material("M_Body_Lens")
if fresh:
    m = lens_master
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    vc = node(m, unreal.MaterialExpressionVertexColor, -1500, -700)
    base = sampler(m, "BaseColor", textures[("lens_clear", "basecolor")], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1500, -450)
    lib.connect_material_property(lerp(m, constant(m, (0.07, 0.07, 0.07), -1150, -600), base, vc, -950, -450, "", "RGB"),
                                  "", unreal.MaterialProperty.MP_BASE_COLOR)
    opacity = scalar(m, "Opacity", 0.12, -1150, -150)
    lib.connect_material_property(lerp(m, constant(m, 0.85, -1150, -250), opacity, vc, -950, -200),
                                  "", unreal.MaterialProperty.MP_OPACITY)
    rough = sampler(m, "Roughness", textures[("lens_clear", "roughness")], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1500, 150)
    lib.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(constant(m, 0.35, -950, 300), "", unreal.MaterialProperty.MP_SPECULAR)
    glow = sampler(m, "Emissive", textures[("lens_clear", "basecolor")], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1500, 750)
    lit = binary(m, unreal.MaterialExpressionMultiply, glow, scalar(m, "EmissiveStrength", 0.0, -1150, 900), -950, 800, "RGB")
    lib.connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(m)
    assets.save_loaded_asset(m)
    unreal.log("M_Body_Lens created")

# M_Body_Mirror: the glass shows the render target "Mirror" (a scene capture looking back from the glass, made in the
# game by URallyCabinComponent) with U flipped, as a mirror shows the world, lit by itself (emissive) so the picture is not
# lit twice; a smashed mirror (R dropping) goes dark. The glass's UV runs 0..1 across it, U to the car's right
# (scripts/export_body_v2.py, split_mirror_glass). Before the game sets a target it shows a grey.
mirror_master, fresh = new_material("M_Body_Mirror")
if fresh:
    m = mirror_master
    vc = node(m, unreal.MaterialExpressionVertexColor, -1500, -700)
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -1900, 0)
    u = node(m, unreal.MaterialExpressionComponentMask, -1700, -50)
    v = node(m, unreal.MaterialExpressionComponentMask, -1700, 80)
    for c in ("r", "g", "b", "a"):
        u.set_editor_property(c, c == "r")
        v.set_editor_property(c, c == "g")
    connect(uv, "", u, "")
    connect(uv, "", v, "")
    flipped = node(m, unreal.MaterialExpressionOneMinus, -1500, -50)
    connect(u, "", flipped, "")
    uvs = node(m, unreal.MaterialExpressionAppendVector, -1300, 0)
    connect(flipped, "", uvs, "A")
    connect(v, "", uvs, "B")
    picture = sampler(m, "Mirror", unreal.load_asset("/Engine/EngineResources/GrayTexture"), unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1100, 0)
    connect(uvs, "", picture, "UVs")
    seen = binary(m, unreal.MaterialExpressionMultiply, picture, vc, -800, 0, "RGB", "R")
    lit = binary(m, unreal.MaterialExpressionMultiply, seen, scalar(m, "Brightness", 0.9, -800, 150), -550, 0)
    lib.connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.connect_material_property(constant(m, (0.01, 0.01, 0.012), -550, -300), "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(constant(m, 0.05, -550, -150), "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(m)
    assets.save_loaded_asset(m)
    unreal.log("M_Body_Mirror created")

glass = unreal.load_asset(DEST + "/M_Prototype_Glass_SolidPreview")
instances = {PAINT: paint_mud or paint}
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
    lens = surface in LENS_OPACITY
    lib.set_material_instance_parent(mi, dash if gauge else lens_master if lens else master)
    for param, kind in (("BaseColor", "basecolor"), ("Normal", "normal"), ("Roughness", "roughness"),
                        ("Metallic", "metallic"), ("Emissive", "emissive")):
        if (surface, kind) in textures:
            lib.set_material_instance_texture_parameter_value(mi, param, textures[(surface, kind)])
    # A lens glows in its own colour over the lamp behind it (the car sets its EmissiveStrength).
    if surface.startswith("lens_") and (surface, "emissive") not in textures:
        lib.set_material_instance_texture_parameter_value(mi, "Emissive", textures[(surface, "basecolor")])
    if gauge:
        lib.set_material_instance_scalar_parameter_value(mi, "IsWarning", 1.0 if surface == "gauge_warning" else 0.0)
    if lens:
        lib.set_material_instance_scalar_parameter_value(mi, "Opacity", LENS_OPACITY[surface])
    # A smashed lamp goes nearly black; scraped plastic, rubber and trim only darken.
    if not gauge and not lens:
        lib.set_material_instance_scalar_parameter_value(mi, "DamageDarken", 0.12 if surface.startswith(("lamp_", "lens_", "reflector")) else 0.35)
    lib.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    instances[surface] = mi

mirror_path = BODY_DEST + "/MI_Body_mirror_glass"
mirror_mi = unreal.load_asset(mirror_path) if assets.does_asset_exist(mirror_path) else tools.create_asset(
    "MI_Body_mirror_glass", BODY_DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
lib.set_material_instance_parent(mirror_mi, mirror_master)
lib.update_material_instance(mirror_mi)
assets.save_loaded_asset(mirror_mi)
instances["mirror_glass"] = mirror_mi

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
        if short == "Windshield" and surface == "glass_clear" and windscreen:
            mesh.set_material(index, windscreen)
        elif surface in instances:
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
