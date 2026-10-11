"""Move existing gravel-lab greybox actors to the current layout in create_gravel_lab.py.
Only location, rotation, scale and collision profile of actors whose labels match the layout change; materials,
surfaces and anything else in the map are kept. Run via scripts/Invoke-EditorPython.ps1.
"""
import importlib.util
from pathlib import Path
import unreal

spec = importlib.util.spec_from_file_location("gravel_lab", Path(__file__).with_name("create_gravel_lab.py"))
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)

layout, corner = lab.make_layout()
lab.validate_layout(layout, corner)

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
    raise RuntimeError("Save the open level first.")
if not levels.load_level(lab.MAP_PATH):
    raise RuntimeError("Could not load " + lab.MAP_PATH)

by_label = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
missing = [item["name"] for item in layout if item["name"] not in by_label]
if missing:
    raise RuntimeError(f"Layout actors missing from the map: {missing}")

moved = 0
changed_collision = 0
for item in layout:
    actor = by_label[item["name"]]
    profile = lab.collision_profile(item["group"])
    component = actor.static_mesh_component
    if str(component.get_collision_profile_name()) != profile:
        component.set_collision_profile_name(profile)
        changed_collision += 1
    location = unreal.Vector(*(v * 100 for v in item["location"]))
    rotation = unreal.Rotator(0, item["yaw"], 0)
    scale = unreal.Vector(*item["dimensions"])
    if (actor.get_actor_location() - location).length() > 0.01 or (actor.get_actor_scale3d() - scale).length() > 1e-4:
        actor.set_actor_location_and_rotation(location, rotation, False, False)
        actor.set_actor_scale3d(scale)
        moved += 1

if (moved or changed_collision) and not levels.save_current_level():
    raise RuntimeError("Could not save " + lab.MAP_PATH)
unreal.log(f"LAB_UPDATE_PASS: {moved} actors moved, {changed_collision} collision profiles changed.")
