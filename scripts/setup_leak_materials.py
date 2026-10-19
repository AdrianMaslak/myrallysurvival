"""The puddle decal for leaks (docs/DAMAGE.md, "Leaks"): M_Puddle, a deferred decal projected onto the ground
under a leaking car. A round spot with a ragged edge (a 3D noise of the world position, offset by Seed per
puddle), Color and Roughness per liquid (oil black and glossy, coolant green, fuel a faint wet patch),
Opacity for how much it covers and for fading as it dries (ARallyPuddleField).
The graph is created once and left alone after (delete_all_material_expressions asserts in 5.8.3).
Run via scripts/Invoke-EditorPython.ps1 -Script scripts/setup_leak_materials.py -PassMarker LEAK_MATERIALS_PASS.
"""
import unreal

DEST = "/Game/Vehicles/Octavia/Prototype"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary


def node(material, kind, x, y):
    return lib.create_material_expression(material, kind, x, y)


def scalar_param(material, name, value, x, y):
    p = node(material, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", value)
    return p


def binary(material, kind, a, b, x, y):
    n = node(material, kind, x, y)
    lib.connect_material_expressions(a, "", n, "A")
    lib.connect_material_expressions(b, "", n, "B")
    return n


path = f"{DEST}/M_Puddle"
if assets.does_asset_exist(path):
    unreal.log("M_Puddle exists; its graph is kept")
else:
    m = tools.create_asset("M_Puddle", DEST, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # Distance from the middle of the decal, 0 there and 1 at its edge.
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -1600, 0)
    middle = node(m, unreal.MaterialExpressionConstant2Vector, -1600, 120)
    middle.set_editor_property("r", 0.5)
    middle.set_editor_property("g", 0.5)
    distance = binary(m, unreal.MaterialExpressionDistance, uv, middle, -1400, 50)
    two = node(m, unreal.MaterialExpressionConstant, -1400, 180)
    two.set_editor_property("r", 2.0)
    radial = binary(m, unreal.MaterialExpressionMultiply, distance, two, -1250, 50)
    # A ragged edge: the noise pulls it in here and there.
    world = node(m, unreal.MaterialExpressionWorldPosition, -1600, 350)
    seed = scalar_param(m, "Seed", 0.0, -1600, 480)
    shifted = binary(m, unreal.MaterialExpressionAdd, world, seed, -1400, 400)
    noise = node(m, unreal.MaterialExpressionNoise, -1250, 400)
    noise.set_editor_property("scale", 0.08)
    noise.set_editor_property("levels", 2)
    noise.set_editor_property("output_min", 0.0)
    noise.set_editor_property("output_max", 1.0)
    lib.connect_material_expressions(shifted, "", noise, "Position")
    ragged = node(m, unreal.MaterialExpressionConstant, -1250, 550)
    ragged.set_editor_property("r", 0.35)
    pull = binary(m, unreal.MaterialExpressionMultiply, noise, ragged, -1050, 400)
    reach = binary(m, unreal.MaterialExpressionAdd, radial, pull, -900, 200)
    inside = node(m, unreal.MaterialExpressionOneMinus, -750, 200)
    lib.connect_material_expressions(reach, "", inside, "")
    soft = node(m, unreal.MaterialExpressionConstant, -750, 320)
    soft.set_editor_property("r", 5.0)
    edge_raw = binary(m, unreal.MaterialExpressionMultiply, inside, soft, -600, 200)
    edge = node(m, unreal.MaterialExpressionSaturate, -450, 200)
    lib.connect_material_expressions(edge_raw, "", edge, "")
    opacity = scalar_param(m, "Opacity", 0.9, -450, 350)
    final_opacity = binary(m, unreal.MaterialExpressionMultiply, edge, opacity, -250, 250)
    lib.connect_material_property(final_opacity, "", unreal.MaterialProperty.MP_OPACITY)

    colour = node(m, unreal.MaterialExpressionVectorParameter, -450, -250)
    colour.set_editor_property("parameter_name", "Color")
    colour.set_editor_property("default_value", unreal.LinearColor(0.008, 0.006, 0.004, 1.0))
    lib.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = scalar_param(m, "Roughness", 0.05, -450, -80)
    lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    metallic = node(m, unreal.MaterialExpressionConstant, -450, 20)
    lib.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    lib.recompile_material(m)
    assets.save_loaded_asset(m)
    unreal.log("M_Puddle created")
unreal.log("LEAK_MATERIALS_PASS")
