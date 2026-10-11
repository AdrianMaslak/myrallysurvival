#include "Damage/RallyBodyDeformComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "StaticMeshLODResourcesToDynamicMesh.h"
#include "StaticMeshResources.h"
#include "UDynamicMesh.h"

DEFINE_LOG_CATEGORY_STATIC(LogRallyDeform, Log, All);

using namespace UE::Geometry;

namespace
{
    // Element of a per-vertex overlay used by each vertex (the copies have exactly one per vertex).
    template <typename OverlayType>
    TArray<int32> ElementPerVertex(const FDynamicMesh3& Mesh, const OverlayType* Overlay)
    {
        TArray<int32> Elements;
        Elements.Init(INDEX_NONE, Mesh.MaxVertexID());
        if (!Overlay)
        {
            return Elements;
        }
        for (const int32 Triangle : Mesh.TriangleIndicesItr())
        {
            const FIndex3i Tri = Mesh.GetTriangle(Triangle);
            const FIndex3i Element = Overlay->GetTriangle(Triangle);
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                Elements[Tri[Corner]] = Element[Corner];
            }
        }
        return Elements;
    }

    // Area-weighted face normal.
    FVector3d FaceNormal(const FDynamicMesh3& Mesh, int32 Triangle)
    {
        FVector3d A, B, C;
        Mesh.GetTriVertices(Triangle, A, B, C);
        return (B - A).Cross(C - A);
    }

    // Seeded smooth noise in about -1..1.
    double Noise(const FVector3d& Position, double Scale, double Seed)
    {
        return FMath::PerlinNoise3D(FVector(Position / Scale + FVector3d(Seed, Seed * 1.7, Seed * 2.3)));
    }
}

URallyBodyDeformComponent::URallyBodyDeformComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void URallyBodyDeformComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(URallyBodyDeformComponent, Dents);
}

void URallyBodyDeformComponent::Initialize(const TArray<UStaticMeshComponent*>& SourceParts)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Parts.IsEmpty())
    {
        return;
    }
    int32 Vertices = 0;
    for (UStaticMeshComponent* Source : SourceParts)
    {
        if (!Source || !Source->GetStaticMesh())
        {
            continue;
        }
        FPart Part;
        Part.Source = Source;
        Part.BaseRelative = Source->GetRelativeTransform();
        Part.LocalBounds = Source->GetStaticMesh()->GetBoundingBox();
        Part.CarBounds = Part.LocalBounds.TransformBy(Part.BaseRelative);
        UDynamicMeshComponent* Copy = NewObject<UDynamicMeshComponent>(Owner, *(Source->GetName() + TEXT("_Deform")));
        Part.Mesh = Copy;
        if (!RebuildPart(Part))
        {
            Copy->MarkAsGarbage();
            continue;
        }
        Copy->SetupAttachment(Source->GetAttachParent());
        Copy->SetRelativeTransform(Source->GetRelativeTransform());
        Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Copy->SetGenerateOverlapEvents(false);
        Copy->SetVertexColorSpaceTransformMode(EDynamicMeshVertexColorTransformMode::NoTransform);
        TArray<UMaterialInterface*> Materials;
        for (int32 Index = 0; Index < Source->GetNumMaterials(); ++Index)
        {
            Materials.Add(Source->GetMaterial(Index));
        }
        Copy->ConfigureMaterialSet(Materials);
        Copy->RegisterComponent();
        Source->SetVisibility(false);
        Vertices += Part.RestNormals.Num();
        Parts.Add(MoveTemp(Part));
    }
    UE_LOG(LogRallyDeform, Display, TEXT("RALLY_DAMAGE deformable parts=%d of %d vertices=%d"), Parts.Num(), SourceParts.Num(), Vertices);
    // Dents that replicated before the copies existed.
    OnRep_Dents();
}

bool URallyBodyDeformComponent::RebuildPart(FPart& Part)
{
    const UStaticMesh* StaticMesh = Part.Source ? Part.Source->GetStaticMesh() : nullptr;
    const FStaticMeshRenderData* RenderData = StaticMesh ? StaticMesh->GetRenderData() : nullptr;
    if (!RenderData || RenderData->LODResources.IsEmpty() || !Part.Mesh)
    {
        return false;
    }
    // Reading render vertices needs "Allow CPU Access" on the asset (scripts/setup_damage_assets.py).
    const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
    if (!LOD.VertexBuffers.StaticMeshVertexBuffer.GetAllowCPUAccess())
    {
        UE_LOG(LogRallyDeform, Warning, TEXT("%s has no CPU access; it will not dent."), *StaticMesh->GetName());
        return false;
    }
    FDynamicMesh3 Mesh;
    FStaticMeshLODResourcesToDynamicMesh::ConversionOptions Options;
    Options.bWantVertexColors = false;
    if (!FStaticMeshLODResourcesToDynamicMesh::Convert(&LOD, Options, Mesh))
    {
        return false;
    }
    // One white colour element per vertex: R is the paint condition the materials blend on.
    Mesh.EnableAttributes();
    Mesh.Attributes()->EnablePrimaryColors();
    FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();
    TArray<int32> NewElements;
    NewElements.Init(INDEX_NONE, Mesh.MaxVertexID());
    for (const int32 Vertex : Mesh.VertexIndicesItr())
    {
        NewElements[Vertex] = Colors->AppendElement(FVector4f(1.0f, 1.0f, 1.0f, 1.0f));
    }
    for (const int32 Triangle : Mesh.TriangleIndicesItr())
    {
        const FIndex3i Tri = Mesh.GetTriangle(Triangle);
        Colors->SetTriangle(Triangle, FIndex3i(NewElements[Tri.A], NewElements[Tri.B], NewElements[Tri.C]));
    }
    Part.ColorElements = MoveTemp(NewElements);
    Part.NormalElements = ElementPerVertex(Mesh, Mesh.Attributes()->PrimaryNormals());

    // Undamaged shading, and which render vertices are one point of the surface (split at UV or
    // normal seams), so the bent shading can be worked out across seams without tearing.
    const FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
    Part.RestNormals.Init(FVector3f::ZeroVector, Mesh.MaxVertexID());
    Part.RestFaceNormals.Init(FVector3d::ZeroVector, Mesh.MaxTriangleID());
    Part.WeldGroup.Init(INDEX_NONE, Mesh.MaxVertexID());
    Part.WeldStart.Reset();
    Part.WeldVertices.Reset();
    TMap<FIntVector, int32> GroupOfPoint;
    TArray<TArray<int32, TInlineAllocator<4>>> Groups;
    for (const int32 Vertex : Mesh.VertexIndicesItr())
    {
        if (Normals && Part.NormalElements[Vertex] != INDEX_NONE)
        {
            Part.RestNormals[Vertex] = Normals->GetElement(Part.NormalElements[Vertex]);
        }
        const FVector3d P = Mesh.GetVertex(Vertex);
        const FIntVector Key(FMath::RoundToInt(P.X * 100.0), FMath::RoundToInt(P.Y * 100.0), FMath::RoundToInt(P.Z * 100.0));
        int32& Group = GroupOfPoint.FindOrAdd(Key, Groups.Num());
        if (Group == Groups.Num())
        {
            Groups.AddDefaulted();
        }
        Groups[Group].Add(Vertex);
        Part.WeldGroup[Vertex] = Group;
    }
    for (const TArray<int32, TInlineAllocator<4>>& Group : Groups)
    {
        Part.WeldStart.Add(Part.WeldVertices.Num());
        Part.WeldVertices.Append(Group);
    }
    Part.WeldStart.Add(Part.WeldVertices.Num());
    for (const int32 Triangle : Mesh.TriangleIndicesItr())
    {
        Part.RestFaceNormals[Triangle] = FaceNormal(Mesh, Triangle);
    }
    Part.Mesh->SetMesh(MoveTemp(Mesh));
    return true;
}

bool URallyBodyDeformComponent::CopyPartMesh(UStaticMeshComponent* Source, FDynamicMesh3& OutMesh, TArray<UMaterialInterface*>& OutMaterials) const
{
    for (const FPart& Part : Parts)
    {
        if (Part.Source != Source || !Part.Mesh || !Part.Mesh->GetMesh())
        {
            continue;
        }
        OutMesh = *Part.Mesh->GetMesh();
        OutMaterials.Reset();
        for (int32 Index = 0; Index < Part.Source->GetNumMaterials(); ++Index)
        {
            OutMaterials.Add(Part.Source->GetMaterial(Index));
        }
        return true;
    }
    return false;
}

USceneComponent* URallyBodyDeformComponent::GetDisplayComponent(UStaticMeshComponent* Source) const
{
    for (const FPart& Part : Parts)
    {
        if (Part.Source == Source)
        {
            return Part.Mesh;
        }
    }
    return Source;
}

void URallyBodyDeformComponent::AddDent(const FRallyDent& Dent)
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }
    if (Dents.Num() >= MaxDents)
    {
        // Past the cap new impacts still deform the server's copy but are not stored, so clients
        // and late joiners keep the first MaxDents.
        ApplyDent(Dent);
        return;
    }
    Dents.Add(Dent);
    OnRep_Dents();
}

void URallyBodyDeformComponent::ClearDents()
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }
    Dents.Reset();
    OnRep_Dents();
}

void URallyBodyDeformComponent::OnRep_Dents()
{
    if (Parts.IsEmpty())
    {
        return;
    }
    // The list shrank (repair): start again from the undamaged meshes.
    if (Dents.Num() < AppliedDents)
    {
        for (FPart& Part : Parts)
        {
            RebuildPart(Part);
        }
        AppliedDents = 0;
    }
    if (AppliedDents == Dents.Num())
    {
        return;
    }
    const double Start = FPlatformTime::Seconds();
    const int32 First = AppliedDents;
    for (; AppliedDents < Dents.Num(); ++AppliedDents)
    {
        ApplyDent(Dents[AppliedDents]);
    }
    UE_LOG(LogRallyDeform, Display, TEXT("RALLY_DAMAGE dents applied=%d total=%d in %.1f ms"), AppliedDents - First, AppliedDents,
        (FPlatformTime::Seconds() - Start) * 1000.0);
}

void URallyBodyDeformComponent::ApplyDent(const FRallyDent& Dent)
{
    const FVector3d Centre(Dent.Point);
    const FVector3d Extent = FVector3d(Dent.Extent).ComponentMax(FVector3d::ZeroVector);
    const double Radius = Dent.Radius;
    const double Depth = Dent.Depth;
    if (Radius <= 1.0 || Depth <= 0.0)
    {
        return;
    }
    const FBox Reach(Centre - Extent - FVector3d(Radius), Centre + Extent + FVector3d(Radius));
    for (FPart& Part : Parts)
    {
        if (!Reach.Intersect(Part.CarBounds))
        {
            continue;
        }
        // Distances and noise are taken in the car's frame, the push is applied in the part's mesh
        // space. Doors are dented as if closed.
        const FTransform& ToCar = Part.BaseRelative;
        const FVector3d Direction = ToCar.InverseTransformVectorNoScale(FVector(Dent.Direction)).GetSafeNormal();
        Part.Mesh->EditMesh([&](FDynamicMesh3& Mesh)
        {
            FDynamicMeshColorOverlay* Colors = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryColors() : nullptr;
            TArray<int32> Moved;
            for (const int32 Vertex : Mesh.VertexIndicesItr())
            {
                const FVector3d Position = Mesh.GetVertex(Vertex);
                const FVector3d InCar = ToCar.TransformPosition(Position);
                // Distance from the patch box.
                const FVector3d Offset = InCar - Centre;
                const FVector3d Outside(
                    FMath::Max(FMath::Abs(Offset.X) - Extent.X, 0.0),
                    FMath::Max(FMath::Abs(Offset.Y) - Extent.Y, 0.0),
                    FMath::Max(FMath::Abs(Offset.Z) - Extent.Z, 0.0));
                const double Distance = Outside.Length();
                if (Distance >= Radius)
                {
                    continue;
                }
                // Flat under the patch, then a smooth (1 - u^2)^2 shoulder. Noise varies the push and
                // buckles the metal sideways to it, so panels that run along the push (bonnet, wings)
                // ripple instead of sliding. Every input is the vertex position, so split copies of one
                // vertex move together and the surface does not tear.
                const double U = Distance / Radius;
                const double Push = Depth * FMath::Square(1.0 - U * U) * (1.0 + 0.35 * Noise(InCar, 12.0, 0.0));
                const FVector3d Wave(Noise(InCar, 16.0, 11.0), Noise(InCar, 16.0, 23.0), Noise(InCar, 16.0, 37.0));
                const FVector3d Buckle = ToCar.InverseTransformVectorNoScale(FVector(Wave));
                const FVector3d Sideways = Buckle - Direction * Buckle.Dot(Direction);
                Mesh.SetVertex(Vertex, Position + Direction * Push + Sideways * (0.45 * Push));
                Moved.Add(Vertex);

                // Paint flakes off in patches where the metal moved most; glass crazes the same way.
                const int32 Element = Part.ColorElements.IsValidIndex(Vertex) ? Part.ColorElements[Vertex] : INDEX_NONE;
                if (Colors && Element != INDEX_NONE)
                {
                    const double Flake = 0.6 + 0.8 * Noise(InCar, 5.0, 53.0);
                    const double Scrape = FMath::Clamp(Push / 16.0 * Flake, 0.0, 1.0);
                    FVector4f Color = Colors->GetElement(Element);
                    Color.X = FMath::Min(Color.X, static_cast<float>(1.0 - Scrape));
                    Colors->SetElement(Element, Color);
                }
            }
            RecomputeNormals(Part, Mesh, Moved);
        }, EDynamicMeshComponentRenderUpdateMode::NoUpdate, EDynamicMeshChangeType::DeformationEdit,
        EDynamicMeshAttributeChangeFlags::VertexPositions | EDynamicMeshAttributeChangeFlags::NormalsTangents | EDynamicMeshAttributeChangeFlags::VertexColors);
    }
}

void URallyBodyDeformComponent::RecomputeNormals(const FPart& Part, FDynamicMesh3& Mesh, const TArray<int32>& Moved)
{
    FDynamicMeshNormalOverlay* Normals = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;
    if (!Normals || Moved.IsEmpty())
    {
        return;
    }
    // Every vertex of a triangle that moved, and its seam copies.
    TBitArray<> Affected(false, Mesh.MaxVertexID());
    for (const int32 Vertex : Moved)
    {
        Mesh.EnumerateVertexTriangles(Vertex, [&](int32 Triangle)
        {
            const FIndex3i Tri = Mesh.GetTriangle(Triangle);
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const int32 Group = Part.WeldGroup[Tri[Corner]];
                for (int32 I = Part.WeldStart[Group]; I < Part.WeldStart[Group + 1]; ++I)
                {
                    Affected[Part.WeldVertices[I]] = true;
                }
            }
        });
    }
    // Each vertex keeps its own undamaged normal (so hard edges stay hard) turned by however much the
    // faces around that point turned. The faces counted are those on the vertex's side of any hard
    // edge, gathered across seam copies, so copies on a smooth seam turn identically.
    for (TConstSetBitIterator<> It(Affected); It; ++It)
    {
        const int32 Vertex = It.GetIndex();
        const int32 Element = Part.NormalElements[Vertex];
        const FVector3d Rest = FVector3d(Part.RestNormals[Vertex]);
        if (Element == INDEX_NONE || Rest.IsNearlyZero())
        {
            continue;
        }
        const FVector3d RestUnit = Rest.GetSafeNormal();
        FVector3d SumRest = FVector3d::ZeroVector;
        FVector3d SumNow = FVector3d::ZeroVector;
        const int32 Group = Part.WeldGroup[Vertex];
        for (int32 I = Part.WeldStart[Group]; I < Part.WeldStart[Group + 1]; ++I)
        {
            Mesh.EnumerateVertexTriangles(Part.WeldVertices[I], [&](int32 Triangle)
            {
                const FVector3d Before = Part.RestFaceNormals[Triangle];
                const double Alignment = Before.GetSafeNormal().Dot(RestUnit);
                if (FMath::Abs(Alignment) > 0.5)
                {
                    const double Sign = Alignment > 0.0 ? 1.0 : -1.0;
                    SumRest += Before * Sign;
                    SumNow += FaceNormal(Mesh, Triangle) * Sign;
                }
            });
        }
        if (SumRest.IsNearlyZero() || SumNow.IsNearlyZero())
        {
            continue;
        }
        const FQuat4d Turn = FQuat4d::FindBetweenVectors(SumRest.GetSafeNormal(), SumNow.GetSafeNormal());
        Normals->SetElement(Element, FVector3f(Turn.RotateVector(RestUnit)));
    }
}
