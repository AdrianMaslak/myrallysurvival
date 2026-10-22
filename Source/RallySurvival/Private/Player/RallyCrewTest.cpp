#include "Player/RallyCharacter.h"
#include "Player/RallyCharacterMovement.h"
#include "Player/RallyCrewMesh.h"
#include "Vehicle/RallyCarPawn.h"
#include "Damage/RallyPanelComponent.h"
#include "Damage/RallyDebrisActor.h"
#include "Damage/RallyEngineComponent.h"
#include "Game/RallyHUD.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"

void ARallyCharacter::TickCrewTest(float Dt)
{
#if !UE_BUILD_SHIPPING
    if (!FParse::Param(FCommandLine::Get(),TEXT("RallyCrewTest")) || !HasAuthority() || !IsLocallyControlled()) return;
    APlayerController* PC=Cast<APlayerController>(GetController());
    if (!PC) return;
    if (ARallyHUD* Hud=Cast<ARallyHUD>(PC->GetHUD())) { Hud->DismissLetter(); Hud->bShowHUD=false; }
    CrewTestTime+=Dt;
    TActorIterator<ARallyCarPawn> CarIt(GetWorld());
    if (!CarIt) return;
    ARallyCarPawn* Car=*CarIt;
    static const TCHAR* Labels[]={TEXT("Idle"),TEXT("Walk"),TEXT("Run"),TEXT("Jump"),TEXT("Crouch"),TEXT("CrouchWalk"),
        TEXT("LieBelly"),TEXT("CrawlBelly"),TEXT("LieBack"),TEXT("CrawlBack"),TEXT("Ratchet"),TEXT("Carry"),
        TEXT("Drive"),TEXT("Passenger"),TEXT("FirstPersonRatchet"),TEXT("FirstPersonDrive")};
    if (CrewTestStep<0)
    {
        if (CrewTestTime<2) return;
        CrewTestOrigin=GetActorLocation()+FVector(200,0,0);
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        CrewTestSubject=GetWorld()->SpawnActor<ARallyCharacter>(ARallyCharacter::StaticClass(),CrewTestOrigin,FRotator::ZeroRotator,Params);
        CrewTestCamera=GetWorld()->SpawnActor<ACameraActor>();
        PC->SetViewTarget(CrewTestCamera);
        const USkeletalMesh* Asset=Cast<USkeletalMesh>(CrewTestSubject->CrewBody->GetSkinnedAsset());
        if (Asset)
        {
            const auto& Ref=Asset->GetRefSkeleton();
            for (int32 I=0;I<Ref.GetNum();++I)
                UE_LOG(LogTemp,Display,TEXT("RALLY_CREW_BONE %s parent=%d local=%s"),*Ref.GetBoneName(I).ToString(),Ref.GetParentIndex(I),*Ref.GetRefBonePose()[I].GetLocation().ToCompactString());
        }
        CrewTestStep=0;
        CrewTestTime=0;
        CrewTestCamera->SetActorLocation(CrewTestOrigin+FVector(150,-170,70));
        CrewTestCamera->SetActorRotation((CrewTestOrigin-CrewTestCamera->GetActorLocation()).Rotation());
    }
    if (CrewTestTime>2)
    {
        ++CrewTestStep;CrewTestTime=0;bCrewTestChecked=false;
        if (CrewTestStep>=UE_ARRAY_COUNT(Labels))
        {
            UE_LOG(LogTemp,Display,TEXT("RALLY_CREWTEST_DONE result=%s"),bCrewTestPassed?TEXT("pass"):TEXT("FAIL"));
            FGenericPlatformMisc::RequestExit(false,TEXT("RallyCrewTest"));return;
        }
        ARallyCharacter* C=CrewTestSubject;
        if (C->CarriedPart) C->CarriedPart->Drop();
        if (C->SeatedCar) C->ClearSeat(CrewTestOrigin,FRotator::ZeroRotator);
        auto* Move=CastChecked<URallyCharacterMovement>(C->GetCharacterMovement());
        Move->bWantsToProne=false;Move->bWantsBackPose=false;
        Move->SetProne(false,false,true);C->bOnBack=false;
        C->UnCrouch();Move->UnCrouch(false);
        Move->SetMovementMode(MOVE_Walking);
        C->SetActorLocation(CrewTestOrigin,false,nullptr,ETeleportType::TeleportPhysics);
        Move->Velocity=FVector::ZeroVector;
        C->HeldTool=0;C->ToolWorkUntil=-1;
        if (CrewTestStep==1 || CrewTestStep==2 || CrewTestStep==5)
            Move->Velocity=FVector(CrewTestStep==2?500:180,0,0);
        if (CrewTestStep==3) Move->SetMovementMode(MOVE_Falling);
        if (CrewTestStep==4 || CrewTestStep==5) { C->Crouch();Move->Crouch(false); }
        if (CrewTestStep>=6 && CrewTestStep<=9)
        {
            Move->bWantsToProne=true;Move->SetProne(true);
            Move->bWantsBackPose=CrewTestStep>=8;C->bOnBack=CrewTestStep>=8;
            if (CrewTestStep==7 || CrewTestStep==9) Move->Velocity=FVector(55,0,0);
        }
        if (CrewTestStep==10) { C->HeldTool=2;C->ToolWorkUntil=GetWorld()->GetTimeSeconds()+3; }
        if (CrewTestStep==11)
        {
            // An engine module: DebugSpawnPart only makes gear, modules and the small kit items.
            // Picked up once it has settled (below), as a player would.
            bCrewTestPassed &= Car->GetPanels()->DebugSpawnPart(URallyPanelComponent::FirstModule,1,C->GetActorLocation()+FVector(40,0,0))!=nullptr;
        }
        if (CrewTestStep==12 || CrewTestStep==13) C->SetSeat(Car,CrewTestStep-12);
        if (CrewTestStep==14)
        {
            HeldTool=2;ToolWorkUntil=GetWorld()->GetTimeSeconds()+3;
            PC->SetViewTarget(this);
        }
        if (CrewTestStep==15)
        {
            HeldTool=0;SetSeat(Car,0);
            PC->SetViewTarget(this);
            // The key turned, so the screenshot falls in the bulb check: every warning lamp's lit face shows.
            if (!Car->GetEngine()->IsIgnitionOn()) Car->GetEngine()->ToggleIgnition();
        }
        FVector Focus=C->GetActorLocation()+FVector(0,0,C->IsProne()?5:0);
        if (C->SeatedCar) Focus=Car->GetActorTransform().TransformPosition(Car->GetSeatEyeLocation(C->SeatIndex))-FVector(0,0,30);
        CrewTestCamera->SetActorLocation(Focus+FVector(150,-170,70));
        CrewTestCamera->SetActorRotation((Focus-CrewTestCamera->GetActorLocation()).Rotation());
    }
    if (CrewTestStep==11 && CrewTestTime>.6f && !CrewTestSubject->GetCarriedPart())
        for (TActorIterator<ARallyDebrisActor> It(GetWorld());It;++It)
            if (It->CanPickUp(CrewTestSubject)) { It->PickUp(CrewTestSubject);break; }
    if (!bCrewTestChecked && CrewTestTime>1.3f && CrewTestStep>=0 && CrewTestStep<UE_ARRAY_COUNT(Labels))
    {
        bCrewTestChecked=true;
        ARallyCharacter* C=CrewTestStep>=14?this:CrewTestSubject.Get();
        const FVector L=C->CrewBody->HandWorld(true),R=C->CrewBody->HandWorld(false);
        bool Pass=C->CrewBody->GetSkinnedAsset()!=nullptr && !L.ContainsNaN() && !R.ContainsNaN()
            && FVector::Distance(L,R)>5 && FVector::Distance(L,C->GetActorLocation())<220;
        if (CrewTestStep==11)
        {
            // Both hands on the carried part, not hanging at the sides.
            const ARallyDebrisActor* Part=C->GetCarriedPart();
            const FVector Anchor=C->GetCarryAnchorLocation();
            Pass &= Part && FVector::Distance(L,Anchor)<30 && FVector::Distance(R,Anchor)<30;
        }
        if (CrewTestStep<10 || CrewTestStep==12 || CrewTestStep==13)
            Pass &= C->CrewBody->GetPoseName()==FName(Labels[CrewTestStep]);
        if (CrewTestStep==12 || CrewTestStep==15)
        {
            const double Error=FMath::Max(FVector::Distance(L,Car->GetSteeringGripLocation(true)),FVector::Distance(R,Car->GetSteeringGripLocation(false)));
            UE_LOG(LogTemp,Display,TEXT("RALLY_CREWTEST wheel_error_cm=%.2f"),Error);
            Pass &= Error<5;
        }
        bCrewTestPassed &= Pass;
        UE_LOG(LogTemp,Display,TEXT("RALLY_CREWTEST %s=%s pose=%s left=%s right=%s"),Labels[CrewTestStep],Pass?TEXT("pass"):TEXT("FAIL"),
            *C->CrewBody->GetPoseName().ToString(),*L.ToCompactString(),*R.ToCompactString());
        FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Crew_%02d_%s.png"),CrewTestStep,Labels[CrewTestStep]),false,false);
    }
#endif
}
