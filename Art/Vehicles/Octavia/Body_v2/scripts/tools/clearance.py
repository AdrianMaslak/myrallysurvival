"""Clearance machinery shared by the baseline check and the final verification.

Tyre motion follows the v2 suspension: fronts steer +-35 deg about the kingpin (strut top -> ball joint) and travel
+-8 cm with the lower arm swinging about its bushing axis; rears travel +-8 cm.
"""
import bpy, math
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

HP = dict(F_TOP=Vector((-0.583, 1.249, 0.784)), F_BJ=Vector((-0.726, 1.321, 0.178)),
          F_ARM_PIV=Vector((-0.374, 1.200, 0.185)), F_ARM_RP=Vector((-0.330, 0.920, 0.183)))

def verts_w(o, M=None):
    M = M if M is not None else o.matrix_world
    return [M @ v.co for v in o.data.vertices]

def tree_of(o, M=None):
    vs = verts_w(o, M)
    return BVHTree.FromPolygons(vs, [tuple(p.vertices) for p in o.data.polygons], epsilon=0.0), vs

def rot_about(p0, axis, ang):
    return Matrix.Translation(p0) @ Matrix.Rotation(ang, 4, axis) @ Matrix.Translation(-p0)

def wheel_poses(side):
    """Yield (label, matrix) world transforms for a tyre at its rest pose -> moved pose."""
    sx = 1 if side in ("FL", "RL") else -1
    S = lambda v: Vector((v.x * sx, v.y, v.z))
    if side[0] == "F":
        top, bj, fp, rp = S(HP["F_TOP"]), S(HP["F_BJ"]), S(HP["F_ARM_PIV"]), S(HP["F_ARM_RP"])
        kp = (top - bj).normalized(); arm_axis = (rp - fp).normalized()
        for steer in (-35, -25, 0, 25, 35):
            for dz in (-0.08, -0.04, 0.0, 0.04, 0.08):
                r_arm = (bj - fp) - arm_axis * (bj - fp).dot(arm_axis)
                ang = math.asin(max(-1, min(1, dz / max(1e-6, r_arm.length))))
                sgn = 1 if (arm_axis.cross(r_arm)).z > 0 else -1
                bj2 = rot_about(fp, arm_axis, sgn * ang) @ bj
                yield (steer, dz), Matrix.Translation(bj2 - bj) @ rot_about(bj, kp, math.radians(steer))
    else:
        for dz in (-0.08, -0.04, 0.0, 0.04, 0.06, 0.08):
            yield (0, dz), Matrix.Translation((0, 0, dz))

def min_gap(src_vs, src_faces, tgt_tree, maxd=0.30):
    """Min distance from the source vertices to the target surface; negative (estimated depth) if they intersect."""
    best, at = 9.0, None
    for w in src_vs:
        h = tgt_tree.find_nearest(w, maxd)
        if h[0] is not None and h[3] < best: best, at = h[3], h[0]
    st = BVHTree.FromPolygons(src_vs, src_faces, epsilon=0.0)
    ov = st.overlap(tgt_tree)
    if ov:
        # depth: deepest source vertex behind the target's nearest face
        deep = 0.0
        for w in src_vs:
            h = tgt_tree.find_nearest(w, 0.2)
            if h[0] is None: continue
            d = (w - h[0]).dot(h[1])
            if d < 0: deep = max(deep, min(-d, 0.2))
        return -max(deep, 0.001), at, len(ov)
    return best, at, 0
