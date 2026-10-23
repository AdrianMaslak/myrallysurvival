"""Build only the generated Kysuce map from Art/Maps/Kysuce (scripts/kysuce_layout.py). Placeholder shapes,
no textures: one flat colour per material."""
import json
import math
from pathlib import Path
import unreal

ART = Path(__file__).resolve().parents[1]/'Art/Maps/Kysuce'
data = json.loads((ART/'route.json').read_text())
props = json.loads((ART/'props.json').read_text())
BASE = '/Game/Maps/Kysuce'
MAP = data['map']
TAG = 'KysuceGenerated'
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary


def asset(name, cls, factory):
    path = BASE+'/Materials/'+name
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(name, BASE+'/Materials', cls, factory)


physics = {}
for name, grip, loose, rolling in [('Mud', .38, 1., .075), ('Gravel', .65, 1., .03), ('Tarmac', .90, 0., .015),
                                   ('Snow', .30, 1., .04), ('Ground', .5, 1., .05)]:
    f = unreal.PhysicalMaterialFactoryNew(); f.set_editor_property('physical_material_class', unreal.RallyPhysicalMaterial)
    pm = asset('PM_'+name, unreal.RallyPhysicalMaterial, f)
    for key, val in dict(friction=grip, static_friction=grip, looseness=loose, rolling_resistance=rolling).items(): pm.set_editor_property(key, val)
    physics[name] = pm
palette = dict(Mud=[.095, .056, .026], Gravel=[.30, .27, .20], Tarmac=[.055, .063, .068], Snow=[.85, .87, .9],
               Ground=[.14, .20, .075], Pine=[.045, .115, .065], Bark=[.10, .055, .025], White=[.78, .76, .65],
               Red=[.55, .045, .025], Roof=[.32, .08, .04], Plaster=[.62, .58, .48], Wood=[.27, .14, .055],
               Steel=[.42, .44, .45], Hay=[.55, .45, .18], Blue=[.035, .13, .27], Yellow=[.8, .47, .04])
materials = {}
for name, col in palette.items():
    m = asset('M_'+name, unreal.Material, unreal.MaterialFactoryNew())
    current = lib.get_material_property_input_node(m, unreal.MaterialProperty.MP_BASE_COLOR)
    if isinstance(current, unreal.MaterialExpressionConstant3Vector):
        old = current.get_editor_property('constant')
        if max(abs(a-b) for a, b in zip((old.r, old.g, old.b), col)) < 1e-6:
            materials[name] = m
            continue
    lib.delete_all_material_expressions(m)
    c = lib.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -300, 0)
    c.set_editor_property('constant', unreal.LinearColor(*col, 1))
    lib.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    r = lib.create_material_expression(m, unreal.MaterialExpressionConstant, -300, 150)
    r.set_editor_property('r', .27 if name == 'Mud' else .5 if name == 'Snow' else .88)
    lib.connect_material_property(r, '', unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property('phys_material', physics.get(name, physics['Ground']))
    lib.recompile_material(m); materials[name] = m

if unreal.EditorAssetLibrary.does_asset_exist(MAP):
    if not levels.load_level(MAP): raise RuntimeError('Could not load generated map')
    for a in actors.get_all_level_actors():
        if a.actor_has_tag(TAG): actors.destroy_actor(a)
else:
    if not levels.new_level(MAP): raise RuntimeError('Could not create map')
world = editor.get_editor_world()


def spawn(cls, name, p=(0, 0, 0), yaw=0, folder='Scenery'):
    a = actors.spawn_actor_from_class(cls, unreal.Vector(*(v*100 for v in p)), unreal.Rotator(pitch=0, yaw=yaw, roll=0))
    a.set_actor_label(name); a.set_folder_path(folder); a.tags = [TAG]; return a


def shape(name, p, scale, mat='White', kind='Cube', yaw=0, collision=True):
    a = spawn(unreal.StaticMeshActor, name, p, yaw)
    c = a.static_mesh_component; c.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/'+kind))
    c.set_material(0, materials[mat]); c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    a.set_actor_scale3d(unreal.Vector(*scale)); return a


def text(name, label, p, yaw=180, size=.65, folder='Signs'):
    a = spawn(unreal.TextRenderActor, name, p, yaw, folder)
    c = a.get_component_by_class(unreal.TextRenderComponent); c.set_text(label); c.set_world_size(size*100)
    c.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    c.set_text_render_color(unreal.Color(240, 229, 190, 255)); return a


def resample(points, spacing):
    out = [points[0]]
    for a, b in zip(points, points[1:]):
        n = max(1, math.ceil(math.dist(a, b)/spacing))
        out.extend([[a[k]+(b[k]-a[k])*i/n for k in range(3)] for i in range(1, n+1)])
    return out


L = data['landscape']
land = unreal.RallyLandscapeTools.import_terrain(world, str(ART/'Kysuce.r16'), L['size'],
    unreal.Vector(*(v*100 for v in L['origin'])), unreal.Vector(L['spacing']*100, L['spacing']*100, L['z_scale']), materials['Ground'])
if not land: raise RuntimeError('Landscape import failed')
land.set_actor_label('Kysuce_Landscape'); land.tags = [TAG]; land.set_folder_path('Landscape')
land.set_editor_property('default_phys_material', physics['Ground'])

roads = []
for route in data['roads']:
    mud = route['surface'] == 'Mud'
    pts = resample(route['samples'], .5) if mud else route['samples']
    rights = []
    for i, p in enumerate(pts):
        a, b = pts[max(0, i-1)], pts[min(len(pts)-1, i+1)]
        dx, dy = b[0]-a[0], b[1]-a[1]; l = math.hypot(dx, dy); rights.append([-dy/l, dx/l, 0])
    step = 96 if mud else 64
    for start in range(0, len(pts)-1, step):
        pp = pts[start:start+step+1]; rr = rights[start:start+step+1]
        origin = pp[0]
        a = spawn(unreal.RallyRoad, route['id']+'_'+str(start), origin, folder='Roads/'+route['id'])
        a.set_editor_property('points', [unreal.Vector(*[(p[k]-origin[k])*100 for k in range(3)]) for p in pp])
        a.set_editor_property('rights', [unreal.Vector(*r) for r in rr])
        a.set_editor_property('width_cm', route['width']*100)
        a.set_editor_property('mud', mud)
        a.set_editor_property('material', materials[route['surface']]); a.set_editor_property('surface', physics[route['surface']])
        a.rebuild(); roads.append(a)

for stage in data['stages']:
    pts = stage['samples']
    for end, label in [(0, 'START'), (-1, 'FINISH')]:
        p = pts[end]; q = pts[3] if end == 0 else pts[-4]
        dx, dy = (q[0]-p[0], q[1]-p[1]) if end == 0 else (p[0]-q[0], p[1]-q[1])
        l = math.hypot(dx, dy); r = [-dy/l, dx/l]; yaw = math.degrees(math.atan2(dy, dx))
        anchor = spawn(unreal.TargetPoint, stage['id']+'_'+label, p, yaw, 'Gameplay')
        anchor.tags = [TAG, 'StageAnchor', stage['id'], label]
        for side in [-1, 1]: shape('GatePost', [p[0]+r[0]*4.5*side, p[1]+r[1]*4.5*side, p[2]+2], (.25, .25, 4), 'Red' if end == 0 else 'Tarmac')
        shape('GateBanner', [p[0], p[1], p[2]+4], (.15, 9.4, .8), 'Red' if end == 0 else 'Tarmac', yaw=yaw, collision=False)
        text('GateLabel', stage['id']+' '+label, [p[0]-.13*dx/l, p[1]-.13*dy/l, p[2]+4], yaw+180)

# Service parks: the HQ by the first start, one between each pair of stages and one at the finish. The
# existing service actions work within 35 m of each RallyService marker.
for i, p in enumerate(data['services']):
    label = 'RALLY HQ / SERVICE' if i == 0 else f'SERVICE {i} / REPAIR & REFUEL' if i < len(data['services'])-1 else 'FINISH SERVICE'
    a = spawn(unreal.TargetPoint, 'Service_'+str(i), p, folder='Gameplay'); a.tags = [TAG, 'RallyService']
    shape('ServiceApron', [p[0], p[1], p[2]-.53], (65, 65, 1.), 'Gravel')
    for side in [-1, 1]:
        q = [p[0]+side*23, p[1]+18, p[2]]
        shape('ServiceCanopy', [q[0], q[1], q[2]+3.4], (10, 7, .3), 'Blue')
        for dx, dy in [(-4, -3), (-4, 3), (4, -3), (4, 3)]: shape('CanopyLeg', [q[0]+dx, q[1]+dy, q[2]+1.7], (.12, .12, 3.4), 'White')
        shape('ToolBench', [q[0], q[1]+2, q[2]+.5], (3, 1, 1), 'Red')
        for k in range(3): shape('SpareTyres', [q[0]+3, q[1]-2, q[2]+.2+k*.22], (.65, .65, .2), 'Tarmac', 'Cylinder')
    text('ServiceSign', label, [p[0]-4, p[1]+12, p[2]+3], 90, .8)

spawn(unreal.PlayerStart, 'DriverStart', (3, -2, 1.2), folder='Gameplay')
spawn(unreal.PlayerStart, 'CodriverStart', (3, 2, 1.2), folder='Gameplay')
text('Welcome', 'KYSUCE RALLY / ' + ' / '.join(f"{s['id']} {s['length_m']/1000:.1f} KM" for s in data['stats']), (25, -12, 2.5), 90, .8)

# Scenery as instances: one actor per kind. Kind: mesh, material, collision, cull distance (m).
kinds = dict(Spruce=('Cone', 'Pine', False, 1500), Trunk=('Cylinder', 'Bark', True, 600), House=('Cube', 'Plaster', True, 2500),
             Roof=('Cube', 'Roof', False, 2500), Fence=('Cube', 'Wood', True, 400), Bale=('Cylinder', 'Hay', True, 800),
             Post=('Cylinder', 'White', False, 500), Rail=('Cube', 'Steel', True, 700), RailPost=('Cube', 'Steel', False, 500))
groups = {}
for prop in props:
    if prop['k'] not in kinds: continue
    p = prop['p']
    rot = unreal.Rotator(pitch=prop.get('pitch', 0), yaw=prop['yaw'], roll=0)
    groups.setdefault(prop['k'], []).append(unreal.Transform(unreal.Vector(*(v*100 for v in p)), rot, unreal.Vector(*prop['s'])))
counts = {}
for kind, transforms in groups.items():
    mesh, mat, collision, cull = kinds[kind]
    a = unreal.RallyLandscapeTools.spawn_instances(world, 'Kysuce_'+kind, unreal.load_asset('/Engine/BasicShapes/'+mesh),
                                                   materials[mat], transforms, collision, cull*100)
    if not a: raise RuntimeError('Instances failed: '+kind)
    a.tags = [TAG]; a.set_folder_path('Scenery'); counts[kind] = len(transforms)
for n, prop in enumerate(p for p in props if p['k'] in ('Board', 'Chevron')):
    x, y, z = prop['p']; colour = prop.get('colour', 'Yellow'); yaw = prop['yaw']
    shape(prop['k'], (x, y, z), prop['s'], colour, yaw=yaw, collision=False)
    out = math.radians(yaw)
    text(prop['k']+'Text', prop['text'], (x-.1*math.cos(out), y-.1*math.sin(out), z-.25), yaw+180, .55)

sun = spawn(unreal.DirectionalLight, 'KysuceSun', (0, 0, 400), folder='Lighting')
sun.set_actor_rotation(unreal.Rotator(pitch=-32, yaw=-45, roll=0), False)
sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun.light_component.set_editor_property('atmosphere_sun_light', True); sun.light_component.set_editor_property('intensity', 4.)
spawn(unreal.SkyAtmosphere, 'KysuceSky', folder='Lighting')
sky = spawn(unreal.SkyLight, 'KysuceFill', folder='Lighting'); sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property('real_time_capture', True); sky.light_component.set_editor_property('intensity', 1.1)
fog = spawn(unreal.ExponentialHeightFog, 'ValleyHaze', (0, 0, -40), folder='Lighting')
fog.component.set_editor_property('fog_density', .002); fog.component.set_editor_property('start_distance', 15000.)
world.get_world_settings().set_editor_property('kill_z', (L['origin'][2]-L['z_scale']*2.56-100)*100)
unreal.EditorAssetLibrary.save_directory(BASE, False, True)
if not levels.save_current_level(): raise RuntimeError('Map save failed')
report = dict(stages=data['stats'], road_chunks=len(roads), instances=counts)
(ART/'build.json').write_text(json.dumps(report, indent=2))
unreal.log('KYSUCE_BUILD_PASS '+json.dumps(report))
