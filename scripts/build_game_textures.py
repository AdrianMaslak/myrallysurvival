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
- windscreen_drops: rain on the windscreen (M_Windscreen), a mask and a normal map.
- dash_labels, a new surface: the radio's display, the button labels, the climate strip, the trip meter, the air
  freshener and the extinguisher (scripts/interior_details.py places them).
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
    "lens_clear": ((0.12, 0.04, 80), 0.85, 1.0),
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


# The lenses are opaque in the game (M_Body), and the delivered colours made them milky: clear glass over a chrome
# reflector reads as a bright metal (metallic, BODY above), the tail lenses and the lamps behind them deep red (they were
# pink), the indicators' amber deeper. Each
# lens also glows in this colour when its lamp is lit (scripts/import_body_v2.py).
LENS_COLOURS = {"lens_clear": (0.62, 0.64, 0.67), "lens_red": (0.42, 0.025, 0.02), "lens_amber": (0.86, 0.42, 0.05),
                "lamp_brake": (0.5, 0.06, 0.05), "lamp_tail": (0.45, 0.05, 0.04)}


def lens_colours(src, dst):
    for surface, colour in LENS_COLOURS.items():
        size = Image.open(src / (surface + "_basecolor.png")).size
        Image.new("RGB", size, tuple(int(round(c * 255)) for c in colour)).save(dst / (surface + "_basecolor.png"))


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


# The dashboard's labels and the cabin's small details (scripts/interior_details.py maps its decals and pieces
# onto these rectangles, px from the top left of a 1024 atlas): the radio's display, its preset and the climate
# panel's button labels, the climate panel's strip, a knob's pointer, the co-driver's trip meter, the air
# freshener, the fire extinguisher, and plain patches. The emissive map lights the displays brightly and the labels
# dimly (the car sets its strength: URallyCabinComponent).
DASH = {
    "lcd": (0, 0, 512, 72), "presets": (0, 80, 384, 128), "climate_strip": (0, 140, 1024, 196),
    "climate_buttons": (0, 200, 576, 248), "pointer": (0, 260, 64, 276), "trip": (0, 300, 512, 428),
    "tree": (600, 300, 856, 556), "extinguisher_label": (0, 600, 256, 728), "red": (300, 600, 364, 664),
    "black": (400, 600, 464, 664), "white": (500, 600, 564, 664), "trip_case": (600, 600, 664, 664),
}
AMBER = (255, 150, 40)


def tree_outline(x0, y0, x1, y1):
    """The air freshener's pine, as a polygon in the rectangle (top point, three tiers, a trunk)."""
    w, h = x1 - x0, y1 - y0
    pts = [(0.5, 0.0), (0.68, 0.22), (0.6, 0.22), (0.8, 0.45), (0.68, 0.45), (0.92, 0.72), (0.56, 0.72), (0.56, 0.9),
           (0.44, 0.9), (0.44, 0.72), (0.08, 0.72), (0.32, 0.45), (0.2, 0.45), (0.4, 0.22), (0.32, 0.22)]
    return [(x0 + px * w, y0 + py * h) for px, py in pts]


def dash_labels(dst):
    size = 1024
    base = Image.new("RGB", (size, size), (20, 20, 22))
    glow = Image.new("RGB", (size, size), (0, 0, 0))
    db, dg = ImageDraw.Draw(base), ImageDraw.Draw(glow)

    def text(draw, box, label, px, fill, anchor="mm"):
        x0, y0, x1, y1 = box
        draw.text(((x0 + x1) / 2, (y0 + y1) / 2), label, font=font(px), fill=fill, anchor=anchor)

    # The radio: a dark amber window with the station.
    x0, y0, x1, y1 = DASH["lcd"]
    db.rectangle(DASH["lcd"], fill=(28, 18, 8))
    dg.rectangle(DASH["lcd"], fill=(14, 7, 0))
    for d in (db, dg):
        text(d, (x0, y0, x0 + 150, y1), "FM1", 34, AMBER)
        text(d, (x0 + 150, y0, x1, y1), "98.40 MHz", 50, AMBER)
    # Preset buttons 1-6 and the climate panel's nine, white on black, lit faintly.
    for i in range(6):
        cell = (i * 64, 80, i * 64 + 64, 128)
        db.rectangle(cell, fill=(16, 16, 18))
        text(db, cell, str(i + 1), 30, (225, 225, 220))
        text(dg, cell, str(i + 1), 30, (70, 90, 70))
    for i, label in enumerate(("A/C", "REC", "DEF", "RR", "MAX", "ECO", "-", "+", "OFF")):
        cell = (i * 64, 200, i * 64 + 64, 248)
        db.rectangle(cell, fill=(16, 16, 18))
        text(db, cell, label, 20, (225, 225, 220))
        text(dg, cell, label, 20, (70, 90, 70))
    # The climate strip: cold to hot, the fan's speeds, where the air goes.
    x0, y0, x1, y1 = DASH["climate_strip"]
    db.rectangle(DASH["climate_strip"], fill=(18, 18, 20))
    for x in range(40, 380):
        t = (x - 40) / 340.0
        c = (int(40 + 200 * t), int(80 * (1 - abs(t - 0.5) * 2)), int(230 * (1 - t) + 20))
        db.line([(x, y0 + 22), (x, y0 + 34)], fill=c)
        dg.line([(x, y0 + 22), (x, y0 + 34)], fill=tuple(v // 4 for v in c))
    for i in range(5):
        cx = 470 + i * 50
        db.rectangle((cx - 4, y1 - 14 - i * 6, cx + 4, y1 - 10), fill=(225, 225, 220))
        text(db, (cx - 20, y0, cx + 20, y0 + 20), str(i), 18, (225, 225, 220))
    for i, label in enumerate(("^", ">", "v", "W")):
        text(db, (760 + i * 64, y0, 820 + i * 64, y1), label, 30, (225, 225, 220))
    # A knob's white pointer.
    db.rectangle(DASH["pointer"], fill=(235, 235, 230))
    dg.rectangle(DASH["pointer"], fill=(60, 60, 60))
    # The co-driver's trip meter: black face, amber digits, a small label.
    x0, y0, x1, y1 = DASH["trip"]
    db.rectangle(DASH["trip"], fill=(14, 14, 15))
    db.rectangle((x0 + 20, y0 + 30, x1 - 20, y1 - 20), fill=(30, 18, 6))
    dg.rectangle((x0 + 20, y0 + 30, x1 - 20, y1 - 20), fill=(12, 6, 0))
    text(db, (x0, y0, x1, y0 + 30), "TRIP  km", 20, (200, 200, 195))
    for d in (db, dg):
        text(d, (x0 + 20, y0 + 30, x1 - 20, y1 - 20), "012.34", 70, AMBER)
    # The air freshener: a green pine with a yellow band.
    x0, y0, x1, y1 = DASH["tree"]
    db.rectangle(DASH["tree"], fill=(20, 90, 40))
    db.polygon(tree_outline(x0, y0, x1, y1), fill=(25, 120, 50))
    db.rectangle((x0 + 30, y0 + 150, x1 - 30, y0 + 175), fill=(230, 200, 60))
    text(db, (x0 + 30, y0 + 150, x1 - 30, y0 + 175), "VANILKA", 18, (40, 30, 10))
    # The extinguisher's label, and plain patches.
    db.rectangle(DASH["extinguisher_label"], fill=(170, 20, 18))
    x0, y0, x1, y1 = DASH["extinguisher_label"]
    db.rectangle((x0 + 30, y0 + 20, x1 - 30, y1 - 20), fill=(235, 232, 220))
    text(db, (x0 + 30, y0 + 20, x1 - 30, y0 + 64), "HASIACI", 22, (20, 20, 20))
    text(db, (x0 + 30, y0 + 64, x1 - 30, y1 - 20), "PRISTROJ 2 kg", 18, (20, 20, 20))
    db.rectangle(DASH["red"], fill=(170, 20, 18))
    db.rectangle(DASH["black"], fill=(14, 14, 15))
    db.rectangle(DASH["white"], fill=(235, 232, 220))
    db.rectangle(DASH["trip_case"], fill=(30, 30, 32))
    dst.mkdir(parents=True, exist_ok=True)
    base.save(dst / "dash_labels_basecolor.png", optimize=True)
    glow.save(dst / "dash_labels_emissive.png", optimize=True)
    rough = np.full((256, 256), 0.6, np.float32)
    save(rough, dst / "dash_labels_roughness.png", "L")
    save(np.zeros((256, 256), np.float32), dst / "dash_labels_metallic.png", "L")
    flat = np.zeros((64, 64, 3), np.float32)
    flat[..., :2] = 0.5
    flat[..., 2] = 1.0
    save(flat, dst / "dash_labels_normal.png", "RGB")


def windscreen_drops(dst, size=1024, seed=31):
    """Raindrops on glass for M_Windscreen: a mask (R: how much drop, 0 dry) and a normal map of their domes, tiling.
    UV0 runs about 2 per metre on the windscreen, so 1024 px is about 50 cm: drops of 1.5 to 6 mm, a few big runs."""
    rng = np.random.default_rng(seed)
    height = np.zeros((size, size), np.float32)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    px_per_mm = size / 500.0
    for i in range(1400):
        r = rng.uniform(0.75, 3.0) * px_per_mm * (1.8 if i < 60 else 1.0)
        cx, cy = rng.uniform(0, size, 2)
        x0, x1 = int(cx - r - 1), int(cx + r + 2)
        y0, y1 = int(cy - r - 1), int(cy + r + 2)
        ys, xs = np.mgrid[y0:y1, x0:x1]
        d = np.hypot(xs - cx, ys - cy) / r
        dome = np.sqrt(np.clip(1.0 - d * d, 0.0, 1.0)) * r
        np.maximum.at(height, (ys % size, xs % size), dome)
    mask = np.clip(height / (1.5 * px_per_mm), 0.0, 1.0)
    gy, gx = np.gradient(height)
    n = np.stack([-gx, gy, np.ones_like(height)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    save(mask, dst / "windscreen_drops_mask.png", "L")
    save(n * 0.5 + 0.5, dst / "windscreen_drops_normal.png", "RGB")


def main():
    for folder, table in SETS:
        src = ROOT / folder / "textures"
        dst = ROOT / folder / "textures_game"
        for i, (surface, spec) in enumerate(sorted(table.items())):
            retune(src, dst, surface, spec, 100 + i)
        if folder.endswith("Body_v2"):
            interior_colours(src, dst)
            lens_colours(src, dst)
            dash_labels(dst)
            windscreen_drops(dst)
            for surface in ("gauge_faces", "gauge_warning"):
                fix_speedo(src, dst, surface + "_basecolor.png", (236, 234, 224))
                fix_speedo(src, dst, surface + "_emissive.png", (128, 166, 255))
        print("GAME_TEXTURES", folder, len(list(dst.glob("*.png"))), "maps")


if __name__ == "__main__":
    main()
