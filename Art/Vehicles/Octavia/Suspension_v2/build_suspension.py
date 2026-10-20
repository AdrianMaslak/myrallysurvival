"""Škoda Octavia I (1U) 1.8T — front and rear suspension and brakes, both sides, for a game.

Run:  python3 build_suspension.py        (needs the `bpy` module and textures from make_textures_susp.py)
Frame: metres, Blender Z-up. +X car's right, +Y car's front, +Z up. Origin on the ground under the middle
of the car. Left side is built from the hard points below; the right side is mirrored in X (except the
driveshafts, which differ left/right because the gearbox sits left of centre).
"""
import bpy, bmesh, math, json, os, sys, csv
from mathutils import Vector, Matrix
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import partlib as PL
from partlib import Part, V, mirror_v, finalize, add_empty

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "export"); os.makedirs(OUT, exist_ok=True)
PL.TEX = os.path.join(OUT, "textures")

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
for cname in ["Front", "Rear", "Shared"]:
    c = bpy.data.collections.new(cname); scene.collection.children.link(c); PL.COLL[cname] = c
X_, Y_, Z_ = V(1, 0, 0), V(0, 1, 0), V(0, 0, 1)
nX, nY, nZ = -X_, -Y_, -Z_

# ============================================================== HARD POINTS (left side, as given)
HP = dict(
    F_WHEEL=V(-0.751, 1.263, 0.288), F_TOP=V(-0.593, 1.249, 0.784), F_CLAMP=V(-0.681, 1.291, 0.317),
    F_ARM_PIV=V(-0.374, 1.200, 0.185), F_BJ=V(-0.735, 1.321, 0.178), F_TRO=V(-0.695, 1.153, 0.258),
    F_RACK_X=0.35, F_DS_IN_L=V(-0.408, 1.353, 0.302), F_DS_IN_R=V(-0.015, 1.353, 0.302),
    R_WHEEL=V(-0.702, -1.307, 0.272), R_DAMP_TOP=V(-0.525, -1.275, 0.645), R_DAMP_BOT=V(-0.571, -1.317, 0.160),
    R_TA_PIV=V(-0.515, -0.842, 0.344), R_TA_CARRIER=V(-0.680, -1.307, 0.362),
    R_LL_IN=V(-0.335, -1.184, 0.278), R_LL_OUT=V(-0.669, -1.188, 0.240),
    R_UL_IN=V(-0.322, -1.244, 0.360), R_UL_OUT=V(-0.687, -1.247, 0.370),
    R_DS_IN_L=V(-0.201, -1.275, 0.295), R_DS_IN_R=V(0.201, -1.275, 0.295),
    R_DS_OUT_L=V(-0.640, -1.307, 0.272), R_DS_OUT_R=V(0.640, -1.307, 0.272),
)
# Allowed tolerance is 1 cm. The strut axis is moved 9 mm inboard (top and clamp) so the 45 mm damper
# tube clears the tyre's inner sidewall; every other point is used exactly.
STRUT_SHIFT = V(0.010, 0, 0)
F_TOP = HP["F_TOP"] + STRUT_SHIFT
F_CLAMP = HP["F_CLAMP"] + STRUT_SHIFT
F_BJ = HP["F_BJ"] + V(0.009, 0, 0)   # 9 mm inboard so the ball-joint housing clears the 288 mm disc
# v2: the rear damper's lower mount moves 90 mm rearward so the damper clears the new rear driveshaft
# (outer boot and shaft) by 15 mm or more over +-8 cm travel; the top mount stays.
R_DAMP_BOT = HP["R_DAMP_BOT"] + V(0, -0.090, 0)
USED = dict(HP); USED["F_TOP"] = F_TOP; USED["F_CLAMP"] = F_CLAMP; USED["F_BJ"] = F_BJ; USED["R_DAMP_BOT"] = R_DAMP_BOT
# the rear anti-roll-bar link moves rearward with it (it would sit where the damper now passes)
R_ARB_Y = -1.460
# the rear caliper turns from 155 to 165 deg (round the disc, 0 = front, 90 = up) so its guide bolts back out past the
# moved damper; the damper now comes off before the caliper carrier (removal stage 2) because it covers the carrier bolts
R_CAL_TH = 165.0

TYRE_R, TYRE_W, ET = 0.310, 0.205, 0.038

# source notes
S_TT = "TightTorque Golf IV (Mk4) torque table (fastener listed; count/size approximate)"
S_TT_W = "TightTorque Golf IV: wheel bolts M14x1.5; 5x100 PCD (Octavia 1U spec)"
S_BJ = "jettatech ball-joint guide (Mk3 Jetta, same design carried to Mk4): 3 bolts 13 mm, pinch bolt + nut 17 mm"
APPROX = "approximate"

def polar(W, a, r, th, tang=0.0):
    """Point at axial offset a (+X inboard for the left side), radius r at angle th (0 = +Y/front, 90 = up)."""
    th = math.radians(th)
    n = V(0, math.cos(th), math.sin(th)); t = V(0, -math.sin(th), math.cos(th))
    return W + V(a, 0, 0) + n * r + t * tang

def rot_basis(th):
    """Basis with X along the wheel axis, Y tangential, Z radial at angle th."""
    th = math.radians(th)
    x = V(1, 0, 0); z = V(0, math.cos(th), math.sin(th)); y = z.cross(x)
    return Matrix((x, y, z)).transposed()

# ============================================================== WHEEL (rim + tyre)
import wheel_profile as WP
def build_wheel(sfx, W, group):
    tyre = Part(f"Tyre_{sfx}", group, "195/65 R15 road tyre, 634 mm overall, 200 mm section width; four main grooves in the mesh, "
                "tread blocks, sipes, shoulder blocks and sidewall lettering in the normal map.", "Held by the rim.", side=sfx, stage=0)
    tyre.origin = W; tyre.spin_axis = [1, 0, 0]; tyre.flip_u = True
    # one texture layout for the whole tyre: U = 2 repeats round, V = 0..1 along the cross-section
    tyre.revolve(WP.TYRE_PROFILE, W, X_, "tyre", segs=36, closed=True, u_reps=WP.TYRE_U_REPS, v_norm=True)

    rim = Part(f"Rim_{sfx}", group, "Steel 6Jx15 ET38, 5x100, centre bore 57.1 mm, silver painted: dished centre with "
               "5 stud holes in pressed conical seats, 6 oval vent holes, rolled outer lip, bead seats with safety humps, valve stem.",
               "5 x M14x1.5 wheel bolts, 17 mm.", side=sfx, stage=0)
    rim.origin = W; rim.spin_axis = [1, 0, 0]
    rim.revolve(WP.RIM_BARREL, W, X_, "rim_silver", segs=30, closed=True)
    # centre disc with stud and vent holes (exact boolean), segments line up with 5 studs and 6 vents
    cutters = []
    for k in range(5):
        t = bmesh.new(); bmesh.ops.create_cone(t, cap_ends=True, cap_tris=False, segments=8, radius1=0.0075, radius2=0.0075, depth=0.040)
        bmesh.ops.transform(t, matrix=Matrix.Translation(polar(W, -0.040, WP.PCD_R, 90 + 72 * k)) @ PL.align_z(X_), verts=t.verts)
        cutters.append(t)
    for k in range(WP.VENT_COUNT):
        th = WP.VENT_START + 360.0 / WP.VENT_COUNT * k; tr = math.radians(th)
        n = V(0, math.cos(tr), math.sin(tr)); tg = V(0, -math.sin(tr), math.cos(tr))
        t = bmesh.new(); bmesh.ops.create_cone(t, cap_ends=True, cap_tris=False, segments=12, radius1=1.0, radius2=1.0, depth=0.070)
        for v in t.verts: v.co = Vector((v.co.x * WP.VENT_HALF_W, v.co.y * WP.VENT_HALF_LEN, v.co.z))
        B = Matrix((n, tg, X_)).transposed().to_4x4()
        bmesh.ops.transform(t, matrix=Matrix.Translation(polar(W, -0.050, WP.VENT_R, th)) @ B, verts=t.verts)
        cutters.append(t)
    rim.revolve_cut(WP.RIM_DISC, W, X_, "rim_silver", cutters, segs=20)
    for k in range(5):     # pressed bosses round the stud holes: 60 deg conical seat for the bolt
        rim.revolve([(0.0075, 0.0), (0.0075, 0.0025), (0.0095, 0.0085), (0.0140, 0.0075), (0.0145, 0.0)],
                    polar(W, -0.040, WP.PCD_R, 90 + 72 * k), nX, "rim_silver", segs=8)
        rim.fastener("wheelbolt", polar(W, -0.0425, WP.PCD_R, 90 + 72 * k), nX, "M14", real="17 mm socket",
                     label="wheel bolt", source=S_TT_W)
    # valve stem through the well, tip visible through the vent hole at 200 deg
    v0, v1 = polar(W, -0.030, 0.171, 200), polar(W, -0.040, 0.140, 200)
    rim.cyl_between(v0, v1, 0.0055, "rubber", segs=8, r2=0.0042)
    rim.cyl_between(v1, v1 + (v1 - v0).normalized() * 0.008, 0.0040, "zinc", segs=8)
    rim.point(f"Rim_{sfx}_ValveStem", polar(W, -0.030, 0.178, 200), polar(W, 0, 1, 200) - W)
    return tyre, rim

# ============================================================== BRAKES (disc, carrier, caliper, pads)
def build_brakes(sfx, W, group, front, knuckle_name):
    if front:
        RO, RI, PLATE, GAP, HAT_R, CAL_TH, AC = 0.144, 0.085, 0.009, 0.007, 0.083, 135.0, -0.008
    else:
        RO, RI, PLATE, GAP, HAT_R, CAL_TH, AC = 0.116, 0.062, 0.009, 0.0, 0.073, R_CAL_TH, -0.0075
    disc = Part(f"BrakeDisc_{sfx}", group, ("Ventilated 288 x 25 mm" if front else "Solid 232 x 9 mm") + " cast-iron disc, machined friction faces.",
                "1 disc retaining screw.", side=sfx, stage=4)
    disc.origin = W; disc.spin_axis = [1, 0, 0]
    half = (2 * PLATE + GAP) / 2 if front else PLATE / 2
    if front:
        for a0 in (AC - half, AC + GAP / 2):
            disc.revolve([(RI, a0), (RO, a0), (RO, a0 + PLATE), (RI, a0 + PLATE)], W, X_, "disc_face", segs=24)
        for k in range(12):                                   # cooling vanes between the plates
            th = 30 * k
            disc.box((GAP, 0.004, RO - RI - 0.012), polar(W, AC, (RO + RI) / 2, th), "cast_iron", basis=rot_basis(th))
    else:
        disc.revolve([(RI, AC - half), (RO, AC - half), (RO, AC + half), (RI, AC + half)], W, X_, "disc_face", segs=24)
    # hat: face against the hub flange at a = -0.038..-0.033, wall out to the friction ring
    disc.revolve([(0.0335, -0.038), (HAT_R + 0.003, -0.038), (HAT_R + 0.003, AC - half), (HAT_R, AC - half),
                  (HAT_R, -0.033), (0.0335, -0.033)], W, X_, "cast_iron", segs=32)
    disc.fastener("screw", polar(W, -0.038, 0.045, 126), nX, "M8", real="Torx T30 (approximate)",
                  label="disc retaining screw", source=S_TT)

    th = CAL_TH; B = rot_basis(th)
    # carrier: two arms either side of the pads, bridges over the disc edge, tie bar inside
    car = Part(f"CaliperCarrier_{sfx}", group, "Cast-iron caliper carrier (pad bracket).", "2 carrier bolts into the knuckle/hub carrier.",
               side=sfx, stage=3)
    car.origin = W; car.spin_axis = [1, 0, 0]
    if front:
        A_IN, A_OUT, R0, R1, TG = (0.0135 + AC, 0.030 + AC), (-0.026 + AC, 0.0135 + AC), 0.095, 0.150, 0.058
    else:
        A_IN, A_OUT, R0, R1, TG = (0.0060 + AC, 0.012 + AC), (-0.024 + AC, 0.0060 + AC), 0.068, 0.122, 0.048
    for sgn in (-1, 1):
        a_c = sum(A_IN) / 2
        car.box((A_IN[1] - A_IN[0], 0.018, R1 - R0), polar(W, a_c, (R0 + R1) / 2, th, sgn * TG), "cast_iron", basis=B, bevel=0.003)
        car.box((A_OUT[1] - A_OUT[0], 0.018, 0.008), polar(W, sum(A_OUT) / 2, R1 + (0.003 if front else 0.002), th, sgn * TG),
                "cast_iron", basis=B, bevel=0.002)
    car.box((A_IN[1] - A_IN[0], 2 * TG + 0.018, 0.010), polar(W, sum(A_IN) / 2, R0 - 0.004, th), "cast_iron", basis=B, bevel=0.003)
    # carrier bolts (through knuckle lugs into the carrier), heads inboard
    a_head = A_IN[1] + 0.008
    for sgn in (-1, 1):
        car.fastener("twelve" if front else "bolt", polar(W, a_head, R0 + 0.010, th, sgn * TG), X_, "M12" if front else "M10",
                     real=("18 mm 12-point (approximate)" if front else "16 mm (approximate)"), label="carrier bolt", source=S_TT)

    # pads
    padr0, padr1, padtg = (R0 + 0.010, R1 - 0.008, TG - 0.013) if front else (R0 + 0.009, R1 - 0.010, TG - 0.015)
    face = half; ac = AC
    for nm, sgn in (("Inner", 1), ("Outer", -1)):
        p = Part(f"BrakePad{nm}_{sfx}", group, f"{nm} brake pad: steel backing plate and friction material.", "Held by the caliper.",
                 side=sfx, stage=2)
        p.origin = W; p.spin_axis = [1, 0, 0]
        fr0 = ac + sgn * face; fr1 = ac + sgn * (face + 0.008); bk1 = ac + sgn * (face + 0.012)
        p.box((0.008, 2 * padtg, padr1 - padr0), polar(W, (fr0 + fr1) / 2, (padr0 + padr1) / 2, th), "pad_friction", basis=B, bevel=0.002)
        p.box((0.004, 2 * padtg + 0.01, padr1 - padr0 + 0.004), polar(W, (fr1 + bk1) / 2, (padr0 + padr1) / 2, th), "zinc", basis=B, bevel=0.001)
        p.tool = "hands"
    # caliper (floating, single piston): inboard housing, bridge over the disc, outboard fingers
    cal = Part(f"BrakeCaliper_{sfx}", group, ("Lucas FS III single-piston floating caliper" if front else "Rear floating caliper with handbrake lever") + ", grey cast.",
               "2 guide bolts into the carrier; brake hose on the inboard side.", side=sfx, stage=1)
    cal.origin = W; cal.spin_axis = [1, 0, 0]
    back_in = ac + face + 0.012; back_out = ac - (face + 0.012)
    housing = (back_in, back_in + (0.028 if front else 0.031))
    calr0, calr1, caltg = (padr0 - 0.004, padr1 + 0.004, TG - 0.011) if front else (padr0 - 0.004, padr1 + 0.002, TG - 0.010)
    bridge_r = (RO + 0.005, RO + (0.020 if front else 0.014))
    cal.box((housing[1] - housing[0], 2 * caltg, calr1 - calr0), polar(W, sum(housing) / 2, (calr0 + calr1) / 2, th), "caliper_grey", basis=B, bevel=0.006)
    cal.cyl_between(polar(W, housing[0] + 0.004, (calr0 + calr1) / 2, th), polar(W, housing[1] + 0.008, (calr0 + calr1) / 2, th),
                    0.026 if front else 0.019, "caliper_grey", segs=24, bevel=0.003)
    cal.box((housing[1] - back_out, 2 * caltg, bridge_r[1] - bridge_r[0]), polar(W, (housing[1] + back_out) / 2, sum(bridge_r) / 2, th),
            "caliper_grey", basis=B, bevel=0.004)
    cal.box((0.009 if front else 0.007, 2 * caltg, bridge_r[1] - calr0 - 0.018), polar(W, back_out - (0.0045 if front else 0.0035), (bridge_r[1] + calr0 + 0.002) / 2, th),
            "caliper_grey", basis=B, bevel=0.003)
    ear_r = (calr0 + calr1) / 2 + 0.006
    for sgn in (-1, 1):
        cal.box((housing[1] - A_IN[1] - 0.002, 0.016, 0.022), polar(W, (A_IN[1] + 0.001 + housing[1]) / 2, ear_r, th, sgn * TG), "caliper_grey", basis=B, bevel=0.003)
        cal.fastener("socket" if front else "bolt", polar(W, housing[1] + 0.0, ear_r, th, sgn * TG), X_, "M8" if front else "M8",
                     real=("hex key 7 mm (guide bolt)" if front else "13 mm, hold guide with 15 mm (approximate)"), label="caliper guide bolt", source=S_TT)
    if not front:   # handbrake lever
        cal.box((0.008, 0.012, 0.05), polar(W, housing[1] + 0.004, calr0 + 0.016, th, -0.01), "caliper_grey", basis=B, bevel=0.002)
    banjo = polar(W, housing[1], calr1 - 0.006, th, 0.02)
    cal.fastener("banjo", banjo + V(0.004, 0, 0), X_, "M10", real="14 mm (hose union, approximate)", label="brake hose union", source=S_TT)
    return disc, car, cal, banjo

# ============================================================== FRONT CORNER (left)
def build_front_left():
    W = HP["F_WHEEL"]; sfx = "FL"
    tyre, rim = build_wheel(sfx, W, "Front")
    disc, car, cal, banjo = build_brakes(sfx, W, "Front", True, f"Knuckle_{sfx}")

    # ---- knuckle with bearing and hub flange
    kn = Part(f"Knuckle_{sfx}", "Front", "Cast-iron steering knuckle with pressed-in wheel bearing and hub flange.",
              "Ball-joint pinch bolt + nut; carrier bolts and strut clamp hold it.", side=sfx, stage=5)
    kn.origin = W; kn.spin_axis = [1, 0, 0]
    kn.revolve([(0.0280, -0.0450), (0.0283, -0.0330), (0.0650, -0.0330), (0.0650, -0.0250), (0.0330, -0.0250),
                (0.0330, -0.0080), (0.0280, -0.0080)], W, X_, "cast_iron", segs=24)                  # hub flange + pilot
    kn.revolve([(0.0300, -0.0080), (0.0530, -0.0080), (0.0560, 0.0000), (0.0560, 0.0240), (0.0520, 0.0300), (0.0300, 0.0300)],
               W, X_, "cast_iron", segs=24)                                                         # bearing housing
    for k in range(5):                                                                              # wheel-bolt bosses on the flange
        kn.cyl(0.008, 0.004, polar(W, -0.0300, 0.050, 90 + 72 * k), X_, "cast_iron", segs=12)
    # upper arm up to the strut clamp (clamp starts at the clamp point and rises)
    top_pad = F_CLAMP + V(-0.036, 0, 0)
    kn.beam(W + V(0.022, 0.022, 0.040), top_pad + V(-0.003, 0, 0.008), 0.040, 0.022, "cast_iron", up=X_, bevel=0.004)
    kn.box((0.010, 0.040, 0.075), top_pad + V(-0.003, 0, 0.036), "cast_iron", bevel=0.003)
    # lower arm down to the ball-joint boss
    BJ = F_BJ; boss_c = V(BJ.x, BJ.y, 0.214)
    kn.beam(W + V(0.022, 0.020, -0.040), boss_c + V(0, -0.010, 0.016), 0.034, 0.024, "cast_iron", up=X_, bevel=0.004)
    kn.cyl(0.019, 0.046, boss_c, Z_, "cast_iron", segs=20, bevel=0.003)
    kn.fastener("bolt", boss_c + V(0, 0.019, 0.010), Y_, "M10", real="17 mm (approximate)", label="ball-joint pinch bolt", source=S_BJ)
    kn.fastener("nut", boss_c + V(0, -0.019, 0.010), nY, "M10", real="17 mm (approximate)", label="ball-joint pinch nut", source=S_BJ)
    # steering arm out to the tie-rod end
    TRO = HP["F_TRO"]; sa_c = V(TRO.x, TRO.y, TRO.z + 0.033)
    kn.beam(W + V(0.024, -0.030, -0.006), sa_c + V(-0.004, 0.016, -0.004), 0.026, 0.022, "cast_iron", up=Z_, bevel=0.004)
    kn.cyl(0.016, 0.030, sa_c, Z_, "cast_iron", segs=18, bevel=0.002)
    # carrier lugs (carrier bolts pass through them)
    for sgn in (-1, 1):
        lug = polar(W, 0.0262, 0.105, 135, sgn * 0.058)
        kn.box((0.008, 0.020, 0.024), lug, "cast_iron", basis=rot_basis(135), bevel=0.002)
        kn.beam(polar(W, 0.0262, 0.050, 135, sgn * 0.030), lug, 0.016, 0.008, "cast_iron", up=X_, bevel=0.002)

    # ---- strut (axis from clamp to top mount)
    u = (F_TOP - F_CLAMP).normalized(); L = (F_TOP - F_CLAMP).length
    at = lambda s: F_CLAMP + u * s
    rear = (V(0, -1, 0) - u * u.dot(V(0, -1, 0))).normalized()
    inb = (V(1, 0, 0) - u * u.dot(V(1, 0, 0))).normalized()
    sl = Part(f"Strut_Lower_{sfx}", "Front", "Damper body with the knuckle clamp and lower spring seat (moves with the wheel).",
              "Clamp pinch bolt + nut.", side=sfx, stage=4)
    sl.origin = F_TOP; sl.orient_axis = u; sl.bottom_point = F_CLAMP
    sl.cyl_between(at(0.000), at(0.085), 0.034, "zinc", segs=28, bevel=0.003)               # clamp sleeve
    for side in (-1, 1):
        sl.box((0.022, 0.008, 0.070), at(0.042) + inb * 0.040 + rear * (side * 0.011), "zinc", basis=PL.basis_from(inb, u), bevel=0.002)
    sl.cyl_between(at(0.085), at(0.152), 0.0225, "zinc", segs=24, caps=False)                # damper tube
    sl.cyl_between(at(0.152), at(0.162), 0.0225, "zinc", segs=24, r2=0.0165)                 # step down past the tyre sidewall
    sl.cyl_between(at(0.162), at(0.300), 0.0165, "zinc", segs=20, caps=False)
    sl.revolve([(0.0110, 0.0), (0.0165, 0.0), (0.0165, -0.004), (0.0110, -0.004)], at(0.300), u, "zinc", segs=24)   # top cap with rod hole
    sl.revolve([(0.0165, -0.006), (0.086, -0.002), (0.086, 0.004), (0.0165, 0.0)], at(0.307), u, "painted_black", segs=24)   # lower spring seat
    sl.box((0.004, 0.030, 0.026), at(0.14) + rear * 0.030, "zinc", basis=PL.basis_from(rear, u), bevel=0.001)   # hose bracket
    sl.fastener("bolt", at(0.042) + inb * 0.040 + rear * 0.015, rear, "M12", real="18 mm (approximate)", label="strut clamp pinch bolt", source=S_TT)
    sl.fastener("nut", at(0.042) + inb * 0.040 - rear * 0.015, -rear, "M12", real="18 mm (approximate)", label="strut clamp pinch nut", source=S_TT)
    su = Part(f"Strut_Upper_{sfx}", "Front", "Top mount with bearing, upper spring seat and piston rod.",
              "3 top-mount nuts + centre nut.", side=sfx, stage=4)
    su.origin = F_TOP; su.orient_axis = u; su.bottom_point = F_CLAMP
    su.cyl_between(at(0.240), at(L + 0.020), 0.0105, "zinc", segs=16)                       # piston rod
    su.revolve([(0.011, 0.004), (0.086, 0.002), (0.086, -0.004), (0.011, -0.006)], at(0.440), u, "painted_black", segs=24)  # upper seat
    su.cyl_between(at(0.444), at(0.456), 0.048, "zinc", segs=24, bevel=0.002)               # bearing
    su.cyl_between(at(0.456), at(L - 0.004), 0.068, "rubber", segs=24, bevel=0.004)          # top-mount rubber
    su.cyl_between(at(L - 0.004), at(L + 0.000), 0.074, "painted_black", segs=24, bevel=0.001)  # top plate
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.5
        dirv = (inb * math.cos(a) + rear * math.sin(a)) * 0.052
        su.fastener("nut", at(L) + dirv, u, "M8", real="13 mm (approximate)", label="top-mount nut", source=APPROX)
    su.fastener("nut", at(L + 0.001), u, "M14", real="21 mm, hold rod with 7 mm hex key (approximate)", label="strut centre nut", source=S_TT)
    sp = Part(f"Spring_{sfx}", "Front", "Black painted coil spring between the strut seats.", "Held by the seats.", side=sfx, stage=4)
    sp.origin = at(0.436); sp.orient_axis = u; sp.collide = True; sp.single_hull = True; sp.tool = "hands (spring compressor)"
    sp.helix(at(0.307), u, 0.072, 0.0068, 0.129, 2.6, "painted_black")
    sp.bottom_point = at(0.307)

    # ---- lower arm
    FP, BJ = HP["F_ARM_PIV"], F_BJ; RP = V(-0.330, 0.920, 0.183)
    arm = Part(f"LowerArm_{sfx}", "Front", "Pressed-steel wishbone with front and rear bushings and bolted ball joint.",
               "Front bushing bolt + nut, rear bushing bolt, 3 ball-joint bolts.", side=sfx, stage=6)
    arm.origin = FP; arm.moving_end = BJ; arm.collide = True
    BJ_PLATE = V(BJ.x + 0.022, BJ.y - 0.012, 0.183)
    arm.beam(FP + V(-0.030, 0, 0), BJ_PLATE + V(0.010, 0, 0), 0.050, 0.035, "painted_black", bevel=0.005)
    KNEE = V(-0.450, 1.180, 0.183)
    arm.beam(RP + V(-0.026, 0.020, 0), KNEE, 0.046, 0.032, "painted_black", bevel=0.005)
    arm.beam(KNEE, BJ_PLATE + V(0.030, -0.020, 0), 0.046, 0.032, "painted_black", bevel=0.005)
    arm.cyl(0.030, 0.050, FP, Y_, "painted_black", segs=24, bevel=0.002)                    # front eye
    arm.cyl(0.026, 0.055, FP, Y_, "rubber", segs=24)                                          # front bushing (in subframe clevis)
    arm.cyl(0.036, 0.042, RP, Z_, "painted_black", segs=24, bevel=0.002)                    # rear eye
    arm.cyl(0.032, 0.046, RP, Z_, "rubber", segs=24)                                          # rear bushing
    arm.box((0.075, 0.060, 0.006), BJ_PLATE + V(0, 0, 0.0175), "painted_black", bevel=0.002)  # ball-joint plate
    arm.cyl(0.018, 0.024, V(BJ.x, BJ.y, 0.178), Z_, "zinc", segs=20, bevel=0.003)            # ball-joint housing
    arm.cyl(0.0095, 0.048, V(BJ.x, BJ.y, 0.212), Z_, "zinc", segs=12)                       # ball stud up into the knuckle
    for k, off in enumerate([V(0.025, 0.015, 0), V(0.025, -0.022, 0), V(-0.005, -0.030, 0)]):
        arm.fastener("bolt", BJ_PLATE + off + V(0, 0, -0.0175), nZ, "M8", real="13 mm", label="ball-joint bolt", source=S_BJ)
    arm.fastener("bolt", FP + V(0, 0.0395, 0), Y_, "M12", real="18 mm (approximate)", label="front bushing bolt", source=S_TT)
    arm.fastener("nut", FP + V(0, -0.0395, 0), nY, "M12", real="18 mm (approximate)", label="front bushing nut", source=S_TT)
    arm.fastener("bolt", RP + V(0, 0, -0.023), nZ, "M12", real="18 mm (approximate)", label="rear bushing bolt", source=S_TT)

    # ---- tie rod (inner) and tie-rod end
    RE = V(-HP["F_RACK_X"], TRO.y, TRO.z); TJ = V(-0.600, TRO.y, TRO.z)
    tr = Part(f"TieRod_{sfx}", "Front", "Inner tie rod with ball joint at the rack.", "Screws into the rack (32 mm flats).", side=sfx, stage=5)
    tr.origin = RE; tr.moving_end = TRO; tr.collide = True; tr.tool = "hands"; tr.real_tool = "32 mm open spanner on the inner joint"
    tr.sphere(0.016, RE, "zinc"); tr.cyl_between(RE + V(-0.010, 0, 0), TJ, 0.0085, "zinc", segs=12)
    te = Part(f"TieRodEnd_{sfx}", "Front", "Outer tie-rod end with ball stud and boot.", "Stud nut on top of the steering arm.", side=sfx, stage=2)
    te.origin = TJ; te.moving_end = TRO; te.collide = True
    te.cyl_between(TJ, TRO + V(0.020, 0, 0), 0.011, "zinc", segs=14)
    te.cyl(0.021, 0.026, TRO + V(0, 0, -0.004), Z_, "zinc", segs=18, bevel=0.003)
    te.cyl(0.016, 0.006, TRO + V(0, 0, 0.012), Z_, "rubber", segs=16)
    te.cyl(0.0075, 0.040, TRO + V(0, 0, 0.035), Z_, "zinc", segs=10)                        # stud through the steering arm
    te.fastener("nut", V(TRO.x, TRO.y, sa_c.z + 0.015), Z_, "M12", real="18 mm, hold stud with 7 mm hex key (approximate)",
                label="tie-rod end nut", source=S_TT)
    te.tool = ""

    # ---- anti-roll bar drop link (bar end above, lower arm below)
    LA = V(-0.400, 1.052, 0.199); LB = V(-0.400, 1.052, 0.290)
    lk = Part(f"AntiRollBarLink_{sfx}", "Front", "Drop link from the anti-roll bar to the lower arm, ball joints both ends.",
              "2 nuts.", side=sfx, stage=3)
    lk.origin = LB + V(0, 0, -0.0205); lk.moving_end = LA + V(0, 0, 0.011); lk.collide = True
    lk.cyl(0.015, 0.020, LB + V(0, 0, -0.0205), Z_, "zinc", segs=16, bevel=0.002)
    lk.cyl_between(LA + V(0, 0, 0.022), LB + V(0, 0, -0.030), 0.0065, "painted_black", segs=10)
    lk.cyl(0.015, 0.022, LA + V(0, 0, 0.0125), Z_, "zinc", segs=16, bevel=0.002)
    lk.cyl(0.006, 0.030, LB + V(0, 0, 0.004), Z_, "zinc", segs=10)
    lk.cyl(0.006, 0.040, LA + V(0, 0, -0.016), Z_, "zinc", segs=10)
    lk.fastener("nut", LB + V(0, 0, 0.009), Z_, "M10", real="16 mm (approximate)", label="drop-link upper nut", source=S_TT)
    lk.fastener("nut", LA + V(0, 0, -0.035), nZ, "M10", real="16 mm (approximate)", label="drop-link lower nut", source=S_TT)
    # drop-link pad on the arm's rear member
    arm.box((0.040, 0.040, 0.006), LA + V(0, 0, -0.002), "painted_black", bevel=0.002)

    # ---- brake hose (strut bracket -> caliper)
    hb = at(0.14) + rear * 0.036
    hose = Part(f"BrakeHose_{sfx}", "Front", "Flexible brake hose from the strut bracket to the caliper.", "Spring clip at the bracket, union at the caliper.",
                side=sfx, stage=1)
    hose.origin = hb; hose.moving_end = banjo
    nb = (banjo - W - V(banjo.x - W.x, 0, 0)).normalized()
    hose.tube([hb, hb + V(0.010, -0.010, -0.040), W + V(0.095, 0, 0) + nb * 0.150, W + V(0.050, 0, 0) + nb * 0.152,
               banjo + nb * 0.014 + V(0.010, 0, 0), banjo + nb * 0.004 + V(0.010, 0, 0)], 0.0055, "rubber", segs=8, per=8)
    hose.cyl(0.008, 0.016, hb, V(0, 0, 1), "zinc", segs=12)
    hose.fastener("clip", hb + rear * 0.006 + V(0, 0, 0.012), rear, real="hands", label="hose clip", source=APPROX)
    return dict(tyre=tyre, rim=rim, disc=disc, car=car, cal=cal, kn=kn, sl=sl, su=su, sp=sp, arm=arm, tr=tr, te=te, lk=lk, hose=hose,
                RP=RP, LB=LB)

# ============================================================== DRIVESHAFT (per side, not mirrored)
def build_driveshaft(sfx, IN, OUT, W, group="Front", bolt_angles=None):
    """bolt_angles: inner CV bolt positions in degrees measured from straight up towards the car's front
    (round the shaft axis). None keeps the front pattern."""
    d = (OUT - IN).normalized(); rear = group == "Rear"
    ds = Part(f"Driveshaft_{sfx}", group, ("Rear" if rear else ("Long" if sfx == "FR" else "Short")) + " driveshaft (plain shaft between the boots).",
              "None; comes out with the joints.", side=sfx, stage=3)
    ds.origin = IN; ds.moving_end = OUT; ds.collide = True
    ds.cyl_between(IN + d * 0.085, OUT - d * 0.090, 0.0125, "painted_black", segs=16)
    ci = Part(f"CVJoint_Inner_{sfx}", group, "Inner constant-velocity joint (100 mm flange to the " + ("rear final drive" if rear else "gearbox") + ").",
              "6 triple-square bolts into the " + ("final-drive" if rear else "gearbox") + " flange.", side=sfx, stage=3)
    ci.origin = IN; ci.moving_end = OUT
    ci.revolve([(0.020, -0.0225), (0.056, -0.0225), (0.056, -0.008), (0.046, -0.002), (0.046, 0.0225), (0.020, 0.0225)], IN, d, "zinc", segs=24)
    if bolt_angles is None:
        for k in range(6):
            a = 2 * math.pi * k / 6 + 0.26
            Mb = PL.align_z(d).to_3x3()
            ci.fastener("socket", IN + d * 0.0225 + Mb @ V(0.050 * math.cos(a), 0.050 * math.sin(a), 0), d, "M8",
                        real="triple-square M8 (XZN)", label="inner CV bolt", source="Golf IV 02J/02M 100 mm CV flange, 6 bolts")
    else:
        e_up = (Z_ - d * d.dot(Z_)).normalized(); e_f = (Y_ - d * d.dot(Y_) - e_up * e_up.dot(Y_)).normalized()
        for th in bolt_angles:
            a = math.radians(th)
            ci.fastener("socket", IN + d * 0.0225 + (e_up * math.cos(a) + e_f * math.sin(a)) * 0.050, d, "M8",
                        real="triple-square M8 (XZN)", label="inner CV bolt", source="Golf IV 4motion rear final drive, 100 mm CV flange, 6 bolts")
    bi = Part(f"CVBoot_Inner_{sfx}", group, "Inner CV boot.", "Clamps only.", side=sfx, stage=3)
    bi.origin = IN; bi.moving_end = OUT
    prof = [(0.0125, 0.085)]
    for k in range(6):
        prof.append((0.040 - 0.004 * k, 0.0225 + 0.010 * k + 0.002)); prof.append((0.030 - 0.003 * k, 0.0225 + 0.010 * k + 0.007))
    prof = [(0.040, 0.0225)] + prof[1:] + [(0.0130, 0.085), (0.0125, 0.085)]
    prof = sorted(prof, key=lambda p: p[1])
    bi.revolve([(0.0126, 0.0225)] + prof + [(0.0126, 0.0855)], IN, d, "rubber", segs=16, closed=True)
    co = Part(f"CVJoint_Outer_{sfx}", group, "Outer CV joint, inside the " + ("hub carrier's bearing bore" if rear else "hub") + "; its stub carries the axle bolt.",
              "Axle bolt (12-point).", side=sfx, stage=3)
    co.origin = W; co.spin_axis = [1, 0, 0]; co.moving_end = OUT
    co.revolve([(0.010, -0.052), (0.025, -0.045), (0.025, -0.020), (0.0265, -0.006), (0.0265, 0.034), (0.020, 0.040), (0.010, 0.040)],
               OUT, -d, "zinc", segs=28)
    outward = V(-1, 0, 0) if W.x < 0 else V(1, 0, 0)
    stub0, stub1 = OUT + d * 0.050, W + outward * 0.044
    if (stub1 - stub0).dot(outward) > 0.01:      # rear: splined stub from the joint out through the hub bearing
        co.cyl_between(stub0, stub1, 0.0140, "zinc", segs=16)
    co.fastener("twelve", W + V(-0.045 if W.x < 0 else 0.045, 0, 0), nX if W.x < 0 else X_, "M16", real="12-point M16 axle bolt (approximate)", label="driveshaft hub bolt", source=S_TT)
    bo = Part(f"CVBoot_Outer_{sfx}", group, "Outer CV boot.", "Clamps only.", side=sfx, stage=3)
    bo.origin = W; bo.moving_end = OUT
    prof = []
    for k in range(5):
        prof.append((0.0265 + 0.0015 * (4 - k) - 0.0015, 0.040 + 0.010 * k + 0.002)); prof.append((0.021 - 0.0015 * k, 0.040 + 0.010 * k + 0.007))
    prof = [(0.0266, 0.040)] + prof + [(0.0127, 0.092), (0.0126, 0.092)]
    bo.revolve([(0.0126, 0.040)] + prof, OUT, -d, "rubber", segs=16, closed=True)
    return [ds, ci, bi, co, bo]

# ============================================================== REAR CORNER (left)
def build_rear_left():
    W = HP["R_WHEEL"]; sfx = "RL"
    tyre, rim = build_wheel(sfx, W, "Rear")
    disc, car, cal, banjo = build_brakes(sfx, W, "Rear", False, f"HubCarrier_{sfx}")
    TA_C, UL_O, LL_O, DB, DT = HP["R_TA_CARRIER"], HP["R_UL_OUT"], HP["R_LL_OUT"], R_DAMP_BOT, HP["R_DAMP_TOP"]
    ARB_LO = V(-0.585, R_ARB_Y, 0.205)

    hc = Part(f"HubCarrier_{sfx}", "Rear", "Rear hub carrier (Octavia 4x4 style) with bolted hub bearing unit.",
              "4 hub-bearing bolts; links, trailing arm and damper bolt to it.", side=sfx, stage=6)
    hc.origin = W; hc.spin_axis = [1, 0, 0]
    hc.revolve([(0.0280, -0.0450), (0.0283, -0.0330), (0.0600, -0.0330), (0.0600, -0.0250), (0.0360, -0.0250),
                (0.0360, -0.0050), (0.0280, -0.0050)], W, X_, "cast_iron", segs=24)                  # hub flange + bearing
    hc.box((0.005, 0.084, 0.084), W + V(-0.0025, 0, 0), "cast_iron", bevel=0.004)                  # bearing flange
    hc.revolve([(0.028, 0.0), (0.050, 0.0), (0.050, 0.040), (0.044, 0.046), (0.028, 0.046)], W, X_, "cast_iron", segs=24)  # carrier body, 56 mm bore
    hc.revolve([(0.028, 0.046), (0.036, 0.046), (0.036, 0.068), (0.028, 0.068)], W, X_, "cast_iron", segs=24)   # bore collar round the outer CV
    for k in range(5): hc.cyl(0.008, 0.004, polar(W, -0.0300, 0.050, 90 + 72 * k), X_, "cast_iron", segs=12)
    body = W + V(0.023, 0, 0)
    # arms to the joints
    boss_ta = TA_C + V(-0.007, 0, 0)
    hc.beam(body + V(-0.008, -0.012, 0.040), V(boss_ta.x, -1.318, 0.345), 0.026, 0.020, "cast_iron", up=X_, bevel=0.004)
    hc.box((0.030, 0.044, 0.042), V(boss_ta.x, boss_ta.y, 0.362), "cast_iron", bevel=0.004)
    for jp, nm in ((UL_O, "upper"), (LL_O, "lower"), (DB, "damper")):
        hc.beam(body + (jp - body).normalized() * 0.040, jp + (body - jp).normalized() * 0.026, 0.030, 0.024, "cast_iron", up=X_, bevel=0.004)
        for sgn in (-1, 1):
            hc.box((0.030, 0.006, 0.044), jp + V(0.005, sgn * 0.0265, 0), "cast_iron", bevel=0.002)    # fork plates either side of the bushing
    hc.beam(DB + V(0, -0.02, 0.01), ARB_LO + V(0, 0.012, 0.004), 0.020, 0.012, "cast_iron", up=X_, bevel=0.003)
    hc.cyl(0.014, 0.012, ARB_LO + V(0, 0, 0.004), Z_, "cast_iron", segs=16)                       # ARB link tab
    for sgn in (-1, 1):                                                                             # caliper-carrier lugs
        lug = polar(W, 0.0095, 0.076, R_CAL_TH, sgn * 0.048)
        hc.box((0.010, 0.020, 0.022), lug, "cast_iron", basis=rot_basis(R_CAL_TH), bevel=0.002)
    for yy in (-0.033, 0.033):
        for zz in (-0.033, 0.033):
            if zz > 0 and yy < 0: continue      # that corner sits under the trailing-arm boss; bolt it from the flange side
            hc.fastener("twelve", W + V(0.046, yy, zz), X_, "M10", real="12-point M10 (approximate)", label="hub bearing bolt", source=APPROX)
    hc.fastener("twelve", W + V(-0.005, -0.033, 0.033), nX, "M10", real="12-point M10 (approximate)", label="hub bearing bolt", source=APPROX)

    # trailing arm
    PIV = HP["R_TA_PIV"]; BEND = V(-0.578, -1.150, 0.333); TA_END = V(TA_C.x + 0.014, -1.300, 0.333)
    ta = Part(f"TrailingArm_{sfx}", "Rear", "Pressed-steel trailing arm with front bushing and body bracket.",
              "3 bracket bolts to the body, pivot bolt + nut, 2 bolts to the hub carrier.", side=sfx, stage=5)
    ta.origin = PIV; ta.moving_end = TA_C; ta.collide = True
    ta.beam(PIV + V(0, -0.022, 0), BEND, 0.014, 0.044, "painted_black", up=Z_, bevel=0.004)
    ta.beam(BEND, TA_END, 0.014, 0.028, "painted_black", up=Z_, bevel=0.004)
    ta.box((0.006, 0.040, 0.070), V(TA_C.x + 0.011, TA_C.y + 0.003, 0.355), "painted_black", bevel=0.002)   # end plate on the boss inboard face
    ta.cyl(0.030, 0.046, PIV, X_, "painted_black", segs=24, bevel=0.002)
    ta.cyl(0.026, 0.050, PIV, X_, "rubber", segs=24)
    for sgn in (-1, 1): ta.box((0.006, 0.060, 0.080), PIV + V(sgn * 0.031, 0, 0.018), "painted_black", bevel=0.002)
    ta.box((0.090, 0.130, 0.008), PIV + V(0, 0.040, 0.060), "painted_black", bevel=0.002)
    for p in (V(-0.030, 0.085, 0), V(0.030, 0.085, 0), V(0, 0.095, 0)):
        ta.fastener("bolt", PIV + p + V(0, 0, 0.056), nZ, "M10", real="16 mm (approximate)", label="trailing-arm bracket bolt", source=APPROX)
    ta.fastener("bolt", PIV + V(-0.040, 0, 0), nX, "M12", real="18 mm (approximate)", label="trailing-arm pivot bolt", source=APPROX)
    ta.fastener("nut", PIV + V(0.040, 0, 0), X_, "M12", real="18 mm (approximate)", label="trailing-arm pivot nut", source=APPROX)
    for dz in (-0.004, 0.024):
        ta.fastener("bolt", V(TA_C.x + 0.014, TA_C.y - 0.006, 0.350 + dz), X_, "M10", real="16 mm (approximate)", label="trailing arm to hub carrier", source=S_TT)

    # links
    def link(nm, A, Bp, w, h, extra=None):
        p = Part(f"{nm}_{sfx}", "Rear", f"{'Lower' if 'Lower' in nm else 'Upper'} transverse link with bushings both ends.",
                 "Bolt + nut at each end.", side=sfx, stage=4)
        p.origin = A; p.moving_end = Bp; p.collide = True
        p.beam(A + (Bp - A).normalized() * 0.022, Bp - (Bp - A).normalized() * 0.022, w, h, "painted_black", bevel=0.004)
        for q in (A, Bp):
            er = 0.014 if (q is Bp and "Upper" in nm) else 0.022
            p.cyl(er, 0.044, q, Y_, "painted_black", segs=20, bevel=0.002)
            p.cyl(er - 0.004, 0.046, q, Y_, "rubber", segs=20)
        for q, lab, sg in ((A, "body", -1), (Bp, "hub carrier", 1)):
            p.fastener("bolt", q + V(0, sg * 0.0295, 0), V(0, sg, 0), "M12", real="18 mm (approximate)", label=f"{lab}-end bolt", source=S_TT if lab != "body" else APPROX)
            if lab == "body" and "Lower" in nm:
                p.fastener("nut", q + V(0, -sg * 0.0295, 0), V(0, -sg, 0), "M12", real="18 mm (approximate)", label=f"{lab}-end nut", source=APPROX)
        if extra: extra(p)
        return p
    SPR_C = V(-0.470, -1.150, 0.0)
    def pan(p):
        zl = HP["R_LL_IN"].z + (HP["R_LL_OUT"].z - HP["R_LL_IN"].z) * ((SPR_C.x - HP["R_LL_IN"].x) / (HP["R_LL_OUT"].x - HP["R_LL_IN"].x))
        p.revolve([(0.0, zl + 0.012), (0.068, zl + 0.012), (0.072, zl + 0.022), (0.076, zl + 0.022), (0.072, zl + 0.008), (0.0, zl + 0.008)],
                  V(SPR_C.x, SPR_C.y, 0), Z_, "painted_black", segs=24)
        p.box((0.060, 0.050, 0.014), V(SPR_C.x, -1.172, zl + 0.004), "painted_black", bevel=0.003)
        p.props["spring_seat_z"] = zl + 0.012
    ll = link("LowerLink", HP["R_LL_IN"], LL_O, 0.034, 0.034, pan)
    ul = link("UpperLink", HP["R_UL_IN"], UL_O, 0.026, 0.026)

    # spring (sits on the lower-link pan) and damper (separate, like the real 4x4 rear)
    zl = ll.props["spring_seat_z"]
    sp = Part(f"Spring_{sfx}", "Rear", "Rear coil spring on the lower-link pan, rubber pads both ends.", "Held by the seats.", side=sfx, stage=3)
    top = V(SPR_C.x, SPR_C.y, 0.520); bot = V(SPR_C.x, SPR_C.y, zl)
    sp.origin = top; sp.orient_axis = Z_; sp.bottom_point = bot; sp.collide = True; sp.single_hull = True; sp.tool = "hands (spring compressor)"
    sp.helix(bot + V(0, 0, 0.004), Z_, 0.054, 0.0068, top.z - bot.z - 0.012, 4.2, "painted_black")
    sp.cyl(0.060, 0.008, top + V(0, 0, -0.004), Z_, "rubber", segs=28)
    sp.cyl(0.060, 0.004, bot + V(0, 0, 0.002), Z_, "rubber", segs=28)
    u = (DT - DB).normalized(); L = (DT - DB).length; at = lambda s: DB + u * s
    dp = Part(f"Damper_{sfx}", "Rear", "Rear damper (separate from the spring) with top mount and bottom eye.",
              "2 top-mount nuts to the body, bottom bolt + nut.", side=sfx, stage=2)
    dp.origin = DT; dp.orient_axis = u; dp.bottom_point = DB
    dp.cyl(0.019, 0.044, DB, Y_, "zinc", segs=20, bevel=0.002)
    dp.cyl(0.015, 0.046, DB, Y_, "rubber", segs=20)
    dp.cyl_between(at(0.016), at(0.300), 0.024, "zinc", segs=24, bevel=0.003)
    dp.cyl_between(at(0.296), at(L - 0.012), 0.0085, "zinc", segs=14)
    dp.cyl_between(at(L - 0.040), at(L - 0.012), 0.036, "rubber", segs=24, bevel=0.004)
    dp.cyl_between(at(L - 0.012), at(L - 0.008), 0.046, "painted_black", segs=28)
    inb = (X_ - u * u.dot(X_)).normalized()
    for sgn in (-1, 1):
        dp.fastener("nut", at(L - 0.008) + inb * (sgn * 0.034), u, "M8", real="13 mm (approximate)", label="damper top nut", source=S_TT)
    dp.fastener("bolt", DB + V(0, -0.0295, 0), nY, "M12", real="18 mm (approximate)", label="damper bottom bolt", source=S_TT)
    dp.fastener("nut", DB + V(0, 0.0295, 0), Y_, "M12", real="18 mm (approximate)", label="damper bottom nut", source=S_TT)

    # anti-roll bar link
    LB = V(-0.505, R_ARB_Y, 0.330)
    lk = Part(f"AntiRollBarLink_{sfx}", "Rear", "Rear anti-roll bar link to the hub carrier.", "2 nuts.", side=sfx, stage=2)
    lk.origin = LB + V(0, 0, -0.020); lk.moving_end = ARB_LO + V(0, 0, 0.026); lk.collide = True
    lk.cyl(0.013, 0.020, LB + V(0, 0, -0.020), Z_, "zinc", segs=14, bevel=0.002)
    lk.cyl_between(LB + V(0, 0, -0.030), ARB_LO + V(0, 0, 0.034), 0.006, "painted_black", segs=10)
    lk.cyl(0.013, 0.020, ARB_LO + V(0, 0, 0.020), Z_, "zinc", segs=14, bevel=0.002)
    lk.cyl(0.0055, 0.028, LB + V(0, 0, 0.004), Z_, "zinc", segs=10)
    lk.cyl(0.0055, 0.034, ARB_LO + V(0, 0, -0.006), Z_, "zinc", segs=10)
    lk.fastener("nut", LB + V(0, 0, 0.009), Z_, "M8", real="13 mm (approximate)", label="link upper nut", source=APPROX)
    lk.fastener("nut", ARB_LO + V(0, 0, -0.002), nZ, "M8", real="13 mm (approximate)", label="link lower nut", source=S_TT)

    # brake hose (body bracket -> caliper)
    hb = V(-0.560, -1.420, 0.420)
    hose = Part(f"BrakeHose_{sfx}", "Rear", "Flexible brake hose from the body bracket to the caliper.", "Spring clip at the bracket, union at the caliper.",
                side=sfx, stage=1)
    hose.origin = hb; hose.moving_end = banjo
    nb = (banjo - W - V(banjo.x - W.x, 0, 0)).normalized()
    hose.tube([hb, hb + V(-0.02, 0.0, -0.05), W + V(0.090, 0, 0) + nb * 0.125, W + V(0.045, 0, 0) + nb * 0.125,
               banjo + nb * 0.014 + V(0.010, 0, 0), banjo + nb * 0.004 + V(0.010, 0, 0)], 0.0055, "rubber", segs=8, per=8)
    hose.cyl(0.008, 0.016, hb, Z_, "zinc", segs=12)
    hose.fastener("clip", hb + V(0, 0, 0.012), Z_, real="hands", label="hose clip", source=APPROX)
    return dict(tyre=tyre, rim=rim, disc=disc, car=car, cal=cal, hc=hc, ta=ta, ll=ll, ul=ul, sp=sp, dp=dp, lk=lk, hose=hose, LB=LB, SPR_C=SPR_C)

# ============================================================== SHARED
def build_shared(F, R):
    TRO = HP["F_TRO"]; FP = HP["F_ARM_PIV"]; RP = F["RP"]
    sf = Part("FrontSubframe", "Shared", "Front subframe: side rails, cross members, wishbone clevises, rack and anti-roll-bar pads.",
              "4 subframe bolts to the body; arm bushing bolts pass through it.", stage=8)
    sf.collide = True
    for sx in (-1, 1):
        sf.box((0.030, 0.360, 0.050), V(sx * 0.270, 1.085, 0.200), "painted_black", bevel=0.006)
        sf.box((0.062, 0.080, 0.012), V(sx * 0.316, FP.y, 0.2075), "painted_black", bevel=0.003)
        for dy in (-0.0355, 0.0355):                                                                # front-bushing clevis plates
            sf.box((0.090, 0.008, 0.075), V(sx * 0.390, FP.y + dy, 0.1875), "painted_black", bevel=0.002)
        sf.box((0.090, 0.075, 0.008), V(sx * 0.315, RP.y, 0.2125), "painted_black", bevel=0.002)     # rear-bushing top plate
    sf.box((0.512, 0.043, 0.050), V(0, 1.0335, 0.200), "painted_black", bevel=0.006)               # cross member 1 (ARB)
    sf.box((0.512, 0.043, 0.046), V(0, 1.1365, 0.203), "painted_black", bevel=0.006)               # cross member 2 (rack)
    for sx in (-1, 1):
        for yy in (0.915, 1.255):
            sf.fastener("bolt", V(sx * 0.270, yy, 0.175), nZ, "M14", real="21 mm (approximate)", label="subframe bolt", source=APPROX)
    rk = Part("SteeringRack", "Shared", "Power steering rack with boots (left-hand drive pinion).", "2 clamp bolts to the subframe.", stage=7)
    yR, zR = TRO.y, TRO.z
    rk.cyl_between(V(-0.300, yR, zR), V(0.300, yR, zR), 0.030, "zinc", segs=24, bevel=0.003)
    rk.cyl_between(V(-0.235, yR, zR), V(-0.235, yR + 0.02, zR + 0.090), 0.034, "zinc", segs=20, bevel=0.003)
    for sx in (-1, 1):
        rk.cyl_between(V(sx * 0.300, yR, zR), V(sx * 0.334, yR, zR), 0.012, "zinc", segs=12)
        prof = [(0.0300, 0.0)] + sum([[(0.029 - 0.0022 * k, 0.008 + 0.014 * k), (0.024 - 0.0018 * k, 0.015 + 0.014 * k)] for k in range(6)], []) + [(0.0098, 0.100)]
        rk.revolve([(0.0094, 0.0)] + prof + [(0.0094, 0.100)], V(sx * 0.300, yR, zR), V(sx, 0, 0), "rubber", segs=14, closed=True)
        c = V(sx * 0.180, yR, zR)
        rk.box((0.040, 0.070, 0.008), c + V(0, 0, 0.034), "painted_black", bevel=0.002)
        for dy in (-0.034, 0.034): rk.box((0.040, 0.006, 0.065), c + V(0, dy, 0.0005), "painted_black", bevel=0.002)
        rk.box((0.040, 0.090, 0.006), c + V(0, -0.0, -0.030), "painted_black", bevel=0.002)
        rk.fastener("bolt", c + V(0, -0.040, -0.027), Z_, "M10", real="16 mm (approximate)", label="rack clamp bolt", source=S_TT)
    fb = Part("FrontAntiRollBar", "Shared", "Front anti-roll bar with rubber bushings and clamps.", "2 clamps, 2 bolts each.", stage=7)
    fb.collide = True; fb.hull_chunk = 0.12
    LB = F["LB"]
    path = [V(-LB.x, LB.y, LB.z), V(0.34, 1.030, 0.290), V(0.22, 1.034, 0.290), V(-0.22, 1.034, 0.290), V(-0.34, 1.030, 0.290), LB]
    fb.tube([V(-LB.x, LB.y, LB.z)] + path[1:-1] + [LB], 0.011, "painted_black", segs=8, per=6)
    for sx in (-1, 1):
        fb.cyl(0.016, 0.018, V(sx * abs(LB.x), LB.y, LB.z), Z_, "painted_black", segs=16)
        c = V(sx * 0.200, 1.034, 0.290)
        fb.cyl(0.021, 0.040, c, X_, "rubber", segs=20)
        fb.box((0.040, 0.050, 0.044), V(c.x, c.y, 0.247), "painted_black", bevel=0.003)
        fb.box((0.040, 0.046, 0.006), c + V(0, 0, 0.024), "painted_black", bevel=0.002)
        for dy in (-0.026, 0.026):
            fb.box((0.040, 0.006, 0.050), c + V(0, dy, 0.0), "painted_black", bevel=0.002)
            fb.fastener("bolt", V(c.x, c.y + dy * 1.55, 0.231), Z_, "M8", real="13 mm (approximate)", label="anti-roll bar clamp bolt", source=S_TT)
            fb.box((0.040, 0.014, 0.006), V(c.x, c.y + dy * 1.55, 0.228), "painted_black", bevel=0.001)

    # ---- rear subframe: pressed-steel U-section cross-member, ends bent up into side members with 4 bushed body mounts
    rs = Part("RearSubframe", "Shared", "Pressed-steel rear cross-member: 70 mm U section, ends bent up into side members with "
              "4 rubber-bushed body mounts; welded link clevises, anti-roll-bar hangers and final-drive brackets.",
              "4 subframe bolts to the body; the final drive hangs from it on 3 bolts.", stage=8)
    rs.collide = True
    YC, HW, D, T = -1.205, 0.055, 0.070, 0.004            # centre line, half width, depth, sheet
    def weld(pts=None, path=None, closed=False):           # weld beads: visual only, no collision hulls
        rs.collide = False
        if path is not None: rs.tube(None, 0.0022, "painted_black", segs=4, closed=closed, path=path)
        else: rs.tube(pts, 0.0022, "painted_black", segs=4, per=2)
        rs.collide = True
    def u_section(p0, p1):
        d = (p1 - p0).normalized(); up = V(-d.z, 0, d.x) if d.x > 0 else V(d.z, 0, -d.x)
        rs.hulls.append([q + V(0, sg * (HW + 0.015), 0) + up * h for q in (p0, p1) for sg in (-1, 1) for h in (-T / 2, D)])
        rs.collide = False
        rs.beam(p0, p1, 2 * HW, T, "painted_black", up=up, bevel=0, ext=0.004)                    # web (bottom)
        for sg in (-1, 1):
            q0, q1 = p0 + V(0, sg * (HW - T / 2), 0) + up * (D / 2), p1 + V(0, sg * (HW - T / 2), 0) + up * (D / 2)
            rs.beam(q0, q1, T, D, "painted_black", up=up, bevel=0, ext=0.004)                        # flanks
            r0, r1 = p0 + V(0, sg * (HW + 0.007), 0) + up * D, p1 + V(0, sg * (HW + 0.007), 0) + up * D
            rs.beam(r0, r1, 0.016, T, "painted_black", up=up, bevel=0, ext=0.004)                    # top lips
        rs.collide = True
    pts = [V(-0.300, YC, 0.415), V(-0.160, YC, 0.385), V(0.160, YC, 0.385), V(0.300, YC, 0.415)]
    for p0, p1 in zip(pts[:-1], pts[1:]): u_section(p0, p1)
    for sx in (-1, 1):
        # side member (box section) between the two body mounts
        rs.box((0.060, 0.096, 0.055), V(sx * 0.330, YC, 0.4425), "painted_black", bevel=0.004)
        for yy in (-1.130, -1.280):
            c = V(sx * 0.330, yy, 0.450)
            rs.revolve([(0.0240, -0.020), (0.0330, -0.020), (0.0330, 0.020), (0.0240, 0.020)], c, Z_, "painted_black", segs=16)  # housing
            rs.revolve([(0.0105, -0.019), (0.0240, -0.017), (0.0240, 0.019), (0.0105, 0.019)], c, Z_, "rubber", segs=16)         # bush
            rs.revolve([(0.0070, -0.020), (0.0105, -0.020), (0.0105, 0.020), (0.0070, 0.020)], c, Z_, "zinc", segs=10)           # sleeve
            weld(path=[c + V(0.0345 * math.cos(2 * math.pi * k / 12), 0.0345 * math.sin(2 * math.pi * k / 12), 0.008) for k in range(12)], closed=True)
        rs.box((0.050, 0.004, 0.036), V(sx * 0.275, YC + 0.059, 0.441), "painted_black")                                       # gusset, front flank
        for sg in (-1, 1):                                                                                                      # seams side member / cross-member
            weld([V(sx * 0.299, YC + sg * 0.052, 0.418), V(sx * 0.299, YC + sg * 0.052, 0.468)])
    # link clevises (where they were)
    LLI, ULI = HP["R_LL_IN"], HP["R_UL_IN"]
    for sx in (-1, 1):
        for yy in (LLI.y - 0.0255, LLI.y + 0.0255):
            rs.box((0.060, 0.006, 0.430 - 0.250), V(sx * abs(LLI.x), yy, (0.430 + 0.250) / 2), "painted_black", bevel=0.002)
            weld([V(sx * abs(LLI.x) - 0.028, yy, 0.4152), V(sx * abs(LLI.x) + 0.028, yy, 0.4152)])
        for yy in (ULI.y - 0.0255, ULI.y + 0.0255):
            rs.box((0.060, 0.006, 0.430 - 0.334), V(sx * abs(ULI.x), yy, (0.430 + 0.334) / 2), "painted_black", bevel=0.002)
        # anti-roll-bar hangers (where they were), welded to the cross-member's rear flank
        rs.box((0.030, 0.180, 0.020), V(sx * 0.250, -1.330, 0.440), "painted_black", bevel=0.003)
        rs.box((0.040, 0.060, 0.072), V(sx * 0.250, -1.400, 0.394), "painted_black", bevel=0.003)
        for yy in (-1.130, -1.280):
            rs.fastener("bolt", V(sx * 0.330, yy, 0.430), nZ, "M12", real="18 mm (approximate)", label="rear subframe bolt", source=APPROX)
    # final-drive bracket: tongue off the rear flank for the rear mount
    rs.box((0.050, 0.140, 0.010), V(0.0, -1.330, 0.391), "painted_black", bevel=0.003)
    weld([V(-0.025, -1.2615, 0.396), V(0.025, -1.2615, 0.396)])

    # ---- rear final drive (Haldex coupling + differential, one casting)
    fd = Part("RearFinalDrive", "Shared", "Rear final drive: Haldex coupling and rear differential in one cast housing; output flanges "
              "to the rear driveshafts, propshaft flange facing forward.", "3 bolts to the rear subframe; drain and filler plugs.", stage=8)
    fd.collide = True
    FC = V(0, -1.275, 0.295)
    fd.revolve([(0.040, -0.168), (0.047, -0.163), (0.047, -0.118), (0.062, -0.102), (0.082, -0.072), (0.084, -0.028),
                (0.083, 0.022), (0.077, 0.058), (0.060, 0.092), (0.047, 0.112), (0.047, 0.163), (0.040, 0.168)],
               FC, X_, "cast_iron", segs=24)                                                          # diff case, ring-gear side bulge
    for sx in (-1, 1):
        fd.revolve([(0.020, 0.163), (0.052, 0.163), (0.052, 0.1805), (0.020, 0.1805)], FC, V(sx, 0, 0), "cast_iron", segs=24)   # output flange
        for k in range(6):
            fd.cyl(0.006, 0.006, FC + V(sx * 0.166, 0.050 * math.cos(math.pi * k / 3 + 0.3), 0.050 * math.sin(math.pi * k / 3 + 0.3)) * 1,
                   X_, "cast_iron", segs=8)
    fd.cyl_between(V(0, -1.205, 0.295), V(0, -1.088, 0.295), 0.070, "caliper_grey", segs=24, bevel=0.004)  # Haldex coupling
    for yy in (-1.180, -1.125):
        fd.cyl(0.074, 0.008, V(0, yy, 0.295), Y_, "caliper_grey", segs=24)                            # housing ribs
    fd.box((0.034, 0.070, 0.060), V(-0.084, -1.140, 0.300), "caliper_grey", bevel=0.005)               # pump / control unit
    fd.cyl_between(V(0, -1.088, 0.295), V(0, -1.070, 0.295), 0.030, "cast_iron", segs=16)              # nose
    fd.revolve([(0.015, 0.0), (0.050, 0.0), (0.050, 0.010), (0.015, 0.010)], V(0, -1.070, 0.295), Y_, "zinc", segs=24)  # propshaft flange (+Y)
    # mounting ears: two at the front under the cross-member, one at the rear under the tongue
    for sx in (-1, 1):
        fd.beam(V(sx * 0.080, -1.240, 0.352), V(sx * 0.130, -1.205, 0.376), 0.022, 0.016, "cast_iron", up=Z_, bevel=0.004)
        fd.box((0.040, 0.040, 0.012), V(sx * 0.130, -1.205, 0.376), "cast_iron", bevel=0.003)
        fd.fastener("bolt", V(sx * 0.130, -1.205, 0.370), nZ, "M12", real="19 mm (approximate)", label="final-drive mount bolt", source=APPROX)
    fd.beam(V(0, -1.320, 0.352), V(0, -1.372, 0.373), 0.026, 0.016, "cast_iron", up=Z_, bevel=0.004)
    fd.box((0.040, 0.040, 0.012), V(0, -1.375, 0.379), "cast_iron", bevel=0.003)
    fd.fastener("bolt", V(0, -1.375, 0.373), nZ, "M12", real="19 mm (approximate)", label="final-drive mount bolt", source=APPROX)
    fd.fastener("bolt", V(0.025, -1.275, 0.295 - 0.0835), nZ, "M14", real="17 mm (approximate)", label="drain plug", source=APPROX)
    fd.fastener("bolt", V(-0.040, -1.275 - 0.0830, 0.300), nY, "M14", real="17 mm (approximate)", label="filler plug", source=APPROX)
    rb = Part("RearAntiRollBar", "Shared", "Rear anti-roll bar with bushings and clamps.", "2 clamps, 2 bolts each.", stage=7)
    rb.collide = True; rb.hull_chunk = 0.12
    LBr = R["LB"]
    rb.tube([V(-LBr.x, LBr.y, LBr.z), V(0.42, -1.400, 0.338), V(0.25, -1.400, 0.338), V(-0.25, -1.400, 0.338), V(-0.42, -1.400, 0.338), LBr],
            0.010, "painted_black", segs=8, per=6)
    for sx in (-1, 1):
        rb.cyl(0.015, 0.016, V(sx * abs(LBr.x), LBr.y, LBr.z), Z_, "painted_black", segs=16)
        c = V(sx * 0.250, -1.400, 0.338)
        rb.cyl(0.020, 0.036, c, X_, "rubber", segs=20)
        rb.box((0.036, 0.060, 0.006), c + V(0, 0, -0.023), "painted_black", bevel=0.002)
        for yy in (-0.025, 0.025):
            rb.fastener("bolt", c + V(0, yy, -0.026), nZ, "M8", real="13 mm (approximate)", label="rear ARB clamp bolt", source=APPROX)
    return [sf, rk, fb, rs, rb, fd]

# ============================================================== BUILD
F = build_front_left()
R = build_rear_left()
right = []
for d in (F, R):
    for k, p in d.items():
        if isinstance(p, Part):
            right.append(p.mirrored(p.name[:-2] + ("FR" if p.name.endswith("FL") else "RR")))
DS = build_driveshaft("FL", HP["F_DS_IN_L"], HP["F_WHEEL"] + V(0.012, 0, 0), HP["F_WHEEL"])
DSR_W = mirror_v(HP["F_WHEEL"])
DS += build_driveshaft("FR", HP["F_DS_IN_R"], DSR_W + V(-0.012, 0, 0), DSR_W)
# rear (4x4): inner bolt pattern turned so no bolt backs out into the upper-link clevis above the shaft
RB = [-8 + 60 * k for k in range(6)]
DS += build_driveshaft("RL", HP["R_DS_IN_L"], HP["R_DS_OUT_L"], HP["R_WHEEL"], "Rear", RB)
DS += build_driveshaft("RR", HP["R_DS_IN_R"], HP["R_DS_OUT_R"], mirror_v(HP["R_WHEEL"]), "Rear", RB)
SH = build_shared(F, R)

# ============================================================== FINALIZE
for p in PL.PARTS: finalize(p, PL.COLL[p.group])
for f in PL.FASTENERS:
    finalize(f, PL.COLL[f.parent_part.group], parent=f.parent_part.obj)
    f.obj["tool"] = f.tool; f.obj["real_tool"] = f.real_tool
bpy.context.view_layer.update()
for p in PL.PARTS:
    co = PL.COLL[p.group]
    for i, fd in enumerate(p.fast, 1):
        fd["bolt_point"] = f"{p.name}_BoltPoint_{i:02d}"
        add_empty(fd["bolt_point"], p.obj, fd["position"], fd["axis"], co)
    if p.moving_end is not None: add_empty(f"{p.name}_MovingEnd", p.obj, p.moving_end, (0, 0, 1), co, 0.03, 'SPHERE')
    if p.bottom_point is not None: add_empty(f"{p.name}_BottomPoint", p.obj, p.bottom_point, (0, 0, 1), co, 0.03, 'SPHERE')
    for nm, pos, ax in p.points: add_empty(nm, p.obj, pos, ax, co, 0.02)

# collision hulls
for p in PL.PARTS:
    p.colliders = 0
    if not p.collide: continue
    for i, pts in enumerate(p.hulls, 1):
        bmc = bmesh.new(); vs = [bmc.verts.new(p.W.inverted() @ v) for v in pts]
        bmesh.ops.convex_hull(bmc, input=vs)
        loose = [v for v in bmc.verts if not v.link_faces]
        if loose: bmesh.ops.delete(bmc, geom=loose, context='VERTS')
        if len(bmc.faces) < 4: bmc.free(); continue
        me = bpy.data.meshes.new(f"UCX_{p.name}_{i:02d}"); bmc.to_mesh(me); bmc.free()
        ob = bpy.data.objects.new(me.name, me); PL.COLL[p.group].objects.link(ob)
        ob.parent = p.obj; ob.matrix_parent_inverse = Matrix.Identity(4); ob.matrix_basis = Matrix.Identity(4)
        ob.display_type = 'WIRE'; ob.hide_render = True; ob["collider"] = True
        p.colliders += 1

# grime vertex colour: dark where parts meet, round fasteners, in inside corners; brake dust on wheel parts
from mathutils.bvhtree import BVHTree
bpy.context.view_layer.update()
WB, BB = {}, {}
for p in PL.PARTS:
    M = p.obj.matrix_world; vw = [M @ v.co for v in p.obj.data.vertices]
    WB[p.name] = BVHTree.FromPolygons(vw, [tuple(pl.vertices) for pl in p.obj.data.polygons])
    BB[p.name] = (Vector([min(v[i] for v in vw) for i in range(3)]), Vector([max(v[i] for v in vw) for i in range(3)]))
GRIME = (0.24, 0.20, 0.15)
def set_grime(me, g):
    att = me.color_attributes.get("Grime") or me.color_attributes.new("Grime", 'BYTE_COLOR', 'POINT')
    for i, v in enumerate(g): att.data[i].color = (1 - (1 - GRIME[0]) * v, 1 - (1 - GRIME[1]) * v, 1 - (1 - GRIME[2]) * v, 1.0)
    me.color_attributes.active_color = att
RJ = 0.035
for p in PL.PARTS:
    lo, hi = BB[p.name]
    others = [q for q in PL.PARTS if q is not p]
    others = [q for q in others if all(BB[q.name][0][i] - RJ <= hi[i] and BB[q.name][1][i] + RJ >= lo[i] for i in range(3))]
    fpos = [Vector(f["position"]) for f in p.fast]
    cbm = bmesh.new(); cbm.from_mesh(p.obj.data); cbm.verts.ensure_lookup_table()
    concave = [0.0] * len(cbm.verts)
    for e in cbm.edges:
        if len(e.link_faces) == 2:
            a = e.calc_face_angle_signed(0.0)
            if a < -0.35:
                for vv in e.verts: concave[vv.index] = max(concave[vv.index], min(1.0, -a / 1.4))
    cbm.free()
    M = p.obj.matrix_world; vals = []
    for vi, v in enumerate(p.obj.data.vertices):
        w = M @ v.co; d = 1.0
        for q in others:
            hit = WB[q.name].find_nearest(w, RJ)
            if hit[0] is not None: d = min(d, hit[3])
        for fp in fpos: d = min(d, max(0.0, (w - fp).length - 0.008))
        g = max(0.0, 1 - d / RJ) ** 1.3 * 0.9
        g = max(g, concave[vi] * 0.6)
        vals.append(min(1.0, g))
    set_grime(p.obj.data, vals)
for f in PL.FASTENERS: set_grime(f.obj.data, [0.3] * len(f.obj.data.vertices))

# custom properties
def r4(v): return [round(x, 4) for x in v]
for p in PL.PARTS:
    o = p.obj
    o["side"] = p.side or "shared"; o["group"] = p.group; o["description"] = p.note; o["fastener_note"] = p.fnote
    o["fastener_count"] = len(p.fast); o["triangles"] = p.tris; o["colliders"] = p.colliders; o["removal_stage"] = p.stage
    if p.spin_axis: o["spin_axis"] = [float(x) for x in p.spin_axis]
    if p.moving_end is not None: o["moving_end_m"] = r4(p.moving_end)
    if p.bottom_point is not None: o["bottom_point_m"] = r4(p.bottom_point)
    if p.tool: o["tool"] = p.tool
    if p.real_tool: o["real_tool"] = p.real_tool

# ============================================================== MANIFEST / CSV
man = []
for p in PL.PARTS:
    e = dict(name=p.name, side=p.side or "shared", group=p.group, description=p.note, fastener_note=p.fnote,
             origin_m=r4(p.center), size_m=r4(p.bbox[1] - p.bbox[0]), triangles=p.tris, colliders=p.colliders,
             removal_stage=p.stage, fasteners=p.fast)
    if p.moving_end is not None: e["moving_end_m"] = r4(p.moving_end)
    if p.bottom_point is not None: e["bottom_point_m"] = r4(p.bottom_point)
    if p.orient_axis is not None: e["local_z_axis"] = r4(p.orient_axis)
    if p.spin_axis: e["spin_axis"] = p.spin_axis
    if p.tool: e["tool"] = p.tool
    if p.real_tool: e["real_tool"] = p.real_tool
    man.append(e)
hp_report = {k: dict(given=r4(HP[k]), used=r4(USED[k]), deviation_mm=round((USED[k] - HP[k]).length * 1000, 1))
             for k in HP if isinstance(HP[k], Vector)}
with open(os.path.join(OUT, "parts_manifest.json"), "w") as fh:
    json.dump(dict(model="Skoda Octavia I (1U) suspension and brakes", units="metres",
                   frame="Blender Z-up; +X car right, +Y car front; origin on the ground under the middle of the car",
                   hard_points=hp_report, parts=man), fh, indent=1)
with open(os.path.join(OUT, "fasteners.csv"), "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["part", "side", "fastener", "bolt_point", "kind", "size", "tool", "real_tool", "label", "source",
                "x", "y", "z", "axis_x", "axis_y", "axis_z"])
    for p in PL.PARTS:
        for fd in p.fast:
            w.writerow([p.name, p.side or "shared", fd["name"], fd["bolt_point"], fd["kind"], fd["size"], fd["tool"], fd["real_tool"],
                        fd["label"], fd["source"], *fd["position"], *fd["axis"]])
        if p.tool and not p.fast:
            w.writerow([p.name, p.side or "shared", p.name, "", "part (no fastener)", "", PL.pick_tool(p.tool), p.tool if PL.pick_tool(p.tool) != p.tool else p.real_tool,
                        "", "", *r4(p.center), 0, 0, 1])

print("PARTS", len(PL.PARTS), "FASTENERS", len(PL.FASTENERS), "COLLIDERS", sum(p.colliders for p in PL.PARTS))
print("TRIS_TOTAL", sum(p.tris for p in PL.PARTS) + sum(f.tris for f in PL.FASTENERS),
      "PARTS_ONLY", sum(p.tris for p in PL.PARTS))
for p in sorted(PL.PARTS, key=lambda p: -p.tris)[:8]: print("  tris", p.tris, p.name)

bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "Octavia1U_Suspension.blend"), relative_remap=True, compress=True)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "Octavia1U_Suspension.fbx"), use_selection=False,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE',
                         path_mode='RELATIVE', add_leaf_bones=False, bake_anim=False, colors_type='SRGB')
print("EXPORTED")
