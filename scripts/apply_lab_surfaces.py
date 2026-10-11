"""Give the gravel lab real surfaces: gravel road (mu 0.65) and loose off-road ground (mu 0.5).
Physical materials are RallyPhysicalMaterial: friction, looseness and rolling resistance for the tyre model.
Values and sources: docs/VEHICLE-PHYSICS.md. Wheel grip reads the hit shape's physical-material friction.
Run via scripts/Invoke-EditorPython.ps1. Re-running updates friction values and reassigns surfaces.
"""
import unreal

MAP = "/Game/Maps/L_GravelLab"
PHYS = "/Game/Physics/Surfaces"
# Plain physical materials from the first surface pass; replaced by RallyPhysicalMaterial assets.
OBSOLETE = ("/Game/Physics/PM_Gravel", "/Game/Physics/PM_LooseGround")
MATS = "/Game/Maps/LabMaterials"
# name: (friction, looseness, rolling resistance, linear base colour, actor label prefixes)
SURFACES = {
    "Gravel": (0.65, 1.0, 0.03, (0.30, 0.24, 0.17), ("AccelerationStraight", "Corner_", "ExitStraight", "Runoff",
                                                     "RepairBay", "BumpLane", "SuspensionStep", "ImpactTestPad", "ImpactPadAccess")),
    "LooseGround": (0.50, 1.0, 0.05, (0.11, 0.14, 0.06), ("Ground",)),
}

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary


def physical_material(name, friction, looseness, rolling):
    path = f"{PHYS}/PM_{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        pm = unreal.load_asset(path)
    else:
        factory = unreal.PhysicalMaterialFactoryNew()
        factory.set_editor_property("physical_material_class", unreal.RallyPhysicalMaterial)
        pm = tools.create_asset(f"PM_{name}", PHYS, unreal.RallyPhysicalMaterial, factory)
    if not isinstance(pm, unreal.RallyPhysicalMaterial):
        raise RuntimeError(path + " is not a RallyPhysicalMaterial")
    pm.set_editor_property("friction", friction)
    pm.set_editor_property("static_friction", friction)
    pm.set_editor_property("looseness", looseness)
    pm.set_editor_property("rolling_resistance", rolling)
    unreal.EditorAssetLibrary.save_loaded_asset(pm, False)
    return pm


def surface_material(name, color, pm):
    path = f"{MATS}/M_Lab{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.load_asset(path)
    else:
        material = tools.create_asset(f"M_Lab{name}", MATS, unreal.Material, unreal.MaterialFactoryNew())
        base = lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
        base.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
        lib.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
        rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 200)
        rough.set_editor_property("r", 0.95)
        lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    material.set_editor_property("phys_material", pm)
    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    return material


levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
    raise RuntimeError("Save the open level first.")
if not levels.load_level(MAP):
    raise RuntimeError("Could not load " + MAP)

counts = {}
level_actors = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor)]
for name, (friction, looseness, rolling, color, prefixes) in SURFACES.items():
    pm = physical_material(name, friction, looseness, rolling)
    material = surface_material(name, color, pm)
    for actor in level_actors:
        label = actor.get_actor_label()
        if label.startswith("OctaviaPreview_") or not label.startswith(prefixes):
            continue
        component = actor.static_mesh_component
        component.set_material(0, material)
        # Body override also covers the simple (box) collision shape.
        component.set_phys_material_override(pm)
        counts[name] = counts.get(name, 0) + 1

if counts.get("Gravel", 0) < 30 or counts.get("LooseGround", 0) != 1:
    raise RuntimeError(f"Unexpected surface assignment counts: {counts}")
if not levels.save_current_level():
    raise RuntimeError("Could not save " + MAP)
for path in OBSOLETE:
    if unreal.EditorAssetLibrary.does_asset_exist(path) and not unreal.EditorAssetLibrary.delete_asset(path):
        raise RuntimeError("Could not delete obsolete " + path)
unreal.log(f"LAB_SURFACES_PASS: {counts}")
