"""Build six reusable roadside meshes with matching convex UCX collision in Blender.

blender --background --factory-startup --python scripts/build_countryside_roadside.py
Owns only Art/Maps/Countryside/Roadside; does not rebuild the existing map art.
"""
import json
import math
import random
import sys
from pathlib import Path

import bpy
import bmesh

sys.path.insert(0, str(Path(__file__).resolve().parent))
from countryside_roadside import ART, SHAPES, make_layout

EXPORT = ART / 'Export'
EXPORT.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system = 'METRIC'
bpy.context.scene.unit_settings.scale_length = 1.0
palette = {'Earth': (0.19, 0.125, 0.065), 'Grass': (0.16, 0.205, 0.085),
           'Stone': (0.30, 0.29, 0.25), 'StoneLight': (0.39, 0.37, 0.31),
           'Bark': (0.12, 0.073, 0.038), 'CutWood': (0.37, 0.25, 0.12)}
materials = {}
for name, color in palette.items():
    mat = bpy.data.materials.new('M_Roadside_' + name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*color, 1)
    bsdf.inputs['Roughness'].default_value = 0.95
    materials[name] = mat


def hull(name, vertices, slots, kind):
    data = bpy.data.meshes.new(name)
    data.from_pydata([(x, -y, z) for x, y, z in vertices], [], [])
    bm = bmesh.new()
    bm.from_mesh(data)
    bmesh.ops.convex_hull(bm, input=list(bm.verts), use_existing_faces=False)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(data)
    bm.free()
    data.update()
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    for slot in slots:
        data.materials.append(materials[slot])
    for face in data.polygons:
        if kind == 'Berm':
            face.material_index = 1 if face.normal.z > 0.65 else 0
        elif kind == 'Stump':
            face.material_index = 1 if face.normal.z > 0.8 else 0
        else:
            face.material_index = int(face.index % 5 == 0)
    uv = data.uv_layers.new(name='UVMap')
    for face in data.polygons:
        for li in face.loop_indices:
            v = data.vertices[data.loops[li].vertex_index].co
            uv.data[li].uv = (v.x, v.y)
    # A separate convex hull per reusable obstacle, not one hull around a whole route.
    collision = bpy.data.objects.new('UCX_' + name + '_00', data.copy())
    bpy.context.collection.objects.link(collision)
    collision.hide_render = True
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    collision.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=str(EXPORT / (name + '.fbx')), use_selection=True,
        object_types={'MESH'}, axis_forward='-Y', axis_up='Z', apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_ALL', bake_anim=False, use_mesh_modifiers=True,
        mesh_smooth_type='FACE', use_triangles=True, add_leaf_bones=False)
    return dict(name=name, file=name + '.fbx', kind=kind, materials=slots,
                bounds_m=[[min(v.co[k] for v in data.vertices) for k in range(3)],
                          [max(v.co[k] for v in data.vertices) for k in range(3)]])


assets = []
for number, (kind, size) in enumerate(SHAPES.items()):
    sx, sy, sz = size
    x, y = sx / 2, sy / 2
    if kind == 'Berm':
        vertices = [(-x, -y, -0.08), (x, -y, -0.08), (x, y, -0.08), (-x, y, -0.08),
                    (-x, -0.08, 0.20), (x, -0.08, 0.23), (-0.4, 0.08, 0.28),
                    (0.75, 0.13, 0.25)]
        slots = ['Earth', 'Grass']
    elif kind == 'StoneBoundary':
        vertices = [(-x, -y, -0.10), (x, -y, -0.10), (x, y, -0.10), (-x, y, -0.10),
                    (-x, -y * .75, .58), (x, -y * .7, .62), (x, y * .75, .66),
                    (-x, y * .8, .64)]
        slots = ['Stone', 'StoneLight']
    elif kind == 'Stump':
        vertices = []
        for z, radius in [(-0.10, 0.34), (0.13, 0.25), (0.68, 0.22)]:
            for i in range(10):
                a = math.tau * i / 10
                vertices.append((radius * math.cos(a), radius * math.sin(a), z))
        slots = ['Bark', 'CutWood']
    else:
        rng = random.Random(130 + number)
        vertices = []
        for level, z, factor in [(0, -0.12, 0.85), (1, sz * .35, 1.0), (2, sz - .12, .52)]:
            for i in range(7):
                a = math.tau * i / 7 + level * .19
                radial = factor * rng.uniform(.86, 1)
                vertices.append((x * radial * math.cos(a), y * radial * math.sin(a),
                                 z if level == 0 else z - rng.uniform(0, .05)))
        slots = ['Stone', 'StoneLight']
    assets.append(hull('SM_Roadside_' + kind, vertices, slots, kind))

manifest = make_layout()
manifest.update(materials=palette, assets=assets)
(ART / 'roadside.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(ART / 'Roadside.blend'))
print(f'COUNTRYSIDE_ROADSIDE_ART_PASS: {len(assets)} meshes; {len(manifest["pockets"])} pockets; '
      f'{len(manifest["objects"])} obstacles; {manifest["counts"]}')
