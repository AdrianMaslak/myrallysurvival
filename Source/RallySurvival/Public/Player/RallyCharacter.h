#pragma once

#include "CoreMinimal.h"
#include "Damage/FixingRules.h"
#include "GameFramework/Character.h"
#include "RallyCharacter.generated.h"

class ARallyCarPawn;
class ARallyDebrisActor;
class URallyPanelComponent;
class URallySynthComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UStaticMeshComponent;
struct FInputActionValue;

// First-person crew member on foot. Walks to a car door and presses Interact to take a seat:
// the driver hands control to the car pawn; the co-driver stays this pawn, fixed in the seat with
// free look. The server decides every seat change (ARallyCarPawn::ServerEnterSeat / ServerExitSeat).
UCLASS()
class RALLYSURVIVAL_API ARallyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ARallyCharacter();

    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void NotifyControllerChanged() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

    // Server only: attach to or detach from a seat. ExitLocation is used when leaving.
    void SetSeat(ARallyCarPawn* Car, int32 Seat);
    void ClearSeat(const FVector& ExitLocation, const FRotator& ExitRotation);

    ARallyDebrisActor* GetCarriedPart() const { return CarriedPart; }
    void SetCarriedPart(ARallyDebrisActor* Part);
    bool ConsumeStrap();
    bool ConsumeSealant();
    uint8 GetSealant() const { return Sealant; }
    bool ConsumePutty();
    uint8 GetPutty() const { return Putty; }
    bool HasSpanner() const { return bHasSpanner; }
    // The tool in hand (Rally::Fixing): 1-8 or the mouse wheel pick it from the toolbox.
    Rally::Fixing::ETool GetHeldTool() const { return static_cast<Rally::Fixing::ETool>(HeldTool); }
    // Server only: one fixing turned now, if the last was at least Rally::Fixing::MinTurnInterval ago.
    bool AdvanceFixingTurn();
    uint8 GetStrapCount() const { return Straps; }
    bool AdvancePanelRefit();
    // How rushed the bolt actions of the current refit were: 1 as fast as allowed, 0 unhurried.
    double GetRefitRush() const;
    // Server only: one action towards taking Part off Car; true on the last of Required. Actions keep the
    // bolting pace (0.75 s apart) and start over after four idle seconds or on another part.
    bool AdvanceRemoval(const AActor* Car, int32 Part, int32 Required);
    // Client: open or shut, or take off, whatever is in view and reach (F, R).
    void RequestTogglePanel();
    void RequestRemovePart();
    void RequestBodge();

    ARallyCarPawn* GetSeatedCar() const { return SeatedCar; }
    // Something in view and reach can be used or picked up (the crosshair turns yellow).
    bool HasInteractionInView() const { return bInteractionInView; }
    int32 GetSeatIndex() const { return SeatIndex; }

protected:
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    // Placeholder body other players see.
    UPROPERTY(VisibleAnywhere, Category = "Body")
    TObjectPtr<UStaticMeshComponent> BodyMesh;

    // The countryside around this player (wind, birds), heard only by them (docs/SOUND.md).
    UPROPERTY(VisibleAnywhere, Category = "Sound")
    TObjectPtr<URallySynthComponent> Ambience;

    UPROPERTY(EditAnywhere, Category = "Movement")
    float WalkSpeed = 350.0f;

    UPROPERTY(EditAnywhere, Category = "Movement")
    float SprintSpeed = 600.0f;

    UPROPERTY(EditAnywhere, Category = "Camera")
    float MouseSensitivity = 0.15f;

private:
    UFUNCTION(Server, Reliable)
    void ServerInteract(ARallyDebrisActor* TargetPart);

    UFUNCTION(Server, Reliable) void ServerBodgePanel(ARallyCarPawn* Car, int32 Panel);
    // The client names the part it chose (what it looks at); the server checks reach and state.
    UFUNCTION(Server, Reliable) void ServerTogglePanel(ARallyCarPawn* Car, int32 Panel);
    UFUNCTION(Server, Reliable) void ServerRemovePart(ARallyCarPawn* Car, int32 Part);
    UFUNCTION(Server, Reliable) void ServerDropPart();
    UFUNCTION(Server, Reliable) void ServerSelectTool(uint8 Tool);
    // Held long enough on a fixing: turn it home (E) or out (R). The server checks reach, tool and pace.
    UFUNCTION(Server, Reliable) void ServerTurnFixing(ARallyCarPawn* Car, int32 Part, int32 Fixing, bool bHome);
    void SelectTool(int32 Tool);
    void OnToolKey(const FInputActionValue& Value);
    void OnToolWheel(const FInputActionValue& Value);
    void OnInteractReleased(const FInputActionValue& Value);
    void OnRemoveReleased(const FInputActionValue& Value);
    // Holding E or R on a fixing: which, and for how long.
    void UpdateFixingHold(float DeltaSeconds);
    UPROPERTY(Replicated) uint8 HeldTool = static_cast<uint8>(Rally::Fixing::ETool::Hands);
    double LastFixingTurn = -10.0;
    bool bHoldHome = false;
    bool bHoldOut = false;
    float HoldTime = 0.0f;
    TWeakObjectPtr<URallyPanelComponent> HoldPanels;
    int32 HoldPart = INDEX_NONE;
    int32 HoldFixing = INDEX_NONE;
    double HoldSentTime = -10.0;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ToolKeyAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ToolWheelAction;
    UFUNCTION() void OnRep_CarriedPart();
    // What the crosshair is on, within reach: a part of a car (its slot) or a loose part. Nothing else counts:
    // actions act on exactly what is in the middle of the view.
    struct FRallyAim
    {
        URallyPanelComponent* Panels = nullptr;
        int32 Slot = INDEX_NONE;
        // A fixing of that part, if the crosshair is on one.
        int32 Fixing = INDEX_NONE;
        ARallyDebrisActor* Debris = nullptr;
    };
    FRallyAim FindAim() const;
    // A carried part: where it would be fitted, else its own place.
    URallyPanelComponent* FindFitForCarried(int32& OutPanel) const;
    void OnBodge(const FInputActionValue& Value);
    void OnDrop(const FInputActionValue& Value);
    void OnOpenPanel(const FInputActionValue& Value);
    void OnRemovePart(const FInputActionValue& Value);
    UPROPERTY(ReplicatedUsing=OnRep_CarriedPart) TObjectPtr<ARallyDebrisActor> CarriedPart;
    // Temporary field kit until the inventory milestone: a spanner, two consumable straps, a tin of
    // radiator sealant and two patches of epoxy putty (sump, fuel tank).
    UPROPERTY(Replicated) bool bHasSpanner = true;
    UPROPERTY(Replicated) uint8 Straps = 2;
    UPROPERTY(Replicated) uint8 Sealant = 1;
    UPROPERTY(Replicated) uint8 Putty = 2;
    UPROPERTY(Replicated) uint8 RefitActions = 0;
    double LastRefitTime = -10.0;
    double FirstRefitTime = -10.0;
    UPROPERTY(Replicated) uint8 RemoveActions = 0;
    double LastRemoveTime = -10.0;
    TWeakObjectPtr<const AActor> RemoveCar;
    int32 RemovePart = INDEX_NONE;
    bool bSprintRequested = false;
    bool bInteractionInView = false;
    bool bAmbienceStarted = false;
    UPROPERTY(Transient) TObjectPtr<UInputAction> BodgeAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> DropAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> OpenPanelAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> RemoveAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> CrouchAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> LeanAction;
    void OnCrouchPressed(const FInputActionValue& Value);
    void OnCrouchReleased(const FInputActionValue& Value);
    void OnLean(const FInputActionValue& Value);
    // Head: leaning (hold Q: forward, or with A, D, S to the left, right or back; the feet stay put) and
    // easing down or up when crouching instead of jumping.
    void UpdateHead(float DeltaSeconds);
    bool bLeanHeld = false;
    // Movement keys while Q is held: X right, Y forward.
    FVector2D LeanStick = FVector2D::ZeroVector;
    // Current lean, X forward and Y right, -1..1.
    FVector2D Lean = FVector2D::ZeroVector;
    float CrouchEyeOffset = 0.0f;

    UFUNCTION(Server, Reliable)
    void ServerSetSprint(bool bSprint);

    UFUNCTION()
    void OnRep_Seat();

    void ApplySeatState();
    void CreateInputObjects();
    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);
    void OnStickLook(const FInputActionValue& Value);
    void OnJump(const FInputActionValue& Value);
    void OnInteract(const FInputActionValue& Value);
    void OnSprint(const FInputActionValue& Value);
    void UpdatePrompt();
    void TickAutoSeat(float DeltaSeconds);
    void TickPanelTest(float DeltaSeconds);
    float PanelTestActionTimer = 0;
    bool bPanelTestSawCarry = false;
    bool bPanelTestSawRefit = false;
    void SetSprint(bool bSprint);

    UPROPERTY(ReplicatedUsing = OnRep_Seat)
    TObjectPtr<ARallyCarPawn> SeatedCar;

    UPROPERTY(ReplicatedUsing = OnRep_Seat)
    int8 SeatIndex = -1;

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> OnFootContext;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MouseLookAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> StickLookAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> InteractAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SprintAction;

    // Seated look, relative to the car.
    FRotator SeatLook = FRotator::ZeroRotator;
    FVector2D StickLook = FVector2D::ZeroVector;
    bool bAutoSeatDone = false;
    int32 AutoSeatWaypoint = 0;
    float AutoSeatShotTimer = -1.0f;
};
