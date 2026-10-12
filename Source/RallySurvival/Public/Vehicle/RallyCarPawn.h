#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HAL/CriticalSection.h"
#include "Vehicle/RallyVehicleSetup.h"
#include "RallyCarPawn.generated.h"

class ARallyCharacter;
class UBoxComponent;
class UPrimitiveComponent;
class URallyBodyDeformComponent;
class URallyDamageComponent;
class URallyEngineComponent;
class URallySynthComponent;
enum class ERallyStartFailure : uint8;
class URallyPanelComponent;
class UInstancedStaticMeshComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
namespace UE::Geometry { class FDynamicMesh3; }
struct FInputActionValue;
namespace Chaos { class FRigidBodyHandle_Internal; }

// Octavia prototype with project-owned vehicle physics (docs/VEHICLE-PHYSICS.md).
// The box root is the only physics body. Each fixed physics step the car raycasts four wheels,
// applies spring-damper suspension, anti-roll bars and tyre forces, and integrates engine, clutch,
// gearbox and an open front differential.
// Networking (docs/MULTIPLAYER.md): only the driver's machine simulates the car (the server when
// nobody drives); every other machine keeps the body kinematic and replays timestamped snapshots.
UCLASS()
class RALLYSURVIVAL_API ARallyCarPawn : public APawn
{
    GENERATED_BODY()

public:
    ARallyCarPawn();

    virtual void Tick(float DeltaSeconds) override;
    virtual void AsyncPhysicsTickActor(float DeltaTime, float SimTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void NotifyControllerChanged() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual FVector GetVelocity() const override;

    URallyPanelComponent* GetPanels() const { return Panels; }
    // Handling from the parts' condition again (a fixing turned, a part fitted).
    void RefreshCondition() { OnDamageChanged(); }
    URallyDamageComponent* GetDamage() const { return Damage; }
    URallyEngineComponent* GetEngine() const { return Engine; }
    // Battery on its tray behind the left headlamp, and the middle of the engine block, assembly frame.
    static FVector BatteryPoint() { return FVector(181.0, -52.0, 66.0); }
    static FVector EngineBlockPoint() { return FVector(147.0, 15.0, 45.0); }
    // Under the floor, assembly frame: the bottom of the sump, the gearbox casing, the fuel tank, and the
    // exhaust's front pipe, middle and silencer.
    static FVector SumpPoint() { return FVector(150.0, 15.0, 17.0); }
    static FVector GearboxPoint() { return FVector(142.0, -10.0, 21.0); }
    static FVector FuelTankPoint() { return FVector(-76.0, 6.0, 20.0); }
    static FVector ExhaustPoint(int32 Index) { return Index == 0 ? FVector(40.0, 0.0, 20.0) : Index == 1 ? FVector(-60.0, -10.0, 21.0) : FVector(-185.0, -35.0, 22.0); }
    static constexpr int32 NumExhaustPoints = 3;
    // The exhaust's front hanger (its mesh pivot), assembly frame.
    static FVector ExhaustHanger() { return FVector(78.8, 0.7, 22.8); }

    // A jack under a corner (0 FL, 1 FR, 2 RL, 3 RR, -1 none) lifts the sill there to JackLiftCm.
    void SetJackCorner(int32 Corner);
    static constexpr double JackLiftCm = 38.0;
    // Jacking point on the sill by each wheel, assembly frame.
    static FVector JackPoint(int32 Corner);
    uint8 GetPanelTestPhase() const { return PanelTestPhase; }

    // Signed: negative while rolling backwards.
    UFUNCTION(BlueprintPure, Category = "Rally|Vehicle")
    float GetForwardSpeedKmh() const;

    UFUNCTION(BlueprintCallable, Category = "Rally|Vehicle")
    void ResetUpright();

    // Seats: 0 driver (front left), 1 co-driver (front right).
    static constexpr int32 SeatCount = 2;
    bool IsSeatFree(int32 Seat) const;
    // Eye point relative to the car's root, for seating a character.
    FVector GetSeatEyeLocation(int32 Seat) const;
    // Where a character stands to use a seat's door, in world space.
    FVector GetSeatDoorLocation(int32 Seat) const;
    // Car and seat whose door the character can reach, or nullptr.
    static ARallyCarPawn* FindSeatInReach(const AActor* Character, int32& OutSeat);
    // Server only. The driver's controller possesses the car; the co-driver stays on foot pawn, seated.
    bool ServerEnterSeat(ARallyCharacter* Character, int32 Seat, bool bSkipReachCheck);
    bool ServerExitSeat(ARallyCharacter* Character, bool bForce = false);
    // Server only: the driving player is leaving the game; put the character out so the car survives.
    void ServerDriverLeaving();

    // Rebuilds physics specs from the setup after parts or damage change it.
    UFUNCTION(BlueprintCallable, Category = "Rally|Vehicle")
    void ApplySetup();

    bool CopyPanelMesh(int32 Panel, UE::Geometry::FDynamicMesh3& OutMesh, TArray<UMaterialInterface*>& OutMaterials) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Setup")
    TArray<FRallyWheelSetup> WheelSetups;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Setup")
    FRallyEngineSetup EngineSetup;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Setup")
    FRallyGearboxSetup GearboxSetup;

    // N/m of compression difference across the axle. Roll stiffness about 52% front: 54% understeered,
    // 46% made the rear slide in every corner.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Setup")
    float FrontAntiRoll = 2500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Setup")
    float RearAntiRoll = 8000.0f;

    // Driver aids, fitted or removed as parts.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Setup")
    bool bABS = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Setup")
    bool bTractionControl = true;

    // Drag coefficient times frontal area, m^2.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Setup")
    float DragArea = 0.69f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Setup", meta = (Units = "Degrees"))
    float MaxSteerAngle = 32.0f;

protected:
    virtual void BeginPlay() override;
    virtual void PostInitializeComponents() override;

    UPROPERTY(VisibleAnywhere, Category = "Vehicle")
    TObjectPtr<UBoxComponent> Body;

    UPROPERTY(VisibleAnywhere, Category = "Vehicle")
    TObjectPtr<UStaticMeshComponent> ChassisMesh;

    UPROPERTY(VisibleAnywhere, Category = "Vehicle")
    TArray<TObjectPtr<UStaticMeshComponent>> WheelMeshes;

    // Each corner's hub, brake disc, strut, arms, driveshaft and tie rod, moved with its wheel.
    UPROPERTY(VisibleAnywhere, Category = "Vehicle")
    TArray<TObjectPtr<UStaticMeshComponent>> SuspensionMeshes;

    UPROPERTY(VisibleAnywhere, Category = "Vehicle")
    TObjectPtr<UStaticMeshComponent> SteeringWheelMesh;

    // Front doors, FL and FR; they swing open while someone gets in or out.
    UPROPERTY(VisibleAnywhere, Category = "Vehicle")
    TArray<TObjectPtr<UStaticMeshComponent>> FrontDoorMeshes;

    // Doors, bonnet, boot lid, radiator, bumpers, wings and lamps, in URallyPanelComponent's order.
    UPROPERTY(VisibleAnywhere, Category = "Vehicle")
    TArray<TObjectPtr<UStaticMeshComponent>> PanelMeshes;

    // Roof, windscreen, bumper bars, interior and engine: fixed, but each crushing its own way.
    UPROPERTY(VisibleAnywhere, Category = "Vehicle")
    TArray<TObjectPtr<UStaticMeshComponent>> BodyPartMeshes;

    // Middle pipe and silencer, hung from the front hanger: they droop and drag when torn off the rear ones.
    UPROPERTY(VisibleAnywhere, Category = "Vehicle")
    TObjectPtr<UStaticMeshComponent> ExhaustMesh;

    // Dents and scraped paint (docs/DAMAGE.md).
    UPROPERTY(VisibleAnywhere, Category = "Damage")
    TObjectPtr<URallyBodyDeformComponent> BodyDeform;

    UPROPERTY(VisibleAnywhere, Category = "Damage")
    TObjectPtr<URallyPanelComponent> Panels;

    // Tyre, suspension and radiator condition; drives handling symptoms.
    UPROPERTY(VisibleAnywhere, Category = "Damage")
    TObjectPtr<URallyDamageComponent> Damage;

    // Temperature, coolant, ignition; steam and smoke.
    UPROPERTY(VisibleAnywhere, Category = "Damage")
    TObjectPtr<URallyEngineComponent> Engine;

    // Steam and smoke puffs, in world space.
    UPROPERTY(VisibleAnywhere, Category = "Damage")
    TObjectPtr<UInstancedStaticMeshComponent> Puffs;

    // Procedural sound (docs/SOUND.md): engine and exhaust, tyres and wind, one-shot events.
    UPROPERTY(VisibleAnywhere, Category = "Sound")
    TObjectPtr<URallySynthComponent> EngineSound;

    UPROPERTY(VisibleAnywhere, Category = "Sound")
    TObjectPtr<URallySynthComponent> RoadSound;

    UPROPERTY(VisibleAnywhere, Category = "Sound")
    TObjectPtr<URallySynthComponent> EventSound;

    UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (Units = "KilometersPerHour"))
    float MaxEnterSpeedKmh = 5.0f;

    UPROPERTY(EditAnywhere, Category = "Vehicle", meta = (Units = "KilometersPerHour"))
    float MaxExitSpeedKmh = 15.0f;

    // Steering-wheel degrees per road-wheel degree; about 15.5 gives roughly 2.8 turns lock to lock.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Setup")
    float SteeringRatio = 15.5f;

    UPROPERTY(VisibleAnywhere, Category = "Camera")
    TObjectPtr<USpringArmComponent> CameraArm;

    UPROPERTY(VisibleAnywhere, Category = "Camera")
    TObjectPtr<UCameraComponent> Camera;

    // First-person view from the driver's eyes; the chase camera is a debug view (C to toggle).
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    TObjectPtr<UCameraComponent> CockpitCamera;

    UPROPERTY(EditAnywhere, Category = "Camera")
    float MouseLookSensitivity = 0.15f;

    UPROPERTY(EditAnywhere, Category = "Camera", meta = (Units = "Degrees"))
    float MaxLookYaw = 120.0f;

    // Seconds without mouse movement before the view drifts back to straight ahead.
    UPROPERTY(EditAnywhere, Category = "Camera", meta = (Units = "Seconds"))
    float LookRecentreDelay = 2.0f;

    // Per second, full lock to centre takes 1 / rate.
    UPROPERTY(EditAnywhere, Category = "Input")
    float SteerRate = 2.5f;

    UPROPERTY(EditAnywhere, Category = "Input")
    float SteerReturnRate = 4.0f;

    // Below this forward speed, holding brake selects reverse.
    UPROPERTY(EditAnywhere, Category = "Input")
    float ReverseEngageKmh = 3.0f;

    UPROPERTY(EditAnywhere, Category = "Input")
    float MaxReverseKmh = 30.0f;

    // Observers replay snapshots this far behind the newest one, to ride out network jitter.
    UPROPERTY(EditAnywhere, Category = "Network", meta = (Units = "Seconds"))
    float InterpolationDelay = 0.1f;

    UPROPERTY(EditAnywhere, Category = "Network")
    float StateSendRate = 30.0f;

    // Steering lock halves at this speed; 0 disables speed-sensitive steering.
    UPROPERTY(EditAnywhere, Category = "Input")
    float SteerSpeedFalloffKmh = 80.0f;

private:
    struct FWheelState
    {
        double Omega = 0.0;
        double SpinAngle = 0.0;
        double Compression = 0.0;
        double Load = 0.0;
        double SlipRatio = 0.0;
        double SlipAngleDeg = 0.0;
        double SteerDeg = 0.0;
        double OffsetCm = 0.0;
        // The differential's side of a broken driveshaft, spinning on its own.
        double StubOmega = 0.0;
        bool bContact = false;
    };

    // Copy handed from the physics thread to the game thread for visuals and readouts.
    struct FVisualState
    {
        TArray<FWheelState> Wheels;
        int32 Gear = 1;
        double EngineRpm = 0.0;
        // What the engine makes at the crank (kW): heats it (URallyEngineComponent).
        double EnginePowerKw = 0.0;
        // Share of full power asked of it, for the sound; a battery cut-out happening now.
        double EngineLoad = 0.0;
        bool bCutOut = false;
        // Road-wheel angle from the steering alone; wheel SteerDeg also holds toe.
        double SteerDeg = 0.0;
    };

    // Contacts from one collision arrive over several physics steps; they are summed into one impact.
    struct FPendingImpact
    {
        bool bActive = false;
        double StartTime = 0.0;
        double Impulse = 0.0;
        FVector WeightedPoint = FVector::ZeroVector;
        FVector WeightedDirection = FVector::ZeroVector;
        // Body surface in contact, car frame; invalid when the other shape could not be sampled.
        FBox Patch = FBox(ForceInit);
    };

    UFUNCTION()
    void OnBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

    // Simulating client to server: one impact, in the car's local frame. DeltaV in m/s.
    UFUNCTION(Server, Reliable)
    void ServerReportImpact(FVector_NetQuantize10 LocalPoint, FVector_NetQuantize10 LocalExtent, FVector_NetQuantizeNormal LocalDirection, float DeltaV);

    UFUNCTION(Server, Reliable)
    void ServerDebugDamage(bool bRepair);

    UFUNCTION()
    void OnDamageChanged();
    void OnEngineChanged();
    void OnStartFailed(ERallyStartFailure Reason);

    // Every machine but the one that felt it (which played it at once): a crash or a floor hit.
    // Kind 0 impact, 1 floor; Strength 0..255.
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastCrashSound(uint8 Kind, uint8 Strength);
    void PlayCrashSound(uint8 Kind, float DeltaV);
    void UpdateSounds(float DeltaSeconds, const FVisualState& Snapshot);
    int32 LastSoundGear = 1;
    bool bSoundWasRunning = false;
    float SurfaceLooseness = 0.0f;
    float SurfaceTraceTimer = 0.0f;

    // Driver pressed Ignition: switch the engine off, or try to start it.
    UFUNCTION(Server, Reliable)
    void ServerToggleIgnition();
    void OnIgnitionPressed(const FInputActionValue& Value);

    void FlushPendingImpact();
    // Server only: dent the body and damage parts the impact reaches.
    void ApplyImpact(const FVector& LocalPoint, const FVector& LocalExtent, const FVector& LocalDirection, float DeltaV);
    // Something pushing up on the floor (a rock, a crest, a hard landing): no dent, but the sump, gearbox,
    // exhaust and tank take what reaches them. DeltaV is the car's change of velocity, m/s.
    UFUNCTION(Server, Reliable)
    void ServerReportUnderside(FVector_NetQuantize10 LocalPoint, FVector_NetQuantize10 LocalExtent, float DeltaV);
    void FlushPendingUnderside();
    void ApplyUndersideHit(const FVector& LocalPoint, const FVector& LocalExtent, float DeltaV);
    // Part of the body box touching OtherComp, car frame; invalid if it cannot be measured.
    FBox FindContactPatch(const UPrimitiveComponent* OtherComp, const FVector& LocalDirection) const;
    FString DescribeDamage() const;
    void TickCrashTest(float DeltaSeconds);
    void TickPanelTest(float DeltaSeconds);
    UPROPERTY(Replicated) uint8 PanelTestPhase = 0;
    UPROPERTY(Transient) TObjectPtr<ARallyCharacter> PanelTestDriver;
    float PanelTestTimer = 0;
    int32 PanelTestDents = 0;
    double PanelTestHeight = 0.0;
    bool bPanelTestPositioned = false;
    UPROPERTY(Transient) TObjectPtr<AActor> PanelTestPost;
    void OnDebugDamage(const FInputActionValue& Value);
    void OnDebugRepair(const FInputActionValue& Value);

    UFUNCTION(Server, Unreliable)
    void ServerSubmitState(const FRallyCarNetState& State);

    // Driver pressed Interact: get out.
    UFUNCTION(Server, Reliable)
    void ServerRequestExit();

    void OnExitPressed(const FInputActionValue& Value);

    // Driver pressed Door: open or pull shut the driver's door. Doors never shut by themselves.
    UFUNCTION(Server, Reliable)
    void ServerToggleDriverDoor();

    void OnDoorPressed(const FInputActionValue& Value);
    void OpenDoorBriefly(int32 Seat);
    void UpdateDoors(float DeltaSeconds);
    bool FindExitLocation(const ARallyCharacter* Character, int32 Seat, FVector& OutLocation) const;

    UFUNCTION()
    void OnRep_NetState();

    void UpdateSimulationRole();
    double PhysicsTime() const;
    void SendState(float DeltaSeconds);
    void AddSnapshot(const FRallyCarNetState& State);
    void TickObserved(float DeltaSeconds);

    void CreateInputObjects();
    void OnThrottle(const FInputActionValue& Value);
    void OnBrake(const FInputActionValue& Value);
    void OnSteer(const FInputActionValue& Value);
    void OnHandbrake(const FInputActionValue& Value);
    void OnReset(const FInputActionValue& Value);
    void OnMouseLook(const FInputActionValue& Value);
    void OnStickLook(const FInputActionValue& Value);
    void OnToggleCamera(const FInputActionValue& Value);
    void SetCockpitView(bool bCockpit);
    void UpdateLook(float DeltaSeconds);
    void TickAutoDrive(float DeltaSeconds);
    void TickNetLog(float DeltaSeconds);
    FRallyCarInput BuildDriverInput(float DeltaSeconds);
    void UpdateWheelVisuals(float DeltaSeconds);
    void SimulatePhysicsStep(Chaos::FRigidBodyHandle_Internal& Handle, const FRallyCarInput& Input, double Dt);

    // Latest state from the simulating machine; the driver does not receive its own.
    UPROPERTY(ReplicatedUsing = OnRep_NetState)
    FRallyCarNetState NetState;

    bool bSimulatesHere = true;
    TArray<FRallyCarNetState> Snapshots;
    double PlayoutTime = -1.0;
    FVector ObservedVelocity = FVector::ZeroVector;
    FVector ObservedAngularVelocity = FVector::ZeroVector;
    float StateSendTimer = 0.0f;
    uint8 ResetCount = 0;

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> DrivingContext;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ThrottleAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> BrakeAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SteerAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> HandbrakeAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ResetAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MouseLookAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> StickLookAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ToggleCameraAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ExitAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> DoorAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> IgnitionAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> DebugDamageAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> DebugRepairAction;

    // Setup before damage effects; WheelSetups is rebuilt from it whenever part condition changes.
    TArray<FRallyWheelSetup> BaseWheelSetups;
    FPendingImpact PendingImpact;
    FPendingImpact PendingUnderside;
    TArray<double> RecentImpactTimes;

    UPROPERTY(Replicated)
    TArray<TObjectPtr<ARallyCharacter>> Occupants;

    // Bit per front door, set by the server while someone gets in or out.
    UPROPERTY(Replicated)
    uint8 DoorsOpen = 0;

    double DoorCloseTime[SeatCount] = { 0.0, 0.0 };
    float DoorAngle[SeatCount] = { 0.0f, 0.0f };

    // Physics-thread data; specs are rebuilt by ApplySetup under the lock.
    FCriticalSection PhysicsLock;
    FRallyCarInput PhysicsInput;
    TArray<FRallyWheelSetup> PhysicsWheels;
    TArray<Rally::Tyre::FSpec> TyreSpecs;
    Rally::Drivetrain::FEngineSpec EngineSpec;
    Rally::Drivetrain::FGearboxSpec GearboxSpec;
    Rally::Drivetrain::FShiftState Shift;
    TArray<FWheelState> Wheels;
    double EngineOmega = 0.0;
    double PhysicsFrontAntiRoll = 0.0;
    int32 PhysicsJackCorner = -1;
    // Height the jack has wound the corner to so far (m), -1 before it starts.
    double PhysicsJackTarget = -1.0;
    double PhysicsRearAntiRoll = 0.0;
    double PhysicsDragArea = 0.0;
    double PhysicsMaxSteer = 0.0;
    bool bPhysicsSimulates = true;
    bool bPhysicsABS = false;
    bool bPhysicsTractionControl = false;
    // Engine damage and electrics (URallyEngineComponent): running, share of torque, battery health, and
    // how long the current cut-out from a bad battery connection has left (s).
    bool bPhysicsEngineRunning = true;
    double PhysicsTorqueScale = 1.0;
    double PhysicsBatteryHealth = 1.0;
    double CutOutLeft = 0.0;
    FRandomStream CutOutRandom;
    // The starter turning the engine over before it catches (s), and whether it was running last step.
    double CrankLeft = 0.0;
    bool bPhysicsWasRunning = false;
    // Gearbox health for jumping out of gear, and a torn exhaust dragging its silencer.
    double PhysicsGearboxHealth = 1.0;
    bool bPhysicsExhaustDragging = false;
    bool bResetWheelState = false;
    FVisualState Visual;
    TArray<float> VisualOffsetCm;
    TArray<float> VisualSpin;

    float ThrottleInput = 0.0f;
    float BrakeInput = 0.0f;
    float SteerTarget = 0.0f;
    float SteerCurrent = 0.0f;
    bool bHandbrake = false;
    bool bReversing = false;

    bool bCockpitView = true;
    FVector2D MouseLook = FVector2D::ZeroVector;
    FVector2D StickLook = FVector2D::ZeroVector;
    float MouseIdleTime = 0.0f;

    // -RallyAutoDrive: scripted input with log output, then quit. For unattended handling checks.
    bool bAutoDrive = false;
    bool bAutoDriveDecided = false;
    bool bNetLog = false;
    float NetLogTimer = 0.0f;
    float AutoDriveTime = 0.0f;
    float NextAutoDriveLog = 0.0f;
    FVector AutoDriveStart = FVector::ZeroVector;
    const TCHAR* AutoDrivePhase = nullptr;
    FVector AutoDrivePhaseStart = FVector::ZeroVector;
    float AutoDrivePhaseKmh = 0.0f;
    bool bAutoDriveStopLogged = false;
    int32 AutoDriveShotStep = 0;
    int32 AutoDriveShotFrames = 0;

    // -RallyCrashTest: drive into the impact barrier, back off, photograph the damage, then quit.
    bool bCrashTest = false;
    float CrashTestTime = 0.0f;
    float CrashImpactTime = -1.0f;
    float CrashImpactKmh = 0.0f;
    float CrashReverseYaw = 0.0f;
    bool bCrashReverseStarted = false;
    int32 CrashShotStep = 0;
    int32 CrashShotFrames = 0;
    float CrashDebrisTime = 0.0f;
    // -RallyEngineTest (with the crash test): after the barrier hit, drive circles on the leaking radiator
    // until the engine overheats, then stop and top it up.
    bool bEngineTest = false;
    float EngineTestStart = -1.0f;
    float EngineTestStopped = -1.0f;
    float NextEngineTestLog = 0.0f;
    double EngineTestMaxTemp = 0.0;
    double EngineTestMinCoolant = 99.0;
    double EngineTestTempAtOff = 0.0;
    int32 EngineTestMaxPuffs = 0;
    int32 EngineTestStep = 0;
    void TickEngineTest(float Since);
    // -RallyUndersideTest (with -RallyAutoSeat=driver -RallyAutoDrive): drive over a ridge of rock between
    // the wheels, then idle and watch what leaks.
    bool bUndersideTest = false;
    float UndersideTestTime = 0.0f;
    int32 UndersideTestStep = 0;
    float UndersideTestMark = 0.0f;
    double UndersideOilAtStop = 0.0;
    double UndersideFuelAtStop = 0.0;
    FVector UndersideRockAt = FVector::ZeroVector;
    int32 UndersideHits = 0;
    float UndersideMaxDeltaV = 0.0f;
    void TickUndersideTest(float DeltaSeconds);
};
