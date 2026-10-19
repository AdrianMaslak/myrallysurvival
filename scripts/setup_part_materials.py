"""Placeholder materials for the running gear (docs/DAMAGE.md, Running gear): until the parts have textures,
each kind gets its own plain colour so they can be told apart under the car - arms black, struts red,
discs and hubs bare steel, driveshafts dark grey.

M_Prototype_Part: Color, Metallic and Roughness, and a bend the car sets per part as it is damaged (a
kink in the middle, both ends where they were): Bend (cm), BendLength (cm from the mesh origin along
BendAxis, the part's length) and BendDir (which way the middle goes), all in the mesh's own space.
The MI_Part_<Kind> instances carry the colours and go on the meshes, so a part taken off keeps its look.
Created once: delete_all_material_expressions asserts in 5.8.3, so an existing graph is left alone.
Run via scripts/Invoke-EditorPython.ps1 -Script scripts/setup_part_materials.py -PassMarker PART_MATERIALS_PASS.
"""
import unreal

DEST = "/Game/Vehicles/Octavia/Prototype"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary

# Kind: (linear colour, metallic, roughness)
KINDS = {
    "Arm": ((0.03, 0.03, 0.035), 0.3, 0.55),
    "Strut": ((0.45, 0.04, 0.03), 0.3, 0.4),
    "Disc": ((0.45, 0.45, 0.47), 0.9, 0.35),
    "Hub": ((0.3, 0.3, 0.31), 0.8, 0.45),
    "Driveshaft": ((0.09, 0.09, 0.1), 0.6, 0.45),
}
MESHES = {}
for corner in ("FL", "FR", "RL", "RR"):
    MESHES[f"Strut_{corner}"] = "Strut"
    MESHES[f"Hub_{corner}"] = "Hub"
    MESHES[f"Disc_{corner}"] = "Disc"
    MESHES[f"LowerArm_{corner}"] = "Arm"
    MESHES[f"Driveshaft_{corner}"] = "Driveshaft"
    if corner[0] == "F":
        MESHES[f"TieRod_{corner}"] = "Arm"
    else:
        MESHES[f"UpperArm_{corner}"] = "Arm"
        MESHES[f"TrailingArm_{corner}"] = "Arm"


def node(material, kind, x, y):
    return lib.create_material_expression(material, kind, x, y)


def rgb(material, source, x, y):
    mask = node(material, unreal.MaterialExpressionComponentMask, x, y)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", True)
    mask.set_editor_property("b", True)
    mask.set_editor_property("a", False)
    lib.connect_material_expressions(source, "", mask, "")
    return mask


def vector_param(material, name, value, x, y):
    p = node(material, unreal.MaterialExpressionVectorParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
    return p


def scalar_param(material, name, value, x, y):
    p = node(material, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", value)
    return p


def build(material):
    colour = vector_param(material, "Color", (0.3, 0.3, 0.3), -600, -300)
    metallic = scalar_param(material, "Metallic", 0.5, -600, -150)
    roughness = scalar_param(material, "Roughness", 0.5, -600, -50)
    lib.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # The bend: this vertex's place along the part (0 at the mesh origin, 1 at BendLength) gives
    # sin(pi t) of the bend, pushed along BendDir in the mesh's space, then turned into world space.
    world = node(material, unreal.MaterialExpressionWorldPosition, -1600, 300)
    world.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    local = node(material, unreal.MaterialExpressionTransformPosition, -1400, 300)
    local.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    lib.connect_material_expressions(world, "", local, "")
    axis = rgb(material, vector_param(material, "BendAxis", (0.0, 1.0, 0.0), -1600, 500), -1400, 500)
    along = node(material, unreal.MaterialExpressionDotProduct, -1200, 350)
    lib.connect_material_expressions(local, "", along, "A")
    lib.connect_material_expressions(axis, "", along, "B")
    length = scalar_param(material, "BendLength", 40.0, -1200, 500)
    share = node(material, unreal.MaterialExpressionDivide, -1000, 350)
    lib.connect_material_expressions(along, "", share, "A")
    lib.connect_material_expressions(length, "", share, "B")
    clamped = node(material, unreal.MaterialExpressionSaturate, -850, 350)
    lib.connect_material_expressions(share, "", clamped, "")
    sine = node(material, unreal.MaterialExpressionSine, -700, 350)
    sine.set_editor_property("period", 2.0)
    lib.connect_material_expressions(clamped, "", sine, "")
    bend = scalar_param(material, "Bend", 0.0, -700, 500)
    amount = node(material, unreal.MaterialExpressionMultiply, -500, 400)
    lib.connect_material_expressions(sine, "", amount, "A")
    lib.connect_material_expressions(bend, "", amount, "B")
    direction = rgb(material, vector_param(material, "BendDir", (-1.0, 0.0, 0.0), -700, 650), -500, 650)
    offset = node(material, unreal.MaterialExpressionMultiply, -300, 500)
    lib.connect_material_expressions(direction, "", offset, "A")
    lib.connect_material_expressions(amount, "", offset, "B")
    to_world = node(material, unreal.MaterialExpressionTransform, -100, 500)
    to_world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    to_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    lib.connect_material_expressions(offset, "", to_world, "")
    lib.connect_material_property(to_world, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


path = f"{DEST}/M_Prototype_Part"
if assets.does_asset_exist(path):
    material = unreal.load_asset(path)
    unreal.log("M_Prototype_Part exists; its graph is kept")
else:
    material = tools.create_asset("M_Prototype_Part", DEST, unreal.Material, unreal.MaterialFactoryNew())
    build(material)
    lib.recompile_material(material)
    assets.save_loaded_asset(material)
    unreal.log("M_Prototype_Part created")

instances = {}
for kind, (colour, metallic, roughness) in KINDS.items():
    mi_path = f"{DEST}/MI_Part_{kind}"
    mi = unreal.load_asset(mi_path) if assets.does_asset_exist(mi_path) else tools.create_asset(
        f"MI_Part_{kind}", DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    lib.set_material_instance_parent(mi, material)
    lib.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*colour, 1.0))
    lib.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
    lib.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
    lib.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    instances[kind] = mi

done = 0
for name, kind in MESHES.items():
    mesh_path = f"{DEST}/SM_Octavia_{name}"
    if not assets.does_asset_exist(mesh_path):
        unreal.log_warning(f"missing {mesh_path}")
        continue
    mesh = unreal.load_asset(mesh_path)
    for index in range(len(mesh.static_materials)):
        mesh.set_material(index, instances[kind])
    assets.save_loaded_asset(mesh)
    done += 1
unreal.log(f"PART_MATERIALS_PASS meshes={done}")
