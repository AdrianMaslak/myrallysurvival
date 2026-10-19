#include "Player/RallyCharacter.h"
#include "Game/RallyGameMode.h"
#include "Game/RallyHUD.h"
#include "Game/RallyDayClock.h"
#include "Damage/RallyDamageComponent.h"
#include "Damage/RallyEngineComponent.h"
#include "Player/BodyRules.h"
#include "Game/EconomyRules.h"
#include "Game/RallyTeamState.h"
#include "Audio/RallySynthComponent.h"
#include "AudioDevice.h"
#include "Misc/App.h"
#include "Damage/RallyDebrisActor.h"
#include "Damage/RallyPanelComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "Vehicle/RallyCarPawn.h"

namespace
{
    UInputAction* MakeAction(UObject* Outer, EInputActionValueType Type)
    {
        UInputAction* Action = NewObject<UInputAction>(Outer);
        Action->ValueType = Type;
        return Action;
    }

    const float EyeHeight = 64.0f;
}

ARallyCharacter::ARallyCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34.0f, 88.0f);
    bUseControllerRotationYaw = true;
    BaseEyeHeight = EyeHeight;

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->SetRelativeLocation(FVector(0.0, 0.0, EyeHeight));
    FirstPersonCamera->bUsePawnControlRotation = true;
    FirstPersonCamera->SetFieldOfView(85.0f);

    Ambience = CreateDefaultSubobject<URallySynthComponent>(TEXT("Ambience"));
    Ambience->SetupAttachment(GetRootComponent());
    Ambience->Configure(ERallySoundKind::Ambience, 1.0f);
    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    BodyMesh->SetupAttachment(GetCapsuleComponent());
    BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BodyMesh->SetOwnerNoSee(true);
    BodyMesh->SetRelativeScale3D(FVector(0.55, 0.55, 1.76));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (Cylinder.Succeeded())
    {
        BodyMesh->SetStaticMesh(Cylinder.Object);
    }

    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    GetCharacterMovement()->JumpZVelocity = 380.0f;
    // Crouching (to look and reach under a car): eyes about a metre off the ground, at a creep.
    GetCharacterMovement()->NavAgentProps.bCanCrouch = true;
    GetCharacterMovement()->SetCrouchedHalfHeight(40.0f);
    GetCharacterMovement()->MaxWalkSpeedCrouched = 150.0f;
    CrouchedEyeHeight = EyeHeight;
}

void ARallyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyCharacter, SeatedCar);
    DOREPLIFETIME(ARallyCharacter, SeatIndex);
    DOREPLIFETIME(ARallyCharacter, CarriedPart);
    DOREPLIFETIME(ARallyCharacter, bHasSpanner);
    DOREPLIFETIME(ARallyCharacter, Straps);
    DOREPLIFETIME(ARallyCharacter, Sealant);
    DOREPLIFETIME(ARallyCharacter, Putty);
    DOREPLIFETIME(ARallyCharacter, RefitActions);
    DOREPLIFETIME(ARallyCharacter, HeldTool);
    DOREPLIFETIME(ARallyCharacter, bSleeping);
    DOREPLIFETIME(ARallyCharacter, bSleepTillRally);
    DOREPLIFETIME(ARallyCharacter, Injury);
    DOREPLIFETIME(ARallyCharacter, Cold);
    DOREPLIFETIME(ARallyCharacter, Wet);
    DOREPLIFETIME(ARallyCharacter, Drunk);
    DOREPLIFETIME(ARallyCharacter, Calm);
    DOREPLIFETIME(ARallyCharacter, PocketSk);
    DOREPLIFETIME(ARallyCharacter, RemoveActions);
}

void ARallyCharacter::SetSeat(ARallyCarPawn* Car, int32 Seat)
{
    // Seated at full height: the seat places the eyes from the standing capsule.
    UnCrouch();
    SeatedCar = Car;
    SeatIndex = static_cast<int8>(Seat);
    ApplySeatState();
}

void ARallyCharacter::ClearSeat(const FVector& ExitLocation, const FRotator& ExitRotation)
{
    bSleeping = false;
    SeatedCar = nullptr;
    SeatIndex = -1;
    ApplySeatState();
    SetActorLocationAndRotation(ExitLocation, FRotator(0.0, ExitRotation.Yaw, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
    if (AController* PlayerController = GetController())
    {
        PlayerController->SetControlRotation(FRotator(0.0, ExitRotation.Yaw, 0.0));
    }
}

void ARallyCharacter::OnRep_Seat()
{
    ApplySeatState();
}

void ARallyCharacter::ApplySeatState()
{
    const bool bSeated = SeatedCar != nullptr && SeatIndex >= 0;
    if (bSeated)
    {
        GetCharacterMovement()->DisableMovement();
        SetActorEnableCollision(false);
        // Each machine attaches to its own copy of the car, so the seat follows that machine's view.
        AttachToComponent(SeatedCar->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        SetActorRelativeLocation(SeatedCar->GetSeatEyeLocation(SeatIndex) - FVector(0.0, 0.0, EyeHeight));
        SetActorRelativeRotation(FRotator::ZeroRotator);
        if (HasAuthority())
        {
            SetReplicatingMovement(false);
        }
        bUseControllerRotationYaw = false;
        FirstPersonCamera->bUsePawnControlRotation = false;
        FirstPersonCamera->SetRelativeLocation(FVector(0.0, 0.0, EyeHeight));
        Lean = FVector2D::ZeroVector;
        CrouchEyeOffset = 0.0f;
        SeatLook = FRotator::ZeroRotator;
        // Bodies are hidden while seated until there is a seated pose.
        BodyMesh->SetHiddenInGame(true);
    }
    else
    {
        if (GetAttachParentActor())
        {
            DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        }
        SetActorEnableCollision(true);
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        if (HasAuthority())
        {
            SetReplicatingMovement(true);
        }
        bUseControllerRotationYaw = true;
        FirstPersonCamera->bUsePawnControlRotation = true;
        FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
        BodyMesh->SetHiddenInGame(false);
    }
}

void ARallyCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority()) TickBody();
    if (HasAuthority() && RefitActions > 0 && (!CarriedPart || !CarriedPart->GetSourceCar()
        || CarriedPart->GetSourceCar()->GetPanels()->FitTarget(this, CarriedPart) == INDEX_NONE
        || GetWorld()->GetTimeSeconds() - LastRefitTime > 4.0)) RefitActions = 0;
    if (!IsLocallyControlled())
    {
        return;
    }
    // Once, if this machine has sound; it stays on while this player drives (the car takes the controller,
    // this pawn rides along).
    if (!bAmbienceStarted)
    {
        bAmbienceStarted = true;
        if (GetWorld()->GetAudioDeviceRaw() && FApp::CanEverRenderAudio()) Ambience->Start();
    }
    // Rain round this player, drumming on the roof once they sit in a car.
    if (const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld()))
        Ambience->SetWeather(static_cast<float>(Clock->GetRain()), SeatedCar ? 1.0f : 0.0f);
    UpdateHead(DeltaSeconds);
    if (SeatedCar)
    {
        // The stick points the head; mouse look accumulates.
        const FRotator Look(FMath::Clamp(SeatLook.Pitch + StickLook.Y * 40.0f, -60.0f, 40.0f),
            FMath::Clamp(SeatLook.Yaw + StickLook.X * 120.0f, -150.0f, 150.0f), 0.0f);
        FirstPersonCamera->SetRelativeRotation(Look);
    }
    TickPanelTest(DeltaSeconds);
    TickLeanTest(DeltaSeconds);
    TickBootTest(DeltaSeconds);
    TickAutoSeat(DeltaSeconds);
    UpdateFixingHold(DeltaSeconds);
    UpdatePrompt();
}

FString ARallyCharacter::GetToolText() const
{
    return FString::Printf(TEXT("In hand: %s   (1-8 or mouse wheel)"), UTF8_TO_TCHAR(Rally::Fixing::ToolName(GetHeldTool())));
}

void ARallyCharacter::UpdatePrompt()
{
    bInteractionInView = false;
    PromptText.Reset();
    AimedText.Reset();
    if (!IsLocallyControlled() && !(SeatedCar && SeatedCar->IsLocallyControlled())) return;
    // Nothing lit while seated or carrying a part.
    if ((SeatedCar || CarriedPart) && LitPanels.Get())
    {
        LitPanels->SetAimHighlight(INDEX_NONE, INDEX_NONE);
        LitPanels = nullptr;
    }
    FString Prompt;
    if (SeatedCar)
    {
        Prompt = SeatIndex == 0 ? TEXT("") : TEXT("Co-driver seat   E: get out   F: open or shut your door");
    }
    else
    {
        int32 Seat = INDEX_NONE;
        const ARallyCarPawn* Car = CarriedPart ? nullptr : ARallyCarPawn::FindSeatInReach(this, Seat);
        if (Car)
        {
            Prompt = Car->GetPanels()->IsDoorJammed(Seat) ? TEXT("Door jammed: try the other side") : !Car->IsSeatFree(Seat) ? TEXT("Seat taken") : Seat == 0 ? TEXT("E: drive") : TEXT("E: ride as co-driver");
        }
    }
    if (!SeatedCar)
    {
        // What is under the crosshair, and what can be done with it; just its name if nothing.
        FString Aimed;
        if (CarriedPart)
        {
            int32 Panel = INDEX_NONE;
            if (URallyPanelComponent* Panels = FindFitForCarried(Panel))
            {
                const FString PanelPrompt = Panels->InteractionPrompt(this, Panel);
                if (!PanelPrompt.IsEmpty()) Prompt = Prompt.IsEmpty() ? PanelPrompt : Prompt + TEXT("   ") + PanelPrompt;
            }
        }
        else
        {
            const FRallyAim Aim = FindAim();
            // The bolts of what is in view glow; whatever was lit before goes back to steel.
            if (LitPanels.Get() && LitPanels.Get() != Aim.Panels) LitPanels->SetAimHighlight(INDEX_NONE, INDEX_NONE);
            if (Aim.Panels) Aim.Panels->SetAimHighlight(Aim.Slot, Aim.Fixing);
            LitPanels = Aim.Panels;
            if (Aim.Debris)
            {
                if (Aim.Debris->CanPickUp(this))
                    Prompt = FString::Printf(TEXT("E: pick up %s"), *URallyPanelComponent::GradedName(Aim.Debris->GetPanel(), Aim.Debris->IsSpare(), Aim.Debris->GetGrade()));
                else if (Aim.Debris->GetMount() == ERallyPartMount::UnderCar)
                    Prompt = TEXT("E: lower the jack");
                else
                    Aimed = URallyPanelComponent::GradedName(Aim.Debris->GetPanel(), Aim.Debris->IsSpare(), Aim.Debris->GetGrade());
            }
            else if (Aim.Panels)
            {
                FString PanelPrompt = Aim.Fixing != INDEX_NONE ? Aim.Panels->FixingPrompt(this, Aim.Slot, Aim.Fixing)
                    : Aim.Panels->InteractionPrompt(this, Aim.Slot);
                const FString Workshop = Aim.Panels->WorkshopPrompt(Aim.Slot);
                if (!Workshop.IsEmpty()) PanelPrompt += (PanelPrompt.IsEmpty() ? FString(Aim.Panels->SlotLabel(Aim.Slot) + TEXT(": ")) : FString(TEXT("   "))) + Workshop;
                const FString Adjust = Aim.Panels->AdjustPrompt(this, Aim.Slot);
                if (!Adjust.IsEmpty()) PanelPrompt += (PanelPrompt.IsEmpty() ? FString(Aim.Panels->SlotLabel(Aim.Slot) + TEXT(": ")) : FString(TEXT("   "))) + Adjust;
                if (Aim.Fixing != INDEX_NONE && HoldTime > 0.0f && (bHoldHome || bHoldOut))
                    PanelPrompt += FString::Printf(TEXT("   %s %d%%"), bHoldHome ? TEXT("doing up") : TEXT("undoing"),
                        FMath::RoundToInt(100.0f * FMath::Min(HoldTime / static_cast<float>(Rally::Fixing::TurnSecondsFor(Aim.Panels->NumFixings(Aim.Slot))), 1.0f)));
                if (!PanelPrompt.IsEmpty()) Prompt = Prompt.IsEmpty() ? PanelPrompt : Prompt + TEXT("   ") + PanelPrompt;
                else Aimed = URallyPanelComponent::PanelName(Aim.Slot);
            }
        }
        if (CarriedPart)
        {
            if (Prompt.IsEmpty())
                Prompt = CarriedPart->GetPanel() == URallyPanelComponent::WaterSlot
                    ? FString::Printf(TEXT("Water can, %.1f L: open the bonnet and pour it in at the radiator   G: drop"),
                        CarriedPart->GetHealth() * URallyPanelComponent::WaterCanLitres)
                    : CarriedPart->GetPanel() == URallyPanelComponent::OilBottleSlot
                    ? FString::Printf(TEXT("Oil, %.1f L: open the bonnet and pour it in at the engine's filler   G: drop"),
                        CarriedPart->GetHealth() * URallyPanelComponent::OilBottleLitres)
                    : CarriedPart->GetPanel() == URallyPanelComponent::ExhaustSlot
                    ? FString(TEXT("Exhaust: no way to refit it here   G: drop"))
                    : FString(TEXT("Carry to its car to refit   G: drop"));
            if (CarriedPart->GetPanel() == URallyPanelComponent::DrinkSlot)
                Prompt = FString::Printf(TEXT("Slivovica (%d swigs left)   B: take a swig   G: drop"), FMath::RoundToInt(CarriedPart->GetHealth() * Rally::Body::SwigsPerBottle));
            if (CarriedPart->GetPanel() == URallyPanelComponent::SmokeSlot)
                Prompt = FString::Printf(TEXT("Cigarettes (%d left)   B: light one   G: drop"), FMath::RoundToInt(CarriedPart->GetHealth() * Rally::Body::CigarettesPerCarton));
            if (CarriedPart->GetPanel() == URallyPanelComponent::FirstAidSlot)
            {
                const ARallyCharacter* Mate = nullptr;
                for (TActorIterator<ARallyCharacter> It(GetWorld()); It && !Mate; ++It)
                    if (*It != this && It->GetInjury() > 0.0 && FVector::Dist(It->GetActorLocation(), GetActorLocation()) < 200.0) Mate = *It;
                const int32 Uses = FMath::RoundToInt(CarriedPart->GetHealth() * Rally::Body::FirstAidUses);
                Prompt = Injury > 0.0f ? FString::Printf(TEXT("E: treat yourself (%d uses left)   G: drop"), Uses)
                    : Mate ? FString::Printf(TEXT("E: treat your team mate (%d uses left)   G: drop"), Uses)
                    : FString(TEXT("First-aid kit: nobody here is hurt   G: drop"));
            }
            // The open boot takes it: strapped into its own place if it has one free there; G does the same
            // when looking into the boot (it would drop through the car).
            if (const ARallyCarPawn* Car = CarriedPart->GetSourceCar())
                if (Car->GetPanels()->FitTarget(this, CarriedPart) == INDEX_NONE && Car->GetPanels()->CanStow(this, CarriedPart))
                    Prompt = FString::Printf(TEXT("%s: %s (%.0f of %.0f kg)%s"), Car->GetPanels()->FacesBoot(this) ? TEXT("E or G") : TEXT("E"),
                        Car->GetPanels()->FreeHomeFor(CarriedPart) != INDEX_NONE ? TEXT("strap it into its place in the boot") : TEXT("put it in the boot"),
                        Car->GetPanels()->GetBootLoadKg(), URallyPanelComponent::BootCapacityKg,
                        Car->GetPanels()->FacesBoot(this) ? TEXT("") : TEXT("   G: drop"));
            Prompt += FString::Printf(TEXT("   straps %d   sealant %d   putty %d"), Straps, Sealant, Putty);
        }
        if (RemoveActions > 0) Prompt += FString::Printf(TEXT("   taking off: %d done"), RemoveActions);
        bInteractionInView = !Prompt.IsEmpty();
        // Name only, in white: something to look at, nothing to do with it here.
        if (Prompt.IsEmpty()) AimedText = Aimed;
    }
    PromptText = Prompt;
}

void ARallyCharacter::TickAutoSeat(float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
    // -RallyAutoSeat=driver|codriver (automated tests): walk round to that door of the first car and
    // press Interact, through the same reach and seat checks as a player.
    if (AutoSeatShotTimer >= 0.0f && SeatedCar)
    {
        AutoSeatShotTimer += DeltaSeconds;
        if (AutoSeatShotTimer > 2.0f)
        {
            AutoSeatShotTimer = -1.0f;
            FScreenshotRequest::RequestScreenshot(SeatIndex == 1 ? TEXT("RallySeat_codriver.png") : TEXT("RallySeat_driver.png"), false, false);
        }
    }
    FString SeatName;
    if (bAutoSeatDone || SeatedCar || !FParse::Value(FCommandLine::Get(), TEXT("RallyAutoSeat="), SeatName))
    {
        return;
    }
    TActorIterator<ARallyCarPawn> It(GetWorld());
    if (!It)
    {
        return;
    }
    const int32 Seat = SeatName.Equals(TEXT("codriver"), ESearchCase::IgnoreCase) ? 1 : 0;
    ARallyCarPawn* Car = *It;
    // Go behind the car on the door's side first, so the path does not run through the body.
    const FVector Door = Car->GetSeatDoorLocation(Seat);
    const FVector Behind = Door - Car->GetActorForwardVector() * 400.0 + (Door - Car->GetActorLocation()).GetSafeNormal2D() * 60.0;
    const FVector Target = AutoSeatWaypoint == 0 ? Behind : Door;
    const FVector ToTarget = (Target - GetActorLocation()) * FVector(1.0, 1.0, 0.0);
    if (ToTarget.Size() < (AutoSeatWaypoint == 0 ? 80.0 : 60.0))
    {
        ++AutoSeatWaypoint;
    }
    int32 ReachSeat = INDEX_NONE;
    if (AutoSeatWaypoint >= 1 && ARallyCarPawn::FindSeatInReach(this, ReachSeat) == Car && ReachSeat == Seat && ToTarget.Size() < 80.0)
    {
        bAutoSeatDone = true;
        AutoSeatShotTimer = 0.0f;
        ServerInteract(nullptr);
        return;
    }
    if (Controller)
    {
        Controller->SetControlRotation(ToTarget.Rotation());
    }
    // Run the long walks (the crash test car is 50 m away), walk the last few metres.
    const bool bRun = ToTarget.Size() > 500.0;
    if (GetCharacterMovement()->MaxWalkSpeed != (bRun ? SprintSpeed : WalkSpeed))
    {
        SetSprint(bRun);
        if (!HasAuthority())
        {
            ServerSetSprint(bRun);
        }
    }
    AddMovementInput(ToTarget.GetSafeNormal());
#endif
}

void ARallyCharacter::ServerInteract_Implementation(ARallyDebrisActor* TargetPart)
{
    UE_LOG(LogTemp, Display, TEXT("RALLY_SEAT %s interact (seated=%d)"), *GetName(), SeatedCar ? 1 : 0);
    // Preserve the client's interaction intent across latency. A rejected pickup must never
    // silently enter the nearby driver's seat instead.
    if (TargetPart)
    {
        if (IsValid(TargetPart) && !SeatedCar) TargetPart->PickUp(this);
        return;
    }
    if (SeatedCar)
    {
        SeatedCar->ServerExitSeat(this);
        return;
    }
    if (CarriedPart)
    {
        if (TreatWithFirstAid()) return;
        // Fitted where it goes, or else into the open boot.
        if (ARallyCarPawn* Car = CarriedPart->GetSourceCar())
            if (!Car->GetPanels()->Refit(this, CarriedPart, false) && Car->GetPanels()->FitTarget(this, CarriedPart) == INDEX_NONE)
                Car->GetPanels()->Stow(this, CarriedPart);
        return;
    }
    // Empty hands at a jacked corner: lower the jack.
    for (TActorIterator<ARallyCarPawn> It(GetWorld()); It; ++It)
    {
        if (It->GetPanels()->LowerJack(this)) return;
    }
    int32 Seat = INDEX_NONE;
    if (ARallyCarPawn* Car = ARallyCarPawn::FindSeatInReach(this, Seat))
    {
        Car->ServerEnterSeat(this, Seat, false);
    }
}

void ARallyCharacter::CreateInputObjects()
{
    if (OnFootContext)
    {
        return;
    }
    MoveAction = MakeAction(this, EInputActionValueType::Axis2D);
    MouseLookAction = MakeAction(this, EInputActionValueType::Axis2D);
    StickLookAction = MakeAction(this, EInputActionValueType::Axis2D);
    JumpAction = MakeAction(this, EInputActionValueType::Boolean);
    InteractAction = MakeAction(this, EInputActionValueType::Boolean);
    ToolKeyAction = MakeAction(this, EInputActionValueType::Boolean);
    AdjustAction = MakeAction(this, EInputActionValueType::Boolean);
    SleepAction = MakeAction(this, EInputActionValueType::Boolean);
    CatalogueAction = MakeAction(this, EInputActionValueType::Boolean);
    HelpAction = MakeAction(this, EInputActionValueType::Boolean);
    WorkshopAction = MakeAction(this, EInputActionValueType::Boolean);
    MoneyAction = MakeAction(this, EInputActionValueType::Boolean);
    CupAction = MakeAction(this, EInputActionValueType::Boolean);
    ConfirmAction = MakeAction(this, EInputActionValueType::Boolean);
    ToolWheelAction = MakeAction(this, EInputActionValueType::Axis1D);
    SprintAction = MakeAction(this, EInputActionValueType::Boolean);
    BodgeAction = MakeAction(this, EInputActionValueType::Boolean);
    DropAction = MakeAction(this, EInputActionValueType::Boolean);
    OpenPanelAction = MakeAction(this, EInputActionValueType::Boolean);
    RemoveAction = MakeAction(this, EInputActionValueType::Boolean);
    CrouchAction = MakeAction(this, EInputActionValueType::Boolean);
    LeanAction = MakeAction(this, EInputActionValueType::Boolean);

    OnFootContext = NewObject<UInputMappingContext>(this);
    // WASD to a 2D move vector: X right, Y forward.
    auto MapMove = [this](const FKey& Key, bool bNegate, bool bSwizzle)
    {
        FEnhancedActionKeyMapping& Mapping = OnFootContext->MapKey(MoveAction, Key);
        if (bSwizzle)
        {
            Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(OnFootContext));
        }
        if (bNegate)
        {
            Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(OnFootContext));
        }
    };
    MapMove(EKeys::W, false, true);
    MapMove(EKeys::S, true, true);
    MapMove(EKeys::D, false, false);
    MapMove(EKeys::A, true, false);
    OnFootContext->MapKey(MoveAction, EKeys::Gamepad_Left2D);
    OnFootContext->MapKey(MouseLookAction, EKeys::Mouse2D);
    OnFootContext->MapKey(StickLookAction, EKeys::Gamepad_Right2D);
    OnFootContext->MapKey(JumpAction, EKeys::SpaceBar);
    OnFootContext->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
    OnFootContext->MapKey(InteractAction, EKeys::E);
    OnFootContext->MapKey(InteractAction, EKeys::Gamepad_FaceButton_Left);
    OnFootContext->MapKey(BodgeAction, EKeys::B);
    OnFootContext->MapKey(DropAction, EKeys::G);
    OnFootContext->MapKey(OpenPanelAction, EKeys::F);
    OnFootContext->MapKey(OpenPanelAction, EKeys::Gamepad_FaceButton_Top);
    OnFootContext->MapKey(RemoveAction, EKeys::R);
    OnFootContext->MapKey(RemoveAction, EKeys::Gamepad_RightShoulder);
    OnFootContext->MapKey(CrouchAction, EKeys::LeftControl);
    OnFootContext->MapKey(CrouchAction, EKeys::C);
    OnFootContext->MapKey(CrouchAction, EKeys::Gamepad_FaceButton_Right);
    OnFootContext->MapKey(LeanAction, EKeys::Q);
    OnFootContext->MapKey(LeanAction, EKeys::Gamepad_LeftShoulder);
    for (const FKey& Key : { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight })
        OnFootContext->MapKey(ToolKeyAction, Key);
    OnFootContext->MapKey(ToolWheelAction, EKeys::MouseWheelAxis);
    OnFootContext->MapKey(AdjustAction, EKeys::T);
    OnFootContext->MapKey(SleepAction, EKeys::Z);
    OnFootContext->MapKey(CatalogueAction, EKeys::K);
    OnFootContext->MapKey(HelpAction, EKeys::F1);
    OnFootContext->MapKey(WorkshopAction, EKeys::P);
    OnFootContext->MapKey(MoneyAction, EKeys::M);
    OnFootContext->MapKey(CupAction, EKeys::J);
    OnFootContext->MapKey(ConfirmAction, EKeys::Enter);
    OnFootContext->MapKey(SprintAction, EKeys::LeftShift);
    OnFootContext->MapKey(SprintAction, EKeys::Gamepad_LeftThumbstick);
}

void ARallyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("RallyPanelTest"))) return;
#endif
    CreateInputObjects();
    UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
    Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ARallyCharacter::OnMove);
    Input->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ARallyCharacter::OnLook);
    Input->BindAction(StickLookAction, ETriggerEvent::Triggered, this, &ARallyCharacter::OnStickLook);
    Input->BindAction(StickLookAction, ETriggerEvent::Completed, this, &ARallyCharacter::OnStickLook);
    Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ARallyCharacter::OnJump);
    Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ARallyCharacter::OnInteract);
    Input->BindAction(InteractAction, ETriggerEvent::Completed, this, &ARallyCharacter::OnInteractReleased);
    Input->BindAction(RemoveAction, ETriggerEvent::Completed, this, &ARallyCharacter::OnRemoveReleased);
    Input->BindAction(ToolKeyAction, ETriggerEvent::Started, this, &ARallyCharacter::OnToolKey);
    Input->BindAction(ToolWheelAction, ETriggerEvent::Triggered, this, &ARallyCharacter::OnToolWheel);
    Input->BindAction(AdjustAction, ETriggerEvent::Started, this, &ARallyCharacter::OnAdjust);
    Input->BindAction(SleepAction, ETriggerEvent::Started, this, &ARallyCharacter::OnSleep);
    Input->BindAction(CatalogueAction, ETriggerEvent::Started, this, &ARallyCharacter::OnCatalogue);
    Input->BindAction(HelpAction, ETriggerEvent::Started, this, &ARallyCharacter::OnHelp);
    Input->BindAction(WorkshopAction, ETriggerEvent::Started, this, &ARallyCharacter::OnWorkshop);
    Input->BindAction(MoneyAction, ETriggerEvent::Started, this, &ARallyCharacter::OnMoney);
    Input->BindAction(CupAction, ETriggerEvent::Started, this, &ARallyCharacter::OnCup);
    Input->BindAction(ConfirmAction, ETriggerEvent::Started, this, &ARallyCharacter::OnConfirm);
    Input->BindAction(BodgeAction, ETriggerEvent::Started, this, &ARallyCharacter::OnBodge);
    Input->BindAction(DropAction, ETriggerEvent::Started, this, &ARallyCharacter::OnDrop);
    Input->BindAction(OpenPanelAction, ETriggerEvent::Started, this, &ARallyCharacter::OnOpenPanel);
    Input->BindAction(RemoveAction, ETriggerEvent::Started, this, &ARallyCharacter::OnRemovePart);
    Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ARallyCharacter::OnCrouchPressed);
    Input->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ARallyCharacter::OnCrouchReleased);
    Input->BindAction(LeanAction, ETriggerEvent::Started, this, &ARallyCharacter::OnLean);
    Input->BindAction(LeanAction, ETriggerEvent::Completed, this, &ARallyCharacter::OnLean);
    Input->BindAction(SprintAction, ETriggerEvent::Triggered, this, &ARallyCharacter::OnSprint);
    Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ARallyCharacter::OnSprint);
}

void ARallyCharacter::NotifyControllerChanged()
{
    // Leaving this pawn (taking the driver's seat): drop the on-foot keys so they do not shadow the car's.
    if (const APlayerController* Previous = Cast<APlayerController>(PreviousController))
    {
        if (Previous->IsLocalController() && OnFootContext)
        {
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Previous->GetLocalPlayer()))
            {
                Subsystem->RemoveMappingContext(OnFootContext);
            }
        }
    }
    Super::NotifyControllerChanged();
    const APlayerController* PC = Cast<APlayerController>(GetController());
    if (PC && PC->IsLocalController())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            CreateInputObjects();
            Subsystem->AddMappingContext(OnFootContext, 0);
        }
    }
}

void ARallyCharacter::OnMove(const FInputActionValue& Value)
{
    if (SeatedCar)
    {
        return;
    }
    const FVector2D Move = Value.Get<FVector2D>();
    // Holding lean, the movement keys steer the head instead of the feet.
    if (bLeanHeld)
    {
        LeanStick = Move;
        return;
    }
    const FRotator Yaw(0.0, GetControlRotation().Yaw, 0.0);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Move.Y);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Move.X);
}

void ARallyCharacter::OnLook(const FInputActionValue& Value)
{
    const FVector2D Delta = Value.Get<FVector2D>() * MouseSensitivity * 10.0f;
    if (SeatedCar)
    {
        SeatLook.Yaw = FMath::Clamp(SeatLook.Yaw + Delta.X, -150.0f, 150.0f);
        SeatLook.Pitch = FMath::Clamp(SeatLook.Pitch + Delta.Y, -60.0f, 40.0f);
        return;
    }
    AddControllerYawInput(Delta.X);
    AddControllerPitchInput(-Delta.Y);
}

void ARallyCharacter::OnStickLook(const FInputActionValue& Value)
{
    if (SeatedCar)
    {
        StickLook = Value.Get<FVector2D>();
        return;
    }
    StickLook = FVector2D::ZeroVector;
    const FVector2D Rate = Value.Get<FVector2D>() * 120.0f * GetWorld()->GetDeltaSeconds();
    AddControllerYawInput(Rate.X);
    AddControllerPitchInput(-Rate.Y);
}

void ARallyCharacter::OnJump(const FInputActionValue& Value)
{
    if (!SeatedCar && !CarriedPart)
    {
        Jump();
    }
}

void ARallyCharacter::OnInteract(const FInputActionValue& Value)
{
    // A fixing in the crosshair is done up while E is held; a loose part is picked up; the jack under a
    // car is lowered; otherwise the usual (fit what is carried, get into a seat).
    const FRallyAim Aim = CarriedPart || SeatedCar ? FRallyAim() : FindAim();
    if (Aim.Panels && Aim.Fixing != INDEX_NONE)
    {
        bHoldHome = true;
        HoldTime = 0.0f;
        return;
    }
    ServerInteract(Aim.Debris && Aim.Debris->CanPickUp(this) ? Aim.Debris : nullptr);
}

void ARallyCharacter::OnInteractReleased(const FInputActionValue& Value) { bHoldHome = false; HoldTime = 0.0f; }
void ARallyCharacter::OnRemoveReleased(const FInputActionValue& Value) { bHoldOut = false; HoldTime = 0.0f; }

void ARallyCharacter::UpdateFixingHold(float DeltaSeconds)
{
    // The fixing being turned follows the hold on this machine (URallyPanelComponent::ShowFixingTurn); just
    // turned, it stays shown until the server's answer turns it the rest of the way.
    const double Since = GetWorld()->GetTimeSeconds() - HoldSentTime;
    const auto StopShowing = [this, Since]()
    {
        if (HoldPanels.Get() && Since > 1.0) HoldPanels->ShowFixingTurn(INDEX_NONE, INDEX_NONE, 0.0f, false);
    };
    if ((!bHoldHome && !bHoldOut) || SeatedCar || CarriedPart)
    {
        HoldTime = 0.0f;
        StopShowing();
        return;
    }
    // Only while the crosshair stays on a fixing that this key would turn; another one starts over.
    const FRallyAim Aim = FindAim();
    if (!Aim.Panels || Aim.Fixing == INDEX_NONE || Aim.Panels->IsFixingOut(Aim.Slot, Aim.Fixing) != bHoldHome)
    {
        HoldTime = 0.0f;
        StopShowing();
        return;
    }
    if (Aim.Panels != HoldPanels.Get() || Aim.Slot != HoldPart || Aim.Fixing != HoldFixing)
    {
        HoldPanels = Aim.Panels;
        HoldPart = Aim.Slot;
        HoldFixing = Aim.Fixing;
        HoldTime = 0.0f;
        HoldSentTime = -10.0;
    }
    // Sent once; wait for the server's answer to come back before trying the same fixing again.
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - HoldSentTime < 1.0) return;
    HoldTime += DeltaSeconds;
    const float Needed = static_cast<float>(Rally::Fixing::TurnSecondsFor(Aim.Panels->NumFixings(Aim.Slot)) * Rally::Body::HandSlowdown(Cold, Calm));
    Aim.Panels->ShowFixingTurn(Aim.Slot, Aim.Fixing, HoldTime / Needed, bHoldHome);
    if (HoldTime < Needed) return;
    HoldSentTime = Now;
    HoldTime = 0.0f;
    ServerTurnFixing(Cast<ARallyCarPawn>(Aim.Panels->GetOwner()), Aim.Slot, Aim.Fixing, bHoldHome);
}

void ARallyCharacter::OnToolKey(const FInputActionValue& Value)
{
    const APlayerController* PC = Cast<APlayerController>(Controller);
    if (!PC) return;
    const FKey Keys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight };
    for (int32 Tool = 0; Tool < UE_ARRAY_COUNT(Keys); ++Tool)
        if (PC->IsInputKeyDown(Keys[Tool])) { SelectTool(Tool); return; }
}

void ARallyCharacter::OnToolWheel(const FInputActionValue& Value)
{
    const float Step = Value.Get<float>();
    if (FMath::Abs(Step) < 0.1f) return;
    if (bCatalogueOpen)
    {
        // Through the catalogue's lines instead of the tools.
        const int32 Lines = static_cast<int32>(Rally::Economy::Catalogue().size());
        CatalogueIndex = (CatalogueIndex + (Step > 0.0f ? Lines - 1 : 1)) % Lines;
        return;
    }
    const int32 Count = static_cast<int32>(Rally::Fixing::ETool::Count);
    SelectTool((HeldTool + (Step > 0.0f ? 1 : Count - 1)) % Count);
}

void ARallyCharacter::SelectTool(int32 Tool)
{
    if (Tool < 0 || Tool >= static_cast<int32>(Rally::Fixing::ETool::Count)) return;
    HeldTool = static_cast<uint8>(Tool);
    HoldTime = 0.0f;
    if (!HasAuthority()) ServerSelectTool(HeldTool);
}

void ARallyCharacter::OnAdjust(const FInputActionValue& Value)
{
    // T sets one step up, Shift+T one down, whatever the tool in hand adjusts on the part in view.
    const FRallyAim Aim = SeatedCar || CarriedPart ? FRallyAim() : FindAim();
    const APlayerController* PC = Cast<APlayerController>(Controller);
    const int32 Step = PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift)) ? -1 : 1;
    if (Aim.Panels) ServerAdjust(Cast<ARallyCarPawn>(Aim.Panels->GetOwner()), Aim.Slot, Step);
}

void ARallyCharacter::ServerAdjust_Implementation(ARallyCarPawn* Car, int32 Part, int32 Step)
{
    if (!SeatedCar && !CarriedPart && IsValid(Car)) Car->GetPanels()->Adjust(this, Part, Step);
}

void ARallyCharacter::RallyPart(const FString& Kind, const FString& Grade)
{
#if !UE_BUILD_SHIPPING
    const int32 Slot = URallyPanelComponent::SlotForKind(Kind);
    const FString G = Grade.ToLower();
    const uint8 Value = G == TEXT("worn") ? 0 : G == TEXT("sport") ? 2 : G == TEXT("rally") ? 3 : 1;
    if (Slot != INDEX_NONE) ServerSpawnPart(Slot, Value);
#endif
}

void ARallyCharacter::OnSleep(const FInputActionValue& Value)
{
    // Z sleeps till morning, Shift+Z till the rally weekend.
    const APlayerController* PC = Cast<APlayerController>(Controller);
    if (SeatedCar) ServerToggleSleep(PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift)));
}

void ARallyCharacter::OnCatalogue(const FInputActionValue& Value) { bCatalogueOpen = !bCatalogueOpen; }

void ARallyCharacter::OnCup(const FInputActionValue& Value)
{
    if (const APlayerController* PC = Cast<APlayerController>(Controller))
        if (ARallyHUD* Hud = Cast<ARallyHUD>(PC->GetHUD())) Hud->ToggleCup();
}

void ARallyCharacter::OnMoney(const FInputActionValue& Value)
{
    // M takes from the kitty, Shift+M puts back.
    const APlayerController* PC = Cast<APlayerController>(Controller);
    const bool bBack = PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
    ServerMoveMoney(bBack ? -Rally::Economy::KittyStepSk : Rally::Economy::KittyStepSk);
}

void ARallyCharacter::ServerMoveMoney_Implementation(int32 Sk)
{
    if (ARallyTeamState* Team = ARallyTeamState::Get(GetWorld())) Team->MovePocketMoney(this, FMath::Clamp(Sk, -5000, 5000));
}

void ARallyCharacter::OnWorkshop(const FInputActionValue& Value)
{
    const FRallyAim Aim = SeatedCar || CarriedPart ? FRallyAim() : FindAim();
    if (Aim.Panels) ServerWorkshop(Cast<ARallyCarPawn>(Aim.Panels->GetOwner()), Aim.Slot);
}

void ARallyCharacter::ServerWorkshop_Implementation(ARallyCarPawn* Car, int32 Part)
{
    if (!SeatedCar && !CarriedPart && IsValid(Car)) Car->GetPanels()->WorkshopRepair(this, Part);
}

void ARallyCharacter::OnHelp(const FInputActionValue& Value)
{
    if (const APlayerController* PC = Cast<APlayerController>(Controller))
        if (ARallyHUD* Hud = Cast<ARallyHUD>(PC->GetHUD())) Hud->ToggleHelp();
}

void ARallyCharacter::OnConfirm(const FInputActionValue& Value)
{
    // Phoned in from the catalogue: paid from the team's kitty, delivered to the service yard next morning.
    if (bCatalogueOpen) ServerOrder(CatalogueIndex);
    // Otherwise the letter on screen is put away.
    else if (const APlayerController* PC = Cast<APlayerController>(Controller))
        if (ARallyHUD* Hud = Cast<ARallyHUD>(PC->GetHUD())) Hud->DismissLetter();
}

void ARallyCharacter::ServerOrder_Implementation(int32 Item)
{
    if (ARallyTeamState* Team = ARallyTeamState::Get(GetWorld())) Team->Order(Item, this);
}

void ARallyCharacter::ServerToggleSleep_Implementation(bool bTillRally)
{
    if (!SeatedCar) return;
    SetSleeping(!bSleeping, bTillRally);
    if (ARallyGameMode* Mode = GetWorld()->GetAuthGameMode<ARallyGameMode>()) Mode->TrySleep();
}

void ARallyCharacter::SetSleeping(bool bSleep, bool bTillRally)
{
    if (!HasAuthority()) return;
    bSleeping = bSleep && SeatedCar != nullptr;
    bSleepTillRally = bSleeping && bTillRally;
    ForceNetUpdate();
}

void ARallyCharacter::AddInjury(double Amount)
{
    if (!HasAuthority() || !(Amount > 0.0)) return;
    Injury = static_cast<float>(FMath::Clamp(Injury + Amount, 0.0, 1.0));
    UE_LOG(LogTemp, Display, TEXT("RALLY_BODY %s hurt: %.0f%%"), *GetName(), Injury * 100.0f);
    ApplyInjury();
    ForceNetUpdate();
    if (Injury >= Rally::Body::HospitalInjury) TakeToHospital();
}

void ARallyCharacter::TakeToHospital()
{
    // Out of the car and to hospital; they wake in the service yard, sore, and the bill is the team's.
    if (SeatedCar) SeatedCar->ServerExitSeat(this, true);
    if (CarriedPart) CarriedPart->Drop();
    if (const ARallyTeamState* Team = ARallyTeamState::Get(GetWorld()))
        SetActorLocation(Team->GetServiceYard() + FVector(-200.0, 200.0, 120.0), false, nullptr, ETeleportType::TeleportPhysics);
    if (ARallyTeamState* Team = ARallyTeamState::Get(GetWorld())) Team->AddMoney(-Rally::Body::HospitalBillSk, TEXT("hospital bill"));
    Injury = static_cast<float>(Rally::Body::AfterHospital);
    Cold = 0.0f;
    Wet = 0.0f;
    ApplyInjury();
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("RALLY_BODY %s taken to hospital, back in the service yard"), *GetName());
}

void ARallyCharacter::TickBody()
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now < NextBodyTick) return;
    const double Step = Now - NextBodyTick < 2.0 ? 1.0 : 1.0;
    NextBodyTick = Now + 1.0;
    const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
    if (!Clock) return;
    const bool bHeater = SeatedCar && SeatedCar->GetEngine() && SeatedCar->GetEngine()->IsRunning();
    const double Before = Cold, WetBefore = Wet;
    const double Air = Rally::Body::AmbientC(Clock->GetHour());
    Wet = static_cast<float>(FMath::Clamp(Wet + Rally::Body::WetRate(Clock->GetRain(), Air, SeatedCar != nullptr, bHeater) * Step, 0.0, 1.0));
    Cold = static_cast<float>(FMath::Clamp(Cold + Rally::Body::ColdRate(Rally::Body::FeltC(Air, Wet), SeatedCar != nullptr, bHeater) * Step, 0.0, 1.0));
    if (FMath::FloorToInt(WetBefore * 4.0) != FMath::FloorToInt(Wet * 4.0))
        UE_LOG(LogTemp, Display, TEXT("RALLY_BODY %s wet: %.0f%%"), *GetName(), Wet * 100.0f);
    if (Drunk > 0.0f || Calm > 0.0f)
    {
        const bool bWasDrunk = Drunk > 0.0f, bWasCalm = Calm > 0.0f;
        Drunk = static_cast<float>(FMath::Max(Drunk - Rally::Body::SoberPerSecond * Step, 0.0));
        Calm = static_cast<float>(FMath::Max(Calm - Step / Rally::Body::CalmSeconds, 0.0));
        if ((bWasDrunk && Drunk <= 0.0f) || (bWasCalm && Calm <= 0.0f)) { ApplyInjury(); ForceNetUpdate(); }
    }
    if (FMath::FloorToInt(Before * 4.0) != FMath::FloorToInt(Cold * 4.0))
    {
        UE_LOG(LogTemp, Display, TEXT("RALLY_BODY %s cold: %.0f%%"), *GetName(), Cold * 100.0f);
        ApplyInjury();
        ForceNetUpdate();
    }
}

bool ARallyCharacter::UseCarriedVice()
{
    if (!HasAuthority() || !CarriedPart || CarriedPart->GetHealth() <= 0.0f) return false;
    const int32 Slot = CarriedPart->GetPanel();
    if (Slot != URallyPanelComponent::DrinkSlot && Slot != URallyPanelComponent::SmokeSlot) return false;
    const bool bDrink = Slot == URallyPanelComponent::DrinkSlot;
    if (bDrink)
    {
        Drunk = static_cast<float>(FMath::Min(Drunk + Rally::Body::SwigDrunk, 1.0));
        Cold = static_cast<float>(FMath::Max(Cold - Rally::Body::SwigWarmth, 0.0));
    }
    else
    {
        Calm = 1.0f;
        Cold = static_cast<float>(FMath::Max(Cold - Rally::Body::SmokeWarmth, 0.0));
    }
    const float Left = CarriedPart->GetHealth() - 1.0f / (bDrink ? Rally::Body::SwigsPerBottle : Rally::Body::CigarettesPerCarton);
    UE_LOG(LogTemp, Display, TEXT("RALLY_BODY %s %s: drunk %.0f%%, calm %.0f%%, cold %.0f%%"), *GetName(), bDrink ? TEXT("takes a swig") : TEXT("lights a cigarette"),
        Drunk * 100.0f, Calm * 100.0f, Cold * 100.0f);
    if (Left <= 0.01f)
    {
        ARallyDebrisActor* Empty = CarriedPart;
        Empty->Drop();
        Empty->Destroy();
    }
    else CarriedPart->SetHealth(Left);
    ApplyInjury();
    ForceNetUpdate();
    return true;
}

bool ARallyCharacter::TreatWithFirstAid()
{
    if (!HasAuthority() || !CarriedPart || CarriedPart->GetPanel() != URallyPanelComponent::FirstAidSlot || CarriedPart->GetHealth() <= 0.0f) return false;
    // Yourself if hurt, else the nearest hurt team mate within two metres.
    ARallyCharacter* Patient = Injury > 0.0f ? this : nullptr;
    for (TActorIterator<ARallyCharacter> It(GetWorld()); It && !Patient; ++It)
        if (*It != this && It->Injury > 0.0f && FVector::Dist(It->GetActorLocation(), GetActorLocation()) < 200.0) Patient = *It;
    if (!Patient) return false;
    Patient->Injury = 0.0f;
    Patient->ApplyInjury();
    Patient->ForceNetUpdate();
    if (Patient != this) UE_LOG(LogTemp, Display, TEXT("RALLY_BODY %s treats %s"), *GetName(), *Patient->GetName());
    const float Left = CarriedPart->GetHealth() - 1.0f / Rally::Body::FirstAidUses;
    UE_LOG(LogTemp, Display, TEXT("RALLY_BODY %s treated with the first-aid kit (%d uses left)"), *GetName(),
        FMath::Max(0, FMath::RoundToInt(Left * Rally::Body::FirstAidUses)));
    if (Left <= 0.01f)
    {
        ARallyDebrisActor* Empty = CarriedPart;
        Empty->Drop();
        Empty->Destroy();
    }
    else CarriedPart->SetHealth(Left);
    ForceNetUpdate();
    return true;
}

void ARallyCharacter::OnRep_Injury() { ApplyInjury(); }

void ARallyCharacter::ApplyInjury()
{
    // Sight, and the pace.
    ApplyBodyView(FirstPersonCamera->PostProcessSettings);
    SetSprint(bSprintRequested);
}

void ARallyCharacter::ApplyBodyView(FPostProcessSettings& Post) const
{
    // Hurt: darker, fringed edges.
    Post.bOverride_VignetteIntensity = Injury > 0.0f;
    Post.VignetteIntensity = static_cast<float>(Rally::Body::Vignette(Injury));
    Post.bOverride_SceneFringeIntensity = Injury > 0.0f;
    Post.SceneFringeIntensity = static_cast<float>(Rally::Body::Fringe(Injury));
    // Drunk: blurred and glowing.
    Post.bOverride_BloomIntensity = Drunk > 0.0f;
    Post.BloomIntensity = 0.675f + 3.0f * Drunk;
    Post.bOverride_DepthOfFieldFstop = Drunk > 0.0f;
    Post.DepthOfFieldFstop = FMath::Lerp(32.0f, 1.2f, Drunk);
    Post.bOverride_DepthOfFieldFocalDistance = Drunk > 0.0f;
    Post.DepthOfFieldFocalDistance = 60.0f;
    // Cold: the world looks blue.
    Post.bOverride_SceneColorTint = Cold > 0.05f;
    Post.SceneColorTint = FLinearColor(1.0f - 0.25f * Cold, 1.0f - 0.12f * Cold, 1.0f);
}

void ARallyCharacter::RallySave() { ServerSaveLoad(true); }
void ARallyCharacter::RallyLoad() { ServerSaveLoad(false); }
void ARallyCharacter::RallyRain(float Rain) { ServerRain(Rain); }

void ARallyCharacter::RallyLeak(const FString& Kind, float Health)
{
    // Bit 0 the sump, 1 the radiator, 2 the tank.
    const uint8 Which = Kind.Equals(TEXT("oil"), ESearchCase::IgnoreCase) ? 1 : Kind.Equals(TEXT("coolant"), ESearchCase::IgnoreCase) ? 2
        : Kind.Equals(TEXT("fuel"), ESearchCase::IgnoreCase) ? 4 : Kind.Equals(TEXT("all"), ESearchCase::IgnoreCase) ? 7 : 0;
    if (Which) ServerLeak(Which, Health);
}

void ARallyCharacter::ServerLeak_Implementation(uint8 Which, float Health)
{
#if !UE_BUILD_SHIPPING
    ARallyCarPawn* Car = SeatedCar;
    for (TActorIterator<ARallyCarPawn> It(GetWorld()); It && !Car; ++It)
        if (FVector::Dist(It->GetActorLocation(), GetActorLocation()) < 1000.0) Car = *It;
    if (!Car) return;
    if (Which & 1) Car->GetDamage()->DebugSetHealth(ERallyPart::OilSump, Health);
    if (Which & 2) Car->GetDamage()->DebugSetHealth(ERallyPart::Radiator, Health);
    if (Which & 4) Car->GetDamage()->DebugSetHealth(ERallyPart::FuelTank, Health);
#endif
}

void ARallyCharacter::ServerRain_Implementation(float Rain)
{
#if !UE_BUILD_SHIPPING
    if (ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld())) Clock->SetRainOverride(Rain);
#endif
}

void ARallyCharacter::ServerSaveLoad_Implementation(bool bSave)
{
#if !UE_BUILD_SHIPPING
    if (ARallyGameMode* Mode = GetWorld()->GetAuthGameMode<ARallyGameMode>())
    {
        if (bSave) Mode->SaveTeam(TEXT("saved by hand"));
        else Mode->LoadTeam();
    }
#endif
}

void ARallyCharacter::ServerSpawnPart_Implementation(int32 Slot, uint8 Grade)
{
#if !UE_BUILD_SHIPPING
    if (TActorIterator<ARallyCarPawn> It(GetWorld()); It)
        It->GetPanels()->DebugSpawnPart(Slot, Grade, GetActorLocation() + GetActorForwardVector() * 80.0 + FVector(0, 0, 20));
#endif
}

void ARallyCharacter::ServerSelectTool_Implementation(uint8 Tool)
{
    if (Tool < static_cast<uint8>(Rally::Fixing::ETool::Count)) HeldTool = Tool;
}

void ARallyCharacter::ServerTurnFixing_Implementation(ARallyCarPawn* Car, int32 Part, int32 Fixing, bool bHome)
{
    if (!SeatedCar && !CarriedPart && IsValid(Car)) Car->GetPanels()->TurnFixing(this, Part, Fixing, bHome);
}

bool ARallyCharacter::AdvanceFixingTurn(double Interval)
{
    if (!HasAuthority()) return false;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - LastFixingTurn < Interval) return false;
    LastFixingTurn = Now;
    return true;
}

void ARallyCharacter::OnSprint(const FInputActionValue& Value)
{
    SetSprint(Value.Get<bool>());
    ServerSetSprint(Value.Get<bool>());
}

void ARallyCharacter::SetSprint(bool bSprint)
{
    bSprintRequested = bSprint;
    GetCharacterMovement()->MaxWalkSpeed = static_cast<float>((CarriedPart
        ? (CarriedPart->GetMassKg() >= 18 ? 150.0f : 230.0f)
        : (bSprint ? SprintSpeed : WalkSpeed)) * Rally::Body::WalkScale(Injury));
}

void ARallyCharacter::ServerSetSprint_Implementation(bool bSprint)
{
    // Speed must match on the server, or its movement checks pull the sprinting player back.
    SetSprint(bSprint);
}

void ARallyCharacter::SetCarriedPart(ARallyDebrisActor* Part)
{
    if (!HasAuthority()) return;
    CarriedPart = Part;
    RefitActions = 0;
    LastRefitTime = -10;
    OnRep_CarriedPart();
    ForceNetUpdate();
}
void ARallyCharacter::OnRep_CarriedPart() { SetSprint(bSprintRequested); }

bool ARallyCharacter::ConsumeStrap()
{
    if (!HasAuthority() || !Straps) return false;
    --Straps;
    return true;
}

bool ARallyCharacter::ConsumeSealant()
{
    if (!HasAuthority() || !Sealant) return false;
    --Sealant;
    return true;
}

bool ARallyCharacter::ConsumePutty()
{
    if (!HasAuthority() || !Putty) return false;
    --Putty;
    return true;
}

bool ARallyCharacter::AdvancePanelRefit()
{
    if (!HasAuthority() || !CarriedPart) return false;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - LastRefitTime < 0.75) return false;
    if (Now - LastRefitTime > 4.0) RefitActions = 0;
    LastRefitTime = Now;
    if (++RefitActions == 1) FirstRefitTime = Now;
    UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL refit_action=%d"), RefitActions);
    return RefitActions >= 3;
}

double ARallyCharacter::GetRefitRush() const
{
    return RefitActions > 1 ? Rally::Panel::RefitRush((LastRefitTime - FirstRefitTime) / (RefitActions - 1)) : 1.0;
}

ARallyCharacter::FRallyAim ARallyCharacter::FindAim() const
{
    FRallyAim Aim;
    if (SeatedCar || !FirstPersonCamera) return Aim;
    // The camera's own rotation only catches up with the controller when the view is next drawn.
    const FVector Eye = FirstPersonCamera->GetComponentLocation();
    const FVector Direction = (Controller ? Controller->GetControlRotation() : GetBaseAimRotation()).Vector();
    double Best = URallyPanelComponent::ReachCm;
    // Anything solid in between stops the reach; the cars' own boxes do not (they are mostly air round the
    // parts), nor do loose parts (they are checked one by one below).
    FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyAim), false, this);
    if (CarriedPart) Params.AddIgnoredActor(CarriedPart);
    for (TActorIterator<ARallyCarPawn> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
    FHitResult Wall;
    if (GetWorld()->LineTraceSingleByChannel(Wall, Eye, Eye + Direction * Best, ECC_Visibility, Params)) Best = Wall.Distance;
    for (TActorIterator<ARallyCarPawn> It(GetWorld()); It; ++It)
    {
        if (FVector::Dist(It->GetActorLocation(), Eye) > Best + 500.0) continue;
        double Distance = 0.0;
        int32 Fixing = INDEX_NONE;
        const int32 Slot = It->GetPanels()->FindTargetAlongRay(Eye, Direction, Best, bIsCrouched, Distance, &Fixing);
        if (Slot != INDEX_NONE && Distance <= Best)
        {
            Aim.Panels = It->GetPanels();
            Aim.Slot = Slot;
            Aim.Fixing = Fixing;
            Best = Distance;
        }
    }
    FCollisionQueryParams PartParams(SCENE_QUERY_STAT(RallyAimPart), false, this);
    for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
    {
        UPrimitiveComponent* Shape = Cast<UPrimitiveComponent>(It->GetRootComponent());
        if (!Shape || *It == CarriedPart || FVector::Dist(It->GetActorLocation(), Eye) > Best + 300.0) continue;
        FHitResult Hit;
        if (Shape->LineTraceComponent(Hit, Eye, Eye + Direction * Best, PartParams) && Hit.Distance <= Best)
        {
            Aim = FRallyAim();
            Aim.Debris = *It;
            Best = Hit.Distance;
        }
    }
    return Aim;
}

URallyPanelComponent* ARallyCharacter::FindFitForCarried(int32& OutPanel) const
{
    OutPanel = INDEX_NONE;
    if (!CarriedPart || !CarriedPart->GetSourceCar()) return nullptr;
    URallyPanelComponent* Panels = CarriedPart->GetSourceCar()->GetPanels();
    const int32 Target = Panels->FitTarget(this, CarriedPart);
    OutPanel = Target != INDEX_NONE ? Target : CarriedPart->GetPanel();
    return Panels;
}
void ARallyCharacter::OnBodge(const FInputActionValue& Value) { RequestBodge(); }
void ARallyCharacter::RequestBodge()
{
    if (SeatedCar) return;
    int32 Panel = INDEX_NONE;
    URallyPanelComponent* Panels = nullptr;
    if (CarriedPart) Panels = FindFitForCarried(Panel);
    else
    {
        const FRallyAim Aim = FindAim();
        Panels = Aim.Panels;
        Panel = Aim.Slot;
    }
    if (Panels) ServerBodgePanel(Cast<ARallyCarPawn>(Panels->GetOwner()), Panel);
    else if (CarriedPart) ServerBodgePanel(CarriedPart->GetSourceCar(), INDEX_NONE);
}
void ARallyCharacter::OnDrop(const FInputActionValue& Value) { ServerDropPart(); }
void ARallyCharacter::OnOpenPanel(const FInputActionValue& Value) { RequestTogglePanel(); }
void ARallyCharacter::OnRemovePart(const FInputActionValue& Value) { RequestRemovePart(); }
void ARallyCharacter::RequestTogglePanel()
{
    const FRallyAim Aim = SeatedCar || CarriedPart ? FRallyAim() : FindAim();
    ServerTogglePanel(Aim.Panels ? Cast<ARallyCarPawn>(Aim.Panels->GetOwner()) : nullptr, Aim.Slot);
}
void ARallyCharacter::RequestRemovePart()
{
    // On a fixing R is held to undo it; on the part itself it lifts it off (or pulls, or cuts a strap).
    const FRallyAim Aim = SeatedCar || CarriedPart ? FRallyAim() : FindAim();
    if (Aim.Panels && Aim.Fixing != INDEX_NONE && !Aim.Panels->IsFixingOut(Aim.Slot, Aim.Fixing))
    {
        bHoldOut = true;
        HoldTime = 0.0f;
        return;
    }
    if (Aim.Panels) ServerRemovePart(Cast<ARallyCarPawn>(Aim.Panels->GetOwner()), Aim.Slot);
}

void ARallyCharacter::OnCrouchPressed(const FInputActionValue& Value) { if (!SeatedCar) Crouch(); }
void ARallyCharacter::OnCrouchReleased(const FInputActionValue& Value) { UnCrouch(); }
void ARallyCharacter::OnLean(const FInputActionValue& Value)
{
    bLeanHeld = Value.Get<bool>();
    LeanStick = FVector2D::ZeroVector;
}

void ARallyCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    // The capsule drops at once; the eyes follow it down over a moment.
    CrouchEyeOffset += ScaledHalfHeightAdjust;
    BodyMesh->SetRelativeScale3D(FVector(0.55, 0.55, 1.76 * 40.0 / 88.0));
}

void ARallyCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    CrouchEyeOffset -= ScaledHalfHeightAdjust;
    BodyMesh->SetRelativeScale3D(FVector(0.55, 0.55, 1.76));
}

void ARallyCharacter::UpdateHead(float DeltaSeconds)
{
    CrouchEyeOffset = FMath::FInterpTo(CrouchEyeOffset, 0.0f, DeltaSeconds, 10.0f);
    // Q alone leans forward (over an engine bay, under a wheel arch); with A, D or S to that side or back.
    FVector2D Wanted = FVector2D::ZeroVector;
    if (bLeanHeld && !SeatedCar)
    {
        Wanted = LeanStick.SizeSquared() < 0.04 ? FVector2D(1.0, 0.0) : FVector2D(LeanStick.Y, LeanStick.X).GetSafeNormal();
        Wanted.X = FMath::Max(Wanted.X, -0.5);
    }
    Lean.X = FMath::FInterpTo(Lean.X, Wanted.X, DeltaSeconds, 7.0f);
    Lean.Y = FMath::FInterpTo(Lean.Y, Wanted.Y, DeltaSeconds, 7.0f);
    if (SeatedCar)
    {
        return;
    }
    // Up to 40 cm forward (bending down a little) or 35 cm to the side (tilting a little), stopping short of
    // walls. The parts of a car do not stop it: leaning into a wheel arch or past a wing is how a bolt
    // behind it is seen and reached (aiming looks through a part the eyes are inside).
    const FVector Head = GetActorLocation() + FVector(0.0, 0.0, EyeHeight + CrouchEyeOffset);
    FVector Offset = GetActorForwardVector() * (40.0 * Lean.X) + GetActorRightVector() * (35.0 * Lean.Y)
        - FVector(0.0, 0.0, 12.0 * FMath::Max(Lean.X, 0.0) + 6.0 * FMath::Abs(Lean.Y));
    const double Length = Offset.Size();
    if (Length > 1.0)
    {
        const FVector Direction = Offset / Length;
        double Allowed = Length;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyLean), false, this);
        if (CarriedPart) Params.AddIgnoredActor(CarriedPart);
        for (TActorIterator<ARallyCarPawn> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
        FHitResult Hit;
        if (GetWorld()->SweepSingleByChannel(Hit, Head, Head + Offset, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(12.0f), Params))
        {
            Allowed = FMath::Min(Allowed, Hit.Distance - 2.0);
        }
        Offset = Direction * FMath::Clamp(Allowed, 0.0, Length);
    }
    FirstPersonCamera->SetRelativeLocation(GetActorTransform().InverseTransformVectorNoScale(Offset) + FVector(0.0, 0.0, EyeHeight + CrouchEyeOffset));
    if (Controller && IsLocallyControlled())
    {
        FRotator View = Controller->GetControlRotation();
        const double Time = GetWorld()->GetTimeSeconds();
        View.Roll = 8.0f * Lean.Y + static_cast<float>(Rally::Body::SwayDeg(Drunk) * FMath::Sin(Time * 0.9));
        Controller->SetControlRotation(View);
    }
}
void ARallyCharacter::ServerTogglePanel_Implementation(ARallyCarPawn* Car, int32 Panel)
{
    if (SeatedCar)
    {
        SeatedCar->GetPanels()->ToggleOpenFromInside(SeatIndex);
        return;
    }
    if (!CarriedPart && IsValid(Car)) Car->GetPanels()->ToggleOpen(this, Panel);
}
void ARallyCharacter::ServerRemovePart_Implementation(ARallyCarPawn* Car, int32 Part)
{
    if (!SeatedCar && !CarriedPart && IsValid(Car)) Car->GetPanels()->Remove(this, Part);
}
bool ARallyCharacter::AdvanceRemoval(const AActor* Car, int32 Part, int32 Required)
{
    if (!HasAuthority()) return false;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - LastRemoveTime < 0.75) return false;
    if (Now - LastRemoveTime > 4.0 || RemoveCar.Get() != Car || RemovePart != Part) RemoveActions = 0;
    RemoveCar = Car;
    RemovePart = Part;
    LastRemoveTime = Now;
    ++RemoveActions;
    UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL remove_action=%d/%d part=%d"), RemoveActions, Required, Part);
    if (RemoveActions < Required) return false;
    RemoveActions = 0;
    return true;
}
void ARallyCharacter::ServerDropPart_Implementation()
{
    if (!CarriedPart) return;
    // Looking into the open boot, it goes in rather than falling through the car.
    if (ARallyCarPawn* Car = CarriedPart->GetSourceCar())
        if (Car->GetPanels()->FitTarget(this, CarriedPart) == INDEX_NONE && Car->GetPanels()->CanStow(this, CarriedPart)
            && Car->GetPanels()->FacesBoot(this) && Car->GetPanels()->Stow(this, CarriedPart)) return;
    CarriedPart->Drop();
}
void ARallyCharacter::ServerBodgePanel_Implementation(ARallyCarPawn* Car, int32 Panel)
{
    if (SeatedCar || !IsValid(Car)) return;
    // A carried part is strapped on where it fits; otherwise the chosen panel is strapped shut or the
    // chosen arm hammered straight.
    if (CarriedPart && UseCarriedVice()) return;
    if (CarriedPart) Car->GetPanels()->Refit(this, CarriedPart, true);
    else Car->GetPanels()->StrapPanel(this, Panel);
}

void ARallyCharacter::TickLeanTest(float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
    static const bool bLeanTest = FParse::Param(FCommandLine::Get(), TEXT("RallyLeanTest"));
    TActorIterator<ARallyCarPawn> It(GetWorld());
    if (!bLeanTest || !It || SeatedCar || !IsLocallyControlled()) return;
    ARallyCarPawn* Car = *It;
    URallyPanelComponent* Panels = Car->GetPanels();
    LeanTestTime += DeltaSeconds;
    // Every fixing of the front left corner's wheel and running gear (behind the wheel, out of reach while
    // it is on), aimed at from the eyes as they are: how many the crosshair lands on.
    const auto Reached = [&](const TCHAR* Label)
    {
        TArray<int32> Parts = { URallyPanelComponent::FirstWheel };
        for (const URallyPanelComponent::EGear Kind : { URallyPanelComponent::EGear::Strut, URallyPanelComponent::EGear::Arm,
                URallyPanelComponent::EGear::Disc, URallyPanelComponent::EGear::Driveshaft })
            Parts.Add(URallyPanelComponent::GearSlot(Kind, 0));
        const FRotator Was = Controller ? Controller->GetControlRotation() : FRotator::ZeroRotator;
        int32 Count = 0;
        FString List;
        for (const int32 Part : Parts)
            for (int32 F = 0; F < Panels->NumFixings(Part); ++F)
            {
                const FVector Eye = FirstPersonCamera->GetComponentLocation();
                if (Controller) Controller->SetControlRotation((Panels->GetFixingLocation(Part, F) - Eye).Rotation());
                const FRallyAim Aim = FindAim();
                const bool bOn = Aim.Slot == Part && Aim.Fixing == F;
                Count += bOn;
                List += FString::Printf(TEXT(" %s%d:%s"), *URallyPanelComponent::PanelName(Part).Replace(TEXT(" "), TEXT("_")), F,
                    bOn ? TEXT("on") : Aim.Slot == INDEX_NONE ? TEXT("none") : *URallyPanelComponent::PanelName(Aim.Slot).Replace(TEXT(" "), TEXT("_")));
            }
        if (Controller) Controller->SetControlRotation(Was);
        UE_LOG(LogTemp, Display, TEXT("RALLY_LEANTEST %s eye=%s reached=%d:%s"), Label,
            *Car->GetActorTransform().InverseTransformPosition(FirstPersonCamera->GetComponentLocation()).ToCompactString(), Count, *List);
        return Count;
    };
    // Looking down past the wing at the top of the strut, as someone bent over the wheel would.
    const auto LookIn = [&]()
    {
        const FVector Strut = Panels->GetFixingLocation(URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Strut, 0), 2);
        if (Controller) Controller->SetControlRotation((Strut - FirstPersonCamera->GetComponentLocation()).Rotation());
    };
    if (LeanTestStep == 0 && LeanTestTime > 2.0f)
    {
        // Beside the front left wheel, facing the car, just clear of its box; crouched.
        const FTransform Frame = Car->GetActorTransform();
        const FVector At = Frame.TransformPosition(FVector(126.3, -122.0, -80.0 + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.0));
        const float Yaw = Frame.Rotator().Yaw + 90.0f;
        SetActorLocationAndRotation(At, FRotator(0.0f, Yaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
        if (Controller) Controller->SetControlRotation(FRotator(-30.0f, Yaw, 0.0f));
        Crouch();
        LeanTestStep = 1;
        LeanTestTime = 0.0f;
    }
    else if (LeanTestStep == 1 && LeanTestTime > 1.5f)
    {
        LeanTestReachedStraight = Reached(TEXT("straight"));
        LookIn();
        FScreenshotRequest::RequestScreenshot(TEXT("RallyLean_straight.png"), false, false);
        LeanTestStep = 2;
        LeanTestTime = 0.0f;
    }
    else if (LeanTestStep == 2 && LeanTestTime > 0.5f)
    {
        bLeanHeld = true;
        LeanStick = FVector2D::ZeroVector;
        LeanTestStep = 3;
        LeanTestTime = 0.0f;
    }
    else if (LeanTestStep == 3 && LeanTestTime > 1.0f)
    {
        const int32 Leaning = Reached(TEXT("leaning"));
        LookIn();
        FScreenshotRequest::RequestScreenshot(TEXT("RallyLean_leaning.png"), false, false);
        // The eyes must have gone past the wing's outer face (the car's side is 86 cm out), where the head
        // used to stop 10 cm short of the car's parts, and still reach bolts from there (from over the wheel
        // its lower bolts are behind the tyre).
        const double EyeOut = -Car->GetActorTransform().InverseTransformPosition(FirstPersonCamera->GetComponentLocation()).Y;
        const bool bPass = Leaning > 0 && EyeOut < 86.0;
        UE_LOG(LogTemp, Display, TEXT("RALLY_LEANTEST_DONE result=%s straight=%d leaning=%d eye_out=%.1f"), bPass ? TEXT("pass") : TEXT("FAIL"),
            LeanTestReachedStraight, Leaning, EyeOut);
        LeanTestStep = 4;
        LeanTestTime = 0.0f;
    }
    else if (LeanTestStep == 4 && LeanTestTime > 1.0f)
    {
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyLeanTest"));
        LeanTestStep = 5;
    }
#endif
}

void ARallyCharacter::TickBootTest(float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
    static const bool bBootTest = FParse::Param(FCommandLine::Get(), TEXT("RallyBootTest"));
    TActorIterator<ARallyCarPawn> It(GetWorld());
    if (!bBootTest || !It || SeatedCar || !IsLocallyControlled() || !HasAuthority()) return;
    ARallyCarPawn* Car = *It;
    URallyPanelComponent* Panels = Car->GetPanels();
    BootTestTime += DeltaSeconds;
    const FTransform Frame = Car->GetActorTransform();
    // The first-aid kit, wherever it is.
    ARallyDebrisActor* Kit = nullptr;
    for (TActorIterator<ARallyDebrisActor> Part(GetWorld()); Part && !Kit; ++Part)
        if (Part->GetSourceCar() == Car && Part->GetPanel() == URallyPanelComponent::FirstAidSlot) Kit = *Part;
    const auto Face = [&](const FVector& Target)
    {
        const FRotator Look = (Target - FirstPersonCamera->GetComponentLocation()).Rotation();
        SetActorRotation(FRotator(0.0f, Look.Yaw, 0.0f));
        if (Controller) Controller->SetControlRotation(Look);
    };
    const FVector Boot = Frame.TransformPosition(FVector(-180.0, 0.0, -20.0));
    if (BootTestStep == 0 && BootTestTime > 2.0f)
    {
        // Behind the car, the boot lid up.
        Panels->DebugSetOpen(5, true);
        SetActorLocation(Frame.TransformPosition(FVector(-300.0, 0.0, -80.0 + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.0)),
            false, nullptr, ETeleportType::TeleportPhysics);
        BootTestStep = 1;
        BootTestTime = 0.0f;
    }
    else if (BootTestStep == 1 && BootTestTime > 1.5f && Kit)
    {
        Face(Kit->GetActorLocation());
        ServerInteract(Kit);
        BootTestStep = 2;
        BootTestTime = 0.0f;
    }
    else if (BootTestStep == 2 && BootTestTime > 0.6f)
    {
        // Carrying it, looking into the boot: its place shows brighter, and the prompt offers G.
        Face(Boot);
        BootTestPrompt = PromptText;
        FScreenshotRequest::RequestScreenshot(TEXT("RallyBoot_carrying.png"), false, false);
        BootTestStep = 3;
        BootTestTime = 0.0f;
    }
    else if (BootTestStep == 3 && BootTestTime > 0.3f)
    {
        UE_LOG(LogTemp, Display, TEXT("RALLY_BOOTTEST carrying=%d prompt=\"%s\""), CarriedPart == Kit ? 1 : 0, *BootTestPrompt);
        ServerDropPart();
        BootTestStep = 4;
        BootTestTime = 0.0f;
    }
    else if (BootTestStep == 4 && BootTestTime > 0.6f)
    {
        FVector Home;
        bBootTestBack = Kit && !CarriedPart && Kit->GetMount() == ERallyPartMount::Stowed
            && Panels->GetBootHome(URallyPanelComponent::FirstAidSlot, Home) && FVector::Dist(Kit->GetMountLocation(), Home) < 0.1;
        UE_LOG(LogTemp, Display, TEXT("RALLY_BOOTTEST G looking into the boot: %s"), bBootTestBack ? TEXT("back in its place") : TEXT("NOT back"));
        FScreenshotRequest::RequestScreenshot(TEXT("RallyBoot_back.png"), false, false);
        BootTestStep = 5;
        BootTestTime = 0.0f;
    }
    else if (BootTestStep == 5 && BootTestTime > 0.6f && Kit)
    {
        Face(Kit->GetActorLocation());
        ServerInteract(Kit);
        BootTestStep = 6;
        BootTestTime = 0.0f;
    }
    else if (BootTestStep == 6 && BootTestTime > 0.6f)
    {
        // Turned away from the car: G drops it where the player stands.
        Face(Frame.TransformPosition(FVector(-600.0, 200.0, -80.0)));
        BootTestStep = 7;
        BootTestTime = 0.0f;
    }
    else if (BootTestStep == 7 && BootTestTime > 0.3f)
    {
        ServerDropPart();
        BootTestStep = 8;
        BootTestTime = 0.0f;
    }
    else if (BootTestStep == 8 && BootTestTime > 1.0f)
    {
        const bool bDropped = Kit && !CarriedPart && Kit->GetMount() == ERallyPartMount::Loose;
        const bool bPass = bBootTestBack && bDropped && BootTestPrompt.Contains(TEXT("E or G: strap it into its place"));
        UE_LOG(LogTemp, Display, TEXT("RALLY_BOOTTEST_DONE result=%s back=%d dropped_away=%d"), bPass ? TEXT("pass") : TEXT("FAIL"),
            bBootTestBack ? 1 : 0, bDropped ? 1 : 0);
        BootTestStep = 9;
        BootTestTime = 0.0f;
    }
    else if (BootTestStep == 9 && BootTestTime > 0.5f)
    {
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyBootTest"));
        BootTestStep = 10;
    }
#endif
}

void ARallyCharacter::TickPanelTest(float Dt)
{
#if !UE_BUILD_SHIPPING
    if (!FParse::Param(FCommandLine::Get(), TEXT("RallyPanelTest"))) return;
    TActorIterator<ARallyCarPawn> It(GetWorld());
    if (!It || SeatedCar) return;
    ARallyCarPawn* Car = *It;
    const uint8 Phase = Car->GetPanelTestPhase();
    if (!Phase) return;
    bAutoSeatDone = true;
    if (CarriedPart) bPanelTestSawCarry = true;
    if (Phase >= 4 && bPanelTestSawCarry && !CarriedPart) bPanelTestSawRefit = true;
    PanelTestActionTimer += Dt;
    if (PanelTestActionTimer < 0.9f) return;
    PanelTestActionTimer = 0;
    if ((Phase == 2 || Phase == 5 || Phase == 8 || Phase == 15) && !CarriedPart)
    {
        if (ARallyDebrisActor* Part = Car->GetPanels()->GetDebris(Phase == 2 ? 7 : Phase == 15 ? URallyPanelComponent::FirstWheel : 4))
        {
            const FRotator Look = (Part->GetActorLocation() - GetActorLocation()).Rotation();
            SetActorRotation(FRotator(0, Look.Yaw, 0));
            if (Controller) Controller->SetControlRotation(FRotator(0, Look.Yaw, 0));
            // Wait for debris to settle and the server's test placement to reach this client.
            // Otherwise E at the starting door would simply get back into the car.
            const bool bReady = Part->CanPickUp(this);
            UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST pickup_ready=%d player=%s part=%s speed=%.1f"), bReady,
                *GetActorLocation().ToCompactString(), *Part->GetActorLocation().ToCompactString(), Part->GetVelocity().Size());
            if (bReady) ServerInteract(Part);
        }
    }
    if (Phase == 1 || Phase == 4)
    {
        const FVector Target = Car->GetActorLocation() + Car->GetActorForwardVector() * 190.0;
        if (Controller) Controller->SetControlRotation((Target - FirstPersonCamera->GetComponentLocation()).Rotation());
        FScreenshotRequest::RequestScreenshot(Phase == 1 ? TEXT("RallyPanel_hanging.png") : TEXT("RallyPanel_refitted.png"), false, false);
    }
    if (Phase == 3) FScreenshotRequest::RequestScreenshot(TEXT("RallyPanel_carried.png"), false, false);
    // Turns the next fixing of Part home (or out) with its own tool, aimed at as a player would; true once
    // none is left to turn.
    const auto WorkFixings = [&](int32 Part, bool bHome) {
        URallyPanelComponent* Panels = Car->GetPanels();
        for (int32 F = 0; F < Panels->NumFixings(Part); ++F)
        {
            if (Panels->IsFixingOut(Part, F) != bHome) continue;
            SelectTool(static_cast<int32>(Panels->FixingTool(Part, F)));
            const FVector Eye = FirstPersonCamera->GetComponentLocation();
            if (Controller) Controller->SetControlRotation((Panels->GetFixingLocation(Part, F) - Eye).Rotation());
            const FRallyAim Aim = FindAim();
            UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST %s part=%d fixing=%d aimed part=%d fixing=%d"),
                Aim.Slot == Part && Aim.Fixing == F ? TEXT("aim_on") : TEXT("aim_miss"), Part, F, Aim.Slot, Aim.Fixing);
            ServerTurnFixing(Car, Part, F, bHome);
            return false;
        }
        return true;
    };
    if ((Phase == 3 || Phase == 6 || Phase == 16) && CarriedPart) ServerInteract(nullptr);
    else if (Phase == 3 && !Car->GetPanels()->IsDetached(7)) WorkFixings(7, true);
    else if (Phase == 16 && !Car->GetPanels()->GetDebris(URallyPanelComponent::FirstWheel)) WorkFixings(URallyPanelComponent::FirstWheel, true);
    if (Phase == 7 && CarriedPart) RequestBodge();
    // Look at the part before working on it: F and R act on what is in view.
    // Aims the crosshair at a point of the part that the view ray reaches before anything else.
    const auto LookAtPart = [&](int32 Part) {
        const FVector Eye = FirstPersonCamera->GetComponentLocation();
        if (Controller) Controller->SetControlRotation((Car->GetPanels()->GetAimPoint(Part, Eye, bIsCrouched != 0) - Eye).Rotation());
    };
    // Under the car (sump, gearbox, tank, exhaust) the test crouches like a player would.
    // So are wheel bolts and the running gear behind the wheel.
    const bool bLow = (Phase >= 51 && Phase <= 54) || Phase == 16 || Phase == 36 || Phase == 37 || Phase == 39 || Phase == 42;
    if (bLow && !bIsCrouched) Crouch();
    if (!bLow && bIsCrouched) UnCrouch();
    if (Phase == 9 && !Car->GetPanels()->IsOpen(4)) { LookAtPart(4); RequestTogglePanel(); }
    if (Phase == 11 && Car->GetPanels()->IsOpen(4)) { LookAtPart(4); RequestTogglePanel(); }
    if (Phase == 24 && !CarriedPart)
    {
        if (!Car->GetPanels()->IsOpen(5)) { LookAtPart(5); RequestTogglePanel(); }
        else if (WorkFixings(5, false)) { LookAtPart(5); RequestRemovePart(); }
    }
    if (Phase == 25)
    {
        if (CarriedPart) ServerInteract(nullptr);
        else if (!Car->GetPanels()->IsDetached(5))
        {
            if (!Car->GetPanels()->IsOpen(5)) { LookAtPart(5); RequestTogglePanel(); }
            else WorkFixings(5, true);
        }
    }
    if (Phase == 26 && !CarriedPart) { LookAtPart(4); RequestRemovePart(); }
    if (Phase == 27 && CarriedPart) ServerDropPart();
    if (Phase == 10 || Phase == 13 || Phase == 14 || Phase == 18 || Phase == 21 || Phase == 23)
    {
        // Look at the bonnet, the front-left corner, the driver's door against the post, the door the
        // post slammed shut, then the left side with its rear door torn off.
        const FVector Target = Phase == 14 ? Car->GetActorTransform().TransformPosition(FVector(130, -80, -50))
            : Phase == 18 ? Car->GetActorTransform().TransformPosition(FVector(20, -120, -10))
            : Phase == 21 ? Car->GetActorTransform().TransformPosition(FVector(30, -90, 0))
            : Phase == 23 ? Car->GetActorTransform().TransformPosition(FVector(-60, -90, -10))
            : Car->GetActorLocation() + Car->GetActorForwardVector() * 150.0;
        if (Controller) Controller->SetControlRotation((Target - FirstPersonCamera->GetComponentLocation()).Rotation());
        FScreenshotRequest::RequestScreenshot(Phase == 10 ? TEXT("RallyPanel_bonnet_open.png")
            : Phase == 13 ? TEXT("RallyPanel_bonnet_droop.png") : Phase == 14 ? TEXT("RallyPanel_wheel_off.png")
            : Phase == 18 ? TEXT("RallyPanel_door_post.png") : Phase == 21 ? TEXT("RallyPanel_door_slammed.png")
            : TEXT("RallyPanel_door_torn.png"), false, false);
    }
    // Running gear: jack, wheel, strut, spare, a bent arm. The client acts through ordinary input.
    const int32 StrutSlot = URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Strut, 0);
    const int32 ArmSlot = URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Arm, 0);
    const auto PickSpare = [&](int32 Slot) {
        for (TActorIterator<ARallyDebrisActor> Part(GetWorld()); Part; ++Part)
        {
            if (Part->GetSourceCar() != Car || Part->GetPanel() != Slot || Part->GetMount() != ERallyPartMount::Stowed) continue;
            if (Controller) Controller->SetControlRotation((Part->GetActorLocation() - FirstPersonCamera->GetComponentLocation()).Rotation());
            if (Part->CanPickUp(this)) ServerInteract(*Part);
            return;
        }
    };
    if (Phase == 33 && !Car->GetPanels()->IsOpen(5)) { LookAtPart(5); RequestTogglePanel(); }
    if (Phase == 34 && !CarriedPart) PickSpare(URallyPanelComponent::JackSlot);
    if (Phase == 35 && CarriedPart) ServerInteract(nullptr);
    if (Phase == 36)
    {
        if (CarriedPart) ServerDropPart();
        else if (Car->GetPanels()->GetJackCorner() == 0 && WorkFixings(URallyPanelComponent::FirstWheel, false))
        {
            LookAtPart(URallyPanelComponent::FirstWheel);
            RequestRemovePart();
        }
    }
    if (Phase == 37)
    {
        if (CarriedPart) ServerDropPart();
        else if (!Car->GetPanels()->IsRemoved(StrutSlot) && WorkFixings(StrutSlot, false)) { LookAtPart(StrutSlot); RequestRemovePart(); }
    }
    if (Phase == 38 && !CarriedPart) PickSpare(StrutSlot);
    if (Phase == 39)
    {
        URallyPanelComponent* Panels = Car->GetPanels();
        if (CarriedPart) ServerInteract(nullptr);
        else if (!Panels->IsRemoved(StrutSlot) && WorkFixings(StrutSlot, true))
        {
            // Damper three clicks harder with the screwdriver, then the collar a centimetre down with the 19 mm.
            const bool bDamper = Panels->GetClicks(StrutSlot) < 8;
            SelectTool(static_cast<int32>(bDamper ? Rally::Fixing::ETool::Screwdriver : Rally::Fixing::ETool::Spanner19));
            LookAtPart(StrutSlot);
            if (bDamper || Panels->GetCollarCm(StrutSlot) > -1) ServerAdjust(Car, StrutSlot, bDamper ? 1 : -1);
        }
    }
    if (Phase == 40) { SelectTool(static_cast<int32>(Rally::Fixing::ETool::Hammer)); LookAtPart(ArmSlot); RequestBodge(); }
    if (Phase == 41 && !CarriedPart)
    {
        if (ARallyDebrisActor* Wheel = Car->GetPanels()->GetDebris(URallyPanelComponent::FirstWheel))
        {
            if (Controller) Controller->SetControlRotation((Wheel->GetActorLocation() - FirstPersonCamera->GetComponentLocation()).Rotation());
            if (Wheel->CanPickUp(this)) ServerInteract(Wheel);
        }
    }
    if (Phase == 42)
    {
        if (CarriedPart) ServerInteract(nullptr);
        else if (!Car->GetPanels()->GetDebris(URallyPanelComponent::FirstWheel)) WorkFixings(URallyPanelComponent::FirstWheel, true);
    }
    if (Phase == 43 && !CarriedPart && Car->GetPanels()->GetJackCorner() == 0) ServerInteract(nullptr);
    if (Phase == 44 || Phase == 36 || Phase == 39)
    {
        // The jacked corner, the strut being swapped, the wheel back on and the jack lowered.
        const FVector Target = Car->GetActorTransform().TransformPosition(FVector(126, -75, -50));
        if (Controller && Phase != 36 && Phase != 39) Controller->SetControlRotation((Target - FirstPersonCamera->GetComponentLocation()).Rotation());
        FScreenshotRequest::RequestScreenshot(Phase == 36 ? TEXT("RallyGear_jacked.png") : Phase == 39 ? TEXT("RallyGear_strut_out.png")
            : TEXT("RallyGear_done.png"), false, false);
    }
    // Engine bay: bonnet, sealant, battery terminals, water can.
    const int32 Radiator = URallyPanelComponent::RadiatorSlot;
    if (Phase == 45 && !Car->GetPanels()->IsOpen(4)) { LookAtPart(4); RequestTogglePanel(); }
    if (Phase == 46)
    {
        const FVector Target = Car->GetActorTransform().TransformPosition(FVector(150, 0, -20));
        if (Controller) Controller->SetControlRotation((Target - FirstPersonCamera->GetComponentLocation()).Rotation());
        FScreenshotRequest::RequestScreenshot(TEXT("RallyEngine_bay.png"), false, false);
        LookAtPart(Radiator);
        RequestBodge();
    }
    if (Phase == 47) { SelectTool(static_cast<int32>(Rally::Fixing::ETool::Spanner10)); LookAtPart(URallyPanelComponent::BatterySlot); RequestBodge(); }
    if (Phase == 48 && !CarriedPart) PickSpare(URallyPanelComponent::WaterSlot);
    if (Phase == 49 && CarriedPart) { LookAtPart(Radiator); ServerInteract(nullptr); }
    if (Phase == 50)
    {
        if (CarriedPart) ServerDropPart();
        else if (Car->GetPanels()->IsOpen(4)) { LookAtPart(4); RequestTogglePanel(); }
    }
    // Under the car: putty on the sump and tank, the exhaust pulled off, oil from the boot.
    if (Phase == 51) { LookAtPart(URallyPanelComponent::SumpSlot); RequestBodge(); }
    if (Phase == 52) { SelectTool(static_cast<int32>(Rally::Fixing::ETool::Spanner10)); LookAtPart(URallyPanelComponent::GearboxSlot); RequestBodge(); }
    if (Phase == 53) { LookAtPart(URallyPanelComponent::TankSlot); RequestBodge(); }
    if (Phase == 54 && !CarriedPart && !Car->GetPanels()->IsRemoved(URallyPanelComponent::ExhaustSlot))
    {
        const FVector Target = Car->GetActorTransform().TransformPosition(FVector(-200, -40, -60));
        if (Controller) Controller->SetControlRotation((Target - FirstPersonCamera->GetComponentLocation()).Rotation());
        FScreenshotRequest::RequestScreenshot(TEXT("RallyUnderside_exhaust_hanging.png"), false, false);
        LookAtPart(URallyPanelComponent::ExhaustSlot);
        RequestRemovePart();
    }
    if (Phase == 55 && !Car->GetPanels()->IsOpen(4)) { LookAtPart(4); RequestTogglePanel(); }
    if (Phase == 56 && !CarriedPart) PickSpare(URallyPanelComponent::OilBottleSlot);
    if (Phase == 57 && CarriedPart) { LookAtPart(URallyPanelComponent::OilFillerSlot); ServerInteract(nullptr); }
    if (Phase == 58)
    {
        if (CarriedPart) ServerDropPart();
        else if (Car->GetPanels()->IsOpen(4)) { LookAtPart(4); RequestTogglePanel(); }
    }
    // Engine modules: the cover, the air filter box and the turbo off and back on, from in front.
    using EModule = URallyPanelComponent::EModule;
    const auto TakeOff = [&](int32 Slot) {
        if (CarriedPart) { ServerDropPart(); return; }
        if (!Car->GetPanels()->IsRemoved(Slot) && WorkFixings(Slot, false))
        {
            LookAtPart(Slot);
            const FRallyAim Aim = FindAim();
            UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST lift part=%d aimed part=%d fixing=%d reach=%d"), Slot, Aim.Slot, Aim.Fixing,
                Car->GetPanels()->CanReach(this, Slot) ? 1 : 0);
            RequestRemovePart();
        }
    };
    const auto PutBack = [&](int32 Slot) {
        URallyPanelComponent* Panels = Car->GetPanels();
        if (!Panels->IsRemoved(Slot)) { WorkFixings(Slot, true); return; }
        if (CarriedPart) { ServerInteract(nullptr); return; }
        if (ARallyDebrisActor* Part = Panels->GetDebris(Slot))
        {
            if (Controller) Controller->SetControlRotation((Part->GetActorLocation() - FirstPersonCamera->GetComponentLocation()).Rotation());
            if (Part->CanPickUp(this)) ServerInteract(Part);
        }
    };
    if (Phase == 59 && !Car->GetPanels()->IsOpen(4)) { LookAtPart(4); RequestTogglePanel(); }
    if (Phase == 60) TakeOff(URallyPanelComponent::ModuleSlot(EModule::Cover));
    if (Phase == 61) TakeOff(URallyPanelComponent::ModuleSlot(EModule::AirFilter));
    if (Phase == 62) TakeOff(URallyPanelComponent::ModuleSlot(EModule::Turbo));
    if (Phase == 63)
    {
        const int32 Turbo = URallyPanelComponent::ModuleSlot(EModule::Turbo);
        if (Car->GetPanels()->IsRemoved(Turbo) || !Car->GetPanels()->AllHome(Turbo)) PutBack(Turbo);
        else if (Car->GetPanels()->GetBoostTenths(Turbo) < 10)
        {
            SelectTool(static_cast<int32>(Rally::Fixing::ETool::Spanner10));
            LookAtPart(Turbo);
            ServerAdjust(Car, Turbo, 1);
        }
    }
    if (Phase == 64) PutBack(URallyPanelComponent::ModuleSlot(EModule::AirFilter));
    if (Phase == 65)
    {
        const int32 Cover = URallyPanelComponent::ModuleSlot(EModule::Cover);
        if (Car->GetPanels()->IsRemoved(Cover) || !Car->GetPanels()->AllHome(Cover)) PutBack(Cover);
        else if (Car->GetPanels()->IsOpen(4)) { LookAtPart(4); RequestTogglePanel(); }
    }
    if (Phase == 66)
    {
        const URallyPanelComponent* Panels = Car->GetPanels();
        const bool bPass = bPanelTestSawCarry && bPanelTestSawRefit && !CarriedPart
            && !Panels->GetDebris(URallyPanelComponent::FirstWheel) && !Panels->IsOpen(4) && !Panels->IsOpen(1)
            && !Panels->IsDetached(2) && !Panels->IsDetached(4) && Panels->GetJackCorner() < 0 && !Panels->IsRemoved(StrutSlot);
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST_CLIENT_%s carry=%d refit=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"),
            bPanelTestSawCarry, bPanelTestSawRefit);
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyPanelTest"));
    }
#endif
}
