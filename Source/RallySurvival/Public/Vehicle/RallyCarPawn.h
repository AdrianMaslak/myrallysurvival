#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HAL/CriticalSection.h"
#include "Vehicle/RallyVehicleSetup.h"
#include "RallyCarPawn.generated.h"

class UBoxComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;
namespace Chaos { class FRigidBodyHandle_Internal; }

// Octavia prototype with project-owned vehicle physics (docs/VEHICLE-PHYSICS.md).
// The box root is the only physics body. Each fixed physics step, on every machine, the car
// raycasts four wheels, applies spring-damper suspension, anti-roll bars and tyre forces, and
// integrates engine, clutch, gearbox and an open front differential.
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

    // Signed: negative while rolling backwards.
    UFUNCTION(BlueprintPure, Category = "Rally|Vehicle")
    float GetForwardSpeedKmh() const;

    UFUNCTION(BlueprintCallable, Category = "Rally|Vehicle")
    void ResetUpright();

    // Rebuilds physics specs from the setup after parts or damage change it.
    UFUNCTION(BlueprintCallable, Category = "Rally|Vehicle")
    void ApplySetup();

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
        bool bContact = false;
    };

    // Copy handed from the physics thread to the game thread for visuals and readouts.
    struct FVisualState
    {
        TArray<FWheelState> Wheels;
        int32 Gear = 1;
        double EngineRpm = 0.0;
    };

    UFUNCTION(Server, Reliable)
    void ServerResetUpright();

    UFUNCTION(Server, Unreliable)
    void ServerSetInput(const FRallyCarInput& Input);

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
    FRallyCarInput BuildDriverInput(float DeltaSeconds);
    void UpdateWheelVisuals(float DeltaSeconds);
    void SimulatePhysicsStep(Chaos::FRigidBodyHandle_Internal& Handle, const FRallyCarInput& Input, double Dt);

    UPROPERTY(Replicated)
    FRallyCarInput ReplicatedInput;

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
    double PhysicsRearAntiRoll = 0.0;
    double PhysicsDragArea = 0.0;
    double PhysicsMaxSteer = 0.0;
    bool bPhysicsABS = false;
    bool bPhysicsTractionControl = false;
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
    float AutoDriveTime = 0.0f;
    float NextAutoDriveLog = 0.0f;
    FVector AutoDriveStart = FVector::ZeroVector;
    const TCHAR* AutoDrivePhase = nullptr;
    FVector AutoDrivePhaseStart = FVector::ZeroVector;
    float AutoDrivePhaseKmh = 0.0f;
    bool bAutoDriveStopLogged = false;
};
