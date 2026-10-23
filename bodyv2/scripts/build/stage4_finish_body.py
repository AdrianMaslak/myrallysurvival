"""Stage 4 (body): final materials, UV0, Livery on the new paint parts, Grime vertex colours, export.

Writes export/Octavia1U_Body_v2.blend (with REFERENCE_DoNotExport) and export/Octavia1U_Body_v2.fbx (BODY_KIT only).
"""
import bpy, bmesh, sys, os, math, json
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import classify
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform

SRC = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.path.join(ROOT, "work", "stage3.blend")
bpy.ops.wm.open_mainfile(filepath=SRC)
O = bpy.data.objects
KIT = bpy.data.collections["BODY_KIT"]
TEX = os.path.join(ROOT, "export", "textures")
meshes = [o for o in KIT.objects if o.type == 'MESH' and not o.name.startswith("UCX_")]

# ------------------------------------------------------------------ materials
TILE = dict(body_paint=1.0, underseal=0.5, plastic_black=0.25, chrome=0.25, metal_black=0.25, rubber=0.15, glass_clear=1.0,
            glass_tinted=1.0, lens_clear=0.12, lens_amber=0.12, lens_red=0.12, reflector=0.12, plate=1.0,
            interior_plastic=0.4, seat_fabric=0.3, headliner_cloth=0.5, carpet=0.5)
for k in ("lamp_lowbeam", "lamp_highbeam", "lamp_indicator", "lamp_fog", "lamp_tail", "lamp_brake", "lamp_reverse", "lamp_plate"): TILE[k] = 0.06
MAP = {"KIT_octavia1u": "body_paint", "KIT_octavia1u_inside": "body_paint", "KIT_octavia1u_plastic": "plastic_black",
       "KIT_octavia1u_undertray": "underseal", "KIT_octavia1u_buttons": "plastic_black", "KIT_octavia1u_doorarmrest": "interior_plastic",
       "KIT_octavia1u_doorpanelcolor": "seat_fabric", "KIT_octavia1u_chrome": "chrome", "KIT_octavia1u_skodalogo": "chrome",
       "KIT_Object_49": "chrome", "KIT_Object_43": "chrome", "KIT_octavia1u_lampglass": "lens_clear", "KIT_octavia1u_lampglassFL": "lens_red",
       "KIT_octavia1u_halogenlightframe": "reflector", "KIT_octavia1u_headliner": "headliner_cloth", "KIT_midsize_mechanical": "metal_black",
       "KIT_rubber_seal": "rubber", "KIT_plate": "plate", "KIT_octavia1u_glass": "glass_clear", "KIT_octavia1u_fog": "lamp_fog",
       "KIT_octavia1u_FL_signal_L": "lamp_indicator", "KIT_octavia1u_FL_brake": "lamp_brake"}


def make_material(name):
    m = bpy.data.materials.get("M_" + name)
    if m: return m
    m = bpy.data.materials.new("M_" + name)
    try: m.use_nodes = True
    except Exception: pass
    nt = m.node_tree; bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    def img(kind, noncolor, loc):
        p = os.path.join(TEX, f"{name}_{kind}.png")
        if not os.path.exists(p): return None
        im = bpy.data.images.load(p, check_existing=True)
        if noncolor: im.colorspace_settings.name = 'Non-Color'
        n = nt.nodes.new('ShaderNodeTexImage'); n.image = im; n.location = loc; return n
    b = img("basecolor", False, (-600, 300))
    ca = nt.nodes.new('ShaderNodeVertexColor'); ca.layer_name = "Grime"; ca.location = (-600, 520)
    mix = nt.nodes.new('ShaderNodeMix'); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'; mix.location = (-300, 400)
    mix.inputs[0].default_value = 1.0
    A = [i for i in mix.inputs if i.name == 'A' and i.type == 'RGBA'][0]; B = [i for i in mix.inputs if i.name == 'B' and i.type == 'RGBA'][0]
    R = [o for o in mix.outputs if o.type == 'RGBA'][0]
    nt.links.new(b.outputs['Color'], A); nt.links.new(ca.outputs['Color'], B); nt.links.new(R, bsdf.inputs['Base Color'])
    r = img("roughness", True, (-600, 0)); nt.links.new(r.outputs['Color'], bsdf.inputs['Roughness'])
    mt = img("metallic", True, (-600, -300)); nt.links.new(mt.outputs['Color'], bsdf.inputs['Metallic'])
    nm = img("normal", True, (-900, -600)); nmap = nt.nodes.new('ShaderNodeNormalMap'); nmap.location = (-300, -600)
    nt.links.new(nm.outputs['Color'], nmap.inputs['Color']); nt.links.new(nmap.outputs['Normal'], bsdf.inputs['Normal'])
    e = img("emissive", False, (-600, -900))
    if e:
        es = bsdf.inputs.get('Emission Color') or bsdf.inputs.get('Emission')
        nt.links.new(e.outputs['Color'], es)
        st = bsdf.inputs.get('Emission Strength')
        if st: st.default_value = 0.0          # the game switches the lamp on
    if name.startswith(("glass", "lens")):
        bsdf.inputs['Alpha'].default_value = 0.35 if name.startswith("glass") else 0.6
        try: m.surface_render_method = 'BLENDED'
        except Exception: pass
    m.diffuse_color = (*[min(1, x * 3) for x in (0.1, 0.15, 0.22)], 1) if name == "body_paint" else m.diffuse_color
    return m


tinted = {"Door_RL", "Door_RR", "DoorGlass_RL", "DoorGlass_RR", "Trunk"}
for ob in meshes:
    me = ob.data
    for i, s in enumerate(me.materials):
        nm = s.name if s else ""
        tgt = MAP.get(nm, nm if nm.startswith("lamp_") else None)
        if nm.startswith("KIT_octavia1u_lowbeam"): tgt = "lamp_lowbeam"
        if nm.startswith("KIT_octavia1u_signal"): tgt = "lamp_indicator" if not ob.name.startswith("Fender") else "lamp_indicator"
        if tgt == "glass_clear" and ob.name in tinted: tgt = "glass_tinted"
        if ob.name.startswith("Fender") and nm == "KIT_octavia1u_lampglass": tgt = "lens_amber"
        if ob.name.startswith("Headlight") and nm.startswith("KIT_octavia1u_signal"): tgt = "lamp_indicator"
        if tgt is None: tgt = "plastic_black"
        me.materials[i] = make_material(tgt)
    # merge duplicate slots
    names = [m.name for m in me.materials]
    first = {}
    for i, n in enumerate(names): first.setdefault(n, i)
    for p in me.polygons: p.material_index = first[names[p.material_index]]

# ------------------------------------------------------------------ UV0 (world-scale box projection per material) and Livery
chs = bmesh.new(); chs.from_mesh(O["Chassis"].data); chs.transform(O["Chassis"].matrix_world); bmesh.ops.triangulate(chs, faces=chs.faces)
ctree = BVHTree.FromBMesh(chs); chs.faces.ensure_lookup_table(); clay = chs.loops.layers.uv.get("Livery")
PAINT_PARTS = {"Chassis", "Door_FL", "Door_FR", "Door_RL", "Door_RR", "Bonnet", "Trunk", "FrontBumper", "RearBumper", "Fender_FL",
               "Fender_FR", "Roof", "FuelFlap"}
uv_report = {}
for ob in meshes:
    me = ob.data
    if "UVMap" not in me.uv_layers: me.uv_layers.new(name="UVMap")
    if ob.name in PAINT_PARTS and "Livery" not in me.uv_layers: me.uv_layers.new(name="Livery")
    # keep exactly UV0 'UVMap' and (paint parts) 'Livery'
    for l in [l for l in me.uv_layers if l.name not in ("UVMap", "Livery")]: me.uv_layers.remove(l)
    bm = bmesh.new(); bm.from_mesh(me)
    uv0 = bm.loops.layers.uv["UVMap"]; lv = bm.loops.layers.uv.get("Livery")
    Mw = ob.matrix_world; R3 = Mw.to_3x3()
    mnames = [m.name[2:] for m in me.materials]
    for f in bm.faces:
        t = TILE.get(mnames[f.material_index], 0.25)
        n = (R3 @ f.normal); ax = max(range(3), key=lambda i: abs(n[i]))
        for l in f.loops:
            c = Mw @ l.vert.co
            if ob.name.startswith("Plate_"):
                lc = l.vert.co; l[uv0].uv = (lc.x / 0.52 + 0.5, lc.y / 0.11 + 0.5); continue
            u, v = ((c.y, c.z) if ax == 0 else (c.x, c.z) if ax == 1 else (c.x, c.y))
            if ax == 0 and n.x < 0: u = -u
            if ax == 1 and n.y > 0: u = -u
            l[uv0].uv = (u / t, v / t)
    if ob.name == "FuelFlap" and lv is not None:
        for f in bm.faces:
            for l in f.loops:
                loc, nn, i, d = ctree.find_nearest(Mw @ l.vert.co)
                cf = chs.faces[i]; ls = list(cf.loops)
                r = barycentric_transform(loc, *(x.vert.co for x in ls[:3]), *(x[clay].uv.to_3d() for x in ls[:3]))
                l[lv].uv = (r.x, r.y)
    bm.to_mesh(me); bm.free()
    uv_report[ob.name] = list(me.uv_layers.keys())
    # UV0 first
    if me.uv_layers.keys() and me.uv_layers.keys()[0] != "UVMap": pass

# ------------------------------------------------------------------ Grime
kit_tree = classify.scene_tree(meshes)
wheels = [Vector((sx * 0.75, 1.263, 0.288)) for sx in (-1, 1)] + [Vector((sx * 0.70, -1.307, 0.272)) for sx in (-1, 1)]
GR = (0.30, 0.26, 0.21)
for ob in meshes:
    me = ob.data
    att = me.color_attributes.get("Grime") or me.color_attributes.new("Grime", 'BYTE_COLOR', 'POINT')
    for a in [a for a in me.color_attributes if a.name != "Grime"]: me.color_attributes.remove(a)
    me.color_attributes.active_color = me.color_attributes["Grime"]
    Mw = ob.matrix_world
    mnames = [m.name[2:] for m in me.materials]
    vmat = {}
    for p in me.polygons:
        for vi in p.vertices: vmat.setdefault(vi, set()).add(mnames[p.material_index])
    for v in me.vertices:
        c = Mw @ v.co
        g = 0.55 * max(0.0, min(1.0, (0.50 - c.z) / 0.40)) ** 1.5                 # lower body and sills
        ms = vmat.get(v.index, set())
        if "underseal" in ms: g = max(g, 0.65)                                      # underbody, arches
        if "carpet" in ms or (ob.name == "Chassis" and c.z < 0.45 and "body_paint" in ms and abs(c.x) < 0.68 and -1.3 < c.y < 0.9): g = max(g, 0.30)
        for w in wheels:
            d = math.hypot(c.y - w.y, c.z - w.z)
            if abs(c.x - w.x) < 0.25 and d < 0.48: g = max(g, 0.55 * max(0.0, 1 - (d - 0.30) / 0.18))
        if ob.name.startswith("Door_") and abs(c.x) < 0.76 and "body_paint" in ms: g = max(g, 0.25)    # door shuts
        if any(k in ms for k in ("glass_clear", "glass_tinted", "lens_clear", "lens_red", "lens_amber", "chrome")): g *= 0.4
        g = min(g, 0.85)
        att.data[v.index].color = (1 - (1 - GR[0]) * g, 1 - (1 - GR[1]) * g, 1 - (1 - GR[2]) * g, 1.0)

# ------------------------------------------------------------------ properties, normals
for ob in meshes:
    for p in ob.data.polygons: p.use_smooth = True
    try: ob.data.set_sharp_from_angle(angle=math.radians(40))
    except Exception: pass
    for a in [a for a in ob.data.attributes if a.name in ("outer", "seat", "lim", "ovid", "feat", "corner")]:
        try: ob.data.attributes.remove(a)
        except Exception: pass
json.dump(uv_report, open(os.path.join(ROOT, "work", "uv_report.json"), "w"), indent=1)

OUTB = os.path.join(ROOT, "export", "Octavia1U_Body_v2.blend")
bpy.ops.wm.save_as_mainfile(filepath=OUTB, compress=True, relative_remap=True)
# FBX: BODY_KIT only
for o in bpy.data.objects: o.select_set(False)
for o in KIT.objects: o.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, "export", "Octavia1U_Body_v2.fbx"), use_selection=True,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE', path_mode='RELATIVE',
                         add_leaf_bones=False, bake_anim=False, colors_type='SRGB')
print("exported body")
