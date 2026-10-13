#include "Game/RallyStageDirector.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "Damage/RallyDamageComponent.h"
#include "Damage/RallyPanelComponent.h"
#include "Damage/UndersideRules.h"
#include "Game/RallyGameMode.h"
#include "Game/RallyDayClock.h"
#include "Game/CalendarRules.h"
#include "Game/EconomyRules.h"
#include "Game/RallyTeamState.h"
#include "Vehicle/RallyCarPawn.h"

namespace
{
#include "CountrysideStages.inl"
}

ARallyStageDirector::ARallyStageDirector()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true;
    SetNetUpdateFrequency(10.0f);
    LoadStages();
}

void ARallyStageDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyStageDirector, Run);
    DOREPLIFETIME(ARallyStageDirector, Results);
    DOREPLIFETIME(ARallyStageDirector, ControlStage);
    DOREPLIFETIME(ARallyStageDirector, ControlDue);
    DOREPLIFETIME(ARallyStageDirector, Event);
    DOREPLIFETIME(ARallyStageDirector, Cup);
}

bool ARallyStageDirector::MapHasStages(const UWorld* World)
{
    return World && World->GetMapName().Contains(TEXT("Countryside"));
}

ARallyStageDirector* ARallyStageDirector::Get(const UWorld* World)
{
    if (!World) return nullptr;
    TActorIterator<ARallyStageDirector> It(const_cast<UWorld*>(World));
    return It ? *It : nullptr;
}

void ARallyStageDirector::LoadStages()
{
    for (const FRallyStageSource& Source : CountrysideStages)
    {
        FStage Stage;
        Stage.Id = Source.Id;
        Stage.Name = Source.Name;
        Stage.Surface = Source.Surface;
        Stage.WidthM = Source.WidthM;
        for (int32 I = 0; I < Source.Count; ++I) Stage.Route.push_back({ Source.Points[I][0], Source.Points[I][1], Source.Points[I][2] });
        Stage.Distance = Rally::Stage::Distances(Stage.Route);
        Stage.LengthM = Stage.Distance.empty() ? 0.0 : Stage.Distance.back();
        Stage.ParS = Rally::Stage::ParSeconds(Stage.LengthM, TCHAR_TO_UTF8(*Stage.Surface));
        Stage.Calls = Rally::Stage::PaceNotes(Stage.Route);
        // Unsigned curvature from the circle through each sample and its neighbours three either side.
        const int32 N = static_cast<int32>(Stage.Route.size());
        Stage.Curvature.assign(N, 0.0);
        for (int32 I = 3; I + 3 < N; ++I)
        {
            const auto& A = Stage.Route[I - 3];
            const auto& B = Stage.Route[I];
            const auto& C = Stage.Route[I + 3];
            const double AB = FMath::Sqrt(FMath::Square(B.X - A.X) + FMath::Square(B.Y - A.Y));
            const double BC = FMath::Sqrt(FMath::Square(C.X - B.X) + FMath::Square(C.Y - B.Y));
            const double CA = FMath::Sqrt(FMath::Square(A.X - C.X) + FMath::Square(A.Y - C.Y));
            const double Cross = FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X));
            if (AB * BC * CA > 1e-6) Stage.Curvature[I] = 2.0 * Cross / (AB * BC * CA);
        }
        Stages.Add(MoveTemp(Stage));
    }
}

double ARallyStageDirector::ServerNow() const
{
    const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
    return State ? State->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

double ARallyStageDirector::GetClock() const
{
    switch (GetPhase())
    {
    case EPhase::Running:
    case EPhase::Recce: return FMath::Max(ServerNow() - Run.StartTime, 0.0);
    case EPhase::Finished:
    case EPhase::Retired: return Run.EndTime - Run.StartTime;
    default: return 0.0;
    }
}

int32 ARallyStageDirector::GetCountdown() const
{
    if (GetPhase() != EPhase::Countdown) return 0;
    return FMath::Clamp(FMath::CeilToInt(Run.CountdownStart + Rally::Stage::CountdownSeconds - ServerNow()), 0, Rally::Stage::CountdownSeconds);
}

double ARallyStageDirector::Progress(int32 StageIndex, const FVector& World, int32& InOutHint) const
{
    if (!Stages.IsValidIndex(StageIndex)) return 0.0;
    const FStage& Stage = Stages[StageIndex];
    const int32 N = static_cast<int32>(Stage.Route.size());
    if (N == 0) return 0.0;
    // Onwards from the last place, a little back in case of a spin. Only from the start does it look along
    // the whole route: stages double back past themselves, and a car off the road must not jump legs.
    const FVector P = World / 100.0;
    const auto Search = [&](int32 From, int32 To, double& BestDist) {
        int32 Best = INDEX_NONE;
        for (int32 I = FMath::Max(From, 0); I < FMath::Min(To, N); ++I)
        {
            const double D = FMath::Square(Stage.Route[I].X - P.X) + FMath::Square(Stage.Route[I].Y - P.Y);
            if (D < BestDist) { BestDist = D; Best = I; }
        }
        return Best;
    };
    double BestDist = TNumericLimits<double>::Max();
    int32 Best = InOutHint > 0 ? Search(InOutHint - 5, InOutHint + 60, BestDist) : INDEX_NONE;
    if (Best == INDEX_NONE) { BestDist = TNumericLimits<double>::Max(); Best = Search(0, FMath::Min(N, 60), BestDist); }
    InOutHint = Best == INDEX_NONE ? 0 : Best;
    return Stage.Distance[InOutHint];
}

FVector ARallyStageDirector::PointAt(int32 StageIndex, double AlongM) const
{
    if (!Stages.IsValidIndex(StageIndex) || Stages[StageIndex].Route.empty()) return FVector::ZeroVector;
    const FStage& Stage = Stages[StageIndex];
    const auto It = std::lower_bound(Stage.Distance.begin(), Stage.Distance.end(), AlongM);
    const int32 I = FMath::Clamp(static_cast<int32>(It - Stage.Distance.begin()), 1, static_cast<int32>(Stage.Route.size()) - 1);
    const double Span = Stage.Distance[I] - Stage.Distance[I - 1];
    const double T = Span > 0.0 ? FMath::Clamp((AlongM - Stage.Distance[I - 1]) / Span, 0.0, 1.0) : 0.0;
    const auto& A = Stage.Route[I - 1];
    const auto& B = Stage.Route[I];
    return FVector(FMath::Lerp(A.X, B.X, T), FMath::Lerp(A.Y, B.Y, T), FMath::Lerp(A.Z, B.Z, T)) * 100.0;
}

double ARallyStageDirector::PastLine(int32 StageIndex, bool bFinish, const FVector& World, double& OutSideM) const
{
    OutSideM = TNumericLimits<double>::Max();
    if (!Stages.IsValidIndex(StageIndex) || Stages[StageIndex].Route.size() < 4) return -TNumericLimits<double>::Max();
    const auto& R = Stages[StageIndex].Route;
    const size_t N = R.size();
    // The line runs across the road at the gate; the stage's direction through it from the samples beside it.
    const Rally::Stage::FPoint Gate = bFinish ? R[N - 1] : R[0];
    const Rally::Stage::FPoint From = bFinish ? R[N - 4] : R[0];
    const Rally::Stage::FPoint To = bFinish ? R[N - 1] : R[3];
    const FVector2D Dir = FVector2D(To.X - From.X, To.Y - From.Y).GetSafeNormal();
    const FVector2D Offset(World.X / 100.0 - Gate.X, World.Y / 100.0 - Gate.Y);
    OutSideM = FMath::Abs(FVector2D::CrossProduct(Dir, Offset));
    return FVector2D::DotProduct(Dir, Offset);
}

TArray<ARallyStageDirector::FStanding> ARallyStageDirector::Standings(const FRallyStageResult& Result) const
{
    TArray<FStanding> List;
    if (!Stages.IsValidIndex(Result.Stage)) return List;
    const double Par = Stages[Result.Stage].ParS;
    List.Add({ TEXT("You"), Result.TimeS, Result.bRetired });
    uint32 Crew = 0;
    for (const Rally::Stage::FRival& Rival : Rally::Stage::Rivals())
    {
        bool bRetired = false;
        const uint32 Seed = 2654435761u * (static_cast<uint32>(Result.Stage) + 1u) + 40503u * Result.Attempt + 977u * ++Crew;
        const double Time = Rally::Stage::RivalTime(Rival, Par, Seed, bRetired);
        List.Add({ UTF8_TO_TCHAR(Rival.Crew), Time, bRetired });
    }
    List.StableSort([](const FStanding& A, const FStanding& B) { return A.bRetired != B.bRetired ? !A.bRetired : A.TimeS < B.TimeS; });
    return List;
}

TArray<ARallyStageDirector::FStanding> ARallyStageDirector::Overall(int32& OutStages) const
{
    TArray<FStanding> Total;
    OutStages = 0;
    for (int32 Stage = 0; Stage < Stages.Num(); ++Stage)
    {
        const FRallyStageResult* Latest = nullptr;
        for (const FRallyStageResult& Result : Results)
            if (Result.Stage == Stage && Result.Event == Event) Latest = &Result;
        if (!Latest) continue;
        ++OutStages;
        for (const FStanding& Row : Standings(*Latest))
        {
            FStanding* Sum = Total.FindByPredicate([&Row](const FStanding& Entry) { return Entry.Crew == Row.Crew; });
            if (Sum) Sum->TimeS += Row.TimeS;
            else Total.Add({ Row.Crew, Row.TimeS, false });
        }
    }
    Total.StableSort([](const FStanding& A, const FStanding& B) { return A.TimeS < B.TimeS; });
    return Total;
}

FString ARallyStageDirector::Scrutineer(const ARallyCarPawn* Car)
{
    if (!Car || !Car->GetPanels() || !Car->GetDamage()) return FString();
    const URallyPanelComponent* Panels = Car->GetPanels();
    for (int32 Corner = 0; Corner < 4; ++Corner)
    {
        FRallyPartCondition Hub;
        Car->GetDamage()->GetPartCondition(static_cast<ERallyPart>(static_cast<int32>(ERallyPart::HubFrontLeft) + Corner), Hub);
        const int32 Wheel = URallyPanelComponent::FirstWheel + Corner;
        if (Hub.Health <= 0.0f) return FString::Printf(TEXT("the %s is missing"), *URallyPanelComponent::PanelName(Wheel));
        if (!Panels->AllHome(Wheel)) return FString::Printf(TEXT("the %s's bolts are not all done up"), *URallyPanelComponent::PanelName(Wheel));
    }
    for (int32 Slot = URallyPanelComponent::FirstGear; Slot < URallyPanelComponent::FirstGear + URallyPanelComponent::NumGear; ++Slot)
    {
        if (Panels->IsRemoved(Slot)) return FString::Printf(TEXT("the %s is missing"), *URallyPanelComponent::PanelName(Slot));
        if (!Panels->AllHome(Slot)) return FString::Printf(TEXT("the %s is not bolted up"), *URallyPanelComponent::PanelName(Slot));
    }
    FRallyPartCondition Tank;
    Car->GetDamage()->GetPartCondition(ERallyPart::FuelTank, Tank);
    if (Rally::Underside::FuelLeakRate(Tank.Health) > 0.0) return TEXT("fuel is leaking from the tank");
    for (int32 Panel : { 0, 1, 2, 3, 4, 5 })
        if (Panels->IsOpen(Panel)) return FString::Printf(TEXT("shut the %s"), *URallyPanelComponent::PanelName(Panel));
    return FString();
}

FString ARallyStageDirector::CalendarRefusal(const UWorld* World)
{
    const ARallyDayClock* Clock = ARallyDayClock::Get(World);
    if (!Clock || Rally::Calendar::IsRallyDay(Clock->GetDay())) return FString();
    return FString::Printf(TEXT("%s; the next rally weekend starts %s (N: a recce)"),
        Rally::Calendar::IsOffSeason(Clock->GetDay()) ? TEXT("the season is over") : TEXT("no rally today"),
        *ARallyDayClock::DateText(Rally::Calendar::NextRallyDay(Clock->GetDay())));
}

bool ARallyStageDirector::StartRecce(const ARallyCarPawn* Car)
{
    if (!HasAuthority() || !Car || (GetPhase() != EPhase::Idle && GetPhase() != EPhase::Countdown)) return false;
    // During the countdown it is called off and the entry fee given back.
    if (GetPhase() == EPhase::Countdown)
        if (ARallyTeamState* Team = ARallyTeamState::Get(GetWorld()))
            Team->AddMoney(Rally::Economy::StageEntrySk, TEXT("entry fee back: a recce instead"));
    for (int32 I = 0; I < Stages.Num(); ++I)
    {
        double Side = 0.0;
        const double Past = PastLine(I, false, Car->GetActorLocation(), Side);
        if (Past <= -Rally::Stage::StartBoxM || Past >= 0.5 || Side >= Stages[I].WidthM * 0.5 + Rally::Stage::GateHalfWidthExtraM) continue;
        Run = FRallyStageRun();
        Run.Stage = static_cast<int8>(I);
        Run.Attempt = Run.Attempt;
        Run.StartTime = static_cast<float>(ServerNow());
        RunHint = 0;
        SetPhase(EPhase::Recce);
        UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE %s recce"), *Stages[I].Id);
        return true;
    }
    return false;
}

void ARallyStageDirector::RetireRun(const FString& Why)
{
    using namespace Rally::Stage;
    if (!HasAuthority() || !Stages.IsValidIndex(Run.Stage) || (GetPhase() != EPhase::Running && GetPhase() != EPhase::Countdown)) return;
    const FStage& Stage = Stages[Run.Stage];
    if (GetPhase() == EPhase::Countdown) Run.StartTime = Run.CountdownStart + CountdownSeconds;
    FRallyStageResult Result;
    Result.Stage = Run.Stage;
    Result.Attempt = Run.Attempt;
    Result.PenaltyS = Run.PenaltyS;
    Result.bRetired = true;
    Result.Event = Event;
    Run.EndTime = static_cast<float>(Run.StartTime + MaxSeconds(Stage.ParS));
    Result.TimeS = Run.EndTime - Run.StartTime + Run.PenaltyS;
    Results.Add(Result);
    SetPhase(EPhase::Retired);
    // Shown from now, not from the stage's closing time.
    Run.EndTime = static_cast<float>(ServerNow());
    Run.StartTime = Run.EndTime - static_cast<float>(MaxSeconds(Stage.ParS));
    UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE %s retired (%s): %.1f s"), *Stage.Id, *Why, Result.TimeS);
}

void ARallyStageDirector::RestoreResults(const TArray<FRallyStageResult>& Saved, uint8 Attempt)
{
    if (!HasAuthority()) return;
    Results = Saved;
    Run = FRallyStageRun();
    Run.Attempt = Attempt;
    ForceNetUpdate();
}

void ARallyStageDirector::FinishEvent()
{
    int32 Counted = 0;
    const TArray<FStanding> Final = Overall(Counted);
    // A new season's first event: the cup starts afresh (last season's table stayed up over the winter).
    const int32 Round = Event % Rally::Calendar::CupEvents + 1, Year = Rally::Calendar::StartYear + Event / Rally::Calendar::CupEvents;
    if (Round == 1) Cup.Reset();
    // Retired from (or never started) a stage of the event: no points from it.
    bool bOut = false;
    for (int32 Stage = 0; Stage < Stages.Num(); ++Stage)
    {
        const FRallyStageResult* Latest = nullptr;
        for (const FRallyStageResult& Result : Results)
            if (Result.Stage == Stage && Result.Event == Event) Latest = &Result;
        bOut = bOut || !Latest || Latest->bRetired;
    }
    FString Line;
    int32 Place = 0;
    for (int32 I = 0; I < Final.Num(); ++I)
    {
        const int32 Points = Final[I].Crew == TEXT("You") && bOut ? 0 : Rally::Stage::CupPoints(I + 1);
        FRallyCupEntry* Entry = Cup.FindByPredicate([&](const FRallyCupEntry& E) { return E.Crew == Final[I].Crew; });
        if (!Entry) { Cup.Add({ Final[I].Crew, 0 }); Entry = &Cup.Last(); }
        Entry->Points += Points;
        if (Final[I].Crew == TEXT("You")) Place = I + 1;
    }
    Cup.StableSort([](const FRallyCupEntry& A, const FRallyCupEntry& B) { return A.Points > B.Points; });
    int32 CupPlace = 1;
    while (CupPlace <= Cup.Num() && Cup[CupPlace - 1].Crew != TEXT("You")) ++CupPlace;
    const int32 Scored = bOut ? 0 : Rally::Stage::CupPoints(Place);
    UE_LOG(LogTemp, Display, TEXT("RALLY_CUP %d event %d done: %s, %d points; cup position %d"), Year, Round,
        bOut ? TEXT("retired or not started") : *FString::Printf(TEXT("%d of %d overall"), Place, Final.Num()), Scored, CupPlace);
    if (ARallyTeamState* Team = ARallyTeamState::Get(GetWorld()))
        Team->SendLetter(FString::Printf(TEXT("Regional cup %d, event %d: %s and take %d points. In the cup you are %d of %d, "
            "%d points behind the leader. %s"), Year, Round,
            bOut ? TEXT("you did not finish every stage") : *FString::Printf(TEXT("you finished %d of %d overall"), Place, Final.Num()),
            Scored, CupPlace, Cup.Num(), Cup.Num() > 0 ? Cup[0].Points - Cup[CupPlace - 1].Points : 0,
            Round == Rally::Calendar::CupEvents ? *FString::Printf(TEXT("That was the season's last event: the final standings stay on J over the winter, "
                    "and the %d cup starts in April."), Year + 1)
                : TEXT("The next event runs the same stages on the next rally weekend; the road book is yours to learn.")));
    ++Event;
    // No road section runs on into the next event.
    ControlStage = -1;
    ForceNetUpdate();
}

ARallyCarPawn* ARallyStageDirector::FindCar() const
{
    TActorIterator<ARallyCarPawn> It(GetWorld());
    return It ? *It : nullptr;
}

void ARallyStageDirector::SetPhase(EPhase Phase)
{
    Run.Phase = static_cast<uint8>(Phase);
    ForceNetUpdate();
}

void ARallyStageDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority())
    {
        CloseMissedEvents();
        TickServer(DeltaSeconds);
    }
}

void ARallyStageDirector::CloseMissedEvents()
{
    // Not while a stage is under way: it is finished first.
    if (!HasAuthority() || Stages.IsEmpty() || GetPhase() == EPhase::Countdown || GetPhase() == EPhase::Running) return;
    const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
    if (!Clock) return;
    const int32 Over = FMath::Min(Rally::Calendar::WeekendsOver(Clock->GetDay()), 255);
    while (Event < Over)
    {
        // The stages not run that weekend count as retirements at the maximum time.
        for (int32 Stage = 0; Stage < Stages.Num(); ++Stage)
        {
            if (Results.ContainsByPredicate([&](const FRallyStageResult& Result) { return Result.Stage == Stage && Result.Event == Event; })) continue;
            FRallyStageResult Missed;
            Missed.Stage = static_cast<int8>(Stage);
            Missed.Event = Event;
            Missed.bRetired = true;
            Missed.Attempt = static_cast<uint8>(200 + Event * Stages.Num() + Stage);
            Missed.TimeS = static_cast<float>(Rally::Stage::MaxSeconds(Stages[Stage].ParS));
            Results.Add(Missed);
        }
        UE_LOG(LogTemp, Display, TEXT("RALLY_CUP event %d: its weekend is over"), Event + 1);
        FinishEvent();
    }
}

void ARallyStageDirector::TickServer(float DeltaSeconds)
{
    using namespace Rally::Stage;
    const ARallyCarPawn* Car = FindCar();
    if (!Car || Stages.IsEmpty()) return;
    const FVector Location = Car->GetActorLocation();
    const double Now = ServerNow();
    const double SpeedMs = Car->GetVelocity().Size() / 100.0;
    switch (GetPhase())
    {
    case EPhase::Finished:
    case EPhase::Retired:
        // The result stays up for a while, then the next start can be taken.
        if (Now - Run.EndTime < 15.0) return;
        SetPhase(EPhase::Idle);
        Run.Stage = -1;
        [[fallthrough]];
    case EPhase::Idle:
    {
        // Stopped behind a start line, on the road, with someone at the wheel, for a moment: the marshal
        // starts the countdown.
        int32 Staged = INDEX_NONE;
        for (int32 I = 0; I < Stages.Num(); ++I)
        {
            double Side = 0.0;
            const double Past = PastLine(I, false, Location, Side);
            if (Past > -StartBoxM && Past < 0.5 && Side < Stages[I].WidthM * 0.5 + GateHalfWidthExtraM && SpeedMs < 1.0 && !Car->IsSeatFree(0)) Staged = I;
        }
        StagedFor = Staged != INDEX_NONE && Staged == StagedStage ? StagedFor + DeltaSeconds : 0.0;
        StagedStage = Staged;
        if (Staged == INDEX_NONE || StagedFor < StageSeconds) return;
        // Only on a rally weekend; then the scrutineer has a look.
        const FString OffDay = CalendarRefusal(GetWorld());
        const FString Refusal = OffDay.IsEmpty() ? Scrutineer(Car) : OffDay;
        if (!Refusal.IsEmpty())
        {
            if (FMath::IsNearlyEqual(StagedFor, StageSeconds, DeltaSeconds * 1.5))
                UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE %s refused at the start: %s"), *Stages[Staged].Id, *Refusal);
            return;
        }
        // The entry fee, paid at the line.
        ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
        if (Team && Team->GetKittySk() < Rally::Economy::StageEntrySk)
        {
            if (FMath::IsNearlyEqual(StagedFor, StageSeconds, DeltaSeconds * 1.5))
                UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE %s refused at the start: the kitty cannot pay the entry fee"), *Stages[Staged].Id);
            return;
        }
        if (Team) Team->AddMoney(-Rally::Economy::StageEntrySk, FString::Printf(TEXT("%s entry fee"), *Stages[Staged].Id));
        Run.Stage = static_cast<int8>(Staged);
        Run.CountdownStart = static_cast<float>(Now);
        Run.PenaltyS = 0.0f;
        // The time control at this start, if it is the one due.
        if (Staged == ControlStage)
        {
            const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
            const double Late = Clock ? (Clock->GetAbsoluteHours() - ControlDue) * 60.0 : 0.0;
            Run.PenaltyS += static_cast<float>(LatePenaltyS(Late));
            UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE %s time control: %s (%.0f s penalty)"), *Stages[Staged].Id,
                Late > 0.0 ? *FString::Printf(TEXT("%.0f min late"), FMath::CeilToDouble(Late)) : TEXT("in time"), LatePenaltyS(Late));
            ControlStage = -1;
        }
        ++Run.Attempt;
        bJumped = false;
        RunHint = 0;
        StagedFor = 0.0;
        SetPhase(EPhase::Countdown);
        UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE %s countdown (attempt %d)"), *Stages[Staged].Id, Run.Attempt);
        return;
    }
    case EPhase::Countdown:
    {
        double Side = 0.0;
        if (!bJumped && PastLine(Run.Stage, false, Location, Side) > 1.0)
        {
            bJumped = true;
            Run.PenaltyS += static_cast<float>(JumpStartPenaltyS);
            UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE %s jump start: +%.0f s"), *Stages[Run.Stage].Id, JumpStartPenaltyS);
            ForceNetUpdate();
        }
        if (Now < Run.CountdownStart + CountdownSeconds) return;
        Run.StartTime = Run.CountdownStart + CountdownSeconds;
        SetPhase(EPhase::Running);
        UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE %s go"), *Stages[Run.Stage].Id);
        return;
    }
    case EPhase::Recce:
    {
        // Driven through to the finish line, or given up (the car far from the stage); nothing recorded.
        const FStage& Stage = Stages[Run.Stage];
        const double Along = Progress(Run.Stage, Location, RunHint);
        double Side = 0.0;
        const double Past = PastLine(Run.Stage, true, Location, Side);
        const bool bDone = Along > Stage.LengthM * 0.8 && Past >= 0.0 && Side < Stage.WidthM * 0.5 + GateHalfWidthExtraM;
        const bool bGone = FVector::Dist2D(Location, PointAt(Run.Stage, Along)) > 15000.0;
        if (!bDone && !bGone) return;
        UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE %s recce %s"), *Stage.Id, bDone ? TEXT("done") : TEXT("abandoned"));
        Run.Stage = -1;
        SetPhase(EPhase::Idle);
        return;
    }
    case EPhase::Running:
    {
        const FStage& Stage = Stages[Run.Stage];
        const double Along = Progress(Run.Stage, Location, RunHint);
        double Side = 0.0;
        const double Past = PastLine(Run.Stage, true, Location, Side);
        const bool bFinished = Along > Stage.LengthM * 0.8 && Past >= 0.0 && Side < Stage.WidthM * 0.5 + GateHalfWidthExtraM;
        const bool bClosed = Now - Run.StartTime > MaxSeconds(Stage.ParS);
        if (!bFinished && !bClosed) return;
        FRallyStageResult Result;
        Result.Stage = Run.Stage;
        Result.Attempt = Run.Attempt;
        Result.PenaltyS = Run.PenaltyS;
        Result.bRetired = !bFinished;
        Result.Event = Event;
        Run.EndTime = static_cast<float>(bFinished ? Now : Run.StartTime + MaxSeconds(Stage.ParS));
        Result.TimeS = Run.EndTime - Run.StartTime + Run.PenaltyS;
        Results.Add(Result);
        SetPhase(bFinished ? EPhase::Finished : EPhase::Retired);
        const TArray<FStanding> Table = Standings(Result);
        int32 Position = 1;
        while (Position <= Table.Num() && Table[Position - 1].Crew != TEXT("You")) ++Position;
        UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE %s %s time=%.1f penalty=%.0f position=%d of %d par=%.1f"), *Stage.Id,
            bFinished ? TEXT("finished") : TEXT("closed, retired"), Result.TimeS, Result.PenaltyS, Position, Table.Num(), Stage.ParS);
        {
            int32 Counted = 0;
            const TArray<FStanding> Rally = Overall(Counted);
            int32 Overall = 1;
            while (Overall <= Rally.Num() && Rally[Overall - 1].Crew != TEXT("You")) ++Overall;
            UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE overall after %d stage(s): %d of %d"), Counted, Overall, Rally.Num());
        }
        // The road section to the next stage: its start is due within the hours allowed.
        if (Run.Stage + 1 < Stages.Num())
            if (const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld()))
            {
                ControlStage = static_cast<int8>(Run.Stage + 1);
                ControlDue = static_cast<float>(Clock->GetAbsoluteHours() + RoadSectionHours);
                UE_LOG(LogTemp, Display, TEXT("RALLY_STAGE time control %s due at %s"), *Stages[ControlStage].Id, *ARallyDayClock::HourText(ControlDue));
            }
        // Prize money for the result.
        if (ARallyTeamState* Team = ARallyTeamState::Get(GetWorld())) Team->OnFirstStageDone();
        if (ARallyTeamState* Team = ARallyTeamState::Get(GetWorld()))
            Team->AddMoney(Rally::Economy::PrizeSk(Position, !bFinished), FString::Printf(TEXT("%s, %d of %d"), *Stage.Id, Position, Table.Num()));
        // Every stage of the event run: the cup's points.
        {
            int32 Counted = 0;
            Overall(Counted);
            if (Counted == Stages.Num()) FinishEvent();
        }
        // A rally milestone: the team's state is saved.
        if (ARallyGameMode* Mode = GetWorld()->GetAuthGameMode<ARallyGameMode>())
            Mode->SaveTeam(FString::Printf(TEXT("%s %s"), *Stage.Id, bFinished ? TEXT("finished") : TEXT("retired")));
        return;
    }
    }
}
