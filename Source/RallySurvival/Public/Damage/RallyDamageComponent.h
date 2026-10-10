#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RallyDamageComponent.generated.h"

UENUM(BlueprintType)
enum class ERallyPart : uint8
{
    TyreFrontLeft,
    TyreFrontRight,
    TyreRearLeft,
    TyreRearRight,
    SuspensionFrontLeft,
    SuspensionFrontRight,
    SuspensionRearLeft,
    SuspensionRearRight,
    Radiator,
    Count UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct FRallyPartCondition
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Damage")
    float Health = 1.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Damage")
    bool bBodged = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRallyDamageChanged);

// Add to a replicated vehicle actor. These are server-local mutations, not client RPCs.
// The interaction layer must validate tools, inventory, distance and completion first.
UCLASS(ClassGroup = (Rally), meta = (BlueprintSpawnableComponent))
class RALLYSURVIVAL_API URallyDamageComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URallyDamageComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintPure, Category = "Rally|Damage")
    bool GetPartCondition(ERallyPart Part, FRallyPartCondition& OutCondition) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Rally|Damage")
    bool ApplyPartDamage(ERallyPart Part, float Amount);

    // True only when a state change was committed. This does not consume inventory.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Rally|Damage")
    bool CompleteValidatedRepair(ERallyPart Part, bool bProperFix);

    // Also broadcast on the server so listen-host visuals update.
    UPROPERTY(BlueprintAssignable, Category = "Rally|Damage")
    FRallyDamageChanged OnDamageChanged;

private:
    UPROPERTY(ReplicatedUsing = OnRep_Conditions)
    TArray<FRallyPartCondition> Conditions;

    UFUNCTION()
    void OnRep_Conditions();

    void NotifyChanged();
};
