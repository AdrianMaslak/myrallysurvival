#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "Damage/CrushProfiles.h"
#include "RallyBodyDeformComponent.generated.h"

class UDynamicMeshComponent;
class UStaticMeshComponent;
namespace UE::Geometry { class FDynamicMesh3; }

// One dent, in the car root's local space: the contact patch (a box, Point +- Extent) is pushed in by
// Depth, and the push fades to nothing Radius beyond the patch. A pole leaves a round dent, a wall
// flattens the whole face it touched.
USTRUCT(BlueprintType)
struct RALLYSURVIVAL_API FRallyDent
{
    GENERATED_BODY()

    UPROPERTY()
    FVector_NetQuantize10 Point;

    // Half-size of the contact patch along the car's axes; zero for a point contact.
    UPROPERTY()
    FVector_NetQuantize10 Extent;

    // Direction the metal moves (into the car).
    UPROPERTY()
    FVector_NetQuantizeNormal Direction;

    // Bit per initialized mesh: chassis 0, then panels, then fixed body parts. Missing parts cannot
    // receive later dents.
    UPROPERTY()
    uint32 PartsMask = 0xffffffff;

    UPROPERTY()
    float Radius = 50.0f;

    UPROPERTY()
    float Depth = 5.0f;
};

// Visual crash damage: replaces the body's static meshes with editable copies at runtime, pushes and
// buckles vertices around each dent, re-shades them and marks scraped paint in vertex colour R
// (1 = clean paint, 0 = bare metal; materials blend on it). The server adds dents; the list
// replicates, so every machine, including late joiners, rebuilds the same shape.
UCLASS(ClassGroup = (Rally), meta = (BlueprintSpawnableComponent))
class RALLYSURVIVAL_API URallyBodyDeformComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URallyBodyDeformComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Makes editable copies of these parts and hides the originals. Call once at BeginPlay. Kinds say
    // what each part is (Rally::Crush::EKind, default the shell), so each gives its own way.
    void Initialize(const TArray<UStaticMeshComponent*>& Parts, const FTransform& Frame = FTransform::Identity,
        const TArray<Rally::Crush::EKind>& Kinds = {});
    TArray<FRallyDent> SnapshotForPart(int32 PartIndex) const;
    void SetSnapshot(const TArray<FRallyDent>& Snapshot);
    float MaxDisplacement(UStaticMeshComponent* Part) const;

    // How far the part's surface nearest this point (the part's own frame, cm) has been pushed from where it was
    // made: what a bolt through it rides along with. Zero for an undented or undeformable part.
    FVector DisplacementNear(UStaticMeshComponent* Part, const FVector& Local) const;
    // Goes up whenever any part's shape changes (a dent, a straightening).
    int32 GetRevision() const { return Revision; }

    // The editable copy drawn in place of a part, or the part itself before Initialize.
    USceneComponent* GetDisplayComponent(UStaticMeshComponent* Part) const;

    // Copies the current dented geometry and material set for a detached debris actor.
    bool CopyPartMesh(UStaticMeshComponent* Part, UE::Geometry::FDynamicMesh3& OutMesh, TArray<UMaterialInterface*>& OutMaterials) const;

    // Server only.
    void AddDent(const FRallyDent& Dent);

    // Server only: straightens every part (debug repair until panel repair exists).
    void ClearDents();
    // Saving (docs/SAVING.md).
    const TArray<FRallyDent>& GetDents() const { return Dents; }

    int32 GetDentCount() const { return Dents.Num(); }

    // Replicated dents are capped so the list and a late joiner's rebuild stay bounded.
    UPROPERTY(EditAnywhere, Category = "Damage")
    int32 MaxDents = 48;

private:
    struct FPart
    {
        int32 Index = 0;
        Rally::Crush::FProfile Profile;
        float MaxDisplacementCm = 0;
        TArray<FVector3d> RestPositions;
        TObjectPtr<UStaticMeshComponent> Source;
        TObjectPtr<UDynamicMeshComponent> Mesh;
        // Part's transform relative to the car root when it was copied (doors are closed then).
        FTransform BaseRelative;
        FBox LocalBounds;
        // LocalBounds in the car's frame.
        FBox CarBounds;
        // Overlay element of each vertex.
        TArray<int32> ColorElements;
        TArray<int32> NormalElements;
        // Undamaged vertex and (area-weighted) face normals.
        TArray<FVector3f> RestNormals;
        TArray<FVector3d> RestFaceNormals;
        // Way each surface point folds when squeezed: its normal averaged over its seam copies and
        // turned to face out of the car, so seam copies and both skins of a panel move together.
        TArray<FVector3f> FoldNormals;
        // Render vertices at one surface point (split at seams): group of each vertex, and each
        // group's vertices as WeldVertices[WeldStart[G] .. WeldStart[G + 1]).
        TArray<int32> WeldGroup;
        TArray<int32> WeldStart;
        TArray<int32> WeldVertices;
    };

    UFUNCTION()
    void OnRep_Dents();

    void ApplyDent(const FRallyDent& Dent);
    // Fresh copy of the part's source mesh, undamaged.
    bool RebuildPart(FPart& Part);
    static void RecomputeNormals(const FPart& Part, UE::Geometry::FDynamicMesh3& Mesh, const TArray<int32>& Moved);

    UPROPERTY(ReplicatedUsing = OnRep_Dents)
    TArray<FRallyDent> Dents;

    TArray<FPart> Parts;
    int32 AppliedDents = 0;
    int32 Revision = 0;
};
