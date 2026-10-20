"""Shared tyre and rim cross-sections for the Octavia 1U wheels (used by the build and the tyre texture).

Profiles are (r, a): r = radius from the spin axis, a = axial offset from the rim centre line, a < 0 outboard.
Tyre: 195/65 R15, 634 mm new (section height 126.75 mm on a 381 mm rim), 200 mm section width with the
sidewall bulge, four circumferential main grooves 8 mm deep. Rim: steel 6Jx15 ET38.
"""
import math

TYRE_R = 0.3172
TYRE_U_REPS = 2          # texture repeats round the tyre (sidewall lettering appears twice)

_half = [
    (0.1918, -0.0585),   # bead toe on the bead seat (just above the safety hump)
    (0.1908, -0.0742),   # bead heel
    (0.1993, -0.0775),   # against the flange face
    (0.2120, -0.0860),   # over the rolled lip
    (0.2780, -0.0985),   # widest point: the sidewall bulge (200 mm overall)
    (0.3030, -0.0880),
    (0.3135, -0.0740),   # shoulder
    (0.3165, -0.0640),   # tread edge
    (0.3168, -0.0540), (0.3088, -0.0490), (0.3168, -0.0440),   # outer main groove
    (0.3172, -0.0230), (0.3092, -0.0180), (0.3172, -0.0130),   # inner main groove
]
TYRE_PROFILE = _half + [(r, -a) for (r, a) in reversed(_half)]
GROOVES = [-0.0490, -0.0180, 0.0180, 0.0490]
GROOVE_W = 0.010

# Rim barrel (closed section, 3 mm steel): outboard rolled lip, bead seats with safety humps, drop centre well.
RIM_BARREL = [
    (0.2040, -0.0905), (0.2080, -0.0850), (0.2050, -0.0800), (0.1990, -0.0778),
    (0.1905, -0.0745), (0.1915, -0.0600), (0.1910, -0.0545), (0.1730, -0.0420), (0.1715, -0.0300),
    (0.1715, 0.0300), (0.1730, 0.0420), (0.1910, 0.0545), (0.1915, 0.0600), (0.1905, 0.0745),
    (0.1990, 0.0778), (0.2050, 0.0800), (0.2080, 0.0850), (0.2040, 0.0905),
    (0.1880, 0.0790), (0.1880, 0.0560), (0.1685, 0.0420), (0.1685, -0.0420), (0.1880, -0.0560), (0.1880, -0.0790),
]
# Centre disc (4 mm): flat mounting face at ET38, raised ring outboard of the bolt circle, sloped dish
# with the vent holes, welded into the well under the outboard bead seat.
RIM_DISC = [
    (0.0286, -0.0420), (0.0660, -0.0420), (0.0880, -0.0600), (0.1040, -0.0620), (0.1500, -0.0560), (0.1872, -0.0640),
    (0.1872, -0.0590), (0.1500, -0.0520), (0.1040, -0.0580), (0.0900, -0.0560), (0.0680, -0.0380), (0.0286, -0.0380),
]
PCD_R = 0.050
VENT_R, VENT_HALF_LEN, VENT_HALF_W, VENT_COUNT, VENT_START = 0.127, 0.021, 0.011, 6, 200.0


def arclen(prof, closed=True):
    """Cumulative length at each profile point, plus the total (including the closing edge)."""
    s = [0.0]
    k = len(prof)
    for j in range(1, k + (1 if closed else 0)):
        (r0, a0), (r1, a1) = prof[j - 1], prof[j % k]
        s.append(s[-1] + math.hypot(r1 - r0, a1 - a0))
    return s
