#include "Player/RallyCharacter.h"
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
}

void ARallyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyCharacter, SeatedCar);
    DOREPLIFETIME(ARallyCharacter, SeatIndex);
}

void ARallyCharacter::SetSeat(ARallyCarPawn* Car, int32 Seat)
{
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
    if (!IsLocallyControlled())
    {
        return;
    }
    if (SeatedCar)
    {
        // The stick points the head; mouse look accumulates.
        const FRotator Look(FMath::Clamp(SeatLook.Pitch + StickLook.Y * 40.0f, -60.0f, 40.0f),
            FMath::Clamp(SeatLook.Yaw + StickLook.X * 120.0f, -150.0f, 150.0f), 0.0f);
        FirstPersonCamera->SetRelativeRotation(Look);
    }
    TickAutoSeat(DeltaSeconds);
    UpdatePrompt();
}

void ARallyCharacter::UpdatePrompt()
{
#if !UE_BUILD_SHIPPING
    FString Prompt;
    if (SeatedCar)
    {
        Prompt = SeatIndex == 0 ? TEXT("") : TEXT("Co-driver seat   E: get out");
    }
    else
    {
        int32 Seat = INDEX_NONE;
        if (const ARallyCarPawn* Car = ARallyCarPawn::FindSeatInReach(this, Seat))
        {
            Prompt = !Car->IsSeatFree(Seat) ? TEXT("Seat taken") : Seat == 0 ? TEXT("E: drive") : TEXT("E: ride as co-driver");
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
        ServerInteract();
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

void ARallyCharacter::ServerInteract_Implementation()
{
    UE_LOG(LogTemp, Display, TEXT("RALLY_SEAT %s interact (seated=%d)"), *GetName(), SeatedCar ? 1 : 0);
    if (SeatedCar)
    {
        SeatedCar->ServerExitSeat(this);
        return;
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
    SprintAction = MakeAction(this, EInputActionValueType::Boolean);

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
    OnFootContext->MapKey(SprintAction, EKeys::LeftShift);
    OnFootContext->MapKey(SprintAction, EKeys::Gamepad_LeftThumbstick);
}

void ARallyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    CreateInputObjects();
    UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
    Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ARallyCharacter::OnMove);
    Input->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ARallyCharacter::OnLook);
    Input->BindAction(StickLookAction, ETriggerEvent::Triggered, this, &ARallyCharacter::OnStickLook);
    Input->BindAction(StickLookAction, ETriggerEvent::Completed, this, &ARallyCharacter::OnStickLook);
    Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ARallyCharacter::OnJump);
    Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ARallyCharacter::OnInteract);
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
    if (!SeatedCar)
    {
        Jump();
    }
}

void ARallyCharacter::OnInteract(const FInputActionValue& Value)
{
    ServerInteract();
}

void ARallyCharacter::OnSprint(const FInputActionValue& Value)
{
    SetSprint(Value.Get<bool>());
    ServerSetSprint(Value.Get<bool>());
}

void ARallyCharacter::SetSprint(bool bSprint)
{
    GetCharacterMovement()->MaxWalkSpeed = bSprint ? SprintSpeed : WalkSpeed;
}

void ARallyCharacter::ServerSetSprint_Implementation(bool bSprint)
{
    // Speed must match on the server, or its movement checks pull the sprinting player back.
    SetSprint(bSprint);
}
