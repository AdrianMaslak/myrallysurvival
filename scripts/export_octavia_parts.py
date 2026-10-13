"""Export the wheeled Octavia prototype to FBX parts with the model's own face normals.
Run: blender --background --factory-startup --python scripts/export_octavia_parts.py

Replaces the export step of prepare_octavia_prototype.py, which recalculated normals "outside" on
joined copies and flipped the headliner, door cards, much of the body and the dashboard (Unreal culls
back faces, so they vanished from inside). The .blend file is read, never saved.
Also exports the steering wheel as its own part with its column axis, so it can turn in game, and
the front and rear bumpers as parts that can come off in a crash. Every part that crushes its own way
in game (Damage/CrushProfiles.h) is its own mesh: bolt-on wings, lamp units, roof, windscreen, bumper
bars, interior, engine (with a stand-in block from the hidden alternatives) and exhaust; the chassis keeps the
body shell, underbody, heat shield, tank, subframes, steering rack and anti-roll bars. The jack, a water can
and an oil bottle for the boot kit are built here. Each corner's moving suspension parts are their own meshes too, with
the points they hang from and the points that move with the wheel (assembly.json anchor_*_cm).
Every mesh is written with two UV channels, the model's own and the paint's Livery map.
Writes Art/Vehicles/Octavia/Prototype/Export/*.fbx and assembly.json (Unreal frame: X forward, Y right,
Z up, cm, origin at ground below the car centre).
"""
import json
import math
from pathlib import Path
import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Art/Vehicles/Octavia/Prototype/Octavia_With_Wheels.blend"
EXPORT = ROOT / "Art/Vehicles/Octavia/Prototype/Export"
CATEGORIES = {"01_Body", "02_Interior", "03_Engine", "04_Suspension"}
# Parts that hinge or come off in crashes are separate meshes. Mirrors are mounted on the front doors.
PANEL_GROUPS = {
    "Door_FL": {"door_FL", "doorglass_FL", "doorpanel_FL", "doortrim_FL", "mirror_L"},
    "Door_FR": {"door_FR", "doorglass_FR", "doorpanel_FR", "doortrim_FR", "mirror_R"},
    "Door_RL": {"door_RL", "doorglass_RL", "doorpanel_RL", "doortrim_RL"},
    "Door_RR": {"door_RR", "doorglass_RR", "doorpanel_RR", "doortrim_RR"},
    "Bonnet": {"FL_hood"},
    "Trunk": {"trunk", "trunkglass", "spoiler", "lettering_R", "lettering_vRS_R", "logo_R"},
    "Radiator": {"radiator", "radiator_fan_L", "radiator_fan_R"},
    # Plastic covers with the grille and badge; the steel bumper bars stay on the chassis.
    "FrontBumper": {"FL_bumper_F_a", "FL_grille", "lettering_vRS_F"},
    "RearBumper": {"FL_bumper_R_a"},
    "SteeringWheel": {"steering_wheel_c"},
    # Bolt-on front wings, and the lamp units (with the corner indicators), which can come off.
    "Fender_FL": {"fender_FL"},
    "Fender_FR": {"fender_FR"},
    "Headlight_L": {"FLheadlight_L", "FLheadlight_frame_L", "FLheadlight_glass_L", "FLheadlight_foglight_L",
                    "signal_L", "signal_frame_L", "signal_glass_L"},
    "Headlight_R": {"FLheadlight_R", "FLheadlight_frame_R", "FLheadlight_glass_R", "FLheadlight_foglight_R",
                    "signal_R", "signal_frame_R", "signal_glass_R"},
    "Taillight_L": {"FL_taillight_L", "FL_taillight_frame_L", "FL_taillight_glass_L"},
    "Taillight_R": {"FL_taillight_R", "FL_taillight_frame_R", "FL_taillight_glass_R"},
    # Fixed to the shell but crushing their own way.
    "Roof": {"roof_a", "antenna_limo"},
    "Windshield": {"windshield"},
    "BumperBar_F": {"bumperbar_F"},
    "BumperBar_R": {"bumperbar_R"},
    # Middle pipe and silencer, which tear from their rear hangers and hang from the front one.
    "Exhaust": {"exhaust_mid", "standard_exhaust_muffler_vRS"},
    # Engine modules that bolt on and off (docs/DAMAGE.md, Engine modules); the block, bay, hoses and
    # gearbox stay in the Engine part.
    "EngineCover": {"enginecover_18T"},
    "AirFilter": {"airfilter_turbo"},
    "Intake": {"engine_i4_intake_turbo"},
    "ExhaustManifold": {"engine_i4_turbo_manifold"},
    "Turbo": {"engine_i4_turbo"},
    "Intercooler": {"intercooler"},
    "Alternator": {"engine_i4_commonparts"},
}
# Whatever else is in these collections becomes one part each (doors keep their cards and trim, the
# radiator its fans); the exhaust heat shield and fuel tank stay with the chassis.
COLLECTION_PARTS = {"02_Interior": "Interior", "03_Engine": "Engine"}
CHASSIS_GROUPS = {"standard_exhaust_heatshield", "fueltank"}
# The selected 1.8T set has no engine block or head; the hidden alternatives' four-cylinder block stands
# in. It is stored turned like the 4x4 rear and stood upright lands under the engine cover, beside the
# gearbox, so it needs no shift.
ENGINE_STAND_IN = {"engine_i4_sohc"}
# Source model is -Y forward; exports are +X forward.
ALIGN = Matrix.Rotation(math.pi / 2, 4, "Z")

# Suspension. The rear is the model's 4x4 multi-link, which it stores among the hidden alternatives
# turned 90 degrees about the car's width: it is stood upright and slid so its hubs sit on the rear wheel
# centres, replacing the torsion beam. The rear springs and dampers come from the torsion-beam set.
ALT_UPRIGHT = Matrix.Rotation(-math.pi / 2, 4, "X")
REAR_LINKS = {"hub_R", "lowerarm_R", "upperarm_R", "trailingarm_R", "subframe_R", "swaybar_R", "halfshaft_4x4_R"}
DROPPED = {"torsionbeam", "hub_R_torsionbeam"}
STATIC_REAR = {"subframe_R", "swaybar_R"}
# name: (source groups, side "L"/"R" or None for whole objects, role). Roles: hub (everything but the
# brake disc), disc (the disc, which spins), strut (fixed top, moving bottom, telescopes), link (fixed
# inner end, moving outer end), trailing (fixed front end, moving rear end).
SUSPENSION = {}
for corner, side in (("FL", "L"), ("FR", "R")):
    SUSPENSION[f"Strut_{corner}"] = ([f"strut_{corner}"], None, "strut")
    SUSPENSION[f"Hub_{corner}"] = ([f"hub_{corner}"], None, "hub")
    SUSPENSION[f"Disc_{corner}"] = ([f"hub_{corner}"], None, "disc")
    SUSPENSION[f"LowerArm_{corner}"] = ([f"lowerarm_{corner}"], None, "link")
    SUSPENSION[f"Driveshaft_{corner}"] = ([f"halfshaft_{side}"], None, "link")
    SUSPENSION[f"TieRod_{corner}"] = (["tierod_F"], side, "link")
for corner, side in (("RL", "L"), ("RR", "R")):
    SUSPENSION[f"Strut_{corner}"] = (["spring_R_torsionbeam", "shock_R_torsionbeam"], side, "strut")
    SUSPENSION[f"Hub_{corner}"] = (["hub_R"], side, "hub")
    SUSPENSION[f"Disc_{corner}"] = (["hub_R"], side, "disc")
    SUSPENSION[f"LowerArm_{corner}"] = (["lowerarm_R"], side, "link")
    SUSPENSION[f"UpperArm_{corner}"] = (["upperarm_R"], side, "link")
    SUSPENSION[f"TrailingArm_{corner}"] = (["trailingarm_R"], side, "trailing")
    SUSPENSION[f"Driveshaft_{corner}"] = (["halfshaft_4x4_R"], side, "link")
SUSPENSION_SOURCES = {g for groups, _, _ in SUSPENSION.values() for g in groups}

bpy.ops.wm.open_mainfile(filepath=str(SOURCE))


def to_unreal(v):
    return [-v.y * 100, -v.x * 100, v.z * 100]


def livery_layers(mesh):
    # Every exported mesh carries two UV channels: the model's own map, then the Livery map that the paint
    # material reads in Unreal (scripts/add_livery_uvs.py; all zero where a mesh has none). Same names and
    # order everywhere, so joined parts line up.
    layers = mesh.uv_layers
    if not len(layers):
        layers.new(name="UVMap")
    first = layers[0].name
    for layer in [l for l in layers if l.name not in (first, "Livery")]:
        layers.remove(layer)
    if "Livery" not in layers:
        layers.new(name="Livery", do_init=False)
    layers[0].name = "UVMap"


def world_points(objects):
    return [o.matrix_world @ v.co for o in objects for v in o.data.vertices]


def bounds(objects):
    pts = world_points(objects)
    lo = Vector(tuple(min(p[i] for p in pts) for i in range(3)))
    hi = Vector(tuple(max(p[i] for p in pts) for i in range(3)))
    return lo, hi


def group_of(o):
    return o.get("source_part_group", "").removeprefix("octavia1u_")


visible = [o for o in bpy.data.objects
           if o.type == "MESH" and o.data.polygons and any(c.name in CATEGORIES for c in o.users_collection)
           and group_of(o) not in SUSPENSION_SOURCES | DROPPED]
by_group = {}
for o in bpy.data.objects:
    if o.type == "MESH" and o.data.polygons:
        by_group.setdefault(group_of(o), []).append(o)
wheel_at = {o.name.removeprefix("Wheel_"): o.location.copy() for o in bpy.data.objects if o.name.startswith("Wheel_") and o.type == "MESH"}


def islands(bm):
    # Connected pieces of a mesh, as lists of faces.
    seen, pieces = set(), []
    for f in bm.faces:
        if f.index in seen:
            continue
        stack, faces = [f], []
        seen.add(f.index)
        while stack:
            cur = stack.pop()
            faces.append(cur)
            for e in cur.edges:
                for g in e.link_faces:
                    if g.index not in seen:
                        seen.add(g.index)
                        stack.append(g)
        pieces.append(faces)
    return pieces


def is_disc(faces):
    # A brake disc face: thin across the car (Blender X) and large in the other two directions.
    pts = [v.co for f in faces for v in f.verts]
    size = [max(p[i] for p in pts) - min(p[i] for p in pts) for i in range(3)]
    return size[0] < 0.025 and max(size[1], size[2]) > 0.14


def baked(o, matrix, keep):
    # A world-space copy of o (identity transform) keeping only the faces keep() accepts, or None.
    mesh = o.data.copy()
    mesh.transform(matrix)
    if matrix.determinant() < 0:
        mesh.flip_normals()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.faces.ensure_lookup_table()
    doomed = [f for piece in islands(bm) for f in piece if not keep(f, piece)]
    bmesh.ops.delete(bm, geom=doomed, context="FACES")
    bm.to_mesh(mesh)
    bm.free()
    if not mesh.polygons:
        return None
    # The hidden alternatives kept the source model's own materials; mechanical parts share one.
    for i, material in enumerate(mesh.materials):
        if material is None or not material.name.startswith("M_"):
            mesh.materials[i] = bpy.data.materials["M_Prototype_Mechanical"]
    copy = bpy.data.objects.new("SUSP_" + o.name, mesh)
    bpy.context.scene.collection.objects.link(copy)
    return copy


def solid_disc(name, flat):
    # The model's discs are two flat faces with a cut-out, paper thin and odd from most angles. Replace them
    # with a modelled disc of the same centre and size: a friction ring (24 mm front, 10 mm rear) and a hat
    # standing 35 mm out towards the wheel, with a centre hole. Blender X runs across the car.
    pts = [o.matrix_world @ v.co for o in flat for v in o.data.vertices]
    lo = Vector(tuple(min(p[i] for p in pts) for i in range(3)))
    hi = Vector(tuple(max(p[i] for p in pts) for i in range(3)))
    centre = (lo + hi) / 2
    radius = max(math.hypot(p.y - centre.y, p.z - centre.z) for p in pts)
    outboard = 1.0 if centre.x > 0 else -1.0
    front = name.split("_")[-1].startswith("F")
    thickness = 0.024 if front else 0.010
    hat_radius, hole_radius, hat_depth = 0.6 * radius, 0.2 * radius, 0.035
    for o in flat:
        bpy.data.objects.remove(o, do_unlink=True)
    segments = 40
    bm = bmesh.new()

    def ring(x, r):
        return [bm.verts.new((centre.x + x, centre.y + r * math.cos(2 * math.pi * k / segments),
                              centre.z + r * math.sin(2 * math.pi * k / segments))) for k in range(segments)]

    def bridge(a, b):
        for k in range(segments):
            bm.faces.new((a[k], a[(k + 1) % segments], b[(k + 1) % segments], b[k]))

    half = outboard * thickness / 2
    out_in, out_out = ring(-half, radius), ring(half, radius)
    hat_in, hat_out = ring(-half, hat_radius), ring(half, hat_radius)
    cap, hole = ring(half + outboard * hat_depth, hat_radius), ring(half + outboard * hat_depth, hole_radius)
    bridge(out_in, out_out)      # outer rim
    bridge(out_out, hat_out)     # outboard friction face
    bridge(hat_in, out_in)       # inboard friction face
    bridge(hat_out, cap)         # hat wall
    bridge(cap, hole)            # hat face round the centre hole
    bridge(hat_in, hat_out)      # inside of the ring
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new("Disc_" + name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(bpy.data.materials["M_Prototype_Mechanical"])
    disc = bpy.data.objects.new("SUSP_" + name, mesh)
    bpy.context.scene.collection.objects.link(disc)
    return disc


# Stand the 4x4 rear upright, then slide it so the middle of its discs meets the rear wheel centres.
def rear_matrix(o):
    return ALT_UPRIGHT @ o.matrix_world


disc_points = [rear_matrix(o) @ v.co for o in by_group.get("hub_R", []) for v in o.data.vertices]
probe = bmesh.new()
for o in by_group.get("hub_R", []):
    m = o.data.copy()
    m.transform(rear_matrix(o))
    probe.from_mesh(m)
disc_centres = [Vector(tuple(sum(v.co[i] for f in piece for v in f.verts) / sum(len(f.verts) for f in piece) for i in range(3)))
                for piece in islands(probe) if is_disc(piece)]
probe.free()
rear_wheels = [wheel_at["RL"], wheel_at["RR"]]
target = sum(rear_wheels, Vector()) / 2
found = sum(disc_centres, Vector()) / len(disc_centres)
REAR_SHIFT = Matrix.Translation(Vector((0.0, target.y - found.y, target.z - found.z)))
print("REAR_SHIFT_CM", [round(x * 100, 1) for x in (target - found)], "from", len(disc_centres), "disc faces")


def source_matrix(o):
    return REAR_SHIFT @ rear_matrix(o) if group_of(o) in REAR_LINKS else o.matrix_world


suspension_parts = {}
anchors = {}
for name, (groups, side, role) in SUSPENSION.items():
    def keep(f, piece, side=side, role=role):
        c = f.calc_center_median()
        if side == "L" and c.x <= 0 or side == "R" and c.x > 0:
            return False
        if role == "disc":
            return is_disc(piece)
        if role == "hub":
            return not is_disc(piece)
        return True
    parts = [p for g in groups for o in by_group.get(g, []) for p in [baked(o, source_matrix(o), keep)] if p]
    if not parts:
        raise RuntimeError("Empty suspension part: " + name)
    if role == "disc":
        parts = [solid_disc(name, parts)]
    suspension_parts[name] = parts
    pts = [o.matrix_world @ v.co for o in parts for v in o.data.vertices]
    corner = name.split("_")[-1]
    if role in ("hub", "disc"):
        fixed = moving = wheel_at[corner].copy()
    else:
        # Mean of the points within 3 cm of each end along the part's own direction.
        if role == "strut":
            key = lambda p: p.z
            fixed_end_high = True
        elif role == "trailing":
            key = lambda p: -p.y
            fixed_end_high = True
        else:
            key = lambda p: -abs(p.x)
            fixed_end_high = True
        hi = max(key(p) for p in pts)
        lo = min(key(p) for p in pts)
        top = [p for p in pts if key(p) >= hi - 0.03]
        bottom = [p for p in pts if key(p) <= lo + 0.03]
        fixed = sum(top, Vector()) / len(top)
        moving = sum(bottom, Vector()) / len(bottom)
    anchors[name] = (role, corner, fixed, moving)
static_rear = [p for g in STATIC_REAR for o in by_group.get(g, []) for p in [baked(o, source_matrix(o), lambda f, piece: True)] if p]
engine_block = [p for g in ENGINE_STAND_IN for o in by_group.get(g, []) for p in [baked(o, ALT_UPRIGHT @ o.matrix_world, lambda f, piece: True)] if p]
if not engine_block:
    raise RuntimeError("Engine stand-in block not found")

# A trolley jack for wheel changes, built here (the model has none): base, screw column, saddle and handle,
# 40 cm tall at full lift, pivot at the middle of its base on the ground.
jack_bm = bmesh.new()
for centre, size in (((0, 0, 0.015), (0.14, 0.30, 0.03)), ((0, 0, 0.18), (0.05, 0.05, 0.30)),
                     ((0, 0, 0.36), (0.08, 0.12, 0.04)), ((0, -0.30, 0.05), (0.02, 0.40, 0.02))):
    bmesh.ops.create_cube(jack_bm, size=1.0, matrix=Matrix.Translation(centre) @ Matrix.Diagonal((*size, 1.0)))
jack_mesh = bpy.data.meshes.new("Jack")
jack_bm.to_mesh(jack_mesh)
jack_bm.free()
jack_mesh.materials.append(bpy.data.materials["M_Prototype_Mechanical"])
jack = bpy.data.objects.new("SUSP_Jack", jack_mesh)
bpy.context.scene.collection.objects.link(jack)

# A five-litre water can for the radiator, built here too: body, carrying handle on two posts and a spout,
# 22 cm long, 13 wide, 30 tall; pivot at the middle of its base.
can_bm = bmesh.new()
for centre, size in (((0, 0, 0.125), (0.13, 0.22, 0.25)), ((0, 0.03, 0.285), (0.025, 0.12, 0.02)),
                     ((0, -0.025, 0.262), (0.02, 0.02, 0.025)), ((0, 0.085, 0.262), (0.02, 0.02, 0.025)),
                     ((0, -0.08, 0.27), (0.04, 0.04, 0.04))):
    bmesh.ops.create_cube(can_bm, size=1.0, matrix=Matrix.Translation(centre) @ Matrix.Diagonal((*size, 1.0)))
can_mesh = bpy.data.meshes.new("WaterCan")
can_bm.to_mesh(can_mesh)
can_bm.free()
can_mesh.materials.append(bpy.data.materials["M_Prototype_Mechanical"])
can = bpy.data.objects.new("SUSP_WaterCan", can_mesh)
bpy.context.scene.collection.objects.link(can)

# A four-litre bottle of engine oil for the boot kit: 16 cm long, 9 wide, 26 tall with its cap.
bottle_bm = bmesh.new()
for centre, size in (((0, 0, 0.12), (0.09, 0.16, 0.24)), ((0, -0.05, 0.25), (0.035, 0.035, 0.03))):
    bmesh.ops.create_cube(bottle_bm, size=1.0, matrix=Matrix.Translation(centre) @ Matrix.Diagonal((*size, 1.0)))
bottle_mesh = bpy.data.meshes.new("OilBottle")
bottle_bm.to_mesh(bottle_mesh)
bottle_bm.free()
bottle_mesh.materials.append(bpy.data.materials["M_Prototype_Mechanical"])
bottle = bpy.data.objects.new("SUSP_OilBottle", bottle_mesh)
bpy.context.scene.collection.objects.link(bottle)

# The model has no starter motor or engine computer: a starter (motor barrel with its solenoid on top,
# lying along the crankshaft under the air box, behind the gearbox's front) and an ECU (a flat box on the
# left of the bay, by the bulkhead), built here in the source frame.
def boxes(name, parts):
    bm = bmesh.new()
    for centre, size in parts:
        bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Translation(centre) @ Matrix.Diagonal((*size, 1.0)))
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(bpy.data.materials["M_Prototype_Mechanical"])
    obj = bpy.data.objects.new("SUSP_" + name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def barrel(name, parts):
    # Cylinders along the source X axis (across the car): (centre, radius, length).
    bm = bmesh.new()
    for centre, radius, length in parts:
        bmesh.ops.create_cone(bm, cap_ends=True, segments=16, radius1=radius, radius2=radius, depth=length,
            matrix=Matrix.Translation(centre) @ Matrix.Rotation(math.pi / 2, 4, "Y"))
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(bpy.data.materials["M_Prototype_Mechanical"])
    obj = bpy.data.objects.new("SUSP_" + name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


# A first-aid kit for the boot: a white box, 26 x 18 x 9 cm, with a red cross on its lid; pivot at the
# middle of its base.
aid_bm = bmesh.new()
bmesh.ops.create_cube(aid_bm, size=1.0, matrix=Matrix.Translation((0, 0, 0.045)) @ Matrix.Diagonal((0.18, 0.26, 0.09, 1.0)))
aid_mesh = bpy.data.meshes.new("FirstAid")
aid_bm.to_mesh(aid_mesh)
aid_bm.free()
aid_mesh.materials.append(bpy.data.materials.get("M_Prototype_Paint") or bpy.data.materials["M_Prototype_Mechanical"])
cross_bm = bmesh.new()
for size in ((0.035, 0.14, 0.004), (0.12, 0.04, 0.004)):
    bmesh.ops.create_cube(cross_bm, size=1.0, matrix=Matrix.Translation((0, 0, 0.092)) @ Matrix.Diagonal((*size, 1.0)))
cross_mesh = bpy.data.meshes.new("FirstAidCross")
cross_bm.to_mesh(cross_mesh)
cross_bm.free()
cross_mesh.materials.append(bpy.data.materials.get("M_Prototype_Taillamp") or bpy.data.materials["M_Prototype_Mechanical"])
first_aid = bpy.data.objects.new("SUSP_FirstAid", aid_mesh)
first_aid_cross = bpy.data.objects.new("SUSP_FirstAidCross", cross_mesh)
bpy.context.scene.collection.objects.link(first_aid)
bpy.context.scene.collection.objects.link(first_aid_cross)

# A bottle of slivovica (round, with a neck and cap, 30 cm) and a carton of cigarettes, for the village's
# vices; pivots at the middle of their bases.
def upright(name, parts, material):
    # Cylinders standing up: (centre, radius, height).
    bm = bmesh.new()
    for centre, radius, height in parts:
        bmesh.ops.create_cone(bm, cap_ends=True, segments=16, radius1=radius, radius2=radius, depth=height,
            matrix=Matrix.Translation(centre))
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(bpy.data.materials.get(material) or bpy.data.materials["M_Prototype_Mechanical"])
    obj = bpy.data.objects.new("SUSP_" + name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


slivovica = upright("Slivovica", [((0, 0, 0.1), 0.04, 0.2), ((0, 0, 0.235), 0.014, 0.07), ((0, 0, 0.278), 0.016, 0.016)], "M_Prototype_Glass_SolidPreview")
carton_bm = bmesh.new()
bmesh.ops.create_cube(carton_bm, size=1.0, matrix=Matrix.Translation((0, 0, 0.025)) @ Matrix.Diagonal((0.09, 0.27, 0.05, 1.0)))
carton_mesh = bpy.data.meshes.new("Cigarettes")
carton_bm.to_mesh(carton_mesh)
carton_bm.free()
carton_mesh.materials.append(bpy.data.materials.get("M_Prototype_Paint") or bpy.data.materials["M_Prototype_Mechanical"])
cigarettes = bpy.data.objects.new("SUSP_Cigarettes", carton_mesh)
bpy.context.scene.collection.objects.link(cigarettes)

starter = barrel("Starter", [((0.20, -1.30, 0.44), 0.045, 0.18), ((0.18, -1.30, 0.51), 0.022, 0.10)])
ecu = boxes("ECU", [((0.50, -1.24, 0.74), (0.16, 0.12, 0.04)), ((0.42, -1.24, 0.74), (0.02, 0.06, 0.025))])

wheels = sorted((o for o in bpy.data.objects if o.name.startswith("Wheel_") and o.type == "MESH"), key=lambda o: o.name)
if len(wheels) != 4:
    raise RuntimeError("Expected four placeholder wheels.")

buckets = {"Chassis": []}
for name in PANEL_GROUPS:
    buckets[name] = []
for name in COLLECTION_PARTS.values():
    buckets[name] = []
for name, parts in suspension_parts.items():
    buckets[name] = parts
for o in visible:
    group = o.get("source_part_group", "").removeprefix("octavia1u_")
    bucket = next((k for k, v in PANEL_GROUPS.items() if group in v), None)
    if bucket is None and group not in CHASSIS_GROUPS:
        bucket = next((p for c, p in COLLECTION_PARTS.items() if any(u.name == c for u in o.users_collection)), None)
    buckets[bucket or "Chassis"].append(o)
buckets["Chassis"].extend(static_rear)
buckets["Engine"].extend(engine_block)
buckets["Jack"] = [jack]
buckets["WaterCan"] = [can]
buckets["OilBottle"] = [bottle]
buckets["FirstAid"] = [first_aid, first_aid_cross]
buckets["Slivovica"] = [slivovica]
buckets["Cigarettes"] = [cigarettes]
buckets["Starter"] = [starter]
buckets["ECU"] = [ecu]
for wheel in wheels:
    buckets[wheel.name] = [wheel]

scene = bpy.context.scene
export_collection = bpy.data.collections.new("TEMP_Export")
scene.collection.children.link(export_collection)
manifest = {
    "note": "Static mesh parts with source face normals; tyre and steering data for the game pawn.",
    "frame": "Unreal cm: X forward, Y right, Z up, origin at ground below the car centre.",
    "parts": [],
}

for name, objects in buckets.items():
    if not objects:
        raise RuntimeError("Empty export part: " + name)
    if name.startswith("Wheel_"):
        pivot = objects[0].location.copy()
    elif name in anchors:
        # Hub and disc turn about the wheel centre; links swing about their fixed end.
        pivot = anchors[name][2].copy()
    elif name in ("Chassis", "Jack", "WaterCan", "OilBottle", "FirstAid", "Slivovica", "Cigarettes"):
        pivot = Vector((0, 0, 0))
    elif name == "Exhaust":
        # Its front hanger: the front end of the pipe (the source model is -Y forward).
        pts = world_points(objects)
        front = min(p.y for p in pts)
        tip = [p for p in pts if p.y <= front + 0.03]
        pivot = sum(tip, Vector()) / len(tip)
    else:
        lo, hi = bounds(objects)
        pivot = (lo + hi) / 2
    copies = []
    for original in objects:
        mesh = original.data.copy()
        livery_layers(mesh)
        matrix = ALIGN @ Matrix.Translation(-pivot) @ original.matrix_world
        mesh.transform(matrix)
        # Baking a mirrored transform reverses winding; restore the source facing.
        if matrix.determinant() < 0:
            mesh.flip_normals()
        copy = bpy.data.objects.new("EXPORT_" + name, mesh)
        export_collection.objects.link(copy)
        copies.append(copy)
    bpy.ops.object.select_all(action="DESELECT")
    for copy in copies:
        copy.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    if len(copies) > 1:
        bpy.ops.object.join()
    export_obj = bpy.context.view_layer.objects.active
    export_obj.name = "SM_Octavia_" + name
    filename = export_obj.name + ".fbx"
    bpy.ops.export_scene.fbx(filepath=str(EXPORT / filename), use_selection=True,
        object_types={"MESH"}, axis_forward="-Y", axis_up="Z", apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL", bake_anim=False, use_mesh_modifiers=True,
        mesh_smooth_type="FACE", use_triangles=True, add_leaf_bones=False)
    lo, hi = bounds(objects)
    size = hi - lo
    entry = {
        "name": export_obj.name, "file": filename,
        "pivot_cm": to_unreal(pivot),
        "unreal_expected_pivot_cm": to_unreal(pivot),
        # Unreal axes: X = source Y extent, Y = source X extent.
        "dimensions_cm": [size.y * 100, size.x * 100, size.z * 100],
        "triangles": sum(len(p.vertices) - 2 for p in export_obj.data.polygons),
    }
    if name in anchors:
        role, corner, fixed, moving = anchors[name]
        entry.update({"role": role, "corner": corner, "anchor_fixed_cm": to_unreal(fixed), "anchor_moving_cm": to_unreal(moving)})
    if name == "SteeringWheel":
        # Column axis = direction of least spread of the wheel's vertices, pointing at the driver.
        pts = np.array([to_unreal(p) for p in world_points(objects)])
        _, _, axes = np.linalg.svd(pts - pts.mean(axis=0), full_matrices=False)
        axis = axes[2] / np.linalg.norm(axes[2])
        if axis[0] > 0:
            axis = -axis
        entry["column_axis"] = [float(a) for a in axis]
    manifest["parts"].append(entry)
    bpy.data.objects.remove(export_obj, do_unlink=True)

(EXPORT / "assembly.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
for part in manifest["parts"]:
    print("EXPORT", part["name"], [round(v, 1) for v in part["pivot_cm"]], [round(v, 1) for v in part["dimensions_cm"]], part.get("column_axis", ""),
          part.get("role", ""), [round(v, 1) for v in part.get("anchor_moving_cm", [])])
print(f"EXPORT_DONE {len(manifest['parts'])} parts")
