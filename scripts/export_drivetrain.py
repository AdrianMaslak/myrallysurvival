"""Export the user's modular drivetrain kit (Art/Vehicles/Octavia/Drivetrain, Octavia1U_Drivetrain.blend) as FBX parts
for the game, in export space (X forward, Y left, Z up, metres; the same conventions as export_suspension_v2.py).
Run: blender --background --factory-startup --python scripts/export_drivetrain.py

The kit builds a front-drive car and a 4x4 from the same body (configs.json). The game is the 4x4; the front-drive parts
are exported too, for a later car. Groups written (Export/SM_Drive_<Name>.fbx), each mesh relative to its pivot, its
UCX_ hulls with it, its fastener meshes left out (the game draws its own heads) and given as sockets Fixing0, 1 ...:
- AngleDrive       the 4x4 transfer box bolted to the back of the 02J; it rolls with the engine.
- Propshaft_Front  flex disc and tube from the angle drive to the centre bearing; spins about spin_axis.
- PropshaftBearing the rubber-mounted centre bearing, bolted up into the tunnel; does not spin.
- Propshaft_Rear   centre CV joint, tube and rear CV joint on the final drive's flange; spins about spin_axis.
- Exhaust          cat, centre silencer and rear silencer as one part (the game's exhaust), pivot the front flange.
- FuelTank         the 4x4 saddle tank (43 l); FuelTank_FWD the front-drive car's 50 l tank.
- FuelFillerNeck   from the tank up to the fuel flap.
- TwistBeam, PivotBracket_RL/RR, RearHub_RL/RR: the front-drive rear axle (not used by the game yet).
Writes Export/drivetrain.json: per group its pivot, moving end and spin axis, its points (hangers, drag, leak, filler,
output flange) and sockets, in Unreal cm with Y right.
"""
import json
import math
from pathlib import Path
import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "Art/Vehicles/Octavia/Drivetrain"
SOURCE = BASE / "Octavia1U_Drivetrain.blend"
EXPORT = BASE / "Export"
# The model's frame into export space (X forward, Y left, Z up, m).
TURN = Matrix.Rotation(-math.pi / 2, 4, "Z")
TOOLS = {"10": "Spanner10", "13": "Spanner13", "17": "Spanner17", "19": "Spanner19"}
POINT_KEYS = ("_Hanger_", "_DragPoint", "_TailpipePoint", "_LeakPoint", "_FillerInlet", "_Cap", "_FloorGrommet",
              "_OutputFlange", "_MovingEnd")


def game_tag(f):
    # The game's kind and tool: sockets 10/13/17/19 mm (16 maps to 17), a screwdriver and hands.
    tool = f["tool"].lower().replace("16 mm", "17 mm")
    if "hands" in tool:
        game_tool = "Hands"
    elif "screwdriver" in tool:
        game_tool = "Screwdriver"
    else:
        game_tool = next((TOOLS[s] for s in TOOLS if tool.startswith(s + " mm")), None)
        if game_tool is None:
            raise RuntimeError("Unknown tool %r on %s" % (f["tool"], f["name"]))
    kind = {"nut": "Nut", "screw": "Screw", "clip": "Clip"}.get(f["kind"], "Bolt")
    return "Clip Hands" if game_tool == "Hands" else f"{kind} {game_tool}"


bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
for ob in [o for o in bpy.data.objects if o.parent is None]:
    ob.matrix_world = TURN @ ob.matrix_world
bpy.context.view_layer.update()
EXPORT.mkdir(parents=True, exist_ok=True)
export_collection = bpy.data.collections.new("EXPORT")
bpy.context.scene.collection.children.link(export_collection)


def to_unreal(v):
    return [round(v.x * 100, 3), round(-v.y * 100, 3), round(v.z * 100, 3)]


def world(name):
    return bpy.data.objects[name].matrix_world.translation.copy()


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
                        "kind": ob.name.split("_")[-2].lower(), "tool": ob.get("tool", ""), "part": part})
    return out


def points_of(parts):
    return [o for p in parts for o in bpy.data.objects
            if o.parent and o.parent.name == p and o.type == "EMPTY" and any(k in o.name for k in POINT_KEYS)]


def spin_axis(part):
    # A shaft turns about the line from its origin to its moving end; otherwise about its local spin_axis_local (the kit's
    # spin_axis property is in the part's own frame).
    ob = bpy.data.objects[part]
    end = bpy.data.objects.get(part + "_MovingEnd")
    if end:
        axis = end.matrix_world.translation - ob.matrix_world.translation
    else:
        local = {"X": Vector((1, 0, 0)), "Y": Vector((0, 1, 0)), "Z": Vector((0, 0, 1))}[ob.get("spin_axis_local", "Z")]
        axis = ob.matrix_world.to_3x3() @ local
    axis.normalize()
    return [round(axis.x, 5), round(-axis.y, 5), round(axis.z, 5)]


groups = []


def add(name, role, parts, pivot_part, moving=None, spin=None):
    groups.append({"name": name, "role": role, "parts": parts, "pivot": world(pivot_part),
                   "moving": world(moving) if moving else None, "spin": spin_axis(spin) if spin else None,
                   "sockets": sorted([f for p in parts for f in fasteners_of(p)],
                                     key=lambda f: tuple(round(c, 1) for c in to_unreal(f["at"])))})


add("AngleDrive", "angledrive", ["AngleDrive"], "AngleDrive", spin="AngleDrive")
add("Propshaft_Front", "propshaft", ["Propshaft_Front"], "Propshaft_Front", "Propshaft_Front_MovingEnd", "Propshaft_Front")
add("PropshaftBearing", "propshaft", ["PropshaftCentreBearing"], "PropshaftCentreBearing")
add("Propshaft_Rear", "propshaft", ["Propshaft_Rear"], "Propshaft_Rear", "Propshaft_Rear_MovingEnd", "Propshaft_Rear")
add("Exhaust", "exhaust", ["Exhaust_Cat", "Exhaust_CentreSilencer", "Exhaust_RearSilencer"], "Exhaust_Cat")
add("FuelTank", "tank", ["FuelTank_4x4"], "FuelTank_4x4")
add("FuelTank_FWD", "tank_fwd", ["FuelTank_FWD"], "FuelTank_FWD")
add("FuelFillerNeck", "filler", ["FuelFillerNeck"], "FuelFillerNeck")
add("TwistBeam", "fwd_axle", ["TwistBeam"], "TwistBeam")
for side in ("RL", "RR"):
    add("PivotBracket_" + side, "fwd_axle", ["PivotBracket_" + side], "PivotBracket_" + side)
    add("RearHub_" + side, "fwd_axle", ["RearHub_" + side], "RearHub_" + side)
socket_meshes = {f["name"] for g in groups for f in g["sockets"]}
for g in groups:
    if len(g["sockets"]) > 16:
        raise RuntimeError("More than 16 sockets on " + g["name"])


def mesh_sources(part):
    ob = bpy.data.objects[part]
    return [ob] + [c for c in bpy.data.objects if c.parent == ob and c.type == "MESH" and not c.name.startswith("UCX_")
                   and c.name not in socket_meshes]


def export_group(g):
    pivot = g["pivot"]
    meshes, hulls = [], []
    for part in g["parts"]:
        for src in mesh_sources(part):
            data = src.data.copy()
            data.transform(Matrix.Translation(-pivot) @ src.matrix_world)
            copy = bpy.data.objects.new("COPY_" + src.name, data)
            export_collection.objects.link(copy)
            meshes.append(copy)
        for hull in children(part, "UCX_"):
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
    mesh_ob.name = "SM_Drive_" + g["name"]
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
    entry = {"name": mesh_ob.name, "file": filename, "role": g["role"], "parts": g["parts"],
             "pivot_cm": to_unreal(pivot),
             "moving_cm": to_unreal(g["moving"]) if g["moving"] is not None else None,
             "spin_axis": g["spin"],
             "dimensions_cm": [round(size.x * 100, 2), round(size.y * 100, 2), round(size.z * 100, 2)],
             "triangles": sum(len(p.vertices) - 2 for p in mesh_ob.data.polygons), "hulls": len(hulls),
             "materials": [m.name for m in mesh_ob.data.materials if m],
             "points": {o.name: {"cm": to_unreal(o.matrix_world.translation), "rel_cm": to_unreal(o.matrix_world.translation - pivot)}
                        for o in points_of(g["parts"])},
             "sockets": [{"name": "Fixing%d" % i, "location_cm": to_unreal(f["at"] - pivot), "facing": to_unreal(f["axis"] * 0.01),
                          "tag": game_tag(f), "model_fastener": f["name"], "part": f["part"], "tool": f["tool"],
                          "real_tool": f["mesh"].get("real_tool", "")}
                         for i, f in enumerate(g["sockets"])]}
    for o in [mesh_ob] + hulls:
        bpy.data.objects.remove(o, do_unlink=True)
    return entry


out = {"model": "Octavia1U_Drivetrain (modular kit v1)", "car": "4x4 (configs.json)", "groups": [export_group(g) for g in groups]}
out["textures"] = sorted(p.name[:-len("_basecolor.png")] for p in (BASE / "textures").glob("*_basecolor.png"))
(EXPORT / "drivetrain.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
for e in out["groups"]:
    print("EXPORT", e["name"], e["pivot_cm"], e["spin_axis"], e["triangles"], "hulls", e["hulls"], "sockets",
          len(e["sockets"]), sorted({s["tag"] for s in e["sockets"]}), "points", len(e["points"]))
print("EXPORT_DONE", len(out["groups"]))
