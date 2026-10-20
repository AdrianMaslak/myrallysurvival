"""Even-quad remesher for open sheet panels (car body skins).

remesh_panel(bm_src, cell) takes a triangulated sheet (bmesh, world coordinates, one material) and returns a new
bmesh of quads about `cell` metres wide that lies on the source surface:

1. Feature edges (open borders, creases sharper than `crease_deg`, the x = 0 cut on panels that cross the centre
   line) are resampled once into shared vertices, so neighbouring patches meet without cracks.
2. The sheet is split along the features into patches; each patch is unwrapped (minimum stretch, scaled to metres).
3. A square grid in UV space gives the interior quads; a zipper strip joins the grid's border to the resampled
   feature curves (mostly quads, a few well-shaped triangles where the counts differ).
4. Narrow patches with no room for a grid fall back to a filled strip.
Every vertex is mapped back onto the original triangles, so the surface stays where it was.
"""
import bpy, bmesh, math
from mathutils import Vector, kdtree
from mathutils.geometry import intersect_point_tri_2d, barycentric_transform, intersect_point_line
from mathutils.bvhtree import BVHTree


# ------------------------------------------------------------------ helpers
def _unwrap(verts, faces, method='MINIMUM_STRETCH'):
    me = bpy.data.meshes.new("unwrap_tmp")
    me.from_pydata([tuple(v) for v in verts], [], faces)
    ob = bpy.data.objects.new("unwrap_tmp", me)
    bpy.context.scene.collection.objects.link(ob)
    for o in bpy.context.view_layer.objects: o.select_set(False)
    bpy.context.view_layer.objects.active = ob; ob.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    try:
        bpy.ops.uv.unwrap(method=method, margin=0.0, correct_aspect=True)
    except TypeError:
        bpy.ops.uv.unwrap(method='CONFORMAL', margin=0.0)
    bpy.ops.object.mode_set(mode='OBJECT')
    uv = [None] * len(verts)
    lay = me.uv_layers.active.data
    for p in me.polygons:
        for li in p.loop_indices:
            uv[me.loops[li].vertex_index] = Vector(lay[li].uv)
    bpy.data.objects.remove(ob); bpy.data.meshes.remove(me)
    return uv


def _distortion(faces):
    """p95/p5 of UV/3D edge-length ratio after a minimum-stretch unwrap (1 = isometric); flipped faces count as bad."""
    vset = {}
    for f in faces:
        for v in f.verts: vset.setdefault(v.index, len(vset))
    verts = [None] * len(vset)
    for f in faces:
        for v in f.verts: verts[vset[v.index]] = v.co.copy()
    tris = [[vset[v.index] for v in f.verts] for f in faces]
    uv = _unwrap(verts, tris)
    if any(u is None for u in uv): return 9.0
    areas = [_signed_area([uv[i] for i in t]) for t in tris]
    pos = sum(1 for a in areas if a > 0); neg = len(areas) - pos
    if min(pos, neg) > 0.01 * len(areas): return 9.0
    r = []
    for t in tris:
        for k in range(3):
            a, b = t[k], t[(k + 1) % 3]
            l3 = (verts[a] - verts[b]).length
            if l3 > 1e-6: r.append((uv[a] - uv[b]).length / l3)
    r.sort()
    if not r: return 1.0
    return r[int(len(r) * 0.95)] / max(1e-9, r[int(len(r) * 0.05)])


def _signed_area(pts):
    return 0.5 * sum(pts[i].x * pts[(i + 1) % len(pts)].y - pts[(i + 1) % len(pts)].x * pts[i].y for i in range(len(pts)))


def _loops_from_edges(edges):
    """edges: list of (a, b) directed or undirected vertex keys forming disjoint simple cycles -> list of vertex lists."""
    adj = {}
    for a, b in edges:
        adj.setdefault(a, []).append(b); adj.setdefault(b, []).append(a)
    seen, loops = set(), []
    for start in adj:
        if start in seen: continue
        loop = [start]; seen.add(start); prev, cur = None, start
        while True:
            nxt = [n for n in adj[cur] if n != prev]
            if not nxt: break
            n = nxt[0]
            if n == start: break
            if n in seen: break
            loop.append(n); seen.add(n); prev, cur = cur, n
        if len(loop) >= 3: loops.append(loop)
    return loops


# ------------------------------------------------------------------ main
def remesh_panel(src, cell=0.032, crease_deg=40.0, xcut=True, min_cell=0.010, log=None):
    """src: bmesh (world coords, triangles), returns (new bmesh, stats dict). src is not modified."""
    bm = src.copy()
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0003)
    bmesh.ops.dissolve_degenerate(bm, dist=0.0001, edges=bm.edges)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bmesh.ops.delete(bm, geom=[e for e in bm.edges if not e.link_faces], context='EDGES')
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.verts.ensure_lookup_table(); bm.edges.ensure_lookup_table(); bm.faces.ensure_lookup_table()
    bm.normal_update()
    ref_tree = BVHTree.FromBMesh(bm)
    ref_normals = [f.normal.copy() for f in bm.faces]

    # ---- feature edges
    crease = math.radians(crease_deg)
    feat = set()
    for e in bm.edges:
        if len(e.link_faces) != 2: feat.add(e.index); continue
        if e.calc_face_angle(0.0) > crease: feat.add(e.index)
    def prune():
        changed = True
        while changed:
            changed = False
            for v in bm.verts:
                es = [e for e in v.link_edges if e.index in feat]
                if len(es) == 1 and len(es[0].link_faces) == 2: feat.discard(es[0].index); changed = True
    prune()
    # ---- cut patches that do not flatten well (wrap-arounds) by a plane, until each unwraps cleanly
    def patches_of():
        seen, res = set(), []
        for f in bm.faces:
            if f.index in seen: continue
            st, cur = [f], []; seen.add(f.index)
            while st:
                g = st.pop(); cur.append(g)
                for e in g.edges:
                    if e.index in feat: continue
                    for h in e.link_faces:
                        if h.index not in seen: seen.add(h.index); st.append(h)
            res.append(cur)
        return res
    for it in range(4):
        bad = 0
        for faces in patches_of():
            if len(faces) < 8: continue
            if _distortion(faces) <= 1.40: continue
            cs = [f.calc_center_median() for f in faces]
            lo = Vector([min(c[i] for c in cs) for i in range(3)]); hi = Vector([max(c[i] for c in cs) for i in range(3)])
            ax = 0 if (xcut and lo.x < -0.15 and hi.x > 0.15) else max(range(3), key=lambda i: hi[i] - lo[i])
            mid = 0.0 if ax == 0 and lo.x < -0.15 and hi.x > 0.15 else sorted(c[ax] for c in cs)[len(cs) // 2]
            fs = {f.index for f in faces}
            for f in faces:
                for e in f.edges:
                    if e.index in feat or len(e.link_faces) != 2: continue
                    g = e.link_faces[0] if e.link_faces[1] is f else e.link_faces[1]
                    if g.index in fs and (f.calc_center_median()[ax] < mid) != (g.calc_center_median()[ax] < mid):
                        feat.add(e.index)
            bad += 1
        if not bad: break
        prune()
    def fdeg(v): return sum(1 for e in v.link_edges if e.index in feat)

    # ---- chains between nodes, resampled once (shared by every patch that borders them)
    out = bmesh.new()
    def is_node(v):
        d = fdeg(v)
        if d != 2: return d > 0
        es = [e for e in v.link_edges if e.index in feat]
        a = es[0].other_vert(v).co - v.co; b = es[1].other_vert(v).co - v.co
        if a.length < 1e-9 or b.length < 1e-9: return False
        return a.angle(b) < math.radians(125)
    node = {v.index for v in bm.verts if fdeg(v) > 0 and is_node(v)}
    node_out = {}
    def nv(vi):
        if vi not in node_out: node_out[vi] = out.verts.new(bm.verts[vi].co)
        return node_out[vi]
    edge_chain = {}            # (a, b) original vids -> (chain id, forward?)
    chains = []                # list of dict(vids=[...], samples=[(BMVert, (va, vb, t))...])
    used = set()
    def walk(v0, e0):
        vids = [v0.index]; v = v0; e = e0
        while True:
            used.add(e.index); w = e.other_vert(v); vids.append(w.index)
            if w.index in node: break
            nxt = [x for x in w.link_edges if x.index in feat and x.index not in used]
            if not nxt: break
            v, e = w, nxt[0]
        return vids
    for v in bm.verts:
        if v.index not in node: continue
        for e in v.link_edges:
            if e.index in feat and e.index not in used:
                chains.append(dict(vids=walk(v, e)))
    for e in bm.edges:                                    # closed chains without a node
        if e.index in feat and e.index not in used:
            v0 = e.verts[0]; node.add(v0.index)
            chains.append(dict(vids=walk(v0, e)))
    for ci, ch in enumerate(chains):
        vids = ch["vids"]; P = [bm.verts[i].co for i in vids]
        seg = [(P[i + 1] - P[i]).length for i in range(len(P) - 1)]; L = sum(seg)
        k = max(3 if vids[0] == vids[-1] else 1, int(round(L / cell)))
        samples = [(nv(vids[0]), (vids[0], vids[min(1, len(vids) - 1)], 0.0))]
        acc, si = 0.0, 0
        for s in range(1, k):
            target = L * s / k
            while si < len(seg) - 1 and acc + seg[si] < target: acc += seg[si]; si += 1
            t = (target - acc) / seg[si] if seg[si] > 1e-12 else 0.0
            p = P[si].lerp(P[si + 1], t)
            samples.append((out.verts.new(p), (vids[si], vids[si + 1], t)))
        samples.append((nv(vids[-1]), (vids[-2], vids[-1], 1.0)))
        ch["samples"] = samples
        for i in range(len(vids) - 1):
            edge_chain[(vids[i], vids[i + 1])] = (ci, True); edge_chain[(vids[i + 1], vids[i])] = (ci, False)

    # ---- split into patches along the features
    ovid = bm.verts.layers.int.new("ovid")
    for v in bm.verts: v[ovid] = v.index
    split_edges = [e for e in bm.edges if e.index in feat and len(e.link_faces) == 2]
    if split_edges: bmesh.ops.split_edges(bm, edges=split_edges)
    bm.verts.ensure_lookup_table(); bm.faces.ensure_lookup_table()
    seen, patches = set(), []
    for f in bm.faces:
        if f.index in seen: continue
        stack, cur = [f], []
        seen.add(f.index)
        while stack:
            g = stack.pop(); cur.append(g)
            for e in g.edges:
                for h in e.link_faces:
                    if h.index not in seen: seen.add(h.index); stack.append(h)
        patches.append(cur)

    stats = dict(patches=len(patches), grid_patches=0, strip_patches=0, cells=[])
    for faces in patches:
        _patch(bm, faces, ovid, chains, edge_chain, out, cell, min_cell, stats, log, set(node_out.values()))

    # ---- orientation: match the original surface normals
    out.faces.ensure_lookup_table(); out.normal_update()
    for f in out.faces:
        loc, n, idx, d = ref_tree.find_nearest(f.calc_center_median())
        if idx is not None and f.normal.dot(ref_normals[idx]) < 0: f.normal_flip()
    out.normal_update()
    bmesh.ops.remove_doubles(out, verts=out.verts, dist=0.0002)
    bmesh.ops.delete(out, geom=[v for v in out.verts if not v.link_faces], context='VERTS')
    bm.free()
    return out, stats


def _patch(bm, faces, ovid, chains, edge_chain, out, cell, min_cell, stats, log, nodes):
    vset = {}
    for f in faces:
        for v in f.verts:
            if v.index not in vset: vset[v.index] = len(vset)
    verts = [None] * len(vset)
    for vi, k in vset.items(): verts[k] = bm.verts[vi].co.copy()
    tris = [[vset[v.index] for v in f.verts] for f in faces]
    uv = _unwrap(verts, tris)
    if any(u is None for u in uv):
        uv = _unwrap(verts, tris, 'CONFORMAL')
    a3 = sum(f.calc_area() for f in faces)
    a2 = abs(sum(_signed_area([uv[i] for i in t]) for t in tris))
    s = math.sqrt(a3 / a2) if a2 > 1e-12 else 1.0
    uv = [u * s for u in uv]
    # patch border loops (as split-mesh vertex indices) in UV order
    bedges = []
    fset = {f.index for f in faces}
    for f in faces:
        for e in f.edges:
            if sum(1 for g in e.link_faces if g.index in fset) == 1: bedges.append((e.verts[0].index, e.verts[1].index))
    bloops = _loops_from_edges(bedges)
    # each border loop -> shared samples, with this patch's UV for each sample
    border = []
    for lp in bloops:
        ov = [bm.verts[i][ovid] for i in lp]
        n = len(lp)
        def chain_starts_here(i):
            key = (ov[i], ov[(i + 1) % n])
            if key not in edge_chain: return False
            ci, fwd = edge_chain[key]; ch = chains[ci]
            return (ch["vids"][0] if fwd else ch["vids"][-1]) == ov[i]
        starts = [i for i in range(n) if chain_starts_here(i)]
        if not starts: continue
        i0 = starts[0]; seq = []; i = i0; guard = 0
        uvmap = {}
        for j in range(n): uvmap.setdefault(ov[j], []).append(uv[vset[lp[j]]])
        while guard < n + 5:
            guard += 1
            key = (ov[i % n], ov[(i + 1) % n])
            if key not in edge_chain: break
            ci, fwd = edge_chain[key]; ch = chains[ci]
            smp = ch["samples"] if fwd else list(reversed(ch["samples"]))
            nseg = len(ch["vids"]) - 1
            for (bv, (va, vb, t)) in smp[:-1]:
                # UV of the sample from this patch's border vertices
                ua, ub = uvmap.get(va, [None])[0], uvmap.get(vb, [None])[0]
                if ua is None or ub is None: u = None
                else: u = ua.lerp(ub, t)
                seq.append((bv, u))
            i += nseg
            if i % n == i0 % n or i >= i0 + n: break
        seq = [q for q in seq if q[1] is not None]
        if len(seq) >= 3: border.append(seq)
    if not border:
        return
    # UV triangle lookup
    tri_uv = [[uv[i] for i in t] for t in tris]
    kd = kdtree.KDTree(len(tris))
    for i, t in enumerate(tri_uv): kd.insert(((t[0].x + t[1].x + t[2].x) / 3, (t[0].y + t[1].y + t[2].y) / 3, 0), i)
    kd.balance()
    rad = max(max((t[0] - t[1]).length, (t[1] - t[2]).length, (t[2] - t[0]).length) for t in tri_uv)
    def locate(p):
        for (co, i, d) in kd.find_range((p.x, p.y, 0), rad):
            t = tri_uv[i]
            if intersect_point_tri_2d(p, t[0], t[1], t[2]):
                a, b, c = (verts[k] for k in tris[i])
                return barycentric_transform(p.to_3d(), t[0].to_3d(), t[1].to_3d(), t[2].to_3d(), a, b, c)
        return None
    # border points in UV for the distance test
    bk_pts = []
    for lp in bloops:
        for j in range(len(lp)):
            pa, pb = uv[vset[lp[j]]], uv[vset[lp[(j + 1) % len(lp)]]]
            m = max(1, int((pa - pb).length / (cell / 8)))
            for q in range(m): bk_pts.append(pa.lerp(pb, q / m))
    bkd = kdtree.KDTree(len(bk_pts))
    for i, p in enumerate(bk_pts): bkd.insert((p.x, p.y, 0), i)
    bkd.balance()
    lo = Vector((min(u.x for u in uv), min(u.y for u in uv))); hi = Vector((max(u.x for u in uv), max(u.y for u in uv)))

    c = cell
    while c >= min_cell - 1e-9:
        res = _grid(c, lo, hi, locate, bkd, border)
        if res is not None: break
        c *= 0.75
    if res is None:
        _strip(border, out, nodes); stats["strip_patches"] += 1; return
    gpos, cells, gloops = res
    stats["grid_patches"] += 1; stats["cells"].append(round(c, 4))
    gv = {k: out.verts.new(p3) for k, (p2, p3) in gpos.items()}
    for (i, j) in cells:
        try: out.faces.new((gv[(i, j)], gv[(i + 1, j)], gv[(i + 1, j + 1)], gv[(i, j + 1)]))
        except ValueError: pass
    # match grid loops to border loops and zip them together
    gl_uv = [[gpos[k][0] for k in lp] for lp in gloops]
    bd_uv = [[u for (bv, u) in b] for b in border]
    def cen(pts): return sum(pts, Vector((0, 0))) / len(pts)
    for gi, lp in enumerate(gloops):
        ga = _signed_area(gl_uv[gi])
        # nearest border loop by mean distance from grid loop points
        best, bi = 1e9, None
        for k, b in enumerate(bd_uv):
            d = sum(min((p - q).length for q in b[::max(1, len(b) // 40)]) for p in gl_uv[gi][::max(1, len(gl_uv[gi]) // 20)])
            if d < best: best, bi = d, k
        if bi is None: continue
        A = [gv[k] for k in lp]; Auv = gl_uv[gi]
        B = [bv for (bv, u) in border[bi]]; Buv = bd_uv[bi]
        if (_signed_area(Buv) > 0) != (ga > 0): B = B[::-1]; Buv = Buv[::-1]
        j0 = min(range(len(Buv)), key=lambda j: (Buv[j] - Auv[0]).length)
        B = B[j0:] + B[:j0]; Buv = Buv[j0:] + Buv[:j0]
        _zip(A, Auv, B, Buv, out)


def _grid(c, lo, hi, locate, bkd, border):
    margin = 0.45 * c
    i0, i1 = int(math.floor(lo.x / c)) - 1, int(math.ceil(hi.x / c)) + 1
    j0, j1 = int(math.floor(lo.y / c)) - 1, int(math.ceil(hi.y / c)) + 1
    ok = {}
    for i in range(i0, i1 + 1):
        for j in range(j0, j1 + 1):
            p = Vector((i * c, j * c))
            if bkd.find((p.x, p.y, 0))[2] < margin: continue
            p3 = locate(p)
            if p3 is not None: ok[(i, j)] = (p, p3)
    cells = {(i, j) for (i, j) in ok if (i + 1, j) in ok and (i, j + 1) in ok and (i + 1, j + 1) in ok}
    # remove pinches (vertex shared by two diagonal cells only) and lone cells
    for _ in range(20):
        bad = set()
        corners = {}
        for (i, j) in cells:
            for k in ((i, j), (i + 1, j), (i, j + 1), (i + 1, j + 1)): corners.setdefault(k, []).append((i, j))
        for k, cs in corners.items():
            if len(cs) == 2:
                (a, b), (p, q) = cs
                if abs(a - p) == 1 and abs(b - q) == 1: bad.add(min(cs))
        for (i, j) in cells:
            nb = sum(1 for d in ((1, 0), (-1, 0), (0, 1), (0, -1)) if (i + d[0], j + d[1]) in cells)
            if nb == 0 and len(cells) > 1: bad.add((i, j))
        if not bad: break
        cells -= bad
    if not cells: return None
    edges = {}
    for (i, j) in cells:
        for e in (((i, j), (i + 1, j)), ((i + 1, j), (i + 1, j + 1)), ((i + 1, j + 1), (i, j + 1)), ((i, j + 1), (i, j))):
            key = tuple(sorted(e))
            edges[key] = edges.get(key, 0) + 1
    bedges = [k for k, n in edges.items() if n == 1]
    gloops = _loops_from_edges(bedges)
    # every border loop should get exactly one grid loop (otherwise the grid is broken into islands: retry finer)
    if len(gloops) != len(border):
        # tolerate missing grid loops for tiny holes, but not islands
        comp = _cell_components(cells)
        if len(comp) > 1: return None
        if len(gloops) > len(border): return None
    used = {k for (i, j) in cells for k in ((i, j), (i + 1, j), (i, j + 1), (i + 1, j + 1))}
    gpos = {k: ok[k] for k in used}
    return gpos, cells, gloops


def _cell_components(cells):
    seen, comps = set(), []
    for c0 in cells:
        if c0 in seen: continue
        st = [c0]; seen.add(c0); n = 0
        while st:
            (i, j) = st.pop(); n += 1
            for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                q = (i + d[0], j + d[1])
                if q in cells and q not in seen: seen.add(q); st.append(q)
        comps.append(n)
    return comps


def _zip(A, Auv, B, Buv, out):
    def cum(P):
        t = [0.0]
        for i in range(1, len(P) + 1): t.append(t[-1] + (P[i % len(P)] - P[i - 1]).length)
        return [x / t[-1] for x in t]
    tA, tB = cum(Auv), cum(Buv)
    nA, nB = len(A), len(B)
    i = j = 0
    stepA, stepB = 1.0 / nA, 1.0 / nB
    def face(vs):
        if len(set(vs)) < len(vs): return
        try: out.faces.new(vs)
        except ValueError: pass
    while i < nA or j < nB:
        a0, a1 = A[i % nA], A[(i + 1) % nA]; b0, b1 = B[j % nB], B[(j + 1) % nB]
        if i >= nA: face((a0, b0, b1)); j += 1; continue
        if j >= nB: face((a0, b0, a1)); i += 1; continue
        ta, tb = tA[i + 1], tB[j + 1]
        if abs(ta - tb) < 0.5 * min(stepA, stepB):
            face((a0, b0, b1, a1)); i += 1; j += 1
        elif ta < tb:
            face((a0, b0, a1)); i += 1
        else:
            face((a0, b0, b1)); j += 1


def _zip_open(A, Auv, B, Buv, out):
    def cum(P):
        t = [0.0]
        for i in range(1, len(P)): t.append(t[-1] + (P[i] - P[i - 1]).length)
        return [x / max(t[-1], 1e-12) for x in t]
    tA, tB = cum(Auv), cum(Buv); nA, nB = len(A), len(B)
    i = j = 0
    def face(vs):
        if len(set(vs)) < len(vs): return
        try: out.faces.new(vs)
        except ValueError: pass
    while i < nA - 1 or j < nB - 1:
        if i >= nA - 1: face((A[i], B[j], B[j + 1])); j += 1; continue
        if j >= nB - 1: face((A[i], B[j], A[i + 1])); i += 1; continue
        ta, tb = tA[i + 1], tB[j + 1]
        if abs(ta - tb) < 0.5 * min(1.0 / (nA - 1), 1.0 / (nB - 1)):
            face((A[i], B[j], B[j + 1], A[i + 1])); i += 1; j += 1
        elif ta < tb: face((A[i], B[j], A[i + 1])); i += 1
        else: face((A[i], B[j], B[j + 1])); j += 1


def _strip(border, out, nodes):
    """Narrow patch with no room for a grid: a ribbon between its two long sides (or its two loops)."""
    if len(border) == 2:
        (A, B) = border
        Auv = [u for _, u in A]; Buv = [u for _, u in B]; Av = [v for v, _ in A]; Bv = [v for v, _ in B]
        if (_signed_area(Auv) > 0) != (_signed_area(Buv) > 0): Bv = Bv[::-1]; Buv = Buv[::-1]
        j0 = min(range(len(Buv)), key=lambda j: (Buv[j] - Auv[0]).length)
        _zip(Av, Auv, Bv[j0:] + Bv[:j0], Buv[j0:] + Buv[:j0], out); return
    L = border[0]; n = len(L)
    nidx = [i for i in range(n) if L[i][0] in nodes]
    if len(nidx) >= 4 or (len(nidx) >= 2 and n >= 6):
        segs = []
        for k in range(len(nidx)):
            i0, i1 = nidx[k], nidx[(k + 1) % len(nidx)]
            seg = [L[(i0 + m) % n] for m in range(((i1 - i0) % n or n) + 1)]
            ln = sum((seg[m + 1][1] - seg[m][1]).length for m in range(len(seg) - 1))
            segs.append((ln, k, seg))
        top = sorted(segs, key=lambda x: -x[0])[:2]
        if len(top) == 2 and top[1][0] > 0:
            (l1, k1, X), (l2, k2, Y) = sorted(top, key=lambda x: x[1])
            A = [v for v, _ in X]; Auv = [u for _, u in X]
            B = [v for v, _ in Y][::-1]; Buv = [u for _, u in Y][::-1]
            _zip_open(A, Auv, B, Buv, out)
            # close the two short ends with fans
            for k in range(len(nidx)):
                if k in (k1, k2): continue
                seg = [q for (ln, kk, q) in segs if kk == k][0]
                vs = [v for v, _ in seg]
                if len(vs) >= 3:
                    for m in range(1, len(vs) - 1):
                        try: out.faces.new((vs[0], vs[m], vs[m + 1]))
                        except ValueError: pass
            return
    vs = [bv for (bv, u) in L]
    if len(set(vs)) < 3: return
    try: f = out.faces.new(vs)
    except ValueError: return
    r = bmesh.ops.triangulate(out, faces=[f], quad_method='BEAUTY', ngon_method='BEAUTY')
    bmesh.ops.join_triangles(out, faces=r["faces"], angle_face_threshold=math.radians(40), angle_shape_threshold=math.radians(40))
