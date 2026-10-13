#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Grades of part and the settings a hand can change on them (docs/DAMAGE.md, Grades and settings).
// A part's grade travels with it: off one car, into the boot, onto another. No engine dependencies.
namespace Rally::Grade
{
    enum class EGrade : uint8_t { Worn, Stock, Sport, Rally };

    inline const char* GradeName(EGrade Grade)
    {
        switch (Grade)
        {
        case EGrade::Worn: return "worn";
        case EGrade::Sport: return "sport";
        case EGrade::Rally: return "rally";
        default: return "";
        }
    }

    // A strut by grade: spring and damping against the factory part, and how much lower it holds the car.
    struct FStrut { double Spring, Damping, LowerCm; };
    inline FStrut StrutGrade(EGrade Grade)
    {
        switch (Grade)
        {
        case EGrade::Worn: return { 0.92, 0.7, -0.5 };
        case EGrade::Sport: return { 1.3, 1.25, 2.0 };
        case EGrade::Rally: return { 1.5, 1.35, 1.0 };
        default: return { 1.0, 1.0, 0.0 };
        }
    }
    // Rally struts are adjustable: damper clicks 1 (soft) to 9 (hard), 5 as delivered; a collar sets the
    // height up to 3 cm either way.
    constexpr int DefaultClicks = 5;
    constexpr int MaxClicks = 9;
    constexpr int MaxCollarCm = 3;
    inline double ClickDamping(int Clicks)
    {
        return 0.75 + 0.0625 * (std::clamp(Clicks, 1, MaxClicks) - 1);
    }

    // Brake torque by disc and caliper grade.
    inline double BrakeGrade(EGrade Grade)
    {
        switch (Grade)
        {
        case EGrade::Worn: return 0.8;
        case EGrade::Sport: return 1.2;
        case EGrade::Rally: return 1.35;
        default: return 1.0;
        }
    }

    // Power by turbo and by engine computer (its chip) grade.
    inline double TurboGrade(EGrade Grade)
    {
        switch (Grade)
        {
        case EGrade::Worn: return 0.92;
        case EGrade::Sport: return 1.12;
        case EGrade::Rally: return 1.25;
        default: return 1.0;
        }
    }
    inline double EcuGrade(EGrade Grade)
    {
        switch (Grade)
        {
        case EGrade::Worn: return 0.95;
        case EGrade::Sport: return 1.06;
        case EGrade::Rally: return 1.1;
        default: return 1.0;
        }
    }

    // The engine computer's map, chosen with a screwdriver at the computer: 1 economy, 2 as delivered,
    // 3 sport. Power, fuel used, and the boost the map tolerates before knocking (bar, added).
    constexpr int DefaultMap = 2;
    struct FMap { double Power, Fuel, SafeBoost; const char* Name; };
    inline FMap EngineMap(int Map)
    {
        switch (std::clamp(Map, 1, 3))
        {
        case 1: return { 0.92, 0.85, 0.1, "economy" };
        case 3: return { 1.05, 1.15, -0.1, "sport" };
        default: return { 1.0, 1.0, 0.0, "as delivered" };
        }
    }

    // Brake bias, the front's share of the braking, set from the driver's seat: as built 62 percent (the
    // calipers' sizes), 2 percent a step from 50 to 76.
    constexpr double BuiltBiasPct = 62.5;
    constexpr int MaxBiasSteps = 6;
    inline double BiasPct(int Steps) { return BuiltBiasPct + 2.0 * std::clamp(Steps, -MaxBiasSteps - 0, MaxBiasSteps); }
    inline double BiasScale(int Steps, bool bFront)
    {
        const double Bias = BiasPct(Steps);
        return bFront ? Bias / BuiltBiasPct : (100.0 - Bias) / (100.0 - BuiltBiasPct);
    }

    // Boost (bar) set at the turbo's wastegate, 0.5 to 1.4 in tenths, 0.8 as delivered. Power follows it;
    // past what the computer's fuel map allows, the engine knocks and wears under load.
    constexpr int DefaultBoostTenths = 8;
    constexpr int MinBoostTenths = 5;
    constexpr int MaxBoostTenths = 14;
    inline double BoostPower(int Tenths)
    {
        return 1.0 + (std::clamp(Tenths, MinBoostTenths, MaxBoostTenths) - DefaultBoostTenths) * 0.05;
    }
    inline double SafeBoost(EGrade Ecu)
    {
        return Ecu == EGrade::Rally ? 1.3 : Ecu == EGrade::Sport ? 1.1 : 0.9;
    }
    // Engine health lost per second of knocking at full load (Load 0..1).
    inline double KnockWear(int Tenths, EGrade Ecu, double Load, int Map = DefaultMap)
    {
        if (!std::isfinite(Load)) return 0.0;
        const double Over = std::clamp(Tenths, MinBoostTenths, MaxBoostTenths) * 0.1 - SafeBoost(Ecu) - EngineMap(Map).SafeBoost;
        return Over > 0.001 ? Over * std::clamp(Load, 0.0, 1.0) * 0.01 : 0.0;
    }
}
