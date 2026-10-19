#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Damage/LeakRules.h"
#include "RallyPuddleField.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

// What leaking cars have left on the ground (docs/DAMAGE.md, "Leaks"): oil, coolant and fuel puddles, each a
// decal that grows as drops land in it and shrinks as it dries. One per world, on every machine that draws;
// nothing in it is replicated or saved (each machine makes its own from the replicated leaks).
UCLASS()
class RALLYSURVIVAL_API ARallyPuddleField : public AActor
{
    GENERATED_BODY()
public:
    ARallyPuddleField();
    virtual void Tick(float DeltaSeconds) override;
    // The world's field, made on first use.
    static ARallyPuddleField* Get(UWorld* World);
    // A drop of Litres landed at Ground: it joins the puddle of that liquid it fell in, or starts one.
    void AddDrop(Rally::Leak::ELiquid Liquid, const FVector& Ground, double Litres);
    // How many puddles of a liquid there are and what is in them (tests).
    int32 CountPuddles(Rally::Leak::ELiquid Liquid, double* OutLitres = nullptr) const;
private:
    struct FPuddle
    {
        Rally::Leak::ELiquid Liquid;
        FVector Centre;
        double Litres = 0.0;
        TObjectPtr<UDecalComponent> Decal;
        TObjectPtr<UMaterialInstanceDynamic> Look;
    };
    TArray<FPuddle> Puddles;
    UPROPERTY(Transient) TArray<TObjectPtr<UDecalComponent>> DecalRefs;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PuddleMaterial;
    void Shape(FPuddle& Puddle);
    void Remove(int32 Index);
};
