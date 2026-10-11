"""Prepare the Octavia assets for crash damage (docs/DAMAGE.md).

1. Body meshes get Allow CPU Access, so the game can copy their vertices into editable meshes.
   Parts that can come off get one convex hull of simple collision for the loose piece's physics
   (the car itself never uses part collision).
2. Paint and glass blend on vertex colour R, which the dent code lowers near impacts
   (1 = intact, 0 = destroyed). Meshes without vertex colours read white, so nothing changes
   until something is hit.
   - Paint: scraped to dark bare steel.
   - Glass: crazed, pale and much less see-through.
Existing nodes are kept and rewired: delete_all_material_expressions asserts (!IsRooted) in 5.8.3.
Safe to run again: existing damage Lerps are retuned to the values below, not duplicated.
Run via scripts/Invoke-EditorPython.ps1 -PassMarker DAMAGE_ASSETS_PASS.
"""
import unreal

DEST = "/Game/Vehicles/Octavia/Prototype"
BODY_MESHES = ["SM_Octavia_Chassis", "SM_Octavia_Door_FL", "SM_Octavia_Door_FR", "SM_Octavia_Door_RL",
               "SM_Octavia_Door_RR", "SM_Octavia_Bonnet", "SM_Octavia_Trunk", "SM_Octavia_Radiator",
               "SM_Octavia_FrontBumper", "SM_Octavia_RearBumper"]
DETACHABLE = ["SM_Octavia_Door_FL", "SM_Octavia_Door_FR", "SM_Octavia_Door_RL", "SM_Octavia_Door_RR",
              "SM_Octavia_Bonnet", "SM_Octavia_Trunk", "SM_Octavia_FrontBumper", "SM_Octavia_RearBumper",
              "SM_Octavia_Wheel_FL", "SM_Octavia_Wheel_FR", "SM_Octavia_Wheel_RL", "SM_Octavia_Wheel_RR"]
PAINT = "M_Prototype_Paint"
GLASS = "M_Prototype_Glass_SolidPreview"
# Darker than the pale paint so scrapes read at a distance.
BARE_STEEL = (0.09, 0.09, 0.085)
BARE_METALLIC = 1.0
BARE_ROUGHNESS = 0.45
CRAZED_GLASS = (0.42, 0.45, 0.46)
CRAZED_OPACITY = 0.85

lib = unreal.MaterialEditingLibrary
changed = []

for name in BODY_MESHES:
    path = f"{DEST}/{name}"
    mesh = unreal.load_asset(path)
    if mesh is None:
        raise RuntimeError("Missing " + path)
    if not mesh.get_editor_property("allow_cpu_access"):
        mesh.set_editor_property("allow_cpu_access", True)
        if not unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
            raise RuntimeError("Could not save " + path)
        changed.append(name)

mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
for name in DETACHABLE:
    path = f"{DEST}/{name}"
    mesh = unreal.load_asset(path)
    if mesh is None:
        raise RuntimeError("Missing " + path)
    if mesh_editor.get_convex_collision_count(mesh) > 0:
        continue
    mesh_editor.remove_collisions(mesh)
    # One hull, up to 32 vertices: enough for a wheel to roll and a panel to tumble.
    if not mesh_editor.set_convex_decomposition_collisions(mesh, 1, 32, 100000):
        raise RuntimeError("Convex collision failed for " + path)
    if not unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
        raise RuntimeError("Could not save " + path)
    changed.append(name + " collision")


def set_value(node, value):
    if isinstance(node, unreal.MaterialExpressionConstant3Vector):
        node.set_editor_property("constant", unreal.LinearColor(*value, 1.0))
    else:
        node.set_editor_property("r", value)


def damage_lerp(material, vertex_color, prop, value, x, y):
    """Feed prop through Lerp(damaged value, current input, VertexColor.R), or retune an existing one."""
    current = lib.get_material_property_input_node(material, prop)
    if current is None:
        raise RuntimeError(f"{material.get_name()} has nothing connected to {prop}")
    if isinstance(current, unreal.MaterialExpressionLinearInterpolate):
        damaged = lib.get_inputs_for_material_expression(material, current)[0]
        set_value(damaged, value)
        return
    kind = unreal.MaterialExpressionConstant3Vector if isinstance(value, tuple) else unreal.MaterialExpressionConstant
    damaged = lib.create_material_expression(material, kind, x - 500, y)
    set_value(damaged, value)
    lerp = lib.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, x, y)
    lib.connect_material_expressions(damaged, "", lerp, "A")
    lib.connect_material_expressions(current, "", lerp, "B")
    lib.connect_material_expressions(vertex_color, "R", lerp, "Alpha")
    lib.connect_material_property(lerp, "", prop)


def find_vertex_color(material):
    base = lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
    if isinstance(base, unreal.MaterialExpressionLinearInterpolate):
        return lib.get_inputs_for_material_expression(material, base)[2]
    return lib.create_material_expression(material, unreal.MaterialExpressionVertexColor, -700, 450)


SETTINGS = {
    PAINT: [(unreal.MaterialProperty.MP_BASE_COLOR, BARE_STEEL, 0), (unreal.MaterialProperty.MP_METALLIC, BARE_METALLIC, 150),
            (unreal.MaterialProperty.MP_ROUGHNESS, BARE_ROUGHNESS, 250)],
    GLASS: [(unreal.MaterialProperty.MP_BASE_COLOR, CRAZED_GLASS, 0), (unreal.MaterialProperty.MP_OPACITY, CRAZED_OPACITY, 350)],
}
for name, settings in SETTINGS.items():
    path = f"{DEST}/{name}"
    material = unreal.load_asset(path)
    if material is None:
        raise RuntimeError("Missing " + path)
    vertex_color = find_vertex_color(material)
    for prop, value, y in settings:
        damage_lerp(material, vertex_color, prop, value, -200, y)
    lib.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
        raise RuntimeError("Could not save " + path)
    changed.append(name)

unreal.log(f"DAMAGE_ASSETS_PASS: changed {changed}")
