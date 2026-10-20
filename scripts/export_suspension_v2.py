"""Export the v2 suspension and brakes model (Art/Vehicles/Octavia/Suspension_v2) as FBX parts for the game's
suspension meshes, in export space (X forward, Y left, Z up, metres; the same conventions as export_engine_18t.py).
Run: blender --background --factory-startup --python scripts/export_suspension_v2.py

The model is in metres, Z up, +X the car's right, +Y the car's front, origin on the ground under the car's middle.
It is turned -90 degrees about Z into export space (model +Y -> export +X, model +X -> export -Y); the model is real
size and is not scaled or moved otherwise.

Groups written (Export/SM_Susp_<Name>.fbx), each mesh relative to its pivot, its UCX_ hulls with it:
- Wheel_C   Tyre, Rim and valve stem; pivot the wheel centre; sockets the rim's 5 wheel bolts.
- Hub_C     Knuckle (front) / HubCarrier (rear) and the drop link: follows the wheel's travel and steering, does
            not spin; pivot the wheel centre; no sockets.
- Brake_C   caliper, caliper carrier, both pads, brake hose: the game's brake part, following the hub without
            spinning; pivot the wheel centre; sockets are the brake's fixings (every fastener of caliper, carrier,
            hose and the disc screw, which would turn with the disc if it sat on it).
- Disc_C    the brake disc (spins); pivot the wheel centre; no sockets (its screw is on Brake_C).
- Strut_C   front Strut_Upper + Strut_Lower + Spring, rear Damper; pivot the top mount, moving end the bottom
            point; sockets all fasteners of those parts.
- Spring_C  rear only, the spring; pivot the top seat, moving end its bottom point; no sockets.
- LowerArm_C front LowerArm (sockets its fasteners and the knuckle's ball-joint pinch bolt and nut), rear LowerLink;
- UpperArm_C / TrailingArm_C  rear UpperLink / TrailingArm;
- TieRod_C  front TieRod + TieRodEnd (socket: the end's nut);
- Driveshaft_C  shaft, both CV joints, both boots (sockets: the 6 inner CV bolts and the axle bolt).
  Arms, tie rods and driveshafts pivot on their body-side origin and give their MovingEnd.
- SuspensionStatic  one mesh at the origin: front and rear subframes, rack, both anti-roll bars, rear final drive,
            with all their fasteners.
Fastener meshes that become sockets are left out of the mesh (the game draws its own heads); the other fastener
meshes stay in. Sockets are ordered along X, then Y, then Z of their Unreal position, at most 16 a mesh.
Writes Export/suspension_v2.json (pivot_cm / moving_cm / socket locations in Unreal cm, Y right).
"""
import json
import math
from pathlib import Path
import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "Art/Vehicles/Octavia/Suspension_v2"
SOURCE = BASE / "Octavia1U_Suspension.blend"
EXPORT = BASE / "Export"
CORNERS = ("FL", "FR", "RL", "RR")
# The model's frame into export space (X forward, Y left, Z up, m).
TURN = Matrix.Rotation(-math.pi / 2, 4, "Z")

TOOLS = {"10": "Spanner10", "13": "Spanner13", "17": "Spanner17", "19": "Spanner19"}


def game_tag(f):
    # The game's kind and tool: sockets 10/13/17/19 mm, a screwdriver and hands.
    tool = f["tool"].lower()
    if "hands" in tool:
        game_tool = "Hands"
    elif "screwdriver" in tool:
        game_tool = "Screwdriver"
    else:
        game_tool = next((TOOLS[s] for s in TOOLS if tool.startswith(s + " mm")), None)
        if game_tool is None:
            raise RuntimeError("Unknown tool %r on %s" % (f["tool"], f["name"]))
    kind = {"nut": "Nut", "screw": "Screw", "clip": "Clip"}.get(f["kind"], "Bolt")
    if game_tool == "Hands":
        kind = "Clip"
    return f"{kind} {game_tool}"


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
                        "kind": ob.name.split("_")[-2].lower(), "tool": ob.get("tool", ""), "label": ob.get("label", ""),
                        "part": part})
    return out


def sort_key(f):
    u = to_unreal(f["at"])
    return (round(u[0], 1), round(u[1], 1), round(u[2], 1))


# ---- the group table ----------------------------------------------------------------------------------------------
groups = []


def add(name, role, corner, parts, pivot, moving=None, sockets=()):
    groups.append({"name": name, "role": role, "corner": corner, "parts": parts, "pivot": pivot, "moving": moving,
                   "sockets": sorted(sockets, key=sort_key)})


for c in CORNERS:
    front = c in ("FL", "FR")
    centre = world("Rim_" + c)
    add("Wheel_" + c, "wheel", c, ["Tyre_" + c, "Rim_" + c], centre, sockets=fasteners_of("Rim_" + c))
    hub = ("Knuckle_" if front else "HubCarrier_") + c
    brake = ["BrakeCaliper_" + c, "CaliperCarrier_" + c, "BrakeHose_" + c]
    fix = [f for p in brake for f in fasteners_of(p)] + fasteners_of("BrakeDisc_" + c)
    # The model has brake pads on the left side only (no BrakePad*_FR / _RR): the right hubs go without.
    pads = [p + c for p in ("BrakePadInner_", "BrakePadOuter_") if (p + c) in bpy.data.objects]
    add("Hub_" + c, "hub", c, [hub, "AntiRollBarLink_" + c], centre)
    add("Brake_" + c, "brake", c, brake + pads, centre, sockets=fix)
    add("Disc_" + c, "disc", c, ["BrakeDisc_" + c], centre)
    if front:
        parts = ["Strut_Upper_" + c, "Strut_Lower_" + c, "Spring_" + c]
        add("Strut_" + c, "strut", c, parts, world(parts[0]), world("Strut_Lower_" + c + "_BottomPoint"),
            fasteners_of(parts[0]) + fasteners_of(parts[1]) + fasteners_of(parts[2]))
        add("LowerArm_" + c, "link", c, ["LowerArm_" + c], world("LowerArm_" + c), world("LowerArm_" + c + "_MovingEnd"),
            fasteners_of("LowerArm_" + c) + fasteners_of("Knuckle_" + c))
        add("TieRod_" + c, "link", c, ["TieRod_" + c, "TieRodEnd_" + c], world("TieRod_" + c), world("TieRod_" + c + "_MovingEnd"),
            fasteners_of("TieRodEnd_" + c) + fasteners_of("TieRod_" + c))
    else:
        add("Strut_" + c, "strut", c, ["Damper_" + c], world("Damper_" + c), world("Damper_" + c + "_BottomPoint"),
            fasteners_of("Damper_" + c))
        add("Spring_" + c, "spring", c, ["Spring_" + c], world("Spring_" + c), world("Spring_" + c + "_BottomPoint"))
        for name, part in (("LowerArm_", "LowerLink_"), ("UpperArm_", "UpperLink_"), ("TrailingArm_", "TrailingArm_")):
            add(name + c, "link", c, [part + c], world(part + c), world(part + c + "_MovingEnd"), fasteners_of(part + c))
    add("Driveshaft_" + c, "link", c, ["Driveshaft_" + c, "CVJoint_Inner_" + c, "CVBoot_Inner_" + c, "CVJoint_Outer_" + c,
                                       "CVBoot_Outer_" + c], world("Driveshaft_" + c), world("Driveshaft_" + c + "_MovingEnd"),
        fasteners_of("CVJoint_Inner_" + c) + fasteners_of("CVJoint_Outer_" + c))
STATIC = ["FrontSubframe", "SteeringRack", "FrontAntiRollBar", "RearSubframe", "RearAntiRollBar", "RearFinalDrive"]
add("SuspensionStatic", "static", None, STATIC, Vector((0, 0, 0)))

# every part of the model lands in exactly one group
model_parts = sorted(o.name for o in bpy.data.objects if o.parent is None and o.type == "MESH")
placed = [p for g in groups for p in g["parts"]]
dupes = sorted({p for p in placed if placed.count(p) > 1})
left_out = [p for p in model_parts if p not in placed]
missing = [p for p in placed if p not in model_parts]
print("EXPORT parts", len(model_parts), "placed", len(placed), "duplicates", dupes, "left out", left_out, "missing", missing)
if dupes or missing:
    raise RuntimeError("Part table is wrong")
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
    meshes, hulls, expected = [], [], []
    for part in g["parts"]:
        for src in mesh_sources(part):
            expected += [src.matrix_world @ v.co for v in src.data.vertices]
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
    mesh_ob.name = "SM_Susp_" + g["name"]
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
    exp_lo = Vector([min(w[i] for w in expected) for i in range(3)])
    exp_hi = Vector([max(w[i] for w in expected) for i in range(3)])
    ulo, uhi = to_unreal(exp_lo), to_unreal(exp_hi)
    entry = {"name": mesh_ob.name, "file": filename, "corner": g["corner"], "role": g["role"],
             "pivot_cm": to_unreal(pivot),
             "moving_cm": to_unreal(g["moving"]) if g["moving"] is not None else None,
             "dimensions_cm": [round(size.x * 100, 2), round(size.y * 100, 2), round(size.z * 100, 2)],
             "triangles": sum(len(p.vertices) - 2 for p in mesh_ob.data.polygons), "hulls": len(hulls),
             "materials": [m.name for m in mesh_ob.data.materials if m],
             "parts": g["parts"],
             # the source parts' bounds in Unreal cm (absolute), for checking the FBX against
             "bounds_cm": {"min": [min(a, b) for a, b in zip(ulo, uhi)], "max": [max(a, b) for a, b in zip(ulo, uhi)]}}
    if g["moving"] is not None:
        entry["moving_rel_cm"] = to_unreal(g["moving"] - pivot)
    entry["sockets"] = [{"name": "Fixing%d" % i, "location_cm": to_unreal(f["at"] - pivot), "facing": to_unreal(f["axis"] * 0.01),
                         "tag": game_tag(f), "model_fastener": f["name"], "part": f["part"], "tool": f["tool"],
                         "real_tool": f["mesh"].get("real_tool", ""), "label": f["label"]}
                        for i, f in enumerate(g["sockets"])]
    for o in [mesh_ob] + hulls:
        bpy.data.objects.remove(o, do_unlink=True)
    return entry


out = {"model": "Octavia1U_Suspension (v2)", "groups": [export_group(g) for g in groups]}
out["textures"] = sorted(p.name[:-len("_basecolor.png")] for p in (BASE / "textures").glob("*_basecolor.png"))
out["left_out_parts"] = left_out
(EXPORT / "suspension_v2.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
for e in out["groups"]:
    print("EXPORT", e["name"], e["pivot_cm"], e["moving_cm"], e["triangles"], "hulls", e["hulls"], "sockets",
          len(e["sockets"]), sorted({s["tag"] for s in e["sockets"]}))
print("EXPORT_DONE", len(out["groups"]))
