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
    float DebugPhase = -1;
private:
    void BuildReference();
    TArray<FTransform> Reference;
    TArray<FTransform> Pose;
    TArray<FTransform> Smoothed;
    TMap<FName,int32> Indices;
    float Phase = 0;
    FName PoseName = NAME_None;
};
