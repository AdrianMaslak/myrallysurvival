#include "Damage/RallyDebrisActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Damage/PartGradeRules.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Damage/RallyBodyDeformComponent.h"
#include "Damage/RallyPanelComponent.h"
#include "Player/RallyCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Vehicle/RallyCarPawn.h"

ARallyDebrisActor::ARallyDebrisActor()
{
    bReplicates = true;
    PrimaryActorTick.bCanEverTick = true;
    SetReplicatingMovement(true);
    Shape = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shape"));
    Shape->SetCollisionProfileName(TEXT("PhysicsActor"));
    Shape->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
    Shape->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    Shape->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    // Wheel raycasts also ignore debris while it overlaps the car at detachment.
    Shape->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    Shape->SetLinearDamping(0.2f);
    Shape->SetAngularDamping(0.3f);
    // Stowed or carried it rides attached to a car or a person. Unreal welds auto-weld children into a
    // body that starts simulating (a client taking over as driver), which would hand the car this part's
    // mass in place of its own.
    Shape->BodyInstance.bAutoWeld = false;
    SetRootComponent(Shape);
    Deform = CreateDefaultSubobject<URallyBodyDeformComponent>(TEXT("Deform"));
}

void ARallyDebrisActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyDebrisActor, Source);
    DOREPLIFETIME(ARallyDebrisActor, Carrier);
    DOREPLIFETIME(ARallyDebrisActor, MountKind);
    DOREPLIFETIME(ARallyDebrisActor, MountLocation);
    DOREPLIFETIME(ARallyDebrisActor, MountRotation);
}

void ARallyDebrisActor::Launch(const FRallyDebrisSource& InSource, const TArray<FRallyDent>& Dents,
    const FVector& Velocity, const FVector& AngularVelocityDeg)
{
    if (!HasAuthority()) return;
    Source = InSource;
    // The snapshot can arrive before or after Source on a client; Initialize handles both.
    Deform->SetSnapshot(Dents);
    OnRep_Source();
    Shape->SetPhysicsLinearVelocity(Velocity);
    Shape->SetPhysicsAngularVelocityInDegrees(AngularVelocityDeg);
    ForceNetUpdate();
}

void ARallyDebrisActor::OnRep_Source()
{
    if (!Source.Mesh || bBuilt) return;
    Shape->SetStaticMesh(Source.Mesh);
    Shape->SetMassOverrideInKg(NAME_None, Source.MassKg, true);
    if (Source.Panel == URallyPanelComponent::PropshaftSlot)
    {
        const TCHAR* Names[] = { TEXT("Propshaft_Front"), TEXT("PropshaftBearing") };
        const FVector Offsets[] = { FVector(125.5, -0.2, 3.3), FVector::ZeroVector };
        for (int32 I = 0; I < 2; ++I)
        {
            UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
            const FString Path = FString::Printf(TEXT("/Game/Vehicles/Octavia/Drivetrain/SM_Drive_%s.SM_Drive_%s"), Names[I], Names[I]);
            Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *Path));
            Part->SetupAttachment(Shape);
            Part->SetRelativeLocation(Offsets[I]);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->RegisterComponent();
        }
    }
    // Only body panels dent; wheels, suspension parts and the jack are drawn as they are.
    if (Source.Panel >= 0 && Source.Panel < URallyPanelComponent::NumPanels)
    {
        // Root world transform must not enter deformation math: meshes use the original car frame.
        const FTransform World = Shape->GetComponentTransform();
        Shape->SetWorldTransform(FTransform::Identity, false, nullptr, ETeleportType::TeleportPhysics);
        Deform->Initialize({Shape}, Source.MeshToCar, {Rally::Crush::KindOfPanel(Source.Panel)});
        Shape->SetWorldTransform(World, false, nullptr, ETeleportType::TeleportPhysics);
    }
    bBuilt = true;
    ApplyCarryState();
}

bool ARallyDebrisActor::CanPickUp(const ARallyCharacter* Character) const
{
    if (!Character || Carrier || Character->GetSeatedCar() || Character->GetCarriedPart()
        || !Source.Mesh || GetVelocity().Size() > 200.0f) return false;
    // A jack holding a corner up is lowered first; a spare stays in the boot until the lid is open.
    if (MountKind == ERallyPartMount::UnderCar) return false;
    if (MountKind == ERallyPartMount::Stowed && (!Source.Car || !(Source.Car->GetPanels()->IsOpen(5) || Source.Car->GetPanels()->IsDetached(5))))
        return false;
    // Measure against the part, not its centre (bonnet and bumpers can be wide).
    FVector Closest;
    const float Distance = Shape->GetClosestPointOnCollision(Character->GetActorLocation(), Closest);
    if (Distance < 0.0f || Distance > 180.0f) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyPickUp), false, Character);
    Params.AddIgnoredActor(this);
    // The car is one broad box, including empty space under the bumpers. It must not prevent
    // reaching a loose part that landed there. Other world obstacles still block the reach.
    if (Source.Car) Params.AddIgnoredActor(Source.Car);
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit, Character->GetActorLocation(), Closest, ECC_Visibility, Params);
}

bool ARallyDebrisActor::PickUp(ARallyCharacter* Character)
{
    if (!HasAuthority() || !CanPickUp(Character)) return false;
    MountKind = ERallyPartMount::Loose;
    Carrier = Character;
    Character->SetCarriedPart(this);
    ApplyCarryState();
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL pickup panel=%d"), Source.Panel);
    return true;
}

void ARallyDebrisActor::Drop()
{
    if (!HasAuthority()) return;
    ARallyCharacter* Previous = Carrier;
    Carrier = nullptr;
    if (IsValid(Previous)) Previous->SetCarriedPart(nullptr);
    ApplyCarryState();
    Shape->SetPhysicsLinearVelocity(IsValid(Previous) ? Previous->GetVelocity() : FVector::ZeroVector);
    ForceNetUpdate();
}

void ARallyDebrisActor::OnRep_Carrier() { ApplyCarryState(); }

FRallySavedPart ARallyDebrisActor::SaveState() const
{
    FRallySavedPart Saved;
    Saved.Slot = Source.Panel;
    Saved.Grade = Source.Grade;
    Saved.Health = Source.Health;
    Saved.MassKg = Source.MassKg;
    Saved.bSpare = Source.bSpare;
    Saved.MeshToCar = Source.MeshToCar;
    // A part in someone's hands is saved lying at their feet.
    Saved.Mount = Carrier ? static_cast<uint8>(ERallyPartMount::Loose) : static_cast<uint8>(MountKind);
    Saved.MountLocation = MountLocation;
    Saved.MountRotation = MountRotation;
    Saved.Transform = Carrier ? FTransform(GetActorRotation(), Carrier->GetActorLocation() + FVector(0, 0, -40)) : GetActorTransform();
    Saved.Dents = Deform->GetDents();
    return Saved;
}

void ARallyDebrisActor::SetGrade(uint8 Grade)
{
    if (!HasAuthority()) return;
    Source.Grade = FMath::Min<uint8>(Grade, static_cast<uint8>(Rally::Grade::EGrade::Rally));
    ForceNetUpdate();
}

void ARallyDebrisActor::SetHealth(float Health)
{
    if (!HasAuthority() || !FMath::IsFinite(Health)) return;
    Source.Health = FMath::Clamp(Health, 0.0f, 1.0f);
    ForceNetUpdate();
}

void ARallyDebrisActor::Mount(ERallyPartMount How, const FVector& Location, const FRotator& Rotation)
{
    if (!HasAuthority()) return;
    MountKind = How;
    MountLocation = Location;
    MountRotation = Rotation;
    ApplyCarryState();
    ForceNetUpdate();
}

void ARallyDebrisActor::ApplyCarryState()
{
    if (!bBuilt) return;
    if (Carrier)
    {
        bIgnoringCars = true;
        Shape->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
        Shape->SetSimulatePhysics(false);
        Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        AttachToComponent(Carrier->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        SetActorRelativeLocation(FVector(115.0, 0.0, 25.0));
        SetActorRelativeRotation(FRotator(0.0, 90.0, 0.0));
    }
    else if (MountKind != ERallyPartMount::Loose && Source.Car)
    {
        // Stowed in the boot or standing under the car: it rides with the car and can still be found
        // by a reaching hand.
        Shape->SetSimulatePhysics(false);
        Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        AttachToComponent(Source.Car->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        SetActorRelativeLocation(MountLocation);
        SetActorRelativeRotation(MountRotation);
    }
    else
    {
        DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        // Server is the sole authority; client physics participates in built-in movement correction.
        Shape->SetSimulatePhysics(true);
    }
    if (HasAuthority()) SetReplicatingMovement(Carrier == nullptr && MountKind == ERallyPartMount::Loose);
    UpdateStraps();
}

void ARallyDebrisActor::UpdateStraps()
{
    const bool bStrapped = !Carrier && MountKind == ERallyPartMount::Stowed && Source.Mesh && Source.Car
        && !URallyPanelComponent::InBootWell(MountLocation) && GetNetMode() != NM_DedicatedServer;
    if (!bStrapped)
    {
        for (UStaticMeshComponent* Strap : Straps) if (Strap) Strap->SetVisibility(false);
        return;
    }
    if (Straps.IsEmpty())
    {
        UStaticMesh* Band = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        UMaterialInterface* Webbing = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/MI_Part_Strap.MI_Part_Strap"));
        for (int32 Piece = 0; Piece < 3; ++Piece)
        {
            UStaticMeshComponent* Strap = NewObject<UStaticMeshComponent>(this);
            Strap->SetStaticMesh(Band);
            if (Webbing) Strap->SetMaterial(0, Webbing);
            Strap->SetupAttachment(Shape);
            Strap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Strap->SetCastShadow(false);
            Strap->RegisterComponent();
            Straps.Add(Strap);
        }
    }
    // In the car's frame: over the top along the car, then down its front and back ends to what it lies on,
    // 2.5 cm wide and a few millimetres proud of the part.
    const FTransform Riding(MountRotation, MountLocation);
    const FBox Box = Source.Mesh->GetBoundingBox().TransformBy(Riding);
    const FVector C = Box.GetCenter(), E = Box.GetExtent();
    const FVector Pieces[3][2] = {
        { FVector(C.X, C.Y, Box.Max.Z + 0.25), FVector(E.X + 0.5, 1.25, 0.25) },
        { FVector(Box.Max.X + 0.25, C.Y, C.Z), FVector(0.25, 1.25, E.Z + 0.25) },
        { FVector(Box.Min.X - 0.25, C.Y, C.Z), FVector(0.25, 1.25, E.Z + 0.25) },
    };
    for (int32 Piece = 0; Piece < 3; ++Piece)
    {
        // The cube is 100 cm along each side, centred.
        const FTransform InCar(FQuat::Identity, Pieces[Piece][0], Pieces[Piece][1] / 50.0);
        Straps[Piece]->SetRelativeTransform(InCar.GetRelativeTransform(Riding));
        Straps[Piece]->SetVisibility(true);
    }
}

void ARallyDebrisActor::Tick(float Dt)
{
    Super::Tick(Dt);
    // Disconnects and actor destruction must not leave an uncollectable suspended part.
    if (HasAuthority() && Carrier && (!IsValid(Carrier) || !Carrier->GetController())) Drop();
    // Once clear of the box of the car it came off (or was dropped beside), cars hit it and it hits them.
    const UBoxComponent* CarBox = Source.Car ? Cast<UBoxComponent>(Source.Car->GetRootComponent()) : nullptr;
    if (bIgnoringCars && !Carrier && MountKind == ERallyPartMount::Loose && bBuilt && CarBox)
    {
        const FVector Local = CarBox->GetComponentTransform().InverseTransformPositionNoScale(Shape->Bounds.Origin);
        const FVector Reach = CarBox->GetUnscaledBoxExtent() + FVector(Shape->Bounds.SphereRadius);
        if (FMath::Abs(Local.X) > Reach.X || FMath::Abs(Local.Y) > Reach.Y || FMath::Abs(Local.Z) > Reach.Z)
        {
            bIgnoringCars = false;
            Shape->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
        }
    }
}

void ARallyDebrisActor::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority() && IsValid(Carrier)) Carrier->SetCarriedPart(nullptr);
    Super::EndPlay(Reason);
}

float ARallyDebrisActor::GetMaxDentCm() const { return Deform->MaxDisplacement(Shape); }
