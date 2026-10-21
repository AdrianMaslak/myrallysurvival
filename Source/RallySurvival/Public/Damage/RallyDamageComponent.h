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
    // Hub and wheel studs: at 0 the wheel has come off.
    HubFrontLeft,
    HubFrontRight,
    HubRearLeft,
    HubRearRight,
    // Running gear (Damage/SuspensionRules.h). The Suspension parts above are each corner's arm (a front
    // one with its steering track rod); the TieRod parts are no longer used (kept so saves line up).
    StrutFrontLeft,
    StrutFrontRight,
    StrutRearLeft,
    StrutRearRight,
    TieRodFrontLeft,
    TieRodFrontRight,
    DriveshaftFrontLeft,
    DriveshaftFrontRight,
    BrakeFrontLeft,
    BrakeFrontRight,
    BrakeRearLeft,
    BrakeRearRight,
    // Engine wear (overheating, very hard hits; 0 = seized) and the battery with its wiring (0 = no
    // electrics). Damage/EngineRules.h.
    Engine,
    Battery,
    // Under the floor (Damage/UndersideRules.h): a cracked sump leaks oil, a holed tank leaks fuel, a torn
    // exhaust hangs and drags, a damaged gearbox jumps out of gear and loses gears.
    OilSump,
    FuelTank,
    Exhaust,
    Gearbox,
    // Engine modules that bolt on (docs/DAMAGE.md, Engine modules; Damage/EngineRules.h FModules). 0 when
    // missing as well as broken.
    AirFilter,
    Intake,
    ExhaustManifold,
    Turbo,
    Intercooler,
    Alternator,
    Starter,
    Ecu,
    // The four ignition coils, cylinder 1 at the gearbox end (EngineRules.h, Ignition).
    Coil1,
    Coil2,
    Coil3,
    Coil4,
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

    // Server only: a part was taken off (Health 0, it is missing) or another one fitted in its place,
    // bringing its own condition. Validation is the caller's.
    bool SetFittedPart(ERallyPart Part, float Health);

    // Saving (docs/SAVING.md).
    const TArray<FRallyPartCondition>& GetConditions() const { return Conditions; }
    // Server only: every part as saved (parts missing from an older save stay as they are).
    void RestoreConditions(const TArray<FRallyPartCondition>& Saved);
    // Server only: a hammer blow on a bent part (Rally::Hammer). A good one brings back Change, up to the bodge
    // (and marks it bodged there); a bad one (Change below zero) bends it further, never quite snapping it.
    bool HammerPart(ERallyPart Part, float Change);
    // Server only, development and tests: one part's health set as it is.
    void DebugSetHealth(ERallyPart Part, float Health);

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
