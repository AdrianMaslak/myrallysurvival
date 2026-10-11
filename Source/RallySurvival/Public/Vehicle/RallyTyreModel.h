#pragma once
#include <algorithm>
#include <cmath>

// Engine-independent tyre model shared by the car and standalone tests. SI units.
// Combined-slip "similarity" model: longitudinal and lateral slip are normalised by their
// peak values and share one friction curve, so braking or power reduces cornering grip.
// Sources and value derivations: docs/VEHICLE-PHYSICS.md.
namespace Rally::Tyre
{
    struct FSpec
    {
        // Grip relative to surface friction, on hard (tarmac) and loose (gravel) surfaces.
        double GripHard = 1.0;
        double GripLoose = 1.0;
        // Slip at peak force. Loose surfaces peak later (dirt about 16 degrees, tarmac about 6).
        double PeakSlipAngleHardDeg = 7.0;
        double PeakSlipAngleLooseDeg = 12.0;
        double PeakSlipRatioHard = 0.10;
        double PeakSlipRatioLoose = 0.15;
        // Share of peak force kept when fully sliding: road tyre on tarmac about 0.8,
        // rally tyre on dirt about 0.9 even beyond 20 degrees.
        double SlideRatioHard = 0.80;
        double SlideRatioLoose = 0.90;
        // Friction falls this fraction per 100% load above nominal.
        double LoadSensitivity = 0.10;
        double NominalLoad = 3500.0;
        // Pressure in bar; NominalPressure gives the values above.
        double Pressure = 2.0;
        double NominalPressure = 2.0;
    };

    struct FSurface
    {
        double Friction = 0.7;
        // 0 = hard tarmac, 1 = loose gravel or snow.
        double Looseness = 0.0;
    };

    struct FForces
    {
        double Longitudinal = 0.0;
        double Lateral = 0.0;
        double CombinedSlip = 0.0;
    };

    // Resolved per surface and pressure, so per-step evaluation stays cheap.
    struct FResolved
    {
        double Grip = 1.0;
        double PeakTanSlipAngle = 0.2;
        double PeakSlipRatio = 0.1;
        double SlideRatio = 0.85;
        double LoadSensitivity = 0.1;
        double NominalLoad = 3500.0;
    };

    inline double Lerp(double A, double B, double T) { return A + (B - A) * T; }

    // Pressure effects are gameplay assumptions in the direction rally practice describes:
    // softer tyres gain contact patch (grip on loose ground) and lose response.
    // Below 0.5 bar the tyre is treated as flat.
    inline FResolved Resolve(const FSpec& Spec, const FSurface& Surface)
    {
        const double Loose = std::clamp(Surface.Looseness, 0.0, 1.0);
        const double Pressure = std::max(Spec.Pressure, 0.0);
        const double Soft = std::clamp((Spec.NominalPressure - Pressure) / std::max(Spec.NominalPressure, 0.1), -0.5, 1.0);
        FResolved Out;
        Out.Grip = Lerp(Spec.GripHard, Spec.GripLoose, Loose) * (1.0 + 0.15 * Soft * Loose - 0.05 * std::max(Soft, 0.0) * (1.0 - Loose));
        double PeakAngle = Lerp(Spec.PeakSlipAngleHardDeg, Spec.PeakSlipAngleLooseDeg, Loose) * (1.0 + 0.4 * Soft);
        Out.PeakSlipRatio = Lerp(Spec.PeakSlipRatioHard, Spec.PeakSlipRatioLoose, Loose) * (1.0 + 0.3 * Soft);
        Out.SlideRatio = Lerp(Spec.SlideRatioHard, Spec.SlideRatioLoose, Loose);
        Out.LoadSensitivity = Spec.LoadSensitivity;
        Out.NominalLoad = std::max(Spec.NominalLoad, 1.0);
        if (Pressure < 0.5)
        {
            Out.Grip *= 0.45;
            PeakAngle *= 2.0;
            Out.SlideRatio *= 0.8;
        }
        Out.PeakTanSlipAngle = std::tan(std::clamp(PeakAngle, 1.0, 40.0) * 3.14159265358979 / 180.0);
        return Out;
    }

    // Normalised force for normalised slip S >= 0: slope 1.5 at zero, 1 at S = 1,
    // then decays smoothly toward the sliding ratio.
    inline double Curve(double S, double SlideRatio)
    {
        if (S <= 1.0)
            return 1.5 * S - 0.5 * S * S * S;
        return SlideRatio + (1.0 - SlideRatio) * std::exp(-1.5 * (S - 1.0));
    }

    // SlipRatio: (wheel surface speed - ground speed) / reference speed, positive when driving.
    // TanSlipAngle: lateral / longitudinal contact velocity, positive when sliding to the right.
    // Returns forces on the car at the contact patch: longitudinal forward-positive, lateral right-positive.
    inline FForces Evaluate(const FResolved& Tyre, const FSurface& Surface, double Load, double SlipRatio, double TanSlipAngle)
    {
        FForces Out;
        if (!(Load > 0.0) || !std::isfinite(SlipRatio) || !std::isfinite(TanSlipAngle))
            return Out;
        const double Sx = SlipRatio / Tyre.PeakSlipRatio;
        const double Sy = TanSlipAngle / Tyre.PeakTanSlipAngle;
        const double S = std::sqrt(Sx * Sx + Sy * Sy);
        Out.CombinedSlip = S;
        if (S < 1e-9)
            return Out;
        // The 0.2 floor stops absurd loads reversing friction in the linear sensitivity model.
        const double LoadFactor = std::max(0.2, 1.0 - Tyre.LoadSensitivity * (Load / Tyre.NominalLoad - 1.0));
        const double Mu = Surface.Friction * Tyre.Grip * LoadFactor;
        const double Force = Mu * Load * Curve(S, Tyre.SlideRatio);
        Out.Longitudinal = Force * Sx / S;
        Out.Lateral = -Force * Sy / S;
        return Out;
    }
}
