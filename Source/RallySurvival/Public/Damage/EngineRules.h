#pragma once
#include <algorithm>
#include <cmath>

// Cooling, engine wear and electrics (docs/DAMAGE.md, "Engine, cooling and battery"). Health 0..1 as in
// PartConditionRules.h; temperatures in degrees C, heat in kW, coolant in litres. Prototype tuning, no
// engine dependencies; shared with standalone tests.
//
// The engine heats its block and coolant with what it burns; the radiator gets rid of it through the
// thermostat, with air from the car's speed or the fans. A damaged radiator cools less and leaks; once the
// coolant is low the pump loses it and the radiator stops working. Above WearStartC the engine wears,
// losing power until it seizes.
namespace Rally::Engine
{
    inline double Clamp01(double H) { return std::isfinite(H) ? std::clamp(H, 0.0, 1.0) : 1.0; }

    constexpr double AmbientC = 20.0;
    constexpr double CoolantCapacityL = 6.0;
    // Where the engine settles on the road; a fresh car starts here.
    constexpr double WarmC = 85.0;
    constexpr double ThermostatOpenC = 82.0;
    constexpr double ThermostatFullC = 94.0;
    constexpr double FanOnC = 98.0;
    constexpr double WearStartC = 112.0;
    constexpr double BoilC = 118.0;
    constexpr double MaxC = 160.0;

    // Heat into the engine: some just to keep running, and three quarters of the power it makes.
    constexpr double IdleHeatKw = 6.0;
    constexpr double HeatPerPowerKw = 0.75;
    // Heat capacity of the block, head and oil, and of each litre of coolant (kJ per degree).
    constexpr double BlockHeatCapacity = 50.0;
    constexpr double CoolantHeatCapacityPerL = 4.0;
    // kW per degree above ambient: a healthy radiator wide open in full airflow, and the bare block.
    constexpr double RadiatorKwPerC = 2.5;
    constexpr double BlockKwPerC = 0.08;

    // Air through the radiator, 0..1: rises with road speed; the fans give a parked car half when hot.
    inline double Airflow(double SpeedMs, double TempC)
    {
        const double Ram = std::clamp(0.15 + std::abs(std::isfinite(SpeedMs) ? SpeedMs : 0.0) / 25.0, 0.15, 1.0);
        return TempC > FanOnC ? std::max(Ram, 0.5) : Ram;
    }
    inline double ThermostatOpening(double TempC)
    {
        return std::clamp((TempC - ThermostatOpenC) / (ThermostatFullC - ThermostatOpenC), 0.03, 1.0);
    }
    // A crushed core passes less air and coolant.
    inline double CoreEfficiency(double RadiatorHealth) { return 0.3 + 0.7 * Clamp01(RadiatorHealth); }
    // Below 70% full the pump starts drawing air; below 30% nothing reaches the radiator.
    inline double CoolantFlow(double CoolantL)
    {
        return std::clamp((CoolantL / CoolantCapacityL - 0.3) / 0.4, 0.0, 1.0);
    }

    // Coolant lost through a damaged radiator (L/s). Minor knocks do not leak; at 0 a full system empties in
    // about a minute. A cold or stopped engine is not under pressure and only drips.
    inline double LeakRate(double RadiatorHealth, double TempC, bool bRunning)
    {
        const double Hole = std::clamp((0.75 - Clamp01(RadiatorHealth)) / 0.75, 0.0, 1.0);
        return 0.1 * Hole * Hole * (bRunning && TempC > 70.0 ? 1.0 : 0.3);
    }
    // Boiling coolant blows out through the cap as steam (L/s).
    inline double BoilOffRate(double TempC, double CoolantL)
    {
        return CoolantL > 0.0 ? 0.03 * std::clamp((TempC - BoilC) / 20.0, 0.0, 1.0) : 0.0;
    }
    // Engine health lost per second of running this hot: about a minute from the red to seized.
    inline double WearRate(double TempC)
    {
        const double Over = std::clamp((TempC - WearStartC) / 20.0, 0.0, 3.0);
        return 0.012 * Over * Over;
    }

    // Share of full torque: a worn engine loses compression, and an overheating one knocks and is held
    // back.
    inline double PowerScale(double EngineHealth, double TempC)
    {
        const double Hot = std::isfinite(TempC) ? std::clamp((TempC - 125.0) / 25.0, 0.0, 1.0) : 0.0;
        return (0.4 + 0.6 * Clamp01(EngineHealth)) * (1.0 - 0.6 * Hot);
    }
    // Internal friction of a worn engine, as a multiple of a healthy one's.
    inline double FrictionScale(double EngineHealth) { return 1.0 + 1.5 * (1.0 - Clamp01(EngineHealth)); }

    struct FThermal
    {
        double TempC = WarmC;
        double CoolantL = CoolantCapacityL;
    };

    struct FThermalInput
    {
        bool bRunning = false;
        // Power the engine makes at the crank now (kW), PowerScale included.
        double PowerKw = 0.0;
        double SpeedMs = 0.0;
        double RadiatorHealth = 1.0;
    };

    // Advances temperature and coolant by Dt seconds; returns the engine health worn away.
    inline double Step(FThermal& State, const FThermalInput& In, double Dt)
    {
        if (!std::isfinite(Dt) || Dt <= 0.0)
            return 0.0;
        if (!std::isfinite(State.TempC)) State.TempC = WarmC;
        if (!std::isfinite(State.CoolantL)) State.CoolantL = CoolantCapacityL;
        const double Power = In.bRunning && std::isfinite(In.PowerKw) ? std::clamp(In.PowerKw, 0.0, 500.0) : 0.0;
        const double HeatIn = In.bRunning ? IdleHeatKw + HeatPerPowerKw * Power : 0.0;
        // With the engine off the pump stops; only the fans and convection move heat through the radiator.
        const double Radiator = RadiatorKwPerC * Airflow(In.SpeedMs, State.TempC) * ThermostatOpening(State.TempC)
            * CoreEfficiency(In.RadiatorHealth) * CoolantFlow(State.CoolantL) * (In.bRunning ? 1.0 : 0.15);
        const double HeatOut = (Radiator + BlockKwPerC) * (State.TempC - AmbientC);
        const double Capacity = BlockHeatCapacity + CoolantHeatCapacityPerL * State.CoolantL;
        State.TempC = std::clamp(State.TempC + (HeatIn - HeatOut) / Capacity * Dt, AmbientC - 30.0, MaxC);
        const double Lost = (LeakRate(In.RadiatorHealth, State.TempC, In.bRunning) + BoilOffRate(State.TempC, State.CoolantL)) * Dt;
        State.CoolantL = std::max(State.CoolantL - Lost, 0.0);
        return In.bRunning ? WearRate(State.TempC) * Dt : 0.0;
    }

    // Pours up to Litres of water at WaterC in; it mixes with what is there and cools a hot engine.
    // Returns the litres used.
    inline double TopUp(FThermal& State, double Litres, double WaterC = 15.0)
    {
        const double Room = CoolantCapacityL - State.CoolantL;
        const double Used = std::isfinite(Litres) ? std::clamp(Litres, 0.0, std::max(Room, 0.0)) : 0.0;
        if (Used <= 0.0)
            return 0.0;
        const double Capacity = BlockHeatCapacity + CoolantHeatCapacityPerL * State.CoolantL;
        const double Added = CoolantHeatCapacityPerL * Used;
        State.TempC = (State.TempC * Capacity + WaterC * Added) / (Capacity + Added);
        State.CoolantL += Used;
        return Used;
    }

    // The battery and its wiring. Damaged, it loses contact now and then and the engine cuts out for a
    // moment; at 0 the engine stops and will not start.
    inline bool HasPower(double BatteryHealth) { return Clamp01(BatteryHealth) > 0.0; }
    // Cut-outs per second.
    inline double CutOutRate(double BatteryHealth)
    {
        const double H = Clamp01(BatteryHealth);
        return H <= 0.0 ? 0.0 : 0.6 * std::clamp((0.5 - H) / 0.5, 0.0, 1.0);
    }
    constexpr double CutOutSeconds = 0.4;
    inline bool CanRun(double EngineHealth, double BatteryHealth)
    {
        return Clamp01(EngineHealth) > 0.0 && HasPower(BatteryHealth);
    }

    // Health lost by the engine and battery from an impact (change of velocity, m/s; Reach 0..1 from
    // ImpactRules ReachFactor). Castings take a very hard hit; the battery sits behind the left headlamp.
    inline double Loss(double DeltaV, double From, double Span, double Reach)
    {
        if (!std::isfinite(DeltaV) || DeltaV < 2.0 || DeltaV > 45.0 || !std::isfinite(Reach)) return 0.0;
        return std::clamp((DeltaV - From) / Span, 0.0, 1.0) * std::clamp(Reach, 0.0, 1.0);
    }
    inline double EngineDamage(double DeltaV, double Reach) { return Loss(DeltaV, 10.0, 25.0, Reach); }
    // Bolt-on engine modules near an impact (turbo, intercooler, alternator...).
    inline double ModuleDamage(double DeltaV, double Reach) { return Loss(DeltaV, 6.0, 20.0, Reach); }
    constexpr double ModuleReachCm = 30.0;

    // The engine's bolt-on modules as it feels them: each one's health, 0 when missing (or its fixings
    // all out), 1 when fitted and sound.
    struct FModules
    {
        double AirFilter = 1.0, Intake = 1.0, ExhaustManifold = 1.0, Turbo = 1.0, Intercooler = 1.0;
        double Alternator = 1.0, Starter = 1.0, Ecu = 1.0;
        // From the turbo's and computer's grades and the boost set (Rally::Grade): power against the
        // factory engine, and engine health lost per second of knocking at full load.
        double Tune = 1.0;
        double KnockPerSecond = 0.0;
    };
    // The starter turns it over at all.
    inline bool CanCrank(const FModules& M) { return Clamp01(M.Starter) > 0.0; }
    // It catches and keeps running: air in through the intake manifold, the computer firing the injectors.
    inline bool Breathes(const FModules& M) { return Clamp01(M.Intake) > 0.0 && Clamp01(M.Ecu) > 0.0; }
    // Share of full power from induction and exhaust. No turbo, it runs on atmospheric pressure (about two
    // thirds); no intercooler, the boost leaks out of the open pipe; no exhaust manifold, the gas blows past
    // the turbine; a damaged part loses in proportion.
    inline double ModulePower(const FModules& M)
    {
        const double Turbo = 0.65 + 0.35 * Clamp01(M.Turbo);
        const double Cooler = Clamp01(M.Turbo) > 0.0 ? 0.85 + 0.15 * Clamp01(M.Intercooler) : 1.0;
        const double Manifold = 0.8 + 0.2 * Clamp01(M.ExhaustManifold);
        const double Intake = 0.6 + 0.4 * Clamp01(M.Intake);
        const double Tune = std::isfinite(M.Tune) ? std::clamp(M.Tune, 0.5, 2.0) : 1.0;
        return Turbo * Cooler * Manifold * Intake * Tune;
    }
    // Battery charge (0..1) per second while running: the alternator charges it; without a working one the
    // ignition and fuel pump drain it, flat in about ten minutes.
    inline double ChargeRate(const FModules& M, bool bRunning)
    {
        if (!bRunning) return 0.0;
        return Clamp01(M.Alternator) > 0.3 ? 1.0 / 240.0 : -1.0 / 600.0;
    }
    // Below this the battery cannot turn the starter or keep the ignition alive.
    constexpr double FlatCharge = 0.05;
    inline double BatteryDamage(double DeltaV, double Reach) { return Loss(DeltaV, 6.0, 18.0, Reach); }
    constexpr double EngineReachCm = 40.0;
    constexpr double BatteryReachCm = 30.0;
}
