#include "Damage/RallyBodyDeformComponent.h"
#include "Damage/HammerRules.h"
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

void URallyBodyDeformComponent::Initialize(const TArray<UStaticMeshComponent*>& SourceParts, const FTransform& Frame,
    const TArray<Rally::Crush::EKind>& Kinds)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Parts.IsEmpty())
    {
        return;
    }
    int32 Vertices = 0;
    for (int32 PartIndex = 0; PartIndex < SourceParts.Num(); ++PartIndex)
    {
        UStaticMeshComponent* Source = SourceParts[PartIndex];
        if (!Source || !Source->GetStaticMesh())
        {
            continue;
        }
        FPart Part;
        Part.Source = Source;
        Part.Index = PartIndex;
        Part.Profile = Rally::Crush::ForKind(Kinds.IsValidIndex(PartIndex) ? Kinds[PartIndex] : Rally::Crush::EKind::Shell);
        Part.BaseRelative = Source->GetRelativeTransform() * Frame;
        Part.LocalBounds = Source->GetStaticMesh()->GetBoundingBox();
        Part.CarBounds = Part.LocalBounds.TransformBy(Part.BaseRelative);
        UDynamicMeshComponent* Copy = NewObject<UDynamicMeshComponent>(Owner, *(Source->GetName() + TEXT("_Deform")));
        Part.Mesh = Copy;
        if (!RebuildPart(Part))
        {
            Copy->MarkAsGarbage();
            continue;
        }
        Copy->SetupAttachment(Source);
        Copy->SetRelativeTransform(FTransform::Identity);
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
    if (!FStaticMeshLODResourcesToDynamicMesh::Convert(&LOD, Options, Mesh))
    {
        return false;
    }
    // One colour element per vertex. R is the paint condition the materials blend on, intact here; G and B
    // keep the mesh's own colours (the v2 body's Grime, white on meshes without vertex colours).
    Mesh.EnableAttributes();
    TArray<FVector4f> Imported;
    Imported.Init(FVector4f(1.0f, 1.0f, 1.0f, 1.0f), Mesh.MaxVertexID());
    if (FDynamicMeshColorOverlay* Source = Mesh.Attributes()->PrimaryColors())
    {
        for (const int32 Triangle : Mesh.TriangleIndicesItr())
        {
            if (!Source->IsSetTriangle(Triangle)) continue;
            const FIndex3i Tri = Mesh.GetTriangle(Triangle);
            const FIndex3i Elements = Source->GetTriangle(Triangle);
            for (int32 Corner = 0; Corner < 3; ++Corner) Imported[Tri[Corner]] = Source->GetElement(Elements[Corner]);
        }
        Source->ClearElements();
    }
    Mesh.Attributes()->EnablePrimaryColors();
    FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();
    TArray<int32> NewElements;
    NewElements.Init(INDEX_NONE, Mesh.MaxVertexID());
    for (const int32 Vertex : Mesh.VertexIndicesItr())
    {
        NewElements[Vertex] = Colors->AppendElement(FVector4f(1.0f, Imported[Vertex].Y, Imported[Vertex].Z, 1.0f));
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
    Part.RestPositions.SetNum(Mesh.MaxVertexID());
    Part.Peak.Init(0.0f, Mesh.MaxVertexID());
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
        Part.RestPositions[Vertex] = P;
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
    // "Out of the car" is away from a point a little below the body's centre, so the bonnet, roof and
    // floor, which sit near the centre's height, still get a clear side.
    const FVector3d CarInside(0.0, 0.0, -30.0);
    Part.FoldNormals.Init(FVector3f::ZeroVector, Mesh.MaxVertexID());
    for (int32 Group = 0; Group + 1 < Part.WeldStart.Num(); ++Group)
    {
        FVector3d Sum = FVector3d::ZeroVector;
        for (int32 I = Part.WeldStart[Group]; I < Part.WeldStart[Group + 1]; ++I)
        {
            const int32 Vertex = Part.WeldVertices[I];
            const FVector3d Normal = Part.BaseRelative.TransformVectorNoScale(FVector(Part.RestNormals[Vertex]));
            const FVector3d Out = Part.BaseRelative.TransformPosition(FVector(Part.RestPositions[Vertex])) - CarInside;
            Sum += Normal.Dot(Out) < 0.0 ? -Normal : Normal;
        }
        const FVector3f Fold(Part.BaseRelative.InverseTransformVectorNoScale(FVector(Sum)).GetSafeNormal());
        for (int32 I = Part.WeldStart[Group]; I < Part.WeldStart[Group + 1]; ++I)
        {
            Part.FoldNormals[Part.WeldVertices[I]] = Fold;
        }
    }
    Part.MaxDisplacementCm = 0;
    Part.Mesh->SetMesh(MoveTemp(Mesh));
    ++Revision;
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

TArray<FRallyDent> URallyBodyDeformComponent::SnapshotForPart(int32 PartIndex) const
{
    TArray<FRallyDent> Result;
    for (FRallyDent Dent : Dents)
    {
        if (Dent.PartsMask & (1u << PartIndex))
        {
            Dent.PartsMask = 1; // A debris actor has just one deformable mesh.
            Result.Add(Dent);
        }
    }
    return Result;
}

void URallyBodyDeformComponent::SetSnapshot(const TArray<FRallyDent>& Snapshot)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    Dents = Snapshot;
    OnRep_Dents();
}

FVector URallyBodyDeformComponent::DisplacementNear(UStaticMeshComponent* Source, const FVector& Local) const
{
    for (const FPart& Part : Parts)
    {
        if (Part.Source != Source || !Part.Mesh || !Part.Mesh->GetMesh()) continue;
        if (Part.MaxDisplacementCm <= 0.0f) return FVector::ZeroVector;
        const FDynamicMesh3& Mesh = *Part.Mesh->GetMesh();
        const FVector3d At(Local);
        int32 Nearest = INDEX_NONE;
        double Best = TNumericLimits<double>::Max();
        for (int32 Vertex = 0; Vertex < Part.RestPositions.Num(); ++Vertex)
        {
            const double Distance = FVector3d::DistSquared(Part.RestPositions[Vertex], At);
            if (Distance < Best && Mesh.IsVertex(Vertex))
            {
                Best = Distance;
                Nearest = Vertex;
            }
        }
        return Nearest == INDEX_NONE ? FVector::ZeroVector : FVector(Mesh.GetVertex(Nearest) - Part.RestPositions[Nearest]);
    }
    return FVector::ZeroVector;
}

bool URallyBodyDeformComponent::FindAlongRay(const FVector& Start, const FVector& Direction, double Length, int32& OutPart,
    UStaticMeshComponent*& OutSource, FVector& OutPoint, FVector& OutDirection) const
{
    double Best = Length;
    bool bFound = false;
    const FVector Dir = Direction.GetSafeNormal();
    for (const FPart& Part : Parts)
    {
        // Not a panel off the car or a windscreen fallen out.
        if (!Part.Source || !Part.Mesh || !Part.Mesh->GetMesh() || !Part.Mesh->IsVisible() || Part.Mesh->bHiddenInGame) continue;
        // The part as it sits now (a door swung open), in its own mesh space.
        const FTransform Now = Part.Source->GetRelativeTransform();
        const FVector3d From(Now.InverseTransformPosition(Start));
        const FVector3d Way(Now.InverseTransformVectorNoScale(Dir));
        const FDynamicMesh3& Mesh = *Part.Mesh->GetMesh();
        int32 Nearest = INDEX_NONE;
        for (const int32 Vertex : Mesh.VertexIndicesItr())
        {
            const FVector3d P = Mesh.GetVertex(Vertex);
            const double T = (P - From).Dot(Way);
            if (T < 0.0 || T >= Best || (P - (From + Way * T)).SquaredLength() > 9.0) continue;
            Best = T;
            Nearest = Vertex;
        }
        if (Nearest == INDEX_NONE) continue;
        bFound = true;
        OutPart = Part.Index;
        OutSource = Part.Source;
        OutPoint = Part.BaseRelative.TransformPosition(FVector(Mesh.GetVertex(Nearest)));
        OutDirection = Part.BaseRelative.TransformVectorNoScale(FVector(Way));
    }
    return bFound;
}

float URallyBodyDeformComponent::MaxDisplacement(UStaticMeshComponent* Source) const
{
    for (const FPart& Part : Parts)
    {
        if (Part.Source != Source || !Part.Mesh || !Part.Mesh->GetMesh()) continue;
        return Part.MaxDisplacementCm;
    }
    return TNumericLimits<float>::Max(); // No geometry: do not silently allow a proper refit.
}

void URallyBodyDeformComponent::AddDent(const FRallyDent& Dent)
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }
    // Keep the server, observers and late joiners identical at the history cap.
    if (Dents.Num() >= MaxEntries) return;
    if (!Dent.bStraighten && Dents.FilterByPredicate([](const FRallyDent& D) { return !D.bStraighten; }).Num() >= MaxDents) return;
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

void URallyBodyDeformComponent::ApplyStraighten(const FRallyDent& Blow)
{
    const FVector3d Centre(Blow.Point);
    const double Reach = FMath::Max(static_cast<double>(Blow.Radius), 1.0);
    ++Revision;
    for (FPart& Part : Parts)
    {
        if (!(Blow.PartsMask & (1u << Part.Index)) || Part.MaxDisplacementCm <= 0.0f
            || !Part.CarBounds.ExpandBy(Reach).IsInside(FVector(Centre))) continue;
        const FTransform& ToCar = Part.BaseRelative;
        Part.Mesh->EditMesh([&](FDynamicMesh3& Mesh)
        {
            TArray<int32> Moved;
            for (const int32 Vertex : Mesh.VertexIndicesItr())
            {
                // Measured where the metal was pressed, so seam copies stay together.
                const double Weight = Rally::Hammer::Weight((ToCar.TransformPosition(FVector(Part.RestPositions[Vertex])) - FVector(Centre)).Length());
                if (Weight <= 0.0) continue;
                const FVector3d Rest = Part.RestPositions[Vertex];
                const FVector3d Bent = Mesh.GetVertex(Vertex) - Rest;
                const double Now = Bent.Length();
                if (Now < 0.01) continue;
                const double After = Rally::Hammer::Straightened(Now, Part.Peak.IsValidIndex(Vertex) ? Part.Peak[Vertex] : Now, Weight);
                if (After >= Now) continue;
                Mesh.SetVertex(Vertex, Rest + Bent * (After / Now));
                Moved.Add(Vertex);
            }
            RecomputeNormals(Part, Mesh, Moved);
            // The worst left on the part, now.
            Part.MaxDisplacementCm = 0.0f;
            for (const int32 Vertex : Mesh.VertexIndicesItr())
                Part.MaxDisplacementCm = FMath::Max(Part.MaxDisplacementCm, static_cast<float>((Mesh.GetVertex(Vertex) - Part.RestPositions[Vertex]).Length()));
        }, EDynamicMeshComponentRenderUpdateMode::NoUpdate, EDynamicMeshChangeType::DeformationEdit,
        EDynamicMeshAttributeChangeFlags::VertexPositions | EDynamicMeshAttributeChangeFlags::NormalsTangents);
    }
}

void URallyBodyDeformComponent::ApplyDent(const FRallyDent& Dent)
{
    if (Dent.bStraighten)
    {
        ApplyStraighten(Dent);
        return;
    }
    const FVector3d Centre(Dent.Point);
    const FVector3d Extent = FVector3d(Dent.Extent).ComponentMax(FVector3d::ZeroVector);
    const double Radius = Dent.Radius;
    if (Radius <= 1.0 || Dent.Depth <= 0.0)
    {
        return;
    }
    ++Revision;
    // A facet's corners reach in from up to a cell's diagonal beyond the radius.
    const double Fringe = Rally::Crush::FacetCm * 1.75;
    const FBox Reach(Centre - Extent - FVector3d(Radius + Fringe), Centre + Extent + FVector3d(Radius + Fringe));
    const FVector3d CarDirection = FVector3d(Dent.Direction).GetSafeNormal();
    // The dent's shape at a point of the car: flat under the patch, then a (1 - u^2)^2 shoulder varied a little.
    const auto Shoulder = [&](const FVector3d& InCar)
    {
        const FVector3d Offset = InCar - Centre;
        const FVector3d Outside(FMath::Max(FMath::Abs(Offset.X) - Extent.X, 0.0), FMath::Max(FMath::Abs(Offset.Y) - Extent.Y, 0.0),
            FMath::Max(FMath::Abs(Offset.Z) - Extent.Z, 0.0));
        const double U = Outside.Length() / Radius;
        return U >= 1.0 ? 0.0 : FMath::Square(1.0 - U * U) * (1.0 + 0.12 * Noise(InCar, 30.0, 0.0));
    };
    // Sampled at the grid's corners (shared by every part and vertex of this dent) and blended trilinearly
    // (Rally::Crush::FacetCm): the metal folds in flat facets. Seam copies sit at the same point, so they stay together.
    TMap<FIntVector, double> Corners;
    const auto Faceted = [&](const FVector3d& InCar)
    {
        const FVector3d Grid = InCar / Rally::Crush::FacetCm;
        const FIntVector Cell(FMath::FloorToInt32(Grid.X), FMath::FloorToInt32(Grid.Y), FMath::FloorToInt32(Grid.Z));
        const FVector3d Into = Grid - FVector3d(Cell.X, Cell.Y, Cell.Z);
        double Sum = 0.0;
        for (int32 Corner = 0; Corner < 8; ++Corner)
        {
            const FIntVector Key = Cell + FIntVector(Corner & 1, (Corner >> 1) & 1, (Corner >> 2) & 1);
            double Value;
            if (const double* Found = Corners.Find(Key)) Value = *Found;
            else Value = Corners.Add(Key, Shoulder(FVector3d(Key.X, Key.Y, Key.Z) * Rally::Crush::FacetCm));
            Sum += Value * ((Corner & 1) ? Into.X : 1.0 - Into.X) * (((Corner >> 1) & 1) ? Into.Y : 1.0 - Into.Y)
                * (((Corner >> 2) & 1) ? Into.Z : 1.0 - Into.Z);
        }
        return Sum;
    };
    for (FPart& Part : Parts)
    {
        if (!(Dent.PartsMask & (1u << Part.Index)) || !Reach.Intersect(Part.CarBounds))
        {
            continue;
        }
        // Distances and noise are taken in the car's frame, the push is applied in the part's mesh
        // space. Doors are dented as if closed. Each part gives its own way (CrushProfiles.h): plastic
        // springs part of the dent back out, a door's beams resist, a bonnet folds in big creases.
        const Rally::Crush::FProfile& Give = Part.Profile;
        const double Depth = Dent.Depth * Give.Give * (1.0 - Give.Springback);
        const double FoldWavelength = Give.FoldWavelength;
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
                if (Distance >= Radius + Fringe)
                {
                    continue;
                }
                // The dent's faceted shape (Faceted, above). Metal already pushed far gives way less.
                const double Room = FMath::Clamp(1.0 - (Position - Part.RestPositions[Vertex]).Length() / Give.MaxCrushCm, 0.0, 1.0);
                const double U = FMath::Min(Distance / Radius, 1.0);
                const double Push = Room * Depth * Faceted(InCar);
                if (Push <= 0.0 && U >= 1.0)
                {
                    continue;
                }

                // Where the push falls off along its own direction (a bonnet or wing behind a frontal
                // hit) the metal is squeezed lengthwise. Sheet metal cannot shorten, so it folds out of
                // its plane into creases running across the push, deeper the harder it is squeezed;
                // under the patch, where everything is shoved together, it barely crumples. Every input
                // is the vertex position or shared by its seam copies, so the surface does not tear.
                const FVector3d Away(FMath::Sign(Offset.X) * Outside.X, FMath::Sign(Offset.Y) * Outside.Y, FMath::Sign(Offset.Z) * Outside.Z);
                const double Squeeze = Distance > 0.0
                    ? 4.0 * U * (1.0 - U * U) * Depth / Radius * FMath::Abs(Away.Dot(CarDirection)) / Distance : 0.0;
                const double FoldDepth = Give.Folding * (Room * FoldWavelength / UE_DOUBLE_PI * FMath::Sqrt(FMath::Min(Squeeze, 0.6)) + 0.06 * Push);
                const double Along = InCar.Dot(CarDirection);
                const FVector3d Creases = CarDirection * Along + (InCar - CarDirection * Along) / 3.0;
                // Mostly outward: folds rise out of the car rather than into it.
                const double Fold = FoldDepth * (Noise(Creases, FoldWavelength, 11.0) + 0.25);
                const FVector3d FoldNormal(Part.FoldNormals.IsValidIndex(Vertex) ? Part.FoldNormals[Vertex] : FVector3f::ZeroVector);
                const FVector3d FoldWay = FoldNormal - Direction * FoldNormal.Dot(Direction);
                Mesh.SetVertex(Vertex, Position + Direction * Push + FoldWay * Fold);
                const float Bent = static_cast<float>((Mesh.GetVertex(Vertex) - Part.RestPositions[Vertex]).Length());
                Part.MaxDisplacementCm = FMath::Max(Part.MaxDisplacementCm, Bent);
                if (Part.Peak.IsValidIndex(Vertex)) Part.Peak[Vertex] = FMath::Max(Part.Peak[Vertex], Bent);
                Moved.Add(Vertex);

                // Paint scrapes off the faces that met the obstacle and cracks along the creases;
                // glass crazes the same way. Faces running along the push keep most of their paint.
                const int32 Element = Part.ColorElements.IsValidIndex(Vertex) ? Part.ColorElements[Vertex] : INDEX_NONE;
                if (Colors && Element != INDEX_NONE)
                {
                    const double Facing = FMath::Clamp(-FoldNormal.Dot(Direction), 0.0, 1.0);
                    const double Flake = 0.6 + 0.8 * Noise(InCar, 8.0, 53.0);
                    const double Scrape = FMath::Clamp((Push / 60.0 * Facing + Squeeze * 1.5) * Flake * Give.Scuff, 0.0, 1.0);
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
