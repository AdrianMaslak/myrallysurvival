#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// The cabin and the lamps (docs/OCTAVIA-VEHICLE.md, Cabin and lamps): where the gauge needles point, which
// warning lamps are lit, the indicators' flash, the wipers' sweep and the gear lever's place in its gate.
// Angles are the v2 interior's rig (Art/Vehicles/Octavia/Body_v2/Export/Game/game_parts.json): each part turns
// from its rest at 0. No engine dependencies.
namespace Rally::Cabin
{
    inline double Finite(double V) { return std::isfinite(V) ? V : 0.0; }

    // The dials: speedometer 0-220 km/h and tachometer 0-7000 rpm over 240 degrees; temperature 50-130 C and fuel
    // empty to full over 90. With the ignition off the needles lie at their stops, as the car's did.
    constexpr double SpeedoKmhPerDeg = 220.0 / 240.0;
    constexpr double TachoRpmPerDeg = 7000.0 / 240.0;
    inline double SpeedoDeg(double Kmh, bool bIgnition)
    {
        return bIgnition ? std::clamp(std::abs(Finite(Kmh)) / SpeedoKmhPerDeg, 0.0, 240.0) : 0.0;
    }
    inline double TachoDeg(double Rpm, bool bIgnition)
    {
        return bIgnition ? std::clamp(Finite(Rpm) / TachoRpmPerDeg, 0.0, 240.0) : 0.0;
    }
    inline double TempDeg(double TempC, bool bIgnition)
    {
        return bIgnition ? std::clamp((Finite(TempC) - 50.0) * 90.0 / 80.0, 0.0, 90.0) : 0.0;
    }
    inline double FuelDeg(double Litres, double CapacityL, bool bIgnition)
    {
        return bIgnition && CapacityL > 0.0 ? std::clamp(Finite(Litres) / CapacityL, 0.0, 1.0) * 90.0 : 0.0;
    }
    // A needle follows its reading like a damped pointer: a fraction of the gap a second, more for a big gap
    // (the tachometer flicks, the temperature creeps).
    inline double FollowNeedle(double Shown, double Target, double Rate, double Dt)
    {
        const double Step = 1.0 - std::exp(-std::max(Rate, 0.0) * std::clamp(Finite(Dt), 0.0, 0.25));
        return Finite(Shown) + (Finite(Target) - Finite(Shown)) * Step;
    }

    // Indicators: off, left, right, or the hazards (both). They flash at 1.5 Hz, lit for the first half of each
    // beat, and a turn indicator cancels itself once the wheel has turned that way by CancelTurnDeg and come back
    // to within CancelReturnDeg of straight.
    enum class EIndicator : uint8_t { Off, Left, Right, Hazard };
    constexpr double FlashHz = 1.5;
    constexpr double CancelTurnDeg = 90.0;
    constexpr double CancelReturnDeg = 20.0;
    inline bool FlashOn(double Seconds)
    {
        const double Beat = Finite(Seconds) * FlashHz;
        return Beat - std::floor(Beat) < 0.5;
    }
    inline bool LeftLit(EIndicator Mode, double Seconds) { return (Mode == EIndicator::Left || Mode == EIndicator::Hazard) && FlashOn(Seconds); }
    inline bool RightLit(EIndicator Mode, double Seconds) { return (Mode == EIndicator::Right || Mode == EIndicator::Hazard) && FlashOn(Seconds); }
    // Steering-wheel degrees, + turning right. Returns the mode after this frame and keeps the furthest turn.
    inline EIndicator SelfCancel(EIndicator Mode, double WheelDeg, double& FurthestDeg)
    {
        if (Mode != EIndicator::Left && Mode != EIndicator::Right) { FurthestDeg = 0.0; return Mode; }
        const double Toward = Mode == EIndicator::Right ? Finite(WheelDeg) : -Finite(WheelDeg);
        FurthestDeg = std::max(FurthestDeg, Toward);
        if (FurthestDeg >= CancelTurnDeg && Toward <= CancelReturnDeg) { FurthestDeg = 0.0; return EIndicator::Off; }
        return Mode;
    }
    // The indicator stalk: up for right, down for left (degrees of its rig, -12..12).
    inline double SignalStalkDeg(EIndicator Mode) { return Mode == EIndicator::Right ? 12.0 : Mode == EIndicator::Left ? -12.0 : 0.0; }

    // Wipers: off, intermittent (one wipe every 4 s), slow (40 a minute) and fast (65 a minute). A wipe goes out
    // and back; switched off, the arms finish the wipe and park. Sweep 0..1 of each arm's range.
    enum class EWiper : uint8_t { Off, Interval, Slow, Fast };
    inline double WipesPerSecond(EWiper Mode) { return Mode == EWiper::Fast ? 65.0 / 60.0 : 40.0 / 60.0; }
    constexpr double IntervalPauseS = 4.0;
    // Advances the wipe phase (0 parked .. 1 back at park) and returns the sweep; Pause counts the interval's wait.
    inline double AdvanceWiper(EWiper Mode, double& Phase, double& Pause, double Dt)
    {
        Dt = std::clamp(Finite(Dt), 0.0, 0.25);
        if (Phase <= 0.0)
        {
            if (Mode == EWiper::Off) { Pause = 0.0; return 0.0; }
            if (Mode == EWiper::Interval && Pause > 0.0) { Pause -= Dt; return 0.0; }
        }
        Phase += Dt * WipesPerSecond(Mode == EWiper::Off || Mode == EWiper::Interval ? EWiper::Slow : Mode);
        if (Phase >= 1.0)
        {
            Phase = Mode == EWiper::Off || Mode == EWiper::Interval ? 0.0 : Phase - 1.0;
            Pause = Mode == EWiper::Interval ? IntervalPauseS : 0.0;
        }
        // Out and back, easing at each end as a crank-driven linkage does.
        return 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * Phase);
    }
    // The wiper stalk: one notch (5 degrees) per speed.
    inline double WiperStalkDeg(EWiper Mode) { return 5.0 * static_cast<double>(static_cast<uint8_t>(Mode)); }
    inline EWiper NextWiper(EWiper Mode) { return static_cast<EWiper>((static_cast<uint8_t>(Mode) + 1) % 4); }

    // The warning lamps, in the cluster's strip order (M_Body_Dash Warn0..Warn7).
    enum EWarning : int { Oil, Battery, Brake, Abs, TurnLeft, TurnRight, HighBeam, Belt, WarningCount };
    struct FWarningInputs
    {
        bool bIgnition = false;
        bool bRunning = false;
        // Seconds since the ignition came on: every lamp lights for the bulb check, the belt reminder for longer.
        double IgnitionSeconds = 0.0;
        double OilL = 4.0;
        // Battery charge 0..1, and whether the alternator is there to charge it.
        double Charge = 1.0;
        bool bCharging = true;
        bool bHandbrake = false;
        bool bLeftFlash = false;
        bool bRightFlash = false;
        bool bHighBeam = false;
    };
    constexpr double BulbCheckS = 2.0;
    constexpr double BeltReminderS = 6.0;
    constexpr double LowOilL = 2.0;
    // Which lamps are lit (bit per EWarning). The indicator and high-beam lamps work with the ignition off, as
    // they are on the lamps' own circuits.
    inline uint32_t WarningLamps(const FWarningInputs& In)
    {
        uint32_t Lit = 0;
        auto Set = [&Lit](int Lamp, bool bOn) { if (bOn) Lit |= 1u << Lamp; };
        Set(TurnLeft, In.bLeftFlash);
        Set(TurnRight, In.bRightFlash);
        Set(HighBeam, In.bHighBeam);
        if (!In.bIgnition) return Lit;
        if (Finite(In.IgnitionSeconds) < BulbCheckS) return Lit | ((1u << WarningCount) - 1u);
        Set(Oil, !In.bRunning || Finite(In.OilL) < LowOilL);
        Set(Battery, !In.bRunning || !In.bCharging || Finite(In.Charge) < 0.2);
        Set(Brake, In.bHandbrake);
        Set(Belt, Finite(In.IgnitionSeconds) < BeltReminderS);
        return Lit;
    }

    // The gear lever's lean in the 02J box's H-gate (degrees of its rig: + forward, + to the right): first and
    // second on the left, third and fourth in the middle, fifth and reverse on the right; neutral upright.
    struct FLeverPose { double ForeDeg = 0.0; double AcrossDeg = 0.0; };
    inline FLeverPose GearLever(int Gear)
    {
        switch (Gear)
        {
        case 1: return { 11.0, -9.0 };
        case 2: return { -11.0, -9.0 };
        case 3: return { 11.0, 0.0 };
        case 4: return { -11.0, 0.0 };
        case 5: return { 11.0, 9.0 };
        case -1: return { -11.0, 9.0 };
        default: return { 0.0, 0.0 };
        }
    }

    // Lamp brightness (the lamps' EmissiveStrength): a lamp lit from a weak battery glows dimmer.
    constexpr double TailGlow = 4.0;
    constexpr double BrakeGlow = 14.0;
    constexpr double HeadGlow = 10.0;
    constexpr double IndicatorGlow = 12.0;
    inline double SupplyScale(double Charge) { return 0.35 + 0.65 * std::clamp(Finite(Charge), 0.0, 1.0); }
}
