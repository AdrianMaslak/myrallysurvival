"""Procedural PBR maps for the Octavia 1U body and interior (same conventions as the engine and suspension sets).

<name>_basecolor.png (sRGB), _normal.png (OpenGL, +Y up), _roughness.png, _metallic.png, and _emissive.png for lamps.
Tileable; calm normals. 2048 px for paint, glass, interior plastics and fabric, 1024 px for small parts.
usage: python3 textures.py <outdir>
"""
import numpy as np, os, sys, math
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else "textures"
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1997)
TILTS = {}


def noise(n, cycles, aniso=(1.0, 1.0)):
    w = rng.standard_normal((n, n))
    fy = np.fft.fftfreq(n)[:, None] * aniso[1]; fx = np.fft.fftfreq(n)[None, :] * aniso[0]
    f = np.sqrt(fx ** 2 + fy ** 2) * n
    o = np.real(np.fft.ifft2(np.fft.fft2(w) * np.exp(-(f / cycles) ** 2)))
    o -= o.min(); o /= o.max() + 1e-9; return o


def fbm(n, cyc, wts, aniso=(1, 1)):
    h = sum(w * noise(n, c, aniso) for c, w in zip(cyc, wts)); h -= h.min(); return h / (h.max() + 1e-9)


def ss(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)


def lerp(a, b, t):
    a = np.asarray(a, float); b = np.asarray(b, float)
    if np.ndim(t) == 2: t = t[..., None]
    return a + (b - a) * t


def save(name, base, height, strength, rough, metal, emissive=None):
    n_ = base.shape[0] if np.ndim(base) == 3 else height.shape[0]
    to8 = lambda a: (np.clip(a, 0, 1) * 255).astype(np.uint8)
    base = np.broadcast_to(base, (n_, n_, 3)) if np.ndim(base) == 1 else base
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * strength
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * strength
    nrm = np.stack([-dx, dy, np.ones_like(height)], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    TILTS[name] = float(np.degrees(np.arccos(np.clip(nrm[..., 2], -1, 1))).mean())
    Image.fromarray(to8(np.clip(base, 0, 1) ** (1 / 2.2))).save(f"{OUT}/{name}_basecolor.png", optimize=True)
    Image.fromarray(((nrm * 0.5 + 0.5) * 255).astype(np.uint8)).save(f"{OUT}/{name}_normal.png", optimize=True)
    Image.fromarray(to8(np.broadcast_to(rough, (n_, n_)))).save(f"{OUT}/{name}_roughness.png", optimize=True)
    Image.fromarray(to8(np.broadcast_to(metal, (n_, n_)))).save(f"{OUT}/{name}_metallic.png", optimize=True)
    if emissive is not None:
        e = np.broadcast_to(emissive, (n_, n_, 3)) if np.ndim(emissive) == 1 else emissive
        Image.fromarray(to8(e)).save(f"{OUT}/{name}_emissive.png", optimize=True)


N, S = 2048, 1024
one = lambda n: np.ones((n, n))

# ---- body paint: plain colour (the livery goes on top in the game), metallic flake, faint orange peel
peel = fbm(N, [180, 60], [0.6, 0.4])
flake = noise(N, 700)
col = lerp((0.105, 0.150, 0.225), (0.115, 0.162, 0.240), fbm(N, [8], [1]))       # dark blue metallic
save("body_paint", col, peel * 0.35 + flake * 0.15, 1.1, 0.22 + 0.06 * fbm(N, [20], [1]), 0.55 * one(N))
# ---- underseal + road dirt (underbody, wheel arches)
h = fbm(N, [120, 40, 12], [0.4, 0.4, 0.2]); dirt = ss(0.45, 0.85, fbm(N, [10, 4], [0.6, 0.4]))
save("underseal", lerp(lerp((0.035, 0.035, 0.037), (0.06, 0.058, 0.055), h), (0.13, 0.11, 0.085), dirt * 0.55), h, 2.0,
     0.85 - 0.1 * dirt, 0.0 * one(N))
# ---- black textured plastic (grille, trims, mirror bodies, seals' carriers)
g = fbm(N, [600, 200], [0.6, 0.4])
save("plastic_black", lerp((0.022, 0.022, 0.024), (0.032, 0.032, 0.034), fbm(N, [16], [1])), g, 0.38, 0.62 + 0.08 * g, 0.0 * one(N))
# ---- chrome and badges (no brand artwork)
save("chrome", lerp((0.78, 0.79, 0.80), (0.86, 0.87, 0.88), fbm(S, [6], [1])), fbm(S, [300], [1]), 0.3, 0.07 + 0.04 * fbm(S, [30], [1]), one(S))
# ---- painted black steel (bumper bars, wiper arms)
h = fbm(S, [60, 20], [0.5, 0.5])
save("metal_black", lerp((0.03, 0.03, 0.032), (0.05, 0.05, 0.05), h), h, 1.2, 0.48 + 0.15 * h, 0.15 * one(S))
# ---- rubber seals
h = fbm(S, [200, 60], [0.5, 0.5])
save("rubber", lerp((0.018, 0.018, 0.019), (0.028, 0.027, 0.026), h), h, 0.55, 0.82 * one(S), 0.0 * one(S))
# ---- glass: clear, and tinted for the rear
for nm, c in (("glass_clear", (0.55, 0.60, 0.60)), ("glass_tinted", (0.16, 0.19, 0.20))):
    h = fbm(S, [40], [1]) * 0.05
    save(nm, np.broadcast_to(np.array(c), (S, S, 3)).copy(), h, 0.2, 0.04 * one(S), 0.0 * one(S))
# ---- lamp lenses: optic pattern (rows of small domes) in the normal map; clear / amber / red
yy, xx = np.mgrid[0:S, 0:S] / S
cell = 32
u = (xx * cell) % 1 - 0.5; v = (yy * cell) % 1 - 0.5
dome = np.clip(1 - (u ** 2 + v ** 2) * 4, 0, 1) ** 0.5
for nm, c in (("lens_clear", (0.85, 0.87, 0.88)), ("lens_amber", (0.95, 0.45, 0.05)), ("lens_red", (0.65, 0.03, 0.03))):
    save(nm, np.broadcast_to(np.array(c), (S, S, 3)).copy(), dome, 0.5, 0.05 * one(S), 0.0 * one(S))
# ---- reflector (chrome-like, faceted) for the lamp housings
fac = ((xx * 24) % 1 > 0.5).astype(float) * 0.3 + ((yy * 24) % 1 > 0.5) * 0.3
save("reflector", np.broadcast_to(np.array((0.85, 0.86, 0.88)), (S, S, 3)).copy(), fac, 0.6, 0.12 * one(S), one(S))
# ---- light-emitting slots: the bulb/lens area that the game lights on its own (emissive = the lit colour)
for nm, c in (("lamp_lowbeam", (1.0, 0.97, 0.90)), ("lamp_highbeam", (1.0, 1.0, 1.0)), ("lamp_indicator", (1.0, 0.50, 0.05)),
              ("lamp_fog", (1.0, 0.96, 0.85)), ("lamp_tail", (0.75, 0.02, 0.02)), ("lamp_brake", (1.0, 0.04, 0.04)),
              ("lamp_reverse", (1.0, 1.0, 1.0)), ("lamp_plate", (1.0, 0.97, 0.90))):
    base = np.array(c) * 0.55 + 0.05
    S2 = 256
    yy2, xx2 = np.mgrid[0:S2, 0:S2] / S2
    d2 = np.clip(1 - (((xx2 * 8) % 1 - 0.5) ** 2 + ((yy2 * 8) % 1 - 0.5) ** 2) * 4, 0, 1) ** 0.5
    save(nm, np.broadcast_to(base, (S2, S2, 3)).copy(), d2, 0.5, 0.08 * one(S2), 0.0 * one(S2),
         emissive=np.broadcast_to(np.array(c), (S2, S2, 3)).copy())
# ---- number plate (generic, fictional registration)
P = 1024
img = Image.new("RGB", (P, P), (235, 235, 228)); dr = ImageDraw.Draw(img)
try: font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 150)
except Exception: font = ImageFont.load_default()
dr.rectangle((0, 0, P - 1, P - 1), outline=(20, 20, 20), width=14)
dr.rectangle((14, 14, 120, P - 15), fill=(20, 50, 140))
dr.text((170, P // 2 - 90), "OC 1U 98", fill=(15, 15, 15), font=font)
pa = np.asarray(img, float) / 255.0
hgt = 1 - pa.mean(-1)
save("plate", pa ** 2.2, hgt, 4.0, 0.35 * one(P), 0.0 * one(P))

# ---- interior: dark grey textured plastic, seat fabric, carpet, headliner cloth
g = fbm(N, [500, 160], [0.6, 0.4])
save("interior_plastic", lerp((0.045, 0.046, 0.05), (0.06, 0.061, 0.066), fbm(N, [12], [1])), g, 0.45, 0.70 + 0.08 * g, 0.0 * one(N))
yy, xx = np.mgrid[0:N, 0:N] / N
weave = (np.sin(xx * 2 * math.pi * 260) * np.sin(yy * 2 * math.pi * 260)) * 0.5 + 0.5
fibre = fbm(N, [700], [1])
save("seat_fabric", lerp((0.055, 0.058, 0.07), (0.085, 0.09, 0.11), fbm(N, [10], [1]) * 0.5 + weave * 0.3 + fibre * 0.2),
     weave * 0.6 + fibre * 0.4, 0.22, 0.92 * one(N), 0.0 * one(N))
pile = fbm(N, [900, 300], [0.6, 0.4])
save("carpet", lerp((0.035, 0.035, 0.038), (0.06, 0.058, 0.06), pile), pile, 0.45, 0.95 * one(N), 0.0 * one(N))
cloth = fbm(N, [500, 120], [0.5, 0.5])
save("headliner_cloth", lerp((0.42, 0.41, 0.39), (0.48, 0.47, 0.45), cloth), cloth, 0.47, 0.9 * one(N), 0.0 * one(N))
print("textures written to", OUT)
for k, v in TILTS.items(): print(f"  mean normal tilt {k:18s} {v:4.1f} deg")
