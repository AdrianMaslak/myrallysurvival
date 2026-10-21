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

    // A side hit bends a corner's arms and strut more than a hit along the car: up to 1.8 times (LateralShare is the
    // hit's direction across the car, 0..1).
    inline double SideHitScale(double LateralShare)
    {
        return 1.0 + 0.8 * std::clamp(std::isfinite(LateralShare) ? std::abs(LateralShare) : 0.0, 0.0, 1.0);
    }
    // A collapsed corner (suspension at 0): the wheel twists further than a bent one (CollapsedExtraToeDeg on top of
    // MaxToeOutDeg), scrubs along (ground drag), and leans in at the top and tucks under the body. A bent one leans a
    // little. The car still moves, crabbing and dragging, but is no longer drivable at speed.
    constexpr double CollapsedExtraToeDeg = 13.0;
    constexpr double CollapsedDrag = 0.3;
    constexpr double CollapsedCamberDeg = 12.0;
    constexpr double CollapsedTuckCm = 5.0;
    inline bool IsCollapsed(double SuspensionHealth) { return std::isfinite(SuspensionHealth) && SuspensionHealth <= 0.0; }
    inline double CamberDeg(double SuspensionHealth)
    {
        if (!std::isfinite(SuspensionHealth)) return 0.0;
        return SuspensionHealth <= 0.0 ? CollapsedCamberDeg : (1.0 - std::clamp(SuspensionHealth, 0.0, 1.0)) * 4.0;
    }
    inline double TuckCm(double SuspensionHealth) { return IsCollapsed(SuspensionHealth) ? CollapsedTuckCm : 0.0; }

    // A failed tyre is flat: it rides on the rim, about this far nearer the road.
    constexpr double FlatTyreDropCm = 7.0;
    constexpr double FlatTyrePressureBar = 0.2;

    // How far from a wheel centre (cm) the contact patch must be to hit the tyre and suspension
    // directly, and from the radiator to hit it.
    constexpr double WheelReachCm = 70.0;
    constexpr double RadiatorReachCm = 30.0;

    // A direct wheel hit tears the hub and the wheel comes off when that corner was already broken
    // (suspension at 0 before the hit) and the hit is more than a kerb knock, or when the hit alone is
    // huge: about 60-70 km/h into a wall. A healthy corner survives the 50 km/h barrier test.
    constexpr double WheelOffDeltaV = 22.0;
    constexpr double BrokenCornerWheelOffDeltaV = 5.0;
    inline bool TearsWheelOff(double DeltaV, double SuspensionHealthBefore)
    {
        if (!IsValidDeltaV(DeltaV) || !std::isfinite(SuspensionHealthBefore))
            return false;
        return DeltaV >= WheelOffDeltaV || (SuspensionHealthBefore <= 0.0 && DeltaV >= BrokenCornerWheelOffDeltaV);
    }

    // Without its wheel the corner rides on the hub and brake disc: this radius (cm), steel-on-ground
    // grip, and a scraping drag added to the surface's rolling resistance.
    constexpr double HubRadiusCm = 15.0;
    constexpr double HubGrip = 0.45;
    constexpr double HubDrag = 0.5;

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
