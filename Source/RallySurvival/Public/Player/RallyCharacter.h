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
    // Server: one more fixing turned, if the last was at least Interval ago (seconds).
    bool AdvanceFixingTurn(double Interval = Rally::Fixing::MinTurnInterval);
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
    // Pocket money (docs/MONEY.md); server changes it.
    int32 GetPocketSk() const { return PocketSk; }
    void AddPocketSk(int32 Sk) { if (HasAuthority()) { PocketSk = FMath::Max(PocketSk + Sk, 0); ForceNetUpdate(); } }
    // The parts catalogue on screen (K), and the line picked (docs/MONEY.md).
    bool IsCatalogueOpen() const { return bCatalogueOpen; }
    int32 GetCatalogueIndex() const { return CatalogueIndex; }
    // Hurt in a crash, 0..1 (docs/BODY.md): sight darkens and blurs at the edges, walking slows, until
    // treated with the first-aid kit. Server sets it.
    double GetInjury() const { return Injury; }
    // Cold, 0..1 (docs/BODY.md): builds outside at night, gone by the heater; slows the hands.
    double GetCold() const { return Cold; }
    // Wet, 0..1: out in the rain; makes the air feel colder; the car's heater dries you.
    double GetWet() const { return Wet; }
    // Drunk 0..1 and calm 0..1 (docs/BODY.md).
    double GetDrunk() const { return Drunk; }
    // Test harness, server only.
    void DebugSetDrunk(float InDrunk) { if (HasAuthority()) { Drunk = FMath::Clamp(InDrunk, 0.0f, 1.0f); ApplyInjury(); } }
    double GetCalm() const { return Calm; }
    // Server only: a swig from the carried bottle or a cigarette from the carried carton (B); true if so.
    bool UseCarriedVice();
    void AddInjury(double Amount);
    // Server only: the carried first-aid kit used on this player; true if it helped.
    bool TreatWithFirstAid();
    // Asleep in a seat, waiting for everyone else to sleep too (docs/DAYS.md). Server sets it.
    // How hurt, drunk and cold look (darkened, fringed, blurred, blue), written into a camera's settings:
    // this player's own camera on foot, the car's cameras while they drive (ARallyCarPawn::Tick).
    void ApplyBodyView(FPostProcessSettings& Post) const;
    bool IsSleeping() const { return bSleeping; }
    // Asleep till the rally weekend (Shift+Z): when everyone sleeps so, the weekdays pass too.
    bool IsSleepingTillRally() const { return bSleeping && bSleepTillRally; }
    void SetSleeping(bool bSleep, bool bTillRally = false);
    // Something in view and reach can be used or picked up (the crosshair turns yellow).
    bool HasInteractionInView() const { return bInteractionInView; }
    // What the HUD shows under the crosshair this frame: what can be done (yellow), or else just the name of
    // what is in view (white); and the tool in hand.
    const FString& GetPromptText() const { return PromptText; }
    const FString& GetAimedText() const { return AimedText; }
    FString GetToolText() const;
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
    // E held on the throttle in the bay: sent a few times a second while it is held.
    UFUNCTION(Server, Unreliable) void ServerHandRev(ARallyCarPawn* Car);
    // I on foot: reach in at the driver's door and turn the key.
    UFUNCTION(Server, Reliable) void ServerTurnKey();
    void OnIgnition(const FInputActionValue& Value);
    bool bHoldThrottle = false;
    double HandRevSentTime = -10.0;
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
    UPROPERTY(Transient) TObjectPtr<UInputAction> AdjustAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> SleepAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> CatalogueAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> HelpAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> WorkshopAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> MoneyAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> CupAction;
    void OnCup(const FInputActionValue& Value);
    UPROPERTY(Replicated) int32 PocketSk = 500;
    void OnMoney(const FInputActionValue& Value);
    UFUNCTION(Server, Reliable) void ServerMoveMoney(int32 Sk);
    void OnWorkshop(const FInputActionValue& Value);
    UFUNCTION(Server, Reliable) void ServerWorkshop(ARallyCarPawn* Car, int32 Part);
    void OnHelp(const FInputActionValue& Value);
    UPROPERTY(Transient) TObjectPtr<UInputAction> ConfirmAction;
    bool bCatalogueOpen = false;
    int32 CatalogueIndex = 0;
    void OnCatalogue(const FInputActionValue& Value);
    void OnConfirm(const FInputActionValue& Value);
    UFUNCTION(Server, Reliable) void ServerOrder(int32 Item);
    UPROPERTY(Replicated) bool bSleeping = false;
    UPROPERTY(Replicated) bool bSleepTillRally = false;
    UPROPERTY(ReplicatedUsing = OnRep_Injury) float Injury = 0.0f;
    UPROPERTY(ReplicatedUsing = OnRep_Injury) float Cold = 0.0f;
    UPROPERTY(Replicated) float Wet = 0.0f;
    UPROPERTY(ReplicatedUsing = OnRep_Injury) float Drunk = 0.0f;
    UPROPERTY(ReplicatedUsing = OnRep_Injury) float Calm = 0.0f;
    double NextBodyTick = 0.0;
    // Server: the night's cold, once a second.
    void TickBody();
    // Server: hurt too badly to go on.
    void TakeToHospital();
    UFUNCTION() void OnRep_Injury();
    void ApplyInjury();
    void OnSleep(const FInputActionValue& Value);
    UFUNCTION(Server, Reliable) void ServerToggleSleep(bool bTillRally);
    void OnAdjust(const FInputActionValue& Value);
    UFUNCTION(Server, Reliable) void ServerAdjust(ARallyCarPawn* Car, int32 Part, int32 Step);
    UFUNCTION(Server, Reliable) void ServerSpawnPart(int32 Slot, uint8 Grade);
public:
    // Development: RallyPart <strut|rearstrut|disc|reardisc|turbo|ecu|intercooler> <worn|stock|sport|rally>
    // puts that part in front of you until there are shops.
    UFUNCTION(Exec) void RallyPart(const FString& Kind, const FString& Grade);
    // Development: save the team now, or load the last save (docs/SAVING.md); the host's world.
    UFUNCTION(Exec) void RallySave();
    UFUNCTION(Exec) void RallyLoad();
    // Development: RallyRain <0..1>, or -1 for the weather's own.
    UFUNCTION(Exec) void RallyRain(float Rain);
    // Development: RallyLeak <oil|coolant|fuel|all> [health 0..1, default 0.2] cracks the sump, holes the
    // radiator or the tank of the car you are in or nearest you, so it leaks (1 mends it).
    UFUNCTION(Exec) void RallyLeak(const FString& Kind, float Health = 0.2f);
    // Development: the nearby car's ignition coil 1-4 to a health (0 dead, 0.3 misfiring now and then).
    UFUNCTION(Exec) void RallyCoil(int32 Coil, float Health = 0.0f);
    UFUNCTION(Server, Reliable) void ServerCoil(int32 Coil, float Health);
private:
    UFUNCTION(Server, Reliable) void ServerSaveLoad(bool bSave);
    UFUNCTION(Server, Reliable) void ServerRain(float Rain);
    UFUNCTION(Server, Reliable) void ServerLeak(uint8 Which, float Health);
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
    FString PromptText;
    FString AimedText;
    // The car whose bolts are lit by this player's aim (URallyPanelComponent::SetAimHighlight).
    TWeakObjectPtr<URallyPanelComponent> LitPanels;
    bool bAmbienceStarted = false;
    UPROPERTY(Transient) TObjectPtr<UInputAction> BodgeAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> DropAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> OpenPanelAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> RemoveAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> CrouchAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> LeanAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> IgnitionAction;
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
    // -RallyLeanTest: crouched beside the front left wheel, how far the eyes lean in past the wing and which
    // fixings of the corner can be aimed at standing straight and leaning; screenshots of both.
    void TickLeanTest(float DeltaSeconds);
    // -RallyIdleTest: at the driver's door the key is turned (I) with nobody in the car; the engine idles
    // where it stands; at the open bonnet E is held on the throttle (it revs and rocks), let go (it settles),
    // and the key turned off again from the door; made hot, the radiator fans come on. With the cover off a
    // good coil is unplugged (the idle drops), coil 3 killed and unplugged (no change: the bad one), and a
    // sport coil from the catalogue fitted in its place. Screenshots RallyIdle_revving.png, RallyIdle_fans.png,
    // RallyIdle_coils.png.
    void TickIdleTest(float DeltaSeconds);
    float IdleTestTime = 0.0f;
    int32 IdleTestStep = 0;
    double IdleTestIdleRpm = 0.0;
    double IdleTestMaxRpm = 0.0;
    double IdleTestMaxSpeed = 0.0;
    double IdleTestSettledRpm = 0.0;
    double IdleTestFanTurns = 0.0;
    double IdleTestBeltStart = -1.0;
    double IdleTestBeltRun = 0.0;
    double IdleTestCoilRpm[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    bool IdleTestCarried = false;
    bool IdleTestCoilSport = false;
    double IdleTestRollBefore = 0.0;
    FString IdleTestPrompt;
    FString IdleTestDoorPrompt;
    float LeanTestTime = 0.0f;
    int32 LeanTestStep = 0;
    int32 LeanTestReachedStraight = 0;
    // -RallyBootTest: behind the car with its boot open, the first-aid kit taken out and G pressed looking
    // into the boot (it must go back into its own place), then taken out again and G pressed looking away
    // (it must drop beside the car).
    void TickBootTest(float DeltaSeconds);
    float BootTestTime = 0.0f;
    int32 BootTestStep = 0;
    bool bBootTestBack = false;
    FString BootTestPrompt;
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
