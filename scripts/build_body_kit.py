"""Builds the Octavia body and interior hand-off kit for detailing, texturing and rigging ("octavia body/").
Run twice, once per file:
  blender -b --factory-startup --python scripts/build_body_kit.py -- <repo> "<repo>/octavia body" body
  blender -b --factory-startup --python scripts/build_body_kit.py -- <repo> "<repo>/octavia body" interior

Source: Art/Vehicles/Octavia/Prototype/Octavia_With_Wheels.blend (read, never saved). Output, in the frame the
engine and suspension sets use (metres, Z up, +X car's right, +Y car's front, origin on the ground under the middle
of the car):
- Octavia1U_Body.blend/.fbx: one object per part the game moves, opens or knocks off, origin at the game's pivot,
  with empties for hinge lines, latches and clips; the chassis (shell, underbody, inner panels) as one object.
- Octavia1U_Interior.blend/.fbx: the cabin split for animation (needles, pedals, wheel, stalks, lever, handbrake).
- A REFERENCE collection (not exported) in both: the game's 1.8T engine set and the v2 suspension, as wireframes.
- body_manifest.json: every part, its pivot, size, triangles, materials, hinge/latch/clip points and angles.
"""
import bpy, json, math, sys
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path(sys.argv[sys.argv.index('--') + 1])
OUT = Path(sys.argv[sys.argv.index('--') + 2])
OUT.mkdir(parents=True, exist_ok=True)
SOURCE = ROOT / 'Art/Vehicles/Octavia/Prototype/Octavia_With_Wheels.blend'
# Car blend world -> kit frame: a half turn about Z (the car blend has the car's front along -Y, right along -X).
CAR_TO_KIT = Matrix.Rotation(math.pi, 4, 'Z')


def ue_to_kit(v):
    # Unreal cm (X forward, Y right, Z up) -> kit metres (X right, Y forward, Z up).
    return Vector((v[1], v[0], v[2])) * 0.01


# Game parts of the body (scripts/export_octavia_parts.py PANEL_GROUPS): source group names inside the
# object names "octavia1u_<group>__<material>".
BODY = {
    'Door_FL': ['door_FL', 'doorglass_FL', 'doorpanel_FL', 'doortrim_FL', 'mirror_L'],
    'Door_FR': ['door_FR', 'doorglass_FR', 'doorpanel_FR', 'doortrim_FR', 'mirror_R'],
    'Door_RL': ['door_RL', 'doorglass_RL', 'doorpanel_RL', 'doortrim_RL'],
    'Door_RR': ['door_RR', 'doorglass_RR', 'doorpanel_RR', 'doortrim_RR'],
    'Bonnet': ['FL_hood'],
    'Trunk': ['trunk', 'trunkglass', 'spoiler', 'lettering_R', 'lettering_vRS_R', 'logo_R', 'shelf'],
    'FrontBumper': ['FL_bumper_F_a', 'FL_grille', 'lettering_vRS_F'],
    'RearBumper': ['FL_bumper_R_a'],
    'Fender_FL': ['fender_FL'],
    'Fender_FR': ['fender_FR'],
    'Headlight_L': ['FLheadlight_L', 'FLheadlight_frame_L', 'FLheadlight_glass_L', 'FLheadlight_foglight_L', 'signal_L', 'signal_frame_L', 'signal_glass_L'],
    'Headlight_R': ['FLheadlight_R', 'FLheadlight_frame_R', 'FLheadlight_glass_R', 'FLheadlight_foglight_R', 'signal_R', 'signal_frame_R', 'signal_glass_R'],
    'Taillight_L': ['FL_taillight_L', 'FL_taillight_frame_L', 'FL_taillight_glass_L'],
    'Taillight_R': ['FL_taillight_R', 'FL_taillight_frame_R', 'FL_taillight_glass_R'],
    'Roof': ['roof_a', 'antenna_limo'],
    'Windshield': ['windshield'],
    'BumperBar_F': ['bumperbar_F'],
    'BumperBar_R': ['bumperbar_R'],
    'Wipers': ['wipers'],
}
# The game's pivots (Unreal cm, Art/Vehicles/Octavia/Prototype/Export/assembly.json); the chassis at the origin.
PIVOTS_CM = {
    'Chassis': (0, 0, 0), 'Door_FL': (27.61, -77.08, 84.78), 'Door_FR': (27.60, 77.06, 84.80), 'Door_RL': (-77.41, -69.80, 84.78),
    'Door_RR': (-77.40, 69.76, 84.79), 'Bonnet': (153.73, 0.0, 83.40), 'Trunk': (-175.79, 0.0, 100.92), 'FrontBumper': (190.90, 0.0, 47.07),
    'RearBumper': (-192.75, 0.05, 43.64), 'Fender_FL': (140.69, -77.64, 55.57), 'Fender_FR': (140.69, 77.62, 55.57),
    'Headlight_L': (200.88, -54.46, 64.36), 'Headlight_R': (200.88, 54.46, 64.36), 'Taillight_L': (-218.79, -60.48, 85.34),
    'Taillight_R': (-218.79, 60.48, 85.34), 'Roof': (-42.91, -0.01, 132.08), 'Windshield': (66.81, -0.01, 114.79),
    'BumperBar_F': (208.68, 0.0, 37.25), 'BumperBar_R': (-225.88, 0.0, 45.41), 'Wipers': (105.0, 0.0, 94.0),
}
# Hinges (or the two clips/bolt groups that hold a part), latch, opening axis, angles and mass: the game's
# panel mounts (Source/RallySurvival/Private/Damage/RallyPanelComponent.cpp, Mounts), Unreal cm.
MOUNTS = {
    'Door_FL': dict(A=(85, -80, 55), B=(85, -80, 110), Latch=(-26, -80, 80), Axis=(0, 0, 1), MaxDeg=70, OpenDeg=65, MassKg=28),
    'Door_FR': dict(A=(85, 80, 55), B=(85, 80, 110), Latch=(-26, 80, 80), Axis=(0, 0, -1), MaxDeg=70, OpenDeg=65, MassKg=28),
    'Door_RL': dict(A=(-22, -78, 55), B=(-22, -78, 110), Latch=(-130, -78, 80), Axis=(0, 0, 1), MaxDeg=70, OpenDeg=65, MassKg=24),
    'Door_RR': dict(A=(-22, 78, 55), B=(-22, 78, 110), Latch=(-130, 78, 80), Axis=(0, 0, -1), MaxDeg=70, OpenDeg=65, MassKg=24),
    'Bonnet': dict(A=(101, -58, 95), B=(101, 58, 95), Latch=(208, 0, 75), Axis=(0, -1, 0), MaxDeg=115, OpenDeg=55, MassKg=18),
    'Trunk': dict(A=(-122, -50, 133), B=(-122, 50, 133), Latch=(-228, 0, 75), Axis=(0, 1, 0), MaxDeg=75, OpenDeg=70, MassKg=24),
    'FrontBumper': dict(A=(197, -78, 49), B=(197, 78, 49), Latch=(197, 0, 49), MassKg=8),
    'RearBumper': dict(A=(-206, -77, 46), B=(-206, 77, 46), Latch=(-206, 0, 46), MassKg=7),
    'Fender_FL': dict(A=(180, -70, 88), B=(100, -78, 90), Latch=(140, -74, 89), MassKg=4),
    'Fender_FR': dict(A=(180, 70, 88), B=(100, 78, 90), Latch=(140, 74, 89), MassKg=4),
    'Headlight_L': dict(A=(200, -36, 64), B=(198, -74, 64), Latch=(199, -55, 64), MassKg=2.5),
    'Headlight_R': dict(A=(200, 36, 64), B=(198, 74, 64), Latch=(199, 55, 64), MassKg=2.5),
    'Taillight_L': dict(A=(-218, -52, 92), B=(-218, -68, 78), Latch=(-218, -60, 85), MassKg=1),
    'Taillight_R': dict(A=(-218, 52, 92), B=(-218, 68, 78), Latch=(-218, 60, 85), MassKg=1),
}
# The cabin, split where things move. Pivots and axes are estimates from the meshes (exact ones are part of the job).
INTERIOR = {
    'Dashboard': ['dashboard', 'inttrimlower', 'radio_a', 'klima_a', 'FL_gaugesdecals', 'benzingaugesvrs_metric', 'gauges_a'],
    'Needle_Speedo': ['needlevrs_speedo'], 'Needle_Tacho': ['needlevrs_tacho'], 'Needle_Temp': ['needlevrs_temp'], 'Needle_Fuel': ['needlevrs_fuel'],
    'SteeringWheel': ['steering_wheel_c'],
    'Stalk_Wiper': ['wiperstalk'], 'Stalk_Signal': ['signalstalk'],
    'Pedal_Clutch': ['pedal_clutch'], 'Pedal_Brake': ['pedal_brake'], 'Pedal_Gas': ['pedal_gas'], 'Footrest': ['footrest'],
    'GearLever': ['shifter_knob_M'], 'GearBoot': ['shifter_base_M'], 'Handbrake': ['handbrake'], 'Armrest': ['armrest'],
    'Seat_FL': ['seat_FL'], 'Seat_FR': ['seat_FR'], 'Seat_Rear': ['seat_R'],
    'Floor': ['intcarpet'], 'Headliner': ['inttrimupper', 'interiorlight'], 'Mirror_Interior': ['mirror_int'],
}

bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
for ob in [o for o in bpy.data.objects if o.parent is None]:
    ob.matrix_world = CAR_TO_KIT @ ob.matrix_world
bpy.context.view_layer.update()


def group_of(name):
    # "octavia1u_door_FL__octavia1u-material.001" -> ("door_FL", "octavia1u")
    body = name[len('octavia1u_'):] if name.startswith('octavia1u_') else name
    group, _, material = body.partition('__')
    material = material.split('-material')[0].split('.')[0] or 'base'
    return group, material


def visible_meshes(collection):
    return [o for o in bpy.data.collections[collection].all_objects if o.type == 'MESH' and o.visible_get()]


def build(name, sources, pivot):
    # One object from the sources, origin at pivot, a material slot per source material (named after it).
    meshes = []
    for src in sources:
        data = src.data.copy()
        data.transform(Matrix.Translation(-pivot) @ src.matrix_world)
        copy = bpy.data.objects.new(name + '_part', data)
        material_name = group_of(src.name)[1]
        mat = bpy.data.materials.get('KIT_' + material_name) or bpy.data.materials.new('KIT_' + material_name)
        data.materials.clear()
        data.materials.append(mat)
        kit.objects.link(copy)
        meshes.append(copy)
    bpy.ops.object.select_all(action='DESELECT')
    for m in meshes:
        m.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = name
    ob.data.name = name
    ob.location = pivot
    # Two UV channels as the game reads them: the model's own map, then the paint's Livery map.
    return ob


def empty(name, at, parent, size=0.04, kind='SPHERE'):
    e = bpy.data.objects.new(name, None)
    e.empty_display_type = kind
    e.empty_display_size = size
    kit.objects.link(e)
    e.parent = parent
    e.matrix_world = Matrix.Translation(at)
    return e


def tris(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def size_of(ob):
    lo = Vector([min(v.co[i] for v in ob.data.vertices) for i in range(3)])
    hi = Vector([max(v.co[i] for v in ob.data.vertices) for i in range(3)])
    return lo, hi


manifest = {'frame': 'metres, Blender Z-up, +X car right, +Y car front, origin on the ground under the middle of the car',
            'game_frame': 'Unreal cm: X = kit Y * 100, Y = kit X * 100, Z = kit Z * 100', 'body': [], 'interior': []}

# --- Body -----------------------------------------------------------------------------------------------------
kit = bpy.data.collections.new('BODY_KIT')
bpy.context.scene.collection.children.link(kit)
body_meshes = visible_meshes('01_Body')
used = set()
for part, groups in BODY.items():
    sources = [o for o in body_meshes + visible_meshes('02_Interior') if group_of(o.name)[0] in groups]
    if not sources:
        print('KIT missing', part)
        continue
    used |= {o.name for o in sources}
    ob = build(part, sources, ue_to_kit(PIVOTS_CM[part]))
    entry = {'name': part, 'pivot_m': [round(c, 4) for c in ob.location], 'triangles': tris(ob),
             'materials': [m.name[4:] for m in ob.data.materials], 'sources': sorted(o.name for o in sources)}
    lo, hi = size_of(ob)
    entry['size_m'] = [round(c, 4) for c in hi - lo]
    m = MOUNTS.get(part)
    if m:
        for key in ('A', 'B', 'Latch'):
            label = {'A': 'HingeA', 'B': 'HingeB', 'Latch': 'Latch'}[key] if 'Axis' in m else {'A': 'FixingA', 'B': 'FixingB', 'Latch': 'FixingMid'}[key]
            at = ue_to_kit(m[key])
            empty(part + '_' + label, at, ob)
            entry[label.lower() + '_m'] = [round(c, 4) for c in at]
        if 'Axis' in m:
            # Swapping X and Y between the frames mirrors them, so the turning sense flips: negate the axis.
            axis = -Vector((m['Axis'][1], m['Axis'][0], m['Axis'][2]))
            entry['open_axis'] = [round(c, 3) for c in axis]
            entry['opens_about'] = 'the line HingeA-HingeB; opening turns it about open_axis (right-hand rule)'
            entry['max_open_deg'] = m['MaxDeg']
            entry['held_open_deg'] = m['OpenDeg']
        entry['mass_kg'] = m['MassKg']
    manifest['body'].append(entry)
# The chassis: every other body mesh (shell, inner panels, underbody, plastics, badges on the shell).
rest = [o for o in body_meshes if o.name not in used]
ob = build('Chassis', rest, Vector())
lo, hi = size_of(ob)
manifest['body'].insert(0, {'name': 'Chassis', 'pivot_m': [0, 0, 0], 'triangles': tris(ob), 'size_m': [round(c, 4) for c in hi - lo],
                            'materials': [m.name[4:] for m in ob.data.materials], 'sources': sorted(o.name for o in rest)})
body_objects = [o for o in kit.objects]

# --- Interior ---------------------------------------------------------------------------------------------------
inner = bpy.data.collections.new('INTERIOR_KIT')
bpy.context.scene.collection.children.link(inner)
kit_body = kit
kit = inner
interior_meshes = visible_meshes('02_Interior')


def estimate(part, ob):
    # Pivot and axis guesses for the moving cabin parts (kit frame).
    lo, hi = size_of(ob)
    lo_w, hi_w = ob.location + lo, ob.location + hi
    centre = (lo_w + hi_w) / 2
    if part.startswith('Needle'):
        # The hub: the needle's lowest end; it turns about the dial's normal (towards the driver, -Y, tilted).
        return Vector((centre.x, centre.y, lo_w.z)), Vector((0, -1, 0)), 'turns in the dial face; rest = 0 on the scale'
    if part.startswith('Pedal'):
        return Vector((centre.x, hi_w.y, hi_w.z)), Vector((1, 0, 0)), 'pivot at the top of the arm, under the dash; pressed = the pad moves forward'
    if part == 'SteeringWheel':
        return None, Vector((0.0045, 0.9204, -0.3909)), 'the column axis (from the game: assembly.json column_axis)'
    if part.startswith('Stalk'):
        return Vector((centre.x, centre.y, centre.z)), Vector((1, 0, 0)), 'pivots at the column end; flicks up and down'
    if part == 'GearLever':
        return Vector((centre.x, centre.y, lo_w.z - 0.08)), Vector((1, 0, 0)), 'ball joint under the boot; moves across and fore-aft through the H'
    if part == 'Handbrake':
        return Vector((centre.x, lo_w.y, lo_w.z)), Vector((1, 0, 0)), 'pivot at the rear end of the lever; pulled = grip up 20-25 deg'
    return None, None, ''


for part, groups in INTERIOR.items():
    sources = [o for o in interior_meshes if group_of(o.name)[0] in groups and o.name not in used]
    if not sources:
        print('KIT missing', part)
        continue
    lo_all = Vector((min((s.matrix_world @ Vector(c)).x for s in sources for c in s.bound_box),
                     min((s.matrix_world @ Vector(c)).y for s in sources for c in s.bound_box),
                     min((s.matrix_world @ Vector(c)).z for s in sources for c in s.bound_box)))
    hi_all = Vector((max((s.matrix_world @ Vector(c)).x for s in sources for c in s.bound_box),
                     max((s.matrix_world @ Vector(c)).y for s in sources for c in s.bound_box),
                     max((s.matrix_world @ Vector(c)).z for s in sources for c in s.bound_box)))
    pivot = (lo_all + hi_all) / 2
    if part == 'SteeringWheel':
        pivot = ue_to_kit((47.34, -38.62, 87.41))
    ob = build(part, sources, pivot)
    at, axis, note = estimate(part, ob)
    if at is not None:
        # Re-centre the origin on the estimated pivot.
        shift = at - ob.location
        ob.data.transform(Matrix.Translation(-shift))
        ob.location = at
    entry = {'name': part, 'pivot_m': [round(c, 4) for c in ob.location], 'triangles': tris(ob),
             'materials': [m.name[4:] for m in ob.data.materials], 'sources': sorted(o.name for o in sources)}
    if axis is not None:
        empty(part + '_Axis', ob.location + axis.normalized() * 0.05, ob, 0.01)
        entry['axis'] = [round(c, 4) for c in axis.normalized()]
        entry['motion'] = note
        entry['pivot_is_estimate'] = part != 'SteeringWheel'
    manifest['interior'].append(entry)

# Points the game needs in the cabin (Unreal cm in the game; here kit metres).
manifest['cabin_points_m'] = {
    'driver_eye_estimate': [round(c, 4) for c in ue_to_kit((-5.0, -38.6, 112.0))],
    'steering_wheel_centre': [round(c, 4) for c in ue_to_kit((47.34, -38.62, 87.41))],
}

# --- Reference (not exported) -------------------------------------------------------------------------------------
ref = bpy.data.collections.new('REFERENCE_DoNotExport')
bpy.context.scene.collection.children.link(ref)
engine = json.load(open(ROOT / 'Art/Vehicles/Octavia/Engine_18T/Export/engine_18t.json'))
EXPORT_TO_KIT = Matrix.Rotation(math.pi / 2, 4, 'Z')
for g in engine['groups']:
    if g['name'].startswith('SM_Engine18T_Fixing'):
        continue
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(ROOT / 'Art/Vehicles/Octavia/Engine_18T/Export' / g['file']), axis_forward='-Y', axis_up='Z')
    p = g['pivot_cm']
    for o in set(bpy.data.objects) - before:
        for c in list(o.users_collection):
            c.objects.unlink(o)
        ref.objects.link(o)
        o.matrix_world = EXPORT_TO_KIT @ Matrix.Translation(Vector((p[0], -p[1], p[2])) * 0.01) @ o.matrix_world
        o.display_type = 'WIRE'
        o.hide_render = True
        o.name = 'REF_' + o.name
susp = ROOT / 'octavia suspension/Octavia1U_Suspension.blend'
if susp.exists():
    with bpy.data.libraries.load(str(susp)) as (src, dst):
        dst.objects = [n for n in src.objects if not n.startswith('UCX_')]
    for o in dst.objects:
        if o is None:
            continue
        ref.objects.link(o)
        if o.type == 'MESH':
            o.display_type = 'WIRE'
            o.hide_render = True
        o.name = 'REF_SUSP_' + o.name

# Drop everything that is not the kit or the reference.
keep = set(kit_body.objects) | set(inner.objects) | set(ref.objects)
for o in list(bpy.data.objects):
    if o not in keep:
        bpy.data.objects.remove(o, do_unlink=True)
for c in list(bpy.data.collections):
    if c not in (kit_body, inner, ref):
        bpy.data.collections.remove(c)
bpy.ops.outliner.orphans_purge(do_recursive=True)


ONLY = sys.argv[sys.argv.index('--') + 3]


def save_kit(name, keep_collection, drop_collection):
    # One .blend and one .fbx for this kit, with the reference collection; the other kit's objects deleted.
    for o in list(drop_collection.all_objects):
        bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.collections.remove(drop_collection)
    bpy.ops.outliner.orphans_purge(do_recursive=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / (name + '.blend')), copy=True)
    bpy.ops.object.select_all(action='DESELECT')
    for o in keep_collection.all_objects:
        o.select_set(True)
    bpy.ops.export_scene.fbx(filepath=str(OUT / (name + '.fbx')), use_selection=True, object_types={'MESH', 'EMPTY'},
        apply_unit_scale=True, add_leaf_bones=False, bake_anim=False, mesh_smooth_type='FACE')


if ONLY == 'body':
    save_kit('Octavia1U_Body', kit_body, inner)
else:
    save_kit('Octavia1U_Interior', inner, kit_body)
manifest['body_triangles'] = sum(e['triangles'] for e in manifest['body'])
manifest['interior_triangles'] = sum(e['triangles'] for e in manifest['interior'])
(OUT / 'body_manifest.json').write_text(json.dumps(manifest, indent=1), encoding='utf-8')
print('KIT_DONE body', len(manifest['body']), manifest['body_triangles'], 'interior', len(manifest['interior']), manifest['interior_triangles'])
for e in manifest['body'] + manifest['interior']:
    print('KIT', e['name'], e['pivot_m'], e['triangles'], e.get('materials'))
