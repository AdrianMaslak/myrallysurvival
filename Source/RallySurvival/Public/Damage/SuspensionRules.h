#pragma once
#include <algorithm>
#include <cmath>

// What damaged running gear does to the car, and how impacts reach it. Health 0..1 as in
// PartConditionRules.h. Prototype tuning, no engine dependencies; shared with standalone tests.
namespace Rally::Suspension
{
    inline double Clamp01(double H) { return std::isfinite(H) ? std::clamp(H, 0.0, 1.0) : 1.0; }

    // Strut (spring and damper together): a worn damper lets the corner bounce, a broken spring lets it
    // sag onto its bump stop.
    inline double SpringRateScale(double StrutHealth) { return 0.5 + 0.5 * Clamp01(StrutHealth); }
    inline double DampingScale(double StrutHealth) { return 0.2 + 0.8 * Clamp01(StrutHealth); }

    // Tie rod: bent, it turns the wheel in by up to 3 degrees; broken, the wheel no longer follows the
    // steering and flops 6 degrees out.
    constexpr double BrokenTieRodToeOutDeg = 6.0;
    inline double TieRodToeOutDeg(double TieRodHealth)
    {
        const double H = Clamp01(TieRodHealth);
        return H <= 0.0 ? BrokenTieRodToeOutDeg : -3.0 * (1.0 - H);
    }
    inline bool Steers(double TieRodHealth) { return Clamp01(TieRodHealth) > 0.0; }

    // Driveshaft: a broken one turns freely, and the open differential sends all the drive to it, so
    // the car has no drive at all until it is replaced.
    inline bool Drives(double DriveshaftHealth) { return Clamp01(DriveshaftHealth) > 0.0; }

    // Brake disc and caliper: a damaged corner brakes weakly and the car pulls when braking.
    inline double BrakeScale(double BrakeHealth) { return 0.15 + 0.85 * Clamp01(BrakeHealth); }

    // Health lost by each part a wheel-area impact reaches (change of velocity, m/s; Reach 0..1 from
    // ImpactRules ReachFactor). Struts and tie rods suffer from direct wheel hits; a driveshaft needs a
    // hard hit crushing back into the wheel well; a disc and caliper sit behind the wheel and need a hard
    // direct hit.
    inline double Loss(double DeltaV, double From, double Span, double Reach)
    {
        if (!std::isfinite(DeltaV) || DeltaV < 2.0 || DeltaV > 45.0 || !std::isfinite(Reach)) return 0.0;
        return std::clamp((DeltaV - From) / Span, 0.0, 1.0) * std::clamp(Reach, 0.0, 1.0);
    }
    inline double StrutDamage(double DeltaV, double Reach) { return Loss(DeltaV, 4.0, 12.0, Reach); }
    inline double TieRodDamage(double DeltaV, double Reach) { return Loss(DeltaV, 3.0, 9.0, Reach); }
    inline double DriveshaftDamage(double DeltaV, double Reach) { return Loss(DeltaV, 6.0, 14.0, Reach); }
    inline double BrakeDamage(double DeltaV, double Reach) { return Loss(DeltaV, 5.0, 15.0, Reach); }
}
