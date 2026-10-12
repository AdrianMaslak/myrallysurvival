"""Make the Octavia's paint paintable (docs/PAINTING.md). Run once, with the .blend closed in Blender:
blender --background --factory-startup --python scripts/add_livery_uvs.py

The model's own UV maps overlap (badges, bumpers and body share the same space) and the body is split
into many small pieces, so neither its maps nor an automatic unwrap make a usable paint layout. This adds
a "Livery" UV map to every object using M_Prototype_Paint, projected like a livery blueprint: each painted
face goes to the view it faces most (left side, right side, top, front or rear; faces pointing down
share the top view) and the five views are laid out in one square texture at one scale:

    +-----------------------+
    | left side (front <-)  |
    | right side (-> front) |
    | top (front up)        |
    | front     | rear      |
    +-----------------------+

It writes Art/Vehicles/Octavia/Prototype/Textures/T_Octavia_Livery.png (4096 px, filled with the current
paint colour; this is the image to paint) and T_Octavia_Livery_Template.png (the car's faces and edges in
each view, to paint over as a guide layer), and wires the livery image into the Blender material through
the Livery map, so Texture Paint mode paints straight onto the car too. Then it saves the .blend.

Refuses to run if a Livery map already exists: whatever was painted depends on it staying where it is.
"""
from pathlib import Path
import bpy
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Art/Vehicles/Octavia/Prototype/Octavia_With_Wheels.blend"
TEXTURES = ROOT / "Art/Vehicles/Octavia/Prototype/Textures"
IMAGE = TEXTURES / "T_Octavia_Livery.png"
TEMPLATE = TEXTURES / "T_Octavia_Livery_Template.png"
PAINT = "M_Prototype_Paint"
LIVERY = "Livery"
SIZE = 4096
GAP = 0.03  # metres between views

bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
paint = bpy.data.materials[PAINT]
objects = [o for o in bpy.data.objects
           if o.type == "MESH" and o.data.polygons and any(s.material == paint for s in o.material_slots)]
if any(LIVERY in o.data.uv_layers for o in objects):
    raise RuntimeError("A Livery UV map already exists; refusing to project again over painted work.")


def unreal_points(o):
    # World positions in the Unreal frame, metres: X forward, Y right, Z up (source model is -Y forward).
    m = np.array(o.matrix_world)
    co = np.array([v.co for v in o.data.vertices]).reshape(-1, 3)
    world = co @ m[:3, :3].T + m[:3, 3]
    return np.stack([-world[:, 1], -world[:, 0], world[:, 2]], 1)


def unreal_normals(o):
    m = np.array(o.matrix_world)[:3, :3]
    n = np.array([p.normal for p in o.data.polygons]).reshape(-1, 3) @ np.linalg.inv(m)
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    return np.stack([-n[:, 1], -n[:, 0], n[:, 2]], 1)


# Extent of the paint, Unreal frame.
painted = []
for o in objects:
    slots = {i for i, s in enumerate(o.material_slots) if s.material == paint}
    faces = [p for p in o.data.polygons if p.material_index in slots]
    painted.append((o, faces, unreal_points(o), unreal_normals(o)))
all_points = np.concatenate([pts[[v for f in faces for v in f.vertices]] for _, faces, pts, _ in painted])
lo, hi = all_points.min(0), all_points.max(0)
length, width, height = hi - lo
total_height = 3 * height + width + 4 * GAP
scale = 1.0 / max(total_height, length + 2 * GAP)
# Each view: which two coordinates make u and v (as functions of a point) and where its box starts (metres
# from the bottom-left of the texture).
row_rear = GAP
row_top = row_rear + height + GAP
row_right = row_top + width + GAP
row_left = row_right + height + GAP
VIEWS = {
    # Standing at the car's left, its front is on your left.
    "left": (lambda p: (hi[0] - p[:, 0], p[:, 2] - lo[2]), (GAP, row_left)),
    "right": (lambda p: (p[:, 0] - lo[0], p[:, 2] - lo[2]), (GAP, row_right)),
    # From above, front up, the car's left on the left.
    "top": (lambda p: (p[:, 1] - lo[1], p[:, 0] - lo[0]), None),
    # From in front the car's left is on your right; from behind its right is.
    "front": (lambda p: (hi[1] - p[:, 1], p[:, 2] - lo[2]), (GAP, row_rear)),
    "rear": (lambda p: (p[:, 1] - lo[1], p[:, 2] - lo[2]), (GAP + width + GAP, row_rear)),
}


def to_uv(view, p):
    project, origin = VIEWS[view]
    u, v = project(p)
    if view == "top":
        # Lying across the texture: the car's length runs left to right, front at the right.
        u, v = (p[:, 0] - lo[0]), (hi[1] - p[:, 1])
        origin = (GAP, row_top)
    return np.stack([(origin[0] + u) * scale, (origin[1] + v) * scale], 1)


def view_of(n):
    ax = np.abs(n)
    if ax[1] >= ax[0] and ax[1] >= ax[2]:
        return "left" if n[1] < 0 else "right"
    if ax[0] >= ax[2]:
        return "front" if n[0] > 0 else "rear"
    return "top"


triangles = []  # (uv triangle, shade) for the template
for o, faces, pts, normals in painted:
    layer = o.data.uv_layers.new(name=LIVERY)
    uv = np.zeros((len(o.data.loops), 2))
    for f in faces:
        view = view_of(normals[f.index])
        loops = list(f.loop_indices)
        verts = [o.data.loops[i].vertex_index for i in loops]
        uv[loops] = to_uv(view, pts[verts])
        shade = 0.55 + 0.45 * float(np.max(np.abs(normals[f.index])))
        for k in range(1, len(loops) - 1):
            triangles.append((uv[[loops[0], loops[k], loops[k + 1]]].copy(), shade))
    layer.data.foreach_set("uv", uv.ravel())
    o.data.uv_layers.active = o.data.uv_layers[0]

# The image to paint, filled with the paint colour.
principled = next(n for n in paint.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
colour = tuple(principled.inputs["Base Color"].default_value)
TEXTURES.mkdir(parents=True, exist_ok=True)
image = bpy.data.images.new("T_Octavia_Livery", SIZE, SIZE, alpha=False)
image.generated_color = colour
image.filepath_raw = str(IMAGE)
image.file_format = "PNG"
image.save()
image.filepath = bpy.path.relpath(str(IMAGE))

# The template: painted faces shaded by how squarely they face their view, edges drawn darker,
# transparent elsewhere.
canvas = np.zeros((SIZE, SIZE, 4), np.float32)
for tri, shade in triangles:
    p = tri * SIZE
    x0, y0 = np.floor(p.min(0)).astype(int)
    x1, y1 = np.ceil(p.max(0)).astype(int)
    x0, y0 = max(x0, 0), max(y0, 0)
    x1, y1 = min(x1, SIZE), min(y1, SIZE)
    if x1 <= x0 or y1 <= y0:
        continue
    xs, ys = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
    a, b, c = p
    d1 = (xs - b[0]) * (a[1] - b[1]) - (a[0] - b[0]) * (ys - b[1])
    d2 = (xs - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (ys - c[1])
    d3 = (xs - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (ys - a[1])
    inside = ~(((d1 < 0) | (d2 < 0) | (d3 < 0)) & ((d1 > 0) | (d2 > 0) | (d3 > 0)))
    region = canvas[y0:y1, x0:x1]
    region[inside] = (shade, shade, shade, 1.0)
for tri, _ in triangles:
    p = tri * SIZE
    for a, b in ((p[0], p[1]), (p[1], p[2]), (p[2], p[0])):
        steps = int(max(abs(b[0] - a[0]), abs(b[1] - a[1]))) + 1
        t = np.linspace(0.0, 1.0, steps)[:, None]
        q = np.clip((a + (b - a) * t).astype(int), 0, SIZE - 1)
        canvas[q[:, 1], q[:, 0]] = (0.25, 0.25, 0.25, 1.0)
template = bpy.data.images.new("T_Octavia_Livery_Template", SIZE, SIZE, alpha=True)
template.pixels.foreach_set(canvas.ravel())
template.filepath_raw = str(TEMPLATE)
template.file_format = "PNG"
template.save()
bpy.data.images.remove(template)

# Blender shows and paints the livery image through the Livery map.
nodes = paint.node_tree.nodes
uv_node = nodes.new("ShaderNodeUVMap")
uv_node.uv_map = LIVERY
uv_node.location = (principled.location.x - 600, principled.location.y)
texture = nodes.new("ShaderNodeTexImage")
texture.image = image
texture.location = (principled.location.x - 350, principled.location.y)
paint.node_tree.links.new(uv_node.outputs["UV"], texture.inputs["Vector"])
paint.node_tree.links.new(texture.outputs["Color"], principled.inputs["Base Color"])
# Texture Paint paints into the material's active image node.
nodes.active = texture

bpy.ops.wm.save_mainfile()
print(f"LIVERY_DONE {len(objects)} objects, {len(triangles)} triangles, paint extent {np.round(hi - lo, 2)} m, "
      f"{scale * SIZE / 100:.1f} px per cm, colour {[round(c, 3) for c in colour[:3]]}")
