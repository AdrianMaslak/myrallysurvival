#include "Player/RallyCharacter.h"
#include "Player/RallyCharacterMovement.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputActionValue.h"
#include "Vehicle/RallyCarPawn.h"
#include "Damage/RallyPanelComponent.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"

void ARallyCharacter::TickProneTest(float Dt)
{
#if !UE_BUILD_SHIPPING
    if (!FParse::Param(FCommandLine::Get(), TEXT("RallyProneTest")) || !HasAuthority() || !IsLocallyControlled()) return;
    ProneTestTime += Dt;
    auto* Movement = CastChecked<URallyCharacterMovement>(GetCharacterMovement());
    auto Check = [&](bool bOK, const TCHAR* What)
    {
        bProneTestPassed &= bOK;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PRONETEST %s=%s"), What, bOK ? TEXT("pass") : TEXT("FAIL"));
    };
    if (ProneTestStep == 0 && ProneTestTime > 2.0f && Movement->IsMovingOnGround())
    {
        ProneTestStart = GetActorLocation();
        ProneTestFloor = ProneTestStart.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        OnProne(FInputActionValue(true));
        ProneTestStep = 1;
        ProneTestTime = 0;
    }
    else if (ProneTestStep == 1 && ProneTestTime > 0.5f)
    {
        Check(IsProne(), TEXT("enter_from_standing"));
        Check(FMath::Abs(GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - ProneTestFloor) < 3, TEXT("feet_preserved"));
        Check(GetPawnViewLocation().Z - ProneTestFloor < 30, TEXT("eyes_below_30cm"));
        SetSprint(true);
        Check(Movement->GetMaxSpeed() <= 65, TEXT("sprint_capped"));
        OnJump(FInputActionValue(true));
        Check(!bPressedJump, TEXT("no_prone_jump"));
        Movement->bWantsToProne = true;
        // A 44 cm gap: the prone collision fits, but neither crouching nor standing does.
        AActor* Ceiling = GetWorld()->SpawnActor<AActor>();
        UBoxComponent* Box = NewObject<UBoxComponent>(Ceiling);
        Ceiling->SetRootComponent(Box);
        Box->SetBoxExtent(FVector(110, 110, 10));
        Box->SetCollisionProfileName(TEXT("BlockAll"));
        Box->RegisterComponent();
        Ceiling->SetActorLocation(FVector(ProneTestStart.X, ProneTestStart.Y, ProneTestFloor + 54));
        Movement->bWantsToProne = false;
        ProneTestStep = 2;
        ProneTestTime = 0;
    }
    else if (ProneTestStep == 2 && ProneTestTime > 0.5f)
    {
        Check(IsProne(), TEXT("blocked_stand"));
        Check(GetPawnViewLocation().Z - ProneTestFloor < 30, TEXT("blocked_camera_stays_low"));
        ProneTestStep = 3;
        ProneTestTime = 0;
    }
    else if (ProneTestStep == 3)
    {
        AddMovementInput(FVector(0, 1, 0));
        if (ProneTestTime > 4)
        {
            Check(GetActorLocation().Y - ProneTestStart.Y > 150, TEXT("crawl_out"));
            Check(!IsProne(), TEXT("stand_after_clearance"));
            Check(FMath::Abs(GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - 88) < 1, TEXT("standing_capsule_restored"));
            Crouch();
            ProneTestStep = 4;
            ProneTestTime = 0;
        }
    }
    else if (ProneTestStep == 4 && ProneTestTime > 0.5f)
    {
        Check(bIsCrouched, TEXT("crouch"));
        OnProne(FInputActionValue(true));
        ProneTestStep = 5;
        ProneTestTime = 0;
    }
    else if (ProneTestStep == 5 && ProneTestTime > 0.5f)
    {
        Check(IsProne() && GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == URallyCharacterMovement::ProneHalfHeight, TEXT("prone_from_crouch"));
        UnCrouch();
        OnProne(FInputActionValue(true));
        ProneTestStep = 6;
        ProneTestTime = 0;
    }
    else if (ProneTestStep == 6 && ProneTestTime > 0.5f)
    {
        Check(!IsProne() && !bIsCrouched && GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == 88, TEXT("crouch_prone_stand"));
        if (TActorIterator<ARallyCarPawn> Car(GetWorld()); Car) Car->GetPanels()->DebugPlaceJack(0);
        ProneTestStep = 7;
        ProneTestTime = 0;
    }
    else if (ProneTestStep == 7 && ProneTestTime > 8)
    {
        if (TActorIterator<ARallyCarPawn> Car(GetWorld()); Car)
        {
            const FVector Side = Car->GetActorTransform().TransformPosition(FVector(195, -165, 0));
            SetActorLocation(FVector(Side.X, Side.Y, ProneTestFloor + 90), false, nullptr, ETeleportType::TeleportPhysics);
            Movement->StopMovementImmediately();
            Movement->bWantsToProne = true;
        }
        ProneTestStep = 8;
        ProneTestTime = 0;
    }
    else if (ProneTestStep == 8 && ProneTestTime > 0.5f)
    {
        if (TActorIterator<ARallyCarPawn> Car(GetWorld()); Car)
        {
            if (ProneTestTime < 2.5f) AddMovementInput(Car->GetActorRightVector());
            else
            {
                const FVector Local = Car->GetActorTransform().InverseTransformPosition(GetActorLocation());
                Check(IsProne() && Local.Y > -86, TEXT("crawl_under_jacked_car"));
                const FVector Target = Car->GetPanels()->GetAimPoint(URallyPanelComponent::SumpSlot, GetPawnViewLocation(), true);
                if (Controller) Controller->SetControlRotation((Target - GetPawnViewLocation()).Rotation());
                const FRallyAim Aim = FindAim();
                Check(Aim.Panels && Aim.Slot == URallyPanelComponent::SumpSlot, TEXT("aim_at_sump"));
                UE_LOG(LogTemp, Display, TEXT("RALLY_PRONETEST car_local=%s eye=%s"), *Local.ToCompactString(), *GetPawnViewLocation().ToCompactString());
                FScreenshotRequest::RequestScreenshot(TEXT("RallyProne_under_car.png"), false, false);
                ProneTestStep = 9;
                ProneTestTime = 0;
            }
        }
    }
    else if (ProneTestStep == 9 && ProneTestTime > 1)
    {
        UE_LOG(LogTemp, Display, TEXT("RALLY_PRONETEST_DONE result=%s"), bProneTestPassed ? TEXT("pass") : TEXT("FAIL"));
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyProneTest"));
        ProneTestStep = 10;
    }
#endif
}
