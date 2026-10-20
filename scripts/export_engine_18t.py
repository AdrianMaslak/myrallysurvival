"""Export the 1.8T 20V engine bay model (Art/Vehicles/Octavia/Engine_18T) into the Octavia, as FBX parts in the
car's Unreal frame (X forward, Y right, Z up, cm, origin at the ground below the car's middle).
Run: blender --background --factory-startup --python scripts/export_engine_18t.py

Placement (docs/OCTAVIA-VEHICLE.md, Engine): the model is real size, but the car model's engine bay is narrower
than a real Octavia's (78-90 cm free between the inner wings at engine height, a real one about a metre), so the
whole set is scaled to 88%. The crank sits at (150.7, 16.0, 38.1) cm: the driveshaft flanges at the height and
fore-aft place of the car's driveshaft joints, the engine and gearbox centred across the bay, and far enough
forward that the turbo clears the bulkhead (the crank pulley and gearbox end still touch the chassis rails a
few cm, low down).

Groups written (Export/SM_Engine18T_<Group>.fbx), each with its pivot:
- Engine: everything bolted to the engine and gearbox that is not a module, pivoting on the crank so the
  engine can rock on its mounts.
- Pulley: the crank pulley, pivoting on the crank's axis so it can spin.
- Modules (EngineCover, AirFilter, Intake, ExhaustManifold, Turbo, Intercooler, Alternator, Starter, ECU): the
  parts that unbolt in the game, each on its own origin; the intercooler with its two charge pipes (turbo to
  intercooler, intercooler to throttle). The engine computer sits where the car keeps it, by the
  bulkhead on the left. Every fastener that holds the module on is left out of the mesh (the game draws and
  turns its own) and written as a socket Fixing0, Fixing1 ... tagged "<kind> <tool>" in the game's terms
  (sockets 10/13/17/19 mm, screwdriver, hands); fasteners that only join pieces of the module (the throttle
  body's to the manifold) and the turbo's wastegate rod nut (the boost adjuster) stay on the mesh.
- Bay: battery and terminals, washer bottle, fuse box, the coolant expansion tank and its cap, on the body.
- Radiator: the radiator, the fan shroud and motors, and both radiator hoses, on the body (the car's radiator
  panel); FanL and FanR: each fan's blades and hub, pivoting on its own axis so it can spin.
Cooling placement: the model's own layout, each body-fixed group moved the least that clears the car's body,
front subframe and struts by a centimetre (COOLING_MOVES_CM, from a fit study against the car's
Octavia_With_Wheels.blend), the hoses and charge pipes bent to follow (bend()).
Collision hulls (UCX_) go with the groups that have them; the radiator and intercooler cores get a box each.
The model's Grime layer goes out as vertex colour.
Not exported yet: brake servo, catalytic converter, silencers and fuel tank (the car keeps its own).
Writes Export/engine_18t.json: each group's file, pivot and sockets (Unreal cm, direction the fastener backs
out), every fastener of each module with its tool, and the points the game needs (oil filler cap, battery
posts, crank) in the car frame.
"""
import json
import math
from pathlib import Path
import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Art/Vehicles/Octavia/Engine_18T/Octavia1U_1.8T_20V.blend"
EXPORT = ROOT / "Art/Vehicles/Octavia/Engine_18T/Export"
SCALE = 0.88
CRANK_CM = Vector((150.7, 16.0, 38.1))
# The model's frame (X the car's right, Y forward, Z up, m) into export space (X forward, Y left, Z up, m).
CRANK = Vector((CRANK_CM.x, -CRANK_CM.y, CRANK_CM.z)) * 0.01
PLACE = Matrix.Translation(CRANK) @ Matrix.Rotation(-math.pi / 2, 4, "Z") @ Matrix.Scale(SCALE, 4)
# Where the game keeps the engine computer (car frame, cm): by the bulkhead on the left.
ECU_AT_CM = Vector((126.0, -49.5, 74.0))

MODULES = {
    "EngineCover": ["EngineCover"],
    "AirFilter": ["AirFilterBox"],
    "Intake": ["IntakeManifold", "ThrottleBody"],
    "ExhaustManifold": ["ExhaustManifold"],
    "Turbo": ["Turbocharger"],
    "Intercooler": ["Intercooler", "IntercoolerPipe_Hot", "IntercoolerPipe_Cold"],
    "Alternator": ["Alternator"],
    "Starter": ["StarterMotor"],
    "ECU": ["ECU"],
}
# Fasteners that do not hold the module on: they join its own pieces, or set the boost.
NOT_FIXINGS = {"ThrottleBody", "Turbocharger_Nut_10"}


def game_tag(fastener):
    # The game's kind and tool for a model fastener: only sockets (10/13/17/19 mm), a screwdriver and hands.
    # A 16 mm fastener takes the 17 mm socket (the kit has no 16); other sizes and drives the 13 mm.
    kind = {"bolt": "Bolt", "banjobolt": "Bolt", "allen": "Bolt", "nut": "Nut", "screw": "Screw", "clip": "Clip",
            "clamp": "Screw", "pin": "Clip", "rubbermount": "Clip"}.get(fastener["kind"], "Bolt")
    tool = fastener["tool"].lower()
    if "hands" in tool:
        game_tool = "Hands"
    elif "screwdriver" in tool or "torx" in tool:
        game_tool = "Screwdriver"
    elif tool.startswith("16 mm"):
        game_tool = "Spanner17"
    else:
        game_tool = next((f"Spanner{s}" for s in ("10", "13", "17", "19") if tool.startswith(s + " mm")), "Spanner13")
    return f"{kind} {game_tool}"
BAY = ["Battery", "BatteryTerminal_Positive", "BatteryTerminal_Negative", "WasherBottle", "FuseBox", "CoolantExpansionTank",
       "CoolantTankCap"]
RADIATOR = ["Radiator", "RadiatorFan", "RadiatorHose_Upper", "RadiatorHose_Lower"]
NOT_YET = {"BrakeServo", "CatalyticConverter", "CentreSilencer", "RearSilencer", "FuelTank"}
# How far each body-fixed cooling group moves from the model's place (Unreal cm): the radiator and fan back and up
# over the front subframe's crossmember (where the car's own radiator sat, 30-75 cm up), the intercooler forward
# and in from the undertray, the coolant tank in from the right strut tower.
COOLING_MOVES_CM = {"Radiator": (-6.0, 0.0, 16.0), "Intercooler": (4.0, 12.0, 0.0), "Tank": (2.0, -10.0, 0.0)}
COOLING_PARTS = {"Radiator": ["Radiator", "RadiatorFan"], "Intercooler": ["Intercooler"], "Tank": ["CoolantExpansionTank", "CoolantTankCap"]}
# Hoses and pipes between a moved group and the engine: the end at the group follows it.
COOLING_PIPES = {"Radiator": ["RadiatorHose_Upper", "RadiatorHose_Lower"], "Intercooler": ["IntercoolerPipe_Hot", "IntercoolerPipe_Cold"]}
# Each fan's axis in the fan shroud's own frame (m): the blades and hub round it spin.
FAN_AXES = {"FanL": Vector((-0.14, 0.0, 0.0)), "FanR": Vector((0.14, 0.0, 0.0))}

bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
manifest = json.loads((ROOT / "Art/Vehicles/Octavia/Engine_18T/parts_manifest.json").read_text(encoding="utf-8"))
part_names = [p["name"] for p in manifest["parts"]]
in_modules = {p for parts in MODULES.values() for p in parts}
ENGINE = [n for n in part_names if n not in in_modules and n not in BAY and n not in NOT_YET and n not in RADIATOR
          and n != "CrankPulley"]

for ob in [o for o in bpy.data.objects if o.parent is None]:
    ob.matrix_world = PLACE @ ob.matrix_world
bpy.context.view_layer.update()


def export_offset(cm):
    # An offset in Unreal cm, in export space (X forward, Y left, m).
    return Vector((cm[0], -cm[1], cm[2])) * 0.01


def world_vertices(names):
    return [bpy.data.objects[n].matrix_world @ v.co for n in names for v in bpy.data.objects[n].data.vertices]


def bend(pipe, group):
    # The pipe's two ends are its two farthest-apart vertices; the one nearer the group moves with it, the other
    # stays on the engine, and every vertex in between moves by a smooth share of the offset (by how far along
    # from the fixed end it is). Its clamps, bolt points and hulls go with the share at their own place.
    ob = bpy.data.objects[pipe]
    offset = export_offset(COOLING_MOVES_CM[group])
    world = [ob.matrix_world @ v.co for v in ob.data.vertices]
    mid = sum(world, Vector()) / len(world)
    a = max(world, key=lambda w: (w - mid).length)
    b = max(world, key=lambda w: (w - a).length)
    near = world_vertices(COOLING_PARTS[group])
    gap = lambda p: min((p - q).length for q in near)
    fixed, moved = (a, b) if gap(a) > gap(b) else (b, a)
    along = moved - fixed

    def share(p):
        t = max(0.0, min(1.0, (p - fixed).dot(along) / along.length_squared))
        return t * t * (3 - 2 * t)
    inverse = ob.matrix_world.inverted()
    for v, w in zip(ob.data.vertices, world):
        v.co = inverse @ (w + offset * share(w))
    for child in [c for c in bpy.data.objects if c.parent == ob]:
        centre = child.matrix_world.translation if child.type != "MESH" else \
            child.matrix_world @ (sum((Vector(c) for c in child.bound_box), Vector()) / 8)
        child.matrix_world = Matrix.Translation(offset * share(centre)) @ child.matrix_world


for group, pipes in COOLING_PIPES.items():
    for pipe in pipes:
        bend(pipe, group)
for group, parts in COOLING_PARTS.items():
    for part in parts:
        ob = bpy.data.objects[part]
        ob.matrix_world = Matrix.Translation(export_offset(COOLING_MOVES_CM[group])) @ ob.matrix_world
bpy.context.view_layer.update()


def box_hull(part):
    # A box round the part's own mesh as its collision hull (the radiator and intercooler cores have none).
    ob = bpy.data.objects[part]
    lo = Vector([min(v.co[i] for v in ob.data.vertices) for i in range(3)])
    hi = Vector([max(v.co[i] for v in ob.data.vertices) for i in range(3)])
    corners = [Vector((x, y, z)) for x in (lo.x, hi.x) for y in (lo.y, hi.y) for z in (lo.z, hi.z)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    mesh = bpy.data.meshes.new("UCX_" + part + "_box")
    mesh.from_pydata(corners, [], faces)
    hull = bpy.data.objects.new("UCX_" + part + "_box", mesh)
    bpy.context.scene.collection.objects.link(hull)
    hull.parent = ob
    hull.matrix_parent_inverse = Matrix()
    hull.matrix_world = ob.matrix_world.copy()


box_hull("Radiator")
box_hull("Intercooler")


def split_fans():
    # The blades and hub of each fan (pieces of the shroud mesh centred on a fan's axis, in front of its motor)
    # become their own objects, so they can spin; the shroud and motors stay.
    import bmesh
    fan = bpy.data.objects["RadiatorFan"]
    bm = bmesh.new()
    bm.from_mesh(fan.data)
    bm.verts.ensure_lookup_table()
    seen, islands = set(), []
    for v in bm.verts:
        if v.index in seen:
            continue
        stack, island = [v], []
        seen.add(v.index)
        while stack:
            a = stack.pop()
            island.append(a)
            for e in a.link_edges:
                b = e.other_vert(a)
                if b.index not in seen:
                    seen.add(b.index)
                    stack.append(b)
        islands.append(island)
    out = {}
    for name, axis in FAN_AXES.items():
        spin = []
        for island in islands:
            xs, ys, zs = [a.co.x for a in island], [a.co.y for a in island], [a.co.z for a in island]
            centre = Vector(((min(xs) + max(xs)) / 2, 0.0, (min(zs) + max(zs)) / 2))
            if (centre - axis).length < 0.01 and min(ys) > -0.012:
                spin += island
        keep = {a.index for a in spin}
        part = bm.copy()
        part.verts.ensure_lookup_table()
        bmesh.ops.delete(part, geom=[v for v in part.verts if v.index not in keep], context="VERTS")
        mesh = bpy.data.meshes.new(name)
        part.to_mesh(mesh)
        for m in fan.data.materials:
            mesh.materials.append(m)
        ob = bpy.data.objects.new(name, mesh)
        bpy.context.scene.collection.objects.link(ob)
        ob.matrix_world = fan.matrix_world.copy()
        out[name] = (ob, fan.matrix_world @ axis, len(keep))
    gone = set()
    for name, axis in FAN_AXES.items():
        for island in islands:
            xs, ys, zs = [a.co.x for a in island], [a.co.y for a in island], [a.co.z for a in island]
            centre = Vector(((min(xs) + max(xs)) / 2, 0.0, (min(zs) + max(zs)) / 2))
            if (centre - axis).length < 0.01 and min(ys) > -0.012:
                gone |= {a.index for a in island}
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index in gone], context="VERTS")
    bm.to_mesh(fan.data)
    return out


FANS = split_fans()


def to_unreal(v):
    return [round(v.x * 100, 3), round(-v.y * 100, 3), round(v.z * 100, 3)]


def children(part, prefix):
    return [o for o in bpy.data.objects if o.parent and o.parent.name == part and o.name.startswith(prefix)]


def fasteners_of(part):
    # Fastener meshes are named <Part>_<Kind>_<NN>; bolt points <Part>_BoltPoint_<NN>, matched by number.
    points = {o.name.rsplit("_", 1)[1]: o for o in children(part, part + "_BoltPoint_")}
    out = []
    for ob in bpy.data.objects:
        if ob.type != "MESH" or not ob.parent or ob.parent.name != part or ob.name.startswith("UCX_"):
            continue
        number = ob.name.rsplit("_", 1)[-1]
        if number in points:
            point = points[number]
            axis = (point.matrix_world.to_3x3() @ Vector((0, 0, 1))).normalized()
            out.append({"mesh": ob, "name": ob.name, "at": point.matrix_world.translation.copy(), "axis": axis,
                        "kind": ob.name.split("_")[-2].lower(), "tool": ob.get("tool", ""), "label": ob.get("label", "")})
    return out


def holding(fasteners):
    # The fasteners that hold the module on, in a stable order (the game's fixing order): along the car,
    # then across, then up.
    keep = [f for f in fasteners if f["name"] not in NOT_FIXINGS and f["mesh"].parent.name not in NOT_FIXINGS]
    if len(keep) > 16:
        raise RuntimeError("More than 16 fixings on one module")
    return sorted(keep, key=lambda f: (round(f["at"].x, 2), round(f["at"].y, 2), round(f["at"].z, 2)))


export_collection = bpy.data.collections.new("EXPORT")
bpy.context.scene.collection.children.link(export_collection)
out = {"scale": SCALE, "crank_cm": [CRANK_CM.x, CRANK_CM.y, CRANK_CM.z], "groups": [], "points": {}}


def export_group(name, parts, pivot, skip=(), move=Vector(), hull_parts=None):
    # Copies of each part's mesh and its fastener meshes (but those in skip), relative to the pivot; the hulls of
    # hull_parts only, if given.
    meshes, hulls = [], []
    for part in parts:
        ob = bpy.data.objects[part]
        sources = [ob] + [c for c in bpy.data.objects if c.parent == ob and c.type == "MESH"
                          and not c.name.startswith("UCX_") and c.name not in skip]
        for src in sources:
            data = src.data.copy()
            # Relative to its pivot wherever it is placed (move shifts the pivot, not the shape).
            data.transform(Matrix.Translation(-pivot) @ src.matrix_world)
            copy = bpy.data.objects.new("COPY_" + src.name, data)
            export_collection.objects.link(copy)
            meshes.append(copy)
        for hull in (children(part, "UCX_") if hull_parts is None or part in hull_parts else []):
            data = hull.data.copy()
            data.transform(Matrix.Translation(-pivot) @ hull.matrix_world)
            copy = bpy.data.objects.new("HULL", data)
            export_collection.objects.link(copy)
            hulls.append(copy)
    bpy.ops.object.select_all(action="DESELECT")
    for m in meshes:
        m.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    mesh_ob = bpy.context.view_layer.objects.active
    mesh_ob.name = "SM_Engine18T_" + name
    for i, h in enumerate(hulls):
        h.name = "UCX_%s_%02d" % (mesh_ob.name, i)
    bpy.ops.object.select_all(action="DESELECT")
    mesh_ob.select_set(True)
    for h in hulls:
        h.select_set(True)
    filename = mesh_ob.name + ".fbx"
    bpy.ops.export_scene.fbx(filepath=str(EXPORT / filename), use_selection=True, object_types={"MESH"},
        axis_forward="-Y", axis_up="Z", apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", bake_anim=False,
        use_mesh_modifiers=True, mesh_smooth_type="FACE", use_triangles=True, add_leaf_bones=False, colors_type="SRGB")
    lo = Vector([min(v.co[i] for v in mesh_ob.data.vertices) for i in range(3)])
    hi = Vector([max(v.co[i] for v in mesh_ob.data.vertices) for i in range(3)])
    size = hi - lo
    entry = {"name": mesh_ob.name, "file": filename, "pivot_cm": to_unreal(pivot), "hulls": len(hulls),
             "dimensions_cm": [round(size.x * 100, 2), round(size.y * 100, 2), round(size.z * 100, 2)],
             "triangles": sum(len(p.vertices) - 2 for p in mesh_ob.data.polygons), "parts": parts}
    for o in [mesh_ob] + hulls:
        bpy.data.objects.remove(o, do_unlink=True)
    return entry


EXPORT.mkdir(parents=True, exist_ok=True)
out["groups"].append(export_group("Engine", ENGINE, CRANK))
out["groups"].append(export_group("Pulley", ["CrankPulley"], bpy.data.objects["CrankPulley"].matrix_world.translation.copy()))
lo = Vector((1e9, 1e9, 1e9))
hi = -lo
for part in BAY:
    for v in bpy.data.objects[part].bound_box:
        w = bpy.data.objects[part].matrix_world @ Vector(v)
        lo = Vector((min(lo.x, w.x), min(lo.y, w.y), min(lo.z, w.z)))
        hi = Vector((max(hi.x, w.x), max(hi.y, w.y), max(hi.z, w.z)))
out["groups"].append(export_group("Bay", BAY, (lo + hi) / 2))
out["groups"].append(export_group("Radiator", RADIATOR, bpy.data.objects["Radiator"].matrix_world.translation.copy(), hull_parts={"Radiator"}))
for name, (ob, axis, count) in FANS.items():
    entry = export_group(name, [name], axis)
    entry["spin_axis"] = to_unreal(ob.matrix_world.to_3x3() @ Vector((0, 1, 0)) * 0.01)
    out["groups"].append(entry)
for module, parts in MODULES.items():
    pivot = bpy.data.objects[parts[0]].matrix_world.translation.copy()
    move = Vector()
    if module == "ECU":
        target = Vector((ECU_AT_CM.x, -ECU_AT_CM.y, ECU_AT_CM.z)) * 0.01
        move = target - pivot
    fasteners = [f for p in parts for f in fasteners_of(p)]
    chosen = holding(fasteners)
    entry = export_group(module, parts, pivot, skip={f["name"] for f in chosen}, move=move)
    entry["pivot_cm"] = to_unreal(pivot + move)
    entry["sockets"] = [{"name": "Fixing%d" % i, "location_cm": to_unreal(f["at"] - pivot), "facing": to_unreal(f["axis"] * 0.01),
                         "tag": game_tag(f), "model_fastener": f["name"], "tool": f["tool"]} for i, f in enumerate(chosen)]
    entry["fasteners"] = [{"name": f["name"], "kind": f["kind"], "tool": f["tool"], "label": f["label"],
                           "location_cm": to_unreal(f["at"] - pivot), "facing": to_unreal(f["axis"] * 0.01)} for f in fasteners]
    out["groups"].append(entry)
# The fixing heads the game draws (URallyPanelComponent::UpdateFixingVisuals), one of each kind from the
# model at full size: a 10 mm bolt, a 13 mm nut, the cover's quarter-turn screw, an air box clip. Each in its
# bolt point's frame: base on the surface at the origin, backing out along +Z (cm in Unreal).
HEADS = {"Bolt": "IntakeManifold_Bolt_01", "Nut": "ExhaustManifold_Nut_01", "Screw": "EngineCover_Screw_01", "Clip": "AirFilterBox_Clip_01"}
out["heads"] = {}
for kind, name in HEADS.items():
    ob = bpy.data.objects[name]
    number = name.rsplit("_", 1)[1]
    point = bpy.data.objects[ob.parent.name + "_BoltPoint_" + number]
    # In the bolt point's own frame the engine's placement and scale cancel out: the head at full size.
    frame = point.matrix_world.inverted()
    data = ob.data.copy()
    data.transform(frame @ ob.matrix_world)
    # In Blender's frame, so that after the FBX axis change the head backs out along Unreal's +Z.
    head = bpy.data.objects.new("SM_Engine18T_Fixing" + kind, data)
    export_collection.objects.link(head)
    bpy.ops.object.select_all(action="DESELECT")
    head.select_set(True)
    bpy.context.view_layer.objects.active = head
    filename = head.name + ".fbx"
    bpy.ops.export_scene.fbx(filepath=str(EXPORT / filename), use_selection=True, object_types={"MESH"},
        axis_forward="-Y", axis_up="Z", apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", bake_anim=False,
        use_mesh_modifiers=True, mesh_smooth_type="FACE", use_triangles=True, add_leaf_bones=False, colors_type="SRGB")
    lo = Vector([min(v.co[i] for v in data.vertices) for i in range(3)])
    hi = Vector([max(v.co[i] for v in data.vertices) for i in range(3)])
    size = hi - lo
    out["groups"].append({"name": head.name, "file": filename, "pivot_cm": [0, 0, 0], "hulls": 0, "parts": [name],
                          "dimensions_cm": [round(size.x * 100, 2), round(size.y * 100, 2), round(size.z * 100, 2)],
                          "triangles": sum(len(p.vertices) - 2 for p in data.polygons)})
    out["heads"][kind] = {"mesh": head.name, "across_cm": round(max(size.x, size.y) * 100, 2), "height_cm": round(size.z * 100, 2)}
    bpy.data.objects.remove(head, do_unlink=True)
for name in ("OilFillerCap", "Dipstick", "BatteryTerminal_Positive", "BatteryTerminal_Negative", "Battery", "CrankPulley", "CoolantTankCap",
             "Radiator_LeakPoint", "Radiator_SteamPoint", "ThrottleBody"):
    ob = bpy.data.objects.get(name)
    if ob:
        out["points"][name] = to_unreal(ob.matrix_world.translation)
(EXPORT / "engine_18t.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
for g in out["groups"]:
    print("EXPORT", g["name"], g["pivot_cm"], g["dimensions_cm"], g["triangles"], "hulls", g["hulls"], "sockets", len(g.get("sockets", [])))
print("EXPORT points", out["points"])
print("EXPORT heads", out["heads"])
print("EXPORT_DONE", len(out["groups"]))
