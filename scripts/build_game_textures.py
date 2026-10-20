"""The game's texture set for the v2 body and interior, the 1.8T engine and the v2 suspension: the delivered maps
retuned for the game's worn 1996 look. The delivered textures/ folders are left as they are; this writes
textures_game/ next to each, and the import scripts take a map from textures_game/ when it is there.
Run: python scripts/build_game_textures.py   (plain Python 3 with numpy and Pillow; not Blender)

Why: the delivered body was showroom-new (paint roughness 0.25 and metallic 0.55, chrome 0.09, lenses 0.05). With no
Lumen and no reflection captures, Unreal lights those from the unoccluded sky, so the whole car, the cabin
included, read as wet plastic, and every ripple of the panels showed in the highlights.
- Roughness: per surface, a base value with a soft, tiling variation (dust, polish wear, sun fade) so no panel is
  a perfect mirror. Metallic: solid 90s paint is not metallic (0); chrome and bare metal keep theirs.
- Normal maps: the paint's orange peel is halved; fabric and carpet keep theirs.
- Interior colours: anthracite plastics with a moulded grain, a 90s patterned seat cloth (charcoal with a woven
  check and coloured flecks), charcoal carpet, a warm light-grey headliner. The delivered ones were flat greys.
- Gauges: the speedometer's 100 and 120 overlapped; its numbers are redrawn smaller on the same dial. Both the base
  colour and the emissive map change the same way.
Everything is seeded, so a rerun writes the same files.
"""
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONTS = ["C:/Windows/Fonts/arialbd.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
         "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"]


def load(path, mode):
    return np.asarray(Image.open(path).convert(mode), dtype=np.float32) / 255.0


def save(arr, path, mode):
    path.parent.mkdir(parents=True, exist_ok=True)
    a = np.clip(arr * 255.0 + 0.5, 0, 255).astype(np.uint8)
    Image.fromarray(a, mode).save(path, optimize=True)


def noise(size, feature_px, seed, octaves=3):
    """Tiling noise in -1..1: white noise low-passed in frequency space (periodic by construction)."""
    rng = np.random.default_rng(seed)
    out = np.zeros((size, size), np.float32)
    f = np.fft.fftfreq(size)
    r = np.sqrt(f[None, :] ** 2 + f[:, None] ** 2)
    amp = 1.0
    for o in range(octaves):
        sigma = 1.0 / (feature_px / (2 ** o))
        field = np.fft.fft2(rng.standard_normal((size, size)))
        layer = np.real(np.fft.ifft2(field * np.exp(-(r / sigma) ** 2)))
        layer /= np.abs(layer).max() + 1e-9
        out += amp * layer
        amp *= 0.5
    return out / np.abs(out).max()


def grain(size, seed, strength):
    """Fine moulded or fibre grain, -strength..strength."""
    return noise(size, 3, seed, octaves=1) * strength


def srgb_to_lin(c):
    c = np.asarray(c, np.float32)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


# Surface: roughness (base, variation, feature size in px), metallic (None keeps the delivered map), normal strength.
BODY = {
    "body_paint": ((0.42, 0.1, 300), 0.0, 0.5),
    "chrome": ((0.28, 0.08, 120), 1.0, 1.0),
    "lens_clear": ((0.14, 0.05, 80), 0.0, 1.0),
    "lens_amber": ((0.16, 0.05, 80), 0.0, 1.0),
    "lens_red": ((0.16, 0.05, 80), 0.0, 1.0),
    "reflector": ((0.32, 0.06, 60), 1.0, 1.0),
    "plate": ((0.55, 0.08, 120), 0.0, 1.0),
    "metal_black": ((0.68, 0.08, 120), 0.2, 1.0),
    "plastic_black": ((0.78, 0.06, 200), 0.0, 1.0),
    "interior_plastic": ((0.82, 0.05, 200), 0.0, 1.0),
    "rubber": ((0.9, 0.04, 120), 0.0, 1.0),
    "underseal": ((0.9, 0.05, 160), 0.0, 1.0),
    "seat_fabric": ((0.97, 0.02, 120), 0.0, 1.0),
    "carpet": ((0.98, 0.02, 120), 0.0, 1.0),
    "headliner_cloth": ((0.95, 0.03, 120), 0.0, 1.0),
    "gauge_faces": ((0.75, 0.0, 64), 0.0, 1.0),
    "gauge_warning": ((0.75, 0.0, 64), 0.0, 1.0),
}
for lamp in ("brake", "fog", "highbeam", "indicator", "lowbeam", "plate", "reverse", "tail"):
    BODY["lamp_" + lamp] = ((0.25, 0.05, 60), 0.0, 1.0)
# The engine and running gear were closer to right; only their shiniest metals and plastics move.
ENGINE = {
    "alu_machined": ((0.45, 0.08, 120), None, 1.0),
    "zinc": ((0.5, 0.1, 120), None, 1.0),
    "plastic_black": ((0.62, 0.06, 160), None, 1.0),
    "plastic_white": ((0.58, 0.06, 160), None, 1.0),
    "painted_black": ((0.6, 0.08, 160), None, 1.0),
    "cap_blue": ((0.55, 0.05, 80), None, 1.0),
    "cap_yellow": ((0.55, 0.05, 80), None, 1.0),
    "green": ((0.55, 0.05, 80), None, 1.0),
    "red": ((0.55, 0.05, 80), None, 1.0),
}
SUSPENSION = {
    "zinc": ((0.5, 0.1, 120), None, 1.0),
    "disc_face": ((0.5, 0.1, 80), None, 1.0),
    "painted_black": ((0.62, 0.08, 160), None, 1.0),
    "rim_silver": ((0.5, 0.08, 120), None, 1.0),
}
SETS = [("Art/Vehicles/Octavia/Body_v2", BODY), ("Art/Vehicles/Octavia/Engine_18T", ENGINE),
        ("Art/Vehicles/Octavia/Suspension_v2", SUSPENSION)]


def retune(src, dst, surface, spec, seed):
    (base, var, feature), metallic, normal = spec
    rough_path = src / (surface + "_roughness.png")
    if not rough_path.exists():
        return
    size = Image.open(rough_path).size[0]
    rough = base + var * noise(size, feature * size / 2048, seed)
    save(np.clip(rough, 0.02, 1.0), dst / rough_path.name, "L")
    if metallic is not None:
        save(np.full((size, size), metallic, np.float32), dst / (surface + "_metallic.png"), "L")
    if normal != 1.0:
        n = load(src / (surface + "_normal.png"), "RGB") * 2.0 - 1.0
        n[..., :2] *= normal
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        save(n * 0.5 + 0.5, dst / (surface + "_normal.png"), "RGB")


def colour_field(size, colour, seed, fade, grain_strength, feature=500):
    """A flat sRGB colour with a soft fade and a fine grain, returned in sRGB."""
    lin = srgb_to_lin(colour)[None, None, :]
    shade = 1.0 + fade * noise(size, feature * size / 2048, seed)[..., None] + grain(size, seed + 1, grain_strength)[..., None]
    return lin_to_srgb(lin * shade)


def seat_cloth(size, seed):
    """Charcoal-blue 90s seat cloth: a fine woven check, a thin stripe every 2.5 cm and scattered coloured flecks.
    UV0 runs about 2 per metre on the seats, so 2048 px is about 50 cm."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    px_per_cm = size / 50.0
    weave = 0.5 + 0.5 * np.sin(x * 2 * np.pi / (0.25 * px_per_cm)) * np.sin(y * 2 * np.pi / (0.25 * px_per_cm))
    base = srgb_to_lin((0.15, 0.16, 0.2))[None, None, :] * (0.85 + 0.25 * weave[..., None])
    # Thin lighter stripes running one way, as the cloth's weave pattern.
    stripe = (np.mod(y, 2.5 * px_per_cm) < 0.12 * px_per_cm).astype(np.float32)[..., None]
    base = base * (1 - 0.35 * stripe) + srgb_to_lin((0.3, 0.32, 0.38))[None, None, :] * 0.35 * stripe
    # Coloured flecks (teal, red, ochre, light blue), a few mm each, tiling.
    img = Image.fromarray(np.zeros((size, size, 3), np.uint8))
    draw = ImageDraw.Draw(img)
    palette = [(45, 120, 125), (140, 55, 55), (150, 125, 65), (95, 115, 160)]
    for _ in range(int(size * size / 1800)):
        cx, cy = rng.uniform(0, size, 2)
        length = rng.uniform(0.1, 0.3) * px_per_cm
        angle = rng.uniform(0, np.pi)
        dx, dy = np.cos(angle) * length / 2, np.sin(angle) * length / 2
        colour = palette[rng.integers(len(palette))]
        for ox in (-size, 0, size):
            for oy in (-size, 0, size):
                draw.line([(cx - dx + ox, cy - dy + oy), (cx + dx + ox, cy + dy + oy)], fill=colour,
                          width=max(1, int(0.07 * px_per_cm)))
    flecks = np.asarray(img, np.float32) / 255.0
    mask = (flecks.max(axis=-1, keepdims=True) > 0).astype(np.float32)
    lin = base * (1 - mask) + srgb_to_lin(flecks) * mask
    # Worn where people sit: a soft lighter, shinier fade.
    lin *= 1.0 + 0.08 * noise(size, 600 * size / 2048, seed + 7)[..., None]
    return lin_to_srgb(lin)


def interior_colours(src, dst):
    size = 2048
    save(colour_field(size, (0.2, 0.2, 0.215), 11, 0.08, 0.04), dst / "interior_plastic_basecolor.png", "RGB")
    save(colour_field(size, (0.11, 0.11, 0.12), 12, 0.06, 0.04), dst / "plastic_black_basecolor.png", "RGB")
    save(colour_field(size, (0.13, 0.125, 0.12), 13, 0.1, 0.12, 300), dst / "carpet_basecolor.png", "RGB")
    save(colour_field(size, (0.62, 0.6, 0.55), 14, 0.07, 0.03, 700), dst / "headliner_cloth_basecolor.png", "RGB")
    save(seat_cloth(size, 15), dst / "seat_fabric_basecolor.png", "RGB")


def font(px):
    for f in FONTS:
        if Path(f).exists():
            return ImageFont.truetype(f, px)
    raise RuntimeError("No bold font found; add one to FONTS")


def fix_speedo(src, dst, name, colour):
    """Redraw the speedometer's numbers (0-220 every 20 over 240 degrees, 0 at lower left) smaller and further in
    so 100 and 120 no longer overlap. The dial is centred at (512, 512) px and its long ticks reach in to 374 px."""
    img = Image.open(src / name).convert("RGB")
    a = np.asarray(img, np.float32)
    cx, cy = 512.0, 512.0
    y, x = np.mgrid[0:img.size[1], 0:img.size[0]].astype(np.float32)
    r = np.hypot(x - cx, y - cy)
    band = (r > 140) & (r < 371)
    # The dial's own face colour, taken from inside the band where nothing is drawn.
    face = np.median(a[(r > 120) & (r < 135)], axis=0)
    a[band] = face
    img = Image.fromarray(a.astype(np.uint8))
    draw = ImageDraw.Draw(img)
    f = font(46)
    for value in range(0, 221, 20):
        t = np.radians(-120.0 + 240.0 * value / 220.0)
        px, py = cx + 300 * np.sin(t), cy - 300 * np.cos(t)
        draw.text((px, py), str(value), font=f, fill=colour, anchor="mm")
    draw.text((cx, cy + 190), "km/h", font=font(46), fill=colour, anchor="mm")
    dst.mkdir(parents=True, exist_ok=True)
    img.save(dst / name, optimize=True)


def main():
    for folder, table in SETS:
        src = ROOT / folder / "textures"
        dst = ROOT / folder / "textures_game"
        for i, (surface, spec) in enumerate(sorted(table.items())):
            retune(src, dst, surface, spec, 100 + i)
        if folder.endswith("Body_v2"):
            interior_colours(src, dst)
            for surface in ("gauge_faces", "gauge_warning"):
                fix_speedo(src, dst, surface + "_basecolor.png", (236, 234, 224))
                fix_speedo(src, dst, surface + "_emissive.png", (128, 166, 255))
        print("GAME_TEXTURES", folder, len(list(dst.glob("*.png"))), "maps")


if __name__ == "__main__":
    main()
