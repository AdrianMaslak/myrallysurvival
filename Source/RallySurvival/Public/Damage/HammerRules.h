#pragma once
#include <algorithm>
#include <cmath>

// Knocking dents out and bent parts back with the hammer (docs/DAMAGE.md, Hammer). Never like new: a panel comes
// back to about three quarters straight, a bent suspension part to a bodge. No engine dependencies.
namespace Rally::Hammer
{
    // A hit on sheet metal: within this reach (cm) of where it lands, the metal is pulled back this share of the
    // way towards its pressed shape, falling off to the edge; but never closer than this share of the worst it
    // was ever bent, so the panel stays a little wavy.
    constexpr double ReachCm = 9.0;
    constexpr double Pull = 0.35;
    constexpr double Floor = 0.25;
    // How far (cm) a point bent Now (its worst ever Peak) is bent after a hit Weight (0..1 across the reach) away.
    inline double Straightened(double Now, double Peak, double Weight)
    {
        if (!std::isfinite(Now) || !std::isfinite(Peak) || !std::isfinite(Weight) || Now <= 0.0) return std::max(Now, 0.0);
        const double Pulled = Now * (1.0 - Pull * std::clamp(Weight, 0.0, 1.0));
        const double Least = Floor * std::max(Peak, Now);
        return Now <= Least ? Now : std::max(Pulled, Least);
    }
    // Weight of a hit at Distance cm: full under the head, smoothly nothing at the edge of its reach.
    inline double Weight(double Distance)
    {
        if (!std::isfinite(Distance) || Distance >= ReachCm) return 0.0;
        const double U = std::max(Distance, 0.0) / ReachCm;
        return (1.0 - U * U) * (1.0 - U * U);
    }

    // A quarter of the blows on the body or a suspension part go wrong (a glancing blow, the wrong spot):
    // the metal gets a small new dent (cracking the paint there), the part loses a little.
    constexpr double MissChance = 0.25;
    constexpr double MissDentCm = 1.2;
    constexpr double MissRadiusCm = 5.0;
    // Glass and lamp lenses only break: each blow crazes them like a stone does.
    constexpr double GlassDentCm = 2.5;
    constexpr double GlassRadiusCm = 7.0;
    // A bent suspension part: each good blow brings back this much health, up to the bodge; a bad one loses this.
    constexpr double PartStep = 0.15;
    constexpr double PartMiss = 0.06;
    // Between blows (s): the swing.
    constexpr double SwingSeconds = 0.45;
}
