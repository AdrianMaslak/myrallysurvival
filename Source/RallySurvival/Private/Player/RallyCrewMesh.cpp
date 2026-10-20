#include "Player/RallyCrewMesh.h"
#include "Player/RallyCharacter.h"
#include "Vehicle/RallyCarPawn.h"
#include "Damage/RallyDebrisActor.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

URallyCrewMesh::URallyCrewMesh()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    bCastDynamicShadow = true;
    BoundsScale = 3;
}

void URallyCrewMesh::BuildReference()
{
    const USkeletalMesh* Asset=Cast<USkeletalMesh>(GetSkinnedAsset());
    if (!Asset) return;
    const FReferenceSkeleton& Rig = Asset->GetRefSkeleton();
    Reference = Rig.GetRefBonePose();
    for (int32 I=0; I<Reference.Num(); ++I)
    {
        Indices.Add(Rig.GetBoneName(I), I);
        const int32 Parent = Rig.GetParentIndex(I);
        if (Parent >= 0) Reference[I] *= Reference[Parent];
    }
    Pose = Reference;
    Smoothed = Reference;
}

FVector URallyCrewMesh::HandWorld(bool bLeft) const
{
    const int32* I = Indices.Find(bLeft ? FName("hand_l") : FName("hand_r"));
    return I && Smoothed.IsValidIndex(*I) ? GetComponentTransform().TransformPosition(Smoothed[*I].GetLocation()) : GetComponentLocation();
}

void URallyCrewMesh::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt, TickType, TickFunction);
    ARallyCharacter* Crew = Cast<ARallyCharacter>(GetOwner());
    const USkeletalMesh* Asset=Cast<USkeletalMesh>(GetSkinnedAsset());
    if (!Crew || !Asset) return;
    if (Reference.IsEmpty()) BuildReference();
    const FName Required[]={"pelvis","spine_01","spine_02","chest","neck","head","shoulder_l","shoulder_r","upper_arm_l","upper_arm_r","forearm_l","forearm_r","hand_l","hand_r","thigh_l","thigh_r","shin_l","shin_r","foot_l","foot_r"};
    for (FName Bone:Required) if (!Indices.Contains(Bone)) return;
    const FReferenceSkeleton& Rig = Asset->GetRefSkeleton();
    const bool bSeated = Crew->GetSeatedCar() != nullptr;
    const bool bLocalView = Crew->IsLocallyControlled() || (bSeated && Crew->GetSeatIndex()==0 && Crew->GetSeatedCar()->IsLocallyControlled());
    SetVisibility(bFirstPerson ? bLocalView : !bLocalView);
    const bool bProne = Crew->IsProne();
    const bool bBack = bProne && Crew->IsOnBack();
    const bool bCrouch = Crew->bIsCrouched && !bProne;
    const bool bJump = Crew->GetCharacterMovement()->IsFalling() && !bSeated;
    const bool bCarry = Crew->GetCarriedPart() != nullptr;
    const bool bTool = Crew->GetHeldTool() != Rally::Fixing::ETool::Hands && !bCarry;
    const bool bWorking = Crew->IsWorkingWithTool();
    const float Speed = bSeated ? 0 : Crew->GetVelocity().Size2D();
    const float Moving = FMath::Clamp(Speed / (bProne ? 50.f : 160.f),0.f,1.f);
    Phase += Dt * (bProne ? 4.5f : bCrouch ? 6.f : Speed > 400 ? 12.f : 8.f) * Moving;
    const float T = DebugPhase >= 0 ? DebugPhase : Phase;
    const float Wave = FMath::Sin(T), Other = -Wave;
    PoseName = bSeated ? (Crew->GetSeatIndex()==0 ? "Drive" : "Passenger")
        : bProne ? (bBack ? (Moving>.1f ? "CrawlBack" : "LieBack") : (Moving>.1f ? "CrawlBelly" : "LieBelly"))
        : bJump ? "Jump" : bCrouch ? (Moving>.1f ? "CrouchWalk" : "Crouch")
        : Moving>.1f ? (Speed>400 ? "Run" : "Walk") : "Idle";
    auto Ref = [&](FName Name) { return Reference[Indices.FindChecked(Name)].GetLocation(); };
    const FVector RestPelvis = Ref("pelvis");
    FVector Pelvis = RestPelvis;
    FQuat BodyRotation = FQuat::Identity;
    if (bSeated) Pelvis = FVector(8,0,58);
    else if (bProne)
    {
        Pelvis = FVector(-60,0,13);
        BodyRotation = FRotationMatrix::MakeFromXZ(bBack ? FVector::UpVector : -FVector::UpVector,FVector::ForwardVector).ToQuat();
    }
    else if (bCrouch) Pelvis += FVector(-12,0,-48);
    else if (bJump) Pelvis += FVector(0,0,-7);
    else Pelvis.Z += .65f*FMath::Sin(GetWorld()->GetTimeSeconds()*2.2f) + FMath::Abs(Wave)*2*Moving;
    // The mesh is placed on the ground; seated eyes stay at the camera's established anchor.
    FVector MeshOffset(0,0,-Crew->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
    if (bSeated) MeshOffset.Z = 64.f - (Ref("head").Z - RestPelvis.Z + Pelvis.Z + 12.f);
    SetRelativeLocation(MeshOffset);
    SetRelativeRotation(FRotator::ZeroRotator);
    auto BodyPoint = [&](FVector P) { return Pelvis + BodyRotation.RotateVector(P-RestPelvis); };
    TMap<int32,FTransform> Overrides;
    // Twist turns the bone about its own length (degrees), e.g. a palm from facing down to facing in.
    auto SetJoint = [&](FName Name,FVector Head,FVector Tail,FVector RefTail,float Twist=0)
    {
        const int32 I=Indices.FindChecked(Name);
        const FVector Dir=(Tail-Head).GetSafeNormal();
        const FQuat Delta = FQuat(Dir,FMath::DegreesToRadians(Twist))*FQuat::FindBetweenNormals((RefTail-Ref(Name)).GetSafeNormal(),Dir);
        Overrides.Add(I,FTransform(Delta*Reference[I].GetRotation(),Head,Reference[I].GetScale3D()));
    };
    // Torso: reference lengths maintained; squat leans forward slightly.
    const FQuat LeanRotation = bCrouch ? FQuat(FVector::RightVector,FMath::DegreesToRadians(18.f)) : FQuat::Identity;
    auto TorsoPoint = [&](FName Name) { return Pelvis + BodyRotation.RotateVector(LeanRotation.RotateVector(Ref(Name)-RestPelvis)); };
    const FName Spine[] = {"pelvis","spine_01","spine_02","chest","neck","head"};
    for (int I=0;I<5;++I) SetJoint(Spine[I],TorsoPoint(Spine[I]),TorsoPoint(Spine[I+1]),Ref(Spine[I+1]));
    FVector Head = TorsoPoint("head");
    FVector HeadUp = BodyRotation.RotateVector(FVector::UpVector);
    // Keep the face up off the road while on the belly.
    if (bProne && !bBack) HeadUp = FVector(.8,0,.6);
    SetJoint("head",Head,Head+HeadUp*10,Ref("head")+FVector(0,0,10));
    auto Limb = [&](FName Upper,FName Lower,FName End,FVector Start,FVector Target,FVector Pole,FVector EndDirection,float EndTwist=0)
    {
        const float A=FVector::Distance(Ref(Upper),Ref(Lower)), B=FVector::Distance(Ref(Lower),Ref(End));
        FVector Axis=(Target-Start).GetSafeNormal();
        float D=FMath::Clamp(static_cast<float>(FVector::Distance(Start,Target)),FMath::Abs(A-B)+.1f,A+B-.1f);
        Target=Start+Axis*D;
        FVector Bend=(Pole-Axis*FVector::DotProduct(Pole,Axis)).GetSafeNormal();
        if (Bend.IsNearlyZero()) Bend=FVector::RightVector;
        float Along=(A*A-B*B+D*D)/(2*D);
        FVector Joint=Start+Axis*Along+Bend*FMath::Sqrt(FMath::Max(0.f,A*A-Along*Along));
        SetJoint(Upper,Start,Joint,Ref(Lower));
        SetJoint(Lower,Joint,Target,Ref(End));
        FVector DefaultEnd = End.ToString().StartsWith("foot") ? Ref(End)+FVector(12,0,0) : Ref(End)+FVector(0,End==FName("hand_l")?-10:10,0);
        SetJoint(End,Target,Target+EndDirection,DefaultEnd,EndTwist);
    };
    const FVector LocalVelocity = Crew->GetActorTransform().InverseTransformVectorNoScale(Crew->GetVelocity());
    FVector Travel = LocalVelocity.GetSafeNormal2D();
    if (Travel.IsNearlyZero()) Travel=FVector::ForwardVector;
    for (int SideIndex=0;SideIndex<2;++SideIndex)
    {
        const bool Left=SideIndex==0;
        const float Side=Left?-1.f:1.f;
        const float Cycle=Left?Wave:Other;
        const FName Thigh=Left?"thigh_l":"thigh_r", Shin=Left?"shin_l":"shin_r", Foot=Left?"foot_l":"foot_r";
        FVector Hip=BodyPoint(Ref(Thigh));
        FVector Ankle=Ref(Foot);
        FVector KneePole=FVector::ForwardVector;
        FVector FootDirection=FVector::ForwardVector;
        if (bSeated) { Hip=BodyPoint(Ref(Thigh)); Ankle=FVector(48,Side*13,8); KneePole=FVector(1,0,.2); }
        else if (bProne)
        {
            Ankle=FVector(-135+Cycle*10*Moving,Side*(15+5*Moving),7+FMath::Max(0.f,Cycle)*5*Moving);
            KneePole=bBack?FVector(0,Side*.35,1):FVector(0,Side, .1);
            FootDirection=FVector(-1,0,0);
        }
        else
        {
            Ankle += Travel*(Cycle*(bCrouch?12:Speed>400?34:24)*Moving);
            Ankle.Z += FMath::Max(0.f,Cycle)*(bCrouch?5:9)*Moving;
            if (bJump) Ankle += FVector(-12,0,18);
        }
        Limb(Thigh,Shin,Foot,Hip,Ankle,KneePole,FootDirection*12);
        const FName Shoulder=Left?"shoulder_l":"shoulder_r", Upper=Left?"upper_arm_l":"upper_arm_r";
        const FName Forearm=Left?"forearm_l":"forearm_r", Hand=Left?"hand_l":"hand_r";
        FVector ShoulderStart=TorsoPoint(Shoulder), ArmStart=TorsoPoint(Upper);
        SetJoint(Shoulder,ShoulderStart,ArmStart,Ref(Upper));
        FVector Wrist=ArmStart+FVector(-Cycle*18*Moving,Side*5,-48);
        FVector ElbowPole(0,Side,-.5), HandDir(0,0,-10);
        float HandTwist=0;
        if (bSeated)
        {
            if (Crew->GetSeatIndex()==0)
            {
                Wrist=GetComponentTransform().InverseTransformPosition(Crew->GetSeatedCar()->GetSteeringGripLocation(Left));
                HandDir=GetComponentTransform().InverseTransformVectorNoScale(Crew->GetSeatedCar()->GetActorUpVector())*10;
            }
            else { Wrist=FVector(23,Side*15,56); HandDir=FVector(12,0,0); }
        }
        else if (bProne)
        {
            Wrist=FVector((bBack?-43:0)+Cycle*10*Moving,Side*(bBack?31:29),5);
            ElbowPole=FVector(-.4,Side,.1); HandDir=FVector(10,0,0);
        }
        else if (bJump) { Wrist=ArmStart+FVector(20,Side*14,-22); HandDir=FVector(10,0,0); }
        // A held tool is in the right hand; the left only joins in to work or carry.
        if (!bSeated && (bCarry || bWorking || (bTool && !Left)))
        {
            // Aim-driven hand targets shared by first and third person, so a partner sees the repair.
            const FRotator Look=Crew->GetBaseAimRotation();
            const FVector WorldEye=Crew->GetPawnViewLocation();
            const FVector Forward=Look.Vector(), Right=FRotationMatrix(Look).GetUnitAxis(EAxis::Y);
            // Low in the first-person view and off to the sides, the way the camera sees its own hands.
            // Higher and closer together, the hands (19 cm, mitten-like) filled the middle of the screen.
            FVector WorldTarget=bWorking ? WorldEye+Forward*(Left?40:44)+Right*(Side*(Left?14:17))-FVector(0,0,Left?32:28)
                : WorldEye+Forward*40+Right*20-FVector(0,0,36);
            if (bCarry) WorldTarget=Crew->GetCarryAnchorLocation()+Crew->GetActorRightVector()*Side*18;
            if (bWorking && !Left) WorldTarget += Right*(3*FMath::Sin(GetWorld()->GetTimeSeconds()*15));
            Wrist=GetComponentTransform().InverseTransformPosition(WorldTarget);
            HandDir=GetComponentTransform().InverseTransformVectorNoScale(Forward)*10;
            // Palms in, thumbs up, as round a handle or the sides of a part; flat palms read as paddles.
            HandTwist=-90*Side;
        }
        Limb(Upper,Forearm,Hand,ArmStart,Wrist,ElbowPole,HandDir,HandTwist);
    }
    // Compose every unmodified finger/toe from its parent. Grip closes the available finger bones.
    const bool Grip=bSeated || bCarry || bTool || bWorking;
    const auto& Local=Rig.GetRefBonePose();
    for (int32 I=0;I<Reference.Num();++I)
    {
        const int Parent=Rig.GetParentIndex(I);
        FTransform L=Local[I];
        const FString Name=Rig.GetBoneName(I).ToString();
        if (Grip && (Name.StartsWith("f_") || Name.StartsWith("thumb_")))
            L.ConcatenateRotation(FQuat(FVector::ForwardVector,FMath::DegreesToRadians(Name.StartsWith("thumb")?15:40)));
        Pose[I]=Overrides.Contains(I)?Overrides[I]:Parent>=0?L*Pose[Parent]:L;
        // Blend local transforms so all child bones retain their segment lengths during transitions.
        FTransform TargetLocal=Parent>=0?Pose[I].GetRelativeTransform(Pose[Parent]):Pose[I];
        if (BoneSpaceTransforms.IsValidIndex(I))
        {
            FTransform Blended;
            Blended.Blend(BoneSpaceTransforms[I],TargetLocal,FMath::Clamp(Dt*14.f,0.f,1.f));
            BoneSpaceTransforms[I]=Blended;
        }
        Smoothed[I]=Parent>=0?BoneSpaceTransforms[I]*Smoothed[Parent]:BoneSpaceTransforms[I];
    }
    MarkRefreshTransformDirty();
    RefreshBoneTransforms();
    if (!bFirstPerson) Crew->UpdateCrewProps(HandWorld(false));
}
