#include "Game/RallyTeamState.h"
#include "Damage/RallyPanelComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Game/EconomyRules.h"
#include "Game/RallyDayClock.h"
#include "Net/UnrealNetwork.h"
#include "Vehicle/RallyCarPawn.h"

ARallyTeamState::ARallyTeamState()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 1.0f;
    KittySk = Rally::Economy::StartingKittySk;
}

void ARallyTeamState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyTeamState, KittySk);
    DOREPLIFETIME(ARallyTeamState, Orders);
    DOREPLIFETIME(ARallyTeamState, DeliveryPoint);
}

ARallyTeamState* ARallyTeamState::Get(const UWorld* World)
{
    if (!World) return nullptr;
    TActorIterator<ARallyTeamState> It(const_cast<UWorld*>(World));
    return It ? *It : nullptr;
}

void ARallyTeamState::AddMoney(int32 Sk, const FString& Why)
{
    if (!HasAuthority() || Sk == 0) return;
    KittySk += Sk;
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("RALLY_MONEY %+d Sk (%s), kitty %d Sk"), Sk, *Why, KittySk);
}

bool ARallyTeamState::Order(int32 Item)
{
    const auto& Catalogue = Rally::Economy::Catalogue();
    if (!HasAuthority() || Item < 0 || Item >= static_cast<int32>(Catalogue.size())) return false;
    const Rally::Economy::FItem& Entry = Catalogue[Item];
    if (KittySk < Entry.PriceSk)
    {
        UE_LOG(LogTemp, Display, TEXT("RALLY_MONEY cannot afford the %s (%d Sk, kitty %d Sk)"), UTF8_TO_TCHAR(Entry.Name), Entry.PriceSk, KittySk);
        return false;
    }
    // Phoned in, it comes with the next morning's post.
    const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
    FRallyOrder Next;
    Next.Item = Item;
    Next.DueDay = Clock ? Clock->GetDay() + 1 : 1;
    Next.DueHour = static_cast<float>(Rally::Economy::DeliveryHour);
    Orders.Add(Next);
    AddMoney(-Entry.PriceSk, FString::Printf(TEXT("ordered %s"), UTF8_TO_TCHAR(Entry.Name)));
    return true;
}

bool ARallyTeamState::InServiceYard(const AActor* Car) const
{
    return Car && FVector::Dist2D(Car->GetActorLocation(), DeliveryPoint) < Rally::Economy::ServiceYardM * 100.0;
}

void ARallyTeamState::Restore(int32 Kitty, const TArray<FRallyOrder>& InOrders)
{
    if (!HasAuthority()) return;
    KittySk = Kitty;
    Orders = InOrders;
    ForceNetUpdate();
}

void ARallyTeamState::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
    if (!HasAuthority() || !Clock || Orders.IsEmpty()) return;
    const int32 Day = Clock->GetDay();
    const double Hour = Clock->GetHour();
    for (int32 I = Orders.Num() - 1; I >= 0; --I)
    {
        const FRallyOrder& Due = Orders[I];
        if (Day > Due.DueDay || (Day == Due.DueDay && Hour >= Due.DueHour))
        {
            Deliver(Due);
            Orders.RemoveAt(I);
            ForceNetUpdate();
        }
    }
}

void ARallyTeamState::Deliver(const FRallyOrder& Due)
{
    const auto& Catalogue = Rally::Economy::Catalogue();
    TActorIterator<ARallyCarPawn> It(GetWorld());
    if (!It || Due.Item < 0 || Due.Item >= static_cast<int32>(Catalogue.size())) return;
    const Rally::Economy::FItem& Entry = Catalogue[Due.Item];
    const int32 Slot = URallyPanelComponent::SlotForKind(UTF8_TO_TCHAR(Entry.Kind));
    const FVector Point = DeliveryPoint + FVector(FMath::FRandRange(-60.0, 60.0), FMath::FRandRange(-60.0, 60.0), 40.0);
    if (It->GetPanels()->DebugSpawnPart(Slot, Entry.Grade, Point))
        UE_LOG(LogTemp, Display, TEXT("RALLY_MONEY delivered %s to the service yard"), UTF8_TO_TCHAR(Entry.Name));
}
