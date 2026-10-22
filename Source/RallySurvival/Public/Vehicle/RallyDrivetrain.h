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
        // Set by damage (Damage/EngineRules.h): share of full torque left, and whether it is running at
        // all. A stopped engine makes no torque, has no idle governor and leaves the clutch open.
        double TorqueScale = 1.0;
        bool bRunning = true;
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
        // Set by damage (Damage/UndersideRules.h): gears that no longer engage, bit 0 reverse, bit N gear N.
        unsigned LostGears = 0;
    };

    inline bool HasGear(const FGearboxSpec& Box, int Gear)
    {
        if (Gear == 0 || Gear > static_cast<int>(Box.Forward.size()))
            return false;
        return (Box.LostGears & (1u << (Gear < 0 ? 0 : Gear))) == 0;
    }

    // Nearest forward gear that engages, searching from Gear in Step (+1 up, -1 down); 0 if none.
    inline int NextGear(const FGearboxSpec& Box, int Gear, int Step)
    {
        for (int Candidate = Gear; Candidate >= 1 && Candidate <= static_cast<int>(Box.Forward.size()); Candidate += Step)
        {
            if (HasGear(Box, Candidate))
                return Candidate;
        }
        return 0;
    }

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
        if (Rpm > Engine.LimiterRpm || !Engine.bRunning)
            Effective = 0.0;
        const double Drive = Effective * FullThrottleTorque(Engine, Rpm) * std::clamp(Engine.TorqueScale, 0.0, 1.0);
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

    // Counts a shift down; the pending gear goes in when it is over.
    inline void TickShift(FShiftState& State, double Dt)
    {
        if (!State.IsShifting())
            return;
        State.ShiftTimer -= Dt;
        if (!State.IsShifting())
            State.Gear = State.PendingGear;
    }

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

    // Automatic box. Shift points use ground speed, so wheelspin cannot trigger upshifts. Gears that no
    // longer engage are skipped; with none left it sits in neutral.
    inline void UpdateAutomatic(const FGearboxSpec& Box, FShiftState& State, double DrivenGroundOmega, double Throttle, bool bReverse, double Dt)
    {
        if (State.IsShifting())
        {
            TickShift(State, Dt);
            return;
        }
        int Target = State.Gear;
        if (bReverse)
            Target = HasGear(Box, -1) ? -1 : 0;
        else if (State.Gear <= 0)
            Target = NextGear(Box, 1, 1);
        else if (!HasGear(Box, State.Gear))
        {
            // Just lost the gear it was in: the nearest one below, else above.
            Target = NextGear(Box, State.Gear, -1);
            if (Target == 0)
                Target = NextGear(Box, State.Gear, 1);
        }
        else
        {
            const auto GroundRpm = [&](int Gear) { return std::abs(DrivenGroundOmega * Ratio(Box, Gear)) * RadPerSecToRpm; };
            const double Up = UpshiftRpmAt(Box, Throttle);
            const int Higher = NextGear(Box, State.Gear + 1, 1);
            const int Lower = NextGear(Box, State.Gear - 1, -1);
            if (Higher != 0 && GroundRpm(State.Gear) > Up)
                Target = Higher;
            else if (Lower != 0 && GroundRpm(State.Gear) < DownshiftRpmAt(Box, Throttle) && GroundRpm(Lower) < Up * 0.9)
                Target = Lower;
        }
        if (Target != State.Gear)
        {
            State.PendingGear = Target;
            State.ShiftTimer = Box.ShiftTime;
        }
    }

    // Manual box (the player's): sequential, one gear a press, neutral between first and reverse. Reverse
    // goes in only below ReverseSelectKmh going forward; otherwise the lever stops in neutral.
    constexpr double ReverseSelectKmh = 5.0;
    inline int SelectGear(int Selected, int Step, int TopGear, double ForwardKmh)
    {
        const int Next = std::clamp(Selected + (Step > 0 ? 1 : Step < 0 ? -1 : 0), -1, std::max(TopGear, 1));
        return Next < 0 && ForwardKmh > ReverseSelectKmh ? 0 : Next;
    }

    // A gear the road would spin the engine more than 5% past its limiter in (a missed downshift) will not
    // go in: the lever stays where it was and the box crunches. WheelOmega is the driven wheels' (rad/s).
    inline bool WouldOverRev(const FEngineSpec& Engine, const FGearboxSpec& Box, int Gear, double WheelOmega)
    {
        if (Gear == 0 || !std::isfinite(WheelOmega)) return false;
        return std::abs(WheelOmega * Ratio(Box, Gear)) * RadPerSecToRpm > Engine.LimiterRpm * 1.05;
    }

    // The selected gear goes in after ShiftTime in neutral, with or without the clutch pedal; another
    // press meanwhile retargets the same shift. Out of gear (neutral) is at once, and a gear that no
    // longer engages leaves the box in neutral.
    inline void UpdateManual(const FGearboxSpec& Box, FShiftState& State, int Selected)
    {
        const int Target = HasGear(Box, Selected) ? Selected : 0;
        if (State.IsShifting())
        {
            State.PendingGear = Target;
            return;
        }
        if (Target == State.Gear)
            return;
        State.PendingGear = Target;
        if (Target == 0)
            State.Gear = 0;
        else
            State.ShiftTimer = Box.ShiftTime;
    }

    // The clutch pedal, 0 up to 1 down, on a key: down in 0.1 s, back up in 0.35 s, so letting it go bites
    // over a moment rather than at once.
    inline double MovePedal(double Pedal, bool bHeld, double Dt)
    {
        const double P = std::isfinite(Pedal) ? std::clamp(Pedal, 0.0, 1.0) : 0.0;
        return bHeld ? std::min(1.0, P + Dt / 0.1) : std::max(0.0, P - Dt / 0.35);
    }

    // Auto clutch: open in neutral or while shifting. Below lock-up it bites in proportion to how far the
    // engine has revved above idle, like a driver slipping the clutch: a full-throttle launch holds about
    // 2200 rpm, and at idle the clutch stays open so the car neither stalls nor creeps.
    inline double ClutchEngagement(const FEngineSpec& Engine, const FGearboxSpec& Box, const FShiftState& State,
        double DrivenOmega, double EngineOmega)
    {
        const int Gear = State.EngagedGear();
        if (Gear == 0 || !Engine.bRunning)
            return 0.0;
        const double LockRpm = Engine.IdleRpm * 1.6;
        const double TransmissionRpm = std::abs(DrivenOmega * Ratio(Box, Gear)) * RadPerSecToRpm;
        if (TransmissionRpm >= LockRpm)
            return 1.0;
        const double BiteStart = Engine.IdleRpm * 1.05;
        return std::clamp((EngineOmega * RadPerSecToRpm - BiteStart) / 2000.0, 0.0, 1.0);
    }

    // The clutch pedal opens the clutch whatever the auto clutch would do; left up, the auto clutch
    // still keeps the car from stalling or creeping, so the pedal is never needed.
    inline double PedalEngagement(double AutoEngagement, double Pedal)
    {
        return std::clamp(AutoEngagement, 0.0, 1.0) * (1.0 - (std::isfinite(Pedal) ? std::clamp(Pedal, 0.0, 1.0) : 0.0));
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

    // Gameplay values at the coupling, not measured Haldex calibration: a light preload, full
    // 300 Nm capacity at 29 rad/s slip, and at most half the input to keep the front axle driving.
    // Speeds and torque are oriented in the selected driving direction (negate all three in reverse).
    // Negative input is overrun: no rear engine braking. Rear overspeed fades preload over 1 rad/s;
    // no reverse transfer or separate friction torque is modelled.
    inline double CentreCouplingTorque(double InputTorque, double FrontOmega, double RearOmega)
    {
        const double Demand = std::max(0.0, 10.0 + 10.0 * (FrontOmega - RearOmega));
        return std::min(Demand, std::min(300.0, 0.5 * std::max(InputTorque, 0.0)));
    }

    struct FRearDriveHealth
    {
        // Health in [0, 1]; zero also means missing. Any positive health still transmits fully.
        double FinalDrive = 1.0;
        double LeftShaft = 1.0;
        double RightShaft = 1.0;
    };

    struct FFourWheelDriveTorque
    {
        // All Nm referred to the coupling, before final-drive multiplication and losses.
        double Front = 0.0;
        double RearLeft = 0.0;
        double RearRight = 0.0;
    };

    inline FFourWheelDriveTorque SplitFourWheelDriveTorque(double InputTorque, double FrontOmega,
        double RearOmega, const FRearDriveHealth& Health)
    {
        // An open rear diff with a free shaft carries no load. Stub inertia and drag are omitted.
        const double Rear = Health.FinalDrive > 0.0 && Health.LeftShaft > 0.0 && Health.RightShaft > 0.0
            ? CentreCouplingTorque(InputTorque, FrontOmega, RearOmega) : 0.0;
        // Same equal wheel torque as the open front diff in StepDrivenAxle.
        return { InputTorque - Rear, 0.5 * Rear, 0.5 * Rear };
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

    // Both final drives use Box.FinalDrive. Rear speeds are the diff's sides (a free stub when broken).
    // Solve clutch and centre transfer together at end-of-step speeds, before applying wheel brakes.
    // Returns rear torque at the coupling, in the selected driving direction.
    inline double StepFourWheelDrive(const FEngineSpec& Engine, const FGearboxSpec& Box,
        const FAxleStep& Front, const FAxleStep& Rear, const FRearDriveHealth& Health, bool bHandbrake,
        double& EngineOmega, double& FrontLeft, double& FrontRight, double& RearLeft, double& RearRight)
    {
        const double G = Ratio(Box, Front.Gear);
        const double Direction = G < 0.0 ? -1.0 : 1.0;
        const double Final = Box.FinalDrive;
        const double Id = 2.0 * Front.WheelInertia;
        const double Te = EngineTorque(Engine, EngineOmega, Front.Throttle);
        const double Tau = Front.GroundTorqueLeft + Front.GroundTorqueRight;
        const double Average = 0.5 * (FrontLeft + FrontRight);
        const double Kc = Box.ClutchStiffness;
        const double Denominator = 1.0 + Kc * Front.Dt * (1.0 / Engine.Inertia + Box.Efficiency * G * G / Id);
        const double Numerator = Kc * ((EngineOmega - G * Average) + Front.Dt * (Te / Engine.Inertia - G * Tau / Id));
        const double Capacity = Front.ClutchEngagement * Box.ClutchMaxTorque;
        const auto WheelTransfer = [&](double Transfer) { return Direction * Final * Box.Efficiency * Transfer; };
        const auto ClutchAt = [&](double Transfer) {
            return G != 0.0 && Front.ClutchEngagement > 0.0
                ? std::clamp((Numerator + Kc * Front.Dt * G * WheelTransfer(Transfer) / Id) / Denominator, -Capacity, Capacity) : 0.0;
        };
        const auto DemandAt = [&](double Transfer) {
            const double Clutch = ClutchAt(Transfer);
            const double FrontSpeed = Average + Front.Dt * (Box.Efficiency * G * Clutch - WheelTransfer(Transfer) + Tau) / Id;
            const double RearSpeed = 0.5 * (RearLeft + RearRight) + Rear.Dt
                * (WheelTransfer(Transfer) + Rear.GroundTorqueLeft + Rear.GroundTorqueRight) / (2.0 * Rear.WheelInertia);
            const auto Split = SplitFourWheelDriveTorque(Clutch * std::abs(G) / Final,
                Direction * Final * FrontSpeed, Direction * Final * RearSpeed, Health);
            return Split.RearLeft + Split.RearRight;
        };
        double Transfer = 0.0;
        if (!bHandbrake && DemandAt(0.0) > 0.0)
        {
            // Demand falls as transfer slows the front and accelerates the rear. The clutch's
            // reaction is included in every trial; 40 halvings resolve below 1e-9 Nm.
            double Low = 0.0, High = 300.0;
            for (int I = 0; I < 40; ++I)
            {
                const double Mid = 0.5 * (Low + High);
                if (DemandAt(Mid) > Mid) Low = Mid;
                else High = Mid;
            }
            Transfer = 0.5 * (Low + High);
        }
        if (Transfer == 0.0)
        {
            // Preserve every operation of the original front-drive solver when the coupling opens.
            StepDrivenAxle(Engine, Box, Front, EngineOmega, FrontLeft, FrontRight);
        }
        else
        {
            const double Clutch = ClutchAt(Transfer);
            EngineOmega = std::max(0.0, EngineOmega + Front.Dt * (Te - Clutch) / Engine.Inertia);
            const double NextAverage = Average + Front.Dt * (Box.Efficiency * G * Clutch - WheelTransfer(Transfer) + Tau) / Id;
            const double Half = 0.5 * (FrontLeft - FrontRight) + Front.Dt * (Front.GroundTorqueLeft - Front.GroundTorqueRight) / Id;
            FrontLeft = ApplyBrake(NextAverage + Half, Front.BrakeLeft, Front.WheelInertia, Front.Dt);
            FrontRight = ApplyBrake(NextAverage - Half, Front.BrakeRight, Front.WheelInertia, Front.Dt);
        }
        RearLeft = StepFreeWheel(RearLeft, Rear.GroundTorqueLeft + 0.5 * WheelTransfer(Transfer), Rear.BrakeLeft, Rear.WheelInertia, Rear.Dt);
        RearRight = StepFreeWheel(RearRight, Rear.GroundTorqueRight + 0.5 * WheelTransfer(Transfer), Rear.BrakeRight, Rear.WheelInertia, Rear.Dt);
        return Transfer;
    }

}
