#include "Vehicle/RallyVehicleSetup.h"

Rally::Tyre::FSpec FRallyTyreSetup::ToSpec() const
{
    Rally::Tyre::FSpec Spec;
    Spec.GripHard = GripHard;
    Spec.GripLoose = GripLoose;
    Spec.PeakSlipAngleHardDeg = PeakSlipAngleHard;
    Spec.PeakSlipAngleLooseDeg = PeakSlipAngleLoose;
    Spec.PeakSlipRatioHard = PeakSlipRatioHard;
    Spec.PeakSlipRatioLoose = PeakSlipRatioLoose;
    Spec.SlideRatioHard = SlideRatioHard;
    Spec.SlideRatioLoose = SlideRatioLoose;
    Spec.LoadSensitivity = LoadSensitivity;
    Spec.NominalLoad = NominalLoad;
    Spec.Pressure = Pressure;
    Spec.NominalPressure = NominalPressure;
    return Spec;
}

Rally::Drivetrain::FEngineSpec FRallyEngineSetup::ToSpec() const
{
    Rally::Drivetrain::FEngineSpec Spec;
    for (const FVector2D& Point : TorqueCurve)
    {
        Spec.CurveRpm.push_back(Point.X);
        Spec.CurveTorque.push_back(Point.Y);
    }
    Spec.IdleRpm = IdleRpm;
    Spec.LimiterRpm = LimiterRpm;
    Spec.Inertia = FMath::Max(Inertia, 0.01f);
    Spec.FrictionTorque = FrictionTorque;
    Spec.EngineBrakeTorque = EngineBrakeTorque;
    return Spec;
}

Rally::Drivetrain::FGearboxSpec FRallyGearboxSetup::ToSpec() const
{
    Rally::Drivetrain::FGearboxSpec Spec;
    for (const float Ratio : ForwardRatios)
    {
        Spec.Forward.push_back(Ratio);
    }
    Spec.Reverse = ReverseRatio;
    Spec.FinalDrive = FinalDrive;
    Spec.Efficiency = Efficiency;
    Spec.ShiftTime = ShiftTime;
    Spec.UpshiftRpm = UpshiftRpm;
    Spec.DownshiftRpm = DownshiftRpm;
    Spec.ClutchMaxTorque = ClutchMaxTorque;
    return Spec;
}
