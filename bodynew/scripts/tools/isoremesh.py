"""Isotropic remesher with quad output, for car body skins.

remesh(src_bm, size_of_face, feature_deg) -> new bmesh, all quads.

1. Isotropic triangle remeshing of the source sheet at a target edge length 2*Q (Botsch & Kobbelt: split long edges,
   collapse short ones, flip toward valence 6, tangential relaxation, projection back onto the source triangles).
   Open borders and folds sharper than `feature_deg` are kept as feature curves: their vertices slide only along them,
   and their corners never move.
2. Every triangle is split into three quads (centre + edge midpoints), giving an all-quad mesh with edges about Q,
   then a light tangential relax.
The target size can vary per source face (e.g. finer on the outer skin, coarser on inner skins).
"""
import bmesh, math
from mathutils import Vector, kdtree
from mathutils.bvhtree import BVHTree
from mathutils.geometry import intersect_point_line


class _FeatureCurves:
    def __init__(self, segs):
        self.segs = segs
        self.kd = kdtree.KDTree(max(1, len(segs)))
        for i, (a, b) in enumerate(segs): self.kd.insert((a + b) / 2, i)
        self.kd.balance()

    def project(self, p):
        if not self.segs: return p
        best, bp = 1e9, p
        for (_, i, _) in self.kd.find_n(p, 8):
            a, b = self.segs[i]
            q, t = intersect_point_line(p, a, b)
            t = max(0.0, min(1.0, t)); q = a.lerp(b, t)
            d = (q - p).length
            if d < best: best, bp = d, q
        return bp


def remesh(src, size_of_face, feature_deg=55.0, iters=7, smooth_quads=2, log=None, lmin=0.035, grow=2.0, eps=0.003, curv_of_face=None, mode='split3', k_split=1.33, k_collapse=0.8):
    ref = src.copy()
    bmesh.ops.remove_doubles(ref, verts=ref.verts, dist=0.0003)
    bmesh.ops.dissolve_degenerate(ref, dist=0.0001, edges=ref.edges)
    bmesh.ops.delete(ref, geom=[v for v in ref.verts if not v.link_faces], context='VERTS')
    bmesh.ops.triangulate(ref, faces=ref.faces)
    ref.faces.ensure_lookup_table(); ref.normal_update()
    tree = BVHTree.FromBMesh(ref)
    ref_n = [f.normal.copy() for f in ref.faces]
    fa = math.radians(feature_deg)

    def is_feat_ref(e):
        return len(e.link_faces) != 2 or e.calc_face_angle(0.0) > fa
    curves = _FeatureCurves([(e.verts[0].co.copy(), e.verts[1].co.copy()) for e in ref.edges if is_feat_ref(e)])
    # target size: the caller's size, finer close to feature curves (folds, borders, narrow flanges)
    ref_size = []
    for f in ref.faces:
        c = f.calc_center_median()
        dfeat = (curves.project(c) - c).length if curves.segs else 9.0
        # curvature: radius from the bend to each neighbour face, edge length for a ~1.5 mm chord error
        lc = 9.0
        for e in f.edges:
            if len(e.link_faces) != 2: continue
            g = e.link_faces[0] if e.link_faces[1] is f else e.link_faces[1]
            th = f.normal.angle(g.normal, 0.0)
            if th < 1e-3 or th > fa: continue
            R = (g.calc_center_median() - c).length / th
            lc = min(lc, math.sqrt(8 * eps * R) * 2.0)     # x2: the tri edge is twice the final quad edge
        base = size_of_face(f)
        if base > 0.1:      # coarse (inner) skin: only the feature limit, with a larger floor
            ref_size.append(min(base, max(2 * lmin, grow * dfeat + lmin)))
        else:
            ref_size.append(min(base, max(lmin, lc), max(lmin, grow * dfeat + lmin * 0.5)))

    if log:
        rs = sorted(ref_size); log(f'    size p10 {rs[len(rs)//10]:.3f} p50 {rs[len(rs)//2]:.3f} p90 {rs[len(rs)*9//10]:.3f}; base p50 {sorted(size_of_face(f) for f in ref.faces)[len(rs)//2]:.3f}')
    bm = ref.copy()
    FE = bm.edges.layers.int.new("feat")
    for e in bm.edges: e[FE] = 1 if is_feat_ref(e) else 0
    def size_at(p):
        loc, n, i, d = tree.find_nearest(p)
        return ref_size[i] if i is not None else 0.06
    # smooth (Phong) surface through the source: per-corner normals averaged over faces within 40 deg
    ref.faces.ensure_lookup_table()
    cn = []
    for f in ref.faces:
        row = []
        for v in f.verts:
            acc = Vector()
            for g in v.link_faces:
                if g.normal.dot(f.normal) > 0.766: acc += g.normal * g.calc_area()
            row.append((v.co.copy(), acc.normalized() if acc.length > 1e-12 else f.normal.copy()))
        cn.append(row)
    def phong(loc, i, alpha=0.75):
        (pa, na), (pb, nb), (pc, nc) = cn[i][:3]
        from mathutils.geometry import barycentric_transform
        w = barycentric_transform(loc, pa, pb, pc, Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))
        u, v, t = max(0, w.x), max(0, w.y), max(0, w.z); ssum = u + v + t
        if ssum < 1e-9: return loc
        u, v, t = u / ssum, v / ssum, t / ssum
        q = (loc - na * (loc - pa).dot(na)) * u + (loc - nb * (loc - pb).dot(nb)) * v + (loc - nc * (loc - pc).dot(nc)) * t
        return loc.lerp(q, alpha)
    def proj(p, hint=None):
        """Point on the smooth (Phong) version of the source; with a normal hint, only on faces facing the same
        way (stops jumps between two sheets close together, e.g. inner and outer skins at a door edge)."""
        loc, n, i, d = tree.find_nearest(p)
        if loc is None: return p
        if hint is None or hint.length < 1e-6 or ref_n[i].dot(hint) > 0.2: return phong(loc, i)
        best = None
        for sgn in (1, -1):
            h = tree.ray_cast(p, hint * sgn, 0.04)
            if h[0] is not None and ref_n[h[2]].dot(hint) > 0.2:
                if best is None or h[3] < best[1]: best = (h[0], h[3], h[2])
        return phong(best[0], best[2]) if best else p
    def vfeat(v): return sum(1 for e in v.link_edges if e[FE])
    def corner(v):
        k = vfeat(v)
        if k == 0: return False
        if k != 2: return True
        es = [e for e in v.link_edges if e[FE]]
        a = es[0].other_vert(v).co - v.co; b = es[1].other_vert(v).co - v.co
        if a.length < 1e-9 or b.length < 1e-9: return True
        return a.angle(b) < math.radians(140)
    CORNER = bm.verts.layers.int.new("corner")
    for v in bm.verts: v[CORNER] = 1 if corner(v) else 0

    import time as _t
    if log: log(f'      initial nm={sum(1 for e in bm.edges if len(e.link_faces) > 2)} faces={len(bm.faces)}')
    for it in range(iters):
        _t0 = _t.time()
        # ---- split long edges (one edge per triangle per pass; each split triangle is cut midpoint -> opposite corner)
        for _ in range(12):
            long_e = [e for e in bm.edges if e.calc_length() > k_split * 0.5 * (size_at(e.verts[0].co) + size_at(e.verts[1].co))]
            if not long_e: break
            long_e.sort(key=lambda e: -e.calc_length())
            busy = set(); did = 0
            for e in long_e:
                if not e.is_valid: continue
                lf = list(e.link_faces)
                if any(f in busy or len(f.verts) != 3 for f in lf): continue
                busy.update(lf)
                f = e[FE]; a, b = e.verts
                mid = (a.co + b.co) / 2
                ne, nv = bmesh.utils.edge_split(e, a, 0.5)
                L0 = (a.co - b.co).length
                q = mid if f else proj(mid, (a.normal + b.normal))
                nv.co = q if (q - mid).length < 0.25 * L0 else mid
                nv[CORNER] = 0
                for x in nv.link_edges: x[FE] = f
                for face in list(nv.link_faces):
                    if len(face.verts) != 4: continue
                    vs = list(face.verts); k = vs.index(nv)
                    opp = vs[(k + 2) % 4]
                    res = bmesh.utils.face_split(face, nv, opp)
                    if res and res[1] is not None: res[1].edge[FE] = 0
                did += 1
            if not did: break
        if log: log(f'      after split nm={sum(1 for e in bm.edges if len(e.link_faces) > 2)}')
        # ---- collapse short edges (independent set per pass, welded in one call)
        for _cp in range(4):
            short = [e for e in bm.edges if e.calc_length() < k_collapse * 0.5 * (size_at(e.verts[0].co) + size_at(e.verts[1].co))]
            short.sort(key=lambda e: e.calc_length())
            locked = set(); tmap = {}
            for e in short:
                a, b = e.verts
                if a in locked or b in locked: continue
                ca, cb = a[CORNER], b[CORNER]
                fa_, fb_ = vfeat(a) > 0, vfeat(b) > 0
                if fa_ or fb_ or ca or cb: continue      # feature curves stay exactly as in the source
                if e[FE]:
                    if ca: keep, gone, pos = a, b, a.co.copy()
                    elif cb: keep, gone, pos = b, a, b.co.copy()
                    else: keep, gone, pos = a, b, curves.project((a.co + b.co) / 2)
                elif fa_ or ca: keep, gone, pos = a, b, a.co.copy()
                elif fb_ or cb: keep, gone, pos = b, a, b.co.copy()
                else: keep, gone, pos = a, b, proj((a.co + b.co) / 2, (a.normal + b.normal))
                na = {x.other_vert(a) for x in a.link_edges}; nb = {x.other_vert(b) for x in b.link_edges}
                if any(n in locked for n in na | nb): continue
                shared = na & nb
                if len(shared) > (2 if len(e.link_faces) == 2 else 1): continue
                lim = k_split * size_at(pos)
                if any((n.co - pos).length > lim for n in (na | nb) - {a, b}): continue
                okflip = True
                for v in (a, b):
                    for f in v.link_faces:
                        if a in f.verts and b in f.verts: continue
                        co = [pos if (x is a or x is b) else x.co for x in f.verts]
                        n_new = (co[1] - co[0]).cross(co[2] - co[0])
                        if n_new.length < 1e-12 or n_new.normalized().dot(f.normal) < 0.3: okflip = False; break
                    if not okflip: break
                if not okflip: continue
                cflag = max(a[CORNER], b[CORNER]); keep[CORNER] = cflag
                keep.co = pos; tmap[gone] = keep
                locked |= na | nb | {a, b}
            if tmap: bmesh.ops.weld_verts(bm, targetmap=tmap)
            if not tmap: break
        if log: log(f'      after collapse nm={sum(1 for e in bm.edges if len(e.link_faces) > 2)}')
        bmesh.ops.dissolve_degenerate(bm, dist=1e-6, edges=bm.edges)
        ng = [f for f in bm.faces if len(f.verts) > 3]
        if ng: bmesh.ops.triangulate(bm, faces=ng)
        bm.normal_update()
        # ---- flip toward valence 6 (4 on borders)
        def tval(v): return 6 if vfeat(v) == 0 else 4
        for e in list(bm.edges):
            if not e.is_valid or e[FE] or len(e.link_faces) != 2: continue
            f1, f2 = e.link_faces
            if len(f1.verts) != 3 or len(f2.verts) != 3: continue
            a, b = e.verts
            c = [v for v in f1.verts if v not in (a, b)][0]; d = [v for v in f2.verts if v not in (a, b)][0]
            if c is d or any(x.other_vert(c) is d for x in c.link_edges): continue
            va, vb, vc, vd = (len(v.link_edges) for v in (a, b, c, d))
            before = abs(va - tval(a)) + abs(vb - tval(b)) + abs(vc - tval(c)) + abs(vd - tval(d))
            after = abs(va - 1 - tval(a)) + abs(vb - 1 - tval(b)) + abs(vc + 1 - tval(c)) + abs(vd + 1 - tval(d))
            if after >= before: continue
            n1 = (a.co - c.co).cross(d.co - c.co); n2 = (b.co - d.co).cross(c.co - d.co)
            if n1.length < 1e-12 or n2.length < 1e-12: continue
            fn = (f1.normal + f2.normal).normalized()
            if abs(n1.normalized().dot(fn)) < 0.5 or abs(n2.normalized().dot(fn)) < 0.5: continue
            bmesh.utils.edge_rotate(e, True)
        if log: log(f'      after flip nm={sum(1 for e in bm.edges if len(e.link_faces) > 2)}')
        bm.normal_update()
        # ---- tangential relaxation + projection
        _relax(bm, FE, CORNER, curves, proj, 0.5)
        if log: log(f'    iter {it}: {len(bm.faces)} tris, {_t.time() - _t0:.1f}s')
    # final: split anything still over the limit (relaxation can stretch a few edges)
    for _ in range(6):
        long_e = [e for e in bm.edges if e.calc_length() > k_split * 0.5 * (size_at(e.verts[0].co) + size_at(e.verts[1].co))]
        if not long_e: break
        busy = set()
        for e in sorted(long_e, key=lambda e: -e.calc_length()):
            if not e.is_valid: continue
            lf = list(e.link_faces)
            if any(f in busy or len(f.verts) != 3 for f in lf): continue
            busy.update(lf)
            f = e[FE]; a, b = e.verts; mid = (a.co + b.co) / 2; L0 = (a.co - b.co).length
            ne, nv = bmesh.utils.edge_split(e, a, 0.5)
            q = mid if f else proj(mid, (a.normal + b.normal))
            nv.co = q if (q - mid).length < 0.25 * L0 else mid
            nv[CORNER] = 0
            for x in nv.link_edges: x[FE] = f
            for face in list(nv.link_faces):
                if len(face.verts) != 4: continue
                vs = list(face.verts); k = vs.index(nv)
                res = bmesh.utils.face_split(face, nv, vs[(k + 2) % 4])
                if res and res[1] is not None: res[1].edge[FE] = 0
        bm.normal_update()
    stats_tri = len(bm.faces)

    if mode == 'join':
        # pair triangles into quads (no feature edge removed), relax, orient
        cand = [e for e in bm.edges if not e[FE]]
        bmesh.ops.join_triangles(bm, faces=bm.faces, angle_face_threshold=math.radians(40), angle_shape_threshold=math.radians(85))
        bm.normal_update()
        out = bm
        out.normal_update()
        for f in out.faces:
            loc, n, i, d = tree.find_nearest(f.calc_center_median())
            if i is not None and f.normal.dot(ref_n[i]) < 0: f.normal_flip()
        out.normal_update(); ref.free()
        return out, dict(tris=stats_tri, quads=sum(1 for f in out.faces if len(f.verts) == 4))
    # ---- triangles -> 3 quads each
    out = bmesh.new()
    OFE = out.edges.layers.int.new("feat"); OC = out.verts.layers.int.new("corner")
    vmap = {v: out.verts.new(v.co) for v in bm.verts}
    emid = {}
    for e in bm.edges:
        m = (e.verts[0].co + e.verts[1].co) / 2
        emid[e] = out.verts.new(m if e[FE] else proj(m, e.verts[0].normal + e.verts[1].normal))
    for f in bm.faces:
        vs = list(f.verts)
        if len(vs) != 3: continue
        c = out.verts.new(proj(sum((v.co for v in vs), Vector()) / 3, f.normal))
        es = {frozenset((x.verts[0], x.verts[1])): x for x in f.edges}
        for i in range(3):
            v, vp, vn = vs[i], vs[i - 1], vs[(i + 1) % 3]
            m1 = emid[es[frozenset((v, vn))]]; m0 = emid[es[frozenset((vp, v))]]
            try: out.faces.new((vmap[v], m1, c, m0))
            except ValueError: pass
    # mark feature edges in the output (halves of feature edges)
    for e in bm.edges:
        if not e[FE]: continue
        for v in e.verts:
            oe = out.edges.get((vmap[v], emid[e]))
            if oe: oe[OFE] = 1
    for v in bm.verts: vmap[v][OC] = v[CORNER]
    out.normal_update()
    for _ in range(smooth_quads): _relax(out, OFE, OC, curves, proj, 0.35)
    # orientation as the source
    out.normal_update()
    for f in out.faces:
        loc, n, i, d = tree.find_nearest(f.calc_center_median())
        if i is not None and f.normal.dot(ref_n[i]) < 0: f.normal_flip()
    out.normal_update()
    ref.free(); bm.free()
    return out, dict(tris=stats_tri, quads=len(out.faces))


def _relax(bm, FE, CORNER, curves, proj, lam):
    newpos = {}
    for v in bm.verts:
        if v[CORNER]: continue
        fe = [e for e in v.link_edges if e[FE]]
        if fe:
            continue
            q = (fe[0].other_vert(v).co + fe[1].other_vert(v).co) / 2
            t = v.co.lerp(q, lam); pp = curves.project(t)
            newpos[v] = pp if (pp - t).length < 0.01 else t
        else:
            nb = [e.other_vert(v).co for e in v.link_edges]
            if not nb: continue
            q = sum(nb, Vector()) / len(nb)
            n = v.normal
            d = q - v.co; d -= n * d.dot(n)
            t = v.co + d * lam; pp = proj(t, n)
            newpos[v] = pp if (pp - t).length < 0.01 else t
    for v, p in newpos.items(): v.co = p
    bm.normal_update()
