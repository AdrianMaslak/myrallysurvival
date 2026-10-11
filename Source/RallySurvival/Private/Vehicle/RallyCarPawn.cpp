#include "Vehicle/RallyCarPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogRallyCar, Log, All);

namespace
{
    struct FPartMount
    {
        const TCHAR* Name;
        const TCHAR* MeshPath;
        FVector Pivot;
    };

    // unreal_expected_pivot_cm from Art/Vehicles/Octavia/Prototype/Export/assembly.json.
    // Assembly origin is ground level below the car centre. Order FL, FR, RL, RR: anti-roll pairs rely on it.
    const FPartMount WheelMounts[] =
    {
        { TEXT("FL"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Wheel_FL.SM_Octavia_Wheel_FL"), FVector(126.3, -75.1, 28.8) },
        { TEXT("FR"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Wheel_FR.SM_Octavia_Wheel_FR"), FVector(126.3, 75.1, 28.8) },
        { TEXT("RL"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Wheel_RL.SM_Octavia_Wheel_RL"), FVector(-130.7, -70.2, 27.2) },
        { TEXT("RR"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Wheel_RR.SM_Octavia_Wheel_RR"), FVector(-130.7, 70.2, 27.2) },
    };

    const FPartMount PanelMounts[] =
    {
        { TEXT("Door_FL"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Door_FL.SM_Octavia_Door_FL"), FVector(27.6, -70.1, 84.8) },
        { TEXT("Door_FR"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Door_FR.SM_Octavia_Door_FR"), FVector(27.6, 70.1, 84.8) },
        { TEXT("Door_RL"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Door_RL.SM_Octavia_Door_RL"), FVector(-77.4, -69.8, 84.8) },
        { TEXT("Door_RR"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Door_RR.SM_Octavia_Door_RR"), FVector(-77.4, 69.8, 84.8) },
        { TEXT("Bonnet"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Bonnet.SM_Octavia_Bonnet"), FVector(153.7, 0.0, 83.4) },
        { TEXT("Trunk"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Trunk.SM_Octavia_Trunk"), FVector(-175.8, 0.0, 100.9) },
        { TEXT("Radiator"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Radiator.SM_Octavia_Radiator"), FVector(193.3, 0.0, 52.8) },
    };

    // The body box is the actor root and spans 20..140 cm above the assembly origin.
    const FVector BodyHalfExtent(225.0, 86.0, 60.0);
    const FVector AssemblyOffset(0.0, 0.0, -80.0);
    const float VehicleMassKg = 1300.0f;
    const double AirDensity = 1.2;
    const int32 DrivetrainSubsteps = 8;
    // Driver's eye in the assembly frame: above the left front seat, about 50 cm behind the steering
    // wheel centre (49, -39, 86), clear of the windscreen top (137). From scripts/inspect_octavia_cabin.py.
    const FVector DriverEye(-2.0, -37.0, 114.0);

    UStaticMeshComponent* CreateVisualMesh(AActor* Owner, USceneComponent* Parent, const FName Name, const TCHAR* MeshPath, const FVector& Location)
    {
        UStaticMeshComponent* Mesh = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Mesh->SetupAttachment(Parent);
        Mesh->SetRelativeLocation(Location);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        ConstructorHelpers::FObjectFinder<UStaticMesh> Asset(MeshPath);
        if (Asset.Succeeded())
        {
            Mesh->SetStaticMesh(Asset.Object);
        }
        return Mesh;
    }

    UInputAction* MakeAction(UObject* Outer, EInputActionValueType Type)
    {
        UInputAction* Action = NewObject<UInputAction>(Outer);
        Action->ValueType = Type;
        return Action;
    }

    // Octavia RS 1.8T front-drive baseline on gravel tyres. Derivations: docs/VEHICLE-PHYSICS.md.
    // Static corner loads with the COM nudge: front about 3740 N, rear about 2640 N.
    FRallyWheelSetup DefaultWheel(const FPartMount& Mount)
    {
        const bool bFront = Mount.Pivot.X > 0.0;
        FRallyWheelSetup Wheel;
        Wheel.Name = Mount.Name;
        Wheel.Pivot = Mount.Pivot;
        Wheel.bSteered = bFront;
        Wheel.bDriven = bFront;
        Wheel.bHandbrake = !bFront;
        // About 1.6 Hz front and 1.7 Hz rear ride frequency; damping about 0.35 critical in bump, 0.55 in rebound.
        Wheel.SpringRate = bFront ? 38500.0f : 30700.0f;
        Wheel.BumpDamping = bFront ? 2700.0f : 2000.0f;
        Wheel.ReboundDamping = bFront ? 4200.0f : 3150.0f;
        // Droop roughly equals static sag, so the body sits at its modelled height.
        Wheel.BumpTravel = bFront ? 14.0f : 15.0f;
        Wheel.DroopTravel = bFront ? 10.0f : 9.0f;
        // MacPherson front roll centre is low; the rear torsion beam's is higher.
        Wheel.RollCentreHeight = bFront ? 8.0f : 20.0f;
        Wheel.MaxBrakeTorque = bFront ? 1500.0f : 900.0f;
        Wheel.HandbrakeTorque = bFront ? 0.0f : 2000.0f;
        // Gravel tread bites into loose surfaces: effective mu about 0.72 on the 0.65 gravel road.
        Wheel.Tyre.GripLoose = 1.1f;
        return Wheel;
    }
}

ARallyCarPawn::ARallyCarPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    bAsyncPhysicsTickEnabled = true;
    bReplicates = true;
    SetReplicatingMovement(true);

    Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
    Body->SetBoxExtent(BodyHalfExtent);
    Body->SetCollisionProfileName(UCollisionProfile::Vehicle_ProfileName);
    Body->SetSimulatePhysics(true);
    Body->BodyInstance.SetMassOverride(VehicleMassKg);
    // Centre of gravity about 50 cm above ground, ahead of mid-wheelbase for the front engine.
    Body->BodyInstance.COMNudge = FVector(20.0, 0.0, -30.0);
    SetRootComponent(Body);

    ChassisMesh = CreateVisualMesh(this, Body, TEXT("ChassisMesh"),
        TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Chassis.SM_Octavia_Chassis"), AssemblyOffset);
    for (const FPartMount& Panel : PanelMounts)
    {
        CreateVisualMesh(this, Body, FName(FString(TEXT("Panel_")) + Panel.Name), Panel.MeshPath, AssemblyOffset + Panel.Pivot);
    }
    for (const FPartMount& Mount : WheelMounts)
    {
        WheelMeshes.Add(CreateVisualMesh(this, Body, FName(FString(TEXT("WheelMesh_")) + Mount.Name), Mount.MeshPath, AssemblyOffset + Mount.Pivot));
        WheelSetups.Add(DefaultWheel(Mount));
    }

    // Octavia RS 1.8T (132 kW): 235 Nm 1950-5000 rpm, 132 kW at 5500 rpm (229 Nm).
    EngineSetup.TorqueCurve = { FVector2D(1000, 150), FVector2D(1950, 235), FVector2D(5000, 235), FVector2D(5500, 229), FVector2D(6000, 210), FVector2D(6800, 175) };
    EngineSetup.LimiterRpm = 6800.0f;
    // Gearbox 02J code EHA (Octavia Mk1 workshop manual, 132 kW). Reverse ratio is not in that source.
    GearboxSetup.ForwardRatios = { 3.300f, 1.944f, 1.308f, 1.034f, 0.838f };
    GearboxSetup.FinalDrive = 3.647f;
    GearboxSetup.UpshiftRpm = 6300.0f;

    CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    CameraArm->SetupAttachment(Body);
    CameraArm->SetRelativeLocation(FVector(0.0, 0.0, 60.0));
    CameraArm->SetRelativeRotation(FRotator(-12.0, 0.0, 0.0));
    CameraArm->TargetArmLength = 650.0f;
    CameraArm->bDoCollisionTest = true;
    CameraArm->bInheritPitch = false;
    CameraArm->bInheritRoll = false;
    CameraArm->bEnableCameraLag = true;
    CameraArm->CameraLagSpeed = 8.0f;
    CameraArm->bEnableCameraRotationLag = true;
    CameraArm->CameraRotationLagSpeed = 6.0f;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
    Camera->SetAutoActivate(false);

    // Fixed to the body, so the view rolls and pitches with the suspension.
    CockpitCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
    CockpitCamera->SetupAttachment(Body);
    CockpitCamera->SetRelativeLocation(AssemblyOffset + DriverEye);
    CockpitCamera->SetFieldOfView(80.0f);
    CockpitCamera->SetAutoActivate(true);
}

void ARallyCarPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // The driving client uses its own input immediately; everyone else follows the server copy.
    DOREPLIFETIME_CONDITION(ARallyCarPawn, ReplicatedInput, COND_SkipOwner);
}

void ARallyCarPawn::PostInitializeComponents()
{
    Super::PostInitializeComponents();

#if !UE_BUILD_SHIPPING
    // Tuning experiments without rebuilding, e.g. -RallySpringScale=0.8 -RallyDampingScale=1.5
    // -RallyGripScale=1.1 -RallyPressure=1.6 -RallyAntiRollScale=0.5 -RallyPeakSlipScale=0.8
    const TCHAR* CommandLine = FCommandLine::Get();
    float Value = 0.0f;
    for (FRallyWheelSetup& Wheel : WheelSetups)
    {
        if (FParse::Value(CommandLine, TEXT("RallySpringScale="), Value)) Wheel.SpringRate *= Value;
        if (FParse::Value(CommandLine, TEXT("RallyDampingScale="), Value)) { Wheel.BumpDamping *= Value; Wheel.ReboundDamping *= Value; }
        if (FParse::Value(CommandLine, TEXT("RallyGripScale="), Value)) { Wheel.Tyre.GripHard *= Value; Wheel.Tyre.GripLoose *= Value; }
        if (FParse::Value(CommandLine, TEXT("RallyPressure="), Value)) Wheel.Tyre.Pressure = Value;
        // Below 1 the tyres peak at smaller slip angles: crisper response, less sideways attitude.
        if (FParse::Value(CommandLine, TEXT("RallyPeakSlipScale="), Value)) { Wheel.Tyre.PeakSlipAngleHard *= Value; Wheel.Tyre.PeakSlipAngleLoose *= Value; }
    }
    if (FParse::Value(CommandLine, TEXT("RallyAntiRollScale="), Value))
    {
        FrontAntiRoll *= Value;
        RearAntiRoll *= Value;
    }
#endif
}

void ARallyCarPawn::BeginPlay()
{
    Super::BeginPlay();
    ApplySetup();
    bAutoDrive = HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("RallyAutoDrive"));
    if (bAutoDrive)
    {
        AutoDriveStart = GetActorLocation();
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_AUTODRIVE start=%s"), *AutoDriveStart.ToCompactString());
    }
}

void ARallyCarPawn::ApplySetup()
{
    FScopeLock Lock(&PhysicsLock);
    PhysicsWheels = WheelSetups;
    TyreSpecs.Reset();
    for (const FRallyWheelSetup& Wheel : WheelSetups)
    {
        TyreSpecs.Add(Wheel.Tyre.ToSpec());
    }
    EngineSpec = EngineSetup.ToSpec();
    GearboxSpec = GearboxSetup.ToSpec();
    PhysicsFrontAntiRoll = FrontAntiRoll;
    PhysicsRearAntiRoll = RearAntiRoll;
    PhysicsDragArea = DragArea;
    PhysicsMaxSteer = MaxSteerAngle;
    bPhysicsABS = bABS;
    bPhysicsTractionControl = bTractionControl;
    if (Wheels.Num() != PhysicsWheels.Num())
    {
        Wheels.SetNum(PhysicsWheels.Num());
        EngineOmega = EngineSpec.IdleRpm / Rally::Drivetrain::RadPerSecToRpm;
    }
}

float ARallyCarPawn::GetForwardSpeedKmh() const
{
    return FVector::DotProduct(GetVelocity(), GetActorForwardVector()) * 0.036f;
}

void ARallyCarPawn::AsyncPhysicsTickActor(float DeltaTime, float SimTime)
{
    Super::AsyncPhysicsTickActor(DeltaTime, SimTime);
    FBodyInstance* Instance = Body ? Body->GetBodyInstance() : nullptr;
    const FPhysicsActorHandle Proxy = Instance ? Instance->GetPhysicsActor() : nullptr;
    Chaos::FRigidBodyHandle_Internal* Handle = Proxy ? Proxy->GetPhysicsThreadAPI() : nullptr;
    if (!Handle)
    {
        return;
    }
    FScopeLock Lock(&PhysicsLock);
    SimulatePhysicsStep(*Handle, PhysicsInput, DeltaTime);
}

void ARallyCarPawn::SimulatePhysicsStep(Chaos::FRigidBodyHandle_Internal& Handle, const FRallyCarInput& Input, double Dt)
{
    UWorld* World = GetWorld();
    const int32 NumWheels = PhysicsWheels.Num();
    if (!World || NumWheels == 0 || Wheels.Num() != NumWheels || Dt <= 0.0)
    {
        return;
    }
    if (bResetWheelState)
    {
        for (FWheelState& State : Wheels)
        {
            State = FWheelState();
        }
        Shift = Rally::Drivetrain::FShiftState();
        bResetWheelState = false;
    }

    // Unreal works in cm and kg; the models use SI. Forces go back as kg cm / s^2 (1 N = 100).
    const FQuat Rotation = Handle.R();
    const FVector Position = Handle.X();
    const FVector Com = Position + Rotation.RotateVector(Handle.CenterOfMass());
    const FVector LinearVelocity = Handle.V();
    const FVector AngularVelocity = Handle.W();
    const FVector Up = Rotation.GetUpVector();
    const FVector Forward = Rotation.GetForwardVector();

    FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyWheel), false, this);
    Params.bReturnPhysicalMaterial = true;

    struct FContact
    {
        bool bHit = false;
        FVector Pivot = FVector::ZeroVector;
        FVector Point = FVector::ZeroVector;
        FVector Normal = FVector::UpVector;
        FVector ForwardDir = FVector::ForwardVector;
        FVector RightDir = FVector::RightVector;
        double SpeedLong = 0.0;
        double SpeedLat = 0.0;
        double Force = 0.0;
        double RollingResistance = 0.015;
        Rally::Tyre::FSurface Surface;
        Rally::Tyre::FResolved Tyre;
        double Fx = 0.0;
        double Fy = 0.0;
    };
    TArray<FContact, TInlineAllocator<4>> Contacts;
    Contacts.SetNum(NumWheels);

    // Suspension: wheel centre travels between BumpTravel above and DroopTravel below the pivot.
    for (int32 I = 0; I < NumWheels; ++I)
    {
        const FRallyWheelSetup& Setup = PhysicsWheels[I];
        FWheelState& State = Wheels[I];
        FContact& Contact = Contacts[I];
        const double Total = Setup.BumpTravel + Setup.DroopTravel;
        Contact.Pivot = Position + Rotation.RotateVector(AssemblyOffset + Setup.Pivot);
        const FVector Start = Contact.Pivot + Up * Setup.BumpTravel;
        FHitResult Hit;
        Contact.bHit = World->LineTraceSingleByChannel(Hit, Start, Start - Up * (Total + Setup.Radius), ECC_Visibility, Params);
        const double CentreDrop = Contact.bHit ? FMath::Clamp(Hit.Distance - Setup.Radius, 0.0, Total) : Total;
        State.OffsetCm = Setup.BumpTravel - CentreDrop;
        // Beyond full bump the tyre is squashed: keep counting so the bump stop pushes back.
        const double CompressionCm = Contact.bHit ? Total - (Hit.Distance - Setup.Radius) : 0.0;
        const double Compression = FMath::Max(CompressionCm, 0.0) * 0.01;
        const double CompressionSpeed = (Compression - State.Compression) / Dt;
        State.Compression = Compression;
        State.bContact = Contact.bHit;
        if (!Contact.bHit)
        {
            continue;
        }
        double Force = Setup.SpringRate * Compression;
        const double BumpStop = (Total - 2.0) * 0.01;
        if (Compression > BumpStop)
        {
            Force += Setup.SpringRate * 8.0 * (Compression - BumpStop);
        }
        Force += CompressionSpeed * (CompressionSpeed > 0.0 ? Setup.BumpDamping : Setup.ReboundDamping);
        Contact.Force = Force;
        Contact.Point = Hit.ImpactPoint;
        Contact.Normal = Hit.ImpactNormal;
        if (const UPhysicalMaterial* Material = Hit.PhysMaterial.Get())
        {
            Contact.Surface.Friction = Material->Friction;
            if (const URallyPhysicalMaterial* RallyMaterial = Cast<URallyPhysicalMaterial>(Material))
            {
                Contact.Surface.Looseness = RallyMaterial->Looseness;
                Contact.RollingResistance = RallyMaterial->RollingResistance;
            }
        }
    }

    const auto AntiRoll = [&](int32 Left, int32 Right, double Rate)
    {
        if (Right >= NumWheels)
        {
            return;
        }
        const double Transfer = Rate * (Wheels[Left].Compression - Wheels[Right].Compression);
        if (Contacts[Left].bHit) Contacts[Left].Force += Transfer;
        if (Contacts[Right].bHit) Contacts[Right].Force -= Transfer;
    };
    AntiRoll(0, 1, PhysicsFrontAntiRoll);
    AntiRoll(2, 3, PhysicsRearAntiRoll);

    // Tyre kinematics at the contact patch.
    TArray<int32, TInlineAllocator<2>> Driven;
    double DrivenGroundOmega = 0.0;
    for (int32 I = 0; I < NumWheels; ++I)
    {
        const FRallyWheelSetup& Setup = PhysicsWheels[I];
        FWheelState& State = Wheels[I];
        FContact& Contact = Contacts[I];
        Contact.Force = FMath::Max(Contact.Force, 0.0);
        State.Load = Contact.Force;
        State.SteerDeg = Setup.bSteered ? Input.Steer * PhysicsMaxSteer : 0.0;
        const FVector WheelForward = FQuat(Up, FMath::DegreesToRadians(State.SteerDeg)).RotateVector(Forward);
        Contact.ForwardDir = (WheelForward - Contact.Normal * FVector::DotProduct(WheelForward, Contact.Normal)).GetSafeNormal();
        Contact.RightDir = FVector::CrossProduct(Contact.Normal, Contact.ForwardDir);
        const FVector PointVelocity = LinearVelocity + FVector::CrossProduct(AngularVelocity, (Contact.bHit ? Contact.Point : Contact.Pivot) - Com);
        Contact.SpeedLong = FVector::DotProduct(PointVelocity, Contact.ForwardDir) * 0.01;
        Contact.SpeedLat = FVector::DotProduct(PointVelocity, Contact.RightDir) * 0.01;
        Contact.Tyre = Rally::Tyre::Resolve(TyreSpecs[I], Contact.Surface);
        if (Setup.bDriven && Driven.Num() < 2)
        {
            Driven.Add(I);
            DrivenGroundOmega += Contact.SpeedLong / (Setup.Radius * 0.01) * 0.5;
        }
    }

    Rally::Drivetrain::UpdateAutomatic(GearboxSpec, Shift, DrivenGroundOmega, Input.Throttle, Input.bReverse, Dt);

    // Wheel spin, engine and tyre forces are stiff together: substep them, then apply average forces.
    const double SubDt = Dt / DrivetrainSubsteps;
    TArray<double, TInlineAllocator<4>> GroundTorque, BrakeTorque;
    GroundTorque.SetNumZeroed(NumWheels);
    BrakeTorque.SetNumZeroed(NumWheels);
    for (int32 Step = 0; Step < DrivetrainSubsteps; ++Step)
    {
        for (int32 I = 0; I < NumWheels; ++I)
        {
            const FRallyWheelSetup& Setup = PhysicsWheels[I];
            FWheelState& State = Wheels[I];
            FContact& Contact = Contacts[I];
            const double Radius = Setup.Radius * 0.01;
            GroundTorque[I] = 0.0;
            // Reference speeds stop slip exploding near standstill.
            State.SlipRatio = (State.Omega * Radius - Contact.SpeedLong) / FMath::Max(FMath::Abs(Contact.SpeedLong), 3.0);
            // ABS releases the pedal brake each substep the wheel is past peak slip; the handbrake is unassisted.
            const bool bLocking = bPhysicsABS && Contact.bHit && State.SlipRatio < -1.2 * Contact.Tyre.PeakSlipRatio;
            BrakeTorque[I] = Input.Brake * Setup.MaxBrakeTorque * (bLocking ? 0.2 : 1.0) + (Input.bHandbrake ? Setup.HandbrakeTorque : 0.0);
            if (!Contact.bHit)
            {
                continue;
            }
            const double TanSlipAngle = Contact.SpeedLat / FMath::Max(FMath::Abs(Contact.SpeedLong), 2.0);
            const Rally::Tyre::FForces Forces = Rally::Tyre::Evaluate(Contact.Tyre, Contact.Surface, State.Load, State.SlipRatio, TanSlipAngle);
            Contact.Fx += Forces.Longitudinal / DrivetrainSubsteps;
            Contact.Fy += Forces.Lateral / DrivetrainSubsteps;
            GroundTorque[I] = -Forces.Longitudinal * Radius;
            BrakeTorque[I] += Contact.RollingResistance * State.Load * Radius;
            State.SlipAngleDeg = FMath::RadiansToDegrees(FMath::Atan(TanSlipAngle));
        }

        if (Driven.Num() == 2)
        {
            FWheelState& Left = Wheels[Driven[0]];
            FWheelState& Right = Wheels[Driven[1]];
            Rally::Drivetrain::FAxleStep Axle;
            Axle.Throttle = Input.Throttle;
            if (bPhysicsTractionControl)
            {
                Axle.Throttle *= Rally::Drivetrain::TractionControlFactor(FMath::Max(Left.SlipRatio, Right.SlipRatio), Contacts[Driven[0]].Tyre.PeakSlipRatio);
            }
            Axle.Gear = Shift.EngagedGear();
            Axle.ClutchEngagement = Rally::Drivetrain::ClutchEngagement(EngineSpec, GearboxSpec, Shift, 0.5 * (Left.Omega + Right.Omega), EngineOmega);
            Axle.GroundTorqueLeft = GroundTorque[Driven[0]];
            Axle.GroundTorqueRight = GroundTorque[Driven[1]];
            Axle.BrakeLeft = BrakeTorque[Driven[0]];
            Axle.BrakeRight = BrakeTorque[Driven[1]];
            Axle.WheelInertia = 0.5 * (PhysicsWheels[Driven[0]].Inertia + PhysicsWheels[Driven[1]].Inertia);
            Axle.Dt = SubDt;
            Rally::Drivetrain::StepDrivenAxle(EngineSpec, GearboxSpec, Axle, EngineOmega, Left.Omega, Right.Omega);
        }
        for (int32 I = 0; I < NumWheels; ++I)
        {
            if (!Driven.Contains(I))
            {
                Wheels[I].Omega = Rally::Drivetrain::StepFreeWheel(Wheels[I].Omega, GroundTorque[I], BrakeTorque[I], PhysicsWheels[I].Inertia, SubDt);
            }
        }
    }

    FVector TotalForce = FVector::ZeroVector;
    FVector TotalTorque = FVector::ZeroVector;
    const auto AddForceAt = [&](const FVector& Force, const FVector& Point)
    {
        TotalForce += Force;
        TotalTorque += FVector::CrossProduct(Point - Com, Force);
    };
    for (int32 I = 0; I < NumWheels; ++I)
    {
        FWheelState& State = Wheels[I];
        const FContact& Contact = Contacts[I];
        State.SpinAngle = FMath::Fmod(State.SpinAngle + State.Omega * Dt, 2.0 * UE_DOUBLE_PI);
        if (!Contact.bHit)
        {
            continue;
        }
        AddForceAt(Up * (Contact.Force * 100.0), Contact.Pivot);
        // Tyre forces act at the roll centre height, which sets how much the body rolls and pitches.
        const FVector TyreForce = (Contact.ForwardDir * Contact.Fx + Contact.RightDir * Contact.Fy) * 100.0;
        AddForceAt(TyreForce, Contact.Point + Contact.Normal * PhysicsWheels[I].RollCentreHeight);
    }
    const FVector Velocity = LinearVelocity * 0.01;
    AddForceAt(-Velocity * Velocity.Size() * (0.5 * AirDensity * PhysicsDragArea * 100.0), Com);
    Handle.AddForce(TotalForce);
    Handle.AddTorque(TotalTorque);

    Visual.Wheels = Wheels;
    Visual.Gear = Shift.IsShifting() ? 0 : Shift.Gear;
    Visual.EngineRpm = EngineOmega * Rally::Drivetrain::RadPerSecToRpm;
}

void ARallyCarPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bAutoDrive)
    {
        TickAutoDrive(DeltaSeconds);
    }

    FRallyCarInput Input;
    if (IsLocallyControlled() || bAutoDrive)
    {
        Input = BuildDriverInput(DeltaSeconds);
        if (HasAuthority())
        {
            ReplicatedInput = Input;
        }
        else
        {
            ServerSetInput(Input);
        }
    }
    else if (HasAuthority() && !GetController())
    {
        // Nobody in the driver's seat: no throttle, handbrake on.
        Input.bHandbrake = true;
        ReplicatedInput = Input;
    }
    else
    {
        Input = ReplicatedInput;
    }

    FVisualState Snapshot;
    {
        FScopeLock Lock(&PhysicsLock);
        PhysicsInput = Input;
        Snapshot = Visual;
    }
    UpdateWheelVisuals(DeltaSeconds);
    UpdateLook(DeltaSeconds);

#if !UE_BUILD_SHIPPING
    if (IsLocallyControlled() && GEngine)
    {
        const FString Gear = Snapshot.Gear < 0 ? TEXT("R") : Snapshot.Gear == 0 ? TEXT("N") : FString::FromInt(Snapshot.Gear);
        double Spin = 0.0;
        for (int32 I = 0; I < FMath::Min(Snapshot.Wheels.Num(), WheelSetups.Num()); ++I)
        {
            if (WheelSetups[I].bDriven)
            {
                Spin = FMath::Max(Spin, Snapshot.Wheels[I].SlipRatio);
            }
        }
        // Wheelspin decouples rpm from road speed; show it so the readout makes sense.
        const FString SpinText = Spin > 0.2 ? FString::Printf(TEXT("   wheelspin %.0f%%"), Spin * 100.0) : FString();
        GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::White,
            FString::Printf(TEXT("%.0f km/h   %s   %.0f rpm%s"), GetForwardSpeedKmh(), *Gear, Snapshot.EngineRpm, *SpinText));
    }
#endif
}

void ARallyCarPawn::UpdateWheelVisuals(float DeltaSeconds)
{
    TArray<FWheelState> States;
    {
        FScopeLock Lock(&PhysicsLock);
        States = Visual.Wheels;
    }
    const int32 Count = FMath::Min3(WheelMeshes.Num(), States.Num(), WheelSetups.Num());
    VisualOffsetCm.SetNum(Count);
    VisualSpin.SetNum(Count);
    for (int32 I = 0; I < Count; ++I)
    {
        const FWheelState& State = States[I];
        // Physics steps at 60 Hz while the body is drawn interpolated: smooth wheel travel and spin
        // on the game thread so the wheels do not jitter against the body.
        VisualOffsetCm[I] = FMath::FInterpTo(VisualOffsetCm[I], static_cast<float>(State.OffsetCm), DeltaSeconds, 25.0f);
        VisualSpin[I] = FMath::Fmod(VisualSpin[I] + static_cast<float>(State.Omega) * DeltaSeconds, 2.0f * UE_PI);
        // Rolling forward is a negative pitch about the axle.
        WheelMeshes[I]->SetRelativeLocationAndRotation(AssemblyOffset + WheelSetups[I].Pivot + FVector(0.0, 0.0, VisualOffsetCm[I]),
            FRotator(-FMath::RadiansToDegrees(VisualSpin[I]), State.SteerDeg, 0.0));
    }
}

FRallyCarInput ARallyCarPawn::BuildDriverInput(float DeltaSeconds)
{
    const float Speed = GetForwardSpeedKmh();
    if (!bReversing && BrakeInput > 0.1f && ThrottleInput < 0.1f && Speed < ReverseEngageKmh)
    {
        bReversing = true;
    }
    else if (bReversing && ThrottleInput > 0.1f && Speed > -ReverseEngageKmh)
    {
        bReversing = false;
    }

    const float Rate = FMath::Abs(SteerTarget) > FMath::Abs(SteerCurrent) ? SteerRate : SteerReturnRate;
    SteerCurrent = FMath::FInterpConstantTo(SteerCurrent, SteerTarget, DeltaSeconds, Rate);

    // In reverse the brake pedal drives and the throttle pedal brakes.
    FRallyCarInput Input;
    Input.Throttle = bReversing ? (Speed > -MaxReverseKmh ? BrakeInput : 0.0f) : ThrottleInput;
    Input.Brake = bReversing ? ThrottleInput : BrakeInput;
    // Speed-sensitive lock: full lock at walking pace, about 45% at 100 km/h, so digital steering
    // does not throw the car sideways at speed.
    Input.Steer = SteerSpeedFalloffKmh > 0.0f ? SteerCurrent / (1.0f + FMath::Abs(Speed) / SteerSpeedFalloffKmh) : SteerCurrent;
    Input.bHandbrake = bHandbrake;
    Input.bReverse = bReversing;
    return Input;
}

void ARallyCarPawn::ServerSetInput_Implementation(const FRallyCarInput& Input)
{
    ReplicatedInput.Throttle = FMath::Clamp(Input.Throttle, 0.0f, 1.0f);
    ReplicatedInput.Brake = FMath::Clamp(Input.Brake, 0.0f, 1.0f);
    ReplicatedInput.Steer = FMath::Clamp(Input.Steer, -1.0f, 1.0f);
    ReplicatedInput.bHandbrake = Input.bHandbrake;
    ReplicatedInput.bReverse = Input.bReverse;
}

void ARallyCarPawn::CreateInputObjects()
{
    if (DrivingContext)
    {
        return;
    }

    ThrottleAction = MakeAction(this, EInputActionValueType::Axis1D);
    BrakeAction = MakeAction(this, EInputActionValueType::Axis1D);
    SteerAction = MakeAction(this, EInputActionValueType::Axis1D);
    HandbrakeAction = MakeAction(this, EInputActionValueType::Boolean);
    ResetAction = MakeAction(this, EInputActionValueType::Boolean);
    MouseLookAction = MakeAction(this, EInputActionValueType::Axis2D);
    StickLookAction = MakeAction(this, EInputActionValueType::Axis2D);
    ToggleCameraAction = MakeAction(this, EInputActionValueType::Boolean);

    // Built in code until rebinding needs editable assets.
    DrivingContext = NewObject<UInputMappingContext>(this);
    auto MapNegated = [this](UInputAction* Action, const FKey& Key)
    {
        DrivingContext->MapKey(Action, Key).Modifiers.Add(NewObject<UInputModifierNegate>(DrivingContext));
    };
    DrivingContext->MapKey(ThrottleAction, EKeys::W);
    DrivingContext->MapKey(ThrottleAction, EKeys::Up);
    DrivingContext->MapKey(ThrottleAction, EKeys::Gamepad_RightTriggerAxis);
    DrivingContext->MapKey(BrakeAction, EKeys::S);
    DrivingContext->MapKey(BrakeAction, EKeys::Down);
    DrivingContext->MapKey(BrakeAction, EKeys::Gamepad_LeftTriggerAxis);
    DrivingContext->MapKey(SteerAction, EKeys::D);
    DrivingContext->MapKey(SteerAction, EKeys::Right);
    MapNegated(SteerAction, EKeys::A);
    MapNegated(SteerAction, EKeys::Left);
    DrivingContext->MapKey(SteerAction, EKeys::Gamepad_LeftX);
    DrivingContext->MapKey(HandbrakeAction, EKeys::SpaceBar);
    DrivingContext->MapKey(HandbrakeAction, EKeys::Gamepad_FaceButton_Bottom);
    DrivingContext->MapKey(ResetAction, EKeys::R);
    DrivingContext->MapKey(ResetAction, EKeys::Gamepad_Special_Left);
    DrivingContext->MapKey(MouseLookAction, EKeys::Mouse2D);
    DrivingContext->MapKey(StickLookAction, EKeys::Gamepad_Right2D);
    DrivingContext->MapKey(ToggleCameraAction, EKeys::C);
    DrivingContext->MapKey(ToggleCameraAction, EKeys::Gamepad_RightThumbstick);
}

void ARallyCarPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    CreateInputObjects();

    UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
    Input->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ARallyCarPawn::OnThrottle);
    Input->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ARallyCarPawn::OnThrottle);
    Input->BindAction(BrakeAction, ETriggerEvent::Triggered, this, &ARallyCarPawn::OnBrake);
    Input->BindAction(BrakeAction, ETriggerEvent::Completed, this, &ARallyCarPawn::OnBrake);
    Input->BindAction(SteerAction, ETriggerEvent::Triggered, this, &ARallyCarPawn::OnSteer);
    Input->BindAction(SteerAction, ETriggerEvent::Completed, this, &ARallyCarPawn::OnSteer);
    Input->BindAction(HandbrakeAction, ETriggerEvent::Triggered, this, &ARallyCarPawn::OnHandbrake);
    Input->BindAction(HandbrakeAction, ETriggerEvent::Completed, this, &ARallyCarPawn::OnHandbrake);
    Input->BindAction(ResetAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnReset);
    Input->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ARallyCarPawn::OnMouseLook);
    Input->BindAction(StickLookAction, ETriggerEvent::Triggered, this, &ARallyCarPawn::OnStickLook);
    Input->BindAction(StickLookAction, ETriggerEvent::Completed, this, &ARallyCarPawn::OnStickLook);
    Input->BindAction(ToggleCameraAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnToggleCamera);
}

void ARallyCarPawn::NotifyControllerChanged()
{
    Super::NotifyControllerChanged();

    // Releasing control must not leave the last pedal or steering values applied.
    ThrottleInput = BrakeInput = SteerTarget = 0.0f;
    bHandbrake = false;

    const APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC || !PC->IsLocalController())
    {
        return;
    }
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
    {
        CreateInputObjects();
        Subsystem->AddMappingContext(DrivingContext, 0);
    }
}

void ARallyCarPawn::OnThrottle(const FInputActionValue& Value)
{
    ThrottleInput = FMath::Clamp(Value.Get<float>(), 0.0f, 1.0f);
}

void ARallyCarPawn::OnBrake(const FInputActionValue& Value)
{
    BrakeInput = FMath::Clamp(Value.Get<float>(), 0.0f, 1.0f);
}

void ARallyCarPawn::OnSteer(const FInputActionValue& Value)
{
    SteerTarget = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f);
}

void ARallyCarPawn::OnHandbrake(const FInputActionValue& Value)
{
    bHandbrake = Value.Get<bool>();
}

void ARallyCarPawn::OnReset(const FInputActionValue& Value)
{
    ResetUpright();
}

void ARallyCarPawn::OnMouseLook(const FInputActionValue& Value)
{
    const FVector2D Delta = Value.Get<FVector2D>();
    MouseLook.X = FMath::Clamp(MouseLook.X + Delta.X * MouseLookSensitivity * 10.0f, -MaxLookYaw, MaxLookYaw);
    MouseLook.Y = FMath::Clamp(MouseLook.Y + Delta.Y * MouseLookSensitivity * 10.0f, -60.0f, 40.0f);
    MouseIdleTime = 0.0f;
}

void ARallyCarPawn::OnStickLook(const FInputActionValue& Value)
{
    // The stick points the head: full deflection looks over the shoulder, release looks ahead.
    StickLook = Value.Get<FVector2D>();
}

void ARallyCarPawn::OnToggleCamera(const FInputActionValue& Value)
{
    SetCockpitView(!bCockpitView);
}

void ARallyCarPawn::SetCockpitView(bool bCockpit)
{
    bCockpitView = bCockpit;
    CockpitCamera->SetActive(bCockpit);
    Camera->SetActive(!bCockpit);
}

void ARallyCarPawn::UpdateLook(float DeltaSeconds)
{
    MouseIdleTime += DeltaSeconds;
    if (MouseIdleTime > LookRecentreDelay)
    {
        MouseLook = FMath::Vector2DInterpTo(MouseLook, FVector2D::ZeroVector, DeltaSeconds, 3.0f);
    }
    const float Yaw = FMath::Clamp(MouseLook.X + StickLook.X * MaxLookYaw, -MaxLookYaw, MaxLookYaw);
    const float Pitch = FMath::Clamp(MouseLook.Y + StickLook.Y * 40.0f, -60.0f, 40.0f);
    CockpitCamera->SetRelativeRotation(FRotator(Pitch, Yaw, 0.0f));
}

void ARallyCarPawn::ResetUpright()
{
    if (!HasAuthority())
    {
        ServerResetUpright();
        return;
    }
    const FRotator Upright(0.0, GetActorRotation().Yaw, 0.0);
    Body->SetWorldLocationAndRotation(GetActorLocation() + FVector(0.0, 0.0, 150.0), Upright, false, nullptr, ETeleportType::ResetPhysics);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    FScopeLock Lock(&PhysicsLock);
    bResetWheelState = true;
}

void ARallyCarPawn::ServerResetUpright_Implementation()
{
    ResetUpright();
}

void ARallyCarPawn::TickAutoDrive(float DeltaSeconds)
{
    struct FPhase
    {
        float EndTime;
        const TCHAR* Name;
        float Throttle;
        float Brake;
        float Steer;
        bool bHandbrake;
    };
    // Drop/settle, straight-line acceleration and braking on the gravel road, then off the road onto the
    // uniform loose ground for steady cornering (no surface edge mid-corner), handbrake turn, reverse.
    static const FPhase Phases[] =
    {
        { 2.5f, TEXT("settle"), 0.0f, 0.0f, 0.0f, false },
        { 10.0f, TEXT("accelerate"), 1.0f, 0.0f, 0.0f, false },
        { 15.0f, TEXT("brake"), 0.0f, 1.0f, 0.0f, false },
        { 20.0f, TEXT("accelerate2"), 1.0f, 0.0f, 0.0f, false },
        { 21.5f, TEXT("exit_road"), 0.6f, 0.0f, 0.3f, false },
        { 22.5f, TEXT("straighten"), 0.6f, 0.0f, 0.0f, false },
        { 28.5f, TEXT("corner"), 0.5f, 0.0f, 0.35f, false },
        { 30.0f, TEXT("handbrake_turn"), 0.0f, 0.0f, 1.0f, true },
        { 33.5f, TEXT("brake2"), 0.0f, 1.0f, 0.0f, false },
        { 36.5f, TEXT("reverse"), 0.0f, 1.0f, 0.0f, false },
        { 38.5f, TEXT("stop_reverse"), 1.0f, 0.0f, 0.0f, false },
        { 39.5f, TEXT("coast"), 0.0f, 0.0f, 0.0f, false },
    };

    AutoDriveTime += DeltaSeconds;
    const FPhase* Phase = nullptr;
    for (const FPhase& Candidate : Phases)
    {
        if (AutoDriveTime < Candidate.EndTime)
        {
            Phase = &Candidate;
            break;
        }
    }

    if (!Phase)
    {
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_AUTODRIVE_DONE travelled=%.1fm"), FVector::Dist2D(GetActorLocation(), AutoDriveStart) / 100.0);
        bAutoDrive = false;
        ThrottleInput = BrakeInput = SteerTarget = 0.0f;
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyAutoDrive"));
        return;
    }

    ThrottleInput = Phase->Throttle;
    BrakeInput = Phase->Brake;
    SteerTarget = Phase->Steer;
    bHandbrake = Phase->bHandbrake;

    const FVector Location = GetActorLocation();
    const float Kmh = GetForwardSpeedKmh();

    // Straight-line stop: distance from the moment the brake phase starts until below 1 km/h.
    if (Phase->Name != AutoDrivePhase)
    {
        AutoDrivePhase = Phase->Name;
        AutoDrivePhaseStart = Location;
        AutoDrivePhaseKmh = Kmh;
        bAutoDriveStopLogged = false;
    }
    if (FCString::Strncmp(Phase->Name, TEXT("brake"), 5) == 0 && !bAutoDriveStopLogged && FMath::Abs(Kmh) < 1.0f)
    {
        bAutoDriveStopLogged = true;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_AUTODRIVE stop phase=%s from_kmh=%.1f distance=%.1fm"),
            Phase->Name, AutoDrivePhaseKmh, FVector::Dist2D(Location, AutoDrivePhaseStart) / 100.0);
    }

    // Saved/Screenshots: cockpit after settling and mid-corner, chase view in between.
    const auto Crossed = [&](float Time) { return AutoDriveTime >= Time && AutoDriveTime - DeltaSeconds < Time; };
    if (Crossed(1.9f)) SetCockpitView(true);
    if (Crossed(2.2f)) FScreenshotRequest::RequestScreenshot(TEXT("RallyAutoDrive_cockpit.png"), false, false);
    if (Crossed(2.3f)) SetCockpitView(false);
    if (Crossed(2.45f)) FScreenshotRequest::RequestScreenshot(TEXT("RallyAutoDrive_chase.png"), false, false);
    if (Crossed(2.5f)) SetCockpitView(true);
    if (Crossed(26.0f)) FScreenshotRequest::RequestScreenshot(TEXT("RallyAutoDrive_corner.png"), false, false);

    TArray<FWheelState> States;
    double EngineRpm = 0.0;
    int32 Gear = 0;
    {
        FScopeLock Lock(&PhysicsLock);
        States = Visual.Wheels;
        EngineRpm = Visual.EngineRpm;
        Gear = Visual.Gear;
    }

    // 20 Hz while settling (drop test for ride frequency and damping), then 2 Hz.
    if (AutoDriveTime >= NextAutoDriveLog)
    {
        NextAutoDriveLog += AutoDriveTime < 2.5f ? 0.05f : 0.5f;
        const FRotator Rotation = GetActorRotation();
        const FVector Velocity = GetVelocity();
        const float Forward = FVector::DotProduct(Velocity, GetActorForwardVector());
        const float Right = FVector::DotProduct(Velocity, GetActorRightVector());
        const float YawRate = FMath::DegreesToRadians(Body->GetPhysicsAngularVelocityInDegrees().Z);
        const float LateralG = Forward / 100.0f * YawRate / 9.81f;
        const float SideSlip = FMath::Abs(Forward) > 140.0f ? FMath::RadiansToDegrees(FMath::Atan2(Right, FMath::Abs(Forward))) : 0.0f;
        FString WheelText;
        for (const FWheelState& State : States)
        {
            WheelText += FString::Printf(TEXT(" [%.0fN c%.1f s%.2f a%.1f]"), State.Load, State.Compression * 100.0, State.SlipRatio, State.SlipAngleDeg);
        }
        // origin_z: 0 means the assembly origin sits at its modelled ground level.
        UE_LOG(LogRallyCar, Display,
            TEXT("RALLY_AUTODRIVE t=%.2f phase=%s kmh=%.1f gear=%d rpm=%.0f origin_z=%.1f roll=%.1f pitch=%.1f yaw=%.0f lat_g=%.2f slip=%.1f wheels%s at=%.1f,%.1f"),
            AutoDriveTime, Phase->Name, Kmh, Gear, EngineRpm, Location.Z + AssemblyOffset.Z,
            Rotation.Roll, Rotation.Pitch, Rotation.Yaw, LateralG, SideSlip, *WheelText, Location.X / 100.0, Location.Y / 100.0);
    }
}
