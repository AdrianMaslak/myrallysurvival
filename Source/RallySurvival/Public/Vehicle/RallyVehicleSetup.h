#pragma once

#include "CoreMinimal.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Vehicle/RallyDrivetrain.h"
#include "Vehicle/RallyTyreModel.h"
#include "RallyVehicleSetup.generated.h"

// Editable car setup. Parts and damage change these values; values and sources: docs/VEHICLE-PHYSICS.md.

USTRUCT(BlueprintType)
struct RALLYSURVIVAL_API FRallyTyreSetup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    float GripHard = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    float GripLoose = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre", meta = (Units = "Degrees"))
    float PeakSlipAngleHard = 7.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre", meta = (Units = "Degrees"))
    float PeakSlipAngleLoose = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    float PeakSlipRatioHard = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    float PeakSlipRatioLoose = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    float SlideRatioHard = 0.80f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    float SlideRatioLoose = 0.90f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    float LoadSensitivity = 0.10f;

    // Newtons.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    float NominalLoad = 3500.0f;

    // Bar. Below 0.5 the tyre behaves as flat.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    float Pressure = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    float NominalPressure = 2.0f;

    Rally::Tyre::FSpec ToSpec() const;
};

USTRUCT(BlueprintType)
struct RALLYSURVIVAL_API FRallyWheelSetup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel")
    FName Name;

    // Wheel centre at modelled ride height, in the Octavia assembly frame (cm, origin at ground).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel")
    FVector Pivot = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel")
    bool bSteered = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel")
    bool bDriven = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel")
    bool bHandbrake = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel", meta = (Units = "Centimeters"))
    float Radius = 31.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel", meta = (Units = "Centimeters"))
    float Width = 20.5f;

    // Wheel, tyre, hub and brake disc, kg m^2.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel")
    float Inertia = 1.2f;

    // N/m at the wheel.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suspension")
    float SpringRate = 38000.0f;

    // N s/m.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suspension")
    float BumpDamping = 2500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suspension")
    float ReboundDamping = 3500.0f;

    // Travel above and below the pivot.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suspension", meta = (Units = "Centimeters"))
    float BumpTravel = 14.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suspension", meta = (Units = "Centimeters"))
    float DroopTravel = 10.0f;

    // Height above the contact patch where tyre forces act on the body: the axle roll centre.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suspension", meta = (Units = "Centimeters"))
    float RollCentreHeight = 8.0f;

    // Nm.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brakes")
    float MaxBrakeTorque = 1500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brakes")
    float HandbrakeTorque = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tyre")
    FRallyTyreSetup Tyre;
};

USTRUCT(BlueprintType)
struct RALLYSURVIVAL_API FRallyEngineSetup
{
    GENERATED_BODY()

    // X = rpm, Y = full-throttle torque in Nm.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
    TArray<FVector2D> TorqueCurve;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
    float IdleRpm = 900.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
    float LimiterRpm = 6900.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
    float Inertia = 0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
    float FrictionTorque = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
    float EngineBrakeTorque = 45.0f;

    Rally::Drivetrain::FEngineSpec ToSpec() const;
};

USTRUCT(BlueprintType)
struct RALLYSURVIVAL_API FRallyGearboxSetup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gearbox")
    TArray<float> ForwardRatios;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gearbox")
    float ReverseRatio = 3.06f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gearbox")
    float FinalDrive = 3.94f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gearbox")
    float Efficiency = 0.9f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gearbox", meta = (Units = "Seconds"))
    float ShiftTime = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gearbox")
    float UpshiftRpm = 6200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gearbox")
    float DownshiftRpm = 3000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gearbox")
    float ClutchMaxTorque = 350.0f;

    Rally::Drivetrain::FGearboxSpec ToSpec() const;
};

// Driver controls after reverse logic; replicated so every machine runs the same forces.
USTRUCT(BlueprintType)
struct RALLYSURVIVAL_API FRallyCarInput
{
    GENERATED_BODY()

    UPROPERTY()
    float Throttle = 0.0f;

    UPROPERTY()
    float Brake = 0.0f;

    UPROPERTY()
    float Steer = 0.0f;

    UPROPERTY()
    bool bHandbrake = false;

    UPROPERTY()
    bool bReverse = false;
};

// Car state sent by whichever machine simulates the car (the driver's, or the server when nobody
// drives) so every other machine can replay it smoothly. Time is the sender's local game clock.
USTRUCT()
struct RALLYSURVIVAL_API FRallyCarNetState
{
    GENERATED_BODY()

    UPROPERTY()
    double Time = 0.0;

    UPROPERTY()
    FVector_NetQuantize10 Location;

    UPROPERTY()
    FQuat Rotation = FQuat::Identity;

    // cm/s.
    UPROPERTY()
    FVector_NetQuantize10 LinearVelocity;

    // rad/s.
    UPROPERTY()
    FVector_NetQuantize100 AngularVelocity;

    UPROPERTY()
    float SteerDeg = 0.0f;

    UPROPERTY()
    int8 Gear = 1;

    UPROPERTY()
    uint16 EngineRpm = 0;

    // Per wheel, FL FR RL RR: suspension travel from the pivot (cm) and spin (rad/s), for visuals.
    UPROPERTY()
    float WheelOffsetCm[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    UPROPERTY()
    float WheelOmega[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Incremented on resets so observers jump instead of smoothing across the teleport.
    UPROPERTY()
    uint8 ResetCount = 0;
};

// Surface data the tyres read from the hit physical material. Plain UPhysicalMaterial counts as hard.
UCLASS(BlueprintType)
class RALLYSURVIVAL_API URallyPhysicalMaterial : public UPhysicalMaterial
{
    GENERATED_BODY()

public:
    // 0 = sealed tarmac, 1 = loose gravel or snow: shifts tyre peak slip and post-peak grip.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rally", meta = (ClampMin = "0", ClampMax = "1"))
    float Looseness = 0.0f;

    // Rolling resistance coefficient: tarmac about 0.012, gravel 0.02 to 0.04.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rally")
    float RollingResistance = 0.015f;
};
