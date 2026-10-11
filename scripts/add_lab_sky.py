"""Add sky atmosphere, sky light and light fog to the gravel lab (it only had a directional light).
Run via scripts/Invoke-EditorPython.ps1; skips work if LabSky actors already exist.
"""
import unreal

MAP = "/Game/Maps/L_GravelLab"
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
    raise RuntimeError("Save the open level first.")
if not levels.load_level(MAP):
    raise RuntimeError("Could not load " + MAP)

existing = actors.get_all_level_actors()
if any(a.get_actor_label().startswith("LabSky") for a in existing):
    unreal.log("LAB_SKY_PASS: sky already present, nothing changed.")
else:
    suns = [a for a in existing if a.get_actor_label() == "LabSun"]
    if len(suns) != 1:
        raise RuntimeError("Expected one LabSun directional light.")
    suns[0].light_component.set_editor_property("atmosphere_sun_light", True)

    sky = actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    sky.set_actor_label("LabSkyAtmosphere")
    sky_light = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
    sky_light.set_actor_label("LabSkyLight")
    component = sky_light.light_component
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_editor_property("real_time_capture", True)
    fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -100))
    fog.set_actor_label("LabSkyFog")
    fog.component.set_editor_property("fog_density", 0.005)
    for actor in (sky, sky_light, fog):
        actor.set_folder_path("Lighting")
    if not levels.save_current_level():
        raise RuntimeError("Could not save " + MAP)
    unreal.log("LAB_SKY_PASS: sky atmosphere, sky light and fog added.")
