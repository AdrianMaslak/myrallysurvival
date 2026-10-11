#pragma once
#include <algorithm>
#include <cmath>

// Engine-independent crash rules shared by the car and standalone tests. Speeds in m/s, lengths in cm.
// Severity is the car's change of velocity in one impact (impulse / mass), not its road speed:
// a glancing hit at 100 km/h can be milder than parking into a wall at 20 km/h.
// All values are prototype tuning, chosen so a 20 km/h tap marks the car, 50 km/h into a wall
// crumples the front and flattens a tyre it reaches, and kerb-style knocks below 7 km/h do nothing.
namespace Rally::Impact
{
    // Below this change of velocity an impact leaves no mark.
    constexpr double MinDeltaV = 2.0;
    // Requests claiming more are treated as bogus.
    constexpr double MaxDeltaV = 45.0;

    struct FDent
    {
        double Depth = 0.0;
        double Radius = 0.0;
    };

    inline bool IsValidDeltaV(double DeltaV)
    {
        return std::isfinite(DeltaV) && DeltaV >= MinDeltaV && DeltaV <= MaxDeltaV;
    }

    inline FDent DentFor(double DeltaV)
    {
        FDent Out;
        if (!IsValidDeltaV(DeltaV))
            return Out;
        Out.Depth = std::min(3.0 + (DeltaV - MinDeltaV) * 1.6, 28.0);
        Out.Radius = std::min(35.0 + DeltaV * 3.0, 85.0);
        return Out;
    }

    // Health lost by a part the impact reaches, 0..1.
    inline double TyreDamage(double DeltaV) { return IsValidDeltaV(DeltaV) ? std::clamp((DeltaV - 4.0) / 8.0, 0.0, 1.0) : 0.0; }
    inline double SuspensionDamage(double DeltaV) { return IsValidDeltaV(DeltaV) ? std::clamp((DeltaV - 3.0) / 10.0, 0.0, 1.0) : 0.0; }
    inline double RadiatorDamage(double DeltaV) { return IsValidDeltaV(DeltaV) ? std::clamp((DeltaV - 3.0) / 9.0, 0.0, 1.0) : 0.0; }

    // A bent corner points its wheel outward; a written-off corner is about 5 degrees out.
    constexpr double MaxToeOutDeg = 5.0;
    inline double ToeOutDeg(double SuspensionHealth)
    {
        return (1.0 - std::clamp(SuspensionHealth, 0.0, 1.0)) * MaxToeOutDeg;
    }

    // A failed tyre is flat: it rides on the rim, about this far nearer the road.
    constexpr double FlatTyreDropCm = 7.0;
    constexpr double FlatTyrePressureBar = 0.2;

    // How far from a wheel centre (cm) the contact patch must be to hit the tyre and suspension
    // directly, and from the radiator to hit it.
    constexpr double WheelReachCm = 70.0;
    constexpr double RadiatorReachCm = 30.0;

    // Share of an impact a part feels, from the patch's distance to it: full within Reach, fading to
    // nothing over the crush distance beyond (a hard frontal hit pushes back into the wheel wells).
    inline double ReachFactor(double DistanceCm, double ReachCm, double CrushCm)
    {
        if (DistanceCm <= ReachCm)
            return 1.0;
        if (CrushCm <= 0.0)
            return 0.0;
        return std::clamp(1.0 - (DistanceCm - ReachCm) / CrushCm, 0.0, 1.0);
    }

    // How far a dent of this depth crushes into the car.
    inline double CrushDistance(double DentDepth) { return DentDepth * 1.5; }
}
