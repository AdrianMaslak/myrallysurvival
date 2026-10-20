#pragma once
#include "CoreMinimal.h"
#include "Components/PoseableMeshComponent.h"
#include "RallyCrewMesh.generated.h"

// Lightweight authored locomotion and two-bone limb placement on the supplied PSX rig.
// Both views evaluate the same pose. Gameplay state is replicated by the owning character.
UCLASS()
class RALLYSURVIVAL_API URallyCrewMesh : public UPoseableMeshComponent
{
    GENERATED_BODY()
public:
    URallyCrewMesh();
    virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    FVector HandWorld(bool bLeft) const;
    FName GetPoseName() const { return PoseName; }
    bool bFirstPerson = false;
    // The player's own body in first person (docs/CHARACTERS.md): the whole suit, with the head and neck and the
    // arms (the arms mesh draws those) folded away, set back behind the eye so the camera is not inside it.
    bool bOwnerBody = false;
    float DebugPhase = -1;
private:
    void BuildReference();
    TArray<FTransform> Reference;
    TArray<FTransform> Pose;
    TArray<FTransform> Smoothed;
    // The bones' blended local transforms; the owner body's drawn ones fold some of them away.
    TArray<FTransform> Blended;
    TArray<int32> Folded;
    TMap<FName,int32> Indices;
    float Phase = 0;
    FName PoseName = NAME_None;
};
