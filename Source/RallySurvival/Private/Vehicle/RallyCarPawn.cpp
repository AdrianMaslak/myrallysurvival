#include "Vehicle/RallyCarPawn.h"
#include "Game/RallyStageDirector.h"
#include "Game/RallyDayClock.h"
#include "Game/WeatherRules.h"
#include "Components/SpotLightComponent.h"
#include "Game/RallyGameMode.h"
#include "Game/RallyHUD.h"
#include "Player/BodyRules.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Damage/ImpactRules.h"
#include "Damage/RallyPanelComponent.h"
#include "Damage/RallyBodyDeformComponent.h"
#include "Damage/RallyDamageComponent.h"
#include "Damage/RallyDebrisActor.h"
#include "Damage/RallyPuddleField.h"
#include "Damage/RallyLeakComponent.h"
#include "Damage/RallyEngineComponent.h"
#include "Damage/EngineRules.h"
#include "Audio/RallySynthComponent.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AudioDevice.h"
#include "Misc/App.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Damage/UndersideRules.h"
#include "Engine/StaticMeshActor.h"
#include "Damage/SuspensionRules.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GameFramework/Character.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Player/RallyCharacter.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Engine/StaticMeshSocket.h"
#include "Materials/MaterialInstanceDynamic.h"

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
        { TEXT("FL"), TEXT("/Game/Vehicles/Octavia/Suspension/SM_Susp_Wheel_FL.SM_Susp_Wheel_FL"), FVector(126.3, -75.1, 28.8) },
        { TEXT("FR"), TEXT("/Game/Vehicles/Octavia/Suspension/SM_Susp_Wheel_FR.SM_Susp_Wheel_FR"), FVector(126.3, 75.1, 28.8) },
        { TEXT("RL"), TEXT("/Game/Vehicles/Octavia/Suspension/SM_Susp_Wheel_RL.SM_Susp_Wheel_RL"), FVector(-130.7, -70.2, 27.2) },
        { TEXT("RR"), TEXT("/Game/Vehicles/Octavia/Suspension/SM_Susp_Wheel_RR.SM_Susp_Wheel_RR"), FVector(-130.7, 70.2, 27.2) },
    };

    const FPartMount PanelMounts[] =
    {
        { TEXT("Door_FL"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Door_FL.SM_Octavia_Door_FL"), FVector(27.6, -77.1, 84.8) },
        { TEXT("Door_FR"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Door_FR.SM_Octavia_Door_FR"), FVector(27.6, 77.1, 84.8) },
        { TEXT("Door_RL"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Door_RL.SM_Octavia_Door_RL"), FVector(-77.4, -69.8, 84.8) },
        { TEXT("Door_RR"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Door_RR.SM_Octavia_Door_RR"), FVector(-77.4, 69.8, 84.8) },
        { TEXT("Bonnet"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Bonnet.SM_Octavia_Bonnet"), FVector(153.7, 0.0, 83.4) },
        { TEXT("Trunk"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Trunk.SM_Octavia_Trunk"), FVector(-175.8, 0.0, 100.9) },
        // The 1.8T set's radiator, fan shroud and hoses (engine_18t.json): up over the subframe's crossmember.
        { TEXT("Radiator"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Radiator.SM_Engine18T_Radiator"), FVector(189.47, 16.0, 56.3) },
        { TEXT("FrontBumper"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_FrontBumper.SM_Octavia_FrontBumper"), FVector(190.9, 0.0, 47.1) },
        { TEXT("RearBumper"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_RearBumper.SM_Octavia_RearBumper"), FVector(-192.7, 0.1, 43.6) },
        { TEXT("Fender_FL"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Fender_FL.SM_Octavia_Fender_FL"), FVector(140.7, -77.6, 55.6) },
        { TEXT("Fender_FR"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Fender_FR.SM_Octavia_Fender_FR"), FVector(140.7, 77.6, 55.6) },
        { TEXT("Headlight_L"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Headlight_L.SM_Octavia_Headlight_L"), FVector(200.9, -54.5, 64.4) },
        { TEXT("Headlight_R"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Headlight_R.SM_Octavia_Headlight_R"), FVector(200.9, 54.5, 64.4) },
        { TEXT("Taillight_L"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Taillight_L.SM_Octavia_Taillight_L"), FVector(-218.8, -60.5, 85.3) },
        { TEXT("Taillight_R"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Taillight_R.SM_Octavia_Taillight_R"), FVector(-218.8, 60.5, 85.3) },
    };

    // Each corner's moving suspension, the v2 suspension set's (Art/Vehicles/Octavia/Suspension_v2, suspension_v2.json):
    // the mesh pivot is the fixed end (wheel centre for hub, brake and disc), Moving the end that goes with the hub.
    // Hub and brake (caliper, carrier, pads, hose) follow the wheel's travel and steering; the disc also spins; arms
    // and tie rods swing about their fixed end; struts, the rear springs and the driveshafts swing and stretch.
    // The rear is the 4x4 multi-link.
    enum class ESuspensionRole : uint8 { Hub, Disc, Swing, Stretch };
    struct FSuspensionMount
    {
        const TCHAR* Name;
        int32 Corner;
        ESuspensionRole Role;
        FVector Pivot;
        FVector Moving;
    };
    const FSuspensionMount SuspensionMounts[] =
    {
        { TEXT("Strut_FL"), 0, ESuspensionRole::Stretch, FVector(124.9, -58.3, 78.4), FVector(129.1, -67.1, 31.7) },
        { TEXT("Hub_FL"), 0, ESuspensionRole::Hub, FVector(126.3, -75.1, 28.8), FVector(126.3, -75.1, 28.8) },
        { TEXT("Brake_FL"), 0, ESuspensionRole::Hub, FVector(126.3, -75.1, 28.8), FVector(126.3, -75.1, 28.8) },
        { TEXT("Disc_FL"), 0, ESuspensionRole::Disc, FVector(126.3, -75.1, 28.8), FVector(126.3, -75.1, 28.8) },
        { TEXT("LowerArm_FL"), 0, ESuspensionRole::Swing, FVector(120.0, -37.4, 18.5), FVector(132.1, -72.6, 17.8) },
        { TEXT("Driveshaft_FL"), 0, ESuspensionRole::Stretch, FVector(135.3, -40.8, 30.2), FVector(126.3, -73.9, 28.8) },
        { TEXT("TieRod_FL"), 0, ESuspensionRole::Swing, FVector(115.3, -35.0, 25.8), FVector(115.3, -69.5, 25.8) },
        { TEXT("Strut_FR"), 1, ESuspensionRole::Stretch, FVector(124.9, 58.3, 78.4), FVector(129.1, 67.1, 31.7) },
        { TEXT("Hub_FR"), 1, ESuspensionRole::Hub, FVector(126.3, 75.1, 28.8), FVector(126.3, 75.1, 28.8) },
        { TEXT("Brake_FR"), 1, ESuspensionRole::Hub, FVector(126.3, 75.1, 28.8), FVector(126.3, 75.1, 28.8) },
        { TEXT("Disc_FR"), 1, ESuspensionRole::Disc, FVector(126.3, 75.1, 28.8), FVector(126.3, 75.1, 28.8) },
        { TEXT("LowerArm_FR"), 1, ESuspensionRole::Swing, FVector(120.0, 37.4, 18.5), FVector(132.1, 72.6, 17.8) },
        { TEXT("Driveshaft_FR"), 1, ESuspensionRole::Stretch, FVector(135.3, -1.5, 30.2), FVector(126.3, 73.9, 28.8) },
        { TEXT("TieRod_FR"), 1, ESuspensionRole::Swing, FVector(115.3, 35.0, 25.8), FVector(115.3, 69.5, 25.8) },
        { TEXT("Strut_RL"), 2, ESuspensionRole::Stretch, FVector(-127.5, -52.5, 64.5), FVector(-140.7, -57.1, 16.0) },
        { TEXT("Spring_RL"), 2, ESuspensionRole::Stretch, FVector(-115.0, -47.0, 52.0), FVector(-115.0, -47.0, 27.5) },
        { TEXT("Hub_RL"), 2, ESuspensionRole::Hub, FVector(-130.7, -70.2, 27.2), FVector(-130.7, -70.2, 27.2) },
        { TEXT("Brake_RL"), 2, ESuspensionRole::Hub, FVector(-130.7, -70.2, 27.2), FVector(-130.7, -70.2, 27.2) },
        { TEXT("Disc_RL"), 2, ESuspensionRole::Disc, FVector(-130.7, -70.2, 27.2), FVector(-130.7, -70.2, 27.2) },
        { TEXT("LowerArm_RL"), 2, ESuspensionRole::Swing, FVector(-118.4, -33.5, 27.8), FVector(-118.8, -66.9, 24.0) },
        { TEXT("UpperArm_RL"), 2, ESuspensionRole::Swing, FVector(-124.4, -32.2, 36.0), FVector(-124.7, -68.7, 37.0) },
        { TEXT("TrailingArm_RL"), 2, ESuspensionRole::Swing, FVector(-84.2, -51.5, 34.4), FVector(-130.7, -68.0, 36.2) },
        { TEXT("Driveshaft_RL"), 2, ESuspensionRole::Stretch, FVector(-127.5, -20.1, 29.5), FVector(-130.7, -64.0, 27.2) },
        { TEXT("Strut_RR"), 3, ESuspensionRole::Stretch, FVector(-127.5, 52.5, 64.5), FVector(-140.7, 57.1, 16.0) },
        { TEXT("Spring_RR"), 3, ESuspensionRole::Stretch, FVector(-115.0, 47.0, 52.0), FVector(-115.0, 47.0, 27.5) },
        { TEXT("Hub_RR"), 3, ESuspensionRole::Hub, FVector(-130.7, 70.2, 27.2), FVector(-130.7, 70.2, 27.2) },
        { TEXT("Brake_RR"), 3, ESuspensionRole::Hub, FVector(-130.7, 70.2, 27.2), FVector(-130.7, 70.2, 27.2) },
        { TEXT("Disc_RR"), 3, ESuspensionRole::Disc, FVector(-130.7, 70.2, 27.2), FVector(-130.7, 70.2, 27.2) },
        { TEXT("LowerArm_RR"), 3, ESuspensionRole::Swing, FVector(-118.4, 33.5, 27.8), FVector(-118.8, 66.9, 24.0) },
        { TEXT("UpperArm_RR"), 3, ESuspensionRole::Swing, FVector(-124.4, 32.2, 36.0), FVector(-124.7, 68.7, 37.0) },
        { TEXT("TrailingArm_RR"), 3, ESuspensionRole::Swing, FVector(-84.2, 51.5, 34.4), FVector(-130.7, 68.0, 36.2) },
        { TEXT("Driveshaft_RR"), 3, ESuspensionRole::Stretch, FVector(-127.5, 20.1, 29.5), FVector(-130.7, 64.0, 27.2) },
    };

    // Fixed to the shell but crushing their own way (Damage/CrushProfiles.h).
    struct FBodyPartMount
    {
        const TCHAR* Name;
        const TCHAR* MeshPath;
        FVector Pivot;
        Rally::Crush::EKind Kind;
    };
    const FBodyPartMount BodyPartMounts[] =
    {
        { TEXT("Roof"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Roof.SM_Octavia_Roof"), FVector(-42.9, 0.0, 132.1), Rally::Crush::EKind::Roof },
        { TEXT("Windshield"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Windshield.SM_Octavia_Windshield"), FVector(66.8, 0.0, 114.8), Rally::Crush::EKind::Glass },
        { TEXT("BumperBar_F"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_BumperBar_F.SM_Octavia_BumperBar_F"), FVector(208.7, 0.0, 37.2), Rally::Crush::EKind::BumperBar },
        { TEXT("BumperBar_R"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_BumperBar_R.SM_Octavia_BumperBar_R"), FVector(-225.9, 0.0, 45.4), Rally::Crush::EKind::BumperBar },
        { TEXT("Interior"), TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Interior.SM_Octavia_Interior"), FVector(-58.1, -0.5, 81.2), Rally::Crush::EKind::Interior },
        // The 1.8T 20V engine and gearbox on the crank (scripts/export_engine_18t.py), and what sits round it on
        // the body: battery, washer bottle, fuse box.
        { TEXT("Engine"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Engine.SM_Engine18T_Engine"), FVector(150.7, 16.0, 38.1), Rally::Crush::EKind::Engine },
        { TEXT("Bay"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Bay.SM_Engine18T_Bay"), FVector(151.272, 10.72, 56.184), Rally::Crush::EKind::Engine },
    };
    // Engine modules, in URallyPanelComponent::EModule order: the 1.8T set's (engine_18t.json pivots; their
    // meshes carry Fixing sockets where the game's fixings go). The intercooler is the side-mount one in the
    // left front corner, with its charge pipes from the turbo and up to the throttle.
    struct FModuleMount { const TCHAR* Name; const TCHAR* MeshPath; FVector Pivot; };
    const FModuleMount ModuleMounts[] =
    {
        { TEXT("EngineCover"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_EngineCover.SM_Engine18T_EngineCover"), FVector(157.407, 17.76, 74.292) },
        { TEXT("AirFilter"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_AirFilter.SM_Engine18T_AirFilter"), FVector(176.342, -28.0, 65.739) },
        { TEXT("Intake"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Intake.SM_Engine18T_Intake"), FVector(168.994, 14.592, 55.348) },
        { TEXT("ExhaustManifold"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_ExhaustManifold.SM_Engine18T_ExhaustManifold"), FVector(134.068, 16.0, 59.704) },
        { TEXT("Turbo"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Turbo.SM_Engine18T_Turbo"), FVector(132.705, 15.252, 40.275) },
        { TEXT("Intercooler"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Intercooler.SM_Engine18T_Intercooler"), FVector(201.78, -24.8, 27.012) },
        { TEXT("Alternator"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Alternator.SM_Engine18T_Alternator"), FVector(166.1, 37.56, 51.3) },
        { TEXT("Starter"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Starter.SM_Engine18T_Starter"), FVector(167.86, -3.976, 32.82) },
        { TEXT("ECU"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_ECU.SM_Engine18T_ECU"), FVector(126.0, -49.5, 74.0) },
        { TEXT("Coil1"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Coil1.SM_Engine18T_Coil1"), FVector(152.152, 4.384, 70.66) },
        { TEXT("Coil2"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Coil2.SM_Engine18T_Coil2"), FVector(152.152, 12.128, 70.66) },
        { TEXT("Coil3"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Coil3.SM_Engine18T_Coil3"), FVector(152.152, 19.872, 70.66) },
        { TEXT("Coil4"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Coil4.SM_Engine18T_Coil4"), FVector(152.152, 27.616, 70.66) },
    };
    // The crank pulley's middle, on the crank's axis; the crank itself (the engine rolls about it).
    const FVector CrankPulleyPivot(150.7, 39.364, 38.1);
    // The radiator fans' hubs; each turns about the car's length.
    const FVector FanPivots[2] = { FVector(185.884, 3.68, 55.86), FVector(185.884, 28.32, 55.86) };
    // The poly-V accessory belt, and the pulleys it turns: the alternator's (on the alternator), the power-steering
    // pump's and the air-conditioning compressor's (engine_18t.json). Each turns as many times for one turn of the
    // crank as the crank pulley (13.5 cm across) is bigger than it.
    const FVector BeltPivot(159.06, 40.112, 47.34);
    const FVector AlternatorPulleyPivot(169.62, 37.824, 45.58);
    const FVector PulleyPivots[2] = { FVector(166.54, 38.462, 59.66), FVector(169.18, 39.76, 34.58) };
    constexpr double AlternatorPulleyRatio = 13.48 / 4.93;
    const double PulleyRatios[2] = { 13.48 / 10.56, 13.48 / 9.68 };
    // The belt's texture runs this many lengths per turn of the crank, shown at most this fast (any quicker and it
    // only flickers).
    constexpr double BeltUvPerCrankTurn = 1.8;
    constexpr double BeltUvMaxPerSecond = 3.0;
    const FVector CrankPoint(150.7, 16.0, 38.1);

    // The body box is the actor root and spans 20..140 cm above the assembly origin.
    const FVector BodyHalfExtent(225.0, 86.0, 60.0);
    // A jack lifts at a steady wind and holds its corner stiffly; its foot grips the ground.
    constexpr double JackStiffness = 150000.0;
    constexpr double JackDamping = 15000.0;
    constexpr double JackGrip = 20000.0;
    constexpr double JackWindSpeed = 0.1;
    const FVector AssemblyOffset(0.0, 0.0, -80.0);
    const float VehicleMassKg = 1300.0f;
    const double AirDensity = 1.2;
    const int32 DrivetrainSubsteps = 8;
    // Driver's eye in the assembly frame: above the left front seat, about 50 cm behind the steering
    // wheel centre (49, -39, 86), clear of the windscreen top (137). From scripts/inspect_octavia_cabin.py.
    const FVector DriverEye(-2.0, -37.0, 114.0);
    // SM_Octavia_SteeringWheel hub and column axis (pointing at the driver), from assembly.json.
    const FVector SteeringWheelPivot(47.3, -38.6, 87.4);
    const FVector SteeringColumnAxis(-0.920, -0.004, 0.391);

    // Per front seat (driver, co-driver), assembly frame: eye point, standing point outside the door
    // (character capsule centre), door hinge near the A-pillar, and which way the door swings.
    struct FSeatMount
    {
        FVector Eye;
        FVector Outside;
        FVector Hinge;
        float OpenYaw;
    };
    const FSeatMount SeatMounts[] =
    {
        { FVector(-2.0, -37.0, 114.0), FVector(15.0, -140.0, 92.0), FVector(85.0, -80.0, 85.0), 65.0f },
        { FVector(-2.0, 38.0, 114.0), FVector(15.0, 140.0, 92.0), FVector(85.0, 80.0, 85.0), -65.0f },
    };
    const float DoorReach = 160.0f;

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

    // Wheel heading offset from toe: positive toe-out turns a left wheel left and a right wheel right.
    double ToeYawDeg(const FRallyWheelSetup& Wheel)
    {
        return Wheel.Pivot.Y < 0.0 ? -Wheel.ToeOutDeg : Wheel.ToeOutDeg;
    }

    const ERallyPart TyreParts[] = { ERallyPart::TyreFrontLeft, ERallyPart::TyreFrontRight, ERallyPart::TyreRearLeft, ERallyPart::TyreRearRight };
    const ERallyPart SuspensionParts[] = { ERallyPart::SuspensionFrontLeft, ERallyPart::SuspensionFrontRight, ERallyPart::SuspensionRearLeft, ERallyPart::SuspensionRearRight };
    const ERallyPart HubParts[] = { ERallyPart::HubFrontLeft, ERallyPart::HubFrontRight, ERallyPart::HubRearLeft, ERallyPart::HubRearRight };
    const ERallyPart StrutParts[] = { ERallyPart::StrutFrontLeft, ERallyPart::StrutFrontRight, ERallyPart::StrutRearLeft, ERallyPart::StrutRearRight };
    const ERallyPart BrakeParts[] = { ERallyPart::BrakeFrontLeft, ERallyPart::BrakeFrontRight, ERallyPart::BrakeRearLeft, ERallyPart::BrakeRearRight };
    // Front corners only.
    const ERallyPart DriveshaftParts[] = { ERallyPart::DriveshaftFrontLeft, ERallyPart::DriveshaftFrontRight };

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
    // Movement is replicated by NetState snapshots, not by engine physics replication.
    SetReplicatingMovement(false);
    SetNetUpdateFrequency(60.0f);

    Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
    Body->SetBoxExtent(BodyHalfExtent);
    Body->SetCollisionProfileName(UCollisionProfile::Vehicle_ProfileName);
    Body->SetSimulatePhysics(true);
    Body->BodyInstance.SetMassOverride(VehicleMassKg);
    // Centre of gravity about 50 cm above ground, ahead of mid-wheelbase for the front engine.
    Body->BodyInstance.COMNudge = FVector(20.0, 0.0, -30.0);
    // Impacts dent the body and damage parts (OnBodyHit).
    Body->SetNotifyRigidBodyCollision(true);
    SetRootComponent(Body);

    ChassisMesh = CreateVisualMesh(this, Body, TEXT("ChassisMesh"),
        TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Chassis.SM_Octavia_Chassis"), AssemblyOffset);
    for (const FPartMount& Panel : PanelMounts)
    {
        UStaticMeshComponent* Mesh = CreateVisualMesh(this, Body, FName(FString(TEXT("Panel_")) + Panel.Name), Panel.MeshPath, AssemblyOffset + Panel.Pivot);
        PanelMeshes.Add(Mesh);
        if (FCString::Strcmp(Panel.Name, TEXT("Door_FL")) == 0 || FCString::Strcmp(Panel.Name, TEXT("Door_FR")) == 0)
        {
            FrontDoorMeshes.Add(Mesh);
        }
    }
    for (const FBodyPartMount& Part : BodyPartMounts)
    {
        BodyPartMeshes.Add(CreateVisualMesh(this, Body, FName(FString(TEXT("Part_")) + Part.Name), Part.MeshPath, AssemblyOffset + Part.Pivot));
    }
    ExhaustMesh = CreateVisualMesh(this, Body, TEXT("Part_Exhaust"),
        TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Exhaust.SM_Octavia_Exhaust"), AssemblyOffset + ExhaustHanger());
    for (const FSuspensionMount& Part : SuspensionMounts)
    {
        const FString Path = FString::Printf(TEXT("/Game/Vehicles/Octavia/Suspension/SM_Susp_%s.SM_Susp_%s"), Part.Name, Part.Name);
        SuspensionMeshes.Add(CreateVisualMesh(this, Body, FName(FString(TEXT("Suspension_")) + Part.Name), *Path, AssemblyOffset + Part.Pivot));
    }
    // The subframes, steering rack, anti-roll bars and rear final drive, fixed to the body.
    SuspensionStaticMesh = CreateVisualMesh(this, Body, TEXT("SuspensionStatic"),
        TEXT("/Game/Vehicles/Octavia/Suspension/SM_Susp_SuspensionStatic.SM_Susp_SuspensionStatic"), AssemblyOffset);
    for (const FModuleMount& Part : ModuleMounts)
    {
        ModuleMeshes.Add(CreateVisualMesh(this, Body, FName(FString(TEXT("Module_")) + Part.Name), Part.MeshPath, AssemblyOffset + Part.Pivot));
    }
    CrankPulleyMesh = CreateVisualMesh(this, Body, TEXT("CrankPulley"),
        TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Pulley.SM_Engine18T_Pulley"), AssemblyOffset + CrankPulleyPivot);
    FanMeshes.Add(CreateVisualMesh(this, Body, TEXT("FanLeft"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_FanL.SM_Engine18T_FanL"),
        AssemblyOffset + FanPivots[0]));
    FanMeshes.Add(CreateVisualMesh(this, Body, TEXT("FanRight"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_FanR.SM_Engine18T_FanR"),
        AssemblyOffset + FanPivots[1]));
    BeltMesh = CreateVisualMesh(this, Body, TEXT("AccessoryBelt"), TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_Belt.SM_Engine18T_Belt"),
        AssemblyOffset + BeltPivot);
    PulleyMeshes.Add(CreateVisualMesh(this, Body, TEXT("PulleyPowerSteering"),
        TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_PulleyPAS.SM_Engine18T_PulleyPAS"), AssemblyOffset + PulleyPivots[0]));
    PulleyMeshes.Add(CreateVisualMesh(this, Body, TEXT("PulleyAirCon"),
        TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_PulleyAC.SM_Engine18T_PulleyAC"), AssemblyOffset + PulleyPivots[1]));
    // On the alternator, so it goes where the alternator goes (rocking with the engine, hanging loose, or off).
    {
        const int32 Alternator = static_cast<int32>(URallyPanelComponent::EModule::Alternator);
        AlternatorPulleyMesh = CreateVisualMesh(this, ModuleMeshes[Alternator], TEXT("PulleyAlternator"),
            TEXT("/Game/Vehicles/Octavia/Engine18T/SM_Engine18T_PulleyAlternator.SM_Engine18T_PulleyAlternator"),
            AlternatorPulleyPivot - ModuleMounts[Alternator].Pivot);
    }
    // Headlamps: dipped beams from the lamp units, a little down and out.
    for (int32 Side = 0; Side < 2; ++Side)
    {
        USpotLightComponent* Lamp = CreateDefaultSubobject<USpotLightComponent>(Side == 0 ? TEXT("HeadlampLeft") : TEXT("HeadlampRight"));
        Lamp->SetupAttachment(Body);
        Lamp->SetRelativeLocationAndRotation(AssemblyOffset + FVector(206.0, Side == 0 ? -54.0 : 54.0, 64.0), FRotator(-4.0, Side == 0 ? -3.0 : 3.0, 0.0));
        Lamp->SetIntensity(9000.0f);
        Lamp->SetAttenuationRadius(6500.0f);
        Lamp->SetInnerConeAngle(14.0f);
        Lamp->SetOuterConeAngle(34.0f);
        Lamp->SetLightColor(FLinearColor(1.0f, 0.93f, 0.8f));
        Lamp->SetCastShadows(false);
        Lamp->SetVisibility(false);
        (Side == 0 ? HeadlampLeft : HeadlampRight) = Lamp;
    }
    Occupants.SetNum(SeatCount);
    BodyDeform = CreateDefaultSubobject<URallyBodyDeformComponent>(TEXT("BodyDeform"));
    Damage = CreateDefaultSubobject<URallyDamageComponent>(TEXT("Damage"));
    Panels = CreateDefaultSubobject<URallyPanelComponent>(TEXT("Panels"));
    Engine = CreateDefaultSubobject<URallyEngineComponent>(TEXT("Engine"));
    // Sound: the engine voice sits between the engine and the exhaust, the road voice and one-shots at the
    // middle of the car.
    EngineSound = CreateDefaultSubobject<URallySynthComponent>(TEXT("EngineSound"));
    EngineSound->SetupAttachment(Body);
    EngineSound->SetRelativeLocation(AssemblyOffset + FVector(60.0, 0.0, 40.0));
    EngineSound->Configure(ERallySoundKind::Engine, 250.0f, 4.0f);
    RoadSound = CreateDefaultSubobject<URallySynthComponent>(TEXT("RoadSound"));
    RoadSound->SetupAttachment(Body);
    RoadSound->Configure(ERallySoundKind::Road, 120.0f, 3.0f);
    EventSound = CreateDefaultSubobject<URallySynthComponent>(TEXT("EventSound"));
    EventSound->SetupAttachment(Body);
    EventSound->Configure(ERallySoundKind::Events, 200.0f, 4.0f);
    // Drops fall free of the car too.
    Leaks = CreateDefaultSubobject<URallyLeakComponent>(TEXT("Leaks"));
    LeakDrops = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LeakDrops"));
    LeakDrops->SetupAttachment(Body);
    LeakDrops->SetUsingAbsoluteLocation(true);
    LeakDrops->SetUsingAbsoluteRotation(true);
    LeakDrops->SetUsingAbsoluteScale(true);
    LeakDrops->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    LeakDrops->SetGenerateOverlapEvents(false);
    LeakDrops->SetCastShadow(false);
    LeakDrops->NumCustomDataFloats = 4;
    // Steam and smoke drift free of the car, so the puffs live in world space.
    Puffs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Puffs"));
    Puffs->SetupAttachment(Body);
    Puffs->SetUsingAbsoluteLocation(true);
    Puffs->SetUsingAbsoluteRotation(true);
    Puffs->SetUsingAbsoluteScale(true);
    Puffs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Puffs->SetGenerateOverlapEvents(false);
    Puffs->SetCastShadow(false);
    Puffs->NumCustomDataFloats = 4;
    {
        ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        if (Sphere.Succeeded()) { Puffs->SetStaticMesh(Sphere.Object); LeakDrops->SetStaticMesh(Sphere.Object); }
        ConstructorHelpers::FObjectFinder<UMaterialInterface> Steam(TEXT("/Game/Vehicles/Octavia/Prototype/M_Prototype_Steam.M_Prototype_Steam"));
        if (Steam.Succeeded()) { Puffs->SetMaterial(0, Steam.Object); LeakDrops->SetMaterial(0, Steam.Object); }
    }
    SteeringWheelMesh = CreateVisualMesh(this, Body, TEXT("SteeringWheelMesh"),
        TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_SteeringWheel.SM_Octavia_SteeringWheel"), AssemblyOffset + SteeringWheelPivot);
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
    DOREPLIFETIME(ARallyCarPawn, BiasSteps);
    DOREPLIFETIME(ARallyCarPawn, bLightsOn);
    DOREPLIFETIME_CONDITION(ARallyCarPawn, NetState, COND_SkipOwner);
    DOREPLIFETIME(ARallyCarPawn, Occupants);
    DOREPLIFETIME(ARallyCarPawn, DoorsOpen);
    DOREPLIFETIME(ARallyCarPawn, PanelTestPhase);
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
    BaseWheelSetups = WheelSetups;
    ApplySetup();
    bNetLog = FParse::Param(FCommandLine::Get(), TEXT("RallyNetLog"));
    bCrashTest = FParse::Param(FCommandLine::Get(), TEXT("RallyCrashTest"));

    Body->OnComponentHit.AddDynamic(this, &ARallyCarPawn::OnBodyHit);
    Damage->OnDamageChanged.AddDynamic(this, &ARallyCarPawn::OnDamageChanged);
    Engine->Initialize(Damage, Puffs);
    Engine->OnEngineChanged.AddUObject(this, &ARallyCarPawn::OnEngineChanged);
    Engine->OnStartFailed.AddUObject(this, &ARallyCarPawn::OnStartFailed);
    Panels->OnFixingLoose.AddUObject(this, &ARallyCarPawn::OnFixingLoose);
    if (GetNetMode() != NM_DedicatedServer && FApp::CanEverRenderAudio() && GetWorld()->GetAudioDeviceRaw())
    {
        // The voices peak well below full scale; lift them to sit with the rest of the game.
        EngineSound->SetVolumeMultiplier(2.0f);
        RoadSound->SetVolumeMultiplier(1.6f);
        EventSound->SetVolumeMultiplier(1.6f);
        EngineSound->Start();
        RoadSound->Start();
        EventSound->Start();
    }
    bEngineTest = bCrashTest && FParse::Param(FCommandLine::Get(), TEXT("RallyEngineTest"));
    bUndersideTest = FParse::Param(FCommandLine::Get(), TEXT("RallyUndersideTest"));
    bLeakTest = FParse::Param(FCommandLine::Get(), TEXT("RallyLeakTest"));
    bKnockTest = FParse::Param(FCommandLine::Get(), TEXT("RallyKnockTest"));
    bPartsTest = FParse::Param(FCommandLine::Get(), TEXT("RallyPartsTest"));
    bBoltSurvey = FParse::Param(FCommandLine::Get(), TEXT("RallyBoltSurvey"));
    bStageDrive = FParse::Param(FCommandLine::Get(), TEXT("RallyStageDrive"));
    // Replicated part condition may have arrived before BeginPlay.
    OnDamageChanged();
    // Server geometry also validates the 15 cm proper-refit limit.
    {
        // Oil soaks the engine block and coolant wets the radiator (URallyLeakComponent): their own material
        // instances, made before the dent-able copies take the materials over.
        TArray<UMaterialInstanceDynamic*> EngineLooks;
        for (int32 Part = 0; Part < BodyPartMeshes.Num(); ++Part)
            if (FCString::Strcmp(BodyPartMounts[Part].Name, TEXT("Engine")) == 0)
                for (int32 Slot = 0; Slot < BodyPartMeshes[Part]->GetNumMaterials(); ++Slot)
                    if (UMaterialInstanceDynamic* Look = BodyPartMeshes[Part]->CreateDynamicMaterialInstance(Slot)) EngineLooks.Add(Look);
        UMaterialInstanceDynamic* RadiatorLook = PanelMeshes.IsValidIndex(6) ? PanelMeshes[6]->CreateDynamicMaterialInstance(0) : nullptr;
        Leaks->Initialize(Damage, Engine, LeakDrops, EngineLooks, RadiatorLook);
        for (int32 Part = 0; Part < BodyPartMeshes.Num(); ++Part)
            if (FCString::Strcmp(BodyPartMounts[Part].Name, TEXT("Engine")) == 0) EngineBodyMesh = BodyPartMeshes[Part];
        if (EngineBodyMesh) EngineBodyRest = EngineBodyMesh->GetRelativeTransform();
        CrankPulleyRest = CrankPulleyMesh->GetRelativeTransform();
        BeltRest = BeltMesh->GetRelativeTransform();
        PulleyRests.Reset();
        for (const UStaticMeshComponent* Pulley : PulleyMeshes) PulleyRests.Add(Pulley->GetRelativeTransform());
        BeltLook = BeltMesh->CreateDynamicMaterialInstance(0);
        // Order matters: the chassis, then each panel at its index + 1, then the fixed body parts.
        TArray<UStaticMeshComponent*> Deformable = { ChassisMesh };
        TArray<Rally::Crush::EKind> Kinds = { Rally::Crush::EKind::Shell };
        for (int32 Panel = 0; Panel < PanelMeshes.Num(); ++Panel)
        {
            Deformable.Add(PanelMeshes[Panel]);
            Kinds.Add(Rally::Crush::KindOfPanel(Panel));
        }
        for (int32 Part = 0; Part < BodyPartMeshes.Num(); ++Part)
        {
            Deformable.Add(BodyPartMeshes[Part]);
            Kinds.Add(BodyPartMounts[Part].Kind);
        }
        BodyDeform->Initialize(Deformable, FTransform::Identity, Kinds);
    }
    // Running gear in URallyPanelComponent's slot order.
    TArray<TObjectPtr<UStaticMeshComponent>> GearMeshes;
    static const TCHAR* const GearNames[] = { TEXT("Strut_FL"), TEXT("Strut_FR"), TEXT("Strut_RL"), TEXT("Strut_RR"),
        TEXT("LowerArm_FL"), TEXT("LowerArm_FR"), TEXT("TrailingArm_RL"), TEXT("TrailingArm_RR"),
        TEXT("Brake_FL"), TEXT("Brake_FR"), TEXT("Brake_RL"), TEXT("Brake_RR"),
        TEXT("TieRod_FL"), TEXT("TieRod_FR"), TEXT("Driveshaft_FL"), TEXT("Driveshaft_FR") };
    for (const TCHAR* Name : GearNames)
    {
        for (int32 Part = 0; Part < SuspensionMeshes.Num(); ++Part)
        {
            if (FCString::Strcmp(SuspensionMounts[Part].Name, Name) == 0) GearMeshes.Add(SuspensionMeshes[Part]);
        }
    }
    Panels->Initialize(PanelMeshes, WheelMeshes, GearMeshes, ExhaustMesh, ModuleMeshes, BodyDeform);

    // Each running-gear mesh gets its own copy of each slot's material (M_Susp), bent along its length (pivot at
    // the fixed end, out to the moving one): it kinks down (and an arm across the car back a little), which is
    // what shows from beside the car with the wheel off. SuspensionLooks keeps the first slot's.
    SuspensionLooks.Reset();
    for (int32 Part = 0; Part < FMath::Min(SuspensionMeshes.Num(), static_cast<int32>(UE_ARRAY_COUNT(SuspensionMounts))); ++Part)
    {
        UStaticMeshComponent* Mesh = SuspensionMeshes[Part];
        UMaterialInstanceDynamic* First = nullptr;
        const FVector Length = SuspensionMounts[Part].Moving - SuspensionMounts[Part].Pivot;
        for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
        {
            UMaterialInstanceDynamic* Look = Mesh->CreateDynamicMaterialInstance(Slot);
            if (!Look) continue;
            Look->SetVectorParameterValue(TEXT("BendAxis"), FLinearColor(Length.GetSafeNormal()));
            Look->SetScalarParameterValue(TEXT("BendLength"), static_cast<float>(FMath::Max(Length.Size(), 1.0)));
            Look->SetVectorParameterValue(TEXT("BendDir"), FMath::Abs(Length.Y) > FMath::Abs(Length.X)
                ? FLinearColor(FVector(-0.45, 0.0, -0.9).GetSafeNormal()) : FLinearColor(0.0f, 0.0f, -1.0f));
            if (!First) First = Look;
        }
        // The bend reaches past the mesh's own bounds.
        if (First) Mesh->SetBoundsScale(1.5f);
        SuspensionLooks.Add(First);
    }
    SuspensionHangCm.Init(0.0f, SuspensionLooks.Num());
    UpdatePartLooks();
}

void ARallyCarPawn::UpdatePartLooks()
{
    const auto Health = [this](ERallyPart Part) {
        FRallyPartCondition Condition;
        Damage->GetPartCondition(Part, Condition);
        return Condition.Health;
    };
    for (int32 Part = 0; Part < FMath::Min(SuspensionLooks.Num(), SuspensionHangCm.Num()); ++Part)
    {
        const FSuspensionMount& Mount = SuspensionMounts[Part];
        const FString Name = Mount.Name;
        float Bend = 0.0f;
        float Hang = 0.0f;
        // Each corner's arm part (the front lower arm, the rear trailing arm) bends 3-9 cm as it is bent and
        // 10 cm snapped, and then drops at its outer end; a snapped driveshaft hangs.
        if (Name.StartsWith(TEXT("LowerArm_F")) || Name.StartsWith(TEXT("TrailingArm_")))
        {
            const float H = Health(SuspensionParts[Mount.Corner]);
            Bend = H >= 1.0f ? 0.0f : H <= 0.0f ? 10.0f : 3.0f + 6.0f * (1.0f - H);
            Hang = H <= 0.0f ? 8.0f : 0.0f;
        }
        else if (Name.StartsWith(TEXT("Driveshaft_F")))
        {
            Hang = Health(DriveshaftParts[Mount.Corner]) <= 0.0f ? 12.0f : 0.0f;
        }
        // Every slot's material on the mesh takes the look.
        const auto Looks = [this, Part](TFunctionRef<void(UMaterialInstanceDynamic*)> Apply) {
            UStaticMeshComponent* Mesh = SuspensionMeshes.IsValidIndex(Part) ? SuspensionMeshes[Part].Get() : nullptr;
            for (int32 Slot = 0; Mesh && Slot < Mesh->GetNumMaterials(); ++Slot)
                if (UMaterialInstanceDynamic* Look = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot))) Apply(Look);
        };
        if (Name.StartsWith(TEXT("Strut_")))
        {
            // A leaking damper wets itself with oil and road dirt: darker and browner as it wears.
            const float Worn = 0.85f * (1.0f - Health(StrutParts[Mount.Corner]));
            Looks([Worn](UMaterialInstanceDynamic* Look) {
                Look->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor::White, FLinearColor(0.16f, 0.1f, 0.07f), Worn)); });
        }
        else if (Name.StartsWith(TEXT("Disc_")) || Name.StartsWith(TEXT("Brake_")))
        {
            // Scored and burnt as it is damaged: from bare steel towards dark brown.
            const float Worn = 0.85f * (1.0f - Health(BrakeParts[Mount.Corner]));
            Looks([Worn](UMaterialInstanceDynamic* Look) {
                Look->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor::White, FLinearColor(0.3f, 0.17f, 0.1f), Worn)); });
        }
        Looks([Bend](UMaterialInstanceDynamic* Look) { Look->SetScalarParameterValue(TEXT("Bend"), Bend); });
        SuspensionHangCm[Part] = Hang;
    }
}

bool ARallyCarPawn::CopyPanelMesh(int32 Panel, UE::Geometry::FDynamicMesh3& OutMesh, TArray<UMaterialInterface*>& OutMaterials) const
{
    return PanelMeshes.IsValidIndex(Panel) && BodyDeform
        ? BodyDeform->CopyPartMesh(PanelMeshes[Panel], OutMesh, OutMaterials)
        : false;
}

FVector ARallyCarPawn::JackPoint(int32 Corner)
{
    // The sill just behind a front wheel or ahead of a rear one.
    return FVector(Corner < 2 ? 86.0 : -90.0, Corner % 2 == 0 ? -72.0 : 72.0, 22.0);
}

void ARallyCarPawn::SetJackCorner(int32 Corner)
{
    {
        FScopeLock Lock(&PhysicsLock);
        PhysicsJackCorner = Corner;
        PhysicsJackTarget = -1.0;
    }
    Body->WakeAllRigidBodies();
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
    PhysicsBaseIdleRpm = EngineSpec.IdleRpm;
    // A worn engine drags; whether it runs, its power and the battery are applied each step.
    if (Engine)
    {
        EngineSpec.FrictionTorque *= Engine->GetFrictionScale();
        bPhysicsEngineRunning = Engine->IsRunning();
        PhysicsTorqueScale = Engine->GetPowerScale();
        PhysicsBatteryHealth = Engine->GetBatteryHealth();
    }
    GearboxSpec = GearboxSetup.ToSpec();
    // A damaged gearbox loses gears from the top, and at 0 all of them.
    if (Damage)
    {
        FRallyPartCondition Gearbox;
        Damage->GetPartCondition(ERallyPart::Gearbox, Gearbox);
        PhysicsGearboxHealth = Gearbox.Health;
        GearboxSpec.LostGears = 0;
        for (int32 Gear = -1; Gear <= static_cast<int32>(GearboxSpec.Forward.size()); ++Gear)
        {
            if (Gear != 0 && !Rally::Underside::HasGear(Gear, Gearbox.Health))
                GearboxSpec.LostGears |= 1u << (Gear < 0 ? 0 : Gear);
        }
    }
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
    if (bPhysicsSimulates)
    {
        SimulatePhysicsStep(*Handle, PhysicsInput, DeltaTime);
    }
}

void ARallyCarPawn::SimulatePhysicsStep(Chaos::FRigidBodyHandle_Internal& Handle, const FRallyCarInput& Input, double Dt)
{
    UWorld* World = GetWorld();
    const int32 NumWheels = PhysicsWheels.Num();
    if (!World || NumWheels == 0 || Wheels.Num() != NumWheels || Dt <= 0.0)
    {
        return;
    }
    // Taking over from replayed motion starts the wheels afresh; their dampers start from the measured
    // compression rather than from zero, or the first step would kick the body up.
    const bool bFreshWheels = bResetWheelState;
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
        const double CompressionSpeed = bFreshWheels ? 0.0 : (Compression - State.Compression) / Dt;
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
            Contact.Surface.Friction = Material->Friction * PhysicsWetGrip;
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
        State.SteerDeg = (Setup.bSteered ? Input.Steer * PhysicsMaxSteer : 0.0) + ToeYawDeg(Setup);
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

    // A damaged gearbox jumps out of gear under load now and then; the automatic box puts it back.
    if (!Shift.IsShifting() && Shift.EngagedGear() != 0 && Input.Throttle > 0.3
        && CutOutRandom.FRand() < Rally::Underside::PopOutRate(PhysicsGearboxHealth) * Dt)
    {
        Shift.PendingGear = Shift.Gear;
        Shift.ShiftTimer = Rally::Underside::PopOutSeconds;
    }
    if (Input.bManual)
    {
        Rally::Drivetrain::TickShift(Shift, Dt);
        Rally::Drivetrain::UpdateManual(GearboxSpec, Shift, Input.Gear);
    }
    else
    {
        Rally::Drivetrain::UpdateAutomatic(GearboxSpec, Shift, DrivenGroundOmega, Input.Throttle, Input.bReverse, Dt);
    }

    // A damaged battery loses contact now and then and the engine cuts out for a moment.
    if (CutOutLeft > 0.0)
    {
        CutOutLeft -= Dt;
    }
    else if (bPhysicsEngineRunning && CutOutRandom.FRand() < Rally::Engine::CutOutRate(PhysicsBatteryHealth) * Dt)
    {
        CutOutLeft = Rally::Engine::CutOutSeconds * CutOutRandom.FRandRange(0.6f, 1.4f);
    }
    // Switched on, the starter turns it over for a moment (slower on a weak battery) before it fires.
    if (bPhysicsEngineRunning && !bPhysicsWasRunning)
    {
        CrankLeft = 0.7;
    }
    bPhysicsWasRunning = bPhysicsEngineRunning;
    if (CrankLeft > 0.0)
    {
        CrankLeft -= Dt;
        const double CrankOmega = (180.0 + 100.0 * FMath::Clamp(PhysicsBatteryHealth, 0.0, 1.0)) / Rally::Drivetrain::RadPerSecToRpm;
        EngineOmega += (CrankOmega - EngineOmega) * FMath::Min(Dt / 0.1, 1.0);
    }
    EngineSpec.bRunning = bPhysicsEngineRunning && CutOutLeft <= 0.0 && CrankLeft <= 0.0;
    // Misfiring coils take their share of the torque; each dead cylinder drops the idle.
    EngineSpec.TorqueScale = PhysicsTorqueScale * (1.0 - PhysicsMisfireShare);
    EngineSpec.IdleRpm = PhysicsBaseIdleRpm - PhysicsIdleDrop;
    double DriveThrottle = 0.0;

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
            // ABS releases the pedal brake each substep the wheel is past peak slip, either way of travel;
            // the handbrake is unassisted.
            const double Travel = Contact.SpeedLong < 0.0 ? -1.0 : 1.0;
            const bool bLocking = bPhysicsABS && Contact.bHit && State.SlipRatio * Travel < -1.2 * Contact.Tyre.PeakSlipRatio;
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
            BrakeTorque[I] += (Contact.RollingResistance + Setup.GroundDrag) * State.Load * Radius;
            State.SlipAngleDeg = FMath::RadiansToDegrees(FMath::Atan(TanSlipAngle));
        }

        if (Driven.Num() == 2)
        {
            FWheelState& Left = Wheels[Driven[0]];
            FWheelState& Right = Wheels[Driven[1]];
            // A broken driveshaft leaves its side of the open differential turning a free stub: the drive
            // all goes there, and that wheel rolls on its own.
            const bool bLeftBroken = PhysicsWheels[Driven[0]].bDriveshaftBroken;
            const bool bRightBroken = PhysicsWheels[Driven[1]].bDriveshaftBroken;
            double& LeftOmega = bLeftBroken ? Left.StubOmega : Left.Omega;
            double& RightOmega = bRightBroken ? Right.StubOmega : Right.Omega;
            Rally::Drivetrain::FAxleStep Axle;
            Axle.Throttle = Input.Throttle;
            if (bPhysicsTractionControl)
            {
                Axle.Throttle *= Rally::Drivetrain::TractionControlFactor(FMath::Max(Left.SlipRatio, Right.SlipRatio), Contacts[Driven[0]].Tyre.PeakSlipRatio);
            }
            DriveThrottle = Axle.Throttle;
            Axle.Gear = Shift.EngagedGear();
            Axle.ClutchEngagement = Rally::Drivetrain::PedalEngagement(
                Rally::Drivetrain::ClutchEngagement(EngineSpec, GearboxSpec, Shift, 0.5 * (LeftOmega + RightOmega), EngineOmega), Input.Clutch);
            Axle.GroundTorqueLeft = bLeftBroken ? 0.0 : GroundTorque[Driven[0]];
            Axle.GroundTorqueRight = bRightBroken ? 0.0 : GroundTorque[Driven[1]];
            Axle.BrakeLeft = bLeftBroken ? 0.0 : BrakeTorque[Driven[0]];
            Axle.BrakeRight = bRightBroken ? 0.0 : BrakeTorque[Driven[1]];
            Axle.WheelInertia = 0.5 * (PhysicsWheels[Driven[0]].Inertia + PhysicsWheels[Driven[1]].Inertia);
            Axle.Dt = SubDt;
            Rally::Drivetrain::StepDrivenAxle(EngineSpec, GearboxSpec, Axle, EngineOmega, LeftOmega, RightOmega);
        }
        for (int32 I = 0; I < NumWheels; ++I)
        {
            if (!Driven.Contains(I) || PhysicsWheels[I].bDriveshaftBroken)
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
    // A jack under one corner winds the sill there up to its full height and holds it, its foot gripping
    // the ground so the car cannot roll off it.
    if (PhysicsJackCorner >= 0 && PhysicsJackCorner < 4)
    {
        const FVector Lifted = Position + Rotation.RotateVector(AssemblyOffset + JackPoint(PhysicsJackCorner));
        FHitResult Ground;
        if (World->LineTraceSingleByChannel(Ground, Lifted + Up * 30.0, Lifted - Up * 150.0, ECC_Visibility, Params))
        {
            const double Height = FVector::DotProduct(Lifted - Ground.ImpactPoint, Up) * 0.01;
            if (PhysicsJackTarget < 0.0) PhysicsJackTarget = Height;
            PhysicsJackTarget = FMath::Min(PhysicsJackTarget + JackWindSpeed * Dt, JackLiftCm * 0.01);
            const FVector PointVelocity = (LinearVelocity + FVector::CrossProduct(AngularVelocity, Lifted - Com)) * 0.01;
            const double Lift = FMath::Max(JackStiffness * (PhysicsJackTarget - Height) - JackDamping * FVector::DotProduct(PointVelocity, Up), 0.0);
            AddForceAt(Up * (Lift * 100.0), Lifted);
            const FVector Slide = PointVelocity - Up * FVector::DotProduct(PointVelocity, Up);
            AddForceAt(-Slide * (JackGrip * 100.0), Lifted);
        }
    }
    const FVector Velocity = LinearVelocity * 0.01;
    AddForceAt(-Velocity * Velocity.Size() * (0.5 * AirDensity * PhysicsDragArea * 100.0), Com);
    // A hanging exhaust drags its silencer along the ground.
    if (bPhysicsExhaustDragging && Velocity.SizeSquared() > 0.01)
    {
        const FVector Ground = Velocity - Up * FVector::DotProduct(Velocity, Up);
        AddForceAt(-Ground.GetSafeNormal() * (Rally::Underside::ExhaustDragN * FMath::Min(Ground.Size() / 3.0, 1.0) * 100.0),
            Position + Rotation.RotateVector(AssemblyOffset + ExhaustPoint(2)));
    }
    Handle.AddForce(TotalForce);
    Handle.AddTorque(TotalTorque);

    Visual.Wheels = Wheels;
    Visual.SteerDeg = Input.Steer * PhysicsMaxSteer;
    Visual.Gear = Shift.IsShifting() ? 0 : Shift.Gear;
    Visual.EngineRpm = EngineOmega * Rally::Drivetrain::RadPerSecToRpm;
    Visual.EnginePowerKw = EngineSpec.bRunning && Visual.EngineRpm < EngineSpec.LimiterRpm
        ? DriveThrottle * Rally::Drivetrain::FullThrottleTorque(EngineSpec, Visual.EngineRpm) * EngineSpec.TorqueScale * EngineOmega * 0.001 : 0.0;
    Visual.EngineLoad = EngineSpec.bRunning ? DriveThrottle : 0.0;
    Visual.bCutOut = bPhysicsEngineRunning && CutOutLeft > 0.0;
}

void ARallyCarPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateBootLoad();
    if (Panels) Panels->UpdateBootGhosts();
    // The driver's body shows on the car's cameras too (hurt, drunk, cold), on the driver's own machine.
    if (IsLocallyControlled())
        if (const ARallyCharacter* Driver = GetDriverCharacter())
        {
            Driver->ApplyBodyView(CockpitCamera->PostProcessSettings);
            Driver->ApplyBodyView(Camera->PostProcessSettings);
        }
    if (const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld())) PhysicsWetGrip = Rally::Weather::GripScale(Clock->GetRain());
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("RallyPanelTest")))
    {
        bHandbrake = true;
        if (HasAuthority()) TickPanelTest(DeltaSeconds);
    }
#endif

    // The scripted drive belongs to whichever process controls this car, host or client. On a client
    // the controller arrives after BeginPlay, so decide on the first tick that has one.
    if (!bAutoDriveDecided && GetController())
    {
        bAutoDriveDecided = true;
        bAutoDrive = IsLocallyControlled() && FParse::Param(FCommandLine::Get(), TEXT("RallyAutoDrive"))
            && !FParse::Param(FCommandLine::Get(), TEXT("RallyPanelTest"));
        if (bAutoDrive)
        {
            // Scripted drives start the engine like a player: the key.
            if (!Engine->IsRunning()) ServerToggleIgnition();
            AutoDriveStart = GetActorLocation();
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_AUTODRIVE start=%s role=%s"), *AutoDriveStart.ToCompactString(),
                HasAuthority() ? TEXT("host") : TEXT("client"));
        }
    }
    if (bAutoDrive)
    {
        if (bKnockTest)
        {
            TickKnockTest(DeltaSeconds);
        }
        else if (bPartsTest)
        {
            TickPartsTest(DeltaSeconds);
        }
        else if (bUndersideTest)
        {
            TickUndersideTest(DeltaSeconds);
        }
        else if (bLeakTest)
        {
            TickLeakTest(DeltaSeconds);
        }
        else if (bStageDrive)
        {
            TickStageDrive(DeltaSeconds);
        }
        else if (bCrashTest)
        {
            TickCrashTest(DeltaSeconds);
        }
        else
        {
            TickAutoDrive(DeltaSeconds);
        }
    }
    if (PendingImpact.bActive && GetWorld()->GetTimeSeconds() - PendingImpact.StartTime > 0.1)
    {
        FlushPendingImpact();
    }
    if (PendingUnderside.bActive && GetWorld()->GetTimeSeconds() - PendingUnderside.StartTime > 0.1)
    {
        FlushPendingUnderside();
    }
    if (bNetLog)
    {
        TickNetLog(DeltaSeconds);
    }
#if !UE_BUILD_SHIPPING
    if (IsLocallyControlled() && FParse::Param(FCommandLine::Get(), TEXT("RallyKeyTest"))) TickKeyTest(DeltaSeconds);
#endif

    UpdateSimulationRole();
    if (bSimulatesHere)
    {
        FRallyCarInput Input;
        const bool bNobodyDriving = !IsLocallyControlled() && !bAutoDrive;
        if (!bNobodyDriving)
        {
            Input = BuildDriverInput(DeltaSeconds);
        }
        else
        {
            // Nobody in the driver's seat: out of gear with the handbrake on, so an engine left running idles
            // where it stands; a hand on the throttle in the bay revs it.
            Input.bHandbrake = true;
            Input.bManual = true;
            Input.Gear = 0;
        }
        FRallyPartCondition Exhaust;
        Damage->GetPartCondition(ERallyPart::Exhaust, Exhaust);
        const bool bDragging = Rally::Underside::ExhaustHanging(Exhaust.Health) && !Panels->IsRemoved(URallyPanelComponent::ExhaustSlot);
        FScopeLock Lock(&PhysicsLock);
        if (bNobodyDriving && GetWorld()->GetTimeSeconds() < HandRevUntil) Input.Throttle = static_cast<float>(Rally::Engine::HandThrottle(Visual.EngineRpm));
        PhysicsInput = Input;
        bPhysicsExhaustDragging = bDragging;
    }
    else
    {
        TickObserved(DeltaSeconds);
    }
    if (bSimulatesHere)
    {
        SendState(DeltaSeconds);
    }

    FVisualState Snapshot;
    {
        FScopeLock Lock(&PhysicsLock);
        Snapshot = Visual;
    }
    EngineRpmShown = Snapshot.EngineRpm;
    UpdateFans(DeltaSeconds);
    UpdateWheelVisuals(DeltaSeconds);
    UpdateLook(DeltaSeconds);
    UpdateEngineMotion(DeltaSeconds, Snapshot.EngineRpm, Snapshot.EnginePowerKw);
    UpdateDoors(DeltaSeconds);
    Engine->TickEngine(DeltaSeconds, Snapshot.EnginePowerKw, Snapshot.EngineRpm, GetController() != nullptr, Snapshot.EngineLoad);
    // The coils that misfire now and then, at the load asked of the engine.
    CoilMisfireShare = Engine->IsRunning() ? Rally::Engine::MisfireShare(Engine->GetModules(), Snapshot.EngineLoad) : 0.0;
    {
        FScopeLock Lock(&PhysicsLock);
        PhysicsMisfireShare = Engine->IsRunning() ? Rally::Engine::IntermittentPowerLoss(Engine->GetModules(), Snapshot.EngineLoad) : 0.0;
    }
    Leaks->TickLeaks(DeltaSeconds);
    UpdateSounds(DeltaSeconds, Snapshot);

    if (IsLocallyControlled())
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
        const FString ExitText = FMath::Abs(GetForwardSpeedKmh()) <= MaxExitSpeedKmh ? TEXT("   E: get out") : TEXT("");
        const FString BiasText = BiasSteps != 0 ? FString::Printf(TEXT("   brake bias %.0f%% front"), Rally::Grade::BiasPct(BiasSteps)) : FString();
        // Throttle in neutral on the manual box does nothing; say how to get into gear.
        const FString NeutralText = bManualGearbox && SelectedGear == 0 && ThrottleInput > 0.5f && FMath::Abs(GetForwardSpeedKmh()) < 5.0f
            ? TEXT("   in neutral: Left Shift for first") : TEXT("");
        ReadoutText = FString::Printf(TEXT("%.0f km/h   gear %s   %.0f rpm   %s%s%s%s%s"), GetForwardSpeedKmh(), *Gear, Snapshot.EngineRpm, *Engine->Describe(),
            *SpinText, *BiasText, *NeutralText, *ExitText);
        DamageText = DescribeDamage();
    }
    else
    {
        ReadoutText.Reset();
        DamageText.Reset();
    }
}

void ARallyCarPawn::TickNetLog(float DeltaSeconds)
{
    // -RallyNetLog: every process logs every car against the shared server clock, so host and client
    // views of the same car (matched by player id) can be compared offline (scripts/net_compare.py).
    NetLogTimer += DeltaSeconds;
    const AGameStateBase* GameState = GetWorld()->GetGameState();
    if (NetLogTimer < 0.1f || !GameState)
    {
        return;
    }
    NetLogTimer = 0.0f;
    const TCHAR* NetRole = GetLocalRole() == ROLE_Authority ? TEXT("authority") : GetLocalRole() == ROLE_AutonomousProxy ? TEXT("autonomous") : TEXT("simulated");
    const FVector Location = GetActorLocation();
    const FVector Velocity = GetVelocity();
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_NET st=%.3f pt=%.3f player=%d role=%s local=%d sim=%d pos=%.1f,%.1f,%.1f vel=%.1f,%.1f yaw=%.2f"),
        GameState->GetServerWorldTimeSeconds(), PhysicsTime(), GetPlayerState() ? GetPlayerState()->GetPlayerId() : -1, NetRole,
        IsLocallyControlled() ? 1 : 0, bSimulatesHere ? 1 : 0, Location.X, Location.Y, Location.Z, Velocity.X, Velocity.Y, GetActorRotation().Yaw);
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

    // Suspension follows its wheel: the hub moves with travel and steering about the wheel centre.
    for (int32 Part = 0; Part < FMath::Min(SuspensionMeshes.Num(), static_cast<int32>(UE_ARRAY_COUNT(SuspensionMounts))); ++Part)
    {
        const FSuspensionMount& Mount = SuspensionMounts[Part];
        if (Mount.Corner >= Count)
        {
            continue;
        }
        const FVector Centre = AssemblyOffset + WheelSetups[Mount.Corner].Pivot;
        const FTransform Hub = FTransform(-Centre) * FTransform(FRotator(0.0, States[Mount.Corner].SteerDeg, 0.0))
            * FTransform(Centre + FVector(0.0, 0.0, VisualOffsetCm[Mount.Corner]));
        const FVector Fixed = AssemblyOffset + Mount.Pivot;
        FTransform Place(Fixed);
        if (Mount.Role == ESuspensionRole::Hub)
        {
            Place = Place * Hub;
        }
        else if (Mount.Role == ESuspensionRole::Disc)
        {
            const FTransform Spin = FTransform(-Centre) * FTransform(FRotator(-FMath::RadiansToDegrees(VisualSpin[Mount.Corner]), 0.0, 0.0))
                * FTransform(Centre);
            Place = Place * Spin * Hub;
        }
        else
        {
            // Turn the part so its moving end meets where the hub has taken that point; a strut or
            // driveshaft also stretches or shortens along its length.
            const FVector Rest = AssemblyOffset + Mount.Moving - Fixed;
            // A snapped arm or driveshaft no longer meets the hub: its outer end hangs.
            const float Hang = SuspensionHangCm.IsValidIndex(Part) ? SuspensionHangCm[Part] : 0.0f;
            const FVector Now = Hub.TransformPosition(AssemblyOffset + Mount.Moving) - Fixed - FVector(0.0, 0.0, Hang);
            FVector Scale = FVector::OneVector;
            if (Mount.Role == ESuspensionRole::Stretch)
            {
                const FVector Abs = Rest.GetAbs();
                const int32 Along = Abs.X >= Abs.Y && Abs.X >= Abs.Z ? 0 : (Abs.Y >= Abs.Z ? 1 : 2);
                Scale[Along] = Now.Size() / FMath::Max(Rest.Size(), 1.0);
            }
            Place = FTransform(FQuat::FindBetweenVectors(Rest, Now), Fixed, Scale);
        }
        SuspensionMeshes[Part]->SetRelativeTransform(Place);
        // The disc is the brake part's too: off with it.
        if (Mount.Role == ESuspensionRole::Disc)
            SuspensionMeshes[Part]->SetVisibility(!Panels->IsRemoved(URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Disc, Mount.Corner)));
    }

    // Turning right spins the wheel clockwise as the driver sees it.
    const float RoadWheelDeg = States.Num() > 0 ? static_cast<float>(States[0].SteerDeg) : 0.0f;
    // Positive rotation about the driver-facing axis is clockwise from the seat in Unreal's left-handed frame.
    SteeringWheelMesh->SetRelativeRotation(FQuat(SteeringColumnAxis.GetSafeNormal(), FMath::DegreesToRadians(RoadWheelDeg * SteeringRatio)));
}

FRallyCarInput ARallyCarPawn::BuildDriverInput(float DeltaSeconds)
{
    const float Speed = GetForwardSpeedKmh();
    // The manual box has its own reverse gear; the brake only brakes.
    const bool bManual = bAutoDrive ? bAutoDriveManual : bManualGearbox;
    if (bManual)
    {
        bReversing = false;
    }
    // Only from (nearly) standing: braking while rolling backwards, after a bounce off a wall or on a
    // slope, must stop the car rather than drive it backwards.
    else if (!bReversing && BrakeInput > 0.1f && ThrottleInput < 0.1f && FMath::Abs(Speed) < ReverseEngageKmh)
    {
        bReversing = true;
    }
    else if (bReversing && ThrottleInput > 0.1f && Speed > -ReverseEngageKmh)
    {
        bReversing = false;
    }
    ClutchPedal = static_cast<float>(Rally::Drivetrain::MovePedal(ClutchPedal, bClutchHeld, DeltaSeconds));

    // A drunk driver's hands wander (docs/BODY.md).
    const ARallyCharacter* Driver = GetDriverCharacter();
    const float Wander = Driver ? static_cast<float>(Rally::Body::SteerWander(Driver->GetDrunk()) * FMath::Sin(GetWorld()->GetTimeSeconds() * 0.7)) : 0.0f;
    const float Wanted = FMath::Clamp(SteerTarget + Wander, -1.0f, 1.0f);
    const float Rate = FMath::Abs(Wanted) > FMath::Abs(SteerCurrent) ? SteerRate : SteerReturnRate;
    SteerCurrent = FMath::FInterpConstantTo(SteerCurrent, Wanted, DeltaSeconds, Rate);

    // In reverse the brake pedal drives and the throttle pedal brakes.
    FRallyCarInput Input;
    Input.Throttle = bReversing ? (Speed > -MaxReverseKmh ? BrakeInput : 0.0f) : ThrottleInput;
    Input.Brake = bReversing ? ThrottleInput : BrakeInput;
    // Speed-sensitive lock: full lock at walking pace, about 45% at 100 km/h, so digital steering
    // does not throw the car sideways at speed.
    Input.Steer = SteerSpeedFalloffKmh > 0.0f ? SteerCurrent / (1.0f + FMath::Abs(Speed) / SteerSpeedFalloffKmh) : SteerCurrent;
    Input.bHandbrake = bHandbrake;
    Input.bReverse = bReversing;
    Input.bManual = bManual;
    Input.Gear = SelectedGear;
    Input.Clutch = bManual ? ClutchPedal : 0.0f;
    return Input;
}

void ARallyCarPawn::OnClutch(const FInputActionValue& Value)
{
    bClutchHeld = Value.Get<bool>();
}

void ARallyCarPawn::ShiftGear(int32 Step)
{
    if (!bManualGearbox) return;
    const int32 Next = Rally::Drivetrain::SelectGear(SelectedGear, Step, GearboxSetup.ForwardRatios.Num(), GetForwardSpeedKmh());
    // A gear the road would over-rev the engine in does not go in: the box crunches.
    const double WheelOmega = GetForwardSpeedKmh() / 3.6 / FMath::Max(WheelSetups.IsValidIndex(0) ? WheelSetups[0].Radius * 0.01 : 0.31, 0.1);
    if (Rally::Drivetrain::WouldOverRev(EngineSetup.ToSpec(), GearboxSetup.ToSpec(), Next, WheelOmega))
    {
        EventSound->Trigger({ Rally::Audio::EEvent::GearGrind, 1.0f });
        return;
    }
    SelectedGear = static_cast<int8>(Next);
}

double ARallyCarPawn::PhysicsTime() const
{
    // Time of the physics results the game thread currently shows.
    const FPhysScene* Scene = GetWorld()->GetPhysicsScene();
    const Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
    return Solver ? Solver->GetPhysicsResultsTime_External() : GetWorld()->GetTimeSeconds();
}

FVector ARallyCarPawn::GetVelocity() const
{
    return bSimulatesHere ? Super::GetVelocity() : ObservedVelocity;
}

void ARallyCarPawn::UpdateSimulationRole()
{
    // The driver's own machine simulates; with no driver the server does; everyone else observes.
    const AController* Driver = GetController();
    const bool bLocalDriver = Driver && Driver->IsLocalController();
    const bool bRemoteDriver = Driver && Driver->IsPlayerController() && !Driver->IsLocalController();
    const bool bShouldSimulate = bLocalDriver || (HasAuthority() && !bRemoteDriver);
    if (bShouldSimulate == bSimulatesHere)
    {
        return;
    }
    bSimulatesHere = bShouldSimulate;
    if (bShouldSimulate)
    {
        // Taking over (for example the server after the driver leaves): continue from the replayed motion.
        Body->SetSimulatePhysics(true);
        Body->SetPhysicsLinearVelocity(ObservedVelocity);
        Body->SetPhysicsAngularVelocityInRadians(ObservedAngularVelocity);
    }
    else
    {
        Body->SetSimulatePhysics(false);
        Snapshots.Reset();
        PlayoutTime = -1.0;
    }
    FScopeLock Lock(&PhysicsLock);
    bPhysicsSimulates = bShouldSimulate;
    bResetWheelState = true;
}

void ARallyCarPawn::SendState(float DeltaSeconds)
{
    StateSendTimer += DeltaSeconds;
    const AGameStateBase* GameState = GetWorld()->GetGameState();
    if (StateSendTimer < 1.0f / FMath::Max(StateSendRate, 1.0f) || !GameState || GetNetMode() == NM_Standalone)
    {
        return;
    }
    StateSendTimer = 0.0f;

    FRallyCarNetState State;
    // Sender's physics clock: observers only use differences between one sender's snapshots. Server time
    // is corrected in steps on clients, and game time outruns physics when a slow machine drops steps.
    State.Time = PhysicsTime();
    State.Location = GetActorLocation();
    State.Rotation = GetActorQuat();
    State.LinearVelocity = Body->GetPhysicsLinearVelocity();
    State.AngularVelocity = Body->GetPhysicsAngularVelocityInRadians();
    State.ResetCount = ResetCount;
    {
        FScopeLock Lock(&PhysicsLock);
        State.Gear = static_cast<int8>(Visual.Gear);
        State.EngineRpm = static_cast<uint16>(FMath::Clamp(Visual.EngineRpm, 0.0, 65000.0));
        State.EnginePowerKw = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Visual.EnginePowerKw), 0, 255));
        for (int32 I = 0; I < FMath::Min(Visual.Wheels.Num(), 4); ++I)
        {
            State.WheelOffsetCm[I] = static_cast<float>(Visual.Wheels[I].OffsetCm);
            State.WheelOmega[I] = static_cast<float>(Visual.Wheels[I].Omega);
        }
        State.SteerDeg = static_cast<float>(Visual.SteerDeg);
    }
    if (HasAuthority())
    {
        NetState = State;
    }
    else
    {
        ServerSubmitState(State);
    }
}

void ARallyCarPawn::ServerSubmitState_Implementation(const FRallyCarNetState& State)
{
    // Co-op trust model: the driver is believed within plausibility limits (no NaN, speed, teleports).
    if (bSimulatesHere || State.Location.ContainsNaN() || State.LinearVelocity.ContainsNaN() || State.Rotation.ContainsNaN())
    {
        return;
    }
    const bool bReset = State.ResetCount != NetState.ResetCount;
    const double Gap = Snapshots.IsEmpty() ? 0.0 : FVector::Dist(State.Location, Snapshots.Last().Location);
    if (State.LinearVelocity.Size() > 8000.0 || (!bReset && Gap > 3000.0))
    {
        UE_LOG(LogRallyCar, Warning, TEXT("Rejected implausible car state from %s (speed %.0f cm/s, jump %.0f cm)."),
            *GetNameSafe(GetController()), State.LinearVelocity.Size(), Gap);
        return;
    }
    NetState = State;
    AddSnapshot(State);
}

void ARallyCarPawn::OnRep_NetState()
{
    AddSnapshot(NetState);
}

void ARallyCarPawn::AddSnapshot(const FRallyCarNetState& State)
{
    if (!Snapshots.IsEmpty() && State.ResetCount != Snapshots.Last().ResetCount)
    {
        Snapshots.Reset();
        PlayoutTime = -1.0;
    }
    if (!Snapshots.IsEmpty() && State.Time <= Snapshots.Last().Time)
    {
        return;
    }
    Snapshots.Add(State);
    if (Snapshots.Num() > 32)
    {
        Snapshots.RemoveAt(0, Snapshots.Num() - 32);
    }
}

void ARallyCarPawn::TickObserved(float DeltaSeconds)
{
    if (Snapshots.IsEmpty())
    {
        return;
    }
    // Playout clock runs InterpolationDelay behind the newest snapshot and slews gently toward that
    // target, so uneven packet arrival does not show up as uneven motion.
    const double Target = Snapshots.Last().Time - InterpolationDelay;
    if (PlayoutTime < 0.0 || FMath::Abs(Target - PlayoutTime) > 0.5)
    {
        PlayoutTime = Target;
    }
    else
    {
        PlayoutTime += DeltaSeconds + (Target - PlayoutTime) * FMath::Min(1.0, DeltaSeconds * 2.0);
    }

    int32 Next = Snapshots.IndexOfByPredicate([this](const FRallyCarNetState& S) { return S.Time > PlayoutTime; });
    FVector Location;
    FQuat Rotation;
    const FRallyCarNetState* A = nullptr;
    const FRallyCarNetState* B = nullptr;
    float Alpha = 0.0f;
    if (Next == INDEX_NONE)
    {
        // Starved: extrapolate briefly from the newest snapshot.
        A = B = &Snapshots.Last();
        const double Ahead = FMath::Min(PlayoutTime - A->Time, 0.25);
        Location = A->Location + A->LinearVelocity * Ahead;
        Rotation = FQuat(A->AngularVelocity.GetSafeNormal(), A->AngularVelocity.Size() * Ahead) * A->Rotation;
        ObservedVelocity = A->LinearVelocity;
    }
    else if (Next == 0)
    {
        A = B = &Snapshots[0];
        Location = A->Location;
        Rotation = A->Rotation;
        ObservedVelocity = A->LinearVelocity;
    }
    else
    {
        // Cubic Hermite between the bracketing snapshots, using their velocities as tangents.
        A = &Snapshots[Next - 1];
        B = &Snapshots[Next];
        const double Span = FMath::Max(B->Time - A->Time, 1e-4);
        const double S = FMath::Clamp((PlayoutTime - A->Time) / Span, 0.0, 1.0);
        const double S2 = S * S, S3 = S2 * S;
        Location = FVector(A->Location) * (2 * S3 - 3 * S2 + 1) + A->LinearVelocity * ((S3 - 2 * S2 + S) * Span)
            + FVector(B->Location) * (-2 * S3 + 3 * S2) + B->LinearVelocity * ((S3 - S2) * Span);
        Rotation = FQuat::Slerp(A->Rotation, B->Rotation, S);
        ObservedVelocity = FMath::Lerp(FVector(A->LinearVelocity), FVector(B->LinearVelocity), S);
        Alpha = static_cast<float>(S);
    }
    ObservedAngularVelocity = B->AngularVelocity;
    // Kinematic move: the body pushes what it hits but is not pushed back.
    Body->SetWorldLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::None);

    FScopeLock Lock(&PhysicsLock);
    Visual.Wheels.SetNum(4);
    for (int32 I = 0; I < 4; ++I)
    {
        Visual.Wheels[I].OffsetCm = FMath::Lerp(A->WheelOffsetCm[I], B->WheelOffsetCm[I], Alpha);
        Visual.Wheels[I].Omega = FMath::Lerp(A->WheelOmega[I], B->WheelOmega[I], Alpha);
        const bool bSteered = WheelSetups.IsValidIndex(I) ? WheelSetups[I].bSteered : I < 2;
        Visual.Wheels[I].SteerDeg = (bSteered ? FMath::Lerp(A->SteerDeg, B->SteerDeg, Alpha) : 0.0f)
            + (WheelSetups.IsValidIndex(I) ? ToeYawDeg(WheelSetups[I]) : 0.0);
    }
    Visual.Gear = B->Gear;
    Visual.EngineRpm = B->EngineRpm;
    Visual.EnginePowerKw = B->EnginePowerKw;
    // Load for the sound, from the power made against what full throttle would make at these revs.
    {
        const double Omega = B->EngineRpm / Rally::Drivetrain::RadPerSecToRpm;
        const double Available = Rally::Drivetrain::FullThrottleTorque(EngineSpec, B->EngineRpm) * EngineSpec.TorqueScale * Omega * 0.001;
        Visual.EngineLoad = Available > 1.0 ? FMath::Clamp(B->EnginePowerKw / Available, 0.0, 1.0) : 0.0;
    }
    for (FWheelState& Wheel : Visual.Wheels)
    {
        Wheel.bContact = true;
    }
    while (Snapshots.Num() > 2 && Snapshots[1].Time < PlayoutTime - 1.0)
    {
        Snapshots.RemoveAt(0);
    }
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
    ExitAction = MakeAction(this, EInputActionValueType::Boolean);
    DoorAction = MakeAction(this, EInputActionValueType::Boolean);
    IgnitionAction = MakeAction(this, EInputActionValueType::Boolean);
    DebugDamageAction = MakeAction(this, EInputActionValueType::Boolean);
    DebugRepairAction = MakeAction(this, EInputActionValueType::Boolean);

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
    DrivingContext->MapKey(ExitAction, EKeys::E);
    DrivingContext->MapKey(ExitAction, EKeys::Gamepad_FaceButton_Left);
    DrivingContext->MapKey(DoorAction, EKeys::F);
    DrivingContext->MapKey(DoorAction, EKeys::Gamepad_FaceButton_Top);
    LightsAction = NewObject<UInputAction>(this);
    LightsAction->ValueType = EInputActionValueType::Boolean;
    SleepAction = NewObject<UInputAction>(this);
    SleepAction->ValueType = EInputActionValueType::Boolean;
    DrivingContext->MapKey(LightsAction, EKeys::L);
    DrivingContext->MapKey(SleepAction, EKeys::Z);
    HelpAction = NewObject<UInputAction>(this);
    HelpAction->ValueType = EInputActionValueType::Boolean;
    DrivingContext->MapKey(HelpAction, EKeys::F1);
    TowAction = NewObject<UInputAction>(this);
    TowAction->ValueType = EInputActionValueType::Boolean;
    DrivingContext->MapKey(TowAction, EKeys::H);
    RecceAction = NewObject<UInputAction>(this);
    RecceAction->ValueType = EInputActionValueType::Boolean;
    DrivingContext->MapKey(RecceAction, EKeys::N);
    CupAction = NewObject<UInputAction>(this);
    CupAction->ValueType = EInputActionValueType::Boolean;
    DrivingContext->MapKey(CupAction, EKeys::J);
    BiasForwardAction = NewObject<UInputAction>(this);
    BiasForwardAction->ValueType = EInputActionValueType::Boolean;
    BiasRearwardAction = NewObject<UInputAction>(this);
    BiasRearwardAction->ValueType = EInputActionValueType::Boolean;
    DrivingContext->MapKey(BiasForwardAction, EKeys::RightBracket);
    DrivingContext->MapKey(BiasRearwardAction, EKeys::LeftBracket);
    ShiftUpAction = MakeAction(this, EInputActionValueType::Boolean);
    ShiftDownAction = MakeAction(this, EInputActionValueType::Boolean);
    ClutchAction = MakeAction(this, EInputActionValueType::Boolean);
    DrivingContext->MapKey(ShiftUpAction, EKeys::LeftShift);
    DrivingContext->MapKey(ShiftUpAction, EKeys::Gamepad_RightShoulder);
    DrivingContext->MapKey(ShiftDownAction, EKeys::LeftControl);
    DrivingContext->MapKey(ShiftDownAction, EKeys::Gamepad_LeftShoulder);
    DrivingContext->MapKey(ClutchAction, EKeys::Q);
    DrivingContext->MapKey(ClutchAction, EKeys::Gamepad_FaceButton_Right);
    DrivingContext->MapKey(IgnitionAction, EKeys::I);
    DrivingContext->MapKey(IgnitionAction, EKeys::Gamepad_Special_Right);
    DrivingContext->MapKey(ToggleCameraAction, EKeys::C);
    DrivingContext->MapKey(ToggleCameraAction, EKeys::Gamepad_RightThumbstick);
#if !UE_BUILD_SHIPPING
    // Test keys until damage comes from the world and repairs from tools.
    DrivingContext->MapKey(DebugDamageAction, EKeys::F8);
    DrivingContext->MapKey(DebugRepairAction, EKeys::F9);
#endif
}

void ARallyCarPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("RallyPanelTest"))) return;
#endif
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
    Input->BindAction(ExitAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnExitPressed);
    Input->BindAction(DoorAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnDoorPressed);
    Input->BindAction(IgnitionAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnIgnitionPressed);
    Input->BindAction(LightsAction, ETriggerEvent::Started, this, &ARallyCarPawn::ServerToggleLights);
    Input->BindAction(SleepAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnSleep);
    Input->BindAction(HelpAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnHelp);
    Input->BindAction(TowAction, ETriggerEvent::Started, this, &ARallyCarPawn::ServerCallTow);
    Input->BindAction(RecceAction, ETriggerEvent::Started, this, &ARallyCarPawn::ServerRecce);
    Input->BindAction(CupAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnCup);
    Input->BindAction(BiasForwardAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnBiasForward);
    Input->BindAction(BiasRearwardAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnBiasRearward);
    Input->BindAction(ShiftUpAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnShiftUp);
    Input->BindAction(ShiftDownAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnShiftDown);
    Input->BindAction(ClutchAction, ETriggerEvent::Triggered, this, &ARallyCarPawn::OnClutch);
    Input->BindAction(ClutchAction, ETriggerEvent::Completed, this, &ARallyCarPawn::OnClutch);
    Input->BindAction(DebugDamageAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnDebugDamage);
    Input->BindAction(DebugRepairAction, ETriggerEvent::Started, this, &ARallyCarPawn::OnDebugRepair);
}

void ARallyCarPawn::NotifyControllerChanged()
{
    // Leaning on foot tilts the view; the car's does not.
    if (Controller)
    {
        FRotator View = Controller->GetControlRotation();
        View.Roll = 0.0f;
        Controller->SetControlRotation(View);
    }
    // The driver got out: drop the driving keys so they do not shadow the on-foot ones.
    if (const APlayerController* Previous = Cast<APlayerController>(PreviousController))
    {
        if (Previous->IsLocalController() && DrivingContext)
        {
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Previous->GetLocalPlayer()))
            {
                Subsystem->RemoveMappingContext(DrivingContext);
            }
        }
    }
    Super::NotifyControllerChanged();

    // Releasing control must not leave the last pedal or steering values applied. A driver taking the
    // seat finds it in first gear.
    ThrottleInput = BrakeInput = SteerTarget = 0.0f;
    bHandbrake = false;
    bClutchHeld = false;
    ClutchPedal = 0.0f;
    SelectedGear = 1;

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

bool ARallyCarPawn::IsSeatFree(int32 Seat) const
{
    return Occupants.IsValidIndex(Seat) && !IsValid(Occupants[Seat]);
}

FVector ARallyCarPawn::GetSeatEyeLocation(int32 Seat) const
{
    return AssemblyOffset + SeatMounts[FMath::Clamp(Seat, 0, SeatCount - 1)].Eye;
}

FVector ARallyCarPawn::GetSteeringGripLocation(bool bLeft) const
{
    // Rim points at nine and three o'clock, transformed with the actual rotating wheel mesh.
    return SteeringWheelMesh->GetComponentTransform().TransformPosition(FVector(0,bLeft?-16.f:16.f,0));
}

FVector ARallyCarPawn::GetSeatDoorLocation(int32 Seat) const
{
    return GetActorTransform().TransformPosition(AssemblyOffset + SeatMounts[FMath::Clamp(Seat, 0, SeatCount - 1)].Outside);
}

ARallyCarPawn* ARallyCarPawn::FindSeatInReach(const AActor* Character, int32& OutSeat)
{
    OutSeat = INDEX_NONE;
    if (!Character)
    {
        return nullptr;
    }
    ARallyCarPawn* Best = nullptr;
    float BestDistance = DoorReach;
    const FVector Where = Character->GetActorLocation();
    for (TActorIterator<ARallyCarPawn> It(Character->GetWorld()); It; ++It)
    {
        for (int32 Seat = 0; Seat < SeatCount; ++Seat)
        {
            const float Distance = FVector::Dist(Where, It->GetSeatDoorLocation(Seat));
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                Best = *It;
                OutSeat = Seat;
            }
        }
    }
    return Best;
}

bool ARallyCarPawn::ServerEnterSeat(ARallyCharacter* Character, int32 Seat, bool bSkipReachCheck)
{
    if (!HasAuthority() || !Character || Character->GetSeatedCar() || !IsSeatFree(Seat))
    {
        return false;
    }
    if (Character->GetCarriedPart() || Panels->IsDoorJammed(Seat)) return false;
    if (!bSkipReachCheck)
    {
        int32 ReachSeat = INDEX_NONE;
        if (FindSeatInReach(Character, ReachSeat) != this || ReachSeat != Seat || FMath::Abs(GetForwardSpeedKmh()) > MaxEnterSpeedKmh)
        {
            return false;
        }
    }
    APlayerController* PlayerController = Cast<APlayerController>(Character->GetController());
    if (Seat == 0 && !PlayerController)
    {
        return false;
    }
    Occupants[Seat] = Character;
    Character->SetSeat(this, Seat);
    OpenDoorBriefly(Seat);
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_SEAT enter %s seat=%d"), *GetNameSafe(Character), Seat);
    if (Seat == 0)
    {
        // The driver's machine now simulates the car (UpdateSimulationRole).
        PlayerController->Possess(this);
    }
    return true;
}

bool ARallyCarPawn::FindExitLocation(const ARallyCharacter* Character, int32 Seat, FVector& OutLocation) const
{
    // Own door first, then the other side, then on the roof.
    const FTransform Upright(FRotator(0.0, GetActorRotation().Yaw, 0.0), GetActorLocation());
    const FVector Candidates[] =
    {
        Upright.TransformPosition(AssemblyOffset + SeatMounts[Seat].Outside),
        Upright.TransformPosition(AssemblyOffset + SeatMounts[1 - Seat].Outside),
        GetActorLocation() + FVector(0.0, 0.0, 250.0),
    };
    FCollisionQueryParams Params(SCENE_QUERY_STAT(RallySeatExit), false, this);
    Params.AddIgnoredActor(Character);
    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(34.0f, 88.0f);
    for (int32 I = 0; I < UE_ARRAY_COUNT(Candidates); ++I)
    {
        if ((I == 0 && Panels->IsDoorJammed(Seat)) || (I == 1 && Panels->IsDoorJammed(1 - Seat))) continue;
        const FVector& Candidate = Candidates[I];
        if (!GetWorld()->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, ECC_Pawn, Capsule, Params))
        {
            OutLocation = Candidate;
            return true;
        }
    }
    return false;
}

bool ARallyCarPawn::ServerExitSeat(ARallyCharacter* Character, bool bForce)
{
    const int32 Seat = Occupants.Find(Character);
    if (!HasAuthority() || Seat == INDEX_NONE || (!bForce && FMath::Abs(GetForwardSpeedKmh()) > MaxExitSpeedKmh))
    {
        return false;
    }
    FVector ExitLocation;
    if (!FindExitLocation(Character, Seat, ExitLocation))
    {
        if (!bForce)
        {
            return false;
        }
        ExitLocation = GetActorLocation() + FVector(0.0, 0.0, 300.0);
    }
    Occupants[Seat] = nullptr;
    if (Seat == 0)
    {
        if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
        {
            PlayerController->Possess(Character);
        }
    }
    Character->ClearSeat(ExitLocation, GetActorRotation());
    OpenDoorBriefly(Seat);
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_SEAT exit %s seat=%d at %s"), *GetNameSafe(Character), Seat, *ExitLocation.ToCompactString());
    return true;
}

void ARallyCarPawn::ServerDriverLeaving()
{
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_SEAT driver leaving the game"));
    if (Occupants.IsValidIndex(0) && IsValid(Occupants[0]))
    {
        ServerExitSeat(Occupants[0], true);
    }
}

void ARallyCarPawn::OnExitPressed(const FInputActionValue& Value)
{
    // Records the source: test runs have shown occasional exit requests with no scripted press.
    const APlayerController* PC = Cast<APlayerController>(GetController());
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_SEAT exit input: E=%d pad=%d"),
        PC && PC->IsInputKeyDown(EKeys::E) ? 1 : 0, PC && PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left) ? 1 : 0);
    ServerRequestExit();
}

void ARallyCarPawn::ServerRequestExit_Implementation()
{
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_SEAT driver pressed exit"));
    if (Occupants.IsValidIndex(0) && IsValid(Occupants[0]))
    {
        ServerExitSeat(Occupants[0]);
    }
}

void ARallyCarPawn::OnDoorPressed(const FInputActionValue& Value)
{
    ServerToggleDriverDoor();
}

void ARallyCarPawn::ServerToggleDriverDoor_Implementation()
{
    Panels->ToggleOpenFromInside(0);
}

void ARallyCarPawn::OnIgnitionPressed(const FInputActionValue& Value)
{
    ServerToggleIgnition();
}

void ARallyCarPawn::ServerToggleIgnition_Implementation()
{
    Engine->ToggleIgnition();
}

bool ARallyCarPawn::TurnKeyFromDoor(ARallyCharacter* Character)
{
    int32 Seat = INDEX_NONE;
    if (!HasAuthority() || !Character || Character->GetSeatedCar() || GetDriverCharacter() || FindSeatInReach(Character, Seat) != this
        || Seat != 0 || FMath::Abs(GetForwardSpeedKmh()) > MaxEnterSpeedKmh) return false;
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_ENGINE key turned from the driver's door by %s"), *GetNameSafe(Character));
    return Engine->ToggleIgnition();
}

void ARallyCarPawn::HandRev()
{
    if (!HasAuthority() || GetController() || !Engine->IsRunning()) return;
    if (GetWorld()->GetTimeSeconds() >= HandRevUntil) UE_LOG(LogRallyCar, Display, TEXT("RALLY_ENGINE revved by hand from %.0f rpm"), EngineRpmShown);
    HandRevUntil = GetWorld()->GetTimeSeconds() + 0.35;
}

void ARallyCarPawn::OpenDoorBriefly(int32 Seat)
{
    DoorsOpen |= static_cast<uint8>(1 << Seat);
    DoorCloseTime[Seat] = GetWorld()->GetTimeSeconds() + 1.4;
}

void ARallyCarPawn::UpdateEngineMotion(float DeltaSeconds, double Rpm, double PowerKw)
{
    using namespace Rally::EngineMotion;
    if (!EngineBodyMesh || GetNetMode() == NM_DedicatedServer || DeltaSeconds <= 0.0f) return;
    const bool bRunning = Engine->IsRunning();
    const double Speed = bRunning ? Rpm : 0.0;
    // A cylinder that does not fire shakes it as a worn engine does.
    const int32 Dead = Rally::Engine::Cylinders - Rally::Engine::FiringCylinders(Engine->GetModules());
    const double Rough = FMath::Max(Roughness(Engine->GetEngineHealth(),
        bRunning ? Rally::Engine::Detonation(Engine->GetModules().KnockPerSecond) : 0.0), 0.35 * Dead);
    // A misfire now and then when it runs rough, and each firing a coil misses: a jolt one way or the other.
    const double CoilMisfires = CoilMisfireShare * Rally::Engine::Cylinders * Speed / 120.0;
    double Jolt = 0.0;
    if (bRunning && FMath::FRand() < (MisfiresPerSecond(Rough) + CoilMisfires) * DeltaSeconds) Jolt = FMath::FRandRange(-1.0, 1.0) * 40.0;
    Step(EngineMotionState, bRunning, Speed, TorqueNm(PowerKw, Speed), Jolt, DeltaSeconds);
    EngineMotionMaxRoll = FMath::Max(EngineMotionMaxRoll, FMath::Abs(EngineMotionState.Roll));
    // The buzz: two tones the frame rate can show, quick enough to read as vibration.
    EngineBuzzTime += DeltaSeconds;
    const double Buzz = BuzzDeg(bRunning, Speed, Rough)
        * (0.6 * FMath::Sin(EngineBuzzTime * 2.0 * UE_DOUBLE_PI * 13.0) + 0.4 * FMath::Sin(EngineBuzzTime * 2.0 * UE_DOUBLE_PI * 23.0 + 1.3));
    // About the crank, which runs across the car: a positive roll takes the top of the engine back towards the
    // bulkhead (the reaction to the crank's turn under power).
    const FVector Crank = AssemblyOffset + CrankPoint;
    const FTransform Motion = FTransform(-Crank) * FTransform(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-(EngineMotionState.Roll + Buzz))))
        * FTransform(Crank);
    EngineBodyMesh->SetRelativeTransform(EngineBodyRest * Motion);
    CrankPulleyMesh->SetRelativeTransform(FTransform(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(EngineMotionState.CrankDeg)))
        * CrankPulleyRest * Motion);
    // The belt runs round the crank, alternator, power-steering and air-conditioning pulleys, each turning at its
    // ratio of the crank; with the alternator off, the belt is off too.
    BeltMesh->SetRelativeTransform(BeltRest * Motion);
    for (int32 P = 0; P < PulleyMeshes.Num() && P < PulleyRests.Num(); ++P)
        PulleyMeshes[P]->SetRelativeTransform(FTransform(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(EngineMotionState.CrankDeg * PulleyRatios[P])))
            * PulleyRests[P] * Motion);
    AlternatorPulleyMesh->SetRelativeRotation(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(EngineMotionState.CrankDeg * AlternatorPulleyRatio)));
    const bool bAlternatorOn = !Panels->IsRemoved(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Alternator));
    BeltMesh->SetVisibility(bAlternatorOn);
    AlternatorPulleyMesh->SetVisibility(bAlternatorOn);
    const double BeltStep = bAlternatorOn ? FMath::Min(Speed / 60.0 * BeltUvPerCrankTurn, BeltUvMaxPerSecond) * DeltaSeconds : 0.0;
    BeltTravel += BeltStep;
    if (BeltLook) BeltLook->SetScalarParameterValue(TEXT("BeltOffset"), static_cast<float>(FMath::Fmod(BeltTravel, 1.0)));
    Panels->SetEngineMotion(Motion);
}

void ARallyCarPawn::UpdateCoilLooks()
{
    // Sport and rally coils are red; the stock ones black (the model's plastic).
    static UMaterialInterface* Red = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Vehicles/Octavia/Engine18T/MI_Engine18T_red.MI_Engine18T_red"));
    static UMaterialInterface* Black = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Vehicles/Octavia/Engine18T/MI_Engine18T_plastic_black.MI_Engine18T_plastic_black"));
    for (int32 C = 0; C < URallyPanelComponent::NumCoils; ++C)
    {
        const URallyPanelComponent::EModule Coil = static_cast<URallyPanelComponent::EModule>(static_cast<int32>(URallyPanelComponent::EModule::Coil1) + C);
        UStaticMeshComponent* Mesh = ModuleMeshes.IsValidIndex(static_cast<int32>(Coil)) ? ModuleMeshes[static_cast<int32>(Coil)].Get() : nullptr;
        if (!Mesh || !Red || !Black) continue;
        const bool bSport = Panels->GetGrade(URallyPanelComponent::ModuleSlot(Coil)) >= Rally::Grade::EGrade::Sport;
        Mesh->SetMaterial(0, bSport ? Red : Black);
    }
}

void ARallyCarPawn::UpdateFans(float DeltaSeconds)
{
    // The radiator's electric fans come on above FanOnC while the battery holds up, engine running or not, and
    // run up and down over a second or so. Turned slowly enough for the blades to read at any frame rate.
    const bool bOn = Engine->GetTempC() > Rally::Engine::FanOnC && Engine->GetCharge() > Rally::Engine::FlatCharge;
    FanTurnsPerSecond = FMath::FInterpTo(FanTurnsPerSecond, bOn ? 6.0 : 0.0, static_cast<double>(DeltaSeconds), 1.5);
    FanDeg = FMath::Fmod(FanDeg + FanTurnsPerSecond * 360.0 * DeltaSeconds, 360.0);
    for (UStaticMeshComponent* Fan : FanMeshes)
        if (Fan) Fan->SetRelativeRotation(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(FanDeg)));
}

void ARallyCarPawn::UpdateDoors(float DeltaSeconds)
{
    if (HasAuthority())
    {
        for (int32 Seat = 0; Seat < SeatCount; ++Seat)
        {
            if ((DoorsOpen & (1 << Seat)) && GetWorld()->GetTimeSeconds() > DoorCloseTime[Seat])
            {
                DoorsOpen &= static_cast<uint8>(~(1 << Seat));
            }
        }
    }
    Panels->TickPanels(DeltaSeconds, DoorsOpen);
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
    // A drunk driver's head sways as it does on foot (docs/BODY.md).
    const ARallyCharacter* Driver = GetDriverCharacter();
    const float Sway = Driver ? static_cast<float>(Rally::Body::SwayDeg(Driver->GetDrunk()) * FMath::Sin(GetWorld()->GetTimeSeconds() * 0.9)) : 0.0f;
    CockpitCamera->SetRelativeRotation(FRotator(Pitch, Yaw, Sway));
}

void ARallyCarPawn::ResetUpright()
{
    // Only the simulating machine can move the car; observers see the jump through ResetCount.
    if (!bSimulatesHere)
    {
        return;
    }
    ++ResetCount;
    const FRotator Upright(0.0, GetActorRotation().Yaw, 0.0);
    Body->SetWorldLocationAndRotation(GetActorLocation() + FVector(0.0, 0.0, 150.0), Upright, false, nullptr, ETeleportType::ResetPhysics);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    FScopeLock Lock(&PhysicsLock);
    bResetWheelState = true;
}

void ARallyCarPawn::TickStageDrive(float DeltaSeconds)
{
    using EPhase = ARallyStageDirector::EPhase;
    StageDriveTime += DeltaSeconds;
    ThrottleInput = BrakeInput = SteerTarget = 0.0f;
    bHandbrake = false;
    const ARallyStageDirector* Director = ARallyStageDirector::Get(GetWorld());
    // -RallyStageLooseBolt: a front wheel bolt left out, which the scrutineer must not let start.
    if (StageDriveTime <= DeltaSeconds && HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("RallyStageLooseBolt")))
        Panels->DebugSetFixingOut(URallyPanelComponent::FirstWheel, 0);
    const auto Finish = [this](const TCHAR* Result) {
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_STAGEDRIVE_DONE result=%s max_side=%.1fm state=[%s]"), Result, StageDriveMaxSide, *DescribeDamage());
        bAutoDrive = false;
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyStageDrive"));
    };
    if (!Director)
    {
        if (StageDriveTime > 10.0) Finish(TEXT("no_stages"));
        return;
    }
    const EPhase Phase = Director->GetPhase();
    if (StageDriveRecce && Phase == EPhase::Idle && StageDriveDone < 0.0) StageDriveDone = StageDriveTime;
    // Held on the handbrake at the line until the countdown ends (the footbrake at a standstill selects
    // reverse).
    // -RallyStageRecce: a recce instead of a timed run, once seated at the line.
    if ((Phase == EPhase::Idle || Phase == EPhase::Countdown) && HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("RallyStageRecce"))
        && StageDriveDone < 0.0 && const_cast<ARallyStageDirector*>(Director)->StartRecce(this))
        return;
    if (Phase == EPhase::Idle && StageDriveDone >= 0.0)
    {
        // The recce done: stopped.
        bHandbrake = true;
        if (StageDriveTime - StageDriveDone > 3.0) Finish(TEXT("recce_done"));
        return;
    }
    // -RallyStageJump: goes with four seconds still on the countdown (it stands 9 m back), to be penalised.
    const bool bJumping = Phase == EPhase::Countdown && Director->GetCountdown() <= 4 && FParse::Param(FCommandLine::Get(), TEXT("RallyStageJump"));
    if ((Phase == EPhase::Idle || Phase == EPhase::Countdown) && !bJumping)
    {
        bHandbrake = true;
        if (Phase == EPhase::Idle && StageDriveTime > 30.0) Finish(TEXT("never_started"));
        return;
    }
    if (Phase == EPhase::Recce) StageDriveRecce = true;
    if (Phase == EPhase::Finished || Phase == EPhase::Retired)
    {
        // Braked to a stop past the finish, then the handbrake.
        BrakeInput = GetForwardSpeedKmh() > 3.0f ? 1.0f : 0.0f;
        bHandbrake = GetForwardSpeedKmh() <= 3.0f;
        if (StageDriveDone < 0.0) StageDriveDone = StageDriveTime;
        // The results on screen, with the HUD.
        if (StageDriveTime - StageDriveDone > 2.0 && StageDriveTime - StageDriveDone - DeltaSeconds <= 2.0)
            FScreenshotRequest::RequestScreenshot(TEXT("RallyStage_results.png"), true, false);
        if (StageDriveTime - StageDriveDone > 4.0) Finish(Phase == EPhase::Finished ? TEXT("finished") : TEXT("retired"));
        return;
    }
    const int32 StageIndex = Director->GetRun().Stage;
    const ARallyStageDirector::FStage& Stage = Director->GetStage(StageIndex);
    const FVector Location = GetActorLocation();
    const double Along = Director->Progress(StageIndex, Location, StageDriveHint);
    const double Speed = FVector::DotProduct(GetVelocity(), GetActorForwardVector()) / 100.0;
    // Steering: at a point further along the road, further the faster it goes (pure pursuit, 2.6 m wheelbase).
    // The steering's lock falls away with speed, so more input for the same wheel angle, as a driver does.
    const double Look = FMath::Clamp(6.0 + 0.55 * Speed, 8.0, 24.0);
    const FVector Local = GetActorTransform().InverseTransformPosition(Director->PointAt(StageIndex, Along + Look)) / 100.0;
    const double Alpha = FMath::Atan2(Local.Y, Local.X);
    const double WheelDeg = FMath::RadiansToDegrees(FMath::Atan(2.0 * 2.6 * FMath::Sin(Alpha) / Look));
    const double Falloff = SteerSpeedFalloffKmh > 0.0f ? 1.0 + FMath::Abs(Speed * 3.6) / SteerSpeedFalloffKmh : 1.0;
    SteerTarget = static_cast<float>(FMath::Clamp(WheelDeg / MaxSteerAngle * Falloff, -1.0, 1.0));
    // Speed: what the bends in the next 90 m allow, braking for them in time; gentler on loose surfaces.
    const bool bLoose = Stage.Surface != TEXT("Asphalt");
    const double Grip = bLoose ? 4.5 : 6.0;
    const double Decel = bLoose ? 4.0 : 5.5;
    // On a recce, no faster than its limit.
    double Allowed = Phase == EPhase::Recce ? Rally::Stage::RecceLimitKmh / 3.6 - 1.0 : 28.0;
    for (int32 I = StageDriveHint; I < static_cast<int32>(Stage.Route.size()) && Stage.Distance[I] - Along < 90.0; ++I)
    {
        const double Corner = Stage.Curvature[I] > 1e-4 ? FMath::Sqrt(Grip / Stage.Curvature[I]) : 28.0;
        Allowed = FMath::Min(Allowed, FMath::Sqrt(Corner * Corner + 2.0 * Decel * FMath::Max(Stage.Distance[I] - Along - 5.0, 0.0)));
    }
    if (Speed < Allowed - 0.5) ThrottleInput = static_cast<float>(FMath::Clamp((Allowed - Speed) / 3.0, 0.35, 1.0));
    else if (Speed > Allowed + 1.0) BrakeInput = static_cast<float>(FMath::Clamp((Speed - Allowed) / 4.0, 0.15, 1.0));
    else ThrottleInput = 0.25f;
    // How far off the centre line, and whether it is still getting anywhere.
    const FVector OnRoute = Director->PointAt(StageIndex, Along);
    StageDriveMaxSide = FMath::Max(StageDriveMaxSide, FVector::Dist2D(Location, OnRoute) / 100.0);
    if (Along > StageDriveBest + 1.0) { StageDriveBest = Along; StageDriveBestTime = StageDriveTime; }
    if (StageDriveTime - StageDriveBestTime > 12.0) { Finish(TEXT("stuck")); return; }
    if (StageDriveTime - StageDriveLogged >= 2.0)
    {
        // Mid-stage, with the clock and the pace notes on screen.
        if (Director->GetClock() > 20.0 && Director->GetClock() <= 22.1)
            FScreenshotRequest::RequestScreenshot(TEXT("RallyStage_running.png"), true, false);
        StageDriveLogged = StageDriveTime;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_STAGEDRIVE t=%.1f along=%.0f/%.0f m speed=%.1f allowed=%.1f m/s side=%.1f m steer=%.2f"),
            Director->GetClock(), Along, Stage.LengthM, Speed, Allowed, FVector::Dist2D(Location, OnRoute) / 100.0, SteerTarget);
    }
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
        // The manual box's selected gear, or 9 for the automatic box; the clutch pedal held down.
        int8 Gear = 9;
        bool bClutch = false;
    };
    // Drop/settle, straight-line acceleration and braking on the gravel road, then off the road onto the
    // uniform loose ground for steady cornering (no surface edge mid-corner), handbrake turn, reverse;
    // then the manual box: clutch-drop launch, a shift without the clutch, a power slide, reverse gear.
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
        { 41.0f, TEXT("brake3"), 0.0f, 1.0f, 0.0f, false, 0, false },
        { 43.0f, TEXT("clutch_rev"), 1.0f, 0.0f, 0.0f, false, 1, true },
        { 45.5f, TEXT("clutch_drop"), 1.0f, 0.0f, 0.0f, false, 1, false },
        { 47.0f, TEXT("second"), 1.0f, 0.0f, 0.0f, false, 2, false },
        // Handbrake flick (clutch in) into a slide, then full throttle through it.
        { 47.8f, TEXT("slide_kick"), 0.0f, 0.0f, 1.0f, true, 2, true },
        { 51.0f, TEXT("power_slide"), 1.0f, 0.0f, 1.0f, false, 2, false },
        { 54.5f, TEXT("brake4"), 0.0f, 1.0f, 0.0f, false, 2, true },
        { 57.0f, TEXT("reverse_gear"), 0.6f, 0.0f, 0.0f, false, -1, false },
        { 58.0f, TEXT("brake5"), 0.0f, 1.0f, 0.0f, false, 0, false },
        { 60.5f, TEXT("get_out"), 0.0f, 0.0f, 0.0f, true, 0, false },
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
    bAutoDriveManual = Phase->Gear != 9;
    if (bAutoDriveManual) SelectedGear = Phase->Gear;
    bClutchHeld = Phase->bClutch;

    const FVector Location = GetActorLocation();
    const float Kmh = GetForwardSpeedKmh();

    // Straight-line stop: distance from the moment the brake phase starts until below 1 km/h.
    if (Phase->Name != AutoDrivePhase)
    {
        // The driver gets out once the car has stopped: exercises the exit path every run.
        if (FCString::Strcmp(Phase->Name, TEXT("get_out")) == 0 && IsLocallyControlled())
        {
            FTimerHandle Handle;
            GetWorldTimerManager().SetTimer(Handle, [this] { if (IsLocallyControlled()) { ServerRequestExit(); } }, 1.5f, false);
        }
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

    // Saved/Screenshots. Steps are spaced in frames, not seconds: start-up frames are slow, and several
    // timed steps landing in one frame would capture only the last camera state.
    const auto Crossed = [&](float Time) { return AutoDriveTime >= Time && AutoDriveTime - DeltaSeconds < Time; };
    const auto Look = [this](float Yaw, float Pitch) { MouseLook = FVector2D(Yaw, Pitch); MouseIdleTime = 0.0f; };
    if (AutoDriveTime >= 1.9f && AutoDriveShotStep < 12 && ++AutoDriveShotFrames >= 8)
    {
        AutoDriveShotFrames = 0;
        switch (AutoDriveShotStep++)
        {
        case 0: SetCockpitView(true); Look(0.0f, 0.0f); break;
        case 1: FScreenshotRequest::RequestScreenshot(TEXT("RallyAutoDrive_cockpit.png"), false, false); break;
        case 2: SetCockpitView(false); break;
        case 3: FScreenshotRequest::RequestScreenshot(TEXT("RallyAutoDrive_chase.png"), false, false); break;
        case 4: SetCockpitView(true); Look(-75.0f, 10.0f); break;
        case 5: FScreenshotRequest::RequestScreenshot(TEXT("RallyAutoDrive_cockpit_left.png"), false, false); break;
        case 6: Look(0.0f, 38.0f); break;
        case 7: FScreenshotRequest::RequestScreenshot(TEXT("RallyAutoDrive_cockpit_up.png"), false, false); break;
        case 8: Look(-150.0f, 0.0f); break;
        case 9: FScreenshotRequest::RequestScreenshot(TEXT("RallyAutoDrive_cockpit_back.png"), false, false); break;
        default: Look(0.0f, 0.0f); break;
        }
    }
    if (Crossed(26.0f)) FScreenshotRequest::RequestScreenshot(TEXT("RallyAutoDrive_corner.png"), false, false);
    // -RallyLoosePanels: bonnet and rear-left door latches gone before the drive, rear-right door left
    // open by hand; their hinges log below.
    const bool bLoosePanels = FParse::Param(FCommandLine::Get(), TEXT("RallyLoosePanels"));
    // -RallyBreakPart=<ERallyPart name> (repeatable as a comma list): those parts fail before the drive.
    FString BreakParts;
    if (Crossed(1.0f) && HasAuthority() && FParse::Value(FCommandLine::Get(), TEXT("RallyBreakPart="), BreakParts, false))
    {
        TArray<FString> Names;
        BreakParts.ParseIntoArray(Names, TEXT(","));
        for (const FString& Name : Names)
        {
            const int64 Value = StaticEnum<ERallyPart>()->GetValueByNameString(Name);
            if (Value != INDEX_NONE) Damage->ApplyPartDamage(static_cast<ERallyPart>(Value), 1.0f);
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_AUTODRIVE broke %s (%s)"), *Name, Value != INDEX_NONE ? TEXT("ok") : TEXT("unknown part"));
        }
    }
    if (bLoosePanels && Crossed(1.0f) && HasAuthority())
    {
        Panels->DebugBreakLatch(4);
        Panels->DebugBreakLatch(2);
        Panels->ToggleOpenFromInside(3);
    }

    TArray<FWheelState> States;
    double EngineRpm = 0.0, EngineLoad = 0.0;
    int32 Gear = 0;
    {
        FScopeLock Lock(&PhysicsLock);
        States = Visual.Wheels;
        EngineRpm = Visual.EngineRpm;
        EngineLoad = Visual.EngineLoad;
        Gear = Visual.Gear;
    }

    // 20 Hz while settling (drop test for ride frequency and damping), then 2 Hz.
    if (AutoDriveTime >= NextAutoDriveLog)
    {
        // 10 Hz through the launch and the slide, where the throttle and rpm change fast.
        NextAutoDriveLog += AutoDriveTime < 2.5f ? 0.05f : AutoDriveTime > 41.0f && AutoDriveTime < 58.0f ? 0.1f : 0.5f;
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
            TEXT("RALLY_AUTODRIVE t=%.2f phase=%s kmh=%.1f gear=%d rpm=%.0f load=%.2f origin_z=%.1f roll=%.1f pitch=%.1f yaw=%.0f lat_g=%.2f slip=%.1f wheels%s at=%.1f,%.1f"),
            AutoDriveTime, Phase->Name, Kmh, Gear, EngineRpm, EngineLoad, Location.Z + AssemblyOffset.Z,
            Rotation.Roll, Rotation.Pitch, Rotation.Yaw, LateralG, SideSlip, *WheelText, Location.X / 100.0, Location.Y / 100.0);
        if (bLoosePanels && AutoDriveTime >= 2.5f)
        {
            const auto Turned = [this](int32 Panel) {
                return FMath::RadiansToDegrees(PanelMeshes[Panel]->GetRelativeRotation().Quaternion().AngularDistance(FQuat::Identity));
            };
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_AUTODRIVE loose t=%.2f phase=%s kmh=%.1f bonnet=%.1f rear_left_door=%.1f rear_right_open=%d at %.1f"),
                AutoDriveTime, Phase->Name, Kmh, Turned(4), Turned(2), Panels->IsOpen(3) ? 1 : 0, Turned(3));
        }
    }
}

void ARallyCarPawn::OnBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    // The simulating machine sees the contacts; the server decides what they break.
    // People on foot are not obstacles that crumple a car.
    if (!bSimulatesHere || Cast<ACharacter>(OtherActor) || NormalImpulse.IsNearlyZero())
    {
        return;
    }
    const FTransform& Frame = GetActorTransform();
    const FVector Point = Frame.InverseTransformPosition(Hit.ImpactPoint);
    // The impulse pushes the car away from what it hit, so it points into the body at the contact.
    const FVector Direction = Frame.InverseTransformVectorNoScale(NormalImpulse.GetSafeNormal());
    const double Impulse = NormalImpulse.Size();
    // Ground or a rock pushing up on the floor (landings, bottoming out, a crest) dents nothing, but what
    // hangs under the floor takes the hit.
    if (Direction.Z > 0.6)
    {
        const FBox Patch = FindContactPatch(OtherComp, Direction);
        const FVector Centre = Patch.IsValid ? Patch.GetCenter() : Point;
        if (!PendingUnderside.bActive)
        {
            PendingUnderside.bActive = true;
            PendingUnderside.StartTime = GetWorld()->GetTimeSeconds();
        }
        PendingUnderside.Impulse += Impulse;
        PendingUnderside.WeightedPoint += Centre * Impulse;
        PendingUnderside.WeightedDirection += Direction * Impulse;
        if (Patch.IsValid)
        {
            PendingUnderside.Patch += Patch;
        }
        return;
    }
    // Chaos reports one contact point per pair, often a box corner; the patch says what actually touched.
    const FBox Patch = FindContactPatch(OtherComp, Direction);
    const FVector Centre = Patch.IsValid ? Patch.GetCenter() : Point;
    if (PendingImpact.bActive && FVector::Dist(Centre, PendingImpact.WeightedPoint / PendingImpact.Impulse) > 150.0)
    {
        FlushPendingImpact();
    }
    if (!PendingImpact.bActive)
    {
        PendingImpact.bActive = true;
        PendingImpact.StartTime = GetWorld()->GetTimeSeconds();
    }
    PendingImpact.Impulse += Impulse;
    PendingImpact.WeightedPoint += Centre * Impulse;
    PendingImpact.WeightedDirection += Direction * Impulse;
    if (Patch.IsValid)
    {
        PendingImpact.Patch += Patch;
    }
}

FBox ARallyCarPawn::FindContactPatch(const UPrimitiveComponent* OtherComp, const FVector& LocalDirection) const
{
    FBox Patch(ForceInit);
    if (!OtherComp)
    {
        return Patch;
    }
    // The box face the impact pushes on: the axis the push is most aligned with.
    const FVector Abs = LocalDirection.GetAbs();
    const int32 Axis = Abs.X >= Abs.Y && Abs.X >= Abs.Z ? 0 : (Abs.Y >= Abs.Z ? 1 : 2);
    const double Side = LocalDirection[Axis] > 0.0 ? -1.0 : 1.0;
    const int32 AxisU = (Axis + 1) % 3;
    const int32 AxisV = (Axis + 2) % 3;
    const int32 Steps[3] = { 9, 5, 4 };
    const FTransform& Frame = GetActorTransform();
    FVector FaceNormal = FVector::ZeroVector;
    FaceNormal[Axis] = Side;
    FaceNormal = Frame.TransformVectorNoScale(FaceNormal);
    FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RallyContactPatch), true, this);
    TArray<TPair<FVector, float>, TInlineAllocator<64>> Samples;
    float Nearest = TNumericLimits<float>::Max();
    for (int32 IU = 0; IU < Steps[AxisU]; ++IU)
    {
        for (int32 IV = 0; IV < Steps[AxisV]; ++IV)
        {
            FVector Sample;
            Sample[Axis] = Side * BodyHalfExtent[Axis];
            Sample[AxisU] = BodyHalfExtent[AxisU] * (-0.95 + 1.9 * IU / (Steps[AxisU] - 1));
            Sample[AxisV] = BodyHalfExtent[AxisV] * (-0.95 + 1.9 * IV / (Steps[AxisV] - 1));
            FVector Closest;
            const FVector At = Frame.TransformPosition(Sample);
            // 0 means inside the other shape; negative means it has no simple collision to query.
            float Distance = OtherComp->GetClosestPointOnCollision(At, Closest);
            if (Distance < 0.0f)
            {
                // Collision made of the mesh's own triangles (scenery, buildings, trees): trace across the
                // face from inside the body instead; a surface already inside the face counts as touching.
                FHitResult Hit;
                // A read-only query; the engine just does not declare it const.
                if (const_cast<UPrimitiveComponent*>(OtherComp)->LineTraceComponent(Hit, At - FaceNormal * 80.0, At + FaceNormal * 60.0, TraceParams))
                {
                    Distance = FMath::Max(static_cast<float>(FVector::DotProduct(Hit.ImpactPoint - At, FaceNormal)), 0.0f);
                }
            }
            if (Distance >= 0.0f)
            {
                Samples.Emplace(Sample, Distance);
                Nearest = FMath::Min(Nearest, Distance);
            }
        }
    }
    // The game thread shows the body interpolated behind the physics step that made the contact
    // (up to about 30 cm at speed), so contact is judged relative to the nearest sample.
    if (Nearest > 60.0f)
    {
        return Patch;
    }
    for (const TPair<FVector, float>& Sample : Samples)
    {
        if (Sample.Value <= Nearest + 12.0f)
        {
            Patch += Sample.Key;
        }
    }
    return Patch;
}

void ARallyCarPawn::FlushPendingUnderside()
{
    const FPendingImpact Hit = PendingUnderside;
    PendingUnderside = FPendingImpact();
    if (!Hit.bActive || Hit.Impulse <= 0.0)
    {
        return;
    }
    const float DeltaV = static_cast<float>(Hit.Impulse / FMath::Max(Body->GetMass(), 1.0f) / 100.0);
    if (!FMath::IsFinite(DeltaV) || DeltaV < Rally::Underside::MinDeltaV || DeltaV > Rally::Impact::MaxDeltaV)
    {
        return;
    }
    const FVector Point = Hit.Patch.IsValid ? Hit.Patch.GetCenter() : Hit.WeightedPoint / Hit.Impulse;
    const FVector Extent = Hit.Patch.IsValid ? Hit.Patch.GetExtent() : FVector::ZeroVector;
    ++UndersideHits;
    UndersideMaxDeltaV = FMath::Max(UndersideMaxDeltaV, DeltaV);
    PlayCrashSound(1, DeltaV);
    if (HasAuthority())
    {
        ApplyUndersideHit(Point, Extent, DeltaV);
    }
    else
    {
        ServerReportUnderside(Point, Extent, DeltaV);
    }
}

void ARallyCarPawn::ServerReportUnderside_Implementation(FVector_NetQuantize10 LocalPoint, FVector_NetQuantize10 LocalExtent, float DeltaV)
{
    // Believed within the same limits as impacts, sharing their burst cap.
    const double Now = GetWorld()->GetTimeSeconds();
    RecentImpactTimes.RemoveAll([Now](double Time) { return Now - Time > 1.0; });
    const FVector Extent = BodyHalfExtent + FVector(50.0);
    const bool bOnBody = FMath::Abs(LocalPoint.X) <= Extent.X && FMath::Abs(LocalPoint.Y) <= Extent.Y && FMath::Abs(LocalPoint.Z) <= Extent.Z;
    const bool bPatchFits = LocalExtent.X >= 0.0 && LocalExtent.Y >= 0.0 && LocalExtent.Z >= 0.0
        && LocalExtent.X <= BodyHalfExtent.X && LocalExtent.Y <= BodyHalfExtent.Y && LocalExtent.Z <= BodyHalfExtent.Z;
    if (bSimulatesHere || !bOnBody || !bPatchFits || LocalPoint.ContainsNaN() || !FMath::IsFinite(DeltaV)
        || DeltaV > Rally::Impact::MaxDeltaV || RecentImpactTimes.Num() >= 8)
    {
        UE_LOG(LogRallyCar, Warning, TEXT("Rejected underside report from %s."), *GetNameSafe(GetController()));
        return;
    }
    RecentImpactTimes.Add(Now);
    ApplyUndersideHit(LocalPoint, LocalExtent, DeltaV);
}

void ARallyCarPawn::ApplyUndersideHit(const FVector& LocalPoint, const FVector& LocalExtent, float DeltaV)
{
    if (!HasAuthority())
    {
        return;
    }
    using namespace Rally::Underside;
    const FBox Patch(LocalPoint - LocalExtent, LocalPoint + LocalExtent);
    const auto Reach = [&](const FVector& At)
    {
        return Rally::Impact::ReachFactor(FMath::Sqrt(Patch.ComputeSquaredDistanceToPoint(AssemblyOffset + At)), ReachCm, 0.0);
    };
    double ExhaustReach = 0.0;
    for (int32 Index = 0; Index < NumExhaustPoints; ++Index)
    {
        ExhaustReach = FMath::Max(ExhaustReach, Reach(ExhaustPoint(Index)));
    }
    // A broad flat contact (the floor landing on the ground) spreads the load.
    const double Spread = Concentration(LocalExtent.X, LocalExtent.Y);
    ExhaustReach *= Spread;
    const double SumpReach = Reach(SumpPoint()) * Spread;
    const double GearboxReach = Reach(GearboxPoint()) * Spread;
    const double TankReach = Reach(FuelTankPoint()) * Spread;
    Damage->ApplyPartDamage(ERallyPart::OilSump, static_cast<float>(SumpDamage(DeltaV, SumpReach)));
    Damage->ApplyPartDamage(ERallyPart::Gearbox, static_cast<float>(GearboxDamage(DeltaV, GearboxReach)));
    Damage->ApplyPartDamage(ERallyPart::FuelTank, static_cast<float>(TankDamage(DeltaV, TankReach)));
    if (!Panels->IsRemoved(URallyPanelComponent::ExhaustSlot))
    {
        Damage->ApplyPartDamage(ERallyPart::Exhaust, static_cast<float>(ExhaustDamage(DeltaV, ExhaustReach)));
    }
    MulticastCrashSound(1, static_cast<uint8>(FMath::Clamp(DeltaV * 8.0f, 0.0f, 255.0f)));
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_UNDERSIDE dv=%.2fm/s at=%s extent=%s reach sump=%.2f gearbox=%.2f exhaust=%.2f tank=%.2f state=[%s]"),
        DeltaV, *LocalPoint.ToCompactString(), *LocalExtent.ToCompactString(), SumpReach, GearboxReach, ExhaustReach, TankReach, *DescribeDamage());
}

void ARallyCarPawn::FlushPendingImpact()
{
    const FPendingImpact Impact = PendingImpact;
    PendingImpact = FPendingImpact();
    if (!Impact.bActive || Impact.Impulse <= 0.0)
    {
        return;
    }
    // Impulse in kg cm/s; change of velocity in m/s.
    const float DeltaV = static_cast<float>(Impact.Impulse / FMath::Max(Body->GetMass(), 1.0f) / 100.0);
    if (!Rally::Impact::IsValidDeltaV(DeltaV))
    {
        return;
    }
    const FVector Point = Impact.Patch.IsValid ? Impact.Patch.GetCenter() : Impact.WeightedPoint / Impact.Impulse;
    const FVector Extent = Impact.Patch.IsValid ? Impact.Patch.GetExtent() : FVector::ZeroVector;
    const FVector Direction = Impact.WeightedDirection.GetSafeNormal();
    if (bCrashTest && CrashImpactTime < 0.0f)
    {
        CrashImpactTime = CrashTestTime;
    }
    PlayCrashSound(0, DeltaV);
    if (HasAuthority())
    {
        ApplyImpact(Point, Extent, Direction, DeltaV);
    }
    else
    {
        ServerReportImpact(Point, Extent, Direction, DeltaV);
    }
}

void ARallyCarPawn::ServerReportImpact_Implementation(FVector_NetQuantize10 LocalPoint, FVector_NetQuantize10 LocalExtent, FVector_NetQuantizeNormal LocalDirection, float DeltaV)
{
    // Co-op trust model as for driving state: believed within plausibility limits. The point must lie
    // on or near the body box, and bursts of reports are capped.
    const double Now = GetWorld()->GetTimeSeconds();
    RecentImpactTimes.RemoveAll([Now](double Time) { return Now - Time > 1.0; });
    const FVector Extent = BodyHalfExtent + FVector(50.0);
    const bool bOnBody = FMath::Abs(LocalPoint.X) <= Extent.X && FMath::Abs(LocalPoint.Y) <= Extent.Y && FMath::Abs(LocalPoint.Z) <= Extent.Z;
    const bool bPatchFits = LocalExtent.X >= 0.0 && LocalExtent.Y >= 0.0 && LocalExtent.Z >= 0.0
        && LocalExtent.X <= BodyHalfExtent.X && LocalExtent.Y <= BodyHalfExtent.Y && LocalExtent.Z <= BodyHalfExtent.Z;
    if (bSimulatesHere || !bOnBody || !bPatchFits || LocalPoint.ContainsNaN() || LocalDirection.ContainsNaN() || RecentImpactTimes.Num() >= 8)
    {
        UE_LOG(LogRallyCar, Warning, TEXT("Rejected impact report from %s."), *GetNameSafe(GetController()));
        return;
    }
    RecentImpactTimes.Add(Now);
    ApplyImpact(LocalPoint, LocalExtent, LocalDirection, DeltaV);
}

void ARallyCarPawn::ApplyImpact(const FVector& LocalPoint, const FVector& LocalExtent, const FVector& LocalDirection, float DeltaV)
{
    if (!HasAuthority() || !Rally::Impact::IsValidDeltaV(DeltaV))
    {
        return;
    }
    const Rally::Impact::FDent Size = Rally::Impact::DentFor(DeltaV);
    FRallyDent Dent;
    Dent.Point = LocalPoint;
    Dent.Extent = LocalExtent;
    Dent.Direction = LocalDirection.GetSafeNormal();
    Dent.Depth = static_cast<float>(Size.Depth);
    Dent.Radius = static_cast<float>(Size.Radius);
    Dent.PartsMask = Panels->AttachedDentMask();
    BodyDeform->AddDent(Dent);

    // Parts the impact reaches. Tyres only take direct hits; a hard hit crushes back into the
    // suspension and radiator. Wheel centres at ride height.
    using namespace Rally::Impact;
    const FBox Patch(LocalPoint - LocalExtent, LocalPoint + LocalExtent);
    const double Crush = CrushDistance(Size.Depth);
    for (int32 I = 0; I < FMath::Min(BaseWheelSetups.Num(), 4); ++I)
    {
        const double Distance = FMath::Sqrt(Patch.ComputeSquaredDistanceToPoint(AssemblyOffset + BaseWheelSetups[I].Pivot));
        FRallyPartCondition Suspension, Hub;
        Damage->GetPartCondition(SuspensionParts[I], Suspension);
        Damage->GetPartCondition(HubParts[I], Hub);
        Damage->ApplyPartDamage(TyreParts[I], static_cast<float>(TyreDamage(DeltaV) * ReachFactor(Distance, WheelReachCm, 0.0)));
        Damage->ApplyPartDamage(SuspensionParts[I], static_cast<float>(SuspensionDamage(DeltaV) * ReachFactor(Distance, WheelReachCm, Crush)));
        Damage->ApplyPartDamage(StrutParts[I], static_cast<float>(Rally::Suspension::StrutDamage(DeltaV, ReachFactor(Distance, WheelReachCm, Crush))));
        Damage->ApplyPartDamage(BrakeParts[I], static_cast<float>(Rally::Suspension::BrakeDamage(DeltaV, ReachFactor(Distance, WheelReachCm, 0.0))));
        if (I < 2)
        {
            Damage->ApplyPartDamage(DriveshaftParts[I], static_cast<float>(Rally::Suspension::DriveshaftDamage(DeltaV, ReachFactor(Distance, WheelReachCm, Crush))));
        }
        // A direct hit on a corner that was already broken, or a huge one, tears the hub.
        if (Hub.Health > 0.0f && ReachFactor(Distance, WheelReachCm, 0.0) >= 1.0 && TearsWheelOff(DeltaV, Suspension.Health))
        {
            Damage->ApplyPartDamage(HubParts[I], 1.0f);
            Panels->DetachWheel(I);
        }
    }
    // The radiator sits behind the grille: only hits pushing backwards reach it.
    if (LocalDirection.X < -0.5)
    {
        const double Distance = FMath::Sqrt(Patch.ComputeSquaredDistanceToPoint(AssemblyOffset + PanelMounts[6].Pivot));
        Damage->ApplyPartDamage(ERallyPart::Radiator, static_cast<float>(RadiatorDamage(DeltaV) * ReachFactor(Distance, RadiatorReachCm, Crush) * (Panels->IsDetached(7) ? 1.35 : 1.0)));
    }

    // The engine block takes only a huge hit; the battery sits behind the left headlamp.
    {
        const double ToEngine = FMath::Sqrt(Patch.ComputeSquaredDistanceToPoint(AssemblyOffset + EngineBlockPoint()));
        const double ToBattery = FMath::Sqrt(Patch.ComputeSquaredDistanceToPoint(AssemblyOffset + BatteryPoint()));
        Damage->ApplyPartDamage(ERallyPart::Engine, static_cast<float>(Rally::Engine::EngineDamage(DeltaV, ReachFactor(ToEngine, Rally::Engine::EngineReachCm, Crush))));
        Damage->ApplyPartDamage(ERallyPart::Battery, static_cast<float>(Rally::Engine::BatteryDamage(DeltaV, ReachFactor(ToBattery, Rally::Engine::BatteryReachCm, Crush))));
        // Bolt-on modules near the hit; the intercooler sits right behind the front bumper.
        for (int32 M = 0; M < ModuleMeshes.Num(); ++M)
        {
            const int32 Slot = URallyPanelComponent::ModuleSlot(static_cast<URallyPanelComponent::EModule>(M));
            const ERallyPart Part = URallyPanelComponent::PartOfModule(Slot);
            if (Part == ERallyPart::Count || Panels->IsRemoved(Slot)) continue;
            const double ToModule = FMath::Sqrt(Patch.ComputeSquaredDistanceToPoint(ModuleMeshes[M]->GetRelativeLocation()));
            Damage->ApplyPartDamage(Part, static_cast<float>(Rally::Engine::ModuleDamage(DeltaV, ReachFactor(ToModule, Rally::Engine::ModuleReachCm, Crush))));
        }
    }

    Panels->ApplyImpact(Patch, LocalDirection, DeltaV);
    MulticastCrashSound(0, static_cast<uint8>(FMath::Clamp(DeltaV * 8.0f, 0.0f, 255.0f)));
    // A hard one hurts whoever is inside (docs/BODY.md).
    for (ARallyCharacter* Occupant : Occupants)
        if (IsValid(Occupant)) Occupant->AddInjury(Rally::Body::InjuryFromImpact(DeltaV));

    UE_LOG(LogRallyCar, Display, TEXT("RALLY_DAMAGE impact dv=%.1fm/s at=%s extent=%s dir=%s depth=%.1f radius=%.0f dents=%d state=[%s]"),
        DeltaV, *LocalPoint.ToCompactString(), *LocalExtent.ToCompactString(), *LocalDirection.ToCompactString(), Dent.Depth, Dent.Radius,
        BodyDeform->GetDentCount(), *DescribeDamage());
}

void ARallyCarPawn::OnDamageChanged()
{
    UpdatePartLooks();
    // Handling is the undamaged setup plus the symptoms of each part's condition.
    if (BaseWheelSetups.Num() != WheelSetups.Num())
    {
        return;
    }
    for (int32 I = 0; I < FMath::Min(WheelSetups.Num(), 4); ++I)
    {
        FRallyWheelSetup Wheel = BaseWheelSetups[I];
        // A part whose fixings are not all home shifts on its mounts and acts damaged (Rally::Fixing).
        const auto Held = [this](int32 Slot, FRallyPartCondition& Condition) {
            int32 Home = 0, Total = 0;
            Panels->GetFixingHold(Slot, Home, Total);
            Condition.Health = static_cast<float>(Rally::Fixing::EffectiveHealth(Condition.Health, Home, Total));
        };
        FRallyPartCondition Tyre, Suspension;
        Damage->GetPartCondition(TyreParts[I], Tyre);
        Damage->GetPartCondition(SuspensionParts[I], Suspension);
        Held(URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Arm, I), Suspension);
        FRallyPartCondition Hub;
        Damage->GetPartCondition(HubParts[I], Hub);
        const bool bWheelOn = Hub.Health > 0.0f;
        if (bWheelOn)
        {
            // Wheel bolts not all home: the wheel wobbles on its hub.
            FRallyPartCondition Bolts;
            Held(URallyPanelComponent::FirstWheel + I, Bolts);
            Wheel.ToeOutDeg += static_cast<float>(Rally::Impact::ToeOutDeg(Bolts.Health));
        }
        if (!bWheelOn)
        {
            // The corner drops onto the hub and brake disc and scrapes along.
            Wheel.Radius = static_cast<float>(Rally::Impact::HubRadiusCm);
            Wheel.Tyre.GripHard = Wheel.Tyre.GripLoose = static_cast<float>(Rally::Impact::HubGrip);
            Wheel.GroundDrag = static_cast<float>(Rally::Impact::HubDrag);
        }
        else if (Tyre.Health <= 0.0f)
        {
            Wheel.Tyre.Pressure = static_cast<float>(Rally::Impact::FlatTyrePressureBar);
            Wheel.Radius -= static_cast<float>(Rally::Impact::FlatTyreDropCm);
        }
        Wheel.ToeOutDeg += static_cast<float>(Rally::Impact::ToeOutDeg(Suspension.Health));
        // Running gear: struts set spring and damping, brakes their torque, a snapped front arm whether the
        // wheel steers at all, front driveshafts whether that side drives.
        FRallyPartCondition Strut, Brake;
        Damage->GetPartCondition(StrutParts[I], Strut);
        Damage->GetPartCondition(BrakeParts[I], Brake);
        Held(URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Strut, I), Strut);
        Held(URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Disc, I), Brake);
        Wheel.SpringRate *= static_cast<float>(Rally::Suspension::SpringRateScale(Strut.Health));
        Wheel.BumpDamping *= static_cast<float>(Rally::Suspension::DampingScale(Strut.Health));
        Wheel.ReboundDamping *= static_cast<float>(Rally::Suspension::DampingScale(Strut.Health));
        Wheel.MaxBrakeTorque *= static_cast<float>(Rally::Suspension::BrakeScale(Brake.Health));
        Wheel.HandbrakeTorque *= static_cast<float>(Rally::Suspension::BrakeScale(Brake.Health));
        // Grades and settings (Rally::Grade): a sport or rally strut is stiffer and holds the corner lower
        // (a rally one's damper clicks and height collar on top); better discs and calipers stop harder.
        {
            const int32 StrutSlot = URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Strut, I);
            const Rally::Grade::EGrade StrutGrade = Panels->GetGrade(StrutSlot);
            const Rally::Grade::FStrut Tune = Rally::Grade::StrutGrade(StrutGrade);
            const bool bAdjustable = StrutGrade == Rally::Grade::EGrade::Rally;
            const double Damping = Tune.Damping * (bAdjustable ? Rally::Grade::ClickDamping(Panels->GetClicks(StrutSlot)) : 1.0);
            Wheel.SpringRate *= static_cast<float>(Tune.Spring);
            Wheel.BumpDamping *= static_cast<float>(Damping);
            Wheel.ReboundDamping *= static_cast<float>(Damping);
            Wheel.Pivot.Z += static_cast<float>(Tune.LowerCm - (bAdjustable ? Panels->GetCollarCm(StrutSlot) : 0));
            const double Brakes = Rally::Grade::BrakeGrade(Panels->GetGrade(URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Disc, I)));
            // The bias moves the footbrake's share between the axles; the handbrake is the rear's alone.
            Wheel.MaxBrakeTorque *= static_cast<float>(Brakes * Rally::Grade::BiasScale(BiasSteps, I < 2));
            Wheel.HandbrakeTorque *= static_cast<float>(Brakes);
        }
        if (I < 2)
        {
            FRallyPartCondition Driveshaft;
            Damage->GetPartCondition(DriveshaftParts[I], Driveshaft);
            Held(URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Driveshaft, I), Driveshaft);
            Wheel.bSteered = Wheel.bSteered && Rally::Suspension::Steers(Suspension.Health);
            Wheel.bDriveshaftBroken = Wheel.bDriven && !Rally::Suspension::Drives(Driveshaft.Health);
        }
        WheelSetups[I] = Wheel;
        if (WheelMeshes.IsValidIndex(I))
        {
            WheelMeshes[I]->SetVisibility(bWheelOn);
        }
    }
    // Engine modules, as the engine feels them: missing or on loose fixings counts against them.
    {
        const auto Module = [this](URallyPanelComponent::EModule Kind) {
            const int32 Slot = URallyPanelComponent::ModuleSlot(Kind);
            const ERallyPart Part = URallyPanelComponent::PartOfModule(Slot);
            FRallyPartCondition Condition;
            Damage->GetPartCondition(Part, Condition);
            int32 Home = 0, Total = 0;
            Panels->GetFixingHold(Slot, Home, Total);
            return Panels->IsRemoved(Slot) || (Total > 0 && Home == 0) ? 0.0 : Rally::Fixing::EffectiveHealth(Condition.Health, Home, Total);
        };
        using EModule = URallyPanelComponent::EModule;
        Rally::Engine::FModules Modules;
        Modules.AirFilter = Module(EModule::AirFilter);
        Modules.Intake = Module(EModule::Intake);
        Modules.ExhaustManifold = Module(EModule::ExhaustManifold);
        Modules.Turbo = Module(EModule::Turbo);
        Modules.Intercooler = Module(EModule::Intercooler);
        Modules.Alternator = Module(EModule::Alternator);
        Modules.Starter = Module(EModule::Starter);
        Modules.Ecu = Module(EModule::Ecu);
        // Each coil as the engine feels it (unplugged counts as dead), and the boost its grade holds.
        for (int32 C = 0; C < URallyPanelComponent::NumCoils; ++C)
        {
            const EModule Coil = static_cast<EModule>(static_cast<int32>(EModule::Coil1) + C);
            Modules.Coils[C] = Module(Coil);
            Modules.CoilBoostLimit[C] = Rally::Grade::CoilBoostLimit(Panels->GetGrade(URallyPanelComponent::ModuleSlot(Coil)));
        }
        // The turbo's and computer's grades and the boost set at the wastegate.
        const int32 TurboSlot = URallyPanelComponent::ModuleSlot(EModule::Turbo);
        const int32 EcuSlot = URallyPanelComponent::ModuleSlot(EModule::Ecu);
        const Rally::Grade::EGrade Chip = Panels->GetGrade(EcuSlot);
        const Rally::Grade::FMap Map = Rally::Grade::EngineMap(Panels->GetEngineMap(EcuSlot));
        Modules.BaseTune = Rally::Grade::EcuGrade(Chip) * Map.Power;
        Modules.Tune = Modules.BaseTune * Rally::Grade::TurboGrade(Panels->GetGrade(TurboSlot))
            * Rally::Grade::BoostPower(Panels->GetBoostTenths(TurboSlot));
        Modules.BoostBar = Panels->GetBoostTenths(TurboSlot) * 0.1;
        const int32 DeliveredTenths = FMath::FloorToInt(Rally::Engine::EffectiveBoost(Modules) * 10.0 + 0.0001);
        Modules.KnockPerSecond = Rally::Grade::KnockWear(DeliveredTenths, Chip, 1.0, Panels->GetEngineMap(EcuSlot));
        Modules.FuelScale = Map.Fuel;
        Engine->SetModules(Modules);
        {
            FScopeLock Lock(&PhysicsLock);
            PhysicsIdleDrop = Rally::Engine::IdleDrop(Modules);
        }
        UpdateCoilLooks();
    }
    UpdateLights();
    // Torn from its rear hangers, the exhaust hangs from the front one with its silencer on the ground.
    FRallyPartCondition Exhaust;
    Damage->GetPartCondition(ERallyPart::Exhaust, Exhaust);
    ExhaustMesh->SetRelativeRotation(FRotator(Rally::Underside::ExhaustHanging(Exhaust.Health) ? 4.0 : 0.0, 0.0, 0.0));
    ApplySetup();
}

void ARallyCarPawn::OnFixingLoose(int32 Slot, bool bNoneLeft)
{
    EventSound->Trigger({ bNoneLeft ? Rally::Audio::EEvent::PartDrop : Rally::Audio::EEvent::BoltLoose, 1.0f });
    if (bKnockTest)
    {
        ++KnockTestLoose;
        bKnockTestNoneLeft = bKnockTestNoneLeft || bNoneLeft;
    }
}

void ARallyCarPawn::OnStartFailed(ERallyStartFailure Reason)
{
    Rally::Audio::FEventRequest Request;
    Request.Kind = Reason == ERallyStartFailure::NoPower || Reason == ERallyStartFailure::NoStarter ? Rally::Audio::EEvent::StarterClick
        : Reason == ERallyStartFailure::Seized ? Rally::Audio::EEvent::Seized : Rally::Audio::EEvent::CrankNoStart;
    EventSound->Trigger(Request);
}

void ARallyCarPawn::MulticastCrashSound_Implementation(uint8 Kind, uint8 Strength)
{
    // The machine that felt it played it straight away.
    if (!bSimulatesHere)
    {
        PlayCrashSound(Kind, Strength / 8.0f);
    }
}

void ARallyCarPawn::PlayCrashSound(uint8 Kind, float DeltaV)
{
    Rally::Audio::FEventRequest Request;
    Request.Kind = Kind == 0 ? Rally::Audio::EEvent::Impact : Rally::Audio::EEvent::Underside;
    Request.Strength = Kind == 0 ? FMath::Clamp((DeltaV - 1.5f) / 14.0f, 0.08f, 1.0f) : FMath::Clamp(DeltaV / 2.0f, 0.1f, 1.0f);
    EventSound->Trigger(Request);
}

void ARallyCarPawn::UpdateSounds(float DeltaSeconds, const FVisualState& Snapshot)
{
    if (!EngineSound->IsPlaying())
    {
        return;
    }
    using namespace Rally::Audio;
    // Listening from inside this car: the driver in the cockpit view, or someone in a seat.
    const APlayerController* LocalPlayer = GetWorld()->GetFirstPlayerController();
    const APawn* Viewer = LocalPlayer ? LocalPlayer->GetPawn() : nullptr;
    bool bInside = Viewer == this && bCockpitView;
    if (const ARallyCharacter* Seated = Cast<ARallyCharacter>(Viewer))
    {
        bInside = Seated->GetSeatedCar() == this;
    }

    FRallyPartCondition Exhaust, Battery;
    Damage->GetPartCondition(ERallyPart::Exhaust, Exhaust);
    Damage->GetPartCondition(ERallyPart::Battery, Battery);
    const bool bExhaustOff = Panels->IsRemoved(URallyPanelComponent::ExhaustSlot);
    const bool bExhaustHanging = Rally::Underside::ExhaustHanging(Exhaust.Health) && !bExhaustOff;
    FEngineParams EngineParams;
    EngineParams.Rpm = static_cast<float>(Snapshot.EngineRpm);
    EngineParams.Load = static_cast<float>(Snapshot.EngineLoad);
    EngineParams.bRunning = Engine->IsRunning();
    EngineParams.EngineHealth = static_cast<float>(Engine->GetEngineHealth());
    // A dented exhaust blows a little at its joints; one hanging off its hangers blows badly.
    EngineParams.ExhaustLeak = bExhaustHanging ? 0.9f : 0.5f * (1.0f - Exhaust.Health);
    EngineParams.bExhaustOff = bExhaustOff;
    const Rally::Engine::FModules& Modules = Engine->GetModules();
    EngineParams.ManifoldLeak = static_cast<float>(1.0 - Rally::Engine::Clamp01(Modules.ExhaustManifold));
    EngineParams.TurboDelivery = static_cast<float>(Rally::Engine::BoostDelivery(Modules));
    EngineParams.IntakeOpen = static_cast<float>(1.0 - Rally::Engine::Clamp01(Modules.AirFilter));
    // The driving machine knows its cut-outs; everyone else hears the same faults at random.
    EngineParams.bCutOut = Snapshot.bCutOut;
    // Coils: every firing a dead or misfiring one misses pops, on every machine.
    for (int32 C = 0; C < Rally::Engine::Cylinders; ++C)
        EngineParams.CylinderMisfire[C] = static_cast<float>(Rally::Engine::CoilMisfire(
            Modules.Coils[C], Rally::Engine::EffectiveBoost(Modules), Modules.CoilBoostLimit[C], Snapshot.EngineLoad));
    EngineParams.MisfireRate = bSimulatesHere ? 0.0f : static_cast<float>(Rally::Engine::CutOutRate(Battery.Health));
    EngineParams.Interior = bInside ? 1.0f : 0.0f;
    // Boost past what the computer allows pinks under load; loose parts rattle.
    EngineParams.Detonation = Engine->IsRunning() ? static_cast<float>(Rally::Engine::Detonation(Engine->GetModules().KnockPerSecond)) : 0.0f;
    EngineParams.Rattle = static_cast<float>(Rally::Fixing::RattleLevel(Panels->GetLoosestShare(), Engine->IsRunning(), GetForwardSpeedKmh()));
    EngineSound->SetEngine(EngineParams);

    // An engine stopping shudders as it comes to rest.
    if (bSoundWasRunning && !EngineParams.bRunning && Snapshot.EngineRpm > 300.0)
    {
        EventSound->Trigger({ EEvent::Shudder, 0.6f });
    }
    bSoundWasRunning = EngineParams.bRunning;
    // A clunk as each gear goes in.
    if (Snapshot.Gear != LastSoundGear)
    {
        if (Snapshot.Gear != 0 && EngineParams.bRunning) EventSound->Trigger({ EEvent::GearShift, 0.6f });
        LastSoundGear = Snapshot.Gear;
    }

    // The surface under the car, a few times a second.
    SurfaceTraceTimer -= DeltaSeconds;
    if (SurfaceTraceTimer <= 0.0f)
    {
        SurfaceTraceTimer = 0.25f;
        FHitResult Ground;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(RallySoundSurface), false, this);
        Params.bReturnPhysicalMaterial = true;
        const FVector From = GetActorLocation();
        if (GetWorld()->LineTraceSingleByChannel(Ground, From, From - GetActorUpVector() * 250.0, ECC_Visibility, Params))
        {
            const URallyPhysicalMaterial* Material = Cast<URallyPhysicalMaterial>(Ground.PhysMaterial.Get());
            SurfaceLooseness = Material ? Material->Looseness : 0.0f;
        }
    }
    // Sliding: each wheel's spin against the ground and the body's drift, as a share of the tyre's peak.
    const FVector Velocity = GetVelocity();
    const double Speed = Velocity.Size() * 0.01;
    const double Forward = FVector::DotProduct(Velocity, GetActorForwardVector()) * 0.01;
    const double Sideways = FVector::DotProduct(Velocity, GetActorRightVector()) * 0.01;
    double Slide = Speed > 2.0 ? FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Sideways), FMath::Abs(Forward))) / 8.0 : 0.0;
    int32 Touching = 0;
    float Scrape = bExhaustHanging ? 1.0f : 0.0f;
    for (int32 I = 0; I < FMath::Min(Snapshot.Wheels.Num(), WheelSetups.Num()); ++I)
    {
        const FWheelState& Wheel = Snapshot.Wheels[I];
        Touching += Wheel.bContact ? 1 : 0;
        const double RimSpeed = Wheel.Omega * WheelSetups[I].Radius * 0.01;
        if (Wheel.bContact)
        {
            Slide = FMath::Max(Slide, FMath::Abs(RimSpeed - Forward) / FMath::Max(FMath::Abs(Forward), 3.0) / 0.12);
        }
        FRallyPartCondition Hub;
        Damage->GetPartCondition(HubParts[I], Hub);
        if (Hub.Health <= 0.0f) Scrape = 1.0f;
    }
    FRoadParams RoadParams;
    RoadParams.SpeedMs = static_cast<float>(Speed);
    RoadParams.Contact = Snapshot.Wheels.Num() ? static_cast<float>(Touching) / Snapshot.Wheels.Num() : 0.0f;
    RoadParams.Loose = SurfaceLooseness;
    RoadParams.Slide = static_cast<float>(Slide);
    RoadParams.Scrape = Scrape;
    RoadParams.Interior = bInside ? 1.0f : 0.0f;
    RoadSound->SetRoad(RoadParams);
}

void ARallyCarPawn::OnEngineChanged()
{
    FScopeLock Lock(&PhysicsLock);
    bPhysicsEngineRunning = Engine->IsRunning();
    PhysicsTorqueScale = Engine->GetPowerScale();
    PhysicsBatteryHealth = Engine->GetBatteryHealth();
}

FString ARallyCarPawn::DescribeDamage() const
{
    static const TCHAR* Corners[] = { TEXT("FL"), TEXT("FR"), TEXT("RL"), TEXT("RR") };
    TArray<FString> Notes;
    for (int32 I = 0; I < 4; ++I)
    {
        FRallyPartCondition Tyre, Suspension;
        Damage->GetPartCondition(TyreParts[I], Tyre);
        Damage->GetPartCondition(SuspensionParts[I], Suspension);
        FRallyPartCondition Hub;
        Damage->GetPartCondition(HubParts[I], Hub);
        if (Hub.Health <= 0.0f)
        {
            Notes.Add(FString::Printf(TEXT("%s wheel off"), Corners[I]));
        }
        else if (Tyre.Health <= 0.0f)
        {
            Notes.Add(FString::Printf(TEXT("%s tyre flat"), Corners[I]));
        }
        else if (Tyre.Health < 1.0f)
        {
            Notes.Add(FString::Printf(TEXT("%s tyre %.0f%%"), Corners[I], Tyre.Health * 100.0f));
        }
        if (Suspension.Health < 1.0f)
        {
            Notes.Add(Suspension.Health <= 0.0f ? FString::Printf(TEXT("%s arm snapped%s"), Corners[I], I < 2 ? TEXT(" (no steering)") : TEXT(""))
                : FString::Printf(TEXT("%s arm %.0f%% (toe %.1f deg)"), Corners[I], Suspension.Health * 100.0f, Rally::Impact::ToeOutDeg(Suspension.Health)));
        }
        const auto Note = [&](ERallyPart Part, const TCHAR* Name)
        {
            FRallyPartCondition Condition;
            Damage->GetPartCondition(Part, Condition);
            if (Condition.Health <= 0.0f) Notes.Add(FString::Printf(TEXT("%s %s broken"), Corners[I], Name));
            else if (Condition.Health < 1.0f) Notes.Add(FString::Printf(TEXT("%s %s %.0f%%"), Corners[I], Name, Condition.Health * 100.0f));
        };
        Note(StrutParts[I], TEXT("strut"));
        Note(BrakeParts[I], TEXT("brake"));
        if (I < 2)
        {
            Note(DriveshaftParts[I], TEXT("driveshaft"));
        }
    }
    FRallyPartCondition Radiator;
    Damage->GetPartCondition(ERallyPart::Radiator, Radiator);
    if (Radiator.Health < 1.0f)
    {
        Notes.Add(FString::Printf(TEXT("radiator %.0f%%%s"), Radiator.Health * 100.0f, Radiator.bBodged ? TEXT(" (sealed)") : TEXT("")));
    }
    for (const ERallyPart Part : { ERallyPart::Engine, ERallyPart::Battery })
    {
        FRallyPartCondition Condition;
        Damage->GetPartCondition(Part, Condition);
        const TCHAR* Name = Part == ERallyPart::Engine ? TEXT("engine") : TEXT("battery");
        if (Condition.Health <= 0.0f) Notes.Add(FString::Printf(TEXT("%s %s"), Name, Part == ERallyPart::Engine ? TEXT("seized") : TEXT("dead")));
        else if (Condition.Health < 1.0f) Notes.Add(FString::Printf(TEXT("%s %.0f%%%s"), Name, Condition.Health * 100.0f, Condition.bBodged ? TEXT(" (bodged)") : TEXT("")));
    }
    // Engine modules.
    for (int32 M = 0; M < URallyPanelComponent::NumModules; ++M)
    {
        const int32 Slot = URallyPanelComponent::ModuleSlot(static_cast<URallyPanelComponent::EModule>(M));
        if (Panels->IsRemoved(Slot)) { Notes.Add(URallyPanelComponent::PanelName(Slot) + TEXT(" off")); continue; }
        const ERallyPart Part = URallyPanelComponent::PartOfModule(Slot);
        FRallyPartCondition Condition;
        if (Part == ERallyPart::Count || !Damage->GetPartCondition(Part, Condition) || Condition.Health >= 1.0f) continue;
        Notes.Add(FString::Printf(TEXT("%s %.0f%%"), *URallyPanelComponent::PanelName(Slot), Condition.Health * 100.0f));
    }
    // Under the floor.
    for (const ERallyPart Part : { ERallyPart::OilSump, ERallyPart::FuelTank, ERallyPart::Exhaust, ERallyPart::Gearbox })
    {
        FRallyPartCondition Condition;
        Damage->GetPartCondition(Part, Condition);
        const TCHAR* Name = Part == ERallyPart::OilSump ? TEXT("sump") : Part == ERallyPart::FuelTank ? TEXT("fuel tank")
            : Part == ERallyPart::Exhaust ? TEXT("exhaust") : TEXT("gearbox");
        if (Part == ERallyPart::Exhaust && Panels->IsRemoved(URallyPanelComponent::ExhaustSlot))
        {
            Notes.Add(TEXT("exhaust off"));
            continue;
        }
        if (Condition.Health >= 1.0f) continue;
        FString Note = FString::Printf(TEXT("%s %.0f%%%s"), Name, Condition.Health * 100.0f, Condition.bBodged ? TEXT(" (bodged)") : TEXT(""));
        if (Part == ERallyPart::Exhaust && Rally::Underside::ExhaustHanging(Condition.Health)) Note += TEXT(" hanging");
        if (Part == ERallyPart::Gearbox)
        {
            FString Lost;
            for (int32 Gear = 5; Gear >= 1; --Gear)
                if (!Rally::Underside::HasGear(Gear, Condition.Health)) Lost += FString::Printf(TEXT("%s%d"), Lost.IsEmpty() ? TEXT("") : TEXT(","), Gear);
            if (!Lost.IsEmpty()) Note += FString::Printf(TEXT(" (no %s)"), *Lost);
        }
        Notes.Add(Note);
    }
    return FString::Join(Notes, TEXT(", "));
}

void ARallyCarPawn::OnDebugDamage(const FInputActionValue& Value)
{
    ServerDebugDamage(false);
}

void ARallyCarPawn::OnDebugRepair(const FInputActionValue& Value)
{
    ServerDebugDamage(true);
}

void ARallyCarPawn::ServerDebugDamage_Implementation(bool bRepair)
{
#if !UE_BUILD_SHIPPING
    // F8 flattens the front-left tyre; F9 repairs everything and straightens the body.
    if (!bRepair)
    {
        Damage->ApplyPartDamage(ERallyPart::TyreFrontLeft, 1.0f);
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_DAMAGE debug puncture FL"));
        return;
    }
    for (int32 Part = 0; Part < static_cast<int32>(ERallyPart::Count); ++Part)
    {
        Damage->CompleteValidatedRepair(static_cast<ERallyPart>(Part), true);
    }
    Panels->RepairAll();
    BodyDeform->ClearDents();
    Engine->Reset();
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_DAMAGE debug repair"));
#endif
}

void ARallyCarPawn::TickLeakTest(float DeltaSeconds)
{
    using Rally::Leak::ELiquid;
    LeakTestTime += DeltaSeconds;
    ThrottleInput = BrakeInput = SteerTarget = 0.0f;
    bHandbrake = true;
    const float T = LeakTestTime;
    const ARallyPuddleField* Field = ARallyPuddleField::Get(GetWorld());
    const auto Report = [&](const TCHAR* Label)
    {
        double Litres[3] = { 0.0, 0.0, 0.0 };
        int32 Count[3] = { 0, 0, 0 };
        for (int32 L = 0; L < 3; ++L) Count[L] = Field ? Field->CountPuddles(static_cast<ELiquid>(L), &Litres[L]) : 0;
        return FString::Printf(TEXT("%s: oil lost %.2f L in %d puddles %.2f L; coolant lost %.2f L in %d puddles %.2f L; fuel lost %.2f L in %d puddles %.2f L; block soaked %.2f; drops falling %d"),
            Label, Leaks->GetLeakedL(ELiquid::Oil), Count[0], Litres[0], Leaks->GetLeakedL(ELiquid::Coolant), Count[1], Litres[1],
            Leaks->GetLeakedL(ELiquid::Fuel), Count[2], Litres[2], Leaks->GetOilSoak(), Leaks->GetDropsInAir());
    };
    switch (LeakTestStep)
    {
    case 0:
        if (T < 2.5f) return;
        // A cracked sump, a holed radiator and tank, the engine idling.
        if (HasAuthority())
        {
            Damage->DebugSetHealth(ERallyPart::OilSump, 0.2f);
            Damage->DebugSetHealth(ERallyPart::Radiator, 0.3f);
            Damage->DebugSetHealth(ERallyPart::FuelTank, 0.35f);
        }
        LeakTestStep = 1;
        LeakTestTime = 0.0f;
        break;
    case 1:
        if (T < 25.0f) return;
        LeakTestParked = Report(TEXT("parked 25 s"));
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_LEAKTEST %s"), *LeakTestParked);
        // From low in front and to the left, the puddles under the front of the car.
        SetCockpitView(false);
        CameraArm->bDoCollisionTest = false;
        CameraArm->bEnableCameraLag = CameraArm->bEnableCameraRotationLag = false;
        CameraArm->SetRelativeLocation(AssemblyOffset + FVector(150.0, 0.0, 25.0));
        CameraArm->SetRelativeRotation(FRotator(-8.0, -150.0, 0.0));
        CameraArm->TargetArmLength = 170.0f;
        LeakTestStep = 2;
        LeakTestTime = 0.0f;
        break;
    case 2:
        if (T < 0.6f) return;
        FScreenshotRequest::RequestScreenshot(TEXT("RallyLeak_parked.png"), false, false);
        LeakTestStep = 3;
        LeakTestTime = 0.0f;
        break;
    case 3:
        // From the ground in front of the car, looking back under it: the oily block and sump over the
        // puddles.
        if (T < 0.5f) return;
        CameraArm->SetRelativeLocation(AssemblyOffset + FVector(150.0, 0.0, 12.0));
        CameraArm->SetRelativeRotation(FRotator(3.0, 180.0, 0.0));
        CameraArm->TargetArmLength = 150.0f;
        LeakTestStep = 4;
        LeakTestTime = 0.0f;
        break;
    case 4:
        if (T < 0.6f) return;
        FScreenshotRequest::RequestScreenshot(TEXT("RallyLeak_block.png"), false, false);
        // Then the bonnet up, from in front and above: the block's sides under the head.
        if (HasAuthority()) Panels->DebugSetOpen(4, true);
        CameraArm->SetRelativeLocation(AssemblyOffset + FVector(150.0, 0.0, 60.0));
        CameraArm->SetRelativeRotation(FRotator(-55.0, 180.0, 0.0));
        CameraArm->TargetArmLength = 120.0f;
        LeakTestStep = 40;
        LeakTestTime = 0.0f;
        break;
    case 40:
        if (T < 1.5f) return;
        FScreenshotRequest::RequestScreenshot(TEXT("RallyLeak_bay.png"), false, false);
        LeakTestStep = 41;
        LeakTestTime = 0.0f;
        break;
    case 41:
        if (T < 0.3f) return;
        if (HasAuthority()) Panels->DebugSetOpen(4, false);
        LeakTestStep = 5;
        LeakTestTime = 0.0f;
        break;
    case 5:
        // Off slowly for 12 s, leaving a trail.
        bHandbrake = false;
        ThrottleInput = GetForwardSpeedKmh() < 15.0f ? 0.35f : 0.0f;
        if (T < 12.0f) return;
        LeakTestStep = 6;
        LeakTestTime = 0.0f;
        break;
    case 6:
        // Stopped on the handbrake (the brake pedal near a standstill would put the automatic in reverse).
        bHandbrake = true;
        if (T < 3.0f) return;
        // Looking back along the road at the trail, from above the boot.
        CameraArm->SetRelativeLocation(AssemblyOffset + FVector(-260.0, 0.0, 190.0));
        CameraArm->SetRelativeRotation(FRotator(-40.0, 180.0, 0.0));
        CameraArm->TargetArmLength = 10.0f;
        LeakTestStep = 7;
        LeakTestTime = 0.0f;
        break;
    case 7:
        if (T < 0.6f) return;
        FScreenshotRequest::RequestScreenshot(TEXT("RallyLeak_trail.png"), false, false);
        LeakTestStep = 8;
        LeakTestTime = 0.0f;
        break;
    case 8:
    {
        if (T < 0.5f) return;
        const FString Driven = Report(TEXT("after the drive"));
        // What drained out lies on the ground: oil and coolant puddles holding most of what was lost (some
        // is still falling, coolant dries a little), fuel puddles drying fast; the block soaked; a trail of
        // more oil spots than when parked.
        double Oil = 0.0, Coolant = 0.0;
        const int32 OilPuddles = Field ? Field->CountPuddles(ELiquid::Oil, &Oil) : 0;
        const int32 CoolantPuddles = Field ? Field->CountPuddles(ELiquid::Coolant, &Coolant) : 0;
        const int32 FuelPuddles = Field ? Field->CountPuddles(ELiquid::Fuel) : 0;
        const bool bPass = Oil > 0.7 * Leaks->GetLeakedL(ELiquid::Oil) && Coolant > 0.5 * Leaks->GetLeakedL(ELiquid::Coolant)
            && OilPuddles > 3 && CoolantPuddles > 3 && FuelPuddles > 0 && Leaks->GetOilSoak() > 0.3;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_LEAKTEST %s"), *Driven);
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_LEAKTEST_DONE result=%s"), bPass ? TEXT("pass") : TEXT("FAIL"));
        bAutoDrive = false;
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyLeakTest"));
        LeakTestStep = 9;
        break;
    }
    default:
        break;
    }
}

void ARallyCarPawn::TickUndersideTest(float DeltaSeconds)
{
    // A ridge of rock rises between the wheels 20 m ahead: 1.6 m long, 50 cm wide, climbing from the ground
    // to 33 cm and then dropping off. The car drives straight over it at about 35 km/h, stops, and idles
    // for 20 s to show what leaks.
    UndersideTestTime += DeltaSeconds;
    ThrottleInput = BrakeInput = SteerTarget = 0.0f;
    bHandbrake = false;
    const float T = UndersideTestTime;
    const FVector Forward = GetActorForwardVector();
    switch (UndersideTestStep)
    {
    case 0:
    {
        if (T < 2.5f) return;
        const FVector Ahead = GetActorLocation() + Forward * 2000.0;
        FHitResult Ground;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyUndersideTest), false, this);
        const double GroundZ = GetWorld()->LineTraceSingleByChannel(Ground, Ahead + FVector(0, 0, 300), Ahead - FVector(0, 0, 500), ECC_Visibility, Params)
            ? Ground.ImpactPoint.Z : Ahead.Z - 80.0;
        UndersideRockAt = FVector(Ahead.X, Ahead.Y, GroundZ + 6.8);
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AStaticMeshActor* Ridge = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), UndersideRockAt,
            FRotator(12.0, GetActorRotation().Yaw, 0.0), Spawn);
        if (Ridge)
        {
            Ridge->SetMobility(EComponentMobility::Movable);
            Ridge->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
            Ridge->SetActorScale3D(FVector(1.6, 0.5, 0.2));
        }
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_UNDERSIDETEST ridge at=%s spawned=%d"), *UndersideRockAt.ToCompactString(), Ridge ? 1 : 0);
        if (FParse::Param(FCommandLine::Get(), TEXT("RallyRecordAudio")))
        {
            UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 60.0f);
        }
        UndersideTestStep = 1;
        break;
    }
    case 1:
        // Hold about 35 km/h straight on until well past it.
        ThrottleInput = GetForwardSpeedKmh() < 35.0f ? 0.7f : 0.0f;
        if (FVector::DotProduct(GetActorLocation() - UndersideRockAt, Forward) > 600.0 || T > 20.0f)
        {
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_UNDERSIDETEST past kmh=%.0f hits=%d max_dv=%.2f state=[%s]"), GetForwardSpeedKmh(),
                UndersideHits, UndersideMaxDeltaV, *DescribeDamage());
            UndersideTestStep = 2;
        }
        break;
    case 2:
        // Brake to a stop, then the handbrake (the brake held at a standstill would select reverse).
        if (FMath::Abs(GetForwardSpeedKmh()) > 2.0f)
        {
            BrakeInput = 1.0f;
            break;
        }
        bHandbrake = true;
        UndersideTestMark = T;
        UndersideOilAtStop = Engine->GetOilL();
        UndersideFuelAtStop = Engine->GetFuelL();
        UndersideTestStep = 3;
        break;
    default:
    {
        bHandbrake = true;
        const float Idle = T - UndersideTestMark;
        if (Idle < 20.0f)
        {
            if (FMath::FloorToInt(Idle / 5.0f) != FMath::FloorToInt((Idle - DeltaSeconds) / 5.0f))
            {
                UE_LOG(LogRallyCar, Display, TEXT("RALLY_UNDERSIDETEST idle t=%.0f %s puffs=%d"), Idle, *Engine->Describe(), Puffs->GetInstanceCount());
            }
            if (Idle > 3.0f && UndersideTestStep == 3)
            {
                UndersideTestStep = 4;
                CameraArm->SetRelativeLocation(FVector(0.0, 0.0, 60.0));
                CameraArm->SetRelativeRotation(FRotator(-10.0, -60.0, 0.0));
                CameraArm->TargetArmLength = 520.0f;
                SetCockpitView(false);
            }
            else if (Idle > 5.0f && UndersideTestStep == 4)
            {
                UndersideTestStep = 5;
                FScreenshotRequest::RequestScreenshot(TEXT("RallyUnderside_after.png"), false, false);
            }
            else if (Idle > 17.0f && UndersideTestStep == 5)
            {
                // What has dripped out by now, from low beside the car.
                UndersideTestStep = 6;
                CameraArm->SetRelativeLocation(AssemblyOffset + FVector(60.0, 0.0, 10.0));
                CameraArm->SetRelativeRotation(FRotator(-28.0, -100.0, 0.0));
                CameraArm->TargetArmLength = 260.0f;
            }
            else if (Idle > 18.5f && UndersideTestStep == 6 && !bUndersidePuddleShot)
            {
                bUndersidePuddleShot = true;
                FScreenshotRequest::RequestScreenshot(TEXT("RallyUnderside_puddles.png"), false, false);
            }
            break;
        }
        FRallyPartCondition Sump, Tank, Exhaust, Gearbox;
        Damage->GetPartCondition(ERallyPart::OilSump, Sump);
        Damage->GetPartCondition(ERallyPart::FuelTank, Tank);
        Damage->GetPartCondition(ERallyPart::Exhaust, Exhaust);
        Damage->GetPartCondition(ERallyPart::Gearbox, Gearbox);
        const bool bDamaged = Sump.Health < 1.0f || Tank.Health < 1.0f || Exhaust.Health < 1.0f || Gearbox.Health < 1.0f;
        // A leak shows: what drained out lies on the ground as puddles of that liquid (fuel dries as it goes).
        using Rally::Leak::ELiquid;
        double OilPuddleL = 0.0, FuelPuddleL = 0.0;
        const ARallyPuddleField* Field = ARallyPuddleField::Get(GetWorld());
        const int32 OilPuddles = Field ? Field->CountPuddles(ELiquid::Oil, &OilPuddleL) : 0;
        const int32 FuelPuddles = Field ? Field->CountPuddles(ELiquid::Fuel, &FuelPuddleL) : 0;
        const double OilLost = Leaks->GetLeakedL(ELiquid::Oil), FuelLost = Leaks->GetLeakedL(ELiquid::Fuel);
        const bool bOilShows = OilLost < 0.01 || (OilPuddles > 0 && OilPuddleL > 0.5 * OilLost);
        const bool bFuelShows = FuelLost < 0.01 || FuelPuddles > 0;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_UNDERSIDETEST leaks oil lost %.2f L, %d puddles %.2f L; fuel lost %.2f L, %d puddles %.2f L; block soaked %.2f"),
            OilLost, OilPuddles, OilPuddleL, FuelLost, FuelPuddles, FuelPuddleL, Leaks->GetOilSoak());
        const bool bPass = UndersideHits > 0 && bDamaged && BodyDeform->GetDentCount() == 0 && bOilShows && bFuelShows;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_UNDERSIDETEST_DONE result=%s hits=%d max_dv=%.2f dents=%d oil %.1f->%.1f L fuel %.1f->%.1f L engine=[%s] state=[%s]"),
            bPass ? TEXT("pass") : TEXT("fail"), UndersideHits, UndersideMaxDeltaV, BodyDeform->GetDentCount(), UndersideOilAtStop, Engine->GetOilL(),
            UndersideFuelAtStop, Engine->GetFuelL(), *Engine->Describe(), *DescribeDamage());
        if (FParse::Param(FCommandLine::Get(), TEXT("RallyRecordAudio")))
        {
            UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, TEXT("RallyUnderside_audio"), FString());
        }
        bAutoDrive = false;
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyUndersideTest"));
        break;
    }
    }
}

void ARallyCarPawn::TickEngineTest(float Since)
{
    // -RallyEngineTest: drive circles on the radiator the barrier crushed until the coolant has gone and
    // the engine has overheated and started to wear, photographing the steam; then stop, switch off, let it
    // stand and pour in five litres of cold water.
    if (EngineTestStart < 0.0f)
    {
        EngineTestStart = CrashTestTime;
        CameraArm->SetRelativeLocation(FVector(0.0, 0.0, 60.0));
        CameraArm->SetRelativeRotation(FRotator(-15.0, 140.0, 0.0));
        CameraArm->TargetArmLength = 600.0f;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_ENGINETEST start engine=[%s] state=[%s]"), *Engine->Describe(), *DescribeDamage());
    }
    const float T = CrashTestTime - EngineTestStart;
    FVisualState Snapshot;
    {
        FScopeLock Lock(&PhysicsLock);
        Snapshot = Visual;
    }
    EngineTestMaxTemp = FMath::Max(EngineTestMaxTemp, Engine->GetTempC());
    EngineTestMinCoolant = FMath::Min(EngineTestMinCoolant, Engine->GetCoolantL());
    EngineTestMaxPuffs = FMath::Max(EngineTestMaxPuffs, Puffs->GetInstanceCount());
    if (T >= NextEngineTestLog)
    {
        NextEngineTestLog = T + 5.0f;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_ENGINETEST t=%.0f kmh=%.0f rpm=%.0f kw=%.0f %s engine=%.2f running=%d puffs=%d at=%s"),
            T, GetForwardSpeedKmh(), Snapshot.EngineRpm, Snapshot.EnginePowerKw, *Engine->Describe(), Engine->GetEngineHealth(),
            Engine->IsRunning() ? 1 : 0, Puffs->GetInstanceCount(), *GetActorLocation().ToCompactString());
    }
    if (EngineTestStopped < 0.0f)
    {
        // Steady circles, well clear of the barrier behind.
        ThrottleInput = 0.5f;
        SteerTarget = 1.0f;
        if (T > 12.0f && EngineTestStep == 0)
        {
            EngineTestStep = 1;
            FScreenshotRequest::RequestScreenshot(TEXT("RallyEngine_steam.png"), false, false);
        }
        if (Engine->GetEngineHealth() < 0.85 || !Engine->IsRunning() || T > 170.0f)
        {
            EngineTestStopped = T;
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_ENGINETEST stopping t=%.0f %s engine=%.2f"), T, *Engine->Describe(), Engine->GetEngineHealth());
        }
        return;
    }
    const float Stopped = T - EngineTestStopped;
    // Brake to a stop, then the handbrake (the brake held at a standstill would select reverse).
    if (FMath::Abs(GetForwardSpeedKmh()) > 3.0f && Stopped < 6.0f)
    {
        BrakeInput = 1.0f;
    }
    else
    {
        bHandbrake = true;
    }
    if (Stopped > 5.0f && EngineTestStep <= 1)
    {
        EngineTestStep = 2;
        if (Engine->IsRunning() && HasAuthority()) Engine->ToggleIgnition();
        EngineTestTempAtOff = Engine->GetTempC();
        CameraArm->SetRelativeRotation(FRotator(-25.0, 160.0, 0.0));
        CameraArm->TargetArmLength = 380.0f;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_ENGINETEST ignition off running=%d %s"), Engine->IsRunning() ? 1 : 0, *Engine->Describe());
    }
    else if (Stopped > 7.0f && EngineTestStep == 2)
    {
        EngineTestStep = 3;
        FScreenshotRequest::RequestScreenshot(TEXT("RallyEngine_stopped.png"), false, false);
    }
    else if (Stopped > 25.0f && EngineTestStep == 3)
    {
        EngineTestStep = 4;
        const double Before = Engine->GetTempC();
        const double Poured = HasAuthority() ? Engine->TopUp(5.0) : 0.0;
        const bool bCooledStanding = Before < EngineTestTempAtOff;
        const bool bPass = EngineTestMinCoolant <= 0.0 && EngineTestMaxTemp > Rally::Engine::WearStartC && Engine->GetEngineHealth() < 1.0
            && EngineTestMaxPuffs > 0 && !Engine->IsRunning() && bCooledStanding && Poured > 4.9 && Engine->GetTempC() < Before;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_ENGINETEST_DONE result=%s min_coolant=%.1f max_temp=%.0f engine=%.2f max_puffs=%d temp_at_off=%.0f before_topup=%.0f poured=%.1f after=[%s] state=[%s]"),
            bPass ? TEXT("pass") : TEXT("fail"), EngineTestMinCoolant, EngineTestMaxTemp, Engine->GetEngineHealth(), EngineTestMaxPuffs,
            EngineTestTempAtOff, Before, Poured, *Engine->Describe(), *DescribeDamage());
        bAutoDrive = false;
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyEngineTest"));
    }
}

void ARallyCarPawn::TickKeyTest(float DeltaSeconds)
{
    const APlayerController* PC = Cast<APlayerController>(GetController());
    UEnhancedInputLocalPlayerSubsystem* Input = PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
    if (!Input || !ShiftUpAction) return;
    KeyTestTime = KeyTestTime < 0.0f ? 0.0f : KeyTestTime + DeltaSeconds;
    // One press every half second: up, up, down, down, down, down (to reverse, standing), up; then the
    // clutch held. Each step injects the key for one frame, as a tap.
    struct FPress { const UInputAction* Action; int8 Expect; };
    const FPress Presses[] = { { ShiftUpAction, 2 }, { ShiftUpAction, 3 }, { ShiftDownAction, 2 }, { ShiftDownAction, 1 },
        { ShiftDownAction, 0 }, { ShiftDownAction, -1 }, { ShiftUpAction, 0 } };
    const int32 Count = UE_ARRAY_COUNT(Presses);
    if (KeyTestTime < 1.0f + 0.5f * KeyTestStep) return;
    if (KeyTestStep < Count * 2)
    {
        const FPress& Press = Presses[KeyTestStep / 2];
        if (KeyTestStep % 2 == 0) Input->InjectInputForAction(Press.Action, FInputActionValue(true), {}, {});
        else KeyTestGears += FString::Printf(TEXT("%d%s "), SelectedGear, SelectedGear == Press.Expect ? TEXT("") : TEXT("!"));
        ++KeyTestStep;
        return;
    }
    if (KeyTestStep == Count * 2)
    {
        // The clutch held for a moment: the pedal goes down.
        Input->InjectInputForAction(ClutchAction, FInputActionValue(true), {}, {});
        if (ClutchPedal < 0.99f && KeyTestTime < 1.0f + 0.5f * KeyTestStep + 1.0f) return;
        KeyTestClutch = ClutchPedal;
        ++KeyTestStep;
        SelectedGear = 2;
        KeyTestMark = KeyTestTime;
        // The key turned, as a player would on getting in.
        if (!Engine->IsRunning()) ServerToggleIgnition();
        return;
    }
    if (KeyTestStep == Count * 2 + 1)
    {
        // Flat out in second to 80 km/h; then a press down, which would spin the engine to about 8 200 rpm
        // in first, must not go in.
        ThrottleInput = 1.0f;
        if ((GetForwardSpeedKmh() < 80.0f || !Engine->IsRunning()) && KeyTestTime - KeyTestMark < 25.0f) return;
        Input->InjectInputForAction(ShiftDownAction, FInputActionValue(true), {}, {});
        KeyTestSpeed = GetForwardSpeedKmh();
        KeyTestMark = KeyTestTime;
        ++KeyTestStep;
        return;
    }
    if (KeyTestStep == Count * 2 + 2)
    {
        ThrottleInput = 1.0f;
        if (KeyTestTime - KeyTestMark < 0.5f) return;
        KeyTestGears += FString::Printf(TEXT("at %.0f km/h down: %d%s "), KeyTestSpeed, SelectedGear, SelectedGear == 2 ? TEXT("") : TEXT("!"));
        // And up into third, which goes.
        Input->InjectInputForAction(ShiftUpAction, FInputActionValue(true), {}, {});
        KeyTestMark = KeyTestTime;
        ++KeyTestStep;
        return;
    }
    if (KeyTestStep == Count * 2 + 3)
    {
        ThrottleInput = 0.0f;
        BrakeInput = 1.0f;
        if (KeyTestTime - KeyTestMark < 0.5f) return;
        KeyTestGears += FString::Printf(TEXT("up: %d%s"), SelectedGear, SelectedGear == 3 ? TEXT("") : TEXT("!"));
        const bool bPass = !KeyTestGears.Contains(TEXT("!")) && KeyTestClutch > 0.95f && KeyTestSpeed >= 75.0f;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_KEYTEST_DONE result=%s gears=[%s] clutch=%.2f"), bPass ? TEXT("pass") : TEXT("fail"), *KeyTestGears, KeyTestClutch);
        ++KeyTestStep;
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyKeyTest"));
    }
}

void ARallyCarPawn::TickBoltSurvey()
{
    ThrottleInput = BrakeInput = SteerTarget = 0.0f;
    bHandbrake = true;
    if (BoltSurveySlot < 0)
    {
        if (PartsTestTime < 3.0f) return;
        // Everything that opens, open; the wheels out of the way of what is behind them.
        if (HasAuthority())
            for (int32 Panel = 0; Panel < 6; ++Panel) Panels->DebugSetOpen(Panel, true);
        SetCockpitView(false);
        CameraArm->bDoCollisionTest = false;
        CameraArm->bEnableCameraLag = CameraArm->bEnableCameraRotationLag = false;
        BoltSurveySlot = 0;
        PartsTestTime = 0.0f;
        // Lit while the doors swing open, so the glow's shaders are compiled by the first shot.
        Panels->SetAimHighlight(0, 0);
        return;
    }
    // The next part with fixings there to see.
    while (BoltSurveySlot < URallyPanelComponent::NumSlots
        && (Panels->NumFixings(BoltSurveySlot) == 0 || URallyPanelComponent::IsRetired(BoltSurveySlot) || Panels->IsRemoved(BoltSurveySlot)))
        ++BoltSurveySlot;
    if (BoltSurveySlot >= URallyPanelComponent::NumSlots)
    {
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_BOLTSURVEY_DONE"));
        bAutoDrive = false;
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyBoltSurvey"));
        return;
    }
    const int32 Slot = BoltSurveySlot;
    const bool bWheel = URallyPanelComponent::IsWheel(Slot);
    for (int32 I = 0; I < WheelMeshes.Num(); ++I) WheelMeshes[I]->SetVisibility(bWheel);
    // An engine module as it is reached: what comes off before it out of the way (the engine cover and
    // the air filter box over the rest; the front bumper in front of the intercooler).
    {
        using EModule = URallyPanelComponent::EModule;
        const bool bModule = URallyPanelComponent::IsModule(Slot);
        const EModule Kind = bModule ? URallyPanelComponent::ModuleKind(Slot) : EModule::Cover;
        ModuleMeshes[static_cast<int32>(EModule::Cover)]->SetVisibility(!bModule || Kind == EModule::Cover);
        ModuleMeshes[static_cast<int32>(EModule::AirFilter)]->SetVisibility(!bModule || Kind == EModule::Cover || Kind == EModule::AirFilter);
        // The bumper shows through its dent-able copy, attached under it: hidden with its children.
        PanelMeshes[7]->SetHiddenInGame(bModule && Kind == EModule::Intercooler, true);
    }
    if (!bBoltSurveyPlaced)
    {
        bBoltSurveyPlaced = true;
        // From outside the car towards the part, a little above it; the engine bay from in front and above.
        // Centred on its fixings (a door's are at its hinges, not its middle).
        FVector Sum = FVector::ZeroVector;
        for (int32 F = 0; F < Panels->NumFixings(Slot); ++F) Sum += Panels->GetFixingLocation(Slot, F);
        const FVector At = GetActorTransform().InverseTransformPosition(Sum / FMath::Max(Panels->NumFixings(Slot), 1));
        FVector Out = FVector(At.X, At.Y, 0.0);
        if (URallyPanelComponent::IsModule(Slot)) Out = FVector(1.0, 0.0, 0.0);
        // A door's hinge from the doorway behind it, as someone standing there sees it; the boot lid's from
        // behind and above.
        else if (Slot < 4) Out = FVector(-1.0, 0.6 * FMath::Sign(At.Y), 0.0);
        else if (Slot == 5) Out = FVector(-1.0, 0.0, 0.0);
        else if (FMath::Abs(At.Y) > 30.0) Out = FVector(0.25 * FMath::Sign(At.X), FMath::Sign(At.Y), 0.0);
        else Out = FVector(FMath::Sign(At.X), 0.0, 0.0);
        CameraArm->SetRelativeLocation(At);
        CameraArm->SetRelativeRotation(FRotator(URallyPanelComponent::IsModule(Slot) ? -60.0 : Slot == 5 ? -45.0 : -20.0, (-Out).Rotation().Yaw, 0.0));
        // The engine computer, by the bulkhead, from closer: further out the raised bonnet is in the way.
        const bool bEcu = URallyPanelComponent::IsModule(Slot) && URallyPanelComponent::ModuleKind(Slot) == URallyPanelComponent::EModule::Ecu;
        CameraArm->TargetArmLength = bEcu ? 70.0f : URallyPanelComponent::IsModule(Slot) ? 110.0f : Slot < 4 ? 80.0f : 150.0f;
        Panels->SetAimHighlight(Slot, INDEX_NONE);
    }
    if (!bBoltSurveyShot && PartsTestTime > 0.5f)
    {
        FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("RallyBolts_%02d.png"), Slot), false, false);
        FString Where;
        for (int32 F = 0; F < Panels->NumFixings(Slot); ++F)
            Where += GetActorTransform().InverseTransformPosition(Panels->GetFixingLocation(Slot, F)).ToCompactString() + TEXT(" ");
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_BOLTSURVEY %02d %s fixings=%d at %s(%s)"), Slot, *URallyPanelComponent::PanelName(Slot), Panels->NumFixings(Slot), *Where,
            *Panels->DebugFixingLooks(Slot));
        bBoltSurveyShot = true;
    }
    if (bBoltSurveyShot && PartsTestTime > 0.8f)
    {
        bBoltSurveyShot = bBoltSurveyPlaced = false;
        ++BoltSurveySlot;
        PartsTestTime = 0.0f;
    }
}

void ARallyCarPawn::TickPartsTest(float DeltaSeconds)
{
    PartsTestTime += DeltaSeconds;
    if (bBoltSurvey)
    {
        TickBoltSurvey();
        return;
    }
    ThrottleInput = BrakeInput = SteerTarget = 0.0f;
    bHandbrake = true;
    // Each corner from outside and a little ahead (behind, at the rear), at wheel height, its wheel hidden.
    static const TCHAR* const Corners[] = { TEXT("FL"), TEXT("FR"), TEXT("RL"), TEXT("RR") };
    const auto LookAt = [this](int32 Corner)
    {
        SetCockpitView(false);
        CameraArm->bDoCollisionTest = false;
        CameraArm->bEnableCameraLag = CameraArm->bEnableCameraRotationLag = false;
        CameraArm->SetRelativeLocation(AssemblyOffset + WheelSetups[Corner].Pivot + FVector(0.0, 0.0, 5.0));
        const double Side = Corner % 2 == 0 ? 90.0 : -90.0;
        const double Ahead = Corner < 2 ? -35.0 : 35.0;
        CameraArm->SetRelativeRotation(FRotator(-12.0, Side + (Corner % 2 == 0 ? Ahead : -Ahead), 0.0));
        CameraArm->TargetArmLength = 170.0f;
    };
    // Steps: 0 wait for the opening card to go; 1-8 sound corners (look, shoot); 9 damage; 10-17 damaged
    // corners; 18 report. The wheels stay hidden (a damage change shows them again).
    if (PartsTestStep == 0)
    {
        // A fixing placed by a socket: the front right driveshaft's Fixing1 socket (the model's own, from the v2
        // suspension set; added if a mesh lacks it) moved elsewhere takes that bolt with it; put back, the bolt is
        // back where it was.
        if (PartsTestSocketCheck == 0 && PartsTestTime > 1.0f)
        {
            PartsTestSocketCheck = 1;
            const int32 Shaft = URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Driveshaft, 1);
            // The driveshaft's own mesh (the front right one; the panels' Gear list is in slot order).
            UStaticMesh* ShaftMesh = nullptr;
            for (int32 Part = 0; Part < UE_ARRAY_COUNT(SuspensionMounts); ++Part)
                if (FCString::Strcmp(SuspensionMounts[Part].Name, TEXT("Driveshaft_FR")) == 0 && SuspensionMeshes.IsValidIndex(Part))
                    ShaftMesh = SuspensionMeshes[Part]->GetStaticMesh();
            if (ShaftMesh)
            {
                const FVector Before = Panels->GetFixingLocation(Shaft, 1);
                UStaticMeshSocket* Existing = ShaftMesh->FindSocket(TEXT("Fixing1"));
                UStaticMeshSocket* Socket = Existing ? Existing : NewObject<UStaticMeshSocket>(ShaftMesh);
                const FVector WasAt = Socket->RelativeLocation;
                const FRotator WasTurned = Socket->RelativeRotation;
                Socket->SocketName = TEXT("Fixing1");
                Socket->RelativeLocation = WasAt + FVector(5.0, 20.0, 8.0);
                Socket->RelativeRotation = FRotator(90.0, 0.0, 0.0);
                if (!Existing) ShaftMesh->AddSocket(Socket);
                Panels->DebugRebuildFixings();
                const UStaticMeshComponent* ShaftComponent = nullptr;
                for (const UStaticMeshComponent* Mesh : SuspensionMeshes)
                    if (Mesh && Mesh->GetStaticMesh() == ShaftMesh) ShaftComponent = Mesh;
                const FVector Wanted = ShaftComponent ? ShaftComponent->GetComponentTransform().TransformPosition(Socket->RelativeLocation) : FVector::ZeroVector;
                const FVector Placed = Panels->GetFixingLocation(Shaft, 1);
                if (Existing)
                {
                    Socket->RelativeLocation = WasAt;
                    Socket->RelativeRotation = WasTurned;
                }
                else
                {
                    ShaftMesh->RemoveSocket(Socket);
                }
                Panels->DebugRebuildFixings();
                const FVector After = Panels->GetFixingLocation(Shaft, 1);
                bPartsTestSocketOk = ShaftComponent && FVector::Dist(Placed, Wanted) < 2.0 && FVector::Dist(After, Before) < 0.1 && FVector::Dist(Placed, Before) > 5.0;
                UE_LOG(LogRallyCar, Display, TEXT("RALLY_PARTSTEST socket %s: placed %.1f cm from the socket, %.1f cm from where it was; back within %.2f cm"),
                    bPartsTestSocketOk ? TEXT("ok") : TEXT("WRONG"), FVector::Dist(Placed, Wanted), FVector::Dist(Placed, Before), FVector::Dist(After, Before));
            }
        }
        // First the front left wheel close up, its bolts lit as a player aiming at it sees them.
        if (PartsTestTime < 2.0f) return;
        if (PartsTestTime < 3.0f)
        {
            LookAt(0);
            CameraArm->TargetArmLength = 90.0f;
            CameraArm->SetRelativeRotation(FRotator(-5.0, 90.0, 0.0));
            Panels->SetAimHighlight(URallyPanelComponent::FirstWheel, 0);
            return;
        }
        // Screenshots are taken as a later frame is drawn, so the view stays put a moment around each.
        if (!bPartsTestShot)
        {
            FScreenshotRequest::RequestScreenshot(TEXT("RallyParts_wheel_bolts.png"), false, false);
            bPartsTestShot = true;
            return;
        }
        if (PartsTestTime < 3.5f) return;
        // A wheel bolt being undone: the second shown half turned out (as a player's hold shows it), the
        // third undone and easing out spinning; then both home again.
        if (PartsTestBoltStep == 0)
        {
            // Unlit, from an angle: the heads as they are, and how far each stands out.
            Panels->SetAimHighlight(INDEX_NONE, INDEX_NONE);
            CameraArm->TargetArmLength = 45.0f;
            CameraArm->SetRelativeRotation(FRotator(-4.0, 160.0, 0.0));
            Panels->ShowFixingTurn(URallyPanelComponent::FirstWheel, 1, 0.5f, false);
            if (HasAuthority()) Panels->DebugSetFixingOut(URallyPanelComponent::FirstWheel, 2, true);
            PartsTestBoltStep = 1;
            PartsTestBoltAt = PartsTestTime;
            return;
        }
        if (PartsTestBoltStep == 1)
        {
            if (PartsTestTime - PartsTestBoltAt < 0.08f) return;
            FScreenshotRequest::RequestScreenshot(TEXT("RallyParts_bolt_turning.png"), false, false);
            PartsTestBoltStep = 2;
            return;
        }
        if (PartsTestBoltStep == 2)
        {
            if (PartsTestTime - PartsTestBoltAt < 0.8f) return;
            FScreenshotRequest::RequestScreenshot(TEXT("RallyParts_bolt_out.png"), false, false);
            PartsTestBoltStep = 3;
            return;
        }
        if (PartsTestBoltStep == 3)
        {
            if (PartsTestTime - PartsTestBoltAt < 1.2f) return;
            Panels->ShowFixingTurn(INDEX_NONE, INDEX_NONE, 0.0f, false);
            if (HasAuthority()) Panels->DebugSetFixingOut(URallyPanelComponent::FirstWheel, 2, false);
            PartsTestBoltStep = 4;
            PartsTestTime = 3.5f;
            return;
        }
        if (PartsTestDrunkShotAt < 0.0f)
        {
            // Then the driver drunk, from the seat: the view leans as the head sways.
            if (ARallyCharacter* Driver = GetDriverCharacter()) Driver->DebugSetDrunk(1.0f);
            SetCockpitView(true);
            MouseLook = FVector2D::ZeroVector;
            if (PartsTestTime < 4.5f || (FMath::Sin(GetWorld()->GetTimeSeconds() * 0.9) < 0.95 && PartsTestTime < 14.0f)) return;
            FScreenshotRequest::RequestScreenshot(TEXT("RallyParts_drunk_cockpit.png"), false, false);
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_PARTSTEST drunk cockpit roll=%.1f deg"), CockpitCamera->GetRelativeRotation().Roll);
            PartsTestDrunkShotAt = PartsTestTime;
            return;
        }
        if (PartsTestTime < PartsTestDrunkShotAt + 0.5f) return;
        if (ARallyCharacter* Driver = GetDriverCharacter()) Driver->DebugSetDrunk(0.0f);
        SetCockpitView(false);
        PartsTestStep = 1;
        PartsTestTime = 0.0f;
        return;
    }
    for (UStaticMeshComponent* Wheel : WheelMeshes) Wheel->SetVisibility(false);
    // Each corner's shots light one part's bolts: the arm when sound, the strut when damaged.
    if (PartsTestStep >= 1 && PartsTestStep <= 17)
    {
        const int32 Corner = PartsTestStep <= 8 ? (PartsTestStep - 1) / 2 : PartsTestStep >= 10 ? (PartsTestStep - 10) / 2 : 0;
        Panels->SetAimHighlight(URallyPanelComponent::GearSlot(PartsTestStep <= 8 ? URallyPanelComponent::EGear::Arm : URallyPanelComponent::EGear::Strut, Corner), INDEX_NONE);
    }
    const auto Shoot = [&](int32 FirstStep, const TCHAR* When)
    {
        const int32 Corner = (PartsTestStep - FirstStep) / 2;
        if ((PartsTestStep - FirstStep) % 2 == 0)
        {
            LookAt(Corner);
            ++PartsTestStep;
            PartsTestTime = 0.0f;
        }
        else if (PartsTestTime > 0.6f)
        {
            FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("RallyParts_%s_%s.png"), Corners[Corner], When), false, false);
            ++PartsTestStep;
            PartsTestTime = 0.0f;
        }
    };
    if (PartsTestStep >= 1 && PartsTestStep <= 8)
    {
        if (PartsTestTime > 0.3f || (PartsTestStep - 1) % 2 == 0) Shoot(1, TEXT("sound"));
        return;
    }
    if (PartsTestStep == 9)
    {
        if (PartsTestTime < 0.3f) return;
        if (HasAuthority())
        {
            Damage->ApplyPartDamage(ERallyPart::SuspensionFrontLeft, 0.6f);
            Damage->ApplyPartDamage(ERallyPart::SuspensionFrontRight, 1.0f);
            Damage->ApplyPartDamage(ERallyPart::DriveshaftFrontRight, 1.0f);
            Damage->ApplyPartDamage(ERallyPart::BrakeRearLeft, 0.9f);
            Damage->ApplyPartDamage(ERallyPart::StrutRearRight, 1.0f);
            Damage->ApplyPartDamage(ERallyPart::TyreFrontLeft, 1.0f);
        }
        ++PartsTestStep;
        PartsTestTime = 0.0f;
        return;
    }
    if (PartsTestStep >= 10 && PartsTestStep <= 17)
    {
        // Let the broken strut's corner settle before the first damaged shot.
        if (PartsTestStep == 10 && PartsTestTime < 2.0f) return;
        if (PartsTestTime > 0.3f || (PartsTestStep - 10) % 2 == 0) Shoot(10, TEXT("damaged"));
        return;
    }
    if (PartsTestStep == 18)
    {
        // The boot, open, from behind and a little above: the spares kit laid out on its floor.
        if (HasAuthority()) Panels->DebugSetOpen(5, true);
        CameraArm->SetRelativeLocation(AssemblyOffset + FVector(-175.0, 0.0, 60.0));
        CameraArm->SetRelativeRotation(FRotator(-28.0, 0.0, 0.0));
        CameraArm->TargetArmLength = 150.0f;
        ++PartsTestStep;
        PartsTestTime = 0.0f;
        return;
    }
    if (PartsTestStep == 19)
    {
        if (PartsTestTime < 1.5f) return;
        FScreenshotRequest::RequestScreenshot(TEXT("RallyParts_boot.png"), false, false);
        ++PartsTestStep;
        PartsTestTime = 0.0f;
        return;
    }
    if (PartsTestStep == 20 && PartsTestTime > 0.5f)
    {
        // The spare wheel and the front strut out of the boot and onto the ground beside the car: their
        // places show as outlines.
        for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
            if (It->GetSourceCar() == this && It->GetMount() == ERallyPartMount::Stowed
                && (URallyPanelComponent::IsWheel(It->GetPanel()) || It->GetPanel() == URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Strut, 0)))
                BootTestParts.Add(*It);
        if (HasAuthority())
            for (const TWeakObjectPtr<ARallyDebrisActor>& Part : BootTestParts)
            {
                Part->Mount(ERallyPartMount::Loose);
                Part->SetActorLocation(GetActorLocation() - GetActorRightVector() * 300.0 + FVector(0.0, 0.0, 40.0 + 60.0 * BootTestParts.IndexOfByKey(Part)),
                    false, nullptr, ETeleportType::TeleportPhysics);
            }
        ++PartsTestStep;
        PartsTestTime = 0.0f;
        return;
    }
    if (PartsTestStep == 21 && PartsTestTime > 0.6f)
    {
        const int32 Strut = URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Strut, 0);
        const bool bOutlines = Panels->IsBootGhostShown(URallyPanelComponent::FirstWheel + 2) && Panels->IsBootGhostShown(Strut);
        FScreenshotRequest::RequestScreenshot(TEXT("RallyParts_boot_out.png"), false, false);
        // Put back as a player would (Stow): each into its own place, the wheel into its well.
        bool bBack = BootTestParts.Num() == 2;
        if (HasAuthority())
            for (const TWeakObjectPtr<ARallyDebrisActor>& Part : BootTestParts)
            {
                FVector Home;
                bBack = bBack && Part.IsValid() && Panels->FreeHomeFor(Part.Get()) != INDEX_NONE && Panels->Stow(nullptr, Part.Get())
                    && Panels->GetBootHome(URallyPanelComponent::IsWheel(Part->GetPanel()) ? URallyPanelComponent::FirstWheel + 2 : Strut, Home)
                    && FVector::Dist(Part->GetMountLocation(), Home) < 0.1;
            }
        bBootTestOk = bOutlines && bBack;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_PARTSTEST boot %s: outlines shown=%d, back in their places=%d"),
            bBootTestOk ? TEXT("ok") : TEXT("WRONG"), bOutlines ? 1 : 0, bBack ? 1 : 0);
        ++PartsTestStep;
        PartsTestTime = 0.0f;
        return;
    }
    if (PartsTestStep == 22 && PartsTestTime > 0.6f)
    {
        // Back in: strapped down, no outlines.
        const int32 Strut = URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Strut, 0);
        bBootTestOk = bBootTestOk && !Panels->IsBootGhostShown(URallyPanelComponent::FirstWheel + 2) && !Panels->IsBootGhostShown(Strut);
        FScreenshotRequest::RequestScreenshot(TEXT("RallyParts_boot_back.png"), false, false);
        ++PartsTestStep;
        PartsTestTime = 0.0f;
        return;
    }
    if (PartsTestStep == 23 && PartsTestTime > 0.5f)
    {
        // The bonnet up first: it swings open over a moment.
        if (HasAuthority()) Panels->DebugSetOpen(4, true);
        PartsTestStep = 24;
        PartsTestTime = 0.0f;
        return;
    }
    if (PartsTestStep == 24 && PartsTestTime > 2.0f)
    {
        // Every engine module's fixings can be reached from somewhere round the open bay, with the parts the
        // game has come off first (cover and air box, the turbo before the exhaust manifold) taken off.
        using EModule = URallyPanelComponent::EModule;
        bPartsTestReachOk = true;
        if (HasAuthority())
        {
            const auto Slot = [](EModule Kind) { return URallyPanelComponent::ModuleSlot(Kind); };
            // The intercooler last: it is worked with the front bumper off (which stays off).
            for (const EModule Kind : { EModule::Cover, EModule::AirFilter, EModule::Intake, EModule::Turbo, EModule::ExhaustManifold,
                    EModule::Alternator, EModule::Starter, EModule::Ecu, EModule::Coil1, EModule::Coil2, EModule::Coil3, EModule::Coil4,
                    EModule::Intercooler })
            {
                if (Kind == EModule::Intercooler) Panels->DebugDetach(7);
                const bool bUnderCover = Kind != EModule::Cover && Kind != EModule::AirFilter && Kind != EModule::Alternator && Kind != EModule::Ecu
                    && Kind != EModule::Intercooler;
                Panels->DebugSetRemoved(Slot(EModule::Cover), bUnderCover);
                Panels->DebugSetRemoved(Slot(EModule::AirFilter), bUnderCover);
                Panels->DebugSetRemoved(Slot(EModule::Turbo), Kind == EModule::ExhaustManifold);
                // The starter is done from under the front, on the jack.
                Panels->DebugPlaceJack(Kind == EModule::Starter ? 0 : -1);
                TArray<int32> Missed;
                const int32 Reached = Panels->DebugReachableFixings(Slot(Kind), Missed);
                FString List;
                for (const int32 F : Missed) List += FString::Printf(TEXT(" %d"), F);
                UE_LOG(LogRallyCar, Display, TEXT("RALLY_PARTSTEST reach %s: %d of %d%s%s"), *URallyPanelComponent::PanelName(Slot(Kind)), Reached,
                    Panels->NumFixings(Slot(Kind)), Missed.IsEmpty() ? TEXT("") : TEXT(", not reached:"), *List);
                bPartsTestReachOk = bPartsTestReachOk && Missed.IsEmpty();
            }
            for (const EModule Kind : { EModule::Cover, EModule::AirFilter, EModule::Turbo }) Panels->DebugSetRemoved(Slot(Kind), false);
            Panels->DebugPlaceJack(-1);
            Panels->DebugSetOpen(4, false);
        }
        PartsTestStep = 25;
        // What a player looking at each part reads, and how far the bent arm kinks.
        FString States;
        for (int32 Slot = URallyPanelComponent::FirstWheel; Slot < URallyPanelComponent::FirstGear + URallyPanelComponent::NumGear; ++Slot)
        {
            if (URallyPanelComponent::IsRetired(Slot)) continue;
            const FString State = Panels->PartState(Slot);
            if (!State.IsEmpty()) States += FString::Printf(TEXT("[%s: %s] "), *URallyPanelComponent::PanelName(Slot), *State);
        }
        float Bend = 0.0f;
        for (int32 Part = 0; Part < SuspensionLooks.Num(); ++Part)
            if (SuspensionLooks[Part] && FCString::Strcmp(SuspensionMounts[Part].Name, TEXT("LowerArm_FL")) == 0)
                Bend = SuspensionLooks[Part]->K2_GetScalarParameterValue(TEXT("Bend"));
        const bool bPass = States.Contains(TEXT("front left arm: bent 40%")) && States.Contains(TEXT("front right arm: snapped"))
            && States.Contains(TEXT("front right driveshaft: snapped")) && States.Contains(TEXT("rear left brake disc: scored"))
            && States.Contains(TEXT("rear right strut: spring broken")) && States.Contains(TEXT("front left wheel: tyre flat"))
            && FMath::IsNearlyEqual(Bend, 6.6f, 0.01f) && !WheelSetups[1].bSteered && WheelSetups[0].bSteered && bPartsTestSocketOk && bBootTestOk && bPartsTestReachOk;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_PARTSTEST_DONE result=%s bend_fl=%.1f steers fl=%d fr=%d states=%s damage=[%s]"),
            bPass ? TEXT("pass") : TEXT("fail"), Bend, WheelSetups[0].bSteered ? 1 : 0, WheelSetups[1].bSteered ? 1 : 0, *States, *DescribeDamage());
        bAutoDrive = false;
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyPartsTest"));
    }
}

void ARallyCarPawn::TickKnockTest(float DeltaSeconds)
{
    KnockTestTime += DeltaSeconds;
    const float T = KnockTestTime;
    const int32 Intake = URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Intake);
    const int32 Turbo = URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Turbo);
    ThrottleInput = BrakeInput = SteerTarget = 0.0f;
    bHandbrake = true;
    // The intake as the camera sees it close up: bonnet up, the engine cover out of the way (it hides the
    // intake; the playtest had taken it off).
    const int32 Cover = URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Cover);
    const auto LookAtIntake = [this, Intake, Cover](bool bLook)
    {
        if (HasAuthority()) Panels->DebugSetOpen(4, bLook);
        ModuleMeshes[URallyPanelComponent::ModuleIndex(Cover)]->SetVisibility(!bLook);
        SetCockpitView(false);
        CameraArm->bDoCollisionTest = !bLook;
        CameraArm->bEnableCameraLag = CameraArm->bEnableCameraRotationLag = !bLook;
        if (bLook)
        {
            const FVector Centre = GetActorTransform().InverseTransformPosition(ModuleMeshes[URallyPanelComponent::ModuleIndex(Intake)]->Bounds.Origin);
            CameraArm->SetRelativeLocation(Centre);
            CameraArm->SetRelativeRotation(FRotator(-50.0, 200.0, 0.0));
            CameraArm->TargetArmLength = 110.0f;
        }
        else
        {
            // Chasing from behind and to the side, where the tail pipe's smoke shows.
            CameraArm->SetRelativeLocation(FVector(0.0, 0.0, 60.0));
            CameraArm->SetRelativeRotation(FRotator(-10.0, 25.0, 0.0));
            CameraArm->TargetArmLength = 600.0f;
        }
    };
    if (T < 0.5f) return;
    if (KnockTestStep == 0)
    {
        KnockTestStep = 1;
        LookAtIntake(true);
        return;
    }
    if (KnockTestStep == 1)
    {
        // Once the camera has settled.
        if (T > 3.0f)
        {
            KnockTestStep = 2;
            FScreenshotRequest::RequestScreenshot(TEXT("RallyKnock_intake_before.png"), false, false);
        }
        return;
    }
    if (KnockTestStep == 2)
    {
        if (T < 3.5f) return;
        KnockTestStep = 3;
        // The first 12 s at the stock 0.8 bar, for a clean recording to compare the pinking against.
        if (HasAuthority())
        {
            Panels->DebugSetGrade(Turbo, static_cast<uint8>(Rally::Grade::EGrade::Stock), 8, 0);
            // All but two of the intake's bolts left out (eleven on the 1.8T): the two left work loose.
            for (int32 F = 0; F + 2 < Panels->NumFixings(Intake); ++F) Panels->DebugSetFixingOut(Intake, F);
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("RallyRecordAudio"))) UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 30.0f);
        KnockTestDriveAt = T;
        LookAtIntake(false);
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_KNOCKTEST start boost=%.1f bar intake %d of %d out engine=[%s]"),
            Panels->GetBoostTenths(Turbo) * 0.1, Panels->CountOut(Intake), Panels->NumFixings(Intake), *Engine->Describe());
    }
    FVisualState Snapshot;
    {
        FScopeLock Lock(&PhysicsLock);
        Snapshot = Visual;
    }
    const double Detonation = Engine->IsRunning() ? Rally::Engine::Detonation(Engine->GetModules().KnockPerSecond) : 0.0;
    // What the driver would hear: detonation, gated by load as the sound model does.
    if (Snapshot.EngineLoad > 0.8) KnockTestMaxDetonation = FMath::Max(KnockTestMaxDetonation, Detonation);
    if (KnockTestSmokeHealth < 0.0 && Puffs->GetInstanceCount() > 0) KnockTestSmokeHealth = Engine->GetEngineHealth();
    if (T >= NextKnockTestLog)
    {
        NextKnockTestLog = T + 2.0f;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_KNOCKTEST t=%.0f kmh=%.0f rpm=%.0f kw=%.0f detonation=%.2f engine=%.2f puffs=%d intake_out=%d tilt=%.1f rattle=%.2f loose=%d running=%d at=%s rot=%s spin=%.0f engine_roll=%.2f"),
            T, GetForwardSpeedKmh(), Snapshot.EngineRpm, Snapshot.EnginePowerKw, Detonation, Engine->GetEngineHealth(), Puffs->GetInstanceCount(),
            Panels->CountOut(Intake), Panels->GetModuleTiltDeg(Intake),
            Rally::Fixing::RattleLevel(Panels->GetLoosestShare(), Engine->IsRunning(), GetForwardSpeedKmh()), KnockTestLoose, Engine->IsRunning() ? 1 : 0,
            *GetActorLocation().ToCompactString(), *GetActorRotation().ToCompactString(), Body->GetPhysicsAngularVelocityInDegrees().Size(),
            GetEngineRollDeg());
    }
    bHandbrake = false;
    if (KnockTestDriveAt >= 0.0f && T - KnockTestDriveAt > 12.0f && Panels->GetBoostTenths(Turbo) != 14 && HasAuthority())
    {
        Panels->DebugSetGrade(Turbo, static_cast<uint8>(Rally::Grade::EGrade::Stock), 14, 0);
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_KNOCKTEST boost 1.4 bar t=%.1f"), T);
    }
    if (KnockTestDriveAt >= 0.0f && T - KnockTestDriveAt > 24.0f && !bKnockTestRecorded)
    {
        bKnockTestRecorded = true;
        if (FParse::Param(FCommandLine::Get(), TEXT("RallyRecordAudio")))
            UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, TEXT("RallyKnock_audio"), FString());
    }
    if (KnockTestStopped < 0.0f)
    {
        // Circles of 18 m in the open ground beside the start, clear of the crash barrier behind it, flat out
        // up to 40 km/h: hard on the engine, and well within the lab (an open-loop circle understeers wider,
        // drifts off the edge or into the barrier).
        const FVector2D ToCentre = FVector2D(AutoDriveStart + FVector(700.0, 2500.0, 0.0) - GetActorLocation());
        const double Distance = ToCentre.Size();
        const FVector2D Inward = Distance > 1.0 ? ToCentre / Distance : FVector2D(1.0, 0.0);
        const FVector2D Around(-Inward.Y, Inward.X);
        const FVector2D Wanted = (Around + Inward * FMath::Clamp((Distance - 1800.0) / 400.0, -1.0, 1.5)).GetSafeNormal();
        const FVector2D Forward = FVector2D(GetActorForwardVector()).GetSafeNormal();
        const double Error = FMath::RadiansToDegrees(FMath::Atan2(Forward.X * Wanted.Y - Forward.Y * Wanted.X, FVector2D::DotProduct(Forward, Wanted)));
        SteerTarget = static_cast<float>(FMath::Clamp(Error / 25.0, -1.0, 1.0));
        ThrottleInput = GetForwardSpeedKmh() < 40.0f ? 1.0f : 0.2f;
        // Wedged against something with the throttle down: give up rather than cook the engine.
        KnockTestStuck = FMath::Abs(GetForwardSpeedKmh()) < 3.0f && T > 10.0f ? KnockTestStuck + DeltaSeconds : 0.0f;
        if (KnockTestStuck > 4.0f)
        {
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_KNOCKTEST_DONE result=fail stuck at=%s"), *GetActorLocation().ToCompactString());
            bAutoDrive = false;
            FGenericPlatformMisc::RequestExit(false, TEXT("RallyKnockTest"));
            return;
        }
        // At the first sign of wear, pull up and rev against the brakes: the blue haze builds behind the car.
        if (KnockTestStep == 3 && Engine->GetEngineHealth() < 0.88 && KnockTestSmokeAt < 0.0f) KnockTestSmokeAt = T;
        if (KnockTestStep == 3 && KnockTestSmokeAt >= 0.0f)
        {
            const float Since = T - KnockTestSmokeAt;
            BrakeInput = ThrottleInput = 1.0f;
            SteerTarget = 0.0f;
            CameraArm->SetRelativeRotation(FRotator(-8.0, 20.0, 0.0));
            CameraArm->TargetArmLength = 450.0f;
            if (Since > 6.0f)
            {
                KnockTestStep = 4;
                FScreenshotRequest::RequestScreenshot(TEXT("RallyKnock_smoke.png"), false, false);
                CameraArm->SetRelativeRotation(FRotator(-10.0, 25.0, 0.0));
                CameraArm->TargetArmLength = 600.0f;
                KnockTestStuck = 0.0f;
            }
            return;
        }
        if (!Engine->IsRunning() || T > 245.0f)
        {
            KnockTestStopped = T;
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_KNOCKTEST stopping t=%.0f running=%d engine=%.2f intake_out=%d"), T, Engine->IsRunning() ? 1 : 0,
                Engine->GetEngineHealth(), Panels->CountOut(Intake));
        }
        return;
    }
    const float Stopped = T - KnockTestStopped;
    if (FMath::Abs(GetForwardSpeedKmh()) > 2.0f && Stopped < 8.0f) BrakeInput = 1.0f;
    else bHandbrake = true;
    if (Stopped > 8.0f && KnockTestStep <= 4)
    {
        KnockTestStep = 5;
        LookAtIntake(true);
    }
    else if (Stopped > 10.0f && KnockTestStep == 5)
    {
        KnockTestStep = 6;
        FScreenshotRequest::RequestScreenshot(TEXT("RallyKnock_intake_after.png"), false, false);
    }
    else if (Stopped > 11.0f && KnockTestStep == 6)
    {
        KnockTestStep = 7;
        ModuleMeshes[URallyPanelComponent::ModuleIndex(Intake)]->SetVisibility(false);
    }
    else if (Stopped > 12.5f && KnockTestStep == 7)
    {
        KnockTestStep = 8;
        FScreenshotRequest::RequestScreenshot(TEXT("RallyKnock_intake_hidden.png"), false, false);
    }
    else if (Stopped > 13.5f && KnockTestStep == 8)
    {
        KnockTestStep = 9;
        const bool bPass = KnockTestMaxDetonation > 0.9 && KnockTestSmokeHealth > 0.8 && KnockTestLoose >= 2 && bKnockTestNoneLeft
            && !Engine->IsRunning() && Panels->CountOut(Intake) == Panels->NumFixings(Intake) && Panels->GetModuleTiltDeg(Intake) >= 12.0
            // The engine leaned on its mounts under load, and settled once it stopped.
            && GetEngineMaxRollDeg() > 1.0 && FMath::Abs(GetEngineRollDeg()) < 0.3;
        const UStaticMeshComponent* IntakeMesh = ModuleMeshes[URallyPanelComponent::ModuleIndex(Intake)];
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_KNOCKTEST_DONE result=%s max_detonation=%.2f first_smoke_at_engine=%.2f loose_events=%d none_left=%d intake_out=%d tilt=%.1f mesh_rot=%s mesh_at=%s size=%s engine=%.2f running=%d engine_roll max=%.2f now=%.2f prompt=[%s]"),
            bPass ? TEXT("pass") : TEXT("fail"), KnockTestMaxDetonation, KnockTestSmokeHealth, KnockTestLoose, bKnockTestNoneLeft ? 1 : 0,
            Panels->CountOut(Intake), Panels->GetModuleTiltDeg(Intake), *IntakeMesh->GetRelativeRotation().ToCompactString(),
            *IntakeMesh->GetRelativeLocation().ToCompactString(), *(IntakeMesh->Bounds.BoxExtent * 2.0).ToCompactString(),
            Engine->GetEngineHealth(), Engine->IsRunning() ? 1 : 0, GetEngineMaxRollDeg(), GetEngineRollDeg(), *Engine->Describe());
        bAutoDrive = false;
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyKnockTest"));
    }
}

void ARallyCarPawn::TickCrashTest(float DeltaSeconds)
{
    // -RallyCrashTest (with -RallyAutoSeat=driver -RallyAutoDrive): the car starts about 30 m short of the
    // lab's impact barrier, drives into it at full throttle, brakes, reverses straight (a bent corner
    // shows as a heading change) and photographs the damage from inside and outside.
    CrashTestTime += DeltaSeconds;
    ThrottleInput = BrakeInput = SteerTarget = 0.0f;
    bHandbrake = false;
    if (CrashImpactTime < 0.0f)
    {
        if (CrashTestTime > 2.5f)
        {
            ThrottleInput = 1.0f;
            // Speed before the first contact; contacts are reported after the step that resolved them.
            if (!PendingImpact.bActive)
            {
                CrashImpactKmh = GetForwardSpeedKmh();
            }
        }
        if (CrashTestTime > 20.0f)
        {
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_CRASHTEST_DONE result=no_impact"));
            bAutoDrive = false;
            FGenericPlatformMisc::RequestExit(false, TEXT("RallyCrashTest"));
        }
        return;
    }
    const float Since = CrashTestTime - CrashImpactTime;
    if (Since >= NextAutoDriveLog)
    {
        NextAutoDriveLog = Since + 0.5f;
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_CRASHTEST t=%.1f kmh=%.1f yaw=%.1f reversing=%d"), Since, GetForwardSpeedKmh(), GetActorRotation().Yaw, bReversing ? 1 : 0);
    }
    if (Since < 2.0f)
    {
        BrakeInput = 1.0f;
        return;
    }
    if (Since < 6.0f)
    {
        // Brake held below 3 km/h selects reverse.
        if (!bCrashReverseStarted)
        {
            bCrashReverseStarted = true;
            CrashReverseYaw = GetActorRotation().Yaw;
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_CRASHTEST impact_kmh=%.1f state=[%s]"), CrashImpactKmh, *DescribeDamage());
        }
        BrakeInput = 0.6f;
        return;
    }
    // In reverse the throttle pedal brakes; handbrake once stopped.
    if (FMath::Abs(GetForwardSpeedKmh()) > 1.0f && Since < 9.0f)
    {
        ThrottleInput = 1.0f;
        return;
    }
    // The engine run takes over after the first photographs (cockpit, both front corners, the nose).
    if (bEngineTest && CrashShotStep >= 8)
    {
        TickEngineTest(Since);
        return;
    }
    bHandbrake = true;
    const auto Look = [this](float Yaw, float Pitch) { MouseLook = FVector2D(Yaw, Pitch); MouseIdleTime = 0.0f; };
    if (++CrashShotFrames < 10)
    {
        return;
    }
    CrashShotFrames = 0;
    switch (CrashShotStep++)
    {
    case 0:
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_CRASHTEST reverse_yaw_change=%.2f deg dents=%d"),
            FMath::FindDeltaAngleDegrees(CrashReverseYaw, GetActorRotation().Yaw), BodyDeform->GetDentCount());
        SetCockpitView(true);
        Look(0.0f, -8.0f);
        break;
    case 1: FScreenshotRequest::RequestScreenshot(TEXT("RallyCrash_cockpit.png"), false, false); break;
    case 2:
        SetCockpitView(false);
        CameraArm->bEnableCameraLag = false;
        CameraArm->bEnableCameraRotationLag = false;
        CameraArm->TargetArmLength = 520.0f;
        CameraArm->SetRelativeRotation(FRotator(-18.0, 145.0, 0.0));
        break;
    case 3: FScreenshotRequest::RequestScreenshot(TEXT("RallyCrash_front_left.png"), false, false); break;
    case 4: CameraArm->SetRelativeRotation(FRotator(-18.0, -145.0, 0.0)); break;
    case 5: FScreenshotRequest::RequestScreenshot(TEXT("RallyCrash_front_right.png"), false, false); break;
    case 6: CameraArm->SetRelativeRotation(FRotator(-35.0, 180.0, 0.0)); CameraArm->TargetArmLength = 420.0f; break;
    case 7: FScreenshotRequest::RequestScreenshot(TEXT("RallyCrash_front.png"), false, false); break;
    case 8:
        // The bonnet, front bumper, left wing and left headlamp come off and are laid beside the nose, as
        // if they had flown off, to check that detached parts still read as the parts they were.
        if (HasAuthority())
        {
            for (const int32 Panel : { 4, 7, 9, 11 })
            {
                Panels->DebugDetach(Panel);
            }
        }
        break;
    case 9:
        // A frame later, once their physics bodies exist.
        CrashDebrisTime = CrashTestTime;
        for (const int32 Panel : { 4, 7, 9, 11 })
        {
            ARallyDebrisActor* Part = HasAuthority() ? Panels->GetDebris(Panel) : nullptr;
            if (UPrimitiveComponent* Shape = Part ? Cast<UPrimitiveComponent>(Part->GetRootComponent()) : nullptr)
            {
                const FVector Beside(Panel == 4 || Panel == 7 ? 170.0 : 20.0, Panel == 4 || Panel == 9 ? -230.0 : 230.0, 0.0);
                Shape->SetWorldLocationAndRotation(GetActorTransform().TransformPosition(Beside), GetActorQuat(), false, nullptr,
                    ETeleportType::ResetPhysics);
                Shape->SetPhysicsLinearVelocity(FVector::ZeroVector);
                Shape->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
            }
        }
        break;
    case 10:
        if (CrashTestTime - CrashDebrisTime < 2.5f)
        {
            --CrashShotStep; // Let the parts land.
            break;
        }
        for (const int32 Panel : { 4, 7, 9, 11 })
        {
            const ARallyDebrisActor* Part = Panels->GetDebris(Panel);
            UE_LOG(LogRallyCar, Display, TEXT("RALLY_CRASHTEST debris panel=%d at=%s max_dent=%.1f"), Panel,
                Part ? *GetActorTransform().InverseTransformPosition(Part->GetActorLocation()).ToCompactString() : TEXT("none"),
                Part ? Part->GetMaxDentCm() : 0.0f);
        }
        CameraArm->SetRelativeRotation(FRotator(-50.0, 180.0, 0.0));
        CameraArm->TargetArmLength = 650.0f;
        break;
    case 11: FScreenshotRequest::RequestScreenshot(TEXT("RallyCrash_debris.png"), false, false); break;
    case 12:
        CameraArm->SetRelativeLocation(FVector(170.0, -230.0, -50.0));
        CameraArm->SetRelativeRotation(FRotator(-45.0, 150.0, 0.0));
        CameraArm->TargetArmLength = 300.0f;
        break;
    case 13: FScreenshotRequest::RequestScreenshot(TEXT("RallyCrash_debris_bonnet.png"), false, false); break;
    case 14:
        CameraArm->SetRelativeLocation(FVector(170.0, 230.0, -50.0));
        CameraArm->SetRelativeRotation(FRotator(-45.0, -150.0, 0.0));
        break;
    case 15: FScreenshotRequest::RequestScreenshot(TEXT("RallyCrash_debris_bumper.png"), false, false); break;
    case 16:
        // Each left corner from outside with its wheel hidden: hub, disc, strut and arms.
        WheelMeshes[0]->SetVisibility(false);
        WheelMeshes[2]->SetVisibility(false);
        CameraArm->SetRelativeLocation(AssemblyOffset + WheelSetups[0].Pivot);
        CameraArm->SetRelativeRotation(FRotator(-12.0, 75.0, 0.0));
        CameraArm->TargetArmLength = 170.0f;
        break;
    case 17: FScreenshotRequest::RequestScreenshot(TEXT("RallyCrash_suspension_front.png"), false, false); break;
    case 18: CameraArm->SetRelativeLocation(AssemblyOffset + WheelSetups[2].Pivot); CameraArm->SetRelativeRotation(FRotator(-12.0, 105.0, 0.0)); break;
    case 19: FScreenshotRequest::RequestScreenshot(TEXT("RallyCrash_suspension_rear.png"), false, false); break;
    default:
        UE_LOG(LogRallyCar, Display, TEXT("RALLY_CRASHTEST_DONE result=impact state=[%s]"), *DescribeDamage());
        bAutoDrive = false;
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyCrashTest"));
        break;
    }
}

void ARallyCarPawn::OnRep_Lights() { UpdateLights(); }

void ARallyCarPawn::UpdateLights()
{
    if (!HeadlampLeft || !HeadlampRight || !Panels || !Engine) return;
    // A lamp unit off the car gives no light; nor does a flat or dead battery.
    const bool bPower = Rally::Engine::HasPower(Engine->GetBatteryHealth()) && Engine->GetCharge() > Rally::Engine::FlatCharge;
    HeadlampLeft->SetVisibility(bLightsOn && bPower && !Panels->IsDetached(11));
    HeadlampRight->SetVisibility(bLightsOn && bPower && !Panels->IsDetached(12));
}

void ARallyCarPawn::SetLights(bool bOn)
{
    if (HasAuthority() && bLightsOn != bOn) ServerToggleLights_Implementation();
}

void ARallyCarPawn::ServerToggleLights_Implementation()
{
    bLightsOn = !bLightsOn;
    UpdateLights();
    ForceNetUpdate();
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_CAR lights %s"), bLightsOn ? TEXT("on") : TEXT("off"));
}

void ARallyCarPawn::OnSleep(const FInputActionValue& Value)
{
    // Z sleeps till morning, Shift+Z till the rally weekend.
    const APlayerController* PC = Cast<APlayerController>(GetController());
    ServerToggleSleep(PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift)));
}

void ARallyCarPawn::ServerToggleSleep_Implementation(bool bTillRally)
{
    if (ARallyCharacter* Driver = Occupants.IsValidIndex(0) ? Occupants[0].Get() : nullptr)
    {
        Driver->SetSleeping(!Driver->IsSleeping(), bTillRally);
        if (ARallyGameMode* Mode = GetWorld()->GetAuthGameMode<ARallyGameMode>()) Mode->TrySleep();
    }
}

void ARallyCarPawn::OnHelp()
{
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
        if (ARallyHUD* Hud = Cast<ARallyHUD>(PC->GetHUD())) Hud->ToggleHelp();
}

void ARallyCarPawn::ServerCallTow_Implementation()
{
    if (ARallyGameMode* Mode = GetWorld()->GetAuthGameMode<ARallyGameMode>()) Mode->Tow(this);
}

void ARallyCarPawn::UpdateBootLoad()
{
    // What is in the boot beyond the kit it left with weighs on the car, at the back: heavier, and its
    // centre of gravity further back (docs/BOOT.md). Every machine works it out from the replicated parts.
    if (!Panels || GetWorld()->GetTimeSeconds() < NextBootCheck) return;
    NextBootCheck = GetWorld()->GetTimeSeconds() + 0.5;
    const double Extra = Panels->GetBootLoadKg() - Panels->GetBootKitKg();
    if (FMath::Abs(Extra - AppliedBootExtraKg) < 0.5) return;
    AppliedBootExtraKg = Extra;
    const double Mass = FMath::Max(VehicleMassKg + Extra, 900.0);
    Body->SetMassOverrideInKg(NAME_None, static_cast<float>(Mass), true);
    // The kit's own place is in the body's tuned centre of gravity; the difference sits about 2 m behind it.
    const double NudgeX = (20.0 * VehicleMassKg - 210.0 * Extra) / Mass;
    Body->SetCenterOfMass(FVector(NudgeX, 0.0, -30.0));
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_BOOT load %+.0f kg against the kit: car %.0f kg, centre of gravity %.1f cm"), Extra, Mass, NudgeX);
}

void ARallyCarPawn::ServerBias_Implementation(int32 Step)
{
    SetBiasSteps(BiasSteps + FMath::Clamp(Step, -1, 1));
}

void ARallyCarPawn::SetBiasSteps(int32 Steps)
{
    if (!HasAuthority()) return;
    const int32 Clamped = FMath::Clamp(Steps, -Rally::Grade::MaxBiasSteps, Rally::Grade::MaxBiasSteps);
    if (Clamped == BiasSteps) return;
    BiasSteps = static_cast<int8>(Clamped);
    OnDamageChanged();
    ForceNetUpdate();
    UE_LOG(LogRallyCar, Display, TEXT("RALLY_TUNE brake bias %.0f%% front"), Rally::Grade::BiasPct(BiasSteps));
}

void ARallyCarPawn::ServerRecce_Implementation()
{
    if (ARallyStageDirector* Director = ARallyStageDirector::Get(GetWorld())) Director->StartRecce(this);
}

void ARallyCarPawn::OnCup()
{
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
        if (ARallyHUD* Hud = Cast<ARallyHUD>(PC->GetHUD())) Hud->ToggleCup();
}
