#include "Player/RallyCharacter.h"
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
    UpdateHead(DeltaSeconds);
    if (SeatedCar)
    {
        // The stick points the head; mouse look accumulates.
        const FRotator Look(FMath::Clamp(SeatLook.Pitch + StickLook.Y * 40.0f, -60.0f, 40.0f),
            FMath::Clamp(SeatLook.Yaw + StickLook.X * 120.0f, -150.0f, 150.0f), 0.0f);
        FirstPersonCamera->SetRelativeRotation(Look);
    }
    TickPanelTest(DeltaSeconds);
    TickAutoSeat(DeltaSeconds);
    UpdateFixingHold(DeltaSeconds);
    UpdatePrompt();
}

void ARallyCharacter::UpdatePrompt()
{
    bInteractionInView = false;
#if !UE_BUILD_SHIPPING
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
                const FString Adjust = Aim.Panels->AdjustPrompt(this, Aim.Slot);
                if (!Adjust.IsEmpty()) PanelPrompt += (PanelPrompt.IsEmpty() ? FString(Aim.Panels->SlotLabel(Aim.Slot) + TEXT(": ")) : FString(TEXT("   "))) + Adjust;
                if (Aim.Fixing != INDEX_NONE && HoldTime > 0.0f && (bHoldHome || bHoldOut))
                    PanelPrompt += FString::Printf(TEXT("   %s %d%%"), bHoldHome ? TEXT("doing up") : TEXT("undoing"),
                        FMath::RoundToInt(100.0f * FMath::Min(HoldTime / static_cast<float>(Rally::Fixing::TurnSeconds), 1.0f)));
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
            Prompt += FString::Printf(TEXT("   straps %d   sealant %d   putty %d"), Straps, Sealant, Putty);
        }
        if (RemoveActions > 0) Prompt += FString::Printf(TEXT("   taking off: %d done"), RemoveActions);
        bInteractionInView = !Prompt.IsEmpty();
        // The tool in hand, always in a corner of the eye.
        if (GEngine) GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()) + 1, 0.0f, FColor(200, 200, 200),
            FString::Printf(TEXT("In hand: %s   (1-8 or mouse wheel)"), UTF8_TO_TCHAR(Rally::Fixing::ToolName(GetHeldTool()))));
        if (Prompt.IsEmpty() && !Aimed.IsEmpty())
        {
            // Name only, in white: something to look at, nothing to do with it here.
            if (GEngine) GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::White, Aimed);
            return;
        }
    }
    if (GEngine && !Prompt.IsEmpty())
    {
        GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::Yellow, Prompt);
    }
#endif
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
        if (ARallyCarPawn* Car = CarriedPart->GetSourceCar()) Car->GetPanels()->Refit(this, CarriedPart, false);
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
    if ((!bHoldHome && !bHoldOut) || SeatedCar || CarriedPart)
    {
        HoldTime = 0.0f;
        return;
    }
    // Only while the crosshair stays on a fixing that this key would turn; another one starts over.
    const FRallyAim Aim = FindAim();
    if (!Aim.Panels || Aim.Fixing == INDEX_NONE || Aim.Panels->IsFixingOut(Aim.Slot, Aim.Fixing) != bHoldHome)
    {
        HoldTime = 0.0f;
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
    if (HoldTime < Rally::Fixing::TurnSeconds) return;
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
    using EGear = URallyPanelComponent::EGear;
    using EModule = URallyPanelComponent::EModule;
    const FString K = Kind.ToLower();
    const int32 Slot = K == TEXT("strut") ? URallyPanelComponent::GearSlot(EGear::Strut, 0) : K == TEXT("rearstrut") ? URallyPanelComponent::GearSlot(EGear::Strut, 2)
        : K == TEXT("disc") ? URallyPanelComponent::GearSlot(EGear::Disc, 0) : K == TEXT("reardisc") ? URallyPanelComponent::GearSlot(EGear::Disc, 2)
        : K == TEXT("turbo") ? URallyPanelComponent::ModuleSlot(EModule::Turbo) : K == TEXT("ecu") ? URallyPanelComponent::ModuleSlot(EModule::Ecu)
        : K == TEXT("intercooler") ? URallyPanelComponent::ModuleSlot(EModule::Intercooler) : INDEX_NONE;
    const FString G = Grade.ToLower();
    const uint8 Value = G == TEXT("worn") ? 0 : G == TEXT("sport") ? 2 : G == TEXT("rally") ? 3 : 1;
    if (Slot != INDEX_NONE) ServerSpawnPart(Slot, Value);
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

bool ARallyCharacter::AdvanceFixingTurn()
{
    if (!HasAuthority()) return false;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - LastFixingTurn < Rally::Fixing::MinTurnInterval) return false;
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
    GetCharacterMovement()->MaxWalkSpeed = CarriedPart
        ? (CarriedPart->GetMassKg() >= 18 ? 150.0f : 230.0f)
        : (bSprint ? SprintSpeed : WalkSpeed);
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
    // walls and of the parts of a car; the car's box is mostly air and does not count.
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
        for (TActorIterator<ARallyCarPawn> It(GetWorld()); It; ++It)
        {
            Params.AddIgnoredActor(*It);
            if (FVector::Dist(It->GetActorLocation(), Head) > 600.0) continue;
            const double ToPart = It->GetPanels()->RayToParts(Head, Direction, Length + 10.0);
            if (ToPart >= 0.0) Allowed = FMath::Min(Allowed, ToPart - 10.0);
        }
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
        View.Roll = 8.0f * Lean.Y;
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
void ARallyCharacter::ServerDropPart_Implementation() { if (CarriedPart) CarriedPart->Drop(); }
void ARallyCharacter::ServerBodgePanel_Implementation(ARallyCarPawn* Car, int32 Panel)
{
    if (SeatedCar || !IsValid(Car)) return;
    // A carried part is strapped on where it fits; otherwise the chosen panel is strapped shut or the
    // chosen arm or tie rod hammered straight.
    if (CarriedPart) Car->GetPanels()->Refit(this, CarriedPart, true);
    else Car->GetPanels()->StrapPanel(this, Panel);
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
    // Running gear: jack, wheel, strut, spare, tie rod. The client acts through ordinary input.
    const int32 StrutSlot = URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Strut, 0);
    const int32 TieRodSlot = URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::TieRod, 0);
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
    if (Phase == 40) { SelectTool(static_cast<int32>(Rally::Fixing::ETool::Hammer)); LookAtPart(TieRodSlot); RequestBodge(); }
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
