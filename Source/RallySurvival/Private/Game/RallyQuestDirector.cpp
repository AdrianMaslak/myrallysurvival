#include "Game/RallyQuestDirector.h"
#include "Damage/RallyEngineComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Game/RallyDayClock.h"
#include "Game/RallyStageDirector.h"
#include "Game/RallyTeamState.h"
#include "Net/UnrealNetwork.h"
#include "Player/RallyCharacter.h"
#include "Vehicle/RallyCarPawn.h"

namespace
{
    enum class ECheck : uint8 { ReachCar, EngineRunning, AtStageStart, FinishStage, InServiceYard, OrderPart, NextMorning };
    struct FObjective { const TCHAR* Quest; const TCHAR* Text; ECheck Check; int32 Stage; };
    // The first day, in order (docs/QUESTS.md: quests 1, 2, 3, 5 and 6, shortened).
    const FObjective Chain[] = {
        { TEXT("Shakedown"), TEXT("Find the team car in the service yard"), ECheck::ReachCar, 0 },
        { TEXT("Shakedown"), TEXT("Start the engine: I in the driver's seat"), ECheck::EngineRunning, 0 },
        { TEXT("Shakedown"), TEXT("Follow the SS1 signs to the start of the first stage"), ECheck::AtStageStart, 0 },
        { TEXT("SS1 Lipova Village"), TEXT("Stop at the line, take the countdown and run the stage to the finish"), ECheck::FinishStage, 0 },
        { TEXT("Service"), TEXT("Back to the service yard: fix what broke (P for the workshop)"), ECheck::InServiceYard, 0 },
        { TEXT("Read the road"), TEXT("Run SS2 Pine Ridge; the co-driver reads the road book"), ECheck::FinishStage, 1 },
        { TEXT("Shopping"), TEXT("Order a sport or rally part from the catalogue (K)"), ECheck::OrderPart, 0 },
        { TEXT("Fields of Hlinik"), TEXT("Run SS3, the longest stage"), ECheck::FinishStage, 2 },
        { TEXT("Day one"), TEXT("Sleep in the car (Z) through to the morning, when the parts arrive"), ECheck::NextMorning, 0 },
    };
}

ARallyQuestDirector::ARallyQuestDirector()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.5f;
}

void ARallyQuestDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyQuestDirector, Step);
}

ARallyQuestDirector* ARallyQuestDirector::Get(const UWorld* World)
{
    if (!World) return nullptr;
    TActorIterator<ARallyQuestDirector> It(const_cast<UWorld*>(World));
    return It ? *It : nullptr;
}

FString ARallyQuestDirector::QuestTitle() const
{
    return Step >= 0 && Step < UE_ARRAY_COUNT(Chain) ? FString(Chain[Step].Quest) : FString();
}

FString ARallyQuestDirector::ObjectiveText() const
{
    return Step >= 0 && Step < UE_ARRAY_COUNT(Chain) ? FString(Chain[Step].Text) : FString();
}

bool ARallyQuestDirector::IsDone(int32 Index) const
{
    TActorIterator<ARallyCarPawn> CarIt(GetWorld());
    const ARallyCarPawn* Car = CarIt ? *CarIt : nullptr;
    if (!Car) return false;
    const FObjective& Objective = Chain[Index];
    switch (Objective.Check)
    {
    case ECheck::ReachCar:
        for (TActorIterator<ARallyCharacter> It(GetWorld()); It; ++It)
            if (It->GetSeatedCar() == Car || FVector::Dist(It->GetActorLocation(), Car->GetActorLocation()) < 400.0) return true;
        return false;
    case ECheck::EngineRunning:
        return Car->GetEngine() && Car->GetEngine()->IsRunning();
    case ECheck::AtStageStart:
    {
        const ARallyStageDirector* Stages = ARallyStageDirector::Get(GetWorld());
        if (!Stages || Objective.Stage >= Stages->NumStages()) return true;
        double Side = 0.0;
        const double Past = Stages->PastLine(Objective.Stage, false, Car->GetActorLocation(), Side);
        return (Past > -40.0 && Past < 5.0 && Side < 20.0) || Stages->GetRun().Stage == Objective.Stage;
    }
    case ECheck::FinishStage:
    {
        const ARallyStageDirector* Stages = ARallyStageDirector::Get(GetWorld());
        if (!Stages) return true;
        for (const FRallyStageResult& Result : Stages->GetResults())
            if (Result.Stage == Objective.Stage) return true;
        return false;
    }
    case ECheck::InServiceYard:
    {
        const ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
        return !Team || Team->InServiceYard(Car);
    }
    case ECheck::OrderPart:
    {
        const ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
        return !Team || !Team->GetOrders().IsEmpty();
    }
    default:
    {
        const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
        return !Clock || Clock->GetDay() > StartDay;
    }
    }
}

void ARallyQuestDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || Step >= UE_ARRAY_COUNT(Chain)) return;
    // A night's sleep counts from the day the last quest began.
    if (Chain[Step].Check != ECheck::NextMorning)
        if (const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld())) StartDay = Clock->GetDay();
    while (Step < UE_ARRAY_COUNT(Chain) && IsDone(Step))
    {
        const bool bLastOfQuest = Step + 1 >= UE_ARRAY_COUNT(Chain) || FCString::Strcmp(Chain[Step + 1].Quest, Chain[Step].Quest) != 0;
        UE_LOG(LogTemp, Display, TEXT("RALLY_QUEST %s: %s - done%s"), Chain[Step].Quest, Chain[Step].Text, bLastOfQuest ? TEXT(" (quest complete)") : TEXT(""));
        ++Step;
        ForceNetUpdate();
    }
}
