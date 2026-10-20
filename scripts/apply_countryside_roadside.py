"""Add/update only tagged roadside actors in the existing countryside map.

Run via Build-CountrysideRoadside.ps1 with the project closed. A map backup is
made before edits. Existing roads, materials, scenery and gameplay actors survive.
"""
import hashlib
import json
import math
import shutil
import sys
from datetime import datetime
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from countryside_roadside import ART, TAG, SHAPES, make_layout, route_data, tangent

BASE = '/Game/Maps/Countryside/Roadside'
MAP = '/Game/Maps/Countryside/L_Countryside'
data = json.loads((ART / 'roadside.json').read_text(encoding='utf-8'))
expected = make_layout()
if expected['objects'] != data['objects']:
    raise RuntimeError('Roadside layout changed: rebuild roadside art first.')
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
meshes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
materials_api = unreal.MaterialEditingLibrary
if levels.is_in_play_in_editor() or unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
    raise RuntimeError('Save the current world and leave PIE before installing roadside hazards.')

project_dir = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
if project_dir not in (ROOT.resolve(), (ROOT / 'Saved/CountrysideAuthoring').resolve()):
    raise RuntimeError('Run only in this project or its countryside authoring copy.')
map_file = project_dir / 'Content/Maps/Countryside/L_Countryside.umap'
backup = project_dir / 'Saved/RoadsideBackups' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
backup.mkdir(parents=True)
shutil.copy2(map_file, backup / map_file.name)
before_hash = hashlib.sha256(map_file.read_bytes()).hexdigest()
if not levels.load_level(MAP):
    raise RuntimeError('Could not load existing countryside level.')
world = editor.get_editor_world()


def finish_mesh_loading():
    # The road junction primitives are ready immediately, while imported static
    # meshes can still be compiling after LoadLevel. These APIs wait for their
    # render/body data and finish the component physics-state refresh.
    seen = set()
    for actor in actors.get_all_level_actors():
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            sm = component.static_mesh
            if sm is not None and sm.get_path_name() not in seen:
                sm.get_num_lods()
                meshes.get_simple_collision_count(sm)
                seen.add(sm.get_path_name())


finish_mesh_loading()
old = [a for a in actors.get_all_level_actors() if a.actor_has_tag(TAG)]
preserved = {a.get_path_name() for a in actors.get_all_level_actors() if not a.actor_has_tag(TAG)}


def trace(x, y, z, ignore=old):
    hit = unreal.SystemLibrary.line_trace_single(world,
        unreal.Vector(x * 100, y * 100, z * 100 + 500),
        unreal.Vector(x * 100, y * 100, z * 100 - 500),
        unreal.TraceTypeQuery.ECC_VISIBILITY, False, ignore, unreal.DrawDebugTrace.NONE, True)
    if hit is None or not hit.to_tuple()[0]:
        raise RuntimeError(f'No ground collision at {x:.2f}, {y:.2f}, {z:.2f}')
    return hit.to_tuple()[5].z / 100


def road_heights(ignore):
    result = []
    for road in route_data():
        pts = road['samples']
        for i in range(0, len(pts), 4):
            p = pts[i]
            t = tangent(pts, i)
            for offset in (-road['width'] * .48, 0, road['width'] * .48):
                x, y = p[0] - t[1] * offset, p[1] + t[0] * offset
                height = trace(x, y, p[2], ignore)
                if abs(height - p[2]) > .25:
                    raise RuntimeError(f'Route manifest no longer matches {road["id"]} sample {i}.')
                result.append(height)
    return result


# Capture existing road heights before changing anything, and ground every object
# against actual saved terrain/shoulders rather than assuming the original mesh height.
before_roads = road_heights(old)
placements = []
for spec in data['objects']:
    x, y, ref_z = spec['position']
    ground = trace(x, y, ref_z)
    pitch = 0.0
    if spec['mesh'] in ('Berm', 'StoneBoundary'):
        half = SHAPES[spec['mesh']][0] * spec['scale'][0] / 2
        yaw = math.radians(spec['yaw'])
        dx, dy = math.cos(yaw) * half, math.sin(yaw) * half
        low, high = trace(x - dx, y - dy, ref_z), trace(x + dx, y + dy, ref_z)
        pitch = math.degrees(math.atan2(high - low, 2 * half))
        ground = (low + high) / 2
    placements.append((spec, ground, pitch))


def asset(name, folder, cls, factory):
    path = f'{BASE}/{folder}/{name}'
    return unreal.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, f'{BASE}/{folder}', cls, factory)


ground_pm = unreal.load_asset('/Game/Maps/Countryside/Materials/PM_Country_Ground')
factory = unreal.PhysicalMaterialFactoryNew()
factory.set_editor_property('physical_material_class', unreal.RallyPhysicalMaterial)
solid_pm = asset('PM_Roadside_Solid', 'Materials', unreal.RallyPhysicalMaterial, factory)
for name, value in dict(friction=.7, static_friction=.7, restitution=.03, looseness=0., rolling_resistance=.025).items():
    solid_pm.set_editor_property(name, value)
lib.save_loaded_asset(solid_pm, False)
material_assets = {}
for name, color in data['materials'].items():
    mat = asset('M_Roadside_' + name, 'Materials', unreal.Material, unreal.MaterialFactoryNew())
    materials_api.delete_all_material_expressions(mat)
    c = materials_api.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
    c.set_editor_property('constant', unreal.LinearColor(*color, 1))
    materials_api.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = materials_api.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 180)
    rough.set_editor_property('r', .95)
    materials_api.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    mat.set_editor_property('phys_material', ground_pm if name in ('Earth', 'Grass') else solid_pm)
    materials_api.recompile_material(mat)
    lib.save_loaded_asset(mat, False)
    material_assets[name] = mat

unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX False')
for spec in data['assets']:
    options = unreal.FbxImportUI()
    for name, value in dict(import_mesh=True, import_as_skeletal=False, import_materials=False,
            import_textures=False, import_animations=False, automated_import_should_detect_type=False,
            mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():
        options.set_editor_property(name, value)
    for name, value in dict(combine_meshes=True, auto_generate_collision=False,
            one_convex_hull_per_ucx=True, build_nanite=False, convert_scene=True,
            convert_scene_unit=True, force_front_x_axis=False, transform_vertex_to_absolute=True,
            import_uniform_scale=1., generate_lightmap_u_vs=True,
            normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS).items():
        options.static_mesh_import_data.set_editor_property(name, value)
    task = unreal.AssetImportTask()
    task.filename = str(ART / 'Export' / spec['file'])
    task.destination_path = BASE + '/Meshes'
    task.destination_name = spec['name']
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    task.factory = unreal.FbxFactory()
    tools.import_asset_tasks([task])

mesh_assets, hull_counts = {}, {}
for spec in data['assets']:
    sm = unreal.load_asset(BASE + '/Meshes/' + spec['name'])
    if not isinstance(sm, unreal.StaticMesh):
        raise RuntimeError('Failed to import ' + spec['name'])
    body = sm.get_editor_property('body_setup')
    body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)
    for i, slot in enumerate(sm.static_materials):
        key = str(slot.material_slot_name).replace('M_Roadside_', '')
        sm.set_material(i, material_assets[key])
    hull_counts[spec['kind']] = meshes.get_convex_collision_count(sm)
    if hull_counts[spec['kind']] != 1:
        raise RuntimeError(f'{spec["name"]}: expected one imported UCX hull, got {hull_counts[spec["kind"]]}')
    bounds = sm.get_bounding_box()
    # FBX mirrors Y; compare dimensions rather than asymmetric min/max signs.
    for axis in ('x', 'y', 'z'):
        k = ('x', 'y', 'z').index(axis)
        extent = (getattr(bounds.max, axis) - getattr(bounds.min, axis)) / 100
        expected_extent = spec['bounds_m'][1][k] - spec['bounds_m'][0][k]
        if abs(extent - expected_extent) > .02:
            raise RuntimeError(f'{spec["name"]}: unexpected {axis} size {extent}')
    lib.save_loaded_asset(sm, False)
    mesh_assets[spec['kind']] = sm

for actor in old:
    if not actors.destroy_actor(actor):
        raise RuntimeError('Could not replace tagged roadside actor')
spawned = []
for spec, ground, pitch in placements:
    x, y, _ = spec['position']
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x * 100, y * 100, ground * 100),
        unreal.Rotator(pitch=pitch, yaw=spec['yaw'], roll=0))
    if a is None:
        raise RuntimeError('Could not spawn ' + spec['name'])
    a.set_actor_label(spec['name'])
    a.set_folder_path(f'10_Roadside/{spec["stage"]}/Pocket_{spec["pocket"]:02}')
    a.tags = [unreal.Name(TAG), unreal.Name(spec['stage']), unreal.Name('Roadside_' + spec['mesh'])]
    c = a.static_mesh_component
    c.set_mobility(unreal.ComponentMobility.STATIC)
    c.set_static_mesh(mesh_assets[spec['mesh']])
    c.set_collision_profile_name('BlockAll')
    c.set_phys_material_override(ground_pm if spec['mesh'] == 'Berm' else solid_pm)
    # Keep physical hazards visible at speed. No per-actor distance disappearance.
    a.set_actor_scale3d(unreal.Vector(*spec['scale']))
    spawned.append(a)

after_roads = road_heights([])
if max(abs(a - b) for a, b in zip(before_roads, after_roads)) > .01:
    raise RuntimeError('A roadside object overlaps the drivable road; map was not saved.')
after_preserved = {a.get_path_name() for a in actors.get_all_level_actors() if not a.actor_has_tag(TAG)}
if preserved != after_preserved:
    raise RuntimeError('An existing non-roadside actor changed; map was not saved.')
query_checks = 0
for a in spawned:
    p = a.get_actor_location()
    distance, closest = a.static_mesh_component.get_closest_point_on_collision(p + unreal.Vector(0, 0, 200))
    if distance < 0:
        raise RuntimeError('Crash contact query failed on ' + a.get_actor_label())
    query_checks += 1
if not levels.save_current_level():
    raise RuntimeError('Could not save roadside additions.')
if not levels.load_level(MAP):
    raise RuntimeError('Could not reopen saved map.')
world = editor.get_editor_world()
finish_mesh_loading()
saved = [a for a in actors.get_all_level_actors() if a.actor_has_tag(TAG)]
if len(saved) != len(data['objects']):
    raise RuntimeError('Saved roadside actor count differs from the layout.')
saved_roads = road_heights([])
if max(abs(a - b) for a, b in zip(before_roads, saved_roads)) > .01:
    raise RuntimeError('Saved road collision differs from the original map.')
report = dict(map=MAP, seed=data['seed'], pockets=len(data['pockets']), objects=len(saved),
              counts=data['counts'], stages=data['stages'], convex_hulls=hull_counts,
              road_collision_rays_per_pass=len(before_roads), closest_point_checks=query_checks,
              preserved_actor_count=len(preserved), map_backup=str(backup / map_file.name),
              original_map_sha256=before_hash, saved_map_sha256=hashlib.sha256(map_file.read_bytes()).hexdigest(),
              minimum_road_clearance_m=data['minimum_road_clearance_m'], errors=[],
              note='Compared road heights before/after and after reload. Simple collision queries passed. Driving severity remains a playtest.')
(ART / 'unreal-validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
unreal.log(f'COUNTRYSIDE_ROADSIDE_PASS: {len(saved)} objects in {len(data["pockets"])} pockets; '
           f'{query_checks} crash contact queries; {len(before_roads)} road rays per pass; '
           f'{len(preserved)} existing actors preserved. Backup: {backup}')
