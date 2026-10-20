"""The cabin's small details on top of the user's v2 interior (scripts/export_body_v2.py builds them into INTERIOR_KIT
before exporting, so they end up in SM_Octavia_Interior). Blender, no file of its own: everything is made here.

The interior's centre stack is modelled (a radio with its knobs, slot and buttons; a climate panel with nine buttons)
but all in one dark plastic, so it read as a blank slab. Thin decals 1.2 mm proud of it carry the dash_labels atlas
(scripts/build_game_textures.py, DASH): the radio's amber display, its preset numbers, the knobs' pointers, the
climate panel's strip and button labels. Each decal sits over the most proud point of the stack in its footprint,
found by casting rays at it. Then three things a 1996 privateer's car has: a trip meter on the dash in front of the
co-driver, a 2 kg fire extinguisher strapped to the floor by the tunnel in the co-driver's footwell, and a pine air
freshener hanging from the mirror.

Frame: the kit's (metres, Z up, +X the car's right, +Y its front). The stack's face is a plane through P0 with
normal N (towards the cabin and up); positions on it are (u along +X, s up the face), measured from a depth scan
of the stack (u -13..13 cm, s -9..24 cm).
"""
import math
import bmesh
import bpy
from mathutils import Matrix, Vector

N = Vector((0.0, -0.94, 0.34)).normalized()
W = Vector((0.0, 0.34, 0.94)).normalized()
P0 = Vector((0.0, 0.530, 0.60))
ATLAS = 1024.0
DASH = {
    "lcd": (0, 0, 512, 72), "presets": (0, 80, 384, 128), "climate_strip": (0, 140, 1024, 196),
    "climate_buttons": (0, 200, 576, 248), "pointer": (0, 260, 64, 276), "trip": (0, 300, 512, 428),
    "tree": (600, 300, 856, 556), "extinguisher_label": (0, 600, 256, 728), "red": (300, 600, 364, 664),
    "black": (400, 600, 464, 664), "white": (500, 600, 564, 664), "trip_case": (600, 600, 664, 664),
}
LIFT = 0.0012
# Parts that move and the seats are out of the way of the rays.
NOT_STACK = ("GearLever", "GearBoot", "SteeringWheel", "Seat_FL", "Seat_FR", "Armrest", "Handbrake", "Stalk_Signal",
             "Stalk_Wiper", "Seat_FL_Back", "Seat_FR_Back")


def rect_uv(key, fx, fy):
    """A point (fx, fy in 0..1 from the rectangle's top left) of an atlas rectangle, as Blender UV (V up)."""
    x0, y0, x1, y1 = DASH[key]
    return ((x0 + fx * (x1 - x0)) / ATLAS, 1.0 - (y0 + fy * (y1 - y0)) / ATLAS)


def cell_uv(key, index, cells, fx, fy):
    x0, y0, x1, y1 = DASH[key]
    w = (x1 - x0) / cells
    return ((x0 + (index + fx) * w) / ATLAS, 1.0 - (y0 + fy * (y1 - y0)) / ATLAS)


class Builder:
    def __init__(self, kit, scene):
        self.kit = kit
        self.scene = scene
        self.mat = {}
        for name in ("M_dash_labels", "M_plastic_black", "M_chrome", "M_rubber"):
            m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
            self.mat[name] = m
        self.objects = []

    def ray(self, origin, direction):
        dg = bpy.context.evaluated_depsgraph_get()
        hit, loc, nor, idx, ob, _ = self.scene.ray_cast(dg, origin, direction)
        return (loc, nor) if hit else (None, None)

    def new_object(self, name, bm, materials):
        me = bpy.data.meshes.new(name)
        bm.to_mesh(me)
        bm.free()
        for m in materials:
            me.materials.append(self.mat[m])
        ob = bpy.data.objects.new(name, me)
        self.kit.objects.link(ob)
        for p in me.polygons:
            p.use_smooth = False
        self.objects.append(ob)
        return ob

    def quad(self, bm, uvl, corners, uvs, mat=0):
        verts = [bm.verts.new(c) for c in corners]
        face = bm.faces.new(verts)
        face.material_index = mat
        for loop, uv in zip(face.loops, uvs):
            loop[uvl].uv = uv
        return face

    # -- decals on the stack --
    def stack_point(self, u, s):
        return P0 + Vector((u, 0.0, 0.0)) + W * s

    def stack_depth(self, u, s, half_w, half_h):
        """The most proud point of the stack (along N) under a decal's footprint."""
        best = None
        for fu in (-1, 0, 1):
            for fs in (-1, 0, 1):
                p = self.stack_point(u + fu * half_w * 0.9, s + fs * half_h * 0.9)
                loc, _ = self.ray(p + N * 0.08, -N)
                if loc is not None:
                    d = (loc - p).dot(N)
                    best = d if best is None else max(best, d)
        return best if best is not None else 0.0

    def decal(self, bm, uvl, u, s, width, height, uv_of):
        """A quad on the stack's face at (u, s), width x height (m); uv_of(fx, fy) gives each corner's UV."""
        depth = self.stack_depth(u, s, width / 2, height / 2) + LIFT
        c = self.stack_point(u, s) + N * depth
        right, up = Vector((1.0, 0.0, 0.0)), W
        corners = [c - right * width / 2 - up * height / 2, c + right * width / 2 - up * height / 2,
                   c + right * width / 2 + up * height / 2, c - right * width / 2 + up * height / 2]
        uvs = [uv_of(0, 1), uv_of(1, 1), uv_of(1, 0), uv_of(0, 0)]
        self.quad(bm, uvl, corners, uvs)

    def stack_decals(self):
        bm = bmesh.new()
        uvl = bm.loops.layers.uv.new("UVMap")
        # The radio's display, in its window.
        self.decal(bm, uvl, 0.0145, 0.0925, 0.085, 0.012, lambda fx, fy: rect_uv("lcd", fx, fy))
        # Preset buttons 1-6.
        for i, u in enumerate((-0.062, -0.038, -0.012, 0.014, 0.040, 0.066)):
            self.decal(bm, uvl, u, 0.118, 0.0072, 0.0054, lambda fx, fy, i=i: cell_uv("presets", i, 6, fx, fy))
        # The volume and tuning knobs' pointers, straight up.
        for u in (-0.081, 0.085):
            self.decal(bm, uvl, u, 0.088, 0.0016, 0.007, lambda fx, fy: rect_uv("pointer", fx, fy))
        # The climate panel: its strip across the middle and its eight buttons' labels.
        self.decal(bm, uvl, 0.0, 0.0225, 0.165, 0.0085, lambda fx, fy: rect_uv("climate_strip", fx, fy))
        for i, u in enumerate((-0.0812, -0.0572, -0.0332, -0.010, 0.014, 0.036, 0.0608, 0.084)):
            self.decal(bm, uvl, u, -0.007, 0.0075, 0.0056, lambda fx, fy, i=i: cell_uv("climate_buttons", i, 9, fx, fy))
        return self.new_object("Details_StackLabels", bm, ["M_dash_labels"])

    # -- pieces --
    def box(self, bm, uvl, centre, axes, size, uv_face, front_uv=None, mat=0):
        """A box at centre with axes (right, forward, up) and size; every face UV'd to uv_face's rectangle except the
        -forward face (the one facing the viewer), which gets front_uv's if given."""
        r, f, u = (a.normalized() for a in axes)
        hx, hy, hz = (v / 2 for v in size)
        P = lambda sx, sy, sz: centre + r * sx * hx + f * sy * hy + u * sz * hz
        faces = {
            "front": [P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1)],
            "back": [P(1, 1, -1), P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1)],
            "left": [P(-1, 1, -1), P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1)],
            "right": [P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1)],
            "top": [P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1)],
            "bottom": [P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), P(-1, -1, -1)],
        }
        for name, corners in faces.items():
            key = front_uv if (name == "front" and front_uv) else uv_face
            self.quad(bm, uvl, corners, [rect_uv(key, 0, 1), rect_uv(key, 1, 1), rect_uv(key, 1, 0), rect_uv(key, 0, 0)], mat)

    def cylinder(self, bm, uvl, a, b, radius, segments, side_uv, cap_uv, mat=0, cap_mat=None, label=None):
        """A closed cylinder from a to b. Its side is UV'd round side_uv's rectangle, or label's over a band
        (label: (key, start, end) as shares of the length)."""
        axis = (b - a).normalized()
        ref = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
        x = axis.cross(ref).normalized()
        y = axis.cross(x)
        rings = [0.0, 1.0] if not label else [0.0, label[1], label[2], 1.0]
        loops = []
        for t in rings:
            c = a + (b - a) * t
            loops.append([bm.verts.new(c + (x * math.cos(2 * math.pi * k / segments) + y * math.sin(2 * math.pi * k / segments)) * radius)
                          for k in range(segments)])
        for ri in range(len(rings) - 1):
            key = label[0] if (label and ri == 1) else side_uv
            for k in range(segments):
                k2 = (k + 1) % segments
                f = bm.faces.new([loops[ri][k], loops[ri][k2], loops[ri + 1][k2], loops[ri + 1][k]])
                f.material_index = mat
                fx0, fx1 = k / segments, (k + 1) / segments
                uvs = [rect_uv(key, fx0, 1), rect_uv(key, fx1, 1), rect_uv(key, fx1, 0), rect_uv(key, fx0, 0)]
                for loop, uv in zip(f.loops, uvs):
                    loop[uvl].uv = uv
        for ring, flip in ((loops[0], True), (loops[-1], False)):
            f = bm.faces.new(list(reversed(ring)) if flip else ring)
            f.material_index = cap_mat if cap_mat is not None else mat
            for loop in f.loops:
                loop[uvl].uv = rect_uv(cap_uv, 0.5, 0.5)

    def trip_meter(self):
        # On the dash top in front of the co-driver, turned to face their eye.
        base = Vector((0.33, 0.69, 0.905))
        eye = Vector((0.38, -0.02, 1.14))
        facing = (eye - base).normalized()
        forward = -facing
        right = forward.cross(Vector((0, 0, 1))).normalized()
        up = right.cross(forward).normalized()
        bm = bmesh.new()
        uvl = bm.loops.layers.uv.new("UVMap")
        centre = base + up * 0.035
        self.box(bm, uvl, centre, (right, forward, up), (0.15, 0.05, 0.065), "trip_case", "trip")
        # Its bracket: a black foot to the dash.
        self.box(bm, uvl, base + up * 0.002 + forward * 0.01, (right, forward, up), (0.07, 0.05, 0.008), "black")
        return self.new_object("Details_TripMeter", bm, ["M_dash_labels"])

    def floor_z(self, x, y):
        # From below the dash and the seat's front edge (the footwell is about 25 cm high).
        loc, _ = self.ray(Vector((x, y, 0.45)), Vector((0, 0, -1)))
        return loc.z if loc is not None else 0.30

    def extinguisher(self):
        # Lying along the car by the tunnel in the co-driver's footwell, clear of the seat, strapped down.
        x, y0, y1, r = 0.205, 0.46, 0.70, 0.048
        a = Vector((x, y0, self.floor_z(x, y0) + r + 0.012))
        b = Vector((x, y1, self.floor_z(x, y1) + r + 0.012))
        bm = bmesh.new()
        uvl = bm.loops.layers.uv.new("UVMap")
        self.cylinder(bm, uvl, a, b, r, 16, "red", "red", label=("extinguisher_label", 0.35, 0.75))
        # The valve and handle at the front end, the cap at the back.
        axis = (b - a).normalized()
        self.cylinder(bm, uvl, b, b + axis * 0.04, 0.018, 10, "black", "black")
        side = axis.cross(Vector((0, 0, 1))).normalized()
        up = side.cross(axis).normalized()
        if up.z < 0:
            up = -up
        self.box(bm, uvl, b + axis * 0.05 + up * 0.02, (side, axis, up), (0.012, 0.06, 0.012), "black")
        # Two straps over it to a plate on the floor.
        for t in (0.2, 0.85):
            c = a + (b - a) * t
            self.box(bm, uvl, c + up * (r + 0.002) * 0.0, (side, axis, up), (2 * r + 0.006, 0.025, 2 * r + 0.006), "black")
        self.box(bm, uvl, (a + b) / 2 - up * (r + 0.008), (side, axis, up), (0.06, (b - a).length * 0.9, 0.006), "black")
        return self.new_object("Details_Extinguisher", bm, ["M_dash_labels"])

    def air_freshener(self):
        # Hung by a thread from under the mirror, turned towards the driver.
        loc, _ = self.ray(Vector((0.012, 0.44, 1.0)), Vector((0, 0, 1)))
        top = loc if loc is not None else Vector((0.012, 0.44, 1.228))
        bm = bmesh.new()
        uvl = bm.loops.layers.uv.new("UVMap")
        thread = 0.055
        self.box(bm, uvl, top - Vector((0, 0, thread / 2)), (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))),
                 (0.0015, 0.0015, thread), "white")
        # The pine: three tiers and a trunk (the atlas's tree, build_game_textures.tree_outline), each a little in
        # front of the one above so they do not fight, on both sides of the card.
        w, h = 0.06, 0.09
        pieces = [[(0.5, 0.0), (0.75, 0.3), (0.25, 0.3)], [(0.5, 0.12), (0.84, 0.5), (0.16, 0.5)],
                  [(0.5, 0.28), (0.92, 0.72), (0.08, 0.72)], [(0.44, 0.72), (0.56, 0.72), (0.56, 0.9), (0.44, 0.9)]]
        # Turned a little, as it never hangs square.
        turn = Matrix.Rotation(math.radians(20), 3, "Z")
        right, normal = turn @ Vector((1, 0, 0)), turn @ Vector((0, -1, 0))
        origin = top - Vector((0, 0, thread))
        for side in (1, -1):
            for layer, piece in enumerate(pieces):
                lift = (0.0004 + 0.0002 * layer) * side
                verts = [bm.verts.new(origin + right * ((px - 0.5) * w) - Vector((0, 0, py * h)) + normal * lift) for px, py in piece]
                face = bm.faces.new(verts if side > 0 else list(reversed(verts)))
                for loop, vert in zip(face.loops, face.verts):
                    px, py = piece[verts.index(vert)]
                    loop[uvl].uv = rect_uv("tree", px, py)
        return self.new_object("Details_AirFreshener", bm, ["M_dash_labels"])


def build(kit, scene):
    """Adds the details to the kit as top-level parts. Returns their objects."""
    hidden = []
    for c in bpy.data.collections:
        if "REFERENCE" in c.name and not c.hide_viewport:
            c.hide_viewport = True
            hidden.append(c)
    moved = [o for o in kit.all_objects if o.name in NOT_STACK and not o.hide_viewport]
    for o in moved:
        o.hide_viewport = True
    b = Builder(kit, scene)
    b.stack_decals()
    b.trip_meter()
    b.extinguisher()
    b.air_freshener()
    for o in moved:
        o.hide_viewport = False
    for c in hidden:
        c.hide_viewport = False
    for o in b.objects:
        o["motion"] = "fixed"
    print("INTERIOR_DETAILS", [(o.name, len(o.data.polygons)) for o in b.objects])
    return b.objects
