"""Capture three road-level views of the saved roadside additions; never save the map."""
import json
import math
import time
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'Art/Maps/Countryside/Roadside'
OUT = ART / 'Preview'
OUT.mkdir(parents=True, exist_ok=True)
data = json.loads((ART / 'roadside.json').read_text(encoding='utf-8'))
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 't.MaxFPS 30')
unreal.SystemLibrary.execute_console_command(world, 'r.ScreenPercentage 100')
unreal.SystemLibrary.execute_console_command(world, 'r.EyeAdaptationQuality 0')
run = time.strftime('%Y%m%d-%H%M%S')
shots = []
for stage, number in [('SS1', 3), ('SS2', 5), ('SS3', 4)]:
    pocket = next(p for p in data['pockets'] if p['stage'] == stage and p['index'] == number)
    p, t, side = pocket['position'], pocket['tangent'], pocket['side']
    target = [p[0] - t[1] * side * 4.5, p[1] + t[0] * side * 4.5, p[2] + .5]
    pos = [p[0] - t[0] * 19 + t[1] * side * 3,
           p[1] - t[1] * 19 - t[0] * side * 3, p[2] + 4.0]
    dx, dy, dz = [target[i] - pos[i] for i in range(3)]
    camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(*(v * 100 for v in pos)),
        unreal.Rotator(pitch=math.degrees(math.atan2(dz, math.hypot(dx, dy))),
                       yaw=math.degrees(math.atan2(dy, dx)), roll=0))
    camera.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view', 65.)
    shots.append((camera, OUT / f'{stage}_{run}.png'))
state = dict(start=time.monotonic(), next=time.monotonic() + 15, index=0, waiting=None)


def tick(delta):
    now = time.monotonic()
    if now - state['start'] > 180:
        unreal.log_error('Roadside preview timed out')
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.quit_editor()
        return
    if now < state['next']:
        return
    if state['waiting'] is not None:
        if not state['waiting'].exists():
            return
        unreal.log('COUNTRYSIDE_ROADSIDE_SCREENSHOT: ' + str(state['waiting']))
        state['waiting'] = None
        state['index'] += 1
        state['next'] = now + 3
        return
    if state['index'] == len(shots):
        (OUT / 'latest.json').write_text(json.dumps([str(p.relative_to(ART)) for _, p in shots], indent=2) + '\n', encoding='utf-8')
        unreal.log('COUNTRYSIDE_ROADSIDE_PREVIEW_PASS: three engine views captured; map not saved')
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.quit_editor()
        return
    camera, destination = shots[state['index']]
    unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, str(destination), camera=camera, delay=2.)
    state['waiting'] = destination
    state['next'] = now + 6


handle = unreal.register_slate_post_tick_callback(tick)
