"""Tileable PBR textures for the Octavia 1U suspension/brake set (engine v3 rules).

Calm normals (mean tilt 2-4 deg), variation in colour and roughness, small faint stains.
One tile = 0.25 m on the model (0.05 m on fasteners). Writes <name>_basecolor/_normal/_roughness/_metallic.png
and prints the mean normal tilt of every map.
"""
import numpy as np
from PIL import Image
import os, sys, math

OUT = sys.argv[1] if len(sys.argv) > 1 else "textures"
N = 1024
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1996)

def noise(cycles, aniso=(1.0, 1.0), size=None):
    M = size or N
    w = rng.standard_normal((M, M))
    fy = np.fft.fftfreq(M)[:, None] * aniso[1]
    fx = np.fft.fftfreq(M)[None, :] * aniso[0]
    f = np.sqrt(fx**2 + fy**2) * M
    out = np.real(np.fft.ifft2(np.fft.fft2(w) * np.exp(-(f / cycles) ** 2)))
    out -= out.min(); out /= out.max() + 1e-9
    return out

def fbm(cyc, wts, aniso=(1, 1)):
    h = sum(w * noise(c, aniso) for c, w in zip(cyc, wts)); h -= h.min(); return h / h.max()

def ss(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def lerp(a, b, t):
    a = np.asarray(a, float); b = np.asarray(b, float)
    if np.ndim(t) == 2: t = t[..., None]
    return a + (b - a) * t

TILTS = {}
def normal_from_height(h, strength):
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    n = np.stack([-dx, dy, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n

def save(name, base, height, nstrength, rough, metal):
    to8 = lambda a: (np.clip(a, 0, 1) * 255).astype(np.uint8)
    base = np.broadcast_to(base, (N, N, 3)) if np.ndim(base) == 1 else base
    n = normal_from_height(height, nstrength)
    TILTS[name] = float(np.degrees(np.arccos(np.clip(n[..., 2], -1, 1))).mean())
    Image.fromarray(to8(np.clip(base, 0, 1) ** (1 / 2.2))).save(f"{OUT}/{name}_basecolor.png", optimize=True)
    Image.fromarray(((n * 0.5 + 0.5) * 255).astype(np.uint8)).save(f"{OUT}/{name}_normal.png", optimize=True)
    Image.fromarray(to8(np.broadcast_to(rough, (N, N)))).save(f"{OUT}/{name}_roughness.png", optimize=True)
    Image.fromarray(to8(np.broadcast_to(metal, (N, N)))).save(f"{OUT}/{name}_metallic.png", optimize=True)

ones = np.ones((N, N))

# black painted pressed steel: arms, links, subframes, anti-roll bars, springs
h = fbm([60, 20], [0.5, 0.5])
chip = ss(0.80, 0.84, fbm([30, 10], [0.5, 0.5]))
rust = ss(0.72, 0.90, fbm([20, 8], [0.6, 0.4])) * (0.4 + 0.6 * chip)
dirt = ss(0.62, 0.92, fbm([22, 9], [0.6, 0.4]))
c = lerp((0.025, 0.025, 0.027), (0.045, 0.045, 0.046), fbm([24], [1]))
c = lerp(c, (0.26, 0.26, 0.26), chip * 0.5)
c = lerp(c, (0.16, 0.08, 0.04), rust * 0.7)
c = lerp(c, (0.07, 0.065, 0.055), dirt * 0.3)
save("painted_black", c, h - chip * 0.3, 1.5, 0.42 + 0.35 * rust + 0.15 * dirt - 0.1 * chip, 0.05 + 0.75 * chip * (1 - rust))

# rubber: bushings, boots, brake hoses
h = fbm([80, 25], [0.5, 0.5])
dust = ss(0.60, 0.92, fbm([24, 9], [0.6, 0.4]))
c = lerp((0.018, 0.018, 0.019), (0.036, 0.034, 0.031), dust)
save("rubber", c, h, 1.1, 0.82 + 0.12 * dust, 0.0 * ones)

# tyre: 2048 px, its own layout. U = half the circumference (2 repeats round the tyre), V = 0..1 along the cross-section
# (wheel_profile.TYRE_PROFILE). Tread blocks, lateral grooves, sipes and shoulder blocks, sidewall rib and size lettering
# are relief in the normal map; the four main circumferential grooves are in the mesh.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wheel_profile as WP
_rng_state = rng.bit_generator.state          # the tyre uses its own draws; later materials stay as in v1
from PIL import ImageDraw, ImageFont, ImageFilter
NT = 2048
prof = WP.TYRE_PROFILE; S = WP.arclen(prof); ST = S[-1]
vrow = (np.arange(NT)[::-1] + 0.5) / NT                       # image row 0 = top = V 1
pts = prof + [prof[0]]
R_ = np.interp(vrow * ST, S, [p[0] for p in pts]); A_ = np.interp(vrow * ST, S, [p[1] for p in pts])
seg_idx = np.clip(np.searchsorted(S, vrow * ST) - 1, 0, len(prof) - 1)
CIRC = 2 * math.pi * WP.TYRE_R / WP.TYRE_U_REPS              # metres of tread per U repeat
NP = 33; P = CIRC / NP                                        # tread pitches per repeat (whole number: tiles)
c = (np.arange(NT) + 0.5) / NT * CIRC                         # tread-surface distance along U
C, Aa = np.meshgrid(c, A_); Rr = np.meshgrid(c, R_)[1]
absA = np.abs(Aa)
px = CIRC / NT                                                # tread metres per pixel (U)
def band(dist, half):                                         # 1 inside a groove of half-width `half`, soft 1.2 px edge
    return np.clip((half - np.abs(dist)) / (1.2 * px) + 0.5, 0, 1)
def per(x, p): return (x + p / 2) % p - p / 2                 # signed distance to the nearest multiple of p
on_top = Rr > 0.3150                                          # tread top surfaces (not the groove walls)
groove_wall = np.zeros_like(Rr, bool)
for g in WP.GROOVES: groove_wall |= np.abs(Aa - g) < WP.GROOVE_W / 2
h = np.ones((NT, NT))
sg = np.sign(Aa)
# centre rib: zig-zag sipes every half pitch
cen = on_top & (absA < 0.013)
h -= 0.55 * band(per(C - 0.35 * Aa, P / 2), 0.0004) * cen
# middle ribs: angled lateral grooves every pitch, one sipe in each block
mid = on_top & (absA > 0.023) & (absA < 0.044)
h -= 1.0 * band(per(C - sg * 0.45 * (absA - 0.023), P), 0.0026) * mid
h -= 0.55 * band(per(C - sg * 0.45 * (absA - 0.023) - P / 2, P), 0.0004) * mid
# shoulder ribs and shoulder blocks: lateral slots offset half a pitch, running over the shoulder and fading out
sh = (absA > 0.054) & (Rr > 0.2990) & ~groove_wall
fade = np.clip((Rr - 0.2990) / 0.010, 0, 1)
h -= 1.0 * band(per(C - sg * 0.15 * (absA - 0.054) - P / 2, P), 0.0031) * sh * fade
h -= 0.55 * band(per(C - sg * 0.15 * (absA - 0.054), P), 0.0004) * (sh & on_top & (absA < 0.064))
# sidewall: protector rib above the rim, a fine ring, then the lettering
side = Rr < 0.2990
h += 0.35 * np.clip(1 - np.abs(Rr - 0.2160) / 0.0035, 0, 1) * side
h += 0.20 * np.clip(1 - np.abs(Rr - 0.2900) / 0.0012, 0, 1) * side
h += 0.03 * (noise(600, size=NT) - 0.5) * side
# lettering: drawn at 0.25 mm/px along the sidewall circumference at r = 0.262, then resampled to the texture grid
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 66)
    font_s = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 38)
except Exception:
    font = font_s = ImageFont.load_default()
for sideSign, rot in ((-1, False), (1, True)):                 # outboard (a < 0) reads from outside; inboard turned 180 deg
    rows = np.where((np.sign(A_) == sideSign) & (R_ > 0.2300) & (R_ < 0.2750))[0]
    if len(rows) == 0: continue
    r_mid = 0.2525; circ_s = 2 * math.pi * r_mid / WP.TYRE_U_REPS
    W_px = int(circ_s / 0.00025); H_px = int(0.045 / 0.00025)
    img = Image.new("L", (W_px, H_px), 0); dr = ImageDraw.Draw(img)
    dr.text((int(W_px * 0.08), int(H_px * 0.18)), "195/65 R15 91T", fill=255, font=font)
    dr.text((int(W_px * 0.46), int(H_px * 0.30)), "TUBELESS     RADIAL     M+S", fill=255, font=font_s)
    dr.text((int(W_px * 0.80), int(H_px * 0.30)), "DOT  2416", fill=255, font=font_s)
    if rot: img = img.rotate(180)
    img = img.filter(ImageFilter.GaussianBlur(1.2))
    # each texture row in the band: arc position along the sidewall -> row in the lettering image (top = larger r on outboard)
    sr = vrow[rows] * ST
    s0, s1 = sr.min(), sr.max()
    lt = np.asarray(img.resize((NT, len(rows)), Image.BILINEAR), float) / 255.0
    order = np.argsort(-vrow[rows]); h[rows[order]] += 0.45 * lt
base = lerp((0.020, 0.020, 0.021), (0.030, 0.029, 0.028), noise(120, size=NT))
scuff = on_top.astype(float) * (0.4 + 0.6 * noise(400, size=NT))
base = lerp(base, (0.040, 0.039, 0.037), scuff * 0.5)
cav = np.clip(h, 0, 1)                                         # grooves and sipes: darker and duller (dust, no wear)
base = base * (0.45 + 0.55 * cav)[..., None]
rough = 0.80 - 0.10 * scuff + 0.05 * (h > 1.1) + 0.12 * (1 - cav)
# normal from height in physical units (U and V texels differ in size), relief scaled down to stay calm
pv = ST / NT
dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) / (2 * px); dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) / (2 * pv)
k = 0.0030                                                   # height of 1.0 in metres for the normal map
n = np.stack([-dx * k, dy * k, np.ones_like(h)], -1); n /= np.linalg.norm(n, axis=-1, keepdims=True)
TILTS["tyre"] = float(np.degrees(np.arccos(np.clip(n[..., 2], -1, 1))).mean())
to8 = lambda a_: (np.clip(a_, 0, 1) * 255).astype(np.uint8)
Image.fromarray(to8(base ** (1 / 2.2))).save(f"{OUT}/tyre_basecolor.png", optimize=True)
Image.fromarray(((n * 0.5 + 0.5) * 255).astype(np.uint8)).save(f"{OUT}/tyre_normal.png", optimize=True)
Image.fromarray(to8(np.broadcast_to(rough, (NT, NT)))).save(f"{OUT}/tyre_roughness.png", optimize=True)
Image.fromarray(to8(np.zeros((NT, NT)))).save(f"{OUT}/tyre_metallic.png", optimize=True)
rng.bit_generator.state = _rng_state
for _ in range(4): rng.standard_normal((N, N))  # same draws as the v1 tyre, so every other texture is unchanged

# zinc fasteners and dampers
h = fbm([60, 20], [0.5, 0.5])
corr = ss(0.62, 0.88, fbm([16, 6], [0.6, 0.4]))
c = lerp(lerp((0.30, 0.31, 0.32), (0.38, 0.39, 0.40), fbm([18], [1])), (0.36, 0.34, 0.27), corr * 0.4)
c = lerp(c, (0.22, 0.12, 0.06), ss(0.80, 0.94, fbm([14, 6], [0.6, 0.4])) * 0.6)
save("zinc", c, h, 1.4, 0.30 + 0.35 * corr, 1.0 - 0.3 * corr)

# cast iron: knuckles, hub carriers, caliper carriers, disc hats and edges (light surface rust)
h = fbm([40, 14], [0.55, 0.45])
tone = fbm([24, 9], [0.6, 0.4])
rust = ss(0.58, 0.88, fbm([24, 10], [0.6, 0.4]))
c = lerp((0.10, 0.10, 0.10), (0.13, 0.125, 0.12), tone)
c = lerp(c, (0.17, 0.09, 0.05), rust * 0.45)
save("cast_iron", c, h + rust * 0.25, 1.8, 0.72 + 0.18 * rust, 0.55 - 0.40 * rust)

# disc friction faces: machined, concentric lathe marks (texture streaks along U; discs are UV'd polar so U runs round)
lathe = fbm([400, 120], [0.6, 0.4], aniso=(0.02, 1.0))
band = fbm([40, 12], [0.6, 0.4], aniso=(0.05, 1.0))
blue = ss(0.75, 0.95, fbm([10, 4], [0.6, 0.4], aniso=(0.2, 1.0)))
c = lerp((0.30, 0.30, 0.31), (0.45, 0.45, 0.46), lathe * 0.5 + band * 0.5)
c = lerp(c, (0.24, 0.24, 0.30), blue * 0.35)
save("disc_face", c, lathe * 0.5, 0.6, 0.30 + 0.18 * band, 0.95 * ones)

# silver painted steel rims
h = fbm([90, 30], [0.5, 0.5])
dust = ss(0.62, 0.94, fbm([30, 12], [0.6, 0.4]))       # brake dust film
chip = ss(0.86, 0.90, fbm([40, 14], [0.5, 0.5]))
c = lerp((0.50, 0.51, 0.52), (0.56, 0.57, 0.58), fbm([20], [1]))
c = lerp(c, (0.30, 0.28, 0.25), dust * 0.3)
c = lerp(c, (0.18, 0.09, 0.05), chip * 0.6)
save("rim_silver", c, h - chip * 0.3, 0.9, 0.40 + 0.25 * dust + 0.2 * chip, 0.55 - 0.3 * dust)

# caliper: plain grey cast, brake-dust dark at the edges via the Grime layer
h = fbm([50, 16], [0.5, 0.5])
stain = ss(0.66, 0.92, fbm([26, 10], [0.6, 0.4]))
c = lerp((0.20, 0.20, 0.20), (0.24, 0.24, 0.235), fbm([20, 8], [0.5, 0.5]))
c = lerp(c, (0.14, 0.11, 0.09), stain * 0.35)
save("caliper_grey", c, h, 1.6, 0.60 + 0.15 * stain, 0.5 * ones)

# brake pad friction material
h = fbm([200, 60], [0.5, 0.5])
c = lerp((0.06, 0.055, 0.05), (0.10, 0.09, 0.08), fbm([60, 20], [0.5, 0.5]))
save("pad_friction", c, h, 0.6, 0.85 * ones, 0.1 * ones)

print("textures written to", OUT)
for k, v in TILTS.items(): print(f"  mean normal tilt {k:14s} {v:4.1f} deg")
