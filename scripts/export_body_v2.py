"""Export the v2 body and interior (Art/Vehicles/Octavia/Body_v2) as one FBX per part, in export space (X forward,
Y left, Z up, metres; the same conventions as export_suspension_v2.py and export_engine_18t.py).
Run: blender --background --factory-startup --python scripts/export_body_v2.py

The user's kits (Octavia1U_Body_v2.blend: BODY_KIT, Octavia1U_Interior_v2.blend: INTERIOR_KIT) are in metres, Z up,
+X the car's right, +Y the car's front, origin on the ground under the car's middle. They are turned -90 degrees
about Z into export space (model +Y -> export +X, model +X -> export -Y), real size, not scaled or moved otherwise.
REFERENCE_DoNotExport is never touched.

Which objects become a file (Export/SM_Octavia_<Name>.fbx):
- every top-level mesh of the kit, and every child mesh that has its own rig `motion` (door glass, handles, winders,
  fuel flap, seat backs and headrests). Seat_Rear is `fixed` but top level, so it is a part too.
- a child without a motion (GaugeFaces and WarningLights on the Dashboard, Plate_F on the FrontBumper, Plate_R on the
  Trunk) is merged into the part it hangs on. Plate_F's UVs are turned half a turn (its text read upside down).
BODY_KIT's paint gets smoothed custom normals first (scripts/body_normals.py), and INTERIOR_KIT
scripts/interior_details.py's details (Details_*) before anything is exported; they go into
the game's Interior.
Each mesh is baked at its rest pose (animation frame 1, where every preview action starts), world-aligned and
relative to the part's origin: the file is the part as it sits in the car, shifted so its pivot is (0,0,0). A rotated
object (needle, pedal) therefore has its rotation baked into the mesh and its rig axis is given in the world frame.
Its UCX_ hulls (also merged children's hulls) go in the same file. Both UV channels (UVMap, Livery) and the Grime
vertex colour are kept; triangulated, face smoothing like the other exports.

Writes Export/body_v2.json: per part its file, parent part, pivot_cm (Unreal cm, X forward, Y right, Z up, origin the
ground under the car's middle), bounds, triangles, hulls, material slots, UV layers, rig and every empty. Rig angles:
`axis_cm` is the right-hand rotation axis mapped into the Unreal frame as a direction; Unreal's FQuat/FRotator are
left-handed, so `fquat_axis` (the same axis negated) is what you hand FQuat(axis, +degrees) to turn the part the way
Blender's preview action does. `preview` is measured from the part's own preview action (axis, angle, travel) as a
cross-check of the declared rig.
"""
import json
import math
from pathlib import Path
import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "Art/Vehicles/Octavia/Body_v2"
EXPORT = BASE / "Export"
KITS = [("BODY_KIT", BASE / "Octavia1U_Body_v2.blend"), ("INTERIOR_KIT", BASE / "Octavia1U_Interior_v2.blend")]
# The kit frame into export space (X forward, Y left, Z up, m).
TURN = Matrix.Rotation(-math.pi / 2, 4, "Z")
HANDLED = {"motion", "local_axis", "range", "rest", "open_axis", "hinge_a", "hinge_b", "hinge_a_m", "hinge_b_m", "note"}


def r3(v):
    return [round(float(x), 3) for x in v]


def ue_pos(v):
    # kit metres (X right, Y front, Z up) -> Unreal cm (X forward, Y right, Z up)
    return [round(v.y * 100, 3), round(v.x * 100, 3), round(v.z * 100, 3)]


def ue_dir(v):
    return [round(v.y, 5), round(v.x, 5), round(v.z, 5)]


def kit_collection(name):
    return bpy.data.collections[name]


def build_parts(kit):
    objs = [o for o in kit.all_objects]
    meshes = [o for o in objs if o.type == "MESH" and not o.name.startswith("UCX_")]
    hulls = [o for o in objs if o.type == "MESH" and o.name.startswith("UCX_")]
    empties = [o for o in objs if o.type == "EMPTY"]
    is_part = {o.name: (o.parent is None or o.get("motion") in ("rotate", "slide")) for o in meshes}

    def host(o):
        while not is_part[o.name]:
            o = o.parent
        return o

    parts = {}
    for o in meshes:
        h = host(o)
        parts.setdefault(h.name, {"obj": h, "members": [h], "hulls": []})
        if o is not h:
            parts[h.name]["members"].append(o)
    for h in hulls:
        parts[host(h.parent).name]["hulls"].append(h)
    return parts, host, empties


def signed_volume(ob):
    me = ob.data
    total = 0.0
    for p in me.polygons:
        vs = [me.vertices[i].co for i in p.vertices]
        for i in range(1, len(vs) - 1):
            total += vs[0].dot(vs[i].cross(vs[i + 1])) / 6.0
    return total


def measure_previews(scene, motion_objs):
    # Each part's own preview action, measured against the rest pose: axis/angle (rotate) or travel (slide).
    rest = {}
    scene.frame_set(scene.frame_start)
    for o in motion_objs:
        par = o.parent.matrix_world.copy() if o.parent else Matrix.Identity(4)
        rest[o.name] = (par @ o.matrix_parent_inverse, (par @ o.matrix_parent_inverse @ o.matrix_basis).copy())
    best = {o.name: [] for o in motion_objs}
    for fr in range(scene.frame_start + 1, scene.frame_end + 1):
        scene.frame_set(fr)
        for o in motion_objs:
            base, m1 = rest[o.name]
            mf = base @ o.matrix_basis
            q = (mf.to_3x3() @ m1.to_3x3().inverted()).to_quaternion()
            axis, ang = q.to_axis_angle()
            best[o.name].append({"frame": fr, "angle": abs(ang), "axis": Vector(axis), "travel": mf.translation - m1.translation})
    scene.frame_set(scene.frame_start)
    return best


GAME_DIR = EXPORT / "Game"
GAME_NAMES = ["Chassis", "Door_FL", "Door_FR", "Door_RL", "Door_RR", "Bonnet", "Trunk", "FrontBumper", "RearBumper",
              "Fender_FL", "Fender_FR", "Headlight_L", "Headlight_R", "Taillight_L", "Taillight_R", "Roof", "Windshield",
              "BumperBar_F", "BumperBar_R", "SteeringWheel", "Interior"]
# The parts the game moves on their own (URallyCabinComponent): wipers on their spindles, and the cabin's gauge needles,
# pedals, levers, stalks, glove box and sun visors. Each is its own game mesh at its own pivot, with its rig.
GAME_MOVING = ["Wiper_L", "Wiper_R", "Needle_Speedo", "Needle_Tacho", "Needle_Temp", "Needle_Fuel", "Pedal_Clutch",
               "Pedal_Brake", "Pedal_Gas", "GearLever", "Handbrake", "Stalk_Signal", "Stalk_Wiper"]
# The glove box lid and sun visors are left out of the game altogether: they sat wrong and clipped, moving or not
# (2026-10-21). Anything parented to them goes too.
GAME_DROPPED = {"GloveBox", "SunVisor_L", "SunVisor_R"}
GAME_NAMES += GAME_MOVING
GAME_INTERIOR_PIVOT_CM = (-58.1, -0.5, 81.2)   # the game's SM_Octavia_Interior pivot, Unreal cm
game_out = []


def fix_plate_uv(name, me):
    """The front plate's UVs ran upside down (the text read rotated half a turn); turn them about their middle."""
    if name != "Plate_F" or "UVMap" not in me.uv_layers:
        return
    uv = me.uv_layers["UVMap"].uv
    us = [d.vector.x for d in uv]
    vs = [d.vector.y for d in uv]
    cu, cv = min(us) + max(us), min(vs) + max(vs)
    for d in uv:
        d.vector = (cu - d.vector.x, cv - d.vector.y)


def game_target(kit_name, name, obj):
    # which game mesh this part ends up in; a name not in GAME_NAMES is not exported
    o = obj
    while o is not None:
        if o.name in GAME_DROPPED or (o is obj and name in GAME_DROPPED):
            return "Dropped"
        o = o.parent
    if name in GAME_MOVING:
        return name
    if kit_name == "INTERIOR_KIT":
        return name if name == "SteeringWheel" else "Interior"
    if name == "FuelFlap":
        return "Chassis"
    if obj.parent is not None:
        return game_target(kit_name, obj.parent.name, obj.parent)
    return name


def prep_game_copy(me):
    """Exactly two UV layers (UVMap, Livery; zeros where a piece has none) and one colour layer 'Col' per corner:
    RGBA = (1, grime, grime, 1); the R channel is the game's damage channel. No Grime -> (1,1,1,1)."""
    n = len(me.loops)
    uvs = {}
    for nm in ("UVMap", "Livery"):
        buf = [0.0] * (2 * n)
        if nm in me.uv_layers:
            me.uv_layers[nm].uv.foreach_get("vector", buf)
        uvs[nm] = buf
    while len(me.uv_layers):
        me.uv_layers.remove(me.uv_layers[0])
    for nm in ("UVMap", "Livery"):
        layer = me.uv_layers.new(name=nm)
        layer.uv.foreach_set("vector", uvs[nm])
    grime = [1.0] * n
    if "Grime" in me.color_attributes:
        a = me.color_attributes["Grime"]
        buf = [0.0] * (4 * len(a.data))
        a.data.foreach_get("color", buf)
        vals = [(buf[4 * i] + buf[4 * i + 1] + buf[4 * i + 2]) / 3.0 for i in range(len(a.data))]
        if a.domain == "POINT":
            vi = [0] * n
            me.loops.foreach_get("vertex_index", vi)
            grime = [vals[v] for v in vi]
        else:
            grime = vals
    while len(me.color_attributes):
        me.color_attributes.remove(me.color_attributes[0])
    col = me.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
    flat = []
    for g in grime:
        flat += [1.0, g, g, 1.0]
    col.data.foreach_set("color", flat)


def export_game_group(kit_name, gname, part_names, parts, export_collection, pivot_kit):
    pivot = TURN @ pivot_kit
    copies, hulls = [], []
    for pn in part_names:
        for src in parts[pn]["members"]:
            sw = TURN @ src.matrix_world
            data = src.data.copy()
            data.transform(Matrix.Translation(-pivot) @ sw)
            if sw.determinant() < 0:
                data.flip_normals()
            fix_plate_uv(src.name, data)
            prep_game_copy(data)
            c = bpy.data.objects.new("GCOPY_" + src.name, data)
            export_collection.objects.link(c)
            copies.append(c)
        for h in parts[pn]["hulls"]:
            data = h.data.copy()
            data.transform(Matrix.Translation(-pivot) @ (TURN @ h.matrix_world))
            c = bpy.data.objects.new("GHULL", data)
            export_collection.objects.link(c)
            hulls.append(c)
    bpy.ops.object.select_all(action="DESELECT")
    for c in copies:
        c.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    if len(copies) > 1:
        bpy.ops.object.join()
    mesh_ob = bpy.context.view_layer.objects.active
    mesh_ob.name = "SM_Octavia_" + gname
    for i, h in enumerate(hulls):
        h.name = "UCX_%s_%02d" % (mesh_ob.name, i + 1)
    bpy.ops.object.select_all(action="DESELECT")
    mesh_ob.select_set(True)
    for h in hulls:
        h.select_set(True)
    GAME_DIR.mkdir(parents=True, exist_ok=True)
    filename = mesh_ob.name + ".fbx"
    bpy.ops.export_scene.fbx(filepath=str(GAME_DIR / filename), use_selection=True, object_types={"MESH"},
        axis_forward="-Y", axis_up="Z", apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", bake_anim=False,
        use_mesh_modifiers=True, mesh_smooth_type="FACE", use_triangles=True, add_leaf_bones=False, colors_type="SRGB")
    me = mesh_ob.data
    assert [u.name for u in me.uv_layers] == ["UVMap", "Livery"], (gname, [u.name for u in me.uv_layers])
    assert [a.name for a in me.color_attributes] == ["Col"], (gname, [a.name for a in me.color_attributes])
    game_out.append({"name": gname, "file": "Game/" + filename, "kit": kit_name, "pivot_cm": ue_pos(pivot_kit),
                     "source_parts": part_names,
                     "material_slots": list(dict.fromkeys(m.name for m in me.materials if m)),
                     "triangles": sum(len(f.vertices) - 2 for f in me.polygons), "hulls": len(hulls),
                     "uv_layers": [u.name for u in me.uv_layers], "color_layer": "Col"})
    for o in [mesh_ob] + hulls:
        bpy.data.objects.remove(o, do_unlink=True)


groups = []
all_materials = set()
empties_out = []
for kit_name, blend in KITS:
    bpy.ops.wm.open_mainfile(filepath=str(blend))
    scene = bpy.context.scene
    scene.frame_set(scene.frame_start)
    kit = kit_collection(kit_name)
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    if kit_name == "BODY_KIT":
        # The paint's shading without the fill's ripples (no vertex moves).
        import body_normals
        body_normals.smooth_objects(list(kit.all_objects))
    if kit_name == "INTERIOR_KIT":
        # The radio's and climate panel's labels, a trip meter, an extinguisher, an air freshener.
        import interior_details
        interior_details.build(kit, scene)
    parts, host, empties = build_parts(kit)
    EXPORT.mkdir(parents=True, exist_ok=True)
    export_collection = bpy.data.collections.new("EXPORT")
    scene.collection.children.link(export_collection)
    motion_objs = [p["obj"] for p in parts.values() if p["obj"].get("motion") in ("rotate", "slide")]
    previews = measure_previews(scene, motion_objs)

    for name, p in sorted(parts.items()):
        ob = p["obj"]
        W = TURN @ ob.matrix_world
        pivot = W.translation.copy()
        pivot_kit = ob.matrix_world.translation.copy()
        copies, expected, ngons, vol = [], [], 0, 0.0
        for src in p["members"]:
            ngons += sum(1 for f in src.data.polygons if len(f.vertices) > 4)
            sw = TURN @ src.matrix_world
            expected += [sw @ v.co for v in src.data.vertices]
            data = src.data.copy()
            data.transform(Matrix.Translation(-pivot) @ sw)
            if sw.determinant() < 0:
                data.flip_normals()
            fix_plate_uv(src.name, data)
            copy = bpy.data.objects.new("COPY_" + src.name, data)
            export_collection.objects.link(copy)
            copies.append(copy)
        hulls = []
        for h in p["hulls"]:
            hw = TURN @ h.matrix_world
            data = h.data.copy()
            data.transform(Matrix.Translation(-pivot) @ hw)
            copy = bpy.data.objects.new("HULL", data)
            export_collection.objects.link(copy)
            hulls.append(copy)
        bpy.ops.object.select_all(action="DESELECT")
        for c in copies:
            c.select_set(True)
        bpy.context.view_layer.objects.active = copies[0]
        if len(copies) > 1:
            bpy.ops.object.join()
        mesh_ob = bpy.context.view_layer.objects.active
        mesh_ob.name = "SM_Octavia_" + name
        for i, h in enumerate(hulls):
            h.name = "UCX_%s_%02d" % (mesh_ob.name, i + 1)
        bpy.ops.object.select_all(action="DESELECT")
        mesh_ob.select_set(True)
        for h in hulls:
            h.select_set(True)
        filename = mesh_ob.name + ".fbx"
        bpy.ops.export_scene.fbx(filepath=str(EXPORT / filename), use_selection=True, object_types={"MESH"},
            axis_forward="-Y", axis_up="Z", apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", bake_anim=False,
            use_mesh_modifiers=True, mesh_smooth_type="FACE", use_triangles=True, add_leaf_bones=False, colors_type="SRGB")
        me = mesh_ob.data
        lo = Vector([min(v.co[i] for v in me.vertices) for i in range(3)])
        hi = Vector([max(v.co[i] for v in me.vertices) for i in range(3)])
        size = hi - lo
        exp_lo = Vector([min(w[i] for w in expected) for i in range(3)])
        exp_hi = Vector([max(w[i] for w in expected) for i in range(3)])
        # export space (X fwd, Y left) -> Unreal cm (Y right)
        ulo = [round(exp_lo.x * 100, 3), round(-exp_hi.y * 100, 3), round(exp_lo.z * 100, 3)]
        uhi = [round(exp_hi.x * 100, 3), round(-exp_lo.y * 100, 3), round(exp_hi.z * 100, 3)]
        vol = signed_volume(mesh_ob)
        entry = {
            "name": name, "file": "Export/" + filename, "kit": kit_name,
            "parent_part": host(ob.parent).name if ob.parent else None,
            "merged_children": [m.name for m in p["members"][1:]],
            "pivot_cm": ue_pos(pivot_kit),
            "dimensions_cm": [round(size.x * 100, 2), round(size.y * 100, 2), round(size.z * 100, 2)],
            "bounds_cm": {"min": ulo, "max": uhi},
            "triangles": sum(len(f.vertices) - 2 for f in me.polygons),
            "source_ngons": ngons, "hulls": len(hulls),
            # the FBX writer merges slots that share a material (Door: plastic_black twice), so the file has these:
            "material_slots": list(dict.fromkeys(m.name for m in me.materials if m)),
            "material_slots_blend": [m.name if m else None for m in me.materials],
            "uv_layers": [u.name for u in me.uv_layers],
            "color_attributes": [a.name for a in me.color_attributes],
            "signed_volume_m3": round(vol, 5),
        }
        all_materials.update(m.name for m in me.materials if m)
        if ob.parent:
            entry["pivot_in_parent_cm"] = ue_pos(ob.matrix_world.translation - host(ob.parent).matrix_world.translation)
        # rig
        motion = ob.get("motion")
        if motion:
            rig = {"motion": motion, "local_axis_blender": ob.get("local_axis"), "range": list(ob.get("range", [])),
                   "rest": ob.get("rest")}
            R = ob.matrix_world.to_3x3().normalized()
            la = ob.get("local_axis")
            rot_axes = {"X": R @ Vector((1, 0, 0)), "Y": R @ Vector((0, 1, 0)), "Z": R @ Vector((0, 0, 1))}
            if la == "hinge_line":
                a = Vector(ob["hinge_a_m"])
                b = Vector(ob["hinge_b_m"])
                line = (b - a).normalized()
                rig["hinge_a_cm"] = ue_pos(a)
                rig["hinge_b_cm"] = ue_pos(b)
                rig["hinge_a_rel_cm"] = ue_pos(a - pivot_kit)
                rig["hinge_b_rel_cm"] = ue_pos(b - pivot_kit)
                rig["hinge_line_cm"] = ue_dir(line)
                oa = Vector(ob["open_axis"])
                rig["open_axis_cm"] = ue_dir(oa)
                declared = oa
                rig["axis_cm"] = ue_dir(oa)
                rig["fquat_axis"] = ue_dir(-oa)
            elif la in rot_axes:
                declared = rot_axes[la]
                rig["axis_cm"] = ue_dir(declared)
                if motion == "rotate":
                    rig["fquat_axis"] = ue_dir(-declared)
                if "open_axis" in ob:
                    rig["open_axis_cm"] = ue_dir(Vector(ob["open_axis"]))
            elif la == "XY":
                declared = None
                rig["axis_x_cm"] = ue_dir(rot_axes["X"])
                rig["axis_y_cm"] = ue_dir(rot_axes["Y"])
            else:
                declared = None
            if motion == "slide":
                rng = list(ob.get("range", []))
                rig["range_cm"] = [round(x * 100, 2) for x in rng]
            if "open_axis" in ob and la != "hinge_line":
                rig["open_axis_blender_kit"] = list(ob["open_axis"])
            for k, v in ob.items():
                if k not in HANDLED:
                    rig[k] = list(v) if hasattr(v, "__len__") and not isinstance(v, str) else v
            if "note" in ob:
                rig["note"] = ob["note"]
            frames = previews.get(name, [])
            pv = None
            if frames:
                # Euler-keyed previews flip through gimbal poses mid-sweep: take the largest sweep among frames whose
                # rotation really is about the declared axis (all frames if there is no single declared axis).
                if motion == "rotate":
                    ok = [f for f in frames if f["angle"] > 1e-6 and (declared is None or abs(f["axis"].dot(declared)) > 0.99)]
                    pv = max(ok, key=lambda f: f["angle"]) if ok else max(frames, key=lambda f: f["angle"])
                    ratio = len(ok) / len(frames)
                else:
                    pv = max(frames, key=lambda f: f["travel"].length)
                    ratio = 1.0
            if pv:
                prev = {"frame": pv["frame"], "frames_about_declared_axis": round(ratio, 2) if motion == "rotate" else None}
                if motion == "rotate":
                    deg = math.degrees(pv["angle"])
                    if declared is not None:
                        # signed angle about the declared axis; below 180 deg only (a winder wraps)
                        s = 1.0 if pv["axis"].dot(declared) >= 0 else -1.0
                        prev["angle_about_declared_deg"] = round(deg * s, 2)
                        prev["axis_alignment"] = round(abs(pv["axis"].dot(declared)), 4)
                    else:
                        prev["angle_deg"] = round(deg, 2)
                    prev["axis_cm"] = ue_dir(pv["axis"])
                else:
                    prev["travel_cm"] = [round(x, 2) for x in ue_pos(pv["travel"])]
                    prev["travel_len_cm"] = round(pv["travel"].length * 100, 2)
                rig["preview"] = prev
            entry["rig"] = rig
        groups.append(entry)
        for o in [mesh_ob] + hulls:
            bpy.data.objects.remove(o, do_unlink=True)

    # ---- the drop-in set for the game's current meshes (Export/Game) ----
    targets = {}
    for pn, p in parts.items():
        targets.setdefault(game_target(kit_name, pn, p["obj"]), []).append(pn)
    for gname in GAME_NAMES:
        if gname not in targets:
            continue
        if gname == "Interior":
            # the game's interior pivot, Unreal cm (X forward, Y right) -> kit m
            piv = Vector((GAME_INTERIOR_PIVOT_CM[1] / 100, GAME_INTERIOR_PIVOT_CM[0] / 100, GAME_INTERIOR_PIVOT_CM[2] / 100))
        else:
            piv = parts[gname]["obj"].matrix_world.translation.copy()
        export_game_group(kit_name, gname, sorted(targets[gname], key=lambda n: (n != gname, n)), parts, export_collection, piv)
    unplaced = [g for g in targets if g not in GAME_NAMES]
    if unplaced:
        raise RuntimeError("Parts not in the game set: %s" % unplaced)

    for e in sorted(empties, key=lambda o: o.name):
        owner = host(e.parent).name if e.parent and e.parent.type == "MESH" else None
        empties_out.append({"name": e.name, "kit": kit_name, "parent": e.parent.name if e.parent else None,
                            "part": owner, "location_cm": ue_pos(e.matrix_world.translation)})

for e in empties_out:
    part = next((g for g in groups if g["name"] == e["part"]), None)
    if part:
        e["rel_cm"] = [round(a - b, 3) for a, b in zip(e["location_cm"], part["pivot_cm"])]
    # attach to the part's record too
    if part is not None:
        part.setdefault("empties", {})[e["name"]] = {"location_cm": e["location_cm"], "rel_cm": e["rel_cm"]}

# materials and the texture sets that belong to them (<material without M_>_<map>.png)
tex_dir = BASE / "textures"
materials = {}
for m in sorted(all_materials):
    stem = m[2:] if m.startswith("M_") else m
    materials[m] = sorted(p.name for p in tex_dir.glob(stem + "_*.png"))
for g in groups:
    g["textures"] = {m: materials.get(m, []) for m in sorted({s for s in g["material_slots"] if s})}

out = {"model": "Octavia1U Body + Interior v2",
       "frame": "pivot_cm/bounds/empties in Unreal cm: X forward, Y right, Z up, origin on the ground under the car's middle. "
                "Meshes are world-aligned at the rest pose, relative to the pivot (export space X fwd, Y left, m, FBX -Y/Z).",
       "parts": groups, "empties": empties_out, "materials": materials}
(EXPORT / "body_v2.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
for g in groups:
    print("EXPORT", g["name"], g["pivot_cm"], g["triangles"], "hulls", g["hulls"], g["uv_layers"], g["color_attributes"],
          "vol", g["signed_volume_m3"], "ngons", g["source_ngons"])
for g in game_out:
    rig = next((p.get("rig") for p in groups if p["name"] == g["name"]), None)
    if g["name"] in GAME_MOVING and rig:
        g["rig"] = {k: rig[k] for k in ("motion", "range", "rest", "fquat_axis", "axis_x_cm", "axis_y_cm", "range_across",
                                        "range_fore_aft", "units_per_degree", "range_cm") if k in rig}
(GAME_DIR / "game_parts.json").write_text(json.dumps({"frame": "pivot_cm in Unreal cm (X forward, Y right, Z up); meshes relative to it",
    "vertex_colour": "layer Col per corner, RGBA = (1, grime, grime, 1); R is the game's damage channel (sRGB-encoded by the FBX writer)",
    "rig": "moving parts: FQuat(fquat_axis, +degrees) turns the part as its Blender preview does, from rest at 0",
    "parts": game_out}, indent=2), encoding="utf-8")
for g in game_out:
    print("GAME", g["name"], g["pivot_cm"], g["triangles"], "hulls", g["hulls"], g["material_slots"], g.get("rig"))
print("EXPORT_DONE", len(groups), "parts", sum(g["triangles"] for g in groups), "triangles", len(empties_out), "empties")
