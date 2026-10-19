"""Placeholder materials for the engine bay (docs/BODY.md, until real engine models with textures arrive):
plain colours per piece, worn by a procedural grime that needs no UVs.

M_Prototype_Engine: Color below SplitHeight (cm, in the mesh's own space) and TopColor above it, blended
over SplitBlend (the engine's cast-iron block under its aluminium head); Metallic and Roughness; Grime
(0..1) darkens and roughens it in blotches where a 3D noise of the mesh's own position rises above 1 - Grime,
so the
pattern rides with the part. MI_Engine_<Kind> instances carry each piece's look and go on its mesh.
The graph is created once and left alone after (delete_all_material_expressions asserts in 5.8.3); the
instances' values and the meshes' materials are applied every run.
Run via scripts/Invoke-EditorPython.ps1 -Script scripts/setup_engine_materials.py -PassMarker ENGINE_MATERIALS_PASS.
"""
import unreal

DEST = "/Game/Vehicles/Octavia/Prototype"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary

CAST_IRON = (0.045, 0.045, 0.05)
# Dull, weathered aluminium; the engine mesh also holds the bay's hoses, tanks and battery, so it stays dark.
DULL_ALLOY = (0.16, 0.16, 0.17)
ALUMINIUM = (0.3, 0.3, 0.31)
BLACK_PLASTIC = (0.018, 0.018, 0.02)
RUSTY = (0.11, 0.055, 0.035)
# Kind: (colour, top colour, split height cm, metallic, roughness, grime), and the meshes it goes on.
KINDS = {
    "Block": (CAST_IRON, DULL_ALLOY, 2.0, 0.5, 0.6, 0.45, ["Engine"]),
    "Plastic": (BLACK_PLASTIC, BLACK_PLASTIC, 0.0, 0.0, 0.45, 0.25, ["EngineCover", "AirFilter"]),
    "Alloy": (ALUMINIUM, ALUMINIUM, 0.0, 0.9, 0.4, 0.35, ["Intake", "Alternator", "ECU", "Intercooler"]),
    "Rusty": (RUSTY, RUSTY, 0.0, 0.4, 0.85, 0.6, ["ExhaustManifold", "Exhaust"]),
    "Turbo": ((0.13, 0.12, 0.11), (0.13, 0.12, 0.11), 0.0, 0.6, 0.6, 0.5, ["Turbo"]),
    "Painted": ((0.035, 0.035, 0.04), (0.035, 0.035, 0.04), 0.0, 0.3, 0.55, 0.4, ["Starter", "Radiator"]),
}


def node(material, kind, x, y):
    return lib.create_material_expression(material, kind, x, y)


def scalar_param(material, name, value, x, y):
    p = node(material, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", value)
    return p


def vector_param(material, name, value, x, y):
    p = node(material, unreal.MaterialExpressionVectorParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
    return p


def mask(material, source, channels, x, y):
    m = node(material, unreal.MaterialExpressionComponentMask, x, y)
    for c in "rgba":
        m.set_editor_property(c, c in channels)
    lib.connect_material_expressions(source, "", m, "")
    return m


def binary(material, kind, a, b, x, y):
    n = node(material, kind, x, y)
    lib.connect_material_expressions(a, "", n, "A")
    lib.connect_material_expressions(b, "", n, "B")
    return n


def lerp(material, a, b, alpha, x, y):
    n = node(material, unreal.MaterialExpressionLinearInterpolate, x, y)
    lib.connect_material_expressions(a, "", n, "A")
    lib.connect_material_expressions(b, "", n, "B")
    lib.connect_material_expressions(alpha, "", n, "Alpha")
    return n


def build(m):
    # The mesh's own position (cm), so the look rides with the part.
    world = node(m, unreal.MaterialExpressionWorldPosition, -2000, 0)
    world.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    local = node(m, unreal.MaterialExpressionTransformPosition, -1800, 0)
    local.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    lib.connect_material_expressions(world, "", local, "")

    # Block below, head above.
    height = mask(m, local, "b", -1600, -300)
    split = scalar_param(m, "SplitHeight", 0.0, -1600, -200)
    blend = scalar_param(m, "SplitBlend", 2.0, -1600, -100)
    above = binary(m, unreal.MaterialExpressionSubtract, height, split, -1400, -300)
    share = binary(m, unreal.MaterialExpressionDivide, above, blend, -1250, -300)
    top = node(m, unreal.MaterialExpressionSaturate, -1100, -300)
    lib.connect_material_expressions(share, "", top, "")
    colour = vector_param(m, "Color", CAST_IRON, -1100, -500)
    top_colour = vector_param(m, "TopColor", ALUMINIUM, -1100, -650)
    base = lerp(m, colour, top_colour, top, -850, -500)

    # Grime in blotches: a 3D noise of the position (the node's own scale, per cm; scaling the position fed
    # in does not do the same); where it rises above 1 - Grime the piece is dirty, fully so GrimeContrast
    # later, and clean metal shows between.
    noise = node(m, unreal.MaterialExpressionNoise, -1400, 200)
    noise.set_editor_property("scale", 0.05)
    noise.set_editor_property("levels", 2)
    noise.set_editor_property("output_min", 0.0)
    noise.set_editor_property("output_max", 1.0)
    lib.connect_material_expressions(local, "", noise, "Position")
    grime = scalar_param(m, "Grime", 0.5, -1400, 400)
    threshold = node(m, unreal.MaterialExpressionOneMinus, -1250, 400)
    lib.connect_material_expressions(grime, "", threshold, "")
    dirty = binary(m, unreal.MaterialExpressionSubtract, noise, threshold, -1150, 250)
    gain = scalar_param(m, "GrimeContrast", 4.0, -1150, 400)
    pushed = binary(m, unreal.MaterialExpressionMultiply, dirty, gain, -950, 250)
    grime_mask = node(m, unreal.MaterialExpressionSaturate, -800, 250)
    lib.connect_material_expressions(pushed, "", grime_mask, "")
    grime_colour = vector_param(m, "GrimeColor", (0.02, 0.016, 0.012), -800, 50)
    final_colour = lerp(m, base, grime_colour, grime_mask, -550, -300)
    lib.connect_material_property(final_colour, "", unreal.MaterialProperty.MP_BASE_COLOR)

    metallic = scalar_param(m, "Metallic", 0.5, -800, 500)
    zero = node(m, unreal.MaterialExpressionConstant, -800, 600)
    final_metallic = lerp(m, metallic, zero, grime_mask, -550, 500)
    lib.connect_material_property(final_metallic, "", unreal.MaterialProperty.MP_METALLIC)

    roughness = scalar_param(m, "Roughness", 0.5, -800, 700)
    rough_grime = node(m, unreal.MaterialExpressionConstant, -800, 800)
    rough_grime.set_editor_property("r", 0.95)
    final_roughness = lerp(m, roughness, rough_grime, grime_mask, -550, 700)
    lib.connect_material_property(final_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)


path = f"{DEST}/M_Prototype_Engine"
if assets.does_asset_exist(path):
    material = unreal.load_asset(path)
    unreal.log("M_Prototype_Engine exists; its graph is kept")
else:
    material = tools.create_asset("M_Prototype_Engine", DEST, unreal.Material, unreal.MaterialFactoryNew())
    build(material)
    lib.recompile_material(material)
    assets.save_loaded_asset(material)
    unreal.log("M_Prototype_Engine created")

done = 0
for kind, (colour, top_colour, split, metallic, roughness, grime, meshes) in KINDS.items():
    mi_path = f"{DEST}/MI_Engine_{kind}"
    mi = unreal.load_asset(mi_path) if assets.does_asset_exist(mi_path) else tools.create_asset(
        f"MI_Engine_{kind}", DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    lib.set_material_instance_parent(mi, material)
    lib.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*colour, 1.0))
    lib.set_material_instance_vector_parameter_value(mi, "TopColor", unreal.LinearColor(*top_colour, 1.0))
    lib.set_material_instance_scalar_parameter_value(mi, "SplitHeight", split)
    lib.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
    lib.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
    lib.set_material_instance_scalar_parameter_value(mi, "Grime", grime)
    # Soft-edged oily dark patches rather than crisp brown ones (which read as camouflage).
    lib.set_material_instance_scalar_parameter_value(mi, "GrimeContrast", 1.8)
    lib.set_material_instance_vector_parameter_value(mi, "GrimeColor", unreal.LinearColor(0.012, 0.011, 0.01, 1.0))
    lib.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    for name in meshes:
        mesh = unreal.load_asset(f"{DEST}/SM_Octavia_{name}")
        if not mesh:
            unreal.log_warning(f"missing SM_Octavia_{name}")
            continue
        if any(m.material_interface != mi for m in mesh.static_materials):
            for index in range(len(mesh.static_materials)):
                mesh.set_material(index, mi)
            assets.save_loaded_asset(mesh)
        done += 1
unreal.log(f"ENGINE_MATERIALS_PASS meshes={done}")
