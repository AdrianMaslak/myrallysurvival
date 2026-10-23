"""Build only the generated Highlands map and its own assets."""
import sys
import json
import math
import random
import struct
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
import highlands_layout as layout

data=json.loads((layout.ART/'route.json').read_text())
art=json.loads((layout.ART/'art.json').read_text())
BASE='/Game/Maps/Highlands'
MAP=data['map']
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
tools=unreal.AssetToolsHelpers.get_asset_tools()
lib=unreal.MaterialEditingLibrary


def asset(name,cls,factory):
    path=BASE+'/Materials/'+name
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(name,BASE+'/Materials',cls,factory)


physics={}
for name,grip,loose,rolling in [('Mud',.38,1.,.075),('Gravel',.65,1.,.03),('Tarmac',.90,0.,.015),('Ground',.5,1.,.05)]:
    f=unreal.PhysicalMaterialFactoryNew(); f.set_editor_property('physical_material_class',unreal.RallyPhysicalMaterial)
    pm=asset('PM_'+name,unreal.RallyPhysicalMaterial,f)
    for key,val in dict(friction=grip,static_friction=grip,looseness=loose,rolling_resistance=rolling).items(): pm.set_editor_property(key,val)
    physics[name]=pm
palette=dict(art['colors'],Mud=[.095,.056,.026],Gravel=[.30,.27,.20],Tarmac=[.055,.063,.068],Ground=[.14,.20,.075],Blue=[.035,.13,.27],Yellow=[.8,.47,.04])
materials={}
for name,col in palette.items():
    m=asset('M_'+name,unreal.Material,unreal.MaterialFactoryNew())
    current=lib.get_material_property_input_node(m,unreal.MaterialProperty.MP_BASE_COLOR)
    if isinstance(current,unreal.MaterialExpressionConstant3Vector):
        old=current.get_editor_property('constant')
        if max(abs(a-b) for a,b in zip((old.r,old.g,old.b),col))<1e-6:
            materials[name]=m
            continue
    lib.delete_all_material_expressions(m)
    c=lib.create_material_expression(m,unreal.MaterialExpressionConstant3Vector,-300,0)
    c.set_editor_property('constant',unreal.LinearColor(*col,1))
    lib.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
    r=lib.create_material_expression(m,unreal.MaterialExpressionConstant,-300,150)
    r.set_editor_property('r',.27 if name=='Mud' else .88)
    lib.connect_material_property(r,'',unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property('phys_material',physics.get(name,physics['Ground']))
    lib.recompile_material(m); materials[name]=m

world=editor.get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX False')
tasks=[]
for name in art['meshes']:
    opt=unreal.FbxImportUI()
    for key,val in dict(import_mesh=True,import_materials=False,import_textures=False,import_animations=False,
        automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():opt.set_editor_property(key,val)
    for key,val in dict(combine_meshes=True,auto_generate_collision=False,build_nanite=False,
        convert_scene=True,convert_scene_unit=True,generate_lightmap_u_vs=False,
        normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS).items():opt.static_mesh_import_data.set_editor_property(key,val)
    task=unreal.AssetImportTask();task.filename=str(layout.ART/(name+'.fbx'));task.destination_path=BASE+'/Meshes'
    task.destination_name=name;task.automated=True;task.replace_existing=True;task.save=True;task.options=opt;task.factory=unreal.FbxFactory()
    tasks.append(task)
tools.import_asset_tasks(tasks)
meshes={}
for name in art['meshes']:
    mesh=unreal.load_asset(BASE+'/Meshes/'+name)
    for i,slot in enumerate(mesh.static_materials): mesh.set_material(i,materials[str(slot.material_slot_name)])
    unreal.EditorAssetLibrary.save_loaded_asset(mesh,False);meshes[name]=mesh

if unreal.EditorAssetLibrary.does_asset_exist(MAP):
    if not levels.load_level(MAP): raise RuntimeError('Could not load generated map')
    for a in actors.get_all_level_actors():
        if a.actor_has_tag('HighlandsGenerated'): actors.destroy_actor(a)
else:
    if not levels.new_level(MAP): raise RuntimeError('Could not create map')
world=editor.get_editor_world()


def spawn(cls,name,p=(0,0,0),yaw=0,folder='Scenery'):
    a=actors.spawn_actor_from_class(cls,unreal.Vector(*(v*100 for v in p)),unreal.Rotator(pitch=0,yaw=yaw,roll=0))
    a.set_actor_label(name);a.set_folder_path(folder);a.tags=['HighlandsGenerated'];return a


def shape(name,p,scale,mat='White',kind='Cube',yaw=0,collision=True):
    a=spawn(unreal.StaticMeshActor,name,p,yaw)
    c=a.static_mesh_component;c.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/'+kind))
    c.set_material(0,materials[mat]);c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    a.set_actor_scale3d(unreal.Vector(*scale));return a


def text(name,label,p,yaw=180,size=.65):
    a=spawn(unreal.TextRenderActor,name,p,yaw,'Signs')
    c=a.get_component_by_class(unreal.TextRenderComponent);c.set_text(label);c.set_world_size(size*100)
    c.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    c.set_text_render_color(unreal.Color(240,229,190,255));return a


land=unreal.RallyLandscapeTools.import_terrain(world,str(layout.ART/'Highlands.r16'),1009,
    unreal.Vector(-80000,-80000,20000),unreal.Vector(800,800,200),materials['Ground'])
if not land:raise RuntimeError('Landscape import failed')
land.set_actor_label('Highlands_Landscape');land.tags=['HighlandsGenerated'];land.set_folder_path('Landscape')
land.set_editor_property('default_phys_material',physics['Ground'])

roads=[]
for route in data['stages']+data['links']:
    pts=layout.resample(route['samples'],.5) if route['surface']=='Mud' else route['samples']
    rights=[]
    for i,p in enumerate(pts):
        a,b=pts[max(0,i-1)],pts[min(len(pts)-1,i+1)]
        dx,dy=b[0]-a[0],b[1]-a[1];l=math.hypot(dx,dy);rights.append([-dy/l,dx/l,0])
    step=96 if route['surface']=='Mud' else 64
    for start in range(0,len(pts)-1,step):
        pp=pts[start:start+step+1];rr=rights[start:start+step+1]
        origin=pp[0]
        a=spawn(unreal.RallyRoad,route['id']+'_Road_'+str(start),origin,folder='Roads/'+route['id'])
        a.set_editor_property('points',[unreal.Vector(*[(p[k]-origin[k])*100 for k in range(3)]) for p in pp])
        a.set_editor_property('rights',[unreal.Vector(*r) for r in rr])
        a.set_editor_property('width_cm',route['width']*100)
        a.set_editor_property('mud',route['surface']=='Mud')
        a.set_editor_property('material',materials[route['surface']]);a.set_editor_property('surface',physics[route['surface']])
        a.rebuild();roads.append(a)
        if route['id']=='SS1' and start==96:a.tags=['HighlandsGenerated','MudTest']
    if route in data['stages']:
        for end,label in [(0,'START'),(-1,'FINISH')]:
            p=pts[end];r=rights[end];yaw=math.degrees(math.atan2(-r[0],r[1]))
            anchor=spawn(unreal.TargetPoint,route['id']+'_'+label,p,yaw,'Gameplay')
            anchor.tags=['HighlandsGenerated','StageAnchor',route['id'],label]
            for side in [-1,1]:shape('GatePost',[p[0]+r[0]*4*side,p[1]+r[1]*4*side,p[2]+2],(.25,.25,4),'Red')
            shape('GateBanner',[p[0],p[1],p[2]+4],(.15,8.4,.8),'Red',yaw=yaw,collision=False)
            text('GateLabel',route['id']+' '+label,[p[0]-.13*r[1],p[1]+.13*r[0],p[2]+4],yaw+180)

# Flat service aprons connect both stages through the control area.
for i,p in enumerate(data['services']):
    a=spawn(unreal.TargetPoint,'Service_'+str(i+1),p,folder='Gameplay');a.tags=['HighlandsGenerated','RallyService']
    shape('ServiceApron',[p[0],p[1],p[2]-.53],(65,65,1.),'Gravel')
    for side in [-1,1]:
        q=[p[0]+side*23,p[1]+10,p[2]]
        shape('ServiceCanopy',[q[0],q[1],q[2]+3.4],(10,7,.3),'Blue')
        for dx,dy in [(-4,-3),(-4,3),(4,-3),(4,3)]:shape('CanopyLeg',[q[0]+dx,q[1]+dy,q[2]+1.7],(.12,.12,3.4),'White')
        shape('ToolBench',[q[0],q[1]+2,q[2]+.5],(3,1,1),'Red')
        for k in range(3):shape('SpareTyres',[q[0]+3,q[1]-2,q[2]+.2+k*.22],(.65,.65,.2),'Tarmac','Cylinder')
    text('ServiceSign','SERVICE '+str(i+1)+' / REPAIR & REFUEL',[p[0]-4,p[1],p[2]+3],180,.8)

shape('StartApron',[15,0,-.53],(70,30,1.),'Gravel')
spawn(unreal.PlayerStart,'DriverStart',(3,-2,1.2),folder='Gameplay')
spawn(unreal.PlayerStart,'CodriverStart',(3,2,1.2),folder='Gameplay')
text('Welcome','HIGHLANDS RALLY / 20 KM',(35,-7,2),180,.9)

raw=(layout.ART/'Highlands.r16').read_bytes()
def height(x,y):
    fx=(x+800)/8;fy=(y+800)/8;ix=int(fx);iy=int(fy);tx=fx-ix;ty=fy-iy
    def h(a,b):return (struct.unpack_from('<H',raw,2*(b*1009+a))[0]-32768)/64+200
    return (h(ix,iy)*(1-tx)+h(ix+1,iy)*tx)*(1-ty)+(h(ix,iy+1)*(1-tx)+h(ix+1,iy+1)*tx)*ty

rng=random.Random(2040)
bins={}
for r in data['stages']+data['links']:
    for p in r['samples'][::2]:bins.setdefault((int(p[0]//32),int(p[1]//32)),[]).append(p)
def clear(x,y,d):
    bx,by=int(x//32),int(y//32)
    return all((x-p[0])**2+(y-p[1])**2>d*d for a in range(bx-1,bx+2) for b in range(by-1,by+2) for p in bins.get((a,b),[]))

tree_count=0;debris_count=0
for route in data['stages']:
    pts=route['samples']
    for i in range(35,len(pts)-35,5):
        p=pts[i];b=pts[i+1];dx,dy=b[0]-p[0],b[1]-p[1];l=math.hypot(dx,dy);nx,ny=-dy/l,dx/l
        for side in [-1,1]:
            offset=side*rng.uniform(12,80)
            x,y=p[0]+nx*offset,p[1]+ny*offset
            if not clear(x,y,11) or any(math.hypot(x-s[0],y-s[1])<65 for s in data['services']):continue
            z=height(x,y);scale=rng.uniform(.8,1.65)
            a=spawn(unreal.StaticMeshActor,'Pine_'+str(tree_count),(x,y,z),rng.uniform(0,360),'Forest')
            c=a.static_mesh_component;c.set_static_mesh(meshes['SM_HighlandPine']);c.set_collision_profile_name('NoCollision');c.set_cull_distance(170000)
            a.set_actor_scale3d(unreal.Vector(scale,scale,scale))
            if abs(offset)<25:shape('TreeTrunk',(x,y,z+2*scale),(.4*scale,.4*scale,4*scale),'Bark','Cylinder')
            tree_count+=1
    for i in range(120,len(pts)-120,85):
        p=pts[i];b=pts[i+1];dx,dy=b[0]-p[0],b[1]-p[1];l=math.hypot(dx,dy)
        side=rng.choice([-1,1]); offset=side*rng.uniform(1.7,2.5)
        log=debris_count%5==0
        q=[p[0]-dy/l*offset,p[1]+dx/l*offset,p[2]+(.25 if log else .38)]
        a=spawn(unreal.RallyRoadDebris,'Loose_'+str(debris_count),q,rng.uniform(0,180),'LooseDebris')
        a.shape.set_static_mesh(meshes['SM_LooseLog' if log else 'SM_LooseRock'])
        a.set_editor_property('mass_kg',45. if log else 28.)
        if debris_count==0:a.tags=['HighlandsGenerated','DebrisTest']
        debris_count+=1
    # Frequent outside posts, chevrons and tape give road direction and scale.
    for i in range(45,len(pts)-45,45):
        p=pts[i];b=pts[i+1];dx,dy=b[0]-p[0],b[1]-p[1];l=math.hypot(dx,dy)
        for side in [-1,1]:
            x,y=p[0]-dy/l*side*4.4,p[1]+dx/l*side*4.4
            shape('MarkerPost',(x,y,p[2]+.45),(.10,.10,.9),'White',collision=False)
            shape('MarkerTop',(x,y,p[2]+.8),(.13,.13,.14),'Red',collision=False)
    if route['surface']=='Tarmac':
        for i in range(10,len(pts)-10,8):
            p=pts[i];b=pts[i+1];yaw=math.degrees(math.atan2(b[1]-p[1],b[0]-p[0]))
            dash=shape('CentreDash',(p[0],p[1],p[2]+.02),(3,.1,.015),'White',yaw=yaw,collision=False)
            pitch=math.degrees(math.atan2(b[2]-p[2],math.hypot(b[0]-p[0],b[1]-p[1])))
            dash.set_actor_rotation(unreal.Rotator(pitch=pitch,yaw=yaw,roll=0),False)
        for i in range(20,len(pts)-20,20):
            p=pts[i];a=pts[i-10];b=pts[i+10]
            incoming=math.atan2(p[1]-a[1],p[0]-a[0]);outgoing=math.atan2(b[1]-p[1],b[0]-p[0])
            turn=math.atan2(math.sin(outgoing-incoming),math.cos(outgoing-incoming))
            if abs(turn)<.4:continue
            side=-1 if turn>0 else 1
            x=p[0]-math.sin(incoming)*side*5.2;y=p[1]+math.cos(incoming)*side*5.2
            shape('HairpinBoard',(x,y,p[2]+1.1),(.12,2.5,.8),'Yellow',yaw=math.degrees(incoming),collision=False)
            text('HairpinChevron','<<<' if turn>0 else '>>>',(x-.1*math.cos(incoming),y-.1*math.sin(incoming),p[2]+1.1),math.degrees(incoming)+180,.65)

sun=spawn(unreal.DirectionalLight,'HighlandSun',(0,0,400),folder='Lighting')
sun.set_actor_rotation(unreal.Rotator(pitch=-32,yaw=-45,roll=0),False)
sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun.light_component.set_editor_property('atmosphere_sun_light',True);sun.light_component.set_editor_property('intensity',4.)
spawn(unreal.SkyAtmosphere,'HighlandSky',folder='Lighting')
sky=spawn(unreal.SkyLight,'HighlandFill',folder='Lighting');sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property('real_time_capture',True);sky.light_component.set_editor_property('intensity',1.1)
fog=spawn(unreal.ExponentialHeightFog,'ValleyHaze',(0,0,-40),folder='Lighting')
fog.component.set_editor_property('fog_density',.002);fog.component.set_editor_property('start_distance',15000.)
world.get_world_settings().set_editor_property('kill_z',-40000.)
unreal.EditorAssetLibrary.save_directory(BASE,False,True)
if not levels.save_current_level():raise RuntimeError('Map save failed')
report=dict(stages=data['stats'],landscape_components=256,road_chunks=len(roads),trees=tree_count,debris=debris_count)
(layout.ART/'build.json').write_text(json.dumps(report,indent=2))
unreal.log('HIGHLANDS_BUILD_PASS '+json.dumps(report))
