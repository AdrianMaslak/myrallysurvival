#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RallyRoad.generated.h"

class UDynamicMeshComponent;
class UPhysicalMaterial;
class UStaticMesh;
class UStaticMeshComponent;

// A short road section. Mud keeps a replicated, centimetre-deep collision rut field.
UCLASS()
class RALLYSURVIVAL_API ARallyRoad : public AActor
{
    GENERATED_BODY()
public:
    ARallyRoad();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Road") void Rebuild();
    UFUNCTION(BlueprintCallable, Category="Road") bool ApplyTrack(FVector From, FVector To, float DepthCm);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Road") TArray<FVector> Points;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Road") TArray<FVector> Rights;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Road") float WidthCm = 640.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Road") bool bMud = false;
    // Optional verges, one per point: X the left bank and Y the right bank (cm above the road's centre; below
    // zero a drop), Z the ditch depth (cm). With them the road is just the carriageway, and an actor with
    // bVergeOnly builds the shoulders, ditches and banks either side instead. Without them, the old 1.5 m
    // shoulders.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Road") TArray<FVector> Verges;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Road") bool bVergeOnly = false;
    // Unevenness (cm): bumps and dips up to about this, the same at a place whatever section builds it.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Road") float BumpCm = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Road") TObjectPtr<UMaterialInterface> Material;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Road") TObjectPtr<UPhysicalMaterial> Surface;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Road") TObjectPtr<UDynamicMeshComponent> Road;
private:
    UFUNCTION() void OnRep_Depths();
    UPROPERTY(ReplicatedUsing=OnRep_Depths) TArray<uint8> Depths;
    TArray<FVector> Base;
    TMap<TWeakObjectPtr<AActor>, TArray<FVector>> Previous;
    int32 Columns = 0;
};

// Rocks and logs use ordinary Chaos bodies, with movement replicated by the server.
UCLASS()
class RALLYSURVIVAL_API ARallyRoadDebris : public AActor
{
    GENERATED_BODY()
public:
    ARallyRoadDebris();
    virtual void BeginPlay() override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Debris") TObjectPtr<UStaticMeshComponent> Shape;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debris") float MassKg = 25.0f;
};

UCLASS()
class RALLYSURVIVAL_API URallyLandscapeTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Landscape", meta=(WorldContext="Context"))
    static AActor* ImportTerrain(UObject* Context, const FString& File, int32 Size, FVector Origin,
                                FVector Scale, UMaterialInterface* Material);
    // One actor holding many copies of a mesh (trees, posts, guardrails), drawn as instances.
    UFUNCTION(BlueprintCallable, Category="Landscape", meta=(WorldContext="Context"))
    static AActor* SpawnInstances(UObject* Context, const FString& Label, UStaticMesh* Mesh, UMaterialInterface* Material,
                                  const TArray<FTransform>& Transforms, bool bCollision, int32 CullDistanceCm);
};
