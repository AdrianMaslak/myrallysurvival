#include "Vehicle/RallyDrivetrain.h"
#include "Vehicle/RallyTyreModel.h"
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    using namespace Rally;

    Drivetrain::FEngineSpec OctaviaEngine()
    {
        Drivetrain::FEngineSpec Engine;
        Engine.CurveRpm = { 1000, 1950, 5000, 5500, 6000, 6800 };
        Engine.CurveTorque = { 150, 235, 235, 229, 210, 175 };
        Engine.LimiterRpm = 6800;
        return Engine;
    }

    Drivetrain::FGearboxSpec OctaviaGearbox()
    {
        Drivetrain::FGearboxSpec Box;
        Box.Forward = { 3.300, 1.944, 1.308, 1.034, 0.838 };
        Box.Reverse = 3.06;
        Box.FinalDrive = 3.647;
        Box.UpshiftRpm = 6300;
        return Box;
    }

    struct FLaunchResult
    {
        double SpeedAt10s = 0.0;
        double MaxEngineRpm = 0.0;
        double MinLaunchRpm = 1e9;
        double LaunchRpm = 0.0;
        double StopDistance = 0.0;
        int FinalGear = 0;
        bool bFinite = true;
    };

    // Straight-line quarter model: front-drive axle and free rear axle on one surface.
    FLaunchResult SimulateLaunchAndStop(const Tyre::FSurface& Surface)
    {
        const auto Engine = OctaviaEngine();
        const auto Box = OctaviaGearbox();
        const Tyre::FResolved Tyre = Tyre::Resolve(Tyre::FSpec(), Surface);
        const double Mass = 1300.0, Radius = 0.31, Inertia = 1.2, FrontLoad = 3737.0, RearLoad = 2640.0;
        const double Dt = 1.0 / 480.0;
        double Speed = 0.0, Position = 0.0, EngineOmega = Engine.IdleRpm / Drivetrain::RadPerSecToRpm;
        double FrontLeft = 0.0, FrontRight = 0.0, Rear = 0.0;
        Drivetrain::FShiftState Shift;
        FLaunchResult Out;
        double BrakeStart = -1.0;
        for (int Step = 0; Step < 20 * 480; ++Step)
        {
            const double Time = Step * Dt;
            const bool bBraking = Time >= 10.0;
            const double Throttle = bBraking ? 0.0 : 1.0;
            const auto Fx = [&](double Omega, double Load) {
                const double Slip = (Omega * Radius - Speed) / std::max(std::abs(Speed), 3.0);
                return Tyre::Evaluate(Tyre, Surface, Load, Slip, 0.0).Longitudinal;
            };
            const double FL = Fx(FrontLeft, FrontLoad), FR = Fx(FrontRight, FrontLoad), FRear = Fx(Rear, RearLoad);
            Drivetrain::UpdateAutomatic(Box, Shift, Speed / Radius, Throttle, false, Dt);
            Drivetrain::FAxleStep Axle;
            Axle.Throttle = Throttle;
            Axle.Gear = Shift.EngagedGear();
            Axle.ClutchEngagement = Drivetrain::ClutchEngagement(Engine, Box, Shift, 0.5 * (FrontLeft + FrontRight), EngineOmega);
            if (Step < 480)
                Out.MinLaunchRpm = std::min(Out.MinLaunchRpm, EngineOmega * Drivetrain::RadPerSecToRpm);
            if (Step == 240)
                Out.LaunchRpm = EngineOmega * Drivetrain::RadPerSecToRpm;
            Axle.GroundTorqueLeft = -FL * Radius;
            Axle.GroundTorqueRight = -FR * Radius;
            Axle.BrakeLeft = Axle.BrakeRight = bBraking ? 1500.0 : 0.0;
            Axle.WheelInertia = Inertia;
            Axle.Dt = Dt;
            Drivetrain::StepDrivenAxle(Engine, Box, Axle, EngineOmega, FrontLeft, FrontRight);
            Rear = Drivetrain::StepFreeWheel(Rear, -FRear * Radius, bBraking ? 900.0 : 0.0, Inertia, Dt);
            const double Drag = 0.5 * 1.2 * 0.69 * Speed * std::abs(Speed);
            Speed += Dt * (FL + FR + 2.0 * FRear - Drag) / Mass;
            Position += Speed * Dt;
            Out.MaxEngineRpm = std::max(Out.MaxEngineRpm, EngineOmega * Drivetrain::RadPerSecToRpm);
            Out.bFinite = Out.bFinite && std::isfinite(Speed) && std::isfinite(EngineOmega) && std::isfinite(FrontLeft);
            if (Step == 10 * 480 - 1)
            {
                Out.SpeedAt10s = Speed;
                Out.FinalGear = Shift.Gear;
            }
            if (bBraking && BrakeStart < 0.0)
                BrakeStart = Position;
            if (bBraking && Speed <= 0.05 && Out.StopDistance == 0.0)
                Out.StopDistance = Position - BrakeStart;
        }
        return Out;
    }
}

namespace
{
    struct FClutchLaunch
    {
        double HeldRpm = 0.0;
        double HeldSpeed = 0.0;
        double SpeedAt3s = 0.0;
        bool bFinite = true;
    };

    // Manual box in first: clutch pedal down at full throttle for 1.5 s, then let up.
    FClutchLaunch SimulateClutchLaunch(const Tyre::FSurface& Surface)
    {
        const auto Engine = OctaviaEngine();
        const auto Box = OctaviaGearbox();
        const Tyre::FResolved Tyre = Tyre::Resolve(Tyre::FSpec(), Surface);
        const double Mass = 1300.0, Radius = 0.31, Inertia = 1.2, FrontLoad = 3737.0, RearLoad = 2640.0;
        const double Dt = 1.0 / 480.0;
        double Speed = 0.0, EngineOmega = Engine.IdleRpm / Drivetrain::RadPerSecToRpm, FrontLeft = 0.0, FrontRight = 0.0, Rear = 0.0, Pedal = 0.0;
        Drivetrain::FShiftState Shift;
        FClutchLaunch Out;
        for (int Step = 0; Step < 3 * 480; ++Step)
        {
            const double Time = Step * Dt;
            Pedal = Drivetrain::MovePedal(Pedal, Time < 1.5, Dt);
            const auto Fx = [&](double Omega, double Load) {
                const double Slip = (Omega * Radius - Speed) / std::max(std::abs(Speed), 3.0);
                return Tyre::Evaluate(Tyre, Surface, Load, Slip, 0.0).Longitudinal;
            };
            const double FL = Fx(FrontLeft, FrontLoad), FR = Fx(FrontRight, FrontLoad), FRear = Fx(Rear, RearLoad);
            Drivetrain::UpdateManual(Box, Shift, 1);
            Drivetrain::TickShift(Shift, Dt);
            Drivetrain::FAxleStep Axle;
            Axle.Throttle = 1.0;
            Axle.Gear = Shift.EngagedGear();
            Axle.ClutchEngagement = Drivetrain::PedalEngagement(
                Drivetrain::ClutchEngagement(Engine, Box, Shift, 0.5 * (FrontLeft + FrontRight), EngineOmega), Pedal);
            Axle.GroundTorqueLeft = -FL * Radius;
            Axle.GroundTorqueRight = -FR * Radius;
            Axle.WheelInertia = Inertia;
            Axle.Dt = Dt;
            Drivetrain::StepDrivenAxle(Engine, Box, Axle, EngineOmega, FrontLeft, FrontRight);
            Rear = Drivetrain::StepFreeWheel(Rear, -FRear * Radius, 0.0, Inertia, Dt);
            Speed += Dt * (FL + FR + 2.0 * FRear) / Mass;
            if (Step == static_cast<int>(1.4 * 480))
            {
                Out.HeldRpm = EngineOmega * Drivetrain::RadPerSecToRpm;
                Out.HeldSpeed = Speed;
            }
            Out.bFinite = Out.bFinite && std::isfinite(Speed) && std::isfinite(EngineOmega);
        }
        Out.SpeedAt3s = Speed;
        return Out;
    }
}

int main()
{
    int Failures = 0;
    int Checks = 0;
    auto Check = [&](bool Pass, const char* Label) {
        ++Checks;
        if (!Pass) { ++Failures; std::cerr << "FAIL: " << Label << '\n'; }
    };
    const Tyre::FSurface Gravel{ 0.65, 1.0 };
    const Tyre::FSurface Tarmac{ 1.0, 0.0 };
    const Tyre::FResolved OnGravel = Tyre::Resolve(Tyre::FSpec(), Gravel);
    const double Load = 3500.0;

    // Tyre model
    Check(Tyre::Evaluate(OnGravel, Gravel, Load, 0.0, 0.0).Lateral == 0.0, "no slip, no force");
    Check(Tyre::Evaluate(OnGravel, Gravel, 0.0, 0.2, 0.2).Lateral == 0.0, "unloaded tyre has no force");
    Check(Tyre::Evaluate(OnGravel, Gravel, Load, std::numeric_limits<double>::quiet_NaN(), 0.0).Longitudinal == 0.0, "NaN slip is ignored");
    const auto Peak = Tyre::Evaluate(OnGravel, Gravel, Load, 0.0, OnGravel.PeakTanSlipAngle);
    Check(std::abs(-Peak.Lateral - 0.65 * Load) < 1.0, "peak lateral force equals mu times load");
    Check(Tyre::Evaluate(OnGravel, Gravel, Load, 0.0, 0.1).Lateral < 0.0, "lateral force opposes sliding");
    const auto Sliding = Tyre::Evaluate(OnGravel, Gravel, Load, 0.0, std::tan(0.6));
    Check(-Sliding.Lateral > 0.88 * 0.65 * Load && -Sliding.Lateral < -Peak.Lateral, "loose surface keeps about 90 percent when sliding");
    const auto TarmacSlide = Tyre::Evaluate(Tyre::Resolve(Tyre::FSpec(), Tarmac), Tarmac, Load, 0.0, std::tan(0.6));
    Check(-TarmacSlide.Lateral < 0.83 * Load, "tarmac drops toward 80 percent when sliding");
    const auto Braking = Tyre::Evaluate(OnGravel, Gravel, Load, -OnGravel.PeakSlipRatio, OnGravel.PeakTanSlipAngle);
    Check(-Braking.Lateral < -Peak.Lateral * 0.8, "braking at the limit reduces cornering force");
    Check(Tyre::Curve(1.0, 0.9) == 1.0 && Tyre::Curve(1.0001, 0.9) < 1.0 && Tyre::Curve(1.0001, 0.9) > 0.999, "curve continuous at peak");
    const double Light = -Tyre::Evaluate(OnGravel, Gravel, Load, 0.0, OnGravel.PeakTanSlipAngle).Lateral;
    const double Heavy = -Tyre::Evaluate(OnGravel, Gravel, 2.0 * Load, 0.0, OnGravel.PeakTanSlipAngle).Lateral;
    Check(Heavy < 2.0 * Light && Heavy > 1.6 * Light, "load sensitivity: doubling load less than doubles grip");
    // Cornering stiffness at a 3737 N front corner on gravel, per degree. Stunt Rally's gravel tyre gives about 309.
    const double Stiffness = -Tyre::Evaluate(OnGravel, Gravel, 3737.0, 0.0, std::tan(0.5 * 3.14159265 / 180.0)).Lateral / 0.5;
    Check(Stiffness > 250.0 && Stiffness < 450.0, "gravel cornering stiffness in reference range");
    Tyre::FSpec Flat;
    Flat.Pressure = 0.2;
    Check(Tyre::Resolve(Flat, Gravel).Grip < 0.6 * OnGravel.Grip, "flat tyre loses most grip");
    Tyre::FSpec Soft;
    Soft.Pressure = 1.6;
    Check(Tyre::Resolve(Soft, Gravel).Grip > OnGravel.Grip && Tyre::Resolve(Soft, Gravel).PeakTanSlipAngle > OnGravel.PeakTanSlipAngle,
        "lower pressure: more loose-surface grip, lazier response");

    // Drivetrain pieces
    const auto Engine = OctaviaEngine();
    const auto Box = OctaviaGearbox();
    Check(std::abs(Drivetrain::FullThrottleTorque(Engine, 3000.0) - 235.0) < 1e-9, "torque plateau");
    Check(std::abs(Drivetrain::FullThrottleTorque(Engine, 5750.0) - 219.5) < 1e-9, "torque interpolation");
    Check(Drivetrain::EngineTorque(Engine, 7000.0 / Drivetrain::RadPerSecToRpm, 1.0) < 0.0, "limiter cuts throttle");
    // Road speed at 6500 rpm, 0.31 m wheel radius: published RS figures are about 120 km/h in 2nd and
    // 170 km/h in 3rd near 7000 rpm, and 235 km/h top speed in 5th.
    const auto KmhAt = [&](int Gear, double Rpm) { return Rpm / Drivetrain::RadPerSecToRpm / Drivetrain::Ratio(Box, Gear) * 0.31 * 3.6; };
    Check(std::abs(KmhAt(1, 6500) - 63.0) < 2.0 && std::abs(KmhAt(2, 6500) - 107.0) < 2.0 && std::abs(KmhAt(3, 6500) - 159.0) < 2.0,
        "RS gearing: 63, 107, 159 km/h at 6500 rpm in 1st to 3rd");
    Check(std::abs(KmhAt(2, 7000) - 120.0) < 6.0 && std::abs(KmhAt(3, 7000) - 170.0) < 6.0, "matches published 120/170 km/h near 7000 rpm");
    Check(KmhAt(5, 6300) > 235.0, "5th gear reaches 235 km/h below the upshift point");
    Check(Drivetrain::TractionControlFactor(0.1, 0.15) == 1.0 && Drivetrain::TractionControlFactor(0.5, 0.15) == 0.1,
        "traction control: no cut at peak slip, full cut at heavy wheelspin");
    Check(Drivetrain::EngineTorque(Engine, 600.0 / Drivetrain::RadPerSecToRpm, 0.0) > 0.0, "idle governor recovers low rpm");
    Check(Drivetrain::Ratio(Box, -1) < 0.0 && Drivetrain::Ratio(Box, 0) == 0.0 && Drivetrain::Ratio(Box, 9) == 0.0, "gear ratio signs");
    Check(Drivetrain::ApplyBrake(1.0, 1000.0, 1.2, 0.01) == 0.0 && Drivetrain::ApplyBrake(-50.0, 100.0, 1.2, 0.01) < 0.0, "brake never reverses a wheel");
    Drivetrain::FShiftState Shift;
    Drivetrain::UpdateAutomatic(Box, Shift, 5.0, 1.0, false, 0.01);
    Check(!Shift.IsShifting(), "no upshift at low ground speed even if wheels spin");
    Drivetrain::UpdateAutomatic(Box, Shift, 60.0, 1.0, false, 0.01);
    Check(Shift.IsShifting() && Shift.EngagedGear() == 0 && Shift.PendingGear == 2, "upshift on ground speed, neutral while shifting");
    const double IdleOmega = Engine.IdleRpm / Drivetrain::RadPerSecToRpm;
    Check(Drivetrain::ClutchEngagement(Engine, Box, Drivetrain::FShiftState(), 0.0, IdleOmega) == 0.0, "stopped at idle: clutch open");
    Check(Drivetrain::ClutchEngagement(Engine, Box, Drivetrain::FShiftState(), 0.0, 3000.0 / Drivetrain::RadPerSecToRpm) == 1.0, "revved launch: clutch bites fully");
    auto Stopped = Engine;
    Stopped.bRunning = false;
    Check(Drivetrain::EngineTorque(Stopped, 600.0 / Drivetrain::RadPerSecToRpm, 1.0) < 0.0
        && Drivetrain::ClutchEngagement(Stopped, Box, Drivetrain::FShiftState(), 30.0, 3000.0 / Drivetrain::RadPerSecToRpm) == 0.0,
        "stopped engine: no torque, no idle governor, clutch open");
    {
        // Lost gears are skipped; with none left the box sits in neutral.
        auto Damaged = Box;
        Damaged.LostGears = 1u << 3;
        Drivetrain::FShiftState Second;
        Second.Gear = Second.PendingGear = 2;
        Drivetrain::UpdateAutomatic(Damaged, Second, 110.0, 1.0, false, 0.01);
        Check(Second.IsShifting() && Second.PendingGear == 4, "upshift skips a lost gear");
        Drivetrain::FShiftState Third;
        Third.Gear = Third.PendingGear = 3;
        Drivetrain::UpdateAutomatic(Damaged, Third, 40.0, 0.5, false, 0.01);
        Check(Third.IsShifting() && Third.PendingGear == 2, "losing the gear it is in drops to the one below");
        Damaged.LostGears = ~0u;
        Drivetrain::FShiftState Broken;
        Drivetrain::UpdateAutomatic(Damaged, Broken, 10.0, 1.0, false, 0.01);
        Drivetrain::UpdateAutomatic(Damaged, Broken, 10.0, 1.0, false, 1.0);
        Check(Broken.EngagedGear() == 0 && Drivetrain::Ratio(Damaged, Broken.EngagedGear()) == 0.0, "a broken box sits in neutral");
    }
    auto Worn = Engine;
    Worn.TorqueScale = 0.5;
    Check(std::abs(Drivetrain::EngineTorque(Worn, 3000.0 / Drivetrain::RadPerSecToRpm, 1.0) - (0.5 * 235.0 - Engine.FrictionTorque)) < 1e-6,
        "worn engine makes its share of the torque");
    bool bNoHunting = true;
    for (double Throttle : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        for (int Gear = 1; Gear < static_cast<int>(Box.Forward.size()); ++Gear)
        {
            // Engine rpm right after an up-shift at this throttle must not trigger a down-shift.
            const double AfterShift = Drivetrain::UpshiftRpmAt(Box, Throttle) * Box.Forward[Gear] / Box.Forward[Gear - 1];
            bNoHunting = bNoHunting && AfterShift > Drivetrain::DownshiftRpmAt(Box, Throttle) * 1.05;
        }
    }
    Check(bNoHunting, "shift points leave a margin against gear hunting at every throttle");
    Drivetrain::FShiftState Cruise;
    Cruise.Gear = 1;
    Drivetrain::UpdateAutomatic(Box, Cruise, 13.9 / 0.31, 0.5, false, 0.01);
    Check(Cruise.IsShifting() && Cruise.PendingGear == 2, "half throttle at 50 km/h: second gear");

    // Integrated straight-line run
    const auto OnGravelRun = SimulateLaunchAndStop(Gravel);
    const auto OnTarmacRun = SimulateLaunchAndStop(Tarmac);
    Check(OnGravelRun.bFinite && OnTarmacRun.bFinite, "launch simulation stays finite");
    Check(OnTarmacRun.SpeedAt10s * 3.6 > 90.0 && OnTarmacRun.SpeedAt10s * 3.6 < 140.0, "tarmac: 90-140 km/h after 10 s (Octavia RS about 8 s to 100)");
    Check(OnGravelRun.SpeedAt10s < OnTarmacRun.SpeedAt10s, "gravel launch slower than tarmac");
    Check(OnTarmacRun.MaxEngineRpm < 7100.0, "rev limiter holds");
    Check(OnGravelRun.MinLaunchRpm > 750.0, "launch does not bog the engine below idle");
    Check(OnGravelRun.LaunchRpm > 1600.0 && OnGravelRun.LaunchRpm < 3200.0, "clutch slips at about 2000-3000 rpm during launch");
    Check(OnTarmacRun.FinalGear >= 3, "automatic box upshifts during the run");
    const double TarmacDecel = std::pow(OnTarmacRun.SpeedAt10s, 2) / (2.0 * OnTarmacRun.StopDistance) / 9.81;
    const double GravelDecel = std::pow(OnGravelRun.SpeedAt10s, 2) / (2.0 * OnGravelRun.StopDistance) / 9.81;
    Check(OnTarmacRun.StopDistance > 0.0 && TarmacDecel > 0.75 && TarmacDecel < 1.05, "tarmac braking about 0.8-1.0 g");
    Check(OnGravelRun.StopDistance > 0.0 && GravelDecel > 0.45 && GravelDecel < 0.7, "gravel braking about 0.5-0.65 g");

    // Manual box.
    Check(Drivetrain::SelectGear(1, 1, 5, 0.0) == 2 && Drivetrain::SelectGear(5, 1, 5, 100.0) == 5 && Drivetrain::SelectGear(1, -1, 5, 20.0) == 0
        && Drivetrain::SelectGear(0, -1, 5, 2.0) == -1 && Drivetrain::SelectGear(0, -1, 5, 30.0) == 0 && Drivetrain::SelectGear(-1, -1, 5, 0.0) == -1
        && Drivetrain::SelectGear(-1, 1, 5, 0.0) == 0, "sequential lever: neutral between first and reverse, reverse only when nearly stopped");
    {
        Drivetrain::FShiftState Manual;
        Drivetrain::UpdateManual(Box, Manual, 2);
        Check(Manual.IsShifting() && Manual.EngagedGear() == 0, "a manual shift goes through neutral");
        Drivetrain::TickShift(Manual, 0.1);
        Drivetrain::UpdateManual(Box, Manual, 3);
        Drivetrain::TickShift(Manual, 0.2);
        Check(!Manual.IsShifting() && Manual.Gear == 3, "a second press mid-shift goes straight to the next gear");
        Drivetrain::UpdateManual(Box, Manual, 0);
        Check(!Manual.IsShifting() && Manual.EngagedGear() == 0, "out of gear at once");
        Drivetrain::FGearboxSpec Damaged = Box;
        Damaged.LostGears = 1u << 4;
        Drivetrain::FShiftState Lost;
        Drivetrain::UpdateManual(Damaged, Lost, 4);
        Check(Lost.EngagedGear() == 0 && !Lost.IsShifting(), "a lost gear will not go in: neutral");
    }
    Check(Drivetrain::MovePedal(0.0, true, 0.05) == 0.5 && Drivetrain::MovePedal(1.0, false, 0.35) == 0.0 && Drivetrain::MovePedal(1.0, false, 0.1) > 0.7,
        "clutch pedal: down quickly, up over a moment");
    Check(Drivetrain::PedalEngagement(1.0, 1.0) == 0.0 && Drivetrain::PedalEngagement(0.6, 0.0) == 0.6 && Drivetrain::PedalEngagement(1.0, 0.25) == 0.75,
        "the pedal opens the clutch; left up, the auto clutch decides");
    const FClutchLaunch Dropped = SimulateClutchLaunch(Gravel);
    Check(Dropped.bFinite && Dropped.HeldRpm > 6000.0 && Dropped.HeldSpeed < 0.1, "clutch down at full throttle: revs to the limiter, car still");
    Check(Dropped.SpeedAt3s * 3.6 > 12.0, "letting the clutch up launches the car (gravel grip limits it to about 0.35 g)");

    std::cout << "clutch launch: held " << Dropped.HeldRpm << " rpm, " << Dropped.SpeedAt3s * 3.6 << " km/h 1.5 s after the drop\n";
    std::cout << "launch: tarmac " << OnTarmacRun.SpeedAt10s * 3.6 << " km/h, gravel " << OnGravelRun.SpeedAt10s * 3.6
              << " km/h at 10 s; braking tarmac " << TarmacDecel << " g, gravel " << GravelDecel << " g\n";
    std::cout << (Checks - Failures) << "/" << Checks << " checks passed\n";
    return Failures == 0 ? 0 : 1;
}
