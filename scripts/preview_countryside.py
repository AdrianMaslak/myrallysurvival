"""Capture the generated map in Unreal using saved preview cameras, then quit.
Run only via Build-Countryside.ps1 -Preview in the isolated authoring project.
"""
from pathlib import Path
import time
import json
import math
import unreal

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Art/Maps/Countryside/Preview'
OUT.mkdir(parents=True,exist_ok=True)
if 'CountrysideAuthoring' not in unreal.Paths.project_dir():raise RuntimeError('Isolated authoring project required')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'t.MaxFPS 30')
unreal.SystemLibrary.execute_console_command(world,'r.ScreenPercentage 100')
unreal.SystemLibrary.execute_console_command(world,'r.EyeAdaptationQuality 0')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cameras={a.get_actor_label().replace('Country_Camera_',''):a for a in actors.get_all_level_actors()
         if isinstance(a,unreal.CameraActor) and a.get_actor_label().startswith('Country_Camera_')}
manifest=json.loads((ROOT/'Art/Maps/Countryside/Export/countryside.json').read_text(encoding='utf-8'))
# Explicit keyword rotation avoids the engine's positional MakeRotator order (roll, pitch, yaw).
for spec in manifest['cameras']:
    pos=spec['position'];target=spec['target'];dx,dy,dz=[target[i]-pos[i] for i in range(3)]
    camera=cameras[spec['name']]
    camera.set_actor_rotation(unreal.Rotator(pitch=math.degrees(math.atan2(dz,math.hypot(dx,dy))),
        yaw=math.degrees(math.atan2(dy,dx)),roll=0),False)
    unreal.log(f'COUNTRYSIDE_CAMERA: {spec["name"]} {camera.get_actor_forward_vector()}')
for actor in actors.get_all_level_actors():
    label=actor.get_actor_label()
    if label=='Countryside_Sun':
        actor.set_actor_rotation(unreal.Rotator(pitch=-38,yaw=-38,roll=0),False)
    elif label.startswith('Country_Sign_'):
        spec=manifest['texts'][int(label.rsplit('_',1)[1])]
        actor.set_actor_rotation(unreal.Rotator(pitch=0,yaw=spec['yaw'],roll=0),False)
    elif label.endswith('_Anchor') and label.startswith(('SS1_','SS2_','SS3_')):
        spec=next(g for g in manifest['gates'] if label==g['stage']+'_'+g['kind']+'_Anchor')
        actor.set_actor_rotation(unreal.Rotator(pitch=0,yaw=spec['yaw'],roll=0),False)
if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
    raise RuntimeError('Could not save corrected preview/sign orientations')
names=['Service','Village','Forest','Overview']
state={'next':time.monotonic()+20,'i':0,'waiting':None,'start':time.monotonic()}


def tick(dt):
    now=time.monotonic()
    if now-state['start']>240:
        unreal.log_error('Countryside preview timeout')
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.quit_editor();return
    if now<state['next']:return
    if state['waiting'] is not None:
        if not state['waiting'].exists():return
        unreal.log('COUNTRYSIDE_SCREENSHOT: '+str(state['waiting']))
        state['waiting']=None;state['i']+=1;state['next']=now+4;return
    if state['i']==len(names):
        unreal.log('COUNTRYSIDE_PREVIEW_PASS: four engine screenshots captured')
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.quit_editor();return
    name=names[state['i']]
    path=OUT/(name+'.png')
    # Request unique filenames, so prior previews cannot be mistaken for new output.
    if path.exists():
        path=OUT/(name+'_'+str(int(time.time()))+'.png')
    camera=cameras[name]
    unreal.AutomationLibrary.take_high_res_screenshot(1280,720,str(path),camera=camera,delay=2.)
    state['waiting']=path;state['next']=now+8


handle=unreal.register_slate_post_tick_callback(tick)
unreal.log('COUNTRYSIDE_PREVIEW_STARTED')
