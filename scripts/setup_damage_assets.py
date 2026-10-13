"""Prepare the Octavia assets for crash damage (docs/DAMAGE.md).

1. Body meshes get Allow CPU Access, so the game can copy their vertices into editable meshes.
   Parts that can come off get one convex hull of simple collision for the loose piece's physics
   (the car itself never uses part collision).
2. Paint and glass blend on vertex colour R, which the dent code lowers near impacts
   (1 = intact, 0 = destroyed). Meshes without vertex colours read white, so nothing changes
   until something is hit.
   - Paint: scraped to dark bare steel.
   - Glass: crazed, pale and much less see-through.
   - Head and tail lamps: smashed, dark.
   Paint and lamps are two-sided: a wing or lamp that has come off shows its back, which is open.
3. M_Prototype_Steam for the engine's steam and smoke puffs (created once).
Existing nodes are kept and rewired: delete_all_material_expressions asserts (!IsRooted) in 5.8.3.
Safe to run again: existing damage Lerps are retuned to the values below, not duplicated.
Run via scripts/Invoke-EditorPython.ps1 -PassMarker DAMAGE_ASSETS_PASS.
"""
import unreal

DEST = "/Game/Vehicles/Octavia/Prototype"
BODY_MESHES = ["SM_Octavia_Chassis", "SM_Octavia_Door_FL", "SM_Octavia_Door_FR", "SM_Octavia_Door_RL",
               "SM_Octavia_Door_RR", "SM_Octavia_Bonnet", "SM_Octavia_Trunk", "SM_Octavia_Radiator",
               "SM_Octavia_FrontBumper", "SM_Octavia_RearBumper", "SM_Octavia_Fender_FL", "SM_Octavia_Fender_FR",
               "SM_Octavia_Headlight_L", "SM_Octavia_Headlight_R", "SM_Octavia_Taillight_L", "SM_Octavia_Taillight_R",
               "SM_Octavia_Roof", "SM_Octavia_Windshield", "SM_Octavia_BumperBar_F", "SM_Octavia_BumperBar_R",
               "SM_Octavia_Interior", "SM_Octavia_Engine"]
DETACHABLE = ["SM_Octavia_Door_FL", "SM_Octavia_Door_FR", "SM_Octavia_Door_RL", "SM_Octavia_Door_RR",
              "SM_Octavia_Bonnet", "SM_Octavia_Trunk", "SM_Octavia_FrontBumper", "SM_Octavia_RearBumper",
              "SM_Octavia_Wheel_FL", "SM_Octavia_Wheel_FR", "SM_Octavia_Wheel_RL", "SM_Octavia_Wheel_RR",
              "SM_Octavia_Fender_FL", "SM_Octavia_Fender_FR", "SM_Octavia_Headlight_L", "SM_Octavia_Headlight_R",
              "SM_Octavia_Taillight_L", "SM_Octavia_Taillight_R"]
# Suspension parts can be taken off and carried, so they need a hull too, as do the jack, water can, oil
# bottle and an exhaust pulled off.
DETACHABLE += ["SM_Octavia_Jack", "SM_Octavia_WaterCan", "SM_Octavia_OilBottle", "SM_Octavia_Exhaust", "SM_Octavia_FirstAid"]
# Engine modules come off and are carried too, and are aimed at by their hulls.
DETACHABLE += [f"SM_Octavia_{part}" for part in ("EngineCover", "AirFilter", "Intake", "ExhaustManifold", "Turbo",
                                                  "Intercooler", "Alternator", "Starter", "ECU")]
for corner in ("FL", "FR", "RL", "RR"):
    DETACHABLE += [f"SM_Octavia_{part}_{corner}" for part in ("Strut", "Hub", "Disc", "LowerArm", "Driveshaft")]
    DETACHABLE += [f"SM_Octavia_{part}_{corner}" for part in (("TieRod",) if corner[0] == "F" else ("UpperArm", "TrailingArm"))]
PAINT = "M_Prototype_Paint"
GLASS = "M_Prototype_Glass_SolidPreview"
HEADLAMP = "M_Prototype_Headlamp"
TAILLAMP = "M_Prototype_Taillamp"
# Darker than the pale paint so scrapes read at a distance.
BARE_STEEL = (0.09, 0.09, 0.085)
BARE_METALLIC = 1.0
BARE_ROUGHNESS = 0.45
CRAZED_GLASS = (0.42, 0.45, 0.46)
CRAZED_OPACITY = 0.85
# A smashed lamp: cracked lens and dark reflector.
BROKEN_LAMP = (0.07, 0.07, 0.07)

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
    HEADLAMP: [(unreal.MaterialProperty.MP_BASE_COLOR, BROKEN_LAMP, 0)],
    TAILLAMP: [(unreal.MaterialProperty.MP_BASE_COLOR, BROKEN_LAMP, 0)],
}
for name, settings in SETTINGS.items():
    path = f"{DEST}/{name}"
    material = unreal.load_asset(path)
    if material is None:
        raise RuntimeError("Missing " + path)
    vertex_color = find_vertex_color(material)
    for prop, value, y in settings:
        damage_lerp(material, vertex_color, prop, value, -200, y)
    if name in (PAINT, HEADLAMP, TAILLAMP):
        material.set_editor_property("two_sided", True)
    lib.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
        raise RuntimeError("Could not save " + path)
    changed.append(name)

# Steam and smoke puffs (URallyEngineComponent): an instanced sphere per puff, unlit and see-through, its
# opacity and colour from the instance's custom data (0 opacity, 1-3 colour), soft towards its silhouette.
STEAM = "M_Prototype_Steam"
if unreal.load_asset(f"{DEST}/{STEAM}") is None:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    steam = tools.create_asset(STEAM, DEST, unreal.Material, unreal.MaterialFactoryNew())
    steam.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    steam.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    steam.set_editor_property("used_with_instanced_static_meshes", True)
    opacity = lib.create_material_expression(steam, unreal.MaterialExpressionPerInstanceCustomData, -700, 200)
    opacity.set_editor_property("data_index", 0)
    colour = lib.create_material_expression(steam, unreal.MaterialExpressionPerInstanceCustomData3Vector, -700, 0)
    colour.set_editor_property("data_index", 1)
    fresnel = lib.create_material_expression(steam, unreal.MaterialExpressionFresnel, -700, 400)
    fresnel.set_editor_property("exponent", 1.5)
    soft = lib.create_material_expression(steam, unreal.MaterialExpressionOneMinus, -450, 400)
    lib.connect_material_expressions(fresnel, "", soft, "")
    fade = lib.create_material_expression(steam, unreal.MaterialExpressionMultiply, -250, 250)
    lib.connect_material_expressions(opacity, "", fade, "A")
    lib.connect_material_expressions(soft, "", fade, "B")
    lib.connect_material_property(colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.connect_material_property(fade, "", unreal.MaterialProperty.MP_OPACITY)
    lib.recompile_material(steam)
    if not unreal.EditorAssetLibrary.save_loaded_asset(steam, False):
        raise RuntimeError("Could not save " + STEAM)
    changed.append(STEAM)

unreal.log(f"DAMAGE_ASSETS_PASS: changed {changed}")
