"""Import only /Game/Maps/Countryside assets and create the countryside level.
Run with Build-Countryside.ps1 in its isolated authoring project.
Existing countryside assets are updated only inside that disposable project.
"""
import json
import math
import hashlib
from pathlib import Path
import unreal

ROOT=Path(__file__).resolve().parents[1]
EXPORT=ROOT/'Art/Maps/Countryside/Export'
REPORT=ROOT/'Art/Maps/Countryside/unreal-validation.json'
data=json.loads((EXPORT/'countryside.json').read_text(encoding='utf-8'))
BASE='/Game/Maps/Countryside';MAP=data['map']
tools=unreal.AssetToolsHelpers.get_asset_tools()
lib=unreal.MaterialEditingLibrary
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

if 'CountrysideAuthoring' not in unreal.Paths.project_dir():
    raise RuntimeError('Use Build-Countryside.ps1: this importer runs only in the isolated authoring project.')
if levels.is_in_play_in_editor() or unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
    raise RuntimeError('Authoring world must be saved and outside PIE.')


def asset(name,folder,cls,factory):
    p=f'{BASE}/{folder}/{name}'
    return unreal.load_asset(p) if unreal.EditorAssetLibrary.does_asset_exist(p) else tools.create_asset(name,f'{BASE}/{folder}',cls,factory)


physics={}
for name,friction,loose,rolling in [('Asphalt',.9,0.,.015),('Gravel',.65,1.,.03),('Ground',.50,1.,.05)]:
    f=unreal.PhysicalMaterialFactoryNew();f.set_editor_property('physical_material_class',unreal.RallyPhysicalMaterial)
    pm=asset('PM_Country_'+name,'Materials',unreal.RallyPhysicalMaterial,f)
    for key,val in dict(friction=friction,static_friction=friction,looseness=loose,rolling_resistance=rolling).items():
        pm.set_editor_property(key,val)
    unreal.EditorAssetLibrary.save_loaded_asset(pm,False);physics[name]=pm

materials={}
for name,color in data['materials'].items():
    mat=asset('M_Country_'+name,'Materials',unreal.Material,unreal.MaterialFactoryNew())
    current=lib.get_material_property_input_node(mat,unreal.MaterialProperty.MP_BASE_COLOR)
    if isinstance(current,unreal.MaterialExpressionConstant3Vector):
        old=current.get_editor_property('constant')
        if max(abs(a-b) for a,b in zip((old.r,old.g,old.b),color))<1e-6:
            materials[name]=mat
            continue
    lib.delete_all_material_expressions(mat)
    c=lib.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector,-350,0)
    c.set_editor_property('constant',unreal.LinearColor(*color,1))
    lib.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
    r=lib.create_material_expression(mat,unreal.MaterialExpressionConstant,-350,180)
    r.set_editor_property('r',.3 if name=='Glass' else .9)
    lib.connect_material_property(r,'',unreal.MaterialProperty.MP_ROUGHNESS)
    mat.set_editor_property('phys_material',physics.get(name,physics['Ground']))
    lib.recompile_material(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat,False);materials[name]=mat

world=editor.get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX False')
tasks=[]
for part in data['parts']:
    part['sha256']=hashlib.sha256((EXPORT/part['file']).read_bytes()).hexdigest()
    existing=unreal.load_asset(BASE+'/Meshes/'+part['name']) if unreal.EditorAssetLibrary.does_asset_exist(BASE+'/Meshes/'+part['name']) else None
    if existing and unreal.EditorAssetLibrary.get_metadata_tag(existing,'CountrySourceSHA256')==part['sha256']:
        continue
    opt=unreal.FbxImportUI()
    for key,val in dict(import_mesh=True,import_as_skeletal=False,import_materials=False,
                        import_textures=False,import_animations=False,automated_import_should_detect_type=False,
                        mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():opt.set_editor_property(key,val)
    for key,val in dict(combine_meshes=True,auto_generate_collision=False,build_nanite=False,
                        convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,
                        transform_vertex_to_absolute=True,import_uniform_scale=1.,
                        generate_lightmap_u_vs=False,
                        normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS).items():
        opt.static_mesh_import_data.set_editor_property(key,val)
    task=unreal.AssetImportTask();task.filename=str(EXPORT/part['file']);task.destination_path=BASE+'/Meshes'
    task.destination_name=part['name'];task.automated=True;task.replace_existing=True;task.save=True
    task.options=opt;task.factory=unreal.FbxFactory();tasks.append(task)
tools.import_asset_tasks(tasks)

mesh_assets={};errors=[]
for part in data['parts']:
    sm=unreal.load_asset(BASE+'/Meshes/'+part['name'])
    if not isinstance(sm,unreal.StaticMesh):raise RuntimeError('Missing mesh '+part['name'])
    bounds=sm.get_bounding_box()
    actual=[[bounds.min.x/100,bounds.min.y/100,bounds.min.z/100],
            [bounds.max.x/100,bounds.max.y/100,bounds.max.z/100]]
    if max(abs(a-b) for aa,bb in zip(actual,part['bounds_m']) for a,b in zip(aa,bb))>.06:
        errors.append(f'{part["name"]} transformed bounds {actual} expected {part["bounds_m"]}')
    unchanged=unreal.EditorAssetLibrary.get_metadata_tag(sm,'CountrySourceSHA256')==part['sha256']
    for i,slot in enumerate(sm.static_materials):
        name=str(slot.material_slot_name).replace('M_Country_','')
        if name not in materials:
            raise RuntimeError(f'Unknown material slot {slot.material_slot_name} on {part["name"]}')
        if slot.material_interface!=materials[name]:
            sm.set_material(i,materials[name]);unchanged=False
    if part['collision'] and not unchanged:
        body=sm.get_editor_property('body_setup')
        body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        body.set_editor_property('double_sided_geometry',True)
    if not unchanged:
        unreal.EditorAssetLibrary.set_metadata_tag(sm,'CountrySourceSHA256',part['sha256'])
        unreal.EditorAssetLibrary.save_loaded_asset(sm,False)
    mesh_assets[part['name']]=sm
if errors:raise RuntimeError('\n'.join(errors[:8]))

# Rebuild generated actors in our isolated map; never delete or replace a live user map.
if unreal.EditorAssetLibrary.does_asset_exist(MAP):
    if not levels.load_level(MAP):raise RuntimeError('Could not reopen '+MAP)
    for old in actors.get_all_level_actors():
        if old.get_actor_label().startswith(('SM_Country_','Country_','Countryside_','SS1_','SS2_','SS3_')):
            if not actors.destroy_actor(old):raise RuntimeError('Could not replace generated actor')
elif not levels.new_level(MAP):
    raise RuntimeError('Could not create '+MAP)
for part in data['parts']:
    actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,0))
    actor.set_actor_label(part['name']);actor.set_folder_path(part['group'])
    c=actor.static_mesh_component;c.set_static_mesh(mesh_assets[part['name']])
    c.set_collision_profile_name('BlockAll' if part['collision'] else 'NoCollision')
    if part['surface']:c.set_phys_material_override(physics[part['surface']])
    if part['cull_m']:c.set_cull_distance(part['cull_m']*100)
    if not part['collision']:c.set_editor_property('cast_shadow','Foliage' in part['name'])
    actor.tags=[unreal.Name('CountrysideGenerated')]

def spawn(cls,name,pos=(0,0,0),rot=(0,0,0),folder='08_Lighting'):
    a=actors.spawn_actor_from_class(cls,unreal.Vector(*(v*100 for v in pos)),
        unreal.Rotator(pitch=rot[0],yaw=rot[1],roll=rot[2]))
    if a is None:raise RuntimeError('Could not spawn '+name)
    a.set_actor_label(name);a.set_folder_path(folder);return a

# The two ribbons meet with different tangents at the gates. Small solid junction
# aprons close the triangular cap gaps across the full lane width. These endpoints
# lie in the almost-flat service surrounds (under 1 cm height variation per apron).
cylinder=unreal.load_asset('/Engine/BasicShapes/Cylinder.Cylinder')
for g in data['gates']:
    r=next(r for r in data['routes'] if r['id']==g['stage'])
    surface=r['surface']
    if surface=='Mixed':surface='Gravel' if g['kind']=='START' else 'Asphalt'
    p=g['position'];radius=r['width']/2+1.8
    a=spawn(unreal.StaticMeshActor,'Country_Junction_'+g['stage']+'_'+g['kind'],
            (p[0],p[1],p[2]-.034),folder='02_Roads')
    c=a.static_mesh_component;c.set_static_mesh(cylinder)
    c.set_material(0,materials[surface]);c.set_phys_material_override(physics[surface])
    c.set_collision_profile_name('BlockAll');a.set_actor_scale3d(unreal.Vector(radius*2,radius*2,.08))

for i,p in enumerate(data['player_starts']):spawn(unreal.PlayerStart,f'Countryside_PlayerStart_{i+1}',p,folder='00_Gameplay')
sun=spawn(unreal.DirectionalLight,'Countryside_Sun',(0,0,100),(-38,-38,0))
sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun.light_component.set_editor_property('atmosphere_sun_light',True)
sun.light_component.set_editor_property('intensity',4.)
spawn(unreal.SkyAtmosphere,'Countryside_Sky')
sky=spawn(unreal.SkyLight,'Countryside_Skylight',(0,0,20))
sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property('real_time_capture',True)
sky.light_component.set_editor_property('intensity',1.2)
fog=spawn(unreal.ExponentialHeightFog,'Countryside_Haze',(0,0,-20))
fog.component.set_editor_property('fog_density',.0015)
fog.component.set_editor_property('start_distance',20000.)

for i,spec in enumerate(data['texts']):
    a=spawn(unreal.TextRenderActor,f'Country_Sign_{i:02}',spec['position'],(0,spec['yaw'],0),'07_RouteMarkers')
    c=a.get_component_by_class(unreal.TextRenderComponent)
    c.set_text(spec['text']);c.set_world_size(spec['size']*100)
    c.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    c.set_text_render_color(unreal.Color(*[int(v*255) for v in data['materials'][spec['color']]],255))
    c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

for g in data['gates']:
    a=spawn(unreal.TargetPoint,g['stage']+'_'+g['kind']+'_Anchor',g['position'],(0,g['yaw'],0),'00_Gameplay/StageAnchors')
    a.tags=[unreal.Name(g['stage']),unreal.Name(g['kind']),unreal.Name('StageAnchor')]
for spec in data['cameras']:
    pos=spec['position'];tar=spec['target'];dx,dy,dz=[tar[i]-pos[i] for i in range(3)]
    rot=(math.degrees(math.atan2(dz,math.hypot(dx,dy))),math.degrees(math.atan2(dy,dx)),0)
    spawn(unreal.CameraActor,'Country_Camera_'+spec['name'],pos,rot,'09_PreviewCameras')

world=editor.get_editor_world()
world.get_world_settings().set_editor_property('kill_z',-25000.)
if not levels.save_current_level():raise RuntimeError('Failed to save countryside level')
# Reopen the saved map to test serialised collision, not only the freshly spawned objects.
if not levels.load_level(MAP):raise RuntimeError('Failed to reopen countryside level')
world=editor.get_editor_world()
checks=0;bad=[]
for road in data['routes']:
    pts=road['samples']
    for i in range(0,len(pts),4):
        p=pts[i];a=pts[max(0,i-1)];b=pts[min(len(pts)-1,i+1)]
        dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy)
        for lateral in (-road['width']*.35,0,road['width']*.35):
            x=p[0]-dy/length*lateral;y=p[1]+dx/length*lateral
            hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x*100,y*100,p[2]*100+3000),
                unreal.Vector(x*100,y*100,p[2]*100-300),unreal.TraceTypeQuery.ECC_VISIBILITY,
                False,[],unreal.DrawDebugTrace.NONE,True)
            checks+=1
            if hit is None:bad.append(f'{road["id"]} {i} offset {lateral}: no collision');continue
            result=hit.to_tuple()
            blocking,position=result[0],result[5]
            if not blocking or abs(position.z/100-p[2])>.09:
                bad.append(f'{road["id"]} {i} offset {lateral}: hit Z={position.z/100:.3f}, expected {p[2]:.3f}')
report=dict(map=MAP,mesh_count=len(data['parts']),triangles=sum(p['triangles'] for p in data['parts']),
            trees=data['tree_count'],stages=data['stages'],road_collision_rays=checks,errors=bad,
            note='Saved and reloaded map. Visibility traces use simple queries, matching vehicle suspension. Full stage driving remains a playtest.')
REPORT.write_text(json.dumps(report,indent=2),encoding='utf-8')
if bad:raise RuntimeError('\n'.join(bad[:15]))
unreal.log(f'COUNTRYSIDE_IMPORT_PASS: {len(data["parts"])} meshes; {checks} road collision rays; 3 stages; saved and reloaded.')

# Keep the additive road-edge pass on future complete map rebuilds as well.
# Its own tag limits replacement to roadside objects and its preflight checks
# preserve every actor produced above. Existing projects can run that pass alone.
if (ROOT/'Art/Maps/Countryside/Roadside/roadside.json').exists():
    import runpy
    runpy.run_path(str(ROOT/'scripts/apply_countryside_roadside.py'), run_name='__main__')
