#pragma once
#include <algorithm>
#include <cmath>

// How the engine moves on its mounts, seen with the bonnet up (docs/OCTAVIA-VEHICLE.md, "Engine"): it rolls
// about the crank against its own torque and rocks back when the throttle lifts, buzzes at idle, jolts as it
// catches and shudders to a stop, and runs rough when worn or knocking. Degrees, seconds, N m. Visual only:
// the car's physics never sees it. Prototype tuning, no engine dependencies; shared with standalone tests.
namespace Rally::EngineMotion
{
    // The engine on its rubber mounts: a spring about the crank at RollHz, damped to RollDamping of critical,
    // so a throttle blip overshoots and settles in a couple of swings.
    constexpr double RollHz = 3.2;
    constexpr double RollDamping = 0.35;
    // Full torque (about 210 N m) leans it 3 degrees against the crank's turn.
    constexpr double DegPerNm = 3.0 / 210.0;
    // Catching, it kicks over this fast (deg/s); stopping, it shudders back.
    constexpr double CatchKick = 60.0;
    constexpr double StopKick = -45.0;
    constexpr double MaxRollDeg = 8.0;

    struct FState
    {
        double Roll = 0.0;
        double RollRate = 0.0;
        // Where the crank pulley has turned to.
        double CrankDeg = 0.0;
        bool bWasRunning = false;
    };

    // Torque at the crank (N m) from power and speed.
    inline double TorqueNm(double PowerKw, double Rpm)
    {
        if (!std::isfinite(PowerKw) || !std::isfinite(Rpm) || Rpm < 100.0)
            return 0.0;
        return PowerKw * 1000.0 / (Rpm * 2.0 * 3.14159265358979 / 60.0);
    }

    // One step: the lean it springs towards (torque while running, nothing stopped), a kick as it catches or
    // stops, and Jolt (deg/s) added now (a misfire). The crank pulley turns with the engine.
    inline void Step(FState& S, bool bRunning, double Rpm, double Torque, double Jolt, double Dt)
    {
        if (!(Dt > 0.0) || !std::isfinite(Dt))
            return;
        if (bRunning != S.bWasRunning)
        {
            S.RollRate += bRunning ? CatchKick : StopKick;
            S.bWasRunning = bRunning;
        }
        if (std::isfinite(Jolt))
            S.RollRate += Jolt;
        const double Target = bRunning && std::isfinite(Torque) ? DegPerNm * std::clamp(Torque, -100.0, 400.0) : 0.0;
        const double W = 2.0 * 3.14159265358979 * RollHz;
        // Semi-implicit Euler: steady for any frame time the game runs at.
        const double Steps = std::ceil(Dt / 0.005);
        const double H = Dt / Steps;
        for (int I = 0; I < static_cast<int>(Steps); ++I)
        {
            S.RollRate += (-W * W * (S.Roll - Target) - 2.0 * RollDamping * W * S.RollRate) * H;
            S.Roll += S.RollRate * H;
        }
        S.Roll = std::clamp(S.Roll, -MaxRollDeg, MaxRollDeg);
        if (std::isfinite(Rpm))
            S.CrankDeg = std::fmod(S.CrankDeg + std::max(Rpm, 0.0) * 6.0 * Dt, 360.0);
    }

    // How rough it runs, 0..1: from wear below 70% health and from knocking (Engine::Detonation 0..1).
    inline double Roughness(double EngineHealth, double Detonation)
    {
        const double Worn = std::isfinite(EngineHealth) ? std::clamp((0.7 - EngineHealth) / 0.7, 0.0, 1.0) : 0.0;
        const double Knock = std::isfinite(Detonation) ? std::clamp(Detonation, 0.0, 1.0) : 0.0;
        return std::clamp(Worn + 0.5 * Knock, 0.0, 1.0);
    }

    // The buzz on top (degrees about the crank): a fine one at idle that smooths out as it revs, more the
    // rougher it runs; none stopped.
    inline double BuzzDeg(bool bRunning, double Rpm, double Rough)
    {
        if (!bRunning || !std::isfinite(Rpm))
            return 0.0;
        const double Idle = std::clamp(1.0 - (Rpm - 800.0) / 2500.0, 0.25, 1.0);
        return 0.12 * Idle + 0.5 * std::clamp(Rough, 0.0, 1.0);
    }

    // Misfires a second when rough: none smooth, up to four.
    inline double MisfiresPerSecond(double Rough)
    {
        return 4.0 * std::clamp(Rough - 0.15, 0.0, 1.0);
    }
}
