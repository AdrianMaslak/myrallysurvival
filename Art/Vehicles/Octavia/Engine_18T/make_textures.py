"""Procedural tileable PBR textures for the Octavia 1U 1.8T 20V engine set (v2).

v2: surface relief is softer and coarser; the variation lives in colour and roughness
(rust patches, heat colouring, oil, grime, faded plastic) instead of fine bump noise.
One tile covers 0.25 m on the model, so a noise cutoff of 8 cycles/tile gives ~3 cm patches.

Each material gets <name>_basecolor.png (sRGB), _normal.png (OpenGL +Y), _roughness.png, _metallic.png.
"""
import numpy as np
from PIL import Image
import os, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "textures"
N = 1024
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1996)

def noise(cycles, aniso=(1.0, 1.0)):
    """Periodic band-limited noise: features down to ~1/cycles of the tile. 0..1."""
    w = rng.standard_normal((N, N))
    fy = np.fft.fftfreq(N)[:, None] * aniso[1]
    fx = np.fft.fftfreq(N)[None, :] * aniso[0]
    f = np.sqrt(fx**2 + fy**2) * N
    out = np.real(np.fft.ifft2(np.fft.fft2(w) * np.exp(-(f / cycles) ** 2)))
    out -= out.min(); out /= out.max() + 1e-9
    return out

def fbm(cyc, wts, aniso=(1, 1)):
    h = sum(w * noise(c, aniso) for c, w in zip(cyc, wts))
    h -= h.min(); return h / h.max()

def ss(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def lerp(a, b, t):
    a = np.asarray(a, float); b = np.asarray(b, float)
    if np.ndim(t) == 2: t = t[..., None]
    return a + (b - a) * t

def normal_from_height(h, strength):
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    n = np.stack([-dx, dy, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return ((n * 0.5 + 0.5) * 255).astype(np.uint8)

def save(name, base, height, nstrength, rough, metal):
    to8 = lambda a: (np.clip(a, 0, 1) * 255).astype(np.uint8)
    base = np.broadcast_to(base, (N, N, 3)) if np.ndim(base) == 1 else base
    Image.fromarray(to8(np.clip(base, 0, 1) ** (1 / 2.2))).save(f"{OUT}/{name}_basecolor.png", optimize=True)
    Image.fromarray(normal_from_height(height, nstrength)).save(f"{OUT}/{name}_normal.png", optimize=True)
    Image.fromarray(to8(np.broadcast_to(rough, (N, N)))).save(f"{OUT}/{name}_roughness.png", optimize=True)
    Image.fromarray(to8(np.broadcast_to(metal, (N, N)))).save(f"{OUT}/{name}_metallic.png", optimize=True)

ones = np.ones((N, N))

# ---------------------------------------------------------------- cast iron (block, exhaust-side bosses)
h = fbm([40, 14], [0.55, 0.45])
tone = fbm([24, 9], [0.6, 0.4])
rust = ss(0.66, 0.90, fbm([28, 11], [0.6, 0.4]))
oil = ss(0.66, 0.90, fbm([22, 9], [0.6, 0.4]))
grime = ss(0.35, 0.05, h) * 0.6                     # low spots collect dirt
c = lerp((0.105, 0.108, 0.112), (0.135, 0.135, 0.136), tone)
c = lerp(c, (0.14, 0.085, 0.055), rust * 0.35)
c = lerp(c, (0.06, 0.055, 0.05), np.maximum(oil * 0.45, grime * 0.5))
save("cast_iron", c, h + rust * 0.25, 1.2,
     0.72 - 0.30 * oil + 0.18 * rust + 0.08 * grime, 0.55 - 0.40 * rust - 0.15 * grime)

# ---------------------------------------------------------------- sand-cast aluminium (head, gearbox, brackets)
h = fbm([50, 16], [0.5, 0.5])
tone = fbm([26, 10], [0.5, 0.5])
oxide = ss(0.62, 0.90, fbm([26, 10], [0.6, 0.4]))
stain = ss(0.68, 0.92, fbm([30, 12], [0.6, 0.4]))
grime = ss(0.3, 0.05, h) * 0.5
c = lerp((0.235, 0.235, 0.23), (0.275, 0.275, 0.268), tone)
c = lerp(c, (0.33, 0.33, 0.31), oxide * 0.2)                       # faint oxidation bloom
c = lerp(c, (0.15, 0.14, 0.12), np.maximum(stain * 0.25, grime * 0.4))  # small oil spots; joint dirt comes from the Grime layer
save("cast_alu", c, h, 1.0, 0.55 + 0.25 * oxide - 0.25 * stain + 0.1 * grime, 0.85 - 0.4 * oxide - 0.2 * grime)

# ---------------------------------------------------------------- machined aluminium (pulleys, throttle, ECU)
br = fbm([300, 60], [0.5, 0.5], aniso=(1.0, 0.03))
tone = fbm([6], [1])
dirt = ss(0.70, 0.92, fbm([24, 10], [0.6, 0.4]))
c = lerp(lerp((0.52, 0.52, 0.53), (0.66, 0.66, 0.67), br * 0.7 + tone * 0.3), (0.2, 0.19, 0.17), dirt * 0.4)
save("alu_machined", c, br, 0.6, 0.25 + 0.15 * br + 0.35 * dirt, 1.0 - 0.5 * dirt)

# ---------------------------------------------------------------- black plastic (covers, manifold-side plastics, airbox, tank)
h = fbm([90, 30], [0.5, 0.5])
fade = ss(0.5, 0.95, fbm([22, 9], [0.6, 0.4]))
dust = ss(0.68, 0.92, fbm([36, 14], [0.6, 0.4]))
c = lerp((0.026, 0.027, 0.029), (0.036, 0.036, 0.038), fade)       # very slight fading
c = lerp(c, (0.07, 0.065, 0.058), dust * 0.25)
save("plastic_black", c, h, 0.6, 0.45 + 0.25 * fade + 0.2 * dust, 0.0 * ones)

# ---------------------------------------------------------------- rubber (hoses, belt, mounts)
h = fbm([80, 25], [0.5, 0.5])
dust = ss(0.60, 0.92, fbm([24, 9], [0.6, 0.4]))
oil = ss(0.72, 0.90, fbm([20, 8], [0.6, 0.4]))
c = lerp((0.018, 0.018, 0.019), (0.034, 0.033, 0.031), dust)
save("rubber", c, h, 0.5, 0.85 + 0.1 * dust - 0.4 * oil, 0.0 * ones)

# ---------------------------------------------------------------- zinc-plated fasteners
h = fbm([60, 20], [0.5, 0.5])
corr = ss(0.6, 0.85, fbm([6, 3], [0.6, 0.4]))
c = lerp(lerp((0.48, 0.49, 0.50), (0.64, 0.65, 0.66), fbm([12], [1])), (0.55, 0.52, 0.40), corr * 0.6)
c = lerp(c, (0.22, 0.10, 0.04), ss(0.75, 0.9, fbm([4], [1])) * 0.7)
save("zinc", c, h, 0.5, 0.32 + 0.35 * corr, 1.0 - 0.3 * corr)

# ---------------------------------------------------------------- painted black steel (sump, brackets, straps, servo)
h = fbm([60, 20], [0.5, 0.5])
chip = ss(0.80, 0.84, fbm([30, 10], [0.5, 0.5]))
rust = ss(0.72, 0.90, fbm([20, 8], [0.6, 0.4])) * (0.4 + 0.6 * chip)
dirt = ss(0.62, 0.92, fbm([22, 9], [0.6, 0.4]))
c = lerp((0.025, 0.025, 0.027), (0.05, 0.05, 0.05), fbm([10], [1]))
c = lerp(c, (0.26, 0.26, 0.26), chip * 0.5)
c = lerp(c, (0.16, 0.08, 0.04), rust * 0.7)
c = lerp(c, (0.07, 0.065, 0.055), dirt * 0.3)
save("painted_black", c, h - chip * 0.3, 0.8, 0.45 + 0.35 * rust + 0.15 * dirt - 0.1 * chip, 0.05 + 0.75 * chip * (1 - rust))

# ---------------------------------------------------------------- exhaust steel / cast manifold: heat colours + rust + soot
h = fbm([40, 12], [0.55, 0.45])
heat = fbm([3, 1.5], [0.6, 0.4])
ramp_pos = np.array([0.0, 0.25, 0.45, 0.62, 0.8, 1.0])
ramp_col = np.array([(0.26, 0.25, 0.24), (0.42, 0.33, 0.16), (0.36, 0.19, 0.08),
                     (0.24, 0.11, 0.17), (0.10, 0.13, 0.24), (0.12, 0.12, 0.12)])
hc = np.stack([np.interp(heat, ramp_pos, ramp_col[:, i]) for i in range(3)], -1)
hc = lerp(hc * 0.7, (0.14, 0.13, 0.12), 0.55)
rust = ss(0.50, 0.85, fbm([20, 8], [0.6, 0.4]))
soot = ss(0.66, 0.90, fbm([18, 7], [0.6, 0.4]))
c = lerp(hc, (0.15, 0.08, 0.04), rust * 0.5)
c = lerp(c, (0.05, 0.045, 0.04), soot * 0.45)
save("exhaust", c, h + rust * 0.35, 2.5, 0.50 + 0.40 * rust + 0.1 * soot, 0.75 - 0.6 * rust - 0.3 * soot)

# ---------------------------------------------------------------- turbine housing cast iron: heat-darkened with orange rust bloom
h = fbm([36, 12], [0.55, 0.45])
heat = fbm([4, 2], [0.6, 0.4])
rust = ss(0.55, 0.88, fbm([20, 8], [0.6, 0.4]))
c = lerp((0.075, 0.068, 0.062), (0.12, 0.10, 0.085), heat)
c = lerp(c, (0.20, 0.10, 0.05), rust * 0.5)
save("turbine_iron", c, h + rust * 0.3, 3.0, 0.70 + 0.25 * rust, 0.45 - 0.35 * rust)

# ---------------------------------------------------------------- radiator / intercooler core: fins in colour, soft relief
y, x = np.mgrid[0:N, 0:N]
fin = 0.5 + 0.5 * np.sin(x / N * 2 * np.pi * 64)
tube = ss(0.6, 0.8, 0.5 + 0.5 * np.sin(y / N * 2 * np.pi * 32))
dirt = ss(0.64, 0.92, fbm([30, 12], [0.6, 0.4]))
bent = ss(0.86, 0.94, fbm([30, 12], [0.6, 0.4]))         # crushed fin spots
c = lerp(lerp((0.16, 0.16, 0.17), (0.40, 0.40, 0.41), fin * 0.6 + 0.2), (0.48, 0.48, 0.50), tube)
c = lerp(c, (0.12, 0.115, 0.10), dirt * 0.25)
c = lerp(c, (0.12, 0.12, 0.12), bent * 0.6)
save("rad_core", c, fin * (1 - tube) * 0.5 + tube * 0.8 - bent * 0.5, 1.5, 0.45 + 0.35 * dirt, 0.85 - 0.5 * dirt)

# ---------------------------------------------------------------- translucent-look white plastic (reservoirs): yellowed, streaked
h = fbm([60, 20], [0.5, 0.5])
yel = fbm([8, 3], [0.5, 0.5])
streak = ss(0.6, 0.85, fbm([40, 8], [0.6, 0.4], aniso=(1.0, 0.15)))
c = lerp((0.68, 0.67, 0.62), (0.64, 0.60, 0.48), yel * 0.6)
c = lerp(c, (0.40, 0.36, 0.28), streak * 0.3)
save("plastic_white", c, h, 0.5, 0.40 + 0.2 * streak, 0.0 * ones)

# ---------------------------------------------------------------- coloured caps / handles / terminal cover / injector bodies
for name, col in {"cap_blue": (0.03, 0.09, 0.25), "cap_yellow": (0.55, 0.38, 0.02), "red": (0.35, 0.02, 0.02), "green": (0.04, 0.16, 0.05)}.items():
    h = fbm([60, 20], [0.5, 0.5])
    dirt = ss(0.6, 0.85, fbm([6, 2], [0.6, 0.4]))
    c = lerp(lerp(tuple(v * 0.8 for v in col), col, fbm([8], [1])), (0.08, 0.07, 0.05), dirt * 0.5)
    save(name, c, h, 0.5, 0.40 + 0.3 * dirt, 0.0 * ones)

print("textures written to", OUT)
