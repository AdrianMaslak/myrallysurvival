"""Škoda Octavia I (1U) — modular drivetrain kit: 4x4 driveline, exhaust, fuel tanks and the front-drive rear axle.

Run:  python3 build_drive.py      (needs the `bpy` module; textures in export/textures)
Same conventions as the engine-bay set (v3) and the suspension set (v2): metres, Blender Z-up, +X car's right,
+Y car's front, origin on the ground under the middle of the car. Built against the engine set as the game places it
(x 0.88 at 0.16, 1.507, 0.381), the v2 suspension and the v2 body.

Collections (= modules):
  Drivetrain4x4   AngleDrive (bolts to the back of the 02J), Propshaft_Front, PropshaftCentreBearing, Propshaft_Rear
  Exhaust         Exhaust_Cat, Exhaust_CentreSilencer, Exhaust_RearSilencer   (one route fits both cars)
  FuelSystem      FuelTank_FWD, FuelTank_4x4 (saddle), FuelFillerNeck (fits both)
  RearAxleFWD     TwistBeam, PivotBracket_RL/RR, RearHub_RL/RR   (springs, dampers, brakes, wheels from the suspension set)
"""
import bpy, bmesh, math, json, os, sys, csv
from mathutils import Vector, Matrix
ROOT = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, ROOT)
import partlib as PL
from partlib import Part, V, mirror_v, finalize, add_empty, basis_from

OUT = os.path.join(ROOT, "export"); os.makedirs(OUT, exist_ok=True)
PL.TEX = os.path.join(OUT, "textures")
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
for cname in ["Drivetrain4x4", "Exhaust", "FuelSystem", "RearAxleFWD"]:
    c = bpy.data.collections.new(cname); scene.collection.children.link(c); PL.COLL[cname] = c
X_, Y_, Z_ = V(1, 0, 0), V(0, 1, 0), V(0, 0, 1)
nX, nY, nZ = -X_, -Y_, -Z_
APPROX = "approximate"
UNDER = json.load(open(os.path.join(ROOT, "work", "underside.json")))


def under(x, y, r=0.015):
    """lowest body underside within r of (x, y) (conservative). Grid: 1 cm, x -0.80..0.80, y 1.30..-2.40."""
    xs, ys, Z = UNDER["xs"], UNDER["ys"], UNDER["z"]
    j0, j1 = int((ys[0] - (y + r)) / 0.01) - 1, int((ys[0] - (y - r)) / 0.01) + 2
    i0, i1 = int((x - r - xs[0]) / 0.01) - 1, int((x + r - xs[0]) / 0.01) + 2
    best = 9.0
    for j in range(max(0, j0), min(len(ys), j1)):
        if abs(ys[j] - y) > r + 1e-9: continue
        for i in range(max(0, i0), min(len(xs), i1)):
            if abs(xs[i] - x) > r + 1e-9: continue
            z = Z[j][i]
            if z is not None: best = min(best, z)
    assert best < 5, f"no body underside near {x:.3f}, {y:.3f}"
    return best


# ============================================================== KEY POINTS (car frame)
GB_FACE_Y = 1.265                      # rear face of the 02J (engine set x0.88) at its right end
OUTF = V(-0.010, 1.212, 0.345)         # angle-drive output flange face, propshaft axis
CB = V(-0.008, -0.050, 0.312)          # centre bearing
FD_FLANGE = V(0.0, -1.060, 0.295)      # propshaft flange face of the rear final drive (suspension set)
RJ = FD_FLANGE + V(0, 0.050, 0)        # rear joint centre
DP_OUT = V(0.2128, 1.0054, 0.1346)     # downpipe outlet (engine set ExhaustFrontPipe, as placed)
R_WHEEL = V(-0.702, -1.307, 0.272)
R_DAMP_BOT = V(-0.571, -1.407, 0.160)  # suspension v2 rear damper lower eye
SPR_C = V(-0.470, -1.150, 0.0); SPR_SEAT_Z = 0.2746   # suspension v2 rear spring axis / lower seat
TB_PIV = V(-0.545, -0.842, 0.245)      # eye clears the floor; beam clears the trailing-arm sheet at bump      # twist-beam pivot (left)
R_CAL_TH = 165.0


def rot_basis(th):
    """as the suspension set: X along the wheel axis, Y tangential, Z radial at angle th."""
    th = math.radians(th)
    x = V(1, 0, 0); z = V(0, math.cos(th), math.sin(th)); y = z.cross(x)
    return Matrix((x, y, z)).transposed()


# ============================================================== 4x4 DRIVELINE
def build_driveline():
    ad = Part("AngleDrive", "Drivetrain4x4", "Angle drive (transfer box): bolts to the back of the 02J at its final drive and turns the drive "
              "90 degrees to the propshaft. Fit it to make the 4x4; leave it off for the front-drive car.",
              "4 bolts to the gearbox; drain and filler plugs.", stage=7)
    ad.origin = OUTF; ad.orient_axis = nY; ad.collide = True; ad.hull_chunk = 0.10
    hc = OUTF + V(0, 0.028, 0)
    ad.cyl(0.042, 0.046, hc, Y_, "cast_alu", segs=28, bevel=0.005)                                  # bevel-gear housing
    ad.cyl(0.036, 0.012, OUTF + V(0, 0.0, 0), Y_, "cast_alu", segs=24, bevel=0.003)               # output neck
    ad.box((0.105, 0.012, 0.082), V(-0.022, GB_FACE_Y - 0.004, 0.340), "cast_alu", bevel=0.004)   # mounting flange on the gearbox
    for dx in (-0.030, 0.0, 0.030): ad.box((0.006, 0.040, 0.012), hc + V(dx, -0.002, 0.040), "cast_alu", bevel=0.002)  # ribs
    ad.box((0.040, 0.030, 0.014), hc + V(0, 0.0, -0.046), "cast_alu", bevel=0.003)               # drain boss
    ad.revolve([(0.012, 0.0), (0.042, 0.0), (0.042, 0.007), (0.012, 0.007)], OUTF + V(0, -0.007, 0), Y_, "zinc", segs=24)  # output flange
    ad.cyl(0.025, 0.004, OUTF + V(0, 0.002, 0), Y_, "rubber", segs=20)                            # shaft seal
    for sx, sz in ((-0.066, 0.306), (-0.066, 0.374), (0.022, 0.306), (0.022, 0.374)):
        ad.fastener("bolt", V(sx, GB_FACE_Y - 0.010, sz), nY, "M10", real="16 mm (approximate)", label="angle drive to gearbox", source=APPROX)
    ad.fastener("socket", hc + V(0, 0.0, -0.053), nZ, "M14", real="17 mm (approximate)", label="drain plug", source=APPROX)
    ad.fastener("socket", hc + V(0.043, 0.0, 0.010), X_, "M14", real="17 mm (approximate)", label="filler plug", source=APPROX)
    ad.points.append(("AngleDrive_OutputFlange", OUTF + V(0, -0.007, 0), nY))
    ad.props.update(motion="spin", spin_axis_local="Z", note="output turns with the propshaft; its input is the gearbox final drive")

    # front propshaft: flex disc at the angle drive, tube, splined stub into the centre bearing
    d = (CB - OUTF).normalized(); L = (CB - OUTF).length
    pf = Part("Propshaft_Front", "Drivetrain4x4", "Front propshaft section: rubber flex disc at the angle drive, 45 mm tube, "
              "stub through the centre bearing.", "3 flex-disc bolts to the angle-drive flange.", stage=6)
    pf.origin = OUTF + V(0, -0.007, 0); pf.orient_axis = d; pf.spin_axis = [0, 0, 1]; pf.moving_end = CB; pf.collide = True; pf.hull_chunk = 0.25
    s0 = pf.origin
    at = lambda s: s0 + d * s
    pf.cyl_between(at(0.0), at(0.018), 0.041, "rubber", segs=24, bevel=0.003)                    # flex disc (guibo)
    pf.revolve([(0.010, 0.0), (0.036, 0.0), (0.036, 0.006), (0.010, 0.006)], at(0.018), d, "zinc", segs=24)   # yoke flange
    pf.cyl_between(at(0.024), at(0.060), 0.028, "zinc", segs=20, r2=0.0225)                       # yoke neck
    pf.cyl_between(at(0.055), at(L - 0.050), 0.0225, "painted_black", segs=20)                     # tube
    pf.cyl_between(at(L - 0.055), at(L - 0.030), 0.0225, "zinc", segs=20, r2=0.016)              # weld-on stub
    pf.cyl_between(at(L - 0.030), at(L + 0.012), 0.016, "zinc", segs=16)                          # splined stub
    for k in range(3):
        a = math.radians(30 + 120 * k)
        off = (basis_from(d) @ V(0, math.cos(a) * 0.030, math.sin(a) * 0.030))
        pf.fastener("bolt", at(0.020) + off, -d, "M10", real="16 mm (approximate)", label="flex disc to angle drive", source=APPROX)
    pf.props.update(motion="spin", spin_axis_local="Z")

    cb = Part("PropshaftCentreBearing", "Drivetrain4x4", "Centre bearing: rubber-mounted ball bearing in a pressed bracket under the tunnel.",
              "2 bolts up into the tunnel.", stage=6)
    cb.origin = CB; cb.collide = True
    top_z = under(CB.x, CB.y, 0.06) - 0.0005
    cb.cyl(0.035, 0.030, CB, Y_, "rubber", segs=24)
    cb.cyl(0.020, 0.034, CB, Y_, "zinc", segs=20)
    cb.revolve([(0.034, -0.016), (0.038, -0.016), (0.038, 0.016), (0.034, 0.016)], CB, Y_, "painted_black", segs=24)
    cb.box((0.130, 0.050, 0.005), V(CB.x, CB.y, top_z - 0.0025), "painted_black", bevel=0.0)
    for sx in (-1, 1):
        cb.box((0.005, 0.040, top_z - CB.z - 0.002), V(CB.x + sx * 0.0405, CB.y, (top_z + CB.z) / 2 - 0.002), "painted_black")
        cb.fastener("bolt", V(CB.x + sx * 0.052, CB.y, top_z - 0.005), nZ, "M8", real="13 mm (approximate)", label="centre bearing to body", source=APPROX)

    d2 = (RJ - CB).normalized(); L2 = (RJ - CB).length
    pr = Part("Propshaft_Rear", "Drivetrain4x4", "Rear propshaft section: CV joint behind the centre bearing, 50 mm tube, CV joint bolted "
              "to the rear final drive (Haldex) flange.", "6 triple-square bolts to the final-drive flange.", stage=6)
    pr.origin = CB; pr.orient_axis = d2; pr.spin_axis = [0, 0, 1]; pr.moving_end = FD_FLANGE; pr.collide = True; pr.hull_chunk = 0.25
    bt = lambda s: CB + d2 * s
    pr.cyl_between(bt(0.020), bt(0.062), 0.038, "zinc", segs=24, bevel=0.003)                    # centre CV joint
    pr.revolve([(0.016, 0.0), (0.034, 0.010), (0.030, 0.020), (0.026, 0.030), (0.022, 0.040), (0.016, 0.040)], bt(0.062), d2, "rubber", segs=20, closed=True)
    pr.cyl_between(bt(0.095), bt(L2 - 0.070), 0.025, "painted_black", segs=20)                     # tube
    pr.cyl_between(bt(0.090), bt(0.105), 0.022, "zinc", segs=16, r2=0.025)
    pr.revolve([(0.016, 0.0), (0.030, 0.0), (0.034, 0.010), (0.038, 0.020), (0.042, 0.030), (0.016, 0.030)], bt(L2 - 0.075), d2, "rubber", segs=20, closed=True)
    pr.cyl_between(bt(L2 - 0.045), bt(L2 + 0.050 - 0.002), 0.045, "zinc", segs=28, bevel=0.003)    # rear CV joint (100 mm flange)
    for k in range(6):
        a = math.radians(30 + 60 * k)
        off = (basis_from(d2) @ V(0, math.cos(a) * 0.036, math.sin(a) * 0.036))
        pr.fastener("socket", RJ + d2 * (-0.050) + off + d2 * 0.0, -d2, "M8", real="triple-square M8 (XZN)", label="rear CV to final drive",
                    source="Golf IV 4motion propshaft to Haldex flange, 6 bolts")
    pr.props.update(motion="spin", spin_axis_local="Z")
    return dict(ad=ad, pf=pf, cb=cb, pr=pr)


# ============================================================== EXHAUST (shared route)
EX_X = 0.088; EX_Z = 0.272; EX_R = 0.0265
def hanger(p, at_pt, label):
    """rubber hanger: pin on the pipe, rubber block, pin up to a body bracket"""
    zb = under(at_pt.x, at_pt.y, 0.02) - 0.002
    p.cyl_between(at_pt + V(0, 0, 0.012), V(at_pt.x, at_pt.y, zb - 0.040), 0.005, "zinc", segs=8)
    p.box((0.020, 0.040, 0.030), V(at_pt.x, at_pt.y, zb - 0.040), "rubber", bevel=0.004)
    p.cyl_between(V(at_pt.x, at_pt.y, zb - 0.026), V(at_pt.x, at_pt.y, zb), 0.005, "zinc", segs=8)
    p.points.append((f"{p.name}_Hanger_{len([q for q in p.points if 'Hanger' in q[0]]) + 1:02d}", V(at_pt.x, at_pt.y, zb), Z_))


def flange(p, c, axis, r=0.040, t=0.008, holes=2, size="M10", label="flange nut", back=None):
    p.revolve([(EX_R - 0.002, 0.0), (r, 0.0), (r, t), (EX_R - 0.002, t)], c, axis, "exhaust", segs=24)
    ax = Vector(axis).normalized(); B = basis_from(ax)
    for k in range(holes):
        a = math.radians(90 + 360 / holes * k)
        off = B @ V(0, math.cos(a) * (r - 0.008), math.sin(a) * (r - 0.008))
        p.fastener("nut", c + off + ax * (t if back is None else back), ax, size, real="13 mm (approximate)", label=label, source=APPROX)


def oval_profile(w, h, n=20):
    return [(w / 2 * math.cos(2 * math.pi * k / n), h / 2 * math.sin(2 * math.pi * k / n)) for k in range(n)]


def build_exhaust():
    cat = Part("Exhaust_Cat", "Exhaust", "Front pipe with catalytic converter, from the downpipe flange up into the right half of the tunnel. "
               "Replaces the engine set's underfloor cat for both cars.", "3 nuts at the downpipe, 2 at the rear flange; 1 hanger.", stage=5)
    cat.collide = True; cat.hull_chunk = 0.10
    A = V(EX_X, 0.780, EX_Z); Bc = V(EX_X, 0.460, EX_Z)
    path = [DP_OUT + V(0, -0.008, 0), DP_OUT + V(-0.004, -0.045, 0.002), V(0.190, 0.935, 0.150), V(0.140, 0.870, 0.205),
            V(0.100, 0.820, 0.255), A + V(0, 0.020, 0)]
    cat.tube(path, EX_R, "exhaust", segs=16, per=6)
    # two-bolt oval flange (bolts either side): keeps it 10 mm clear of the subframe cross member just in front
    cat.box((0.090, 0.008, 0.058), DP_OUT + V(0, -0.004, 0), "exhaust", bevel=0.003)
    for sx in (-1, 1):
        cat.fastener("nut", DP_OUT + V(sx * 0.034, -0.008, 0), nY, "M10", real="13 mm (approximate)", label="downpipe flange nut", source=APPROX)
    cat.cyl_between(A + V(0, 0.022, 0), A + V(0, -0.025, 0), EX_R, "exhaust", segs=24, r2=0.054)
    cat.cyl_between(A + V(0, -0.025, 0), Bc + V(0, 0.025, 0), 0.054, "exhaust", segs=28, bevel=0.004)
    cat.cyl_between(Bc + V(0, 0.025, 0), Bc + V(0, -0.022, 0), 0.054, "exhaust", segs=24, r2=EX_R)
    for yy in (0.70, 0.62, 0.54): cat.cyl(0.0565, 0.006, V(EX_X, yy, EX_Z), Y_, "exhaust", segs=28)     # shell beads
    cat.cyl_between(V(EX_X, 0.475, EX_Z) + V(0.030, 0, 0.034), V(EX_X, 0.475, EX_Z) + V(0.040, 0, 0.046), 0.009, "zinc", segs=12)  # lambda sensor
    cat.cyl_between(Bc + V(0, -0.020, 0), V(EX_X, 0.420, EX_Z), EX_R, "exhaust", segs=16)
    flange(cat, V(EX_X, 0.420, EX_Z), nY, holes=2, label="cat rear flange nut")
    hanger(cat, V(EX_X, 0.740, EX_Z + EX_R + 0.001) + V(0.0, 0, 0.0), "cat")
    cat.points.append(("Exhaust_Cat_DragPoint", V(EX_X, 0.62, EX_Z - 0.054), nZ))

    cs = Part("Exhaust_CentreSilencer", "Exhaust", "Centre silencer (slim, in the tunnel beside the propshaft) and the pipe that leaves the "
              "tunnel and runs back along the right of the floor.", "2 nuts at each flange; 2 hangers.", stage=5)
    cs.collide = True; cs.hull_chunk = 0.12
    S0, S1 = V(EX_X, 0.380, EX_Z - 0.004), V(EX_X, 0.060, EX_Z - 0.004)
    cs.cyl_between(V(EX_X, 0.420, EX_Z), V(EX_X, 0.395, EX_Z), EX_R, "exhaust", segs=16)
    flange(cs, V(EX_X, 0.420, EX_Z), Y_, holes=2, label="centre silencer front flange nut", back=-0.0001) if False else None
    cs.tube(None, 0, "exhaust", path=[S0 + V(0, 0.016, 0), S0, S1, S1 + V(0, -0.016, 0)], profile=oval_profile(0.092, 0.068, 24), segs=24)
    cs.cyl_between(V(EX_X, 0.395, EX_Z), S0 + V(0, 0.010, 0), EX_R, "exhaust", segs=16, r2=0.030)
    rear = [S1 + V(0, -0.010, 0), V(EX_X, -0.150, EX_Z - 0.006), V(0.100, -0.270, 0.178), V(0.200, -0.350, 0.150), V(0.320, -0.420, 0.150),
            V(0.360, -0.490, 0.150), V(0.360, -1.000, 0.150), V(0.360, -1.420, 0.150)]
    cs.tube(rear, EX_R, "exhaust", segs=16, per=6)
    flange(cs, V(0.360, -1.420, 0.150), nY, holes=2, label="centre silencer rear flange nut")
    hanger(cs, V(0.360, -0.600, 0.150 + EX_R), "mid"); hanger(cs, V(0.360, -0.800, 0.150 + EX_R), "rear")
    cs.points.append(("Exhaust_CentreSilencer_DragPoint", V(0.360, -1.0, 0.150 - EX_R), nZ))

    rs = Part("Exhaust_RearSilencer", "Exhaust", "Pipe under the rear axle into the transverse rear silencer behind it; tailpipe on the left.",
              "2 nuts at the front flange; 2 hangers.", stage=4)
    rs.collide = True; rs.hull_chunk = 0.12
    rs.tube([V(0.360, -1.420, 0.150), V(0.360, -1.500, 0.158), V(0.320, -1.560, 0.205), V(0.300, -1.590, 0.240)], EX_R, "exhaust", segs=16, per=6)
    box_c = V(0.0, -1.720, 0.268)
    rs.box((0.740, 0.260, 0.100), box_c, "exhaust", bevel=0.030)
    rs.cyl(0.068, 0.014, box_c + V(0.374, 0, 0), X_, "exhaust", segs=24)
    rs.cyl(0.068, 0.014, box_c + V(-0.374, 0, 0), X_, "exhaust", segs=24)
    tail = [box_c + V(-0.380, -0.060, -0.010), V(-0.410, -1.820, 0.245), V(-0.425, -1.980, 0.205), V(-0.430, -2.200, 0.180), V(-0.430, -2.300, 0.178)]
    rs.tube(tail, EX_R * 0.9, "exhaust", segs=16, per=6)
    rs.cyl_between(V(-0.430, -2.240, 0.178), V(-0.430, -2.330, 0.178), 0.032, "alu_machined", segs=24)        # tail tip
    for hx in (-0.300, 0.300): hanger(rs, box_c + V(hx, 0.0, 0.050), "rear silencer")
    rs.points.append(("Exhaust_RearSilencer_TailPipe", V(-0.430, -2.330, 0.178), nY))
    rs.points.append(("Exhaust_RearSilencer_DragPoint", box_c + V(0, 0, -0.050), nZ))
    return dict(cat=cat, cs=cs, rs=rs)


# ============================================================== FUEL TANKS
TANK_BOT = 0.140
def lobe(p, x0, x1, y0, y1, zb=TANK_BOT, ztop=0.345, gap=0.012, inset=0.015, step=0.03):
    """tank lobe whose top follows the floor (gap below it); rounded shoulders; quads; returns volume (m3)."""
    t = bmesh.new()
    nx = max(2, int(round((x1 - x0) / step))); ny = max(2, int(round((y1 - y0) / step)))
    ix0, ix1, iy0, iy1 = x0 + inset, x1 - inset, y0 + inset, y1 - inset
    def top(x, y): return min(ztop, under(x, y, 0.03) - gap)
    G = [[None] * (nx + 1) for _ in range(ny + 1)]; Bt = [[None] * (nx + 1) for _ in range(ny + 1)]
    vol = 0.0
    for j in range(ny + 1):
        for i in range(nx + 1):
            x = ix0 + (ix1 - ix0) * i / nx; y = iy0 + (iy1 - iy0) * j / ny
            G[j][i] = t.verts.new((x, y, top(x, y))); Bt[j][i] = t.verts.new((x, y, zb))
    for j in range(ny):
        for i in range(nx):
            t.faces.new((G[j][i], G[j][i + 1], G[j + 1][i + 1], G[j + 1][i]))
            t.faces.new((Bt[j][i], Bt[j + 1][i], Bt[j + 1][i + 1], Bt[j][i + 1]))
            h = sum(G[j + a][i + b].co.z for a in (0, 1) for b in (0, 1)) / 4 - zb
            vol += h * (ix1 - ix0) / nx * (iy1 - iy0) / ny
    ring_t, ring_b = [], []
    loop = [(0, i) for i in range(nx)] + [(j, nx) for j in range(ny)] + [(ny, i) for i in range(nx, 0, -1)] + [(j, 0) for j in range(ny, 0, -1)]
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    for (j, i) in loop:
        g, b = G[j][i], Bt[j][i]
        ox = -inset if i == 0 else inset if i == nx else 0.0; oy = -inset if j == 0 else inset if j == ny else 0.0
        o = Vector((ox, oy, 0))
        A1 = t.verts.new(g.co + o * 0.72 + Vector((0, 0, -0.005)))
        A2 = t.verts.new(g.co + o + Vector((0, 0, -inset)))
        B2 = t.verts.new(b.co + o + Vector((0, 0, inset)))
        B1 = t.verts.new(b.co + o * 0.72 + Vector((0, 0, 0.005)))
        ring_t.append((g, A1, A2, B2, B1, b))
    n = len(ring_t)
    for k in range(n):
        a, c = ring_t[k], ring_t[(k + 1) % n]
        for m in range(5):
            try: t.faces.new((a[m], a[m + 1], c[m + 1], c[m]))
            except ValueError: pass
    bmesh.ops.recalc_face_normals(t, faces=t.faces)
    p.collide = True
    p._merge(t, "plastic_black", Matrix.Identity(4))
    return vol


def strap(p, x0, x1, y, zb=TANK_BOT):
    zt0, zt1 = under(x0 - 0.02, y, 0.02), under(x1 + 0.02, y, 0.02)
    p.box((x1 - x0 + 0.020, 0.030, 0.003), V((x0 + x1) / 2, y, zb - 0.0015), "painted_black")
    for x, zt, sg in ((x0 - 0.010, zt0, -1), (x1 + 0.010, zt1, 1)):
        p.box((0.003, 0.030, zt - zb + 0.002), V(x, y, (zt + zb) / 2), "painted_black")
        p.box((0.025, 0.030, 0.003), V(x + sg * 0.012, y, zt - 0.0015), "painted_black")
        p.fastener("bolt", V(x + sg * 0.014, y, zt - 0.003), nZ, "M8", real="13 mm (approximate)", label="tank strap bolt", source=APPROX)


INLET = V(0.312, -0.900, 0.250)
def build_tanks():
    tf = Part("FuelTank_FWD", "FuelSystem", "Fuel tank for the front-drive car: plastic, under the rear seat, top shaped to the floor.",
              "2 straps, 2 bolts each; filler neck and lines.", stage=6)
    vf = lobe(tf, -0.440, 0.310, -0.940, -0.520)
    for y in (-0.600, -0.840): strap(tf, -0.440, 0.310, y)
    tf.cyl_between(INLET + V(-0.012, 0, 0), INLET + V(0.010, 0, 0), 0.024, "plastic_black", segs=20)
    tf.cyl(0.045, 0.012, V(-0.10, -0.70, under(-0.10, -0.70, 0.05) - 0.012 - 0.004), Z_, "plastic_black", segs=24)   # sender flange
    tf.points.append(("FuelTank_FWD_LeakPoint", V(0.0, -0.80, TANK_BOT), nZ)); tf.points.append(("FuelTank_FWD_FillerInlet", INLET + V(0.010, 0, 0), X_))
    tf.props["volume_l"] = round(vf * 1000 * 0.94, 1)

    t4 = Part("FuelTank_4x4", "FuelSystem", "Saddle fuel tank for the 4x4: two lobes either side of the propshaft joined by a crossover "
              "under it; top shaped to the floor.", "2 straps, 2 bolts each; filler neck and lines.", stage=6)
    vl = lobe(t4, -0.440, -0.072, -1.000, -0.520); vr = lobe(t4, 0.064, 0.310, -1.000, -0.520)
    t4.tube([V(-0.086, -0.960, 0.175), V(-0.060, -0.960, 0.180), V(-0.004, -0.960, 0.200), V(0.050, -0.960, 0.180), V(0.078, -0.960, 0.175)],
            0.013, "rubber", segs=12, per=4)
    for y in (-0.600, -0.880): strap(t4, -0.440, 0.310, y)
    t4.cyl_between(INLET + V(-0.012, 0, 0), INLET + V(0.010, 0, 0), 0.024, "plastic_black", segs=20)
    t4.cyl(0.045, 0.012, V(-0.25, -0.70, under(-0.25, -0.70, 0.05) - 0.012 - 0.004), Z_, "plastic_black", segs=24)
    t4.points.append(("FuelTank_4x4_LeakPoint", V(-0.25, -0.80, TANK_BOT), nZ)); t4.points.append(("FuelTank_4x4_FillerInlet", INLET + V(0.010, 0, 0), X_))
    t4.props["volume_l"] = round((vl + vr) * 1000 * 0.94, 1)

    fn = Part("FuelFillerNeck", "FuelSystem", "Filler neck from the fuel flap down behind the right rear wheel and forward under the floor "
              "to the tank (fits both tanks).", "2 hose clips at the tank, 3 screws at the filler cup.", stage=5)
    fn.collide = True; fn.hull_chunk = 0.10
    path = [INLET + V(0.010, 0, 0), V(0.372, -0.904, 0.240), V(0.418, -0.925, 0.190), V(0.440, -0.990, 0.160), V(0.446, -1.300, 0.158),
            V(0.446, -1.560, 0.165), V(0.520, -1.680, 0.235), V(0.660, -1.715, 0.320), V(0.720, -1.716, 0.400), V(0.734, -1.712, 0.600),
            V(0.740, -1.695, 0.790), V(0.752, -1.668, 0.852), V(0.762, -1.660, 0.860)]
    fn.tube(path, 0.019, "painted_black", segs=14, per=6)
    fn.tube([INLET + V(0.010, 0, 0), V(0.360, -0.902, 0.251)], 0.023, "rubber", segs=14, per=2)
    fn.cyl_between(V(0.752, -1.660, 0.860), V(0.776, -1.660, 0.860), 0.034, "painted_black", segs=24)      # filler cup behind the flap
    fn.cyl_between(V(0.776, -1.660, 0.860), V(0.782, -1.660, 0.860), 0.030, "plastic_black", segs=24)     # cap
    for k in range(3):
        a = math.radians(90 + 120 * k)
        fn.fastener("screw", V(0.776, -1.660 + math.cos(a) * 0.027, 0.860 + math.sin(a) * 0.027), X_, real="Phillips screwdriver",
                    label="filler cup screw", source=APPROX)
    fn.fastener("clip", INLET + V(0.025, 0, 0.024), Z_, real="flat screwdriver", label="hose clip", source=APPROX)
    # rubber grommet where the neck passes through the inner wheelhouse / quarter panel (found from the body)
    GROM = V(0.7215, -1.716, under(0.7215, -1.716, 0.004))      # through the floor behind the wheelhouse, as a real neck
    fn.revolve([(0.019, -0.010), (0.031, -0.006), (0.031, 0.006), (0.019, 0.010)], GROM, V(0.05, 0.0, 1.0).normalized(), "rubber", segs=20)
    fn.points.append(("FuelFillerNeck_FloorGrommet", GROM, Z_))
    fn.points.append(("FuelFillerNeck_Cap", V(0.782, -1.660, 0.860), X_))
    return dict(tf=tf, t4=t4, fn=fn)


# ============================================================== FRONT-DRIVE REAR AXLE (twist beam)
def build_twist_beam():
    PL_, PR_ = TB_PIV, mirror_v(TB_PIV)
    tb = Part("TwistBeam", "RearAxleFWD", "Twist-beam rear axle for the front-drive car: two trailing arms on a U-section cross member "
              "(it twists, so it is also the rear anti-roll bar), spring pans, damper eyes and hub plates.",
              "Pivot bolt + nut each side (in the brackets); the hubs, springs and dampers bolt to it.", stage=6)
    tb.origin = (PL_ + PR_) / 2; tb.collide = True; tb.hull_chunk = 0.10
    Wl = R_WHEEL
    for sx in (-1, 1):
        f = lambda v: V(v.x * (1 if sx < 0 else -1), v.y, v.z)
        piv = f(PL_); W = f(Wl)
        tb.cyl(0.032, 0.050, piv, X_, "painted_black", segs=24, bevel=0.003)                    # pivot eye
        tb.cyl(0.027, 0.052, piv, X_, "rubber", segs=24)
        bend = f(V(-0.563, -1.160, 0.285)); endp = f(V(-0.622, -1.268, 0.275))
        tb.beam(piv + V(0, -0.028, 0), bend, 0.032, 0.050, "painted_black", up=Z_, bevel=0.005)
        tb.beam(bend, endp, 0.032, 0.046, "painted_black", up=Z_, bevel=0.005)
        plate_x = W.x + (0.058 if sx < 0 else -0.058)
        tb.box((0.010, 0.094, 0.094), V(plate_x, W.y, W.z), "painted_black", bevel=0.008)       # hub plate
        tb.beam(endp, V(plate_x, W.y + 0.030, W.z), 0.030, 0.044, "painted_black", up=Z_, bevel=0.004)
        DB = f(R_DAMP_BOT)
        body = V(plate_x, W.y - 0.045, W.z - 0.058)
        tb.beam(body, DB + (body - DB).normalized() * 0.024, 0.026, 0.022, "painted_black", up=X_, bevel=0.004)
        for sgn in (-1, 1): tb.box((0.030, 0.006, 0.044), DB + V(0, sgn * 0.0265, 0), "painted_black", bevel=0.002)
        sc = f(SPR_C)                                                                           # spring pan on the arm
        tb.revolve([(0.0, SPR_SEAT_Z), (0.064, SPR_SEAT_Z), (0.066, SPR_SEAT_Z + 0.008), (0.070, SPR_SEAT_Z + 0.008), (0.068, SPR_SEAT_Z - 0.004),
                    (0.0, SPR_SEAT_Z - 0.004)], V(sc.x, sc.y, 0), Z_, "painted_black", segs=24)
        tb.box((0.110, 0.030, 0.018), V((sc.x + bend.x) / 2, sc.y, SPR_SEAT_Z - 0.012), "painted_black", bevel=0.003)   # pan to arm
        tb.beam(V(sc.x, sc.y + 0.040, SPR_SEAT_Z - 0.010), V(sc.x, -1.050, 0.270), 0.030, 0.016, "painted_black", up=Z_, bevel=0.003)   # pan to cross member
        tb.points.append((f"TwistBeam_Wheel{'L' if sx < 0 else 'R'}", W, X_))
        tb.points.append((f"TwistBeam_Pivot{'L' if sx < 0 else 'R'}", piv, X_))
        tb.points.append((f"TwistBeam_SpringSeat{'L' if sx < 0 else 'R'}", V(sc.x, sc.y, SPR_SEAT_Z), Z_))
        tb.points.append((f"TwistBeam_DamperEye{'L' if sx < 0 else 'R'}", DB, Y_))
    yc, zc = -1.015, 0.255; xs = 0.565      # web top 0.292: 14 mm under the rear rails at full bump (+3 cm here)
    tb.box((2 * xs, 0.085, 0.006), V(0, yc, zc + 0.034), "painted_black", bevel=0.0)             # U section: web
    for dy in (-0.040, 0.040): tb.box((2 * xs, 0.006, 0.062), V(0, yc + dy, zc + 0.004), "painted_black", bevel=0.0)   # legs
    for sx in (-1, 1):
        tb.box((0.050, 0.090, 0.068), V(sx * (xs - 0.010), yc, zc + 0.004), "painted_black", bevel=0.006)               # arm joint
    tb.props.update(motion="rotate", local_axis="X", range=[-9.8, 9.8], rest="middle",
                    note="whole beam about the pivot axis = both wheels +-8 cm. For one wheel, turn that arm about its own pivot "
                         "(the cross member twists; that is the axle's anti-roll stiffness).")

    br = []
    for sx, sfx in ((-1, "RL"), (1, "RR")):
        piv = V(sx * abs(PL_.x), PL_.y, PL_.z); zt = under(piv.x, piv.y, 0.03) - 0.0005
        b = Part(f"PivotBracket_{sfx}", "RearAxleFWD", "Pressed body bracket holding the twist-beam pivot bush.",
                 "3 bolts to the body, pivot bolt + nut.", side=sfx, stage=7)
        b.origin = piv; b.collide = True
        for sg in (-1, 1): b.box((0.006, 0.070, zt - piv.z + 0.035), V(piv.x + sg * 0.031, piv.y, (zt + piv.z - 0.035) / 2), "painted_black", bevel=0.002)
        b.box((0.090, 0.120, 0.006), V(piv.x, piv.y + 0.010, zt - 0.003), "painted_black", bevel=0.002)
        for p in (V(-0.030, 0.050, 0), V(0.030, 0.050, 0), V(0, -0.040, 0)):
            b.fastener("bolt", V(piv.x, piv.y, zt) + p + V(0, 0, -0.006), nZ, "M10", real="16 mm (approximate)", label="bracket to body", source=APPROX)
        b.fastener("bolt", piv + V(-0.037 * sx, 0, 0), V(-sx, 0, 0), "M12", real="18 mm (approximate)", label="pivot bolt", source=APPROX)
        b.fastener("nut", piv + V(0.037 * sx, 0, 0), V(sx, 0, 0), "M12", real="18 mm (approximate)", label="pivot nut", source=APPROX)
        br.append(b)

    hubs = []
    for sx, sfx in ((-1, "RL"), (1, "RR")):
        W = V(sx * abs(R_WHEEL.x), R_WHEEL.y, R_WHEEL.z); ax = V(-sx, 0, 0)      # axis points inboard (as the 4x4 hub carrier's X_ on the left)
        h = Part(f"RearHub_{sfx}", "RearAxleFWD", "Rear stub axle with bolted hub-bearing unit for the twist beam; carries the brake disc and "
                 "the caliper carrier (same positions as the 4x4 hub carrier).", "4 bolts to the twist-beam hub plate.", side=sfx, stage=5)
        h.origin = W; h.spin_axis = [1, 0, 0]; h.collide = True
        h.revolve([(0.0280, -0.0450), (0.0283, -0.0330), (0.0600, -0.0330), (0.0600, -0.0250), (0.0360, -0.0250),
                   (0.0360, -0.0050), (0.0280, -0.0050)], W, ax, "cast_iron", segs=24)
        h.revolve([(0.020, 0.0), (0.048, 0.0), (0.048, 0.050), (0.020, 0.050)], W, ax, "cast_iron", segs=24)        # stub axle carrier
        h.box((0.008, 0.096, 0.096), W + ax * 0.054, "cast_iron", bevel=0.004)                                         # flange on the hub plate
        for sgn in (-1, 1):
            lug = W + ax * 0.0095 + V(0, (0.076 * math.cos(math.radians(R_CAL_TH)) - sgn * 0.048 * math.sin(math.radians(R_CAL_TH))),
                                         (0.076 * math.sin(math.radians(R_CAL_TH)) + sgn * 0.048 * math.cos(math.radians(R_CAL_TH))))
            h.box((0.010, 0.020, 0.022), lug, "cast_iron", basis=rot_basis(R_CAL_TH), bevel=0.002)
        for yy in (-0.036, 0.036):
            for zz in (-0.036, 0.036):
                h.fastener("twelve", W + ax * 0.058 + V(0, yy, zz), ax, "M10", real="12-point M10 (approximate)", label="hub to twist beam", source=APPROX)
        hubs.append(h)
    return dict(tb=tb, br=br, hubs=hubs)


DL = build_driveline(); EX = build_exhaust(); FT = build_tanks(); RA = build_twist_beam()

# ============================================================== FINALIZE (as the suspension set)
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
    for nm, pos, ax in p.points: add_empty(nm, p.obj, pos, ax, co, 0.02)
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

# grime: joints, fasteners, inside corners; road dirt on everything low
from mathutils.bvhtree import BVHTree
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
RJ_ = 0.035
for p in PL.PARTS:
    lo, hi = BB[p.name]
    others = [q for q in PL.PARTS if q is not p and all(BB[q.name][0][i] - RJ_ <= hi[i] and BB[q.name][1][i] + RJ_ >= lo[i] for i in range(3))]
    fpos = [Vector(f["position"]) for f in p.fast]
    M = p.obj.matrix_world; vals = []
    for v in p.obj.data.vertices:
        w = M @ v.co; d = 1.0
        for q in others:
            hit = WB[q.name].find_nearest(w, RJ_)
            if hit[0] is not None: d = min(d, hit[3])
        for fpp in fpos: d = min(d, max(0.0, (w - fpp).length - 0.008))
        g = max(0.0, 1 - d / RJ_) ** 1.3 * 0.9
        g = max(g, 0.35 * max(0.0, min(1.0, (0.30 - w.z) / 0.20)))            # road spray on the underside
        vals.append(min(1.0, g))
    set_grime(p.obj.data, vals)
for f in PL.FASTENERS: set_grime(f.obj.data, [0.3] * len(f.obj.data.vertices))

def r4(v): return [round(x, 4) for x in v]
for p in PL.PARTS:
    o = p.obj
    o["side"] = p.side or "shared"; o["group"] = p.group; o["description"] = p.note; o["fastener_note"] = p.fnote
    o["fastener_count"] = len(p.fast); o["triangles"] = p.tris; o["colliders"] = p.colliders; o["removal_stage"] = p.stage
    if p.spin_axis: o["spin_axis"] = [float(x) for x in p.spin_axis]
    if p.moving_end is not None: o["moving_end_m"] = r4(p.moving_end)
    for k, v in p.props.items(): o[k] = v

man = []
for p in PL.PARTS:
    e = dict(name=p.name, side=p.side or "shared", group=p.group, description=p.note, fastener_note=p.fnote,
             origin_m=r4(p.center), size_m=r4(p.bbox[1] - p.bbox[0]), triangles=p.tris, colliders=p.colliders,
             removal_stage=p.stage, fasteners=p.fast, points={nm: r4(pos) for nm, pos, ax in p.points})
    if p.moving_end is not None: e["moving_end_m"] = r4(p.moving_end)
    if p.orient_axis is not None: e["local_z_axis"] = r4(p.orient_axis)
    if p.spin_axis: e["spin_axis"] = p.spin_axis
    e.update(p.props)
    man.append(e)
json.dump(dict(model="Skoda Octavia I (1U) modular drivetrain kit", units="metres",
               frame="Blender Z-up; +X car right, +Y car front; origin on the ground under the middle of the car",
               engine_placement=dict(scale=0.88, translation_m=[0.16, 1.507, 0.381], note="the engine set as the game places it"),
               key_points=dict(angle_drive_output=r4(OUTF), centre_bearing=r4(CB), final_drive_flange=r4(FD_FLANGE), downpipe_outlet=r4(DP_OUT),
                               twist_beam_pivot_L=r4(TB_PIV)),
               parts=man), open(os.path.join(OUT, "parts_manifest.json"), "w"), indent=1)
with open(os.path.join(OUT, "fasteners.csv"), "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["part", "side", "fastener", "bolt_point", "kind", "size", "tool", "real_tool", "label", "source", "x", "y", "z", "axis_x", "axis_y", "axis_z"])
    for p in PL.PARTS:
        for fd in p.fast:
            w.writerow([p.name, p.side or "shared", fd["name"], fd["bolt_point"], fd["kind"], fd["size"], fd["tool"], fd["real_tool"],
                        fd["label"], fd["source"], *fd["position"], *fd["axis"]])
print("PARTS", len(PL.PARTS), "FASTENERS", len(PL.FASTENERS), "COLLIDERS", sum(p.colliders for p in PL.PARTS))
for p in PL.PARTS: print(f"  {p.name:26s} tris {p.tris:5d}  hulls {p.colliders:3d}  fasteners {len(p.fast):2d}", p.props.get("volume_l", ""))
print("TRIS_TOTAL", sum(p.tris for p in PL.PARTS) + sum(f.tris for f in PL.FASTENERS))
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "Octavia1U_Drivetrain.blend"), relative_remap=True, compress=True)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "Octavia1U_Drivetrain.fbx"), use_selection=False,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE',
                         path_mode='RELATIVE', add_leaf_bones=False, bake_anim=False, colors_type='SRGB')
print("EXPORTED")
