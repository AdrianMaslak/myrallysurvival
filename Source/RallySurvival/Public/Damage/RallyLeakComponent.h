#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Damage/LeakRules.h"
#include "RallyLeakComponent.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class URallyDamageComponent;
class URallyEngineComponent;

// What a damaged car loses, seen (docs/DAMAGE.md, "Leaks"): drops falling from a cracked sump (oil), a
// holed radiator (coolant) and a holed tank (fuel) to the ground, where they make puddles that stay behind
// (ARallyPuddleField); oil soaks the engine's lower block dark and shiny, coolant wets the radiator. Drawn
// on every machine from the replicated damage and fluid levels; nothing of its own is replicated.
UCLASS()
class RALLYSURVIVAL_API URallyLeakComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URallyLeakComponent();
    void Initialize(URallyDamageComponent* InDamage, URallyEngineComponent* InEngine, UInstancedStaticMeshComponent* InDrops,
        UMaterialInstanceDynamic* InEngineLook, UMaterialInstanceDynamic* InRadiatorLook);
    void TickLeaks(float Dt);
    // Litres each liquid has lost here, as drawn (tests); how soaked the engine is (0..1).
    double GetLeakedL(Rally::Leak::ELiquid Liquid) const { return Leaked[static_cast<int>(Liquid)]; }
    double GetOilSoak() const { return Rally::Leak::OilSoak(Leaked[static_cast<int>(Rally::Leak::ELiquid::Oil)]); }
    int32 GetDropsInAir() const { return Drops.Num(); }
private:
    struct FDrop
    {
        Rally::Leak::ELiquid Liquid;
        FVector Location;
        FVector Velocity;
        double Litres;
        // Where it lands; it is gone into a puddle there.
        FVector Ground;
    };
    TArray<FDrop> Drops;
    double Owed[Rally::Leak::NumLiquids] = { 0.0, 0.0, 0.0 };
    double Leaked[Rally::Leak::NumLiquids] = { 0.0, 0.0, 0.0 };
    double RadiatorWet = 0.0;
    UPROPERTY(Transient) TObjectPtr<URallyDamageComponent> Damage;
    UPROPERTY(Transient) TObjectPtr<URallyEngineComponent> Engine;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> DropMeshes;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> EngineLook;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RadiatorLook;
    void Release(Rally::Leak::ELiquid Liquid, const FVector& CarPoint, const FVector& Spread, double Litres);
    void DrawDrops();
};
