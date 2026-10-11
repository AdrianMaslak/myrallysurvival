"""Create the flat preview materials the Octavia meshes reference.
The first (crashed) import never saved them, so the meshes render with the default material.
Colours and metalness match scripts/prepare_octavia_prototype.py (linear RGB).
Glass is tinted translucent and two-sided so the first-person view sees out; the asset keeps its
original "SolidPreview" name because the meshes reference it.
Run via scripts/Invoke-EditorPython.ps1; existing opaque materials are left untouched.
"""
import unreal

DEST = "/Game/Vehicles/Octavia/Prototype"
MATERIALS = {
    "M_Placeholder_Rubber": ((0.025, 0.03, 0.035), 0.0, 0.85),
    "M_Placeholder_Rim": ((0.38, 0.4, 0.42), 0.65, 0.4),
    "M_Placeholder_Hub": ((0.07, 0.08, 0.09), 0.3, 0.6),
    "M_Prototype_Paint": ((0.39, 0.43, 0.40), 0.25, 0.45),
    "M_Prototype_Glass_SolidPreview": ((0.02, 0.03, 0.035), 0.0, 0.05),
    "M_Prototype_Interior": ((0.065, 0.07, 0.075), 0.0, 0.8),
    "M_Prototype_Mechanical": ((0.17, 0.19, 0.21), 0.5, 0.55),
    "M_Prototype_Headlamp": ((0.6, 0.65, 0.65), 0.2, 0.2),
    "M_Prototype_Taillamp": ((0.35, 0.025, 0.018), 0.0, 0.35),
}

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
GLASS = "M_Prototype_Glass_SolidPreview"
GLASS_OPACITY = 0.22
created = []
for name, (color, metallic, roughness) in MATERIALS.items():
    path = f"{DEST}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.load_asset(path)
        # Convert only glass that is still the old opaque preview. Existing nodes are kept and retinted:
        # delete_all_material_expressions asserts (!IsRooted) in 5.8.3.
        if name != GLASS or material.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_TRANSLUCENT:
            continue
        base = lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
        base.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        material.set_editor_property("two_sided", True)
        opacity = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 350)
        opacity.set_editor_property("r", GLASS_OPACITY)
        lib.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
        lib.recompile_material(material)
        if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
            raise RuntimeError("Could not save " + path)
        created.append(name)
        continue
    else:
        material = tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Could not create " + path)
    if name == GLASS:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        material.set_editor_property("two_sided", True)
        opacity = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 350)
        opacity.set_editor_property("r", GLASS_OPACITY)
        lib.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    base = lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
    base.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
    lib.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for value, prop, y in ((metallic, unreal.MaterialProperty.MP_METALLIC, 150),
                           (roughness, unreal.MaterialProperty.MP_ROUGHNESS, 250)):
        constant = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -400, y)
        constant.set_editor_property("r", value)
        lib.connect_material_property(constant, "", prop)
    lib.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
        raise RuntimeError("Could not save " + path)
    created.append(name)

# Meshes loaded before the materials existed keep empty slots; slot names equal material names.
missing = []
for asset in unreal.EditorAssetLibrary.list_assets(DEST, recursive=False):
    mesh = unreal.load_asset(asset)
    if not isinstance(mesh, unreal.StaticMesh):
        continue
    changed = False
    for index, slot in enumerate(mesh.static_materials):
        if slot.material_interface is not None:
            continue
        name = str(slot.material_slot_name)
        if name in MATERIALS:
            mesh.set_material(index, unreal.load_asset(f"{DEST}/{name}"))
            changed = True
        else:
            missing.append(f"{mesh.get_name()}:{name}")
    if changed and not unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
        raise RuntimeError("Could not save " + mesh.get_path_name())
if missing:
    raise RuntimeError("Unresolved material slots: " + ", ".join(missing))
unreal.log(f"OCTAVIA_MATERIALS_PASS: created or converted {created}, all mesh slots resolved.")
