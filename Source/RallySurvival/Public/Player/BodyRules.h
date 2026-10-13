#pragma once
#include <algorithm>
#include <cmath>

// The crew's bodies (docs/BODY.md): injuries from hard crashes and the first-aid kit. No engine dependencies.
namespace Rally::Body
{
    // A crash's change of speed (m/s) hurts whoever is in the car from about 30 km/h; a 50 km/h wall about
    // halfway (0..1, adding up).
    inline double InjuryFromImpact(double DeltaV)
    {
        if (!std::isfinite(DeltaV)) return 0.0;
        return std::clamp((DeltaV - 8.5) / 18.0, 0.0, 1.0);
    }
    // Hurt, the edges of sight darken and blur and walking slows.
    inline double WalkScale(double Injury) { return 1.0 - 0.45 * std::clamp(Injury, 0.0, 1.0); }
    inline double Vignette(double Injury) { return 0.4 + 1.1 * std::clamp(Injury, 0.0, 1.0); }
    inline double Fringe(double Injury) { return 5.0 * std::clamp(Injury, 0.0, 1.0); }
    // A first-aid kit treats three times; each treatment takes the injury away.
    constexpr int FirstAidUses = 3;
}
