"""Capture engine views without saving preview actors."""
import json
import time
import math
from pathlib import Path
import unreal
ART=Path(__file__).resolve().parents[1]/'Art/Maps/Highlands'
OUT=ART/'Preview';OUT.mkdir(exist_ok=True)
data=json.loads((ART/'route.json').read_text())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'t.MaxFPS 30')
unreal.SystemLibrary.execute_console_command(world,'r.EyeAdaptationQuality 2')
unreal.SystemLibrary.execute_console_command(world,'r.ScreenPercentage 100')
for actor in actors.get_all_level_actors():
    if actor.get_actor_label().startswith(('ServiceApron','StartApron')):
        scale=actor.get_actor_scale3d()
        if scale.z<.99:
            pos=actor.get_actor_location();pos.z-=(1.-scale.z)*50
            actor.set_actor_location(pos,False,False)
            actor.set_actor_scale3d(unreal.Vector(scale.x,scale.y,1.))
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
specs=[]
for stage in data['stages'][:2]:
    p=stage['samples'][350];q=stage['samples'][400]
    specs.append((stage['id'],[p[0]-25,p[1]-35,p[2]+16],[q[0],q[1],q[2]+2]))
p=data['stages'][2]['samples'][0]
specs.append(('Hairpins',[p[0]-600,p[1]-800,p[2]+600],[p[0]-550,p[1]-200,p[2]+120]))
p=data['services'][0]
specs.append(('Service',[p[0]-65,p[1]-75,p[2]+55],p))
state={'i':0,'next':time.monotonic()+20,'waiting':None,'start':time.monotonic(),'paths':[]}


def tick(dt):
    now=time.monotonic()
    if now-state['start']>240:
        unreal.log_error('HIGHLANDS_PREVIEW_TIMEOUT');unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor();return
    if now<state['next']:return
    if state['waiting']:
        if not state['waiting'].exists():return
        state['paths'].append(str(state['waiting']));state['waiting']=None;state['i']+=1;return
    if state['i']==len(specs):
        (OUT/'latest.json').write_text(json.dumps(state['paths'],indent=2))
        unreal.log('HIGHLANDS_PREVIEW_PASS');unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor();return
    name,p,q=specs[state['i']];dx,dy,dz=[q[k]-p[k] for k in range(3)]
    cam=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(*(v*100 for v in p)),
        unreal.Rotator(pitch=math.degrees(math.atan2(dz,math.hypot(dx,dy))),yaw=math.degrees(math.atan2(dy,dx)),roll=0))
    path=OUT/(name+'_'+str(int(time.time()))+'.png')
    state['waiting']=path;state['next']=now+7
    unreal.AutomationLibrary.take_high_res_screenshot(1280,720,str(path),camera=cam,delay=2.)


handle=unreal.register_slate_post_tick_callback(tick)
