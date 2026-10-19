#include "Vehicle/RallyCarPawn.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Damage/RallyBodyDeformComponent.h"
#include "Damage/PartConditionRules.h"
#include "Damage/RallyDamageComponent.h"
#include "Damage/RallyDebrisActor.h"
#include "Damage/RallyEngineComponent.h"
#include "Damage/RallyPanelComponent.h"
#include "Player/RallyCharacter.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"

void ARallyCarPawn::TickPanelTest(float Dt)
{
#if !UE_BUILD_SHIPPING
    // Server seeds failures; the owning on-foot pawn uses normal interaction RPCs.
    if (PanelTestPhase == 0 && (!Occupants.IsValidIndex(0) || !Occupants[0])) return;
    PanelTestTimer += Dt;
    auto Next = [&](uint8 Phase) {
        PanelTestPhase = Phase; PanelTestTimer = 0; bPanelTestPositioned = false; ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST phase=%d"), Phase);
    };
    auto Require = [&](bool Pass, const TCHAR* What) {
        if (!Pass) {
            UE_LOG(LogTemp, Error, TEXT("RALLY_PANELTEST_FAIL %s phase=%d"), What, PanelTestPhase);
            FGenericPlatformMisc::RequestExit(false, TEXT("RallyPanelTest"));
        }
        return Pass;
    };
    auto HitFixing = [&](const FVector& AssemblyPoint, float DeltaV) {
        const FVector P = AssemblyPoint + FVector(0,0,-80);
        Panels->ApplyImpact(FBox(P-FVector(3),P+FVector(3)), FVector(-1,0,0), DeltaV);
    };
    auto PositionForPickup = [&](int32 Panel) {
        ARallyDebrisActor* Part = Panels->GetDebris(Panel);
        if (!Part) return;
        const FVector At = Part->GetActorLocation() + FVector(0,-130,80);
        PanelTestDriver->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
        PanelTestDriver->ForceNetUpdate();
        bPanelTestPositioned = true;
    };
    const FVector AssemblyOffset(0, 0, -80);
    // Angle a panel has turned from its shut position (its rest rotation is identity).
    auto TurnedDeg = [&](int32 Panel) {
        return FMath::RadiansToDegrees(PanelMeshes[Panel]->GetRelativeRotation().Quaternion().AngularDistance(FQuat::Identity));
    };
    auto CornerZ = [&](int32 Wheel) {
        return GetActorTransform().TransformPosition(AssemblyOffset + BaseWheelSetups[Wheel].Pivot).Z;
    };
    // A 30 x 30 cm post, 1.5 m tall, at a point in the car's frame.
    auto SpawnPost = [&](const FVector& CarLocal) {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AStaticMeshActor* Post = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),
            GetActorTransform().TransformPosition(CarLocal), GetActorRotation(), Params);
        if (!Post) return false;
        Post->SetMobility(EComponentMobility::Movable);
        Post->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Post->SetActorScale3D(FVector(0.3, 0.3, 1.5));
        PanelTestPost = Post;
        return true;
    };
    auto Stand = [&](const FVector& World) {
        PanelTestDriver->SetActorLocation(World, false, nullptr, ETeleportType::TeleportPhysics);
        PanelTestDriver->ForceNetUpdate();
        bPanelTestPositioned = true;
    };
    // Drive the parked car along its own axis at a steady acceleration (cm/s^2) up to a speed (cm/s),
    // keeping whatever vertical motion it has: a real car's pace, not a jump.
    auto Roll = [&](float Accel, float TopSpeed, float Since) {
        const FVector Now = Body->GetPhysicsLinearVelocity();
        const float Speed = FMath::Sign(TopSpeed) * FMath::Min(FMath::Abs(Accel) * FMath::Max(PanelTestTimer - Since, 0.0f), FMath::Abs(TopSpeed));
        // Along the ground: the car's own forward dips with its nose and would push it into the road.
        Body->SetPhysicsLinearVelocity(GetActorForwardVector().GetSafeNormal2D() * Speed + FVector(0, 0, Now.Z));
    };
    auto Carrying = [&](int32 Panel) {
        return Panels->GetDebris(Panel) && Panels->GetDebris(Panel)->GetCarrier() == PanelTestDriver;
    };
    const int32 StrutSlot = URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Strut, 0);
    auto Health = [&](ERallyPart Part) { FRallyPartCondition C; Damage->GetPartCondition(Part, C); return C.Health; };
    auto CarriedSlot = [&]() { return PanelTestDriver->GetCarriedPart() ? PanelTestDriver->GetCarriedPart()->GetPanel() : INDEX_NONE; };
    if (PanelTestTimer > 30 && PanelTestPhase < 66)
    {
        Require(false, TEXT("phase timeout"));
        return;
    }
    switch(PanelTestPhase)
    {
    case 0:
        if (PanelTestTimer < 3) return;
        PanelTestDriver = Occupants[0];
        if (!Require(ServerExitSeat(PanelTestDriver, true), TEXT("exit"))) return;
        PanelTestDriver->SetActorLocation(GetActorTransform().TransformPosition(FVector(400,-300,10)),
            false, nullptr, ETeleportType::TeleportPhysics);
        HitFixing(FVector(197,-78,49),45);
        HitFixing(FVector(208,0,75),10);
        Require(Panels->GetStage(7)==Rally::Panel::EStage::Hanging, TEXT("one bumper clip hangs"));
        Require(Panels->GetStage(4)==Rally::Panel::EStage::Loose, TEXT("bonnet latch starts loose"));
        Next(1);
        break;
    case 1:
        if (PanelTestTimer < 2) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST bumper_droop_roll=%.1f"), PanelMeshes[7]->GetRelativeRotation().Roll);
        Require(FMath::Abs(PanelMeshes[7]->GetRelativeRotation().Roll) > 1.0, TEXT("bumper hangs from its last clip"));
        Require(FMath::Abs(PanelMeshes[7]->GetRelativeRotation().Roll) < 33.0, TEXT("ground holds the hanging bumper up"));
        {
            FRallyDent Dent; Dent.Point=FVector(215,-40,-32); Dent.Extent=FVector(10); Dent.Direction=FVector(-1,0,0);
            Dent.Depth=5; Dent.Radius=35; Dent.PartsMask=Panels->AttachedDentMask(); BodyDeform->AddDent(Dent);
            HitFixing(FVector(197,78,49),45);
            if (!Require(Panels->GetDebris(7)!=nullptr, TEXT("spawn debris"))) return;
            Require(BodyDeform->MaxDisplacement(PanelMeshes[7])>0, TEXT("bumper dent exists"));
            const float Before = BodyDeform->MaxDisplacement(PanelMeshes[7]);
            Require(FMath::IsNearlyEqual(Before, Panels->GetDebris(7)->GetMaxDentCm(), 0.01f), TEXT("independent debris dent matches car"));
            Dent.Depth=10; Dent.PartsMask=Panels->AttachedDentMask(); BodyDeform->AddDent(Dent);
            Require(FMath::IsNearlyEqual(Before,BodyDeform->MaxDisplacement(PanelMeshes[7])), TEXT("missing panel frozen"));
        }
        Next(2);
        break;
    case 2:
    case 5:
    case 8:
    case 15:
        if (PanelTestTimer > 2 && !bPanelTestPositioned)
            PositionForPickup(PanelTestPhase==2 ? 7 : PanelTestPhase==15 ? URallyPanelComponent::FirstWheel : 4);
        if (!PanelTestDriver->GetCarriedPart()) return;
        if (PanelTestPhase==8)
        {
            ServerDebugDamage_Implementation(true);
            Require(!PanelTestDriver->GetCarriedPart(), TEXT("repair releases carrier"));
            int32 Count=0;
            for(TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
                if(!It->IsActorBeingDestroyed() && It->GetMount() == ERallyPartMount::Loose) ++Count;
            Require(Count==0, TEXT("repair clears all debris"));
            Next(9);
        }
        else if (PanelTestPhase==15)
        {
            PanelTestDriver->SetActorLocation(Panels->GetRefitLocation(URallyPanelComponent::FirstWheel), false, nullptr, ETeleportType::TeleportPhysics);
            PanelTestDriver->ForceNetUpdate();
            Next(16);
        }
        else
        {
            const int32 Panel = PanelTestPhase==2 ? 7 : 4;
            PanelTestDriver->SetActorLocation(Panels->GetRefitLocation(Panel), false, nullptr, ETeleportType::TeleportPhysics);
            PanelTestDriver->ForceNetUpdate();
            Next(PanelTestPhase==2 ? 3 : 6);
        }
        break;
    case 3:
        // Offered up, then its two bolts and two screws done up.
        if (Panels->IsDetached(7) || !Panels->AllHome(7)) { PanelTestTimer = 0; return; }
        if (PanelTestTimer < 0.3f) return;
        Require(!PanelTestDriver->GetCarriedPart(), TEXT("proper refit releases carrier"));
        Require(Panels->GetStage(7)==Rally::Panel::EStage::Intact || Panels->GetStage(7)==Rally::Panel::EStage::Loose,
            TEXT("proper refit holds the bumper on (fully, or half caught when crooked)"));
        Require(TurnedDeg(7) > 0.2, TEXT("refitted bumper sits off square"));
        Require(BodyDeform->MaxDisplacement(PanelMeshes[7])>0, TEXT("proper refit retains dents"));
        Next(4);
        break;
    case 4:
        if (PanelTestTimer < 2) return;
        {
            FRallyDent Dent; Dent.Point=FVector(170,0,5); Dent.Extent=FVector(15); Dent.Direction=FVector(-1,0,0);
            Dent.Depth=28; Dent.Radius=70; Dent.PartsMask=Panels->AttachedDentMask(); BodyDeform->AddDent(Dent);
            HitFixing(FVector(101,-58,95),45); HitFixing(FVector(101,58,95),45); HitFixing(FVector(208,0,75),45);
            Require(Panels->IsDetached(4), TEXT("bonnet detached"));
            Require(BodyDeform->MaxDisplacement(PanelMeshes[4])>15, TEXT("bonnet too crushed"));
        }
        Next(5);
        break;
    case 6:
        if (PanelTestTimer < 3) return;
        Require(Panels->IsDetached(4) && PanelTestDriver->GetCarriedPart(), TEXT("crushed refit rejected"));
        Next(7);
        break;
    case 7:
        if (Panels->IsDetached(4)) return;
        // Give the strapped bonnet a moment to settle into its crooked pose.
        if (!bPanelTestPositioned) { bPanelTestPositioned = true; PanelTestTimer = 0; return; }
        if (PanelTestTimer < 0.5f) return;
        Require(Panels->GetStage(4)==Rally::Panel::EStage::Loose, TEXT("strap holds crushed bonnet"));
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST strapped_bonnet_skew=%.1f"), TurnedDeg(4));
        Require(TurnedDeg(4) > 1.5, TEXT("strapped bonnet hangs crooked"));
        Require(PanelTestDriver->GetStrapCount()==1, TEXT("bodge consumes exactly one strap"));
        Require(!Panels->StrapPanel(PanelTestDriver,4) && PanelTestDriver->GetStrapCount()==1, TEXT("duplicate bodge consumes nothing"));
        HitFixing(FVector(208,0,75),45);
        Require(Panels->IsDetached(4), TEXT("impact breaks strap"));
        Next(8);
        break;
    case 9:
        // The client opens the bonnet by hand (F) from the front of the car.
        if (!bPanelTestPositioned)
        {
            PanelTestDriver->SetActorLocation(Panels->GetRefitLocation(4), false, nullptr, ETeleportType::TeleportPhysics);
            PanelTestDriver->ForceNetUpdate();
            bPanelTestPositioned = true;
        }
        if (!Panels->IsOpen(4)) return;
        Next(10);
        break;
    case 10:
        if (PanelTestTimer < 1.5f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST bonnet_open_deg=%.1f"), TurnedDeg(4));
        Require(TurnedDeg(4) > 45.0, TEXT("bonnet held open by its prop"));
        Next(11);
        break;
    case 11:
        // The client shuts it again.
        if (Panels->IsOpen(4)) return;
        Next(12);
        break;
    case 12:
        if (PanelTestTimer < 1.5f) return;
        Require(TurnedDeg(4) < 3.0, TEXT("bonnet shut on its latch"));
        // Latch and one hinge gone: the bonnet hangs from the other hinge and cannot be latched.
        HitFixing(FVector(208,0,75),45);
        HitFixing(FVector(101,-58,95),45);
        Require(Panels->GetStage(4)==Rally::Panel::EStage::Hanging, TEXT("bonnet hangs from one hinge"));
        Require(!Panels->ToggleOpen(PanelTestDriver,4) && !Panels->IsOpen(4), TEXT("hanging bonnet cannot be opened by hand"));
        Next(13);
        break;
    case 13:
        if (PanelTestTimer < 2.5f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST bonnet_droop_roll=%.1f"), PanelMeshes[4]->GetRelativeRotation().Roll);
        Require(FMath::Abs(PanelMeshes[4]->GetRelativeRotation().Roll) > 1.0 && FMath::Abs(PanelMeshes[4]->GetRelativeRotation().Roll) < 2.0,
            TEXT("bonnet sags from its last hinge onto the body's edge"));
        {
            // A side hit breaks the front-left corner; a second, smaller one tears the hub.
            const FVector Wheel = AssemblyOffset + BaseWheelSetups[0].Pivot;
            ApplyImpact(Wheel - FVector(0,30,0), FVector(10), FVector(0,1,0), 14.0f);
            Require(!Panels->GetDebris(URallyPanelComponent::FirstWheel), TEXT("healthy corner keeps its wheel"));
            ApplyImpact(Wheel - FVector(0,30,0), FVector(10), FVector(0,1,0), 6.0f);
            FRallyPartCondition Hub;
            Damage->GetPartCondition(ERallyPart::HubFrontLeft, Hub);
            Require(Hub.Health <= 0.0f && Panels->GetDebris(URallyPanelComponent::FirstWheel), TEXT("broken corner loses its wheel"));
            Require(!WheelMeshes[0]->IsVisible(), TEXT("wheel gone from the car"));
        }
        Next(14);
        break;
    case 14:
        if (PanelTestTimer < 2.5f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST corner_drop=%.1f cm"), CornerZ(1) - CornerZ(0));
        Require(CornerZ(1) - CornerZ(0) > 5.0, TEXT("corner drops onto the hub"));
        Next(15);
        break;
    case 16:
        if (Panels->GetDebris(URallyPanelComponent::FirstWheel) || !Panels->AllHome(URallyPanelComponent::FirstWheel)) return;
        {
            FRallyPartCondition Hub;
            Damage->GetPartCondition(ERallyPart::HubFrontLeft, Hub);
            Require(Hub.Health >= 1.0f && WheelMeshes[0]->IsVisible() && !PanelTestDriver->GetCarriedPart(), TEXT("wheel bolted back on"));
        }
        Next(17);
        break;
    case 17:
        // Repaired first: the front-left corner broken above leaves the car leaning on it, and an open
        // door on a leaning car swings on by itself.
        if (!bPanelTestPositioned) { ServerDebugDamage_Implementation(true); bPanelTestPositioned = true; PanelTestTimer = 0; return; }
        if (PanelTestTimer < 2) return;
        // A post beside the driver's door; the driver's door is opened from the seat into it.
        if (!Require(SpawnPost(FVector(-5, -150, -30)), TEXT("spawn post"))) return;
        Require(Panels->ToggleOpenFromInside(0) && Panels->IsOpen(0), TEXT("door opens from the seat"));
        Next(18);
        break;
    case 18:
        // Past the 1.5 s the hand guides an opened door: let go against the post by then.
        if (PanelTestTimer < 2.0f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST door_against_post=%.1f"), TurnedDeg(0));
        Require(TurnedDeg(0) > 10.0 && TurnedDeg(0) < 55.0, TEXT("post stops the opening door"));
        if (PanelTestPost) PanelTestPost->Destroy();
        Next(19);
        break;
    case 19:
        if (PanelTestTimer < 1.5f) return;
        // Let go against the post, the door stays where it stopped; nothing pushes it on.
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST door_clear=%.1f"), TurnedDeg(0));
        Require(Panels->IsOpen(0) && TurnedDeg(0) < 55.0, TEXT("door let go against the post stays there"));
        Require(Panels->ToggleOpenFromInside(0) && !Panels->IsOpen(0), TEXT("door pulled shut from the seat"));
        Next(20);
        break;
    case 20:
        // The car, repaired so it runs straight, rolls forward with the driver's door open into a post:
        // the post slams it shut.
        if (PanelTestTimer < 1) return;
        if (!bPanelTestPositioned)
        {
            ServerDebugDamage_Implementation(true);
            Stand(GetActorTransform().TransformPosition(FVector(-50, -450, 10)));
            if (!Require(SpawnPost(FVector(600, -150, -30)), TEXT("spawn post"))) return;
            Require(Panels->ToggleOpenFromInside(0) && Panels->IsOpen(0), TEXT("driver's door open for the slam"));
            return;
        }
        if (PanelTestTimer < 3.5f) return;
        PanelTestDents = BodyDeform->GetDentCount();
        Next(21);
        break;
    case 21:
        // 0.15 g, which the door check holds, up to 16 km/h.
        if (PanelTestTimer < 3.4f) Roll(150.0f, 450.0f, 0.0f);
        if (PanelTestTimer < 4.5f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST slammed_door open=%d new_dents=%d stage=%d"), Panels->IsOpen(0) ? 1 : 0,
            BodyDeform->GetDentCount() - PanelTestDents, static_cast<int32>(Panels->GetStage(0)));
        Require(!Panels->IsOpen(0), TEXT("post slams the open door shut"));
        Require(BodyDeform->GetDentCount() > PanelTestDents, TEXT("door dented where it hit the post"));
        if (PanelTestPost) PanelTestPost->Destroy();
        Next(22);
        break;
    case 22:
        // Reversing with the rear door open into a post forces it past its stop and tears it off.
        if (PanelTestTimer < 1) return;
        if (!bPanelTestPositioned)
        {
            Stand(GetActorTransform().TransformPosition(FVector(-50, -450, 10)));
            if (!Require(SpawnPost(FVector(-1300, -150, -30)), TEXT("spawn post"))) return;
            Require(Panels->ToggleOpenFromInside(2) && Panels->IsOpen(2), TEXT("rear door open for the tear"));
            return;
        }
        if (PanelTestTimer < 3.5f) return;
        Next(23);
        break;
    case 23:
        // Reversing at 0.6 g up to 50 km/h: well past the 27 km/h that tears a door off head on.
        if (PanelTestTimer < 2.6f) Roll(600.0f, -1400.0f, 0.0f);
        else if (PanelTestTimer < 3.0f) Body->SetPhysicsLinearVelocity(FVector(0, 0, Body->GetPhysicsLinearVelocity().Z));
        if (PanelTestTimer < 3.5f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST torn_door detached=%d"), Panels->IsDetached(2) ? 1 : 0);
        Require(Panels->IsDetached(2), TEXT("reversing into a post tears the open rear door off"));
        if (PanelTestPost) PanelTestPost->Destroy();
        Next(24);
        break;
    case 24:
        // The client opens the boot lid, undoes its four hinge bolts (10 mm) and lifts it off (F, R on each, R).
        if (!bPanelTestPositioned) Stand(Panels->GetRefitLocation(5));
        if (!Carrying(5)) return;
        Require(Panels->IsDetached(5), TEXT("boot lid unbolted into the player's hands"));
        Next(25);
        break;
    case 25:
        // ...and offers it back up and does the bolts up again (E, F, E on each).
        if (Panels->IsDetached(5) || !Panels->AllHome(5)) return;
        Next(26);
        break;
    case 26:
        // The bonnet, left hanging from one hinge, is pulled off by hand (R twice, no tools).
        if (!bPanelTestPositioned)
        {
            HitFixing(FVector(208,0,75),45);
            HitFixing(FVector(101,-58,95),45);
            Require(Panels->GetStage(4)==Rally::Panel::EStage::Hanging, TEXT("bonnet hangs for pulling off"));
            Stand(Panels->GetRefitLocation(4));
        }
        if (!Carrying(4)) return;
        Next(27);
        break;
    case 27:
        if (PanelTestDriver->GetCarriedPart()) return;
        Next(28);
        break;
    case 28:
        // Everything repaired; the bonnet propped open by hand.
        if (!bPanelTestPositioned)
        {
            ServerDebugDamage_Implementation(true);
            Stand(Panels->GetRefitLocation(4));
            return;
        }
        if (PanelTestTimer < 1.5f) return;
        Require(Panels->ToggleOpen(PanelTestDriver, 4) && Panels->IsOpen(4), TEXT("bonnet propped open"));
        Next(29);
        break;
    case 29:
        // Pulling away gently (0.2 g) the prop holds; then a hard stop slams the bonnet down onto its latch.
        if (PanelTestTimer < 2.0f) return;
        if (!bPanelTestPositioned)
        {
            Require(TurnedDeg(4) > 45.0, TEXT("bonnet stands on its prop"));
            Stand(GetActorTransform().TransformPosition(FVector(-50, -450, 10)));
            return;
        }
        if (PanelTestTimer < 4.5f) { Roll(200.0f, 500.0f, 2.0f); return; }
        Require(Panels->IsOpen(4) && TurnedDeg(4) > 40.0, TEXT("prop holds the bonnet up while pulling away"));
        Body->SetPhysicsLinearVelocity(FVector(0, 0, Body->GetPhysicsLinearVelocity().Z));
        Next(30);
        break;
    case 30:
        if (PanelTestTimer < 2.0f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST hard_stop bonnet_open=%d angle=%.1f"), Panels->IsOpen(4) ? 1 : 0, TurnedDeg(4));
        Require(!Panels->IsOpen(4) && TurnedDeg(4) < 3.0, TEXT("hard stop slams the open bonnet shut and the latch catches"));
        Require(Panels->ToggleOpenFromInside(1) && Panels->IsOpen(1), TEXT("co-driver's door open for the drive"));
        Next(31);
        break;
    case 31:
        // Driving off gently (0.15 g) with the co-driver's door open: the wind swings it shut and it latches.
        if (PanelTestTimer < 1.5f) return;
        Roll(150.0f, 1400.0f, 1.5f);
        if (Panels->IsOpen(1) && PanelTestTimer < 12.0f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST wind_shut door_open=%d after=%.1fs kmh=%.0f"), Panels->IsOpen(1) ? 1 : 0,
            PanelTestTimer - 1.5f, GetForwardSpeedKmh());
        Require(!Panels->IsOpen(1), TEXT("wind swings the open door shut and the latch catches"));
        Body->SetPhysicsLinearVelocity(FVector(0, 0, Body->GetPhysicsLinearVelocity().Z));
        Next(32);
        break;
    case 32:
        // Running gear. Everything repaired and the car parked; the boot kit restocked.
        if (PanelTestTimer < 2) return;
        ServerDebugDamage_Implementation(true);
        Stand(Panels->GetRefitLocation(5));
        Next(33);
        break;
    case 33:
        // The client opens the boot.
        if (!Panels->IsOpen(5)) return;
        Next(34);
        break;
    case 34:
        // ...takes the jack out and carries it to the front-left wheel.
        if (CarriedSlot() != URallyPanelComponent::JackSlot) return;
        PanelTestHeight = CornerZ(0);
        Stand(Panels->GetRefitLocation(URallyPanelComponent::FirstWheel));
        Next(35);
        break;
    case 35:
        // ...puts it under the sill: the corner winds up.
        if (Panels->GetJackCorner() != 0 || PanelTestTimer < 4.0f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST jack_lift=%.1f cm"), CornerZ(0) - PanelTestHeight);
        Require(CornerZ(0) - PanelTestHeight > 8.0, TEXT("jack lifts the corner"));
        Next(36);
        break;
    case 36:
        // ...takes the wheel off and sets it down: the corner stays up on the jack.
        if (!Panels->GetDebris(URallyPanelComponent::FirstWheel) || PanelTestDriver->GetCarriedPart()) return;
        Require(CornerZ(0) - PanelTestHeight > 8.0, TEXT("jacked corner stays up without its wheel"));
        // A strut broken and the arm bent in some earlier crash.
        Damage->ApplyPartDamage(ERallyPart::StrutFrontLeft, 1.0f);
        Damage->ApplyPartDamage(ERallyPart::SuspensionFrontLeft, 0.5f);
        Next(37);
        break;
    case 37:
        // ...undoes the broken strut's three bolts (19, 19, 13 mm), lifts it out and sets it down.
        if (!Panels->IsRemoved(StrutSlot) || PanelTestDriver->GetCarriedPart()) return;
        Require(Health(ERallyPart::StrutFrontLeft) <= 0.0f, TEXT("a missing strut counts as broken"));
        Stand(Panels->GetRefitLocation(5));
        Next(38);
        break;
    case 38:
        // ...fetches the spare front strut from the boot: a rally one, adjustable.
        if (!bPanelTestPositioned)
        {
            for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
                if (It->GetSourceCar() == this && It->GetPanel() == StrutSlot && It->GetMount() == ERallyPartMount::Stowed)
                    It->SetGrade(static_cast<uint8>(Rally::Grade::EGrade::Rally));
            bPanelTestPositioned = true;
        }
        if (CarriedSlot() != StrutSlot || !PanelTestDriver->GetCarriedPart()->IsSpare()) return;
        Stand(Panels->GetRefitLocation(URallyPanelComponent::FirstWheel));
        Next(39);
        break;
    case 39:
        // ...offers it up, does its bolts up, sets the damper three clicks harder (screwdriver) and winds the
        // collar a centimetre down (19 mm).
        if (Panels->IsRemoved(StrutSlot) || !Panels->AllHome(StrutSlot) || Panels->GetClicks(StrutSlot) != 8
            || Panels->GetCollarCm(StrutSlot) != -1) return;
        Require(Health(ERallyPart::StrutFrontLeft) >= 1.0f, TEXT("spare strut fitted as new"));
        Require(Panels->GetGrade(StrutSlot) == Rally::Grade::EGrade::Rally, TEXT("rally strut keeps its grade on the car"));
        {
            const double Spring = WheelSetups[0].SpringRate / BaseWheelSetups[0].SpringRate;
            const double Damping = WheelSetups[0].BumpDamping / BaseWheelSetups[0].BumpDamping;
            const double Lower = WheelSetups[0].Pivot.Z - BaseWheelSetups[0].Pivot.Z;
            UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST rally_strut spring=%.2f damping=%.3f lower=%.1f cm"), Spring, Damping, Lower);
            Require(FMath::IsNearlyEqual(Spring, 1.5, 0.01) && FMath::IsNearlyEqual(Damping, 1.35 * Rally::Grade::ClickDamping(8), 0.01)
                && FMath::IsNearlyEqual(Lower, 2.0, 0.01), TEXT("rally strut stiffer, damped harder and lower"));
        }
        Next(40);
        break;
    case 40:
        // ...hammers the bent arm roughly straight.
        if (Health(ERallyPart::SuspensionFrontLeft) < Rally::Damage::BodgeHealth) return;
        Next(41);
        break;
    case 41:
        // ...picks the wheel up again...
        if (!bPanelTestPositioned)
        {
            if (ARallyDebrisActor* Wheel = Panels->GetDebris(URallyPanelComponent::FirstWheel))
                Stand(Wheel->GetActorLocation() + FVector(0, -130, 80));
        }
        if (CarriedSlot() != URallyPanelComponent::FirstWheel) return;
        Stand(Panels->GetRefitLocation(URallyPanelComponent::FirstWheel));
        Next(42);
        break;
    case 42:
        // ...offers it up and does the four wheel bolts up...
        if (Health(ERallyPart::HubFrontLeft) <= 0.0f || !Panels->AllHome(URallyPanelComponent::FirstWheel)) return;
        Next(43);
        break;
    case 43:
        // ...and lowers the jack: the corner comes back down onto its wheel.
        if (Panels->GetJackCorner() >= 0) return;
        Next(44);
        break;
    case 44:
        if (PanelTestTimer < 3) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST lowered=%.1f cm"), CornerZ(0) - PanelTestHeight);
        Require(FMath::Abs(CornerZ(0) - PanelTestHeight) < 3.0, TEXT("lowered corner back at ride height"));
        Next(45);
        break;
    case 45:
        // Engine bay. A radiator and battery hit in some earlier crash, the coolant mostly gone; the client
        // opens the bonnet.
        if (!bPanelTestPositioned)
        {
            Damage->ApplyPartDamage(ERallyPart::Radiator, 0.7f);
            Damage->ApplyPartDamage(ERallyPart::Battery, 0.8f);
            Engine->DebugSetCoolant(1.0);
            Stand(Panels->GetRefitLocation(URallyPanelComponent::RadiatorSlot));
        }
        if (!Panels->IsOpen(4)) return;
        Next(46);
        break;
    case 46:
        // ...seals the radiator's leak from the tin (B)...
        if (Health(ERallyPart::Radiator) < Rally::Damage::BodgeHealth) return;
        Require(PanelTestDriver->GetSealant() == 0, TEXT("sealing used the tin"));
        Next(47);
        break;
    case 47:
        // ...cleans and tightens the battery terminals (B)...
        if (Health(ERallyPart::Battery) < Rally::Damage::BodgeHealth) return;
        Stand(Panels->GetRefitLocation(5));
        Next(48);
        break;
    case 48:
        // ...fetches the water can from the boot...
        if (CarriedSlot() != URallyPanelComponent::WaterSlot) return;
        PanelTestHeight = Engine->GetCoolantL();
        Stand(Panels->GetRefitLocation(URallyPanelComponent::RadiatorSlot));
        Next(49);
        break;
    case 49:
        // ...and pours it in at the radiator (E): the five litres fill the system.
        if (Engine->GetCoolantL() < PanelTestHeight + 1.0) return;
        if (PanelTestTimer < 1.0f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST coolant %.1f -> %.1f L, can %.1f L left"), PanelTestHeight, Engine->GetCoolantL(),
            PanelTestDriver->GetCarriedPart() ? PanelTestDriver->GetCarriedPart()->GetHealth() * URallyPanelComponent::WaterCanLitres : -1.0);
        Require(Engine->GetCoolantL() > Rally::Engine::CoolantCapacityL - 0.2, TEXT("water can tops the coolant up"));
        Require(PanelTestDriver->GetCarriedPart() && PanelTestDriver->GetCarriedPart()->GetHealth() < 0.5f, TEXT("can poured out"));
        Next(50);
        break;
    case 50:
        // ...puts the can down and shuts the bonnet.
        if (PanelTestDriver->GetCarriedPart() || Panels->IsOpen(4)) return;
        Next(51);
        break;
    case 51:
        // Under the car. A rock has cracked the sump, holed the tank, damaged the gearbox and torn the exhaust
        // off its rear hangers, and most of the oil has gone. With the front on the jack, the client patches the
        // sump from in front (B, putty)...
        if (!bPanelTestPositioned)
        {
            Damage->ApplyPartDamage(ERallyPart::OilSump, 0.7f);
            Damage->ApplyPartDamage(ERallyPart::FuelTank, 0.7f);
            Damage->ApplyPartDamage(ERallyPart::Exhaust, 0.85f);
            Damage->ApplyPartDamage(ERallyPart::Gearbox, 0.6f);
            Engine->DebugSetOil(1.0);
            Panels->DebugPlaceJack(-1);
            Stand(Panels->GetRefitLocation(URallyPanelComponent::SumpSlot));
        }
        if (PanelTestTimer < 3.0f) return;
        if (Panels->GetJackCorner() < 0)
        {
            // Three seconds of trying: nothing under the car can be reached without the jack. Then the jack.
            Require(Health(ERallyPart::OilSump) < Rally::Damage::BodgeHealth, TEXT("no patching the sump without the jack"));
            Panels->DebugPlaceJack(0);
            return;
        }
        if (Health(ERallyPart::OilSump) < Rally::Damage::BodgeHealth) return;
        Next(52);
        break;
    case 52:
        // ...frees the gear linkage and tops up the gearbox (B): all gears back...
        if (!bPanelTestPositioned) Stand(Panels->GetRefitLocation(URallyPanelComponent::GearboxSlot));
        if (Health(ERallyPart::Gearbox) < Rally::Damage::BodgeHealth) return;
        Next(53);
        break;
    case 53:
        // ...moves the jack to the rear and patches the tank from the left...
        if (!bPanelTestPositioned)
        {
            Panels->DebugPlaceJack(2);
            Stand(Panels->GetRefitLocation(URallyPanelComponent::TankSlot));
        }
        if (Health(ERallyPart::FuelTank) < Rally::Damage::BodgeHealth) return;
        Require(PanelTestDriver->GetPutty() == 0, TEXT("two patches used the putty"));
        Next(54);
        break;
    case 54:
        // ...pulls the hanging exhaust off from behind (R, two tugs) and leaves it on the ground; the jack
        // comes down...
        if (!bPanelTestPositioned) Stand(Panels->GetRefitLocation(URallyPanelComponent::ExhaustSlot));
        if (!Panels->IsRemoved(URallyPanelComponent::ExhaustSlot)) return;
        Require(!ExhaustMesh->IsVisible() && !PanelTestDriver->GetCarriedPart(), TEXT("exhaust off the car and on the ground"));
        Panels->DebugPlaceJack(-1);
        Next(55);
        break;
    case 55:
        // ...opens the bonnet...
        if (!bPanelTestPositioned) Stand(Panels->GetRefitLocation(URallyPanelComponent::OilFillerSlot));
        if (!Panels->IsOpen(4)) return;
        Stand(Panels->GetRefitLocation(5));
        Next(56);
        break;
    case 56:
        // ...takes the oil bottle from the boot...
        if (CarriedSlot() != URallyPanelComponent::OilBottleSlot) return;
        Stand(Panels->GetRefitLocation(URallyPanelComponent::OilFillerSlot));
        Next(57);
        break;
    case 57:
        // ...and pours it in at the engine's filler: 1.0 L plus the 4 L bottle, less what fits.
        if (Engine->GetOilL() < Rally::Underside::OilCapacityL - 0.1 || PanelTestTimer < 1.0f) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST oil %.1f L, bottle %.1f L left"), Engine->GetOilL(),
            PanelTestDriver->GetCarriedPart() ? PanelTestDriver->GetCarriedPart()->GetHealth() * URallyPanelComponent::OilBottleLitres : -1.0);
        Require(PanelTestDriver->GetCarriedPart() && FMath::IsNearlyEqual(PanelTestDriver->GetCarriedPart()->GetHealth()
            * URallyPanelComponent::OilBottleLitres, 0.5f, 0.15f), TEXT("half a litre left in the bottle"));
        Next(58);
        break;
    case 58:
        // ...puts the bottle down and shuts the bonnet.
        if (PanelTestDriver->GetCarriedPart() || Panels->IsOpen(4)) return;
        Next(59);
        break;
    case 59:
        // Engine modules, from in front with the bonnet open (F): the turbo's nuts cannot be got at under the
        // engine cover and the air filter box.
        if (!bPanelTestPositioned)
        {
            Stand(Panels->GetRefitLocation(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Cover)));
            PanelTestPower = Engine->GetPowerScale();
        }
        if (!Panels->IsOpen(4)) return;
        Require(!Panels->TurnFixing(PanelTestDriver, URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Turbo), 0, false),
            TEXT("turbo nuts out of reach under the cover and air box"));
        Next(60);
        break;
    case 60:
        // ...undoes the engine cover's two screws and sets it down...
        if (!Panels->IsRemoved(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Cover)) || PanelTestDriver->GetCarriedPart()) return;
        Next(61);
        break;
    case 61:
        // ...unclips the air filter box by hand and sets it down...
        if (!Panels->IsRemoved(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::AirFilter)) || PanelTestDriver->GetCarriedPart()) return;
        Stand(Panels->GetRefitLocation(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Turbo)));
        Next(62);
        break;
    case 62:
        // ...and takes the turbo off (three 13 mm nuts): the engine is left with about two thirds of its power.
        if (!Panels->IsRemoved(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Turbo)) || PanelTestDriver->GetCarriedPart()) return;
        {
            const double Ratio = Engine->GetPowerScale() / FMath::Max(PanelTestPower, 0.01);
            UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST no_turbo_power=%.2f"), Ratio);
            Require(Ratio > 0.6 && Ratio < 0.7, TEXT("without the turbo about two thirds of the power"));
        }
        Next(63);
        break;
    case 63:
        // ...puts the turbo back, does its nuts up and winds the wastegate up to 1.0 bar (10 mm)...
        if (Panels->IsRemoved(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Turbo))
            || !Panels->AllHome(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Turbo))
            || Panels->GetBoostTenths(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Turbo)) != 10) return;
        Next(64);
        break;
    case 64:
        // ...then the air filter box...
        if (Panels->IsRemoved(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::AirFilter))
            || !Panels->AllHome(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::AirFilter))) return;
        Next(65);
        break;
    case 65:
        // ...and the cover, and shuts the bonnet: full power again, a tenth more for the boost.
        if (Panels->IsRemoved(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Cover))
            || !Panels->AllHome(URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Cover)) || Panels->IsOpen(4)) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST boosted_power=%.3f"), Engine->GetPowerScale() / FMath::Max(PanelTestPower, 0.01));
        Require(FMath::IsNearlyEqual(Engine->GetPowerScale(), PanelTestPower * Rally::Grade::BoostPower(10), 0.01),
            TEXT("full power with the turbo back, 10% more at 1.0 bar"));
        Next(66);
        break;
    case 66:
        if (PanelTestTimer < 3) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANELTEST_SERVER_PASS"));
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyPanelTest"));
        break;
    }
#endif
}
