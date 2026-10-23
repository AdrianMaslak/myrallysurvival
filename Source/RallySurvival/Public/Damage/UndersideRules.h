#pragma once
#include <algorithm>
#include <cmath>

// What lives under the floor (docs/DAMAGE.md, "Underside: sump, fuel tank, exhaust, gearbox"): oil, fuel,
// the exhaust and the gearbox, and how hits on the floor reach them. Health 0..1 as in
// PartConditionRules.h; litres, seconds, rpm. Prototype tuning, no engine dependencies; shared with
// standalone tests.
namespace Rally::Underside
{
    inline double Clamp01(double H) { return std::isfinite(H) ? std::clamp(H, 0.0, 1.0) : 1.0; }

    // Oil leaks faster while running. EngineRules models lubrication, pressure and bearing wear.
    constexpr double OilCapacityL = 4.5;
    inline double OilLeakRate(double SumpHealth, bool bRunning)
    {
        const double Hole = std::clamp((0.8 - Clamp01(SumpHealth)) / 0.8, 0.0, 1.0);
        return 0.05 * Hole * Hole * (bRunning ? 1.0 : 0.4);
    }
    // Fuel: about 0.8 L an hour idling plus a third of a litre per kWh of work; a holed tank leaks on top.
    constexpr double FuelCapacityL = 43.0;
    inline double FuelUse(double PowerKw, bool bRunning)
    {
        const double Power = std::isfinite(PowerKw) ? std::clamp(PowerKw, 0.0, 500.0) : 0.0;
        return bRunning ? (0.8 + 0.33 * Power) / 3600.0 : 0.0;
    }
    inline double FuelLeakRate(double TankHealth)
    {
        const double Hole = std::clamp((0.7 - Clamp01(TankHealth)) / 0.7, 0.0, 1.0);
        return 0.03 * Hole * Hole;
    }

    // Exhaust: below this it has torn from its rear hangers, hangs from the front and drags its silencer.
    constexpr double ExhaustHangHealth = 0.35;
    inline bool ExhaustHanging(double ExhaustHealth) { return Clamp01(ExhaustHealth) < ExhaustHangHealth; }
    // Dragging silencer, N.
    constexpr double ExhaustDragN = 350.0;

    // Gearbox: a damaged one jumps out of gear under load now and then; worse, it loses gears from the top
    // (fifth below 0.5, fourth below 0.3, third below 0.15). At 0 nothing engages: service or a tow.
    inline bool HasGear(int Gear, double GearboxHealth)
    {
        const double H = Clamp01(GearboxHealth);
        if (H <= 0.0 || Gear == 0)
            return false;
        if (Gear < 0)
            return true;
        return !(Gear >= 5 && H < 0.5) && !(Gear >= 4 && H < 0.3) && !(Gear >= 3 && H < 0.15);
    }
    // Pop-outs per second under load.
    inline double PopOutRate(double GearboxHealth)
    {
        const double H = Clamp01(GearboxHealth);
        return H <= 0.0 ? 0.0 : 0.25 * std::clamp((0.6 - H) / 0.6, 0.0, 1.0);
    }
    constexpr double PopOutSeconds = 0.6;

    // Where the floor hits land, assembly frame (cm): the sump under the engine, the gearbox casing beside
    // it, the exhaust along the tunnel (front pipe, middle, silencer) and the tank under the rear seat.
    // A floor hit reaches parts within ReachCm of its patch.
    constexpr double ReachCm = 35.0;
    // Health lost from a hit on the floor: DeltaV is the car's upward change of velocity (m/s). Landings
    // that only bottom out the suspension stay below the thresholds.
    inline double Loss(double DeltaV, double From, double Span, double Reach)
    {
        if (!std::isfinite(DeltaV) || DeltaV > 45.0 || !std::isfinite(Reach)) return 0.0;
        return std::clamp((DeltaV - From) / Span, 0.0, 1.0) * std::clamp(Reach, 0.0, 1.0);
    }
    // Tuned on a 33 cm ridge of rock driven over at 32 km/h (1.1-1.3 m/s): the sump cracks a little, the
    // exhaust is half torn off, the tank and gearbox casing are dented.
    inline double SumpDamage(double DeltaV, double Reach) { return Loss(DeltaV, 0.6, 1.6, Reach); }
    inline double GearboxDamage(double DeltaV, double Reach) { return Loss(DeltaV, 1.0, 3.0, Reach); }
    inline double ExhaustDamage(double DeltaV, double Reach) { return Loss(DeltaV, 0.5, 1.2, Reach); }
    inline double TankDamage(double DeltaV, double Reach) { return Loss(DeltaV, 0.8, 2.0, Reach); }
    // A rock or crest presses on a small patch and does full damage; the floor landing flat on the ground
    // (patch half-extents in cm, a whole floor is about 225 x 86) spreads the load and does a quarter.
    inline double Concentration(double HalfLengthCm, double HalfWidthCm)
    {
        if (!std::isfinite(HalfLengthCm) || !std::isfinite(HalfWidthCm)) return 1.0;
        const double Area = 4.0 * std::max(HalfLengthCm, 0.0) * std::max(HalfWidthCm, 0.0);
        return std::clamp(1.25 - Area / 40000.0, 0.25, 1.0);
    }
    // Smallest floor hit worth reporting to the server.
    constexpr double MinDeltaV = 0.4;
}
