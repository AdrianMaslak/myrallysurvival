"""Materials for the boot's places (docs/BOOT.md): M_Prototype_Ghost, a see-through outline (unlit Color times
Strength, Opacity) showing where a kit item belongs while its place is empty; MI_Part_Strap, orange webbing
(an instance of M_Prototype_Part) for the straps holding what is in the boot.
The ghost's graph is created once and left alone after (delete_all_material_expressions asserts in 5.8.3);
its settings and the strap's colour are applied every run.
Run via scripts/Invoke-EditorPython.ps1 -Script scripts/setup_boot_materials.py -PassMarker BOOT_MATERIALS_PASS.
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


ghost_path = f"{DEST}/M_Prototype_Ghost"
if assets.does_asset_exist(ghost_path):
    ghost = unreal.load_asset(ghost_path)
    unreal.log("M_Prototype_Ghost exists; its graph is kept")
else:
    ghost = tools.create_asset("M_Prototype_Ghost", DEST, unreal.Material, unreal.MaterialFactoryNew())
    ghost.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    ghost.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    colour = node(ghost, unreal.MaterialExpressionVectorParameter, -600, 0)
    colour.set_editor_property("parameter_name", "Color")
    colour.set_editor_property("default_value", unreal.LinearColor(1.0, 0.55, 0.1, 1.0))
    strength = scalar_param(ghost, "Strength", 1.0, -600, 200)
    lit = node(ghost, unreal.MaterialExpressionMultiply, -300, 100)
    lib.connect_material_expressions(colour, "", lit, "A")
    lib.connect_material_expressions(strength, "", lit, "B")
    lib.connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = scalar_param(ghost, "Opacity", 0.2, -600, 350)
    lib.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.log("M_Prototype_Ghost created")
# Drawn through whatever is in front, so the spare wheel's place shows under the load floor.
ghost.set_editor_property("disable_depth_test", True)
lib.recompile_material(ghost)
assets.save_loaded_asset(ghost)

strap_path = f"{DEST}/MI_Part_Strap"
part = unreal.load_asset(f"{DEST}/M_Prototype_Part")
if not part:
    raise RuntimeError("M_Prototype_Part is missing: run setup_part_materials.py first")
strap = unreal.load_asset(strap_path) if assets.does_asset_exist(strap_path) else tools.create_asset(
    "MI_Part_Strap", DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
lib.set_material_instance_parent(strap, part)
# Dark orange webbing: seen, without shouting over the parts it holds.
lib.set_material_instance_vector_parameter_value(strap, "Color", unreal.LinearColor(0.22, 0.05, 0.01, 1.0))
lib.set_material_instance_scalar_parameter_value(strap, "Metallic", 0.0)
lib.set_material_instance_scalar_parameter_value(strap, "Roughness", 0.9)
lib.update_material_instance(strap)
assets.save_loaded_asset(strap)
unreal.log("BOOT_MATERIALS_PASS")
