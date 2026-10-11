#include "Damage/RallyDebrisActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Damage/RallyBodyDeformComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "Vehicle/RallyCarPawn.h"

ARallyDebrisActor::ARallyDebrisActor()
{
    bReplicates = true;
    SetReplicatingMovement(true);
    Shape = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shape"));
    Shape->SetCollisionProfileName(TEXT("PhysicsActor"));
    // Spawned where the part sat, inside the car's body box: colliding with cars would explode it out.
    Shape->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
    Shape->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    Shape->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    Shape->SetSimulatePhysics(true);
    SetRootComponent(Shape);
}

void ARallyDebrisActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyDebrisActor, SourceCar);
    DOREPLIFETIME(ARallyDebrisActor, PartMesh);
    DOREPLIFETIME(ARallyDebrisActor, Panel);
}

void ARallyDebrisActor::Launch(ARallyCarPawn* Car, UStaticMesh* Mesh, int32 InPanel, float MassKg, float AirDamping,
    const FVector& Velocity, const FVector& AngularVelocityDeg)
{
    SourceCar = Car;
    PartMesh = Mesh;
    Panel = static_cast<int8>(InPanel);
    BuildVisual();
    Shape->SetMassOverrideInKg(NAME_None, MassKg, true);
    // Stands in for air resistance, so a bonnet torn off at speed falls behind the car.
    Shape->SetLinearDamping(AirDamping);
    Shape->SetAngularDamping(0.3f);
    Shape->SetPhysicsLinearVelocity(Velocity);
    Shape->SetPhysicsAngularVelocityInDegrees(AngularVelocityDeg);
}

void ARallyDebrisActor::BeginPlay()
{
    Super::BeginPlay();
    BuildVisual();
}

void ARallyDebrisActor::OnRep_Source()
{
    BuildVisual();
}

void ARallyDebrisActor::BuildVisual()
{
    if (!PartMesh)
    {
        return;
    }
    if (Shape->GetStaticMesh() != PartMesh)
    {
        Shape->SetStaticMesh(PartMesh);
    }
    // Panels: draw the car's dented copy, which shares the part mesh's origin.
    if (Dented || Panel == INDEX_NONE || !SourceCar)
    {
        return;
    }
    UE::Geometry::FDynamicMesh3 Copy;
    TArray<UMaterialInterface*> Materials;
    if (!SourceCar->CopyPanelMesh(Panel, Copy, Materials))
    {
        return;
    }
    Dented = NewObject<UDynamicMeshComponent>(this, TEXT("Dented"));
    Dented->SetupAttachment(Shape);
    Dented->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Dented->SetVertexColorSpaceTransformMode(EDynamicMeshVertexColorTransformMode::NoTransform);
    Dented->SetMesh(MoveTemp(Copy));
    Dented->ConfigureMaterialSet(Materials);
    Dented->RegisterComponent();
    Shape->SetVisibility(false);
}
