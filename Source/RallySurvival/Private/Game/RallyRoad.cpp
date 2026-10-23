#include "Game/RallyRoad.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "UDynamicMesh.h"
#include "Vehicle/RallyCarPawn.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include "Game/RallyStageDirector.h"
#include "Game/RallyTeamState.h"
#if WITH_EDITOR
#include "Landscape.h"
#include "Misc/FileHelper.h"
#endif

using namespace UE::Geometry;

ARallyRoad::ARallyRoad()
{
    Road = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Road"));
    SetRootComponent(Road);
    Road->SetCollisionProfileName(TEXT("BlockAll"));
    Road->SetComplexAsSimpleCollisionEnabled(true, false);
    Road->SetDeferredCollisionUpdatesEnabled(true, false);
    Road->bUseAsyncCooking = true;
    bReplicates = true;
    SetNetCullDistanceSquared(FMath::Square(25000.0f));
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.1f;
}

void ARallyRoad::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Rebuild();
}

void ARallyRoad::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    if (!IsTemplate()) Rebuild();
}

void ARallyRoad::Rebuild()
{
    if (Points.Num() < 2 || Rights.Num() != Points.Num()) return;
    Road->bUseAsyncCooking = GetWorld() && GetWorld()->IsGameWorld();
    const float TotalWidth = WidthCm + 300.0f;
    Columns = bMud ? FMath::CeilToInt(TotalWidth / 25.0f) + 1 : 7;
    Base.Reset();
    FDynamicMesh3 Mesh;
    for (int32 I = 0; I < Points.Num(); ++I)
        for (int32 J = 0; J < Columns; ++J)
        {
            const double Across = static_cast<double>(J) / (Columns - 1);
            const double Lateral = (Across - 0.5) * TotalWidth;
            FVector P = Points[I] + Rights[I] * Lateral;
            // Overlap end caps by 1 cm to cover float rounding in separate Chaos meshes.
            if (I == 0) P -= (Points[1]-Points[0]).GetSafeNormal();
            if (I == Points.Num()-1) P += (Points.Last()-Points[Points.Num()-2]).GetSafeNormal();
            // A 1.5 m shoulder joins the road surface to the carved Landscape.
            P.Z -= 70.0 * FMath::Clamp((FMath::Abs(Lateral)-WidthCm*0.5)/150.0, 0.0, 1.0);
            Base.Add(P);
            const int32 V = Mesh.AppendVertex(FVector3d(P));
            if (Depths.IsValidIndex(V)) Mesh.SetVertex(V, FVector3d(P - FVector(0,0,Depths[V]*0.1)));
        }
    for (int32 I = 0; I + 1 < Points.Num(); ++I)
        for (int32 J = 0; J + 1 < Columns; ++J)
        {
            const int32 A = I * Columns + J, B = A + Columns;
            Mesh.AppendTriangle(A, B + 1, B);
            Mesh.AppendTriangle(A, A + 1, B + 1);
        }
    Mesh.EnableAttributes();
    FMeshNormals::QuickComputeVertexNormals(Mesh, false);
    FMeshNormals::InitializeOverlayToPerVertexNormals(Mesh.Attributes()->PrimaryNormals(), true);
    Road->SetMaterial(0, Material);
    Road->SetPhysMaterialOverride(Surface);
    Road->GetDynamicMesh()->SetMesh(MoveTemp(Mesh));
    Road->UpdateCollision(false);
}

void ARallyRoad::BeginPlay()
{
    Super::BeginPlay();
    if (Base.IsEmpty()) Rebuild();
    SetActorTickEnabled(bMud && HasAuthority());
    if (HasAuthority() && ActorHasTag(TEXT("MudTest")) && FParse::Param(FCommandLine::Get(), TEXT("RallyRoadTest")))
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [this]
        {
            const FVector P = GetActorTransform().TransformPosition(Base[(Points.Num()/2)*Columns+Columns/2]);
            FHitResult Before;
            FCollisionQueryParams Query;
            Query.bReturnPhysicalMaterial = true;
            const bool bBefore = Road->LineTraceComponent(Before, P+FVector(0,0,100), P-FVector(0,0,100), Query);
            UE_LOG(LogTemp, Display, TEXT("HIGHLANDS_RUNTIME_SURFACE %s"), *GetNameSafe(Before.PhysMaterial.Get()));
            for (int32 I=0; I<30; ++I) ApplyTrack(P-FVector(80,0,0), P+FVector(80,0,0), 1.0f);
            OnRep_Depths();
            FTimerHandle Check;
            GetWorldTimerManager().SetTimer(Check, [this,P,bBefore,Before]
            {
                FHitResult After;
                const bool bAfter = Road->LineTraceComponent(After, P+FVector(0,0,100), P-FVector(0,0,100), FCollisionQueryParams());
                const double Depth = Before.ImpactPoint.Z-After.ImpactPoint.Z;
                ARallyStageDirector* Stages = ARallyStageDirector::Get(GetWorld());
                const bool bStages = Stages && FMath::Abs(Stages->GetStage(0).LengthM+Stages->GetStage(1).LengthM+Stages->GetStage(2).LengthM-20000.0)<0.1;
                ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
                int32 Services = 0;
                for (TActorIterator<AActor> It(GetWorld()); It; ++It)
                    if (It->ActorHasTag(TEXT("RallyService")) && Team && Team->InServiceYard(*It)) ++Services;
                UE_LOG(LogTemp, Display, TEXT("HIGHLANDS_RUNTIME_MUD_%s depth_cm=%.2f stages=%d services=%d"),
                    bBefore && bAfter && Depth>5 && Depth<=12.1 && bStages && Services==2 ? TEXT("PASS") : TEXT("FAIL"), Depth, bStages, Services);
            }, 2.0f, false);
        }, 3.0f, false);
    }
}

void ARallyRoad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyRoad, Depths);
}

bool ARallyRoad::ApplyTrack(FVector From, FVector To, float DepthCm)
{
    if (!bMud || !HasAuthority() || Base.IsEmpty() || DepthCm <= 0) return false;
    From = GetActorTransform().InverseTransformPosition(From);
    To = GetActorTransform().InverseTransformPosition(To);
    if (Depths.Num() != Base.Num()) Depths.Init(0, Base.Num());
    const FVector2D A(From), B(To), AB = B - A;
    const double LengthSq = AB.SizeSquared();
    bool Changed = false;
    for (int32 V = 0; V < Base.Num(); ++V)
    {
        // Shared end rows and shoulders stay fixed, so adjacent chunks cannot crack.
        if (V < Columns || V >= Base.Num()-Columns || V%Columns == 0 || V%Columns == Columns-1) continue;
        const double T = LengthSq > 0.01 ? FMath::Clamp(FVector2D::DotProduct(FVector2D(Base[V])-A, AB)/LengthSq, 0.0, 1.0) : 0;
        const double D = FVector2D::Distance(FVector2D(Base[V]), A+AB*T);
        if (D >= 35.0 || FMath::Abs(Base[V].Z-FMath::Lerp(From.Z, To.Z, T)) > 45.0) continue;
        const int32 Next = FMath::Min(120, Depths[V] + FMath::RoundToInt(DepthCm*10.0*(1.0-D/35.0)));
        if (Next != Depths[V]) { Depths[V] = static_cast<uint8>(Next); Changed = true; }
    }
    return Changed;
}

void ARallyRoad::OnRep_Depths()
{
    if (Base.Num() != Depths.Num()) return;
    Road->GetDynamicMesh()->EditMesh([this](FDynamicMesh3& Mesh)
    {
        for (int32 V = 0; V < Base.Num(); ++V) Mesh.SetVertex(V, FVector3d(Base[V]-FVector(0,0,Depths[V]*0.1)));
        FMeshNormals::QuickComputeVertexNormals(Mesh, false);
        FMeshNormals::InitializeOverlayToPerVertexNormals(Mesh.Attributes()->PrimaryNormals(), true);
    });
    Road->UpdateCollision(false);
}

void ARallyRoad::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    bool Changed = false;
    for (TActorIterator<ARallyCarPawn> It(GetWorld()); It; ++It)
    {
        ARallyCarPawn* Car = *It;
        if (!Road->Bounds.GetBox().ExpandBy(500.0).IsInside(Car->GetActorLocation()))
        { Previous.Remove(Car); continue; }
        TArray<FVector> Contact = Car->GetRoadContacts();
        TArray<FVector>& Last = Previous.FindOrAdd(Car);
        for (int32 I = 0; I < Contact.Num(); ++I)
        {
            const FVector From = Last.IsValidIndex(I) && FVector::DistSquared(Last[I],Contact[I]) < FMath::Square(1500.0)
                ? Last[I] : Contact[I];
            if (Car->GetVelocity().SizeSquared() > 100.0)
                Changed |= ApplyTrack(From, Contact[I], FMath::Min(DeltaSeconds, 0.2f)*5.0f);
        }
        Last = MoveTemp(Contact);
    }
    if (Changed)
    {
        OnRep_Depths(); ForceNetUpdate();
        if (FParse::Param(FCommandLine::Get(), TEXT("RallyRoadTest")))
            UE_LOG(LogTemp, Display, TEXT("HIGHLANDS_TYRE_RUT %s"), *GetName());
    }
}

ARallyRoadDebris::ARallyRoadDebris()
{
    Shape = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shape"));
    SetRootComponent(Shape);
    Shape->SetMobility(EComponentMobility::Movable);
    Shape->SetCollisionProfileName(TEXT("PhysicsActor"));
    Shape->SetNotifyRigidBodyCollision(true);
    Shape->SetLinearDamping(0.4f);
    Shape->SetAngularDamping(0.8f);
    bReplicates = true;
    SetReplicateMovement(true);
}

void ARallyRoadDebris::BeginPlay()
{
    Super::BeginPlay();
    Shape->SetMassOverrideInKg(NAME_None, MassKg);
    Shape->SetSimulatePhysics(HasAuthority());
    Shape->SetUseCCD(true);
    if (HasAuthority() && ActorHasTag(TEXT("DebrisTest")) && FParse::Param(FCommandLine::Get(), TEXT("RallyRoadTest")))
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [this]
        {
            const FVector Start = GetActorLocation();
            Shape->AddImpulse(FVector(20000,0,15000));
            FTimerHandle Check;
            GetWorldTimerManager().SetTimer(Check, [this,Start]
            {
                const double Distance = FVector::Dist(Start,GetActorLocation());
                UE_LOG(LogTemp, Display, TEXT("HIGHLANDS_RUNTIME_DEBRIS_%s moved_cm=%.2f"),
                    Shape->IsSimulatingPhysics() && Distance>25 ? TEXT("PASS") : TEXT("FAIL"), Distance);
            }, 2.0f, false);
        }, 3.0f, false);
    }
}

AActor* URallyLandscapeTools::ImportTerrain(UObject* Context, const FString& File, int32 Size,
                                          FVector Origin, FVector Scale, UMaterialInterface* Material)
{
#if WITH_EDITOR
    TArray<uint8> Bytes;
    if (!Context || !Context->GetWorld() || Size < 64 || (Size-1)%63 != 0 ||
        !FFileHelper::LoadFileToArray(Bytes, *File) || Bytes.Num() != Size*Size*2) return nullptr;
    TArray<uint16> Heights;
    Heights.SetNumUninitialized(Size*Size);
    FMemory::Memcpy(Heights.GetData(), Bytes.GetData(), Bytes.Num());
    ALandscape* Land = Context->GetWorld()->SpawnActor<ALandscape>(Origin, FRotator::ZeroRotator);
    Land->SetActorScale3D(Scale);
    Land->LandscapeMaterial = Material;
    TMap<FGuid,TArray<uint16>> HeightData;
    HeightData.Add(FGuid(), MoveTemp(Heights));
    TMap<FGuid,TArray<FLandscapeImportLayerInfo>> Layers;
    Layers.Add(FGuid(), {});
    Land->Import(FGuid::NewGuid(), 0, 0, Size-1, Size-1, 1, 63, HeightData, *File, Layers,
                 ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
    Land->PostEditChange();
    return Land;
#else
    return nullptr;
#endif
}
