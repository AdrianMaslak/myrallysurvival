#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Engine-independent engine, clutch, gearbox and open-differential model. SI units:
// angular velocity in rad/s, torque in Nm, inertia in kg m^2.
namespace Rally::Drivetrain
{
    constexpr double RadPerSecToRpm = 60.0 / (2.0 * 3.14159265358979);

    struct FEngineSpec
    {
        // Full-throttle torque curve; linear between points, clamped at the ends.
        std::vector<double> CurveRpm;
        std::vector<double> CurveTorque;
        double IdleRpm = 900.0;
        double LimiterRpm = 6900.0;
        double Inertia = 0.18;
        double FrictionTorque = 12.0;
        // Closed-throttle engine braking at the limiter, scaled linearly with rpm.
        double EngineBrakeTorque = 45.0;
    };

    struct FGearboxSpec
    {
        std::vector<double> Forward;
        double Reverse = 3.0;
        double FinalDrive = 4.0;
        double Efficiency = 0.9;
        double ShiftTime = 0.25;
        double UpshiftRpm = 6200.0;
        double DownshiftRpm = 3000.0;
        double ClutchMaxTorque = 350.0;
        // Implicitly integrated, so it can be stiff; it only sets how fast slip closes.
        double ClutchStiffness = 400.0;
    };

    inline double FullThrottleTorque(const FEngineSpec& Engine, double Rpm)
    {
        const auto& X = Engine.CurveRpm;
        const auto& Y = Engine.CurveTorque;
        if (X.empty() || X.size() != Y.size())
            return 0.0;
        if (Rpm <= X.front())
            return Y.front();
        for (size_t I = 1; I < X.size(); ++I)
        {
            if (Rpm <= X[I])
                return Y[I - 1] + (Y[I] - Y[I - 1]) * (Rpm - X[I - 1]) / std::max(X[I] - X[I - 1], 1e-6);
        }
        return Y.back();
    }

    // Net crank torque: throttle torque, idle governor, rev limiter, engine braking and friction.
    inline double EngineTorque(const FEngineSpec& Engine, double Omega, double Throttle)
    {
        const double Rpm = Omega * RadPerSecToRpm;
        double Effective = std::clamp(Throttle, 0.0, 1.0);
        if (Rpm < Engine.IdleRpm)
            Effective = std::max(Effective, std::clamp((Engine.IdleRpm - Rpm) / 150.0, 0.0, 1.0) * 0.5);
        if (Rpm > Engine.LimiterRpm)
            Effective = 0.0;
        const double Drive = Effective * FullThrottleTorque(Engine, Rpm);
        const double Braking = (1.0 - Effective) * Engine.EngineBrakeTorque * std::clamp(Rpm / Engine.LimiterRpm, 0.0, 1.5);
        return Drive - Braking - (Omega > 0.0 ? Engine.FrictionTorque : 0.0);
    }

    // Overall ratio from engine to wheel; reverse is negative, neutral zero.
    inline double Ratio(const FGearboxSpec& Box, int Gear)
    {
        if (Gear > 0 && Gear <= static_cast<int>(Box.Forward.size()))
            return Box.Forward[Gear - 1] * Box.FinalDrive;
        if (Gear < 0)
            return -Box.Reverse * Box.FinalDrive;
        return 0.0;
    }

    struct FShiftState
    {
        int Gear = 1;
        int PendingGear = 1;
        double ShiftTimer = 0.0;

        bool IsShifting() const { return ShiftTimer > 0.0; }
        // Gear coupled to the wheels right now; neutral while shifting.
        int EngagedGear() const { return IsShifting() ? 0 : Gear; }
    };

    // Shift points scale with throttle like a road automatic: early shifts when cruising, near the
    // limiter at full throttle. Down-shift points stay below the rpm reached after an up-shift.
    inline double UpshiftRpmAt(const FGearboxSpec& Box, double Throttle)
    {
        return Box.UpshiftRpm * (0.55 + 0.45 * std::clamp(Throttle, 0.0, 1.0));
    }

    inline double DownshiftRpmAt(const FGearboxSpec& Box, double Throttle)
    {
        return Box.DownshiftRpm * (0.6 + 0.4 * std::clamp(Throttle, 0.0, 1.0));
    }

    // Automatic box. Shift points use ground speed, so wheelspin cannot trigger upshifts.
    inline void UpdateAutomatic(const FGearboxSpec& Box, FShiftState& State, double DrivenGroundOmega, double Throttle, bool bReverse, double Dt)
    {
        if (State.IsShifting())
        {
            State.ShiftTimer -= Dt;
            if (!State.IsShifting())
                State.Gear = State.PendingGear;
            return;
        }
        int Target = State.Gear;
        if (bReverse)
            Target = -1;
        else if (State.Gear <= 0)
            Target = 1;
        else
        {
            const auto GroundRpm = [&](int Gear) { return std::abs(DrivenGroundOmega * Ratio(Box, Gear)) * RadPerSecToRpm; };
            const double Up = UpshiftRpmAt(Box, Throttle);
            if (State.Gear < static_cast<int>(Box.Forward.size()) && GroundRpm(State.Gear) > Up)
                Target = State.Gear + 1;
            else if (State.Gear > 1 && GroundRpm(State.Gear) < DownshiftRpmAt(Box, Throttle) && GroundRpm(State.Gear - 1) < Up * 0.9)
                Target = State.Gear - 1;
        }
        if (Target != State.Gear)
        {
            State.PendingGear = Target;
            State.ShiftTimer = Box.ShiftTime;
        }
    }

    // Auto clutch: open in neutral or while shifting. Below lock-up it bites in proportion to how far the
    // engine has revved above idle, like a driver slipping the clutch: a full-throttle launch holds about
    // 2200 rpm, and at idle the clutch stays open so the car neither stalls nor creeps.
    inline double ClutchEngagement(const FEngineSpec& Engine, const FGearboxSpec& Box, const FShiftState& State,
        double DrivenOmega, double EngineOmega)
    {
        const int Gear = State.EngagedGear();
        if (Gear == 0)
            return 0.0;
        const double LockRpm = Engine.IdleRpm * 1.6;
        const double TransmissionRpm = std::abs(DrivenOmega * Ratio(Box, Gear)) * RadPerSecToRpm;
        if (TransmissionRpm >= LockRpm)
            return 1.0;
        const double BiteStart = Engine.IdleRpm * 1.05;
        return std::clamp((EngineOmega * RadPerSecToRpm - BiteStart) / 2000.0, 0.0, 1.0);
    }

    // Traction control throttle factor: full throttle up to 1.5 x peak slip, cut to 10% by 2.5 x peak.
    inline double TractionControlFactor(double DrivenSlipRatio, double PeakSlipRatio)
    {
        const double Peak = std::max(PeakSlipRatio, 0.01);
        return std::clamp(1.0 - (DrivenSlipRatio - 1.5 * Peak) / Peak, 0.1, 1.0);
    }

    // Brake torque stops a wheel but never spins it backwards.
    inline double ApplyBrake(double Omega, double BrakeTorque, double Inertia, double Dt)
    {
        const double Change = std::max(BrakeTorque, 0.0) * Dt / Inertia;
        return std::abs(Omega) <= Change ? 0.0 : Omega - std::copysign(Change, Omega);
    }

    struct FAxleStep
    {
        double Throttle = 0.0;
        double ClutchEngagement = 0.0;
        int Gear = 0;
        // Ground reaction torque on each driven wheel (Nm, positive accelerates spin), brakes excluded.
        double GroundTorqueLeft = 0.0;
        double GroundTorqueRight = 0.0;
        double BrakeLeft = 0.0;
        double BrakeRight = 0.0;
        double WheelInertia = 1.2;
        double Dt = 1.0 / 480.0;
    };

    // One substep of engine, clutch and open differential (equal torque to both driven wheels).
    // The clutch coupling is solved implicitly; returns the clutch torque.
    inline double StepDrivenAxle(const FEngineSpec& Engine, const FGearboxSpec& Box, const FAxleStep& In,
        double& EngineOmega, double& OmegaLeft, double& OmegaRight)
    {
        const double Ie = Engine.Inertia;
        const double Id = 2.0 * In.WheelInertia;
        const double G = Ratio(Box, In.Gear);
        const double Eta = Box.Efficiency;
        const double Te = EngineTorque(Engine, EngineOmega, In.Throttle);
        const double Tau = In.GroundTorqueLeft + In.GroundTorqueRight;
        double Average = 0.5 * (OmegaLeft + OmegaRight);
        double Half = 0.5 * (OmegaLeft - OmegaRight);

        double Clutch = 0.0;
        if (G != 0.0 && In.ClutchEngagement > 0.0)
        {
            const double Kc = Box.ClutchStiffness;
            const double Denominator = 1.0 + Kc * In.Dt * (1.0 / Ie + Eta * G * G / Id);
            Clutch = Kc * ((EngineOmega - G * Average) + In.Dt * (Te / Ie - G * Tau / Id)) / Denominator;
            const double Capacity = In.ClutchEngagement * Box.ClutchMaxTorque;
            Clutch = std::clamp(Clutch, -Capacity, Capacity);
        }

        EngineOmega = std::max(0.0, EngineOmega + In.Dt * (Te - Clutch) / Ie);
        Average += In.Dt * (Eta * G * Clutch + Tau) / Id;
        Half += In.Dt * (In.GroundTorqueLeft - In.GroundTorqueRight) / Id;
        OmegaLeft = ApplyBrake(Average + Half, In.BrakeLeft, In.WheelInertia, In.Dt);
        OmegaRight = ApplyBrake(Average - Half, In.BrakeRight, In.WheelInertia, In.Dt);
        return Clutch;
    }

    inline double StepFreeWheel(double Omega, double GroundTorque, double Brake, double Inertia, double Dt)
    {
        return ApplyBrake(Omega + Dt * GroundTorque / Inertia, Brake, Inertia, Dt);
    }
}
