"""Run in Unreal Editor via File > Execute Python Script.
Outside Unreal: python scripts/create_gravel_lab.py --dry-run
Units in this file are metres; the editor adapter converts to centimetres.
"""
import math
import sys

MAP_PATH = "/Game/Maps/L_GravelLab"


def make_layout():
    boxes = []

    def box(name, location, dimensions, yaw=0.0, group="Track"):
        boxes.append(dict(name=name, location=location, dimensions=dimensions,
                          yaw=yaw, group=group))

    # Engine cube is 1 m across. All road slab tops are at z=0.
    box("Ground", (160, 75, -0.7), (440, 280, 1), group="Ground")
    box("AccelerationStraight", (150, 0, -0.1), (300, 8, 0.2))
    points = [
        (300 + 30 * math.cos(-math.pi / 2 + i * math.pi / 48),
         30 + 30 * math.sin(-math.pi / 2 + i * math.pi / 48))
        for i in range(25)
    ]
    for i, (a, b) in enumerate(zip(points, points[1:])):
        dx, dy = b[0] - a[0], b[1] - a[1]
        box(f"Corner_{i:02}", ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, -0.1),
            (math.hypot(dx, dy) + 0.02, 8, 0.2), math.degrees(math.atan2(dy, dx)))
    box("ExitStraight", (330, 105, -0.1), (150, 8, 0.2), yaw=90)
    box("Runoff", (330, 200, -0.1), (40, 16, 0.2), yaw=90)
    box("RepairBay", (15, -16, -0.1), (24, 16, 0.2), group="Service")
    box("RepairBayAccess", (15, -6, -0.1), (8, 4, 0.2), group="Service")
    box("Workbench", (10, -22, 0.45), (2, 0.8, 0.9), group="Service")
    box("BumpLane", (150, 14, -0.1), (50, 8, 0.2), group="Suspension")
    box("BumpLaneEntry", (128, 7, -0.1), (6, 6, 0.2), group="Suspension")
    box("BumpLaneExit", (172, 7, -0.1), (6, 6, 0.2), group="Suspension")
    box("SuspensionStep_10cm", (150, 14, 0.05), (1, 8, 0.1), group="Suspension")
    box("ImpactTestPad", (70, -14, -0.1), (30, 20, 0.2), group="Impact")
    box("ImpactPadAccess", (60, -6, -0.1), (6, 4, 0.2), group="Impact")
    box("ImpactBarrier", (82, -14, 0.75), (0.5, 8, 1.5), group="Impact")

    for distance in range(0, 301, 25):
        for side in (-1, 1):
            box(f"Distance_{distance:03}_{side}", (distance, side * 5, 0.5),
                (0.25, 0.25, 1), group="Markers")
    # Braking cue beside the road, never an obstacle in the normal driving lane.
    box("BrakeMarker_240m", (240, -6, 1), (0.3, 2, 2), group="Markers")
    for side in (-1, 1):
        box(f"FinishPost_{side}", (330 + side * 5, 180, 1.5),
            (0.3, 0.3, 3), group="Markers")
    return boxes, points


def validate_layout(boxes, points):
    names = [item["name"] for item in boxes]
    if len(names) != len(set(names)):
        raise ValueError("Actor labels must be unique.")
    for item in boxes:
        numbers = (*item["location"], *item["dimensions"], item["yaw"])
        if not all(math.isfinite(v) for v in numbers):
            raise ValueError(f"Non-finite geometry: {item['name']}")
        if not all(v > 0 for v in item["dimensions"]):
            raise ValueError(f"Invalid dimensions: {item['name']}")
    if math.dist(points[0], (300, 0)) > 0.0001 or math.dist(points[-1], (330, 30)) > 0.0001:
        raise ValueError("Corner does not meet the straights.")
    return 300 + sum(math.dist(a, b) for a, b in zip(points, points[1:])) + 150


def create_level(boxes):
    import unreal

    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if levels.is_in_play_in_editor():
        raise RuntimeError("Stop Play In Editor before creating the lab.")
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        raise RuntimeError(f"{MAP_PATH} already exists. Open it instead; this script will not overwrite it.")
    # new_level closes the current world without saving it.
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Save your current level before running this script.")
    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
    if cube is None:
        raise RuntimeError("Engine cube mesh is unavailable.")
    if not levels.new_level(MAP_PATH):
        raise RuntimeError(f"Could not create {MAP_PATH}.")

    for item in boxes:
        location = unreal.Vector(*(v * 100 for v in item["location"]))
        actor = actors.spawn_actor_from_class(
            unreal.StaticMeshActor, location, unreal.Rotator(0, item["yaw"], 0))
        if actor is None:
            raise RuntimeError(f"Could not spawn {item['name']}. Inspect the partially created map.")
        actor.set_actor_label(item["name"])
        actor.set_folder_path(item["group"])
        component = actor.static_mesh_component
        component.set_static_mesh(cube)
        component.set_collision_profile_name("BlockAll")
        actor.set_actor_scale3d(unreal.Vector(*item["dimensions"]))

    sun = actors.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(-45, -30, 0))
    if sun is None:
        raise RuntimeError("Could not create the directional light.")
    sun.set_actor_label("LabSun")
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    for index, y in enumerate((-1.5, 1.5)):
        start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(300, y * 100, 150))
        if start is None:
            raise RuntimeError("Could not create PlayerStart.")
        start.set_actor_label(f"PlayerStart_{index + 1}")
    if not levels.save_current_level():
        raise RuntimeError("Could not save the generated level.")
    unreal.log(f"Created {MAP_PATH}. Vehicle, gravel physics and gameplay still need integration.")


if __name__ == "__main__":
    layout, corner = make_layout()
    route_length = validate_layout(layout, corner)
    if "--dry-run" in sys.argv:
        print(f"Layout valid: {len(layout)} mesh actors, 2 player starts, 1 light.")
        print(f"Route: {route_length:.1f} m; road width: 8 m; corner radius: 30 m.")
        print("Separate service bay, 10 cm suspension step and collision-test barrier.")
        print("No Unreal assets created by dry-run.")
    else:
        create_level(layout)
