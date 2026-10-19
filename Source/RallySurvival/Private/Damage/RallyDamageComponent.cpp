#include "Damage/RallyDamageComponent.h"
#include "Damage/PartConditionRules.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

URallyDamageComponent::URallyDamageComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    Conditions.SetNum(static_cast<int32>(ERallyPart::Count));
}

void URallyDamageComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(URallyDamageComponent, Conditions);
}

bool URallyDamageComponent::GetPartCondition(ERallyPart Part, FRallyPartCondition& OutCondition) const
{
    const int32 Index = static_cast<int32>(Part);
    if (!Conditions.IsValidIndex(Index))
    {
        OutCondition = FRallyPartCondition();
        return false;
    }
    OutCondition = Conditions[Index];
    return true;
}

bool URallyDamageComponent::ApplyPartDamage(ERallyPart Part, float Amount)
{
    const int32 Index = static_cast<int32>(Part);
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Conditions.IsValidIndex(Index))
        return false;

    if (!Rally::Damage::ApplyDamage(Conditions[Index].Health, Amount))
        return false;

    NotifyChanged();
    return true;
}

bool URallyDamageComponent::CompleteValidatedRepair(ERallyPart Part, bool bProperFix)
{
    const int32 Index = static_cast<int32>(Part);
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Conditions.IsValidIndex(Index))
        return false;

    FRallyPartCondition& Condition = Conditions[Index];
    if (!Rally::Damage::Repair(Condition.Health, Condition.bBodged, bProperFix))
        return false;

    NotifyChanged();
    return true;
}

bool URallyDamageComponent::SetFittedPart(ERallyPart Part, float Health)
{
    const int32 Index = static_cast<int32>(Part);
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Conditions.IsValidIndex(Index) || !FMath::IsFinite(Health))
        return false;
    Conditions[Index].Health = FMath::Clamp(Health, 0.0f, 1.0f);
    Conditions[Index].bBodged = false;
    NotifyChanged();
    return true;
}

void URallyDamageComponent::RestoreConditions(const TArray<FRallyPartCondition>& Saved)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    for (int32 I = 0; I < FMath::Min(Saved.Num(), Conditions.Num()); ++I)
    {
        Conditions[I].Health = FMath::IsFinite(Saved[I].Health) ? FMath::Clamp(Saved[I].Health, 0.0f, 1.0f) : 1.0f;
        Conditions[I].bBodged = Saved[I].bBodged;
    }
    NotifyChanged();
}

void URallyDamageComponent::DebugSetHealth(ERallyPart Part, float Health)
{
    const int32 Index = static_cast<int32>(Part);
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Conditions.IsValidIndex(Index) || !FMath::IsFinite(Health)) return;
    Conditions[Index].Health = FMath::Clamp(Health, 0.0f, 1.0f);
    NotifyChanged();
}

void URallyDamageComponent::NotifyChanged()
{
    GetOwner()->FlushNetDormancy();
    GetOwner()->ForceNetUpdate();
    OnDamageChanged.Broadcast();
}

void URallyDamageComponent::OnRep_Conditions()
{
    OnDamageChanged.Broadcast();
}
