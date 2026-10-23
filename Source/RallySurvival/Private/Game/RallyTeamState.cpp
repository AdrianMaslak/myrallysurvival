#include "Game/RallyTeamState.h"
#include "Damage/RallyPanelComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Game/EconomyRules.h"
#include "Game/RallyDayClock.h"
#include "Net/UnrealNetwork.h"
#include "Vehicle/RallyCarPawn.h"
#include "Player/RallyCharacter.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"

ARallyTeamState::ARallyTeamState()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 1.0f;
    KittySk = Rally::Economy::StartingKittySk;
    LoanSk = Rally::Economy::StartingLoanSk;
    NextPaymentDay = 1 + Rally::Economy::LoanEveryDays;
}

void ARallyTeamState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyTeamState, KittySk);
    DOREPLIFETIME(ARallyTeamState, Orders);
    DOREPLIFETIME(ARallyTeamState, DeliveryPoint);
    DOREPLIFETIME(ARallyTeamState, Note);
    DOREPLIFETIME(ARallyTeamState, NoteTime);
    DOREPLIFETIME(ARallyTeamState, LoanSk);
    DOREPLIFETIME(ARallyTeamState, NextPaymentDay);
    DOREPLIFETIME(ARallyTeamState, Letter);
    DOREPLIFETIME(ARallyTeamState, LetterTime);
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

bool ARallyTeamState::MovePocketMoney(ARallyCharacter* Who, int32 Sk)
{
    if (!HasAuthority() || !Who || Sk == 0 || !InServiceYard(Who)) return false;
    if (Sk > 0 ? KittySk < Sk : Who->GetPocketSk() < -Sk) return false;
    KittySk -= Sk;
    Who->AddPocketSk(Sk);
    // Everyone sees who took what.
    const FString Name = Who->GetPlayerState() ? Who->GetPlayerState()->GetPlayerName() : Who->GetName();
    Note = FString::Printf(TEXT("%s %s %d Sk %s the kitty"), *Name, Sk > 0 ? TEXT("took") : TEXT("put"), FMath::Abs(Sk), Sk > 0 ? TEXT("from") : TEXT("back in"));
    const AGameStateBase* State = GetWorld()->GetGameState();
    NoteTime = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("RALLY_MONEY %s, kitty %d Sk, pocket %d Sk"), *Note, KittySk, Who->GetPocketSk());
    return true;
}

bool ARallyTeamState::Order(int32 Item, ARallyCharacter* Buyer)
{
    const auto& Catalogue = Rally::Economy::Catalogue();
    if (!HasAuthority() || Item < 0 || Item >= static_cast<int32>(Catalogue.size())) return false;
    const Rally::Economy::FItem& Entry = Catalogue[Item];
    // One's own vices come out of one's own pocket.
    if (Buyer && Rally::Economy::PersonalItem(Entry.Kind))
    {
        if (Buyer->GetPocketSk() < Entry.PriceSk)
        {
            UE_LOG(LogTemp, Display, TEXT("RALLY_MONEY %s cannot afford the %s from their pocket"), *Buyer->GetName(), UTF8_TO_TCHAR(Entry.Name));
            return false;
        }
        Buyer->AddPocketSk(-Entry.PriceSk);
        const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
        FRallyOrder Mine;
        Mine.Item = Item;
        Mine.DueDay = Clock ? Clock->GetDay() + 1 : 1;
        Mine.DueHour = static_cast<float>(Rally::Economy::DeliveryHour);
        Orders.Add(Mine);
        ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("RALLY_MONEY %s ordered %s from their pocket (%d Sk left)"), *Buyer->GetName(), UTF8_TO_TCHAR(Entry.Name), Buyer->GetPocketSk());
        return true;
    }
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
    if (!Car) return false;
    if (FVector::Dist2D(Car->GetActorLocation(), DeliveryPoint) < Rally::Economy::ServiceYardM * 100.0) return true;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        if (It->ActorHasTag(TEXT("RallyService")) && FVector::Dist(Car->GetActorLocation(), It->GetActorLocation()) < 3500.0) return true;
    return false;
}

void ARallyTeamState::Restore(int32 Kitty, const TArray<FRallyOrder>& InOrders)
{
    if (!HasAuthority()) return;
    KittySk = Kitty;
    Orders = InOrders;
    ForceNetUpdate();
}

void ARallyTeamState::SendLetter(const FString& Text)
{
    if (!HasAuthority()) return;
    Letter = Text;
    const AGameStateBase* State = GetWorld()->GetGameState();
    LetterTime = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("RALLY_STORY letter: %s"), *Text.Left(80));
}

void ARallyTeamState::OnFirstStageDone()
{
    if (!HasAuthority() || (LettersMask & 2)) return;
    LettersMask |= 2;
    SendLetter(TEXT("A telegram from Varga Holding: \"Mr Varga saw your first stage. His works car was faster. "
        "He reminds you that the payments are due every Monday whether you finish or not.\""));
}

void ARallyTeamState::TickLoan()
{
    const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
    if (!Clock) return;
    // The first morning: the letter that starts it all.
    if (!(LettersMask & 1) && GetWorld()->GetTimeSeconds() > 3.0)
    {
        LettersMask |= 1;
        SendLetter(FString::Printf(TEXT("Lipova, 1996. The factory is shut, and you have a rally car: a written-off liftback bought with a loan of "
            "%s from Varga Holding, the firm that bought the factory. %s every seventh day, from the kitty. Mr Varga's own works team "
            "runs in the regional cup too. Win the money back on the stages."),
            UTF8_TO_TCHAR(Rally::Economy::Money(Rally::Economy::StartingLoanSk).c_str()), UTF8_TO_TCHAR(Rally::Economy::Money(Rally::Economy::LoanPaymentSk).c_str())));
    }
    if (LoanSk <= 0 || Clock->GetDay() < NextPaymentDay || (Clock->GetDay() == NextPaymentDay && Clock->GetHour() < 8.0)) return;
    // Payment day.
    NextPaymentDay += Rally::Economy::LoanEveryDays;
    const int32 Payment = FMath::Min(Rally::Economy::LoanPaymentSk, LoanSk);
    if (KittySk >= Payment)
    {
        AddMoney(-Payment, TEXT("loan payment to Varga Holding"));
        LoanSk -= Payment;
        SendLetter(LoanSk > 0 ? FString::Printf(TEXT("Varga Holding thanks you for your payment. Still owed: %s."), UTF8_TO_TCHAR(Rally::Economy::Money(LoanSk).c_str()))
            : FString(TEXT("Varga Holding: the loan is paid off. The car is yours. Mr Varga will see you on the stages.")));
    }
    else
    {
        LoanSk += Rally::Economy::MissedPaymentSk(Payment) - Payment;
        SendLetter(FString::Printf(TEXT("Varga Holding: your payment was not made. A tenth is added; you now owe %s. Mr Varga does not like to wait."),
            UTF8_TO_TCHAR(Rally::Economy::Money(LoanSk).c_str())));
        UE_LOG(LogTemp, Display, TEXT("RALLY_MONEY missed a loan payment, owed %d Sk"), LoanSk);
    }
    ForceNetUpdate();
}

void ARallyTeamState::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority()) TickLoan();
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
