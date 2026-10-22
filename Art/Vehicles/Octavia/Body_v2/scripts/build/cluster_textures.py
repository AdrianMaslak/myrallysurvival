"""Instrument-cluster textures (v2.1).
gauge_faces   2048x2048 atlas: speedo, tacho, temperature, fuel faces, centre panel; basecolor + emissive (night lighting)
gauge_warning 2048x1024 atlas: 18 tell-tales; left half = unlit look, right half = lit (emissive) look
gauge_needle  flat orange-red with matching emissive
Everything is drawn 4x and downsampled for clean edges. Layout constants are imported by stage8_cluster.py."""
import os, math
from PIL import Image, ImageDraw, ImageFont
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEX = os.path.join(ROOT, "export", "textures")
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
COND = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf"
if not os.path.exists(COND): COND = BOLD

A = 2048                       # gauge_faces atlas
DIALS = {                      # atlas centre (px), atlas radius (px)
    "Needle_Speedo": ((512, 512), 480), "Needle_Tacho": ((1536, 512), 480),
    "Needle_Temp": ((256, 1280), 230), "Needle_Fuel": ((768, 1280), 230)}
PANEL_PX = (1100, 1050, 1500, 1950)  # centre-panel background area (x0, y0, x1, y1), plain face colour

# tell-tales: id, colour, where (centre column grid / tacho row / speedo row)
RED, AMB, GRN, BLU = (235, 40, 30), (255, 165, 20), (40, 225, 70), (45, 120, 255)
TELLTALES = [
    ("turn_left", GRN, "centre"), ("turn_right", GRN, "centre"),
    ("high_beam", BLU, "centre"), ("check_engine", AMB, "centre"),
    ("oil_pressure", RED, "centre"), ("battery", RED, "centre"),
    ("brake_system", RED, "centre"), ("handbrake", RED, "centre"),
    ("abs", AMB, "centre"), ("airbag", RED, "centre"),
    ("coolant", RED, "tacho"), ("epc", AMB, "tacho"), ("esp", AMB, "tacho"), ("rear_fog", AMB, "tacho"),
    ("fuel_reserve", AMB, "speedo"), ("seatbelt", RED, "speedo"), ("door_open", RED, "speedo"), ("front_fog", GRN, "speedo")]
WA_W, WA_H, CELL = 2048, 1024, 200   # warning atlas; 5 columns x 4 rows of 200 px cells per half
def cell_px(i, lit):
    c, r = i % 5, i // 5
    x0 = (1024 if lit else 0) + 12 + c * CELL; y0 = 112 + r * CELL
    return (x0, y0, x0 + CELL, y0 + CELL)

S = 4  # supersampling


def font(sz, cond=False): return ImageFont.truetype(COND if cond else BOLD, int(sz))


# ------------------------------------------------------------------ tell-tale glyphs (drawn into a unit cell 0..1)
def glyph(d, name, col, x0, y0, s):
    P = lambda u, v: (x0 + u * s, y0 + v * s)
    W = max(2, int(s * 0.07))
    def line(*pts, w=W): d.line([P(*p) for p in pts], fill=col, width=int(w), joint="curve")
    def poly(pts, fill=True): d.polygon([P(*p) for p in pts], fill=col if fill else None, outline=col)
    def ell(u0, v0, u1, v1, w=W, fill=False): d.ellipse([P(u0, v0), P(u1, v1)], outline=col, width=int(w), fill=col if fill else None)
    def arc(u0, v0, u1, v1, a0, a1, w=W): d.arc([P(u0, v0), P(u1, v1)], a0, a1, fill=col, width=int(w))
    def text(t, sz, u=0.5, v=0.5, cond=False):
        f = font(s * sz, cond); bb = d.textbbox((0, 0), t, font=f)
        d.text((x0 + u * s - (bb[2] + bb[0]) / 2, y0 + v * s - (bb[3] + bb[1]) / 2), t, fill=col, font=f)
    def brackets():
        arc(0.04, 0.12, 0.30, 0.88, 120, 240); arc(0.70, 0.12, 0.96, 0.88, -60, 60)
    if name in ("turn_left", "turn_right"):
        pts = [(0.10, 0.5), (0.45, 0.18), (0.45, 0.36), (0.88, 0.36), (0.88, 0.64), (0.45, 0.64), (0.45, 0.82)]
        if name == "turn_right": pts = [(1 - u, v) for u, v in pts]
        poly(pts)
    elif name == "high_beam":
        d.chord([P(0.50, 0.22), P(0.94, 0.78)], 270, 90, fill=col)
        line((0.66, 0.22), (0.66, 0.78))
        for v in (0.26, 0.38, 0.50, 0.62, 0.74): line((0.08, v), (0.42, v))
    elif name == "check_engine":
        pts = [(0.20, 0.36), (0.30, 0.36), (0.30, 0.28), (0.44, 0.28), (0.44, 0.22), (0.62, 0.22), (0.62, 0.28), (0.72, 0.28),
               (0.80, 0.36), (0.86, 0.36), (0.86, 0.30), (0.94, 0.30), (0.94, 0.70), (0.86, 0.70), (0.86, 0.62), (0.80, 0.62),
               (0.70, 0.76), (0.34, 0.76), (0.28, 0.66), (0.20, 0.66)]
        d.line([P(*p) for p in pts + pts[:1]], fill=col, width=W, joint="curve")
        line((0.06, 0.40), (0.06, 0.62)); line((0.06, 0.51), (0.20, 0.51))
    elif name == "oil_pressure":
        poly([(0.20, 0.42), (0.62, 0.42), (0.74, 0.56), (0.92, 0.44), (0.94, 0.48), (0.70, 0.74), (0.20, 0.74)], fill=False)
        line((0.20, 0.42), (0.62, 0.42), (0.74, 0.56), (0.92, 0.44)); line((0.70, 0.74), (0.20, 0.74), (0.20, 0.42))
        line((0.38, 0.42), (0.38, 0.32)); line((0.30, 0.32), (0.46, 0.32))
        line((0.20, 0.48), (0.06, 0.40), (0.06, 0.54), (0.20, 0.60))
        ell(0.86, 0.56, 0.94, 0.66, fill=True)
    elif name == "battery":
        d.rectangle([P(0.10, 0.32), P(0.90, 0.78)], outline=col, width=W)
        d.rectangle([P(0.20, 0.22), P(0.34, 0.32)], fill=col); d.rectangle([P(0.66, 0.22), P(0.80, 0.32)], fill=col)
        line((0.20, 0.54), (0.36, 0.54)); line((0.62, 0.54), (0.78, 0.54)); line((0.70, 0.46), (0.70, 0.62))
    elif name == "brake_system":
        ell(0.24, 0.20, 0.76, 0.80); brackets(); text("!", 0.42)
    elif name == "handbrake":
        ell(0.24, 0.20, 0.76, 0.80); brackets(); text("P", 0.34)
    elif name == "abs":
        ell(0.20, 0.20, 0.80, 0.80); brackets(); text("ABS", 0.20)
    elif name == "airbag":
        ell(0.20, 0.16, 0.36, 0.32, fill=True)
        line((0.30, 0.36), (0.24, 0.62), (0.52, 0.62), (0.58, 0.86)); line((0.28, 0.44), (0.46, 0.52))
        ell(0.58, 0.26, 0.92, 0.60, fill=True)
    elif name == "seatbelt":
        ell(0.40, 0.08, 0.60, 0.28, fill=True)
        line((0.50, 0.32), (0.44, 0.62), (0.70, 0.62), (0.76, 0.92)); line((0.44, 0.40), (0.30, 0.66))
        line((0.62, 0.34), (0.36, 0.80), w=W * 1.4); d.rectangle([P(0.30, 0.78), P(0.42, 0.86)], fill=col)
    elif name == "coolant":
        line((0.50, 0.14), (0.50, 0.62)); ell(0.40, 0.58, 0.60, 0.76, fill=True)
        for v in (0.24, 0.36, 0.48): line((0.50, v), (0.66, v), w=W * 0.7)
        for v in (0.70, 0.84):
            d.line([P(0.08 + k * 0.05, v + 0.04 * math.sin(k * 1.4)) for k in range(18)], fill=col, width=int(W * 0.8))
    elif name == "epc":
        text("EPC", 0.36, cond=True)
    elif name == "esp":
        d.rounded_rectangle([P(0.24, 0.20), P(0.76, 0.56)], radius=int(s * 0.08), outline=col, width=W)
        line((0.30, 0.32), (0.70, 0.32)); d.rectangle([P(0.26, 0.56), P(0.36, 0.64)], fill=col); d.rectangle([P(0.64, 0.56), P(0.74, 0.64)], fill=col)
        for u in (0.28, 0.62):
            d.line([P(u + 0.05 * math.sin(k * 0.9), 0.70 + k * 0.03) for k in range(9)], fill=col, width=int(W * 0.8))
    elif name == "fuel_reserve":
        d.rectangle([P(0.22, 0.20), P(0.58, 0.84)], outline=col, width=W); d.rectangle([P(0.28, 0.27), P(0.52, 0.44)], fill=col)
        line((0.14, 0.84), (0.66, 0.84)); line((0.58, 0.40), (0.70, 0.40), (0.76, 0.48), (0.76, 0.74), (0.84, 0.74), (0.84, 0.34), (0.74, 0.24))
    elif name == "door_open":
        d.rounded_rectangle([P(0.30, 0.08), P(0.70, 0.92)], radius=int(s * 0.12), outline=col, width=W)
        line((0.36, 0.34), (0.64, 0.34)); line((0.36, 0.70), (0.64, 0.70))
        line((0.30, 0.38), (0.10, 0.58)); line((0.70, 0.38), (0.90, 0.58))
    elif name in ("rear_fog", "front_fog"):
        rear = name == "rear_fog"
        if rear:
            d.chord([P(0.50, 0.22), P(0.94, 0.78)], 270, 90, fill=col); line((0.66, 0.22), (0.66, 0.78))
            for v in (0.30, 0.50, 0.70): line((0.08, v), (0.42, v))
            d.line([P(0.25 + 0.05 * math.sin(k * 1.2), 0.18 + k * 0.05) for k in range(14)], fill=col, width=int(W * 0.8))
        else:
            d.chord([P(0.06, 0.22), P(0.50, 0.78)], 90, 270, fill=col); line((0.34, 0.22), (0.34, 0.78))
            for v in (0.30, 0.50, 0.70): line((0.58, v - 0.04), (0.92, v + 0.06))
            d.line([P(0.75 + 0.05 * math.sin(k * 1.2), 0.18 + k * 0.05) for k in range(14)], fill=col, width=int(W * 0.8))


def warning_atlas():
    W_, H_ = WA_W * S, WA_H * S
    base = Image.new("RGB", (W_, H_), (7, 7, 8)); emi = Image.new("RGB", (W_, H_), (0, 0, 0))
    db, de = ImageDraw.Draw(base), ImageDraw.Draw(emi)
    for i, (nm, col, _) in enumerate(TELLTALES):
        for lit in (False, True):
            x0, y0, x1, y1 = [c * S for c in cell_px(i, lit)]
            pad = 0.12 * (x1 - x0)
            if lit:
                glyph(db, nm, col, x0 + pad, y0 + pad, x1 - x0 - 2 * pad)
                glyph(de, nm, col, x0 + pad, y0 + pad, x1 - x0 - 2 * pad)
            else:
                glyph(db, nm, (34, 34, 36), x0 + pad, y0 + pad, x1 - x0 - 2 * pad)   # unlit: smoked lens, symbol just visible
    base.resize((WA_W, WA_H), Image.LANCZOS).save(os.path.join(TEX, "gauge_warning_basecolor.png"))
    emi.resize((WA_W, WA_H), Image.LANCZOS).save(os.path.join(TEX, "gauge_warning_emissive.png"))


# ------------------------------------------------------------------ dial faces
FACE, PRINT, GLOW = (13, 14, 16), (238, 238, 232), (215, 222, 230)


def dial(db, de, cx, cy, R, v0, v1, minor, labels, zero=-120, sweep=240, red_from=None, title="", sub="", lcd=False,
         mid=None, label_r=0.66, label_sz=0.115, icon=None):
    cx, cy, R = cx * S, cy * S, R * S
    db.ellipse((cx - R, cy - R, cx + R, cy + R), fill=FACE)
    db.ellipse((cx - R, cy - R, cx + R, cy + R), outline=(44, 46, 50), width=int(R * 0.02))
    ang = lambda val: math.radians(zero + sweep * (val - v0) / (v1 - v0))
    pt = lambda a, r: (cx + r * math.sin(a), cy - r * math.cos(a))
    if red_from is not None:
        a0, a1 = zero + sweep * (red_from - v0) / (v1 - v0), zero + sweep
        for d_, c in ((db, (205, 28, 22)), (de, (200, 30, 20))):
            d_.arc((cx - R * 0.93, cy - R * 0.93, cx + R * 0.93, cy + R * 0.93), a0 - 90, a1 - 90, fill=c, width=int(R * 0.11))
    n = int(round((v1 - v0) / minor))
    for k in range(n + 1):
        val = v0 + k * minor; a = ang(val)
        big = any(abs(val - lv) < 1e-6 for lv, _ in labels); half = (mid is not None and abs(((val - v0) / mid) - round((val - v0) / mid)) < 1e-6)
        r0 = R * (0.80 if big else 0.86 if half else 0.89)
        w = int(R * (0.026 if big else 0.016 if half else 0.010))
        for d_, c in ((db, PRINT), (de, GLOW)): d_.line((*pt(a, r0), *pt(a, R * 0.955)), fill=c, width=w)
    f = font(R * label_sz)
    for val, txt in labels:
        a = ang(val); x, y = pt(a, R * label_r)
        bb = db.textbbox((0, 0), txt, font=f)
        for d_, c in ((db, PRINT), (de, GLOW)): d_.text((x - (bb[2] + bb[0]) / 2, y - (bb[3] + bb[1]) / 2), txt, fill=c, font=f)
    if title:
        f2 = font(R * 0.075); bb = db.textbbox((0, 0), title, font=f2)
        for d_, c in ((db, (190, 190, 185)), (de, (150, 156, 162))):
            d_.text((cx - (bb[2] + bb[0]) / 2, cy - R * 0.36 - (bb[3] + bb[1]) / 2), title, fill=c, font=f2)
    if sub:
        f3 = font(R * 0.055); bb = db.textbbox((0, 0), sub, font=f3)
        db.text((cx - (bb[2] + bb[0]) / 2, cy - R * 0.25 - (bb[3] + bb[1]) / 2), sub, fill=(120, 122, 124), font=f3)
    if lcd:   # LCD window low in the dial (odometer / clock); ghost segments only - the game draws live values
        x0, y0, x1, y1 = cx - R * 0.27, cy + R * 0.56, cx + R * 0.27, cy + R * 0.74
        db.rounded_rectangle((x0, y0, x1, y1), radius=int(R * 0.03), fill=(22, 27, 24), outline=(55, 58, 60), width=int(R * 0.008))
        de.rounded_rectangle((x0, y0, x1, y1), radius=int(R * 0.03), fill=(10, 16, 12))
        f4 = font(R * 0.12, cond=True); t = "888888"; bb = db.textbbox((0, 0), t, font=f4)
        db.text(((x0 + x1) / 2 - (bb[2] + bb[0]) / 2, (y0 + y1) / 2 - (bb[3] + bb[1]) / 2), t, fill=(32, 38, 34), font=f4)
    if icon:
        s = R * 0.30
        for d_, c in ((db, PRINT), (de, GLOW)): glyph(d_, icon, c, cx - s / 2, cy + R * 0.18, s)


def faces_atlas():
    W_ = A * S
    base = Image.new("RGB", (W_, W_), FACE); emi = Image.new("RGB", (W_, W_), (0, 0, 0))
    db, de = ImageDraw.Draw(base), ImageDraw.Draw(emi)
    (sc, sr), (tc, tr) = DIALS["Needle_Speedo"], DIALS["Needle_Tacho"]
    dial(db, de, *sc, sr, 0, 220, 10, [(v, str(v)) for v in range(0, 221, 20)], title="km/h", lcd=True, mid=None, label_r=0.69, label_sz=0.098)
    dial(db, de, *tc, tr, 0, 7, 0.25, [(v, str(v)) for v in range(0, 8)], red_from=6.5, title="1/min x1000", lcd=True, mid=0.5,
         label_r=0.64, label_sz=0.15)
    (mc, mr), (fc, fr) = DIALS["Needle_Temp"], DIALS["Needle_Fuel"]
    dial(db, de, *mc, mr, 50, 130, 10, [(50, "50"), (90, "90"), (130, "130")], zero=-45, sweep=90, red_from=115, mid=None,
         label_r=0.62, label_sz=0.17, icon="coolant")
    dial(db, de, *fc, fr, 0, 1, 0.125, [(0, "0"), (0.5, "1/2"), (1, "1")], zero=-45, sweep=90, red_from=None, mid=0.25,
         label_r=0.62, label_sz=0.17, icon="fuel_reserve")
    # red reserve band on the fuel dial
    cx, cy, R = fc[0] * S, fc[1] * S, fr * S
    for d_, c in ((db, (205, 28, 22)), (de, (200, 30, 20))):
        d_.arc((cx - R * 0.93, cy - R * 0.93, cx + R * 0.93, cy + R * 0.93), -45 - 90, -45 + 90 * 0.125 - 90, fill=c, width=int(R * 0.09))
    base.resize((A, A), Image.LANCZOS).save(os.path.join(TEX, "gauge_faces_basecolor.png"))
    emi.resize((A, A), Image.LANCZOS).save(os.path.join(TEX, "gauge_faces_emissive.png"))


def flat_maps(name, rough, metal):
    Image.new("L", (64, 64), rough).save(os.path.join(TEX, f"{name}_roughness.png"))
    Image.new("L", (64, 64), metal).save(os.path.join(TEX, f"{name}_metallic.png"))
    Image.new("RGB", (64, 64), (128, 128, 255)).save(os.path.join(TEX, f"{name}_normal.png"))


if __name__ == "__main__":
    faces_atlas(); warning_atlas()
    for nm in ("gauge_faces", "gauge_warning"): flat_maps(nm, 140, 0)
    Image.new("RGB", (64, 64), (235, 70, 20)).save(os.path.join(TEX, "gauge_needle_basecolor.png"))
    Image.new("RGB", (64, 64), (235, 70, 20)).save(os.path.join(TEX, "gauge_needle_emissive.png"))
    flat_maps("gauge_needle", 90, 0)
    print("cluster textures written")
