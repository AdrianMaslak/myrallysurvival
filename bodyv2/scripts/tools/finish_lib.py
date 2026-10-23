"""Shared finishing helpers: PBR materials with the Grime multiply, world-scale UV0, preview actions."""
import bpy, bmesh, os, math
from mathutils import Vector, Matrix

TILE = dict(body_paint=1.0, underseal=0.5, plastic_black=0.25, chrome=0.25, metal_black=0.25, rubber=0.15, glass_clear=1.0,
            glass_tinted=1.0, lens_clear=0.12, lens_amber=0.12, lens_red=0.12, reflector=0.12, plate=1.0,
            interior_plastic=0.4, seat_fabric=0.3, headliner_cloth=0.5, carpet=0.5)


def make_material(name, tex):
    m = bpy.data.materials.get("M_" + name)
    if m and m.node_tree and any(n.type == 'TEX_IMAGE' for n in m.node_tree.nodes): return m
    m = m or bpy.data.materials.new("M_" + name)
    try: m.use_nodes = True
    except Exception: pass
    nt = m.node_tree; bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    def img(kind, noncolor, loc):
        p = os.path.join(tex, f"{name}_{kind}.png")
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
    if b: nt.links.new(b.outputs['Color'], A)
    nt.links.new(ca.outputs['Color'], B); nt.links.new(R, bsdf.inputs['Base Color'])
    r = img("roughness", True, (-600, 0))
    if r: nt.links.new(r.outputs['Color'], bsdf.inputs['Roughness'])
    mt = img("metallic", True, (-600, -300))
    if mt: nt.links.new(mt.outputs['Color'], bsdf.inputs['Metallic'])
    nm = img("normal", True, (-900, -600))
    if nm:
        nmap = nt.nodes.new('ShaderNodeNormalMap'); nmap.location = (-300, -600)
        nt.links.new(nm.outputs['Color'], nmap.inputs['Color']); nt.links.new(nmap.outputs['Normal'], bsdf.inputs['Normal'])
    e = img("emissive", False, (-600, -900))
    if e:
        es = bsdf.inputs.get('Emission Color') or bsdf.inputs.get('Emission')
        nt.links.new(e.outputs['Color'], es)
        st = bsdf.inputs.get('Emission Strength')
        if st: st.default_value = 1.0 if name.startswith("gauge") else 0.0
    if name.startswith(("glass", "lens")):
        bsdf.inputs['Alpha'].default_value = 0.35 if name.startswith("glass") else 0.6
        try: m.surface_render_method = 'BLENDED'
        except Exception: pass
    return m


def box_uv(ob, keep_names=("GaugeFaces", "WarningLights")):
    me = ob.data
    if "UVMap" not in me.uv_layers: me.uv_layers.new(name="UVMap")
    if ob.name in keep_names: return
    bm = bmesh.new(); bm.from_mesh(me); uv0 = bm.loops.layers.uv["UVMap"]
    Mw = ob.matrix_world; R3 = Mw.to_3x3()
    mnames = [m.name[2:] if m else "" for m in me.materials]
    for f in bm.faces:
        t = TILE.get(mnames[f.material_index] if mnames else "", 0.25)
        n = (R3 @ f.normal); ax = max(range(3), key=lambda i: abs(n[i]))
        for l in f.loops:
            c = Mw @ l.vert.co
            u, v = ((c.y, c.z) if ax == 0 else (c.x, c.z) if ax == 1 else (c.x, c.y))
            l[uv0].uv = (u / t, v / t)
    bm.to_mesh(me); bm.free()


def grime(ob, fn, GR=(0.30, 0.26, 0.21)):
    me = ob.data
    att = me.color_attributes.get("Grime") or me.color_attributes.new("Grime", 'BYTE_COLOR', 'POINT')
    for a in [a for a in me.color_attributes if a.name != "Grime"]: me.color_attributes.remove(a)
    me.color_attributes.active_color = me.color_attributes["Grime"]
    Mw = ob.matrix_world
    for v in me.vertices:
        g = max(0.0, min(0.85, fn(Mw @ v.co)))
        att.data[v.index].color = (1 - (1 - GR[0]) * g, 1 - (1 - GR[1]) * g, 1 - (1 - GR[2]) * g, 1.0)


def preview_action(ob, frames=48):
    """one Action per moving object, running its full range (for checking only)."""
    mo = ob.get("motion")
    if mo not in ("rotate", "slide"): return None
    r0, r1 = list(ob["range"])
    base = ob.matrix_basis.copy()
    ob.animation_data_create()
    act = bpy.data.actions.new(f"{ob.name}_preview"); ob.animation_data.action = act
    ax = ob["local_axis"]
    def pose(t):
        val = r0 + (r1 - r0) * t
        if ob.get("local_axis") == "hinge_line":
            a = Vector(ob["hinge_a_m"]); b = Vector(ob["hinge_b_m"]); axis = Vector(ob["open_axis"]).normalized()
            M = Matrix.Translation(a) @ Matrix.Rotation(math.radians(val), 4, axis) @ Matrix.Translation(-a)
            return "world", M
        if mo == "slide":
            v = Vector((0, 0, 0)); v["XYZ".index(ax)] = val
            return "basis", base @ Matrix.Translation(v)
        k = ax[0] if ax in ("X", "Y", "Z") else "Y"
        return "basis", base @ Matrix.Rotation(math.radians(val), 4, k)
    W0 = ob.matrix_world.copy()
    for i, t in enumerate((0.0, 0.25, 0.5, 0.75, 1.0, 0.5, 0.0)):
        f = 1 + int(i * frames / 6)
        kind, M = pose(t if ob.get("rest") != "middle" else (0.5 + 0.5 * math.sin(2 * math.pi * i / 6)))
        if kind == "world":
            ob.matrix_world = M @ W0
        else:
            ob.matrix_basis = M
        ob.keyframe_insert("location", frame=f); ob.keyframe_insert("rotation_euler", frame=f)
    ob.matrix_basis = base
    return act
