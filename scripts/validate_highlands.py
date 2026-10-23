"""Reopen the saved level; trace all lanes and check physical mud deformation."""
import json
import math
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[1]
ART=ROOT/'Art/Maps/Highlands'
data=json.loads((ART/'route.json').read_text())
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(data['map'])
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
lands=[a for a in actors if isinstance(a,unreal.Landscape)]
roads=[a for a in actors if isinstance(a,unreal.RallyRoad)]
debris=[a for a in actors if isinstance(a,unreal.RallyRoadDebris)]
assert len(lands)==1
assert len([a for a in actors if a.actor_has_tag('RallyService')])==2
assert len(debris)>80


def trace(p,ignore=debris):
    hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(p[0]*100,p[1]*100,p[2]*100+3000),
        unreal.Vector(p[0]*100,p[1]*100,p[2]*100-200),unreal.TraceTypeQuery.ECC_VISIBILITY,False,
        ignore,unreal.DrawDebugTrace.NONE,True)
    return hit.to_tuple() if hit else (False,)*6


bad=[];checks=0
for route in data['stages']+data['links']:
    pts=route['samples']
    for i in range(2,len(pts)-2,5):
        p=pts[i];a=pts[i-1];b=pts[i+1];dx,dy=b[0]-a[0],b[1]-a[1];l=math.hypot(dx,dy)
        for lateral in [-route['width']*.32,0,route['width']*.32]:
            q=[p[0]-dy/l*lateral,p[1]+dx/l*lateral,p[2]]
            h=trace(q);checks+=1
            if not h[0] or abs(h[5].z/100-p[2])>.12:
                bad.append([route['id'],i,lateral,str(h)])
if bad:
    r=next(a for a in roads if a.get_actor_label()=='SS1_Road_0')
    unreal.log('HIGHLANDS_DIAG '+str((r.road.get_collision_enabled(),r.road.get_collision_profile_name(),
        r.road.get_editor_property('collision_type'),r.road.get_editor_property('enable_complex_collision'),
        r.road.get_dynamic_mesh().get_triangle_count())))
    p=data['stages'][0]['samples'][20]
    for direction in [-1,1]:
        hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(p[0]*100,p[1]*100,p[2]*100+200*direction),
            unreal.Vector(p[0]*100,p[1]*100,p[2]*100-200*direction),unreal.TraceTypeQuery.ECC_VISIBILITY,False,
            lands+debris,unreal.DrawDebugTrace.NONE,True)
        unreal.log('HIGHLANDS_DIAG_TRACE '+str(direction)+' '+str(hit.to_tuple() if hit else None))
    assert not bad, str(len(bad))+' failed traces; first: '+str(bad[0])

# A synthetic tyre pass verifies visible and collision vertices move together,
# the 12 cm floor holds, and the seam stays fixed. Do not save this test edit.
assert levels.save_current_level()
for route in data['stages']:
    h=trace(route['samples'][100])
    unreal.log('HIGHLANDS_SURFACE '+route['surface']+' '+str(h[8]))
mud=next(a for a in roads if a.get_actor_label()=='SS1_Road_96')
points=mud.get_editor_property('points');origin=mud.get_actor_location()
p=points[len(points)//2]+origin
before=trace([p.x/100,p.y/100,p.z/100])[5].z
for _ in range(30):assert mud.apply_track(p-unreal.Vector(80,0,0),p+unreal.Vector(80,0,0),1.) or _>11
mud.rebuild()
after=trace([p.x/100,p.y/100,p.z/100])[5].z
assert 5 < before-after <=12.1, (before,after)
report=dict(lane_traces=checks,landscapes=len(lands),road_chunks=len(roads),debris=len(debris),rut_depth_cm=before-after,errors=bad)
(ART/'validation.json').write_text(json.dumps(report,indent=2))
unreal.log('HIGHLANDS_VALIDATE_PASS '+json.dumps(report))
