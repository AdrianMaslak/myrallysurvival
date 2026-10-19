#include "Game/RallyHUD.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Game/RallyStageDirector.h"
#include "Game/RallyDayClock.h"
#include "Game/CalendarRules.h"
#include "Game/RallyQuestDirector.h"
#include "Game/EconomyRules.h"
#include "Game/RallyTeamState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Player/RallyCharacter.h"
#include "Vehicle/RallyCarPawn.h"
#include "Damage/RallyEngineComponent.h"
#include "Misc/CommandLine.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

void ARallyHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
#if !UE_BUILD_SHIPPING
    TickStartShots();
#endif
    DrawStage();
    if (bCup) DrawCup();
    // The time, in a corner; asleep, the screen goes dark until everyone sleeps.
    if (const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld()))
    {
        const float Corner = FMath::Max(Canvas->ClipY / 1080.0f, 0.8f);
        DrawLine(Clock->GetRain() > 0.05 ? Clock->DescribeDate() + (Clock->GetRain() > 0.5 ? TEXT("  pouring") : TEXT("  rain")) : Clock->DescribeDate(),
            Canvas->ClipX - 150.0f * Corner, 20.0f, Corner, FLinearColor(0.9f, 0.9f, 0.9f));
        // The calendar, left of the date: this weekend's event, or when the next is.
        const ARallyStageDirector* Cup = ARallyStageDirector::Get(GetWorld());
        const int32 Event = Cup ? Cup->GetEvent() : 0;
        const FString When = Rally::Calendar::IsRallyDay(Clock->GetDay())
            ? FString::Printf(TEXT("rally weekend: cup event %d of %d"), Event % Rally::Calendar::CupEvents + 1, Rally::Calendar::CupEvents)
            : FString::Printf(TEXT("%s: %s"), Rally::Calendar::IsOffSeason(Clock->GetDay()) ? TEXT("off-season; the next cup starts") : TEXT("next rally"),
                *ARallyDayClock::DateText(Rally::Calendar::NextRallyDay(Clock->GetDay())));
        DrawLine(When, Canvas->ClipX - 420.0f * Corner, 20.0f, 0.85f * Corner, FLinearColor(0.85f, 0.85f, 0.7f));
    }
    const float Ui = FMath::Max(Canvas->ClipY / 1080.0f, 0.8f);
    // The quest tracker, bottom left.
    if (const ARallyQuestDirector* Quests = ARallyQuestDirector::Get(GetWorld()))
    {
        const FString Title = Quests->QuestTitle();
        if (!Title.IsEmpty())
        {
            UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
            const float Y = Canvas->ClipY - 90.0f * Ui;
            DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), 16.0f, Y - 8.0f, 760.0f * Ui, 62.0f * Ui);
            DrawText(Title, FLinearColor(1.0f, 0.8f, 0.25f), 26.0f, Y, Font, 1.1f * Ui, false);
            DrawText(Quests->ObjectiveText(), FLinearColor::White, 26.0f, Y + 26.0f * Ui, Font, Ui, false);
        }
    }
    if (const ARallyTeamState* Team = ARallyTeamState::Get(GetWorld()))
    {
        DrawLine(FString::Printf(TEXT("kitty %s%s"), UTF8_TO_TCHAR(Rally::Economy::Money(Team->GetKittySk()).c_str()),
            Team->GetOrders().IsEmpty() ? TEXT("") : *FString::Printf(TEXT("   %d on order"), Team->GetOrders().Num())),
            Canvas->ClipX - 130.0f * Ui, 52.0f, Ui, FLinearColor(0.9f, 0.9f, 0.9f));
        // The debt, and the latest letter for a while (Enter puts it away).
        DrawLine(FString::Printf(TEXT("owed %s, next payment day %d"), UTF8_TO_TCHAR(Rally::Economy::Money(Team->GetLoanSk()).c_str()), Team->GetNextPaymentDay()),
            Canvas->ClipX - 170.0f * Ui, Canvas->ClipY - 40.0f * Ui, 0.8f * Ui, FLinearColor(0.75f, 0.75f, 0.75f));
        // A letter stays until put away, out of the way while the car moves or a stage is counting down or
        // running (it would cover the road and the pace notes).
        const ARallyCarPawn* Driving = Cast<ARallyCarPawn>(GetOwningPawn());
        const ARallyStageDirector* Stages = ARallyStageDirector::Get(GetWorld());
        // Scripted drives and the panel test never press Enter, and photograph the car, not the post.
        static const bool bScripted = FParse::Param(FCommandLine::Get(), TEXT("RallyAutoDrive")) || FParse::Param(FCommandLine::Get(), TEXT("RallyPanelTest"))
            || FParse::Param(FCommandLine::Get(), TEXT("RallyDayTest")) || FParse::Param(FCommandLine::Get(), TEXT("RallyLeanTest"))
            || FParse::Param(FCommandLine::Get(), TEXT("RallyBootTest"));
        const bool bBusy = bScripted || (Driving && FMath::Abs(Driving->GetForwardSpeedKmh()) > 5.0f)
            || (Stages && (Stages->GetPhase() == ARallyStageDirector::EPhase::Countdown || Stages->GetPhase() == ARallyStageDirector::EPhase::Running));
        if (!Team->GetLetter().IsEmpty() && Team->GetLetterTime() != DismissedLetter && !bHelp && !bBusy)
            DrawLetter(Team->GetLetter(), Ui);
        // One's own pocket, and who last took from or put into the kitty.
        const ARallyCharacter* Reader = Cast<ARallyCharacter>(GetOwningPawn());
        if (const ARallyCarPawn* Driven = Cast<ARallyCarPawn>(GetOwningPawn())) Reader = Driven->GetDriverCharacter();
        if (Reader)
            DrawLine(FString::Printf(TEXT("pocket %s"), UTF8_TO_TCHAR(Rally::Economy::Money(Reader->GetPocketSk()).c_str())), Canvas->ClipX - 300.0f * Ui, 52.0f,
                Ui, FLinearColor(0.9f, 0.9f, 0.9f));
        const AGameStateBase* State = GetWorld()->GetGameState();
        if (State && State->GetServerWorldTimeSeconds() - Team->GetNoteTime() < 6.0 && !Team->GetNote().IsEmpty())
            DrawLine(Team->GetNote(), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.72f, Ui, FLinearColor(1.0f, 0.8f, 0.25f));
        // The catalogue: phoned through, paid from the kitty (one's own vices from the pocket), delivered next morning.
        if (Reader && Reader->IsCatalogueOpen())
        {
            const auto& Items = Rally::Economy::Catalogue();
            DrawLine(TEXT("Parts catalogue   mouse wheel: choose   Enter: order   K: close"), Canvas->ClipX * 0.5f, 140.0f * Ui, Ui, FLinearColor(1.0f, 0.8f, 0.25f));
            for (int32 I = 0; I < static_cast<int32>(Items.size()); ++I)
            {
                const bool bPicked = I == Reader->GetCatalogueIndex();
                DrawLine(FString::Printf(TEXT("%s%s   %s"), bPicked ? TEXT("> ") : TEXT(""), UTF8_TO_TCHAR(Items[I].Name),
                    UTF8_TO_TCHAR(Rally::Economy::Money(Items[I].PriceSk).c_str())), Canvas->ClipX * 0.5f, (180.0f + 30.0f * I) * Ui, 0.85f * Ui,
                    bPicked ? FLinearColor(1.0f, 0.8f, 0.25f) : (Items[I].PriceSk > Team->GetKittySk() ? FLinearColor(0.6f, 0.6f, 0.6f) : FLinearColor::White));
            }
        }
    }
    {
        const ARallyCharacter* Me = Cast<ARallyCharacter>(GetOwningPawn());
        if (const ARallyCarPawn* Driven = Cast<ARallyCarPawn>(GetOwningPawn())) Me = Driven->GetDriverCharacter();
        if (Me && Me->GetInjury() > 0.0)
            DrawLine(FString::Printf(TEXT("hurt %.0f%%: the first-aid kit is in the boot"), Me->GetInjury() * 100.0), Canvas->ClipX - 200.0f * Ui, 84.0f,
                0.9f * Ui, FLinearColor(1.0f, 0.4f, 0.35f));
        if (Me && (Me->GetDrunk() > 0.0 || Me->GetCalm() > 0.0))
            DrawLine(FString::Printf(TEXT("%s%s"), Me->GetDrunk() > 0.0 ? *FString::Printf(TEXT("drunk %.0f%%   "), Me->GetDrunk() * 100.0) : TEXT(""),
                Me->GetCalm() > 0.0 ? TEXT("calm") : TEXT("")), Canvas->ClipX - 120.0f * Ui, 144.0f, 0.9f * Ui, FLinearColor(0.9f, 0.85f, 0.6f));
        if (Me && Me->GetCold() > 0.1)
            DrawLine(FString::Printf(TEXT("cold %.0f%%: hands slow; warm up by the car's heater"), Me->GetCold() * 100.0), Canvas->ClipX - 220.0f * Ui, 114.0f,
                0.9f * Ui, FLinearColor(0.55f, 0.75f, 1.0f));
        if (Me && Me->GetWet() > 0.1)
            DrawLine(FString::Printf(TEXT("wet %.0f%%: the air feels colder; dry off in the car"), Me->GetWet() * 100.0), Canvas->ClipX - 220.0f * Ui, 174.0f,
                0.9f * Ui, FLinearColor(0.6f, 0.8f, 0.9f));
    }
    {
        const ARallyCharacter* Sleeper = Cast<ARallyCharacter>(GetOwningPawn());
        if (const ARallyCarPawn* Driven = Cast<ARallyCarPawn>(GetOwningPawn())) Sleeper = Driven->GetDriverCharacter();
        if (Sleeper && Sleeper->IsSleeping())
        {
            DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
            const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
            DrawLine(Sleeper->IsSleepingTillRally() && Clock
                    ? FString::Printf(TEXT("Asleep till the rally weekend (%s) when everyone sleeps so   Z: wake up"),
                        *ARallyDayClock::DateText(Rally::Calendar::WakeDay(Clock->GetDay(), Clock->GetHour(), ARallyDayClock::WakeHour, true)))
                    : FString(TEXT("Asleep: the night passes when everyone sleeps   Z: wake up")),
                Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f, 1.0f, FLinearColor::White);
        }
    }
    DrawPrompts(Ui);
    // The keys for the first steps, above the quest tracker; the full card on F1, over everything.
    const FString Hint = FirstStepsHint();
    if (!Hint.IsEmpty()) DrawLine(Hint, Canvas->ClipX * 0.5f, Canvas->ClipY - 196.0f * Ui, Ui, FLinearColor(0.95f, 0.95f, 0.85f));
    if (bHelp) DrawHelp();
    const ARallyCharacter* Character = Cast<ARallyCharacter>(GetOwningPawn());
    if (!Character || Character->GetSeatedCar())
    {
        return;
    }
    // Sized for the screen height, with a dark rim so it shows on pale paint and bright sky.
    const float Scale = FMath::Max(Canvas->ClipY / 1080.0f, 0.5f);
    const bool bTarget = Character->HasInteractionInView();
    const float Inner = (bTarget ? 6.0f : 4.0f) * Scale;
    const float Rim = Inner + 2.0f * Scale;
    const float X = Canvas->ClipX * 0.5f;
    const float Y = Canvas->ClipY * 0.5f;
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), X - Rim * 0.5f, Y - Rim * 0.5f, Rim, Rim);
    DrawRect(bTarget ? FLinearColor(1.0f, 0.85f, 0.2f, 0.95f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.9f), X - Inner * 0.5f, Y - Inner * 0.5f, Inner, Inner);
}

void ARallyHUD::DrawLine(const FString& Text, float CentreX, float Y, float Size, const FLinearColor& Color)
{
    UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
    if (!Font) return;
    float W = 0.0f, H = 0.0f;
    Canvas->StrLen(Font, Text, W, H);
    W *= Size;
    H *= Size;
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), CentreX - W * 0.5f - 8.0f, Y - 3.0f, W + 16.0f, H + 6.0f);
    DrawText(Text, Color, CentreX - W * 0.5f, Y, Font, Size, false);
}

void ARallyHUD::DrawStage()
{
    using EPhase = ARallyStageDirector::EPhase;
    const ARallyStageDirector* Director = ARallyStageDirector::Get(GetWorld());
    if (!Director || Director->NumStages() == 0) return;
    // The team car: the one driven here, the one this player rides in, or the only one.
    const APawn* Pawn = GetOwningPawn();
    const ARallyCarPawn* Car = Cast<ARallyCarPawn>(Pawn);
    const ARallyCharacter* Character = Cast<ARallyCharacter>(Pawn);
    if (!Car && Character) Car = Character->GetSeatedCar();
    const bool bInCar = Car != nullptr;
    if (!Car) { TActorIterator<ARallyCarPawn> It(GetWorld()); Car = It ? *It : nullptr; }
    if (!Car) return;
    // Readable on small windows too.
    const float Scale = FMath::Max(Canvas->ClipY / 1080.0f, 0.8f);
    const float X = Canvas->ClipX * 0.5f;
    const FRallyStageRun& Run = Director->GetRun();
    const EPhase Phase = Director->GetPhase();
    const FLinearColor White(1.0f, 1.0f, 1.0f), Amber(1.0f, 0.8f, 0.25f), Red(1.0f, 0.35f, 0.3f);
    if (Phase == EPhase::Idle)
    {
        // The road section's deadline.
        if (Director->GetControlStage() >= 0 && Director->GetControlStage() < Director->NumStages())
            if (const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld()))
            {
                const double Left = (Director->GetControlDue() - Clock->GetAbsoluteHours()) * 60.0;
                DrawLine(FString::Printf(TEXT("time control %s: check in by %s (%s)"), *Director->GetStage(Director->GetControlStage()).Id,
                    *ARallyDayClock::HourText(Director->GetControlDue()),
                    Left >= 0.0 ? *FString::Printf(TEXT("%.0f min left"), Left) : *FString::Printf(TEXT("%.0f min late, 10 s a minute"), -Left)),
                    X, 40.0f * Scale, 1.0f * Scale, Left >= 0.0 ? White : Red);
            }
        // Near a start: what the stage is and how to begin it.
        for (int32 I = 0; I < Director->NumStages(); ++I)
        {
            double Side = 0.0;
            const double Past = Director->PastLine(I, false, Car->GetActorLocation(), Side);
            if (Past < -60.0 || Past > 5.0 || Side > 25.0) continue;
            const ARallyStageDirector::FStage& Stage = Director->GetStage(I);
            DrawLine(FString::Printf(TEXT("%s %s   %.1f km %s   stop at the start line to be counted down   N: recce"), *Stage.Id, *Stage.Name,
                Stage.LengthM / 1000.0, *Stage.Surface.ToLower()), X, 90.0f * Scale, 1.1f * Scale, White);
            const FString OffDay = ARallyStageDirector::CalendarRefusal(GetWorld());
            const FString Refusal = ARallyStageDirector::Scrutineer(Car);
            const ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
            if (!OffDay.IsEmpty()) DrawLine(FString::Printf(TEXT("Marshal: %s"), *OffDay), X, 130.0f * Scale, 1.0f * Scale, Red);
            else if (!Refusal.IsEmpty()) DrawLine(FString::Printf(TEXT("Scrutineer: %s"), *Refusal), X, 130.0f * Scale, 1.0f * Scale, Red);
            else if (Team && Team->GetKittySk() < Rally::Economy::StageEntrySk)
                DrawLine(FString::Printf(TEXT("Entry fee %s: the kitty cannot pay it"), UTF8_TO_TCHAR(Rally::Economy::Money(Rally::Economy::StageEntrySk).c_str())),
                    X, 130.0f * Scale, 1.0f * Scale, Red);
            else DrawLine(FString::Printf(TEXT("entry fee %s"), UTF8_TO_TCHAR(Rally::Economy::Money(Rally::Economy::StageEntrySk).c_str())),
                X, 130.0f * Scale, 0.9f * Scale, White);
            break;
        }
        return;
    }
    if (Run.Stage < 0 || Run.Stage >= Director->NumStages()) return;
    const ARallyStageDirector::FStage& Stage = Director->GetStage(Run.Stage);
    if (Phase == EPhase::Countdown)
    {
        const int32 Left = Director->GetCountdown();
        DrawLine(FString::Printf(TEXT("%s %s"), *Stage.Id, *Stage.Name), X, 90.0f * Scale, 1.1f * Scale, White);
        DrawLine(Left > 0 ? FString::FromInt(Left) : FString(TEXT("GO")), X, Canvas->ClipY * 0.3f, 4.0f * Scale, Left > 1 ? Red : Amber);
        if (Run.PenaltyS > 0.0f) DrawLine(TEXT("jump start: +10 s"), X, Canvas->ClipY * 0.3f + 90.0f * Scale, 1.2f * Scale, Red);
        return;
    }
    const FString Clock = UTF8_TO_TCHAR(Rally::Stage::Clock(Director->GetClock()).c_str());
    const FString Penalty = Run.PenaltyS > 0.0f ? FString::Printf(TEXT("   +%.0f s"), Run.PenaltyS) : FString();
    if (Phase == EPhase::Running || Phase == EPhase::Recce)
    {
        const bool bRecce = Phase == EPhase::Recce;
        if (bRecce)
        {
            const bool bFast = FMath::Abs(Car->GetForwardSpeedKmh()) > Rally::Stage::RecceLimitKmh;
            DrawLine(FString::Printf(TEXT("%s recce: %.0f km/h at most, learn the notes"), *Stage.Id, Rally::Stage::RecceLimitKmh), X, 40.0f * Scale, 1.2f * Scale,
                bFast ? Red : White);
        }
        else
        {
            if (Director->GetClock() < 1.0) DrawLine(TEXT("GO"), X, Canvas->ClipY * 0.3f, 4.0f * Scale, Amber);
            DrawLine(FString::Printf(TEXT("%s   %s%s"), *Stage.Id, *Clock, *Penalty), X, 40.0f * Scale, 1.4f * Scale, White);
        }
        if (!bInCar) return;
        // Pace notes. With a co-driver aboard they are theirs, a page of the road book to read aloud; the
        // driver sees them only when alone in the car.
        const bool bCoDriver = Character && Character->GetSeatIndex() == 1;
        const bool bSolo = Car->IsSeatFree(1);
        if (!bCoDriver && !bSolo && !bRecce) return;
        const double Along = Director->Progress(Run.Stage, Car->GetActorLocation(), PaceHint);
        const int32 Lines = bCoDriver ? 4 : 2;
        const float Column = bCoDriver ? Canvas->ClipX * 0.78f : X;
        if (bCoDriver) DrawLine(FString::Printf(TEXT("road book   %s   %.2f km"), *Stage.Id, Along / 1000.0), Column, 80.0f * Scale, 0.9f * Scale, White);
        const float Top = bCoDriver ? 115.0f : 80.0f;
        int32 Shown = 0;
        for (const Rally::Stage::FCall& Call : Stage.Calls)
        {
            const double Ahead = Call.AtM - Along;
            if (Ahead < -5.0) continue;
            if (Ahead > (bCoDriver ? 400.0 : 250.0) || Shown >= Lines) break;
            const int32 Rounded = FMath::Max(10, FMath::RoundToInt(Ahead / 10.0) * 10);
            DrawLine(FString::Printf(TEXT("%d   %s"), Rounded, UTF8_TO_TCHAR(Rally::Stage::CallText(Call).c_str())), Column,
                (Top + 42.0f * Shown) * Scale, (Shown == 0 ? 1.3f : 1.0f) * Scale, Shown == 0 ? Amber : White);
            ++Shown;
        }
        if (Shown == 0 && Stage.LengthM - Along < 300.0)
            DrawLine(FString::Printf(TEXT("%d   flat to the finish"), FMath::Max(10, FMath::RoundToInt((Stage.LengthM - Along) / 10.0) * 10)),
                Column, Top * Scale, 1.3f * Scale, Amber);
        return;
    }
    // Finished or retired: the stage's results with the rivals.
    const TArray<FRallyStageResult>& Results = Director->GetResults();
    if (Results.IsEmpty()) return;
    const FRallyStageResult& Result = Results.Last();
    DrawLine(Phase == EPhase::Finished ? FString::Printf(TEXT("%s finished   %s%s"), *Stage.Id, *Clock, *Penalty)
        : FString::Printf(TEXT("%s closed: retired, %s"), *Stage.Id, *Clock), X, 40.0f * Scale, 1.4f * Scale, Phase == EPhase::Finished ? Amber : Red);
    const TArray<ARallyStageDirector::FStanding> Table = Director->Standings(Result);
    for (int32 I = 0; I < Table.Num(); ++I)
    {
        const ARallyStageDirector::FStanding& Row = Table[I];
        const FString Time = Row.bRetired ? FString(TEXT("retired")) : FString(UTF8_TO_TCHAR(Rally::Stage::Clock(Row.TimeS).c_str()));
        DrawLine(FString::Printf(TEXT("%d.  %s   %s"), I + 1, *Row.Crew, *Time), X, (100.0f + 34.0f * I) * Scale, 0.9f * Scale,
            Row.Crew == TEXT("You") ? Amber : White);
    }
    // The rally so far, beside it.
    int32 Counted = 0;
    const TArray<ARallyStageDirector::FStanding> Rally = Director->Overall(Counted);
    if (Counted < 2) return;
    const float Column = Canvas->ClipX * 0.82f;
    // Below the clock, kitty and body lines in the top right.
    DrawLine(FString::Printf(TEXT("overall after %d stages"), Counted), Column, 180.0f * Scale, 0.85f * Scale, White);
    for (int32 I = 0; I < Rally.Num(); ++I)
        DrawLine(FString::Printf(TEXT("%d.  %s   %s"), I + 1, *Rally[I].Crew, UTF8_TO_TCHAR(Rally::Stage::Clock(Rally[I].TimeS).c_str())), Column,
            (212.0f + 30.0f * I) * Scale, 0.8f * Scale, Rally[I].Crew == TEXT("You") ? Amber : White);
}

FString ARallyHUD::FirstStepsHint() const
{
    // Shakedown: find the car, start it, drive to the first stage (RallyQuestDirector steps 0-2).
    const ARallyQuestDirector* Quests = ARallyQuestDirector::Get(GetWorld());
    if (!Quests || Quests->GetStep() > 2 || bHelp) return FString();
    const ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwningPawn());
    const ARallyCharacter* Me = Car ? Car->GetDriverCharacter() : Cast<ARallyCharacter>(GetOwningPawn());
    if (Car)
    {
        if (!Car->GetEngine()->IsRunning()) return TEXT("I: start the engine   F1: all the controls");
        return TEXT("W throttle   S brake   A D steer   Space handbrake   Left Shift / Ctrl: gear up / down   Q clutch   F1: all the controls");
    }
    if (Me && Me->GetSeatedCar()) return TEXT("E: get out   F1: all the controls");
    return TEXT("WASD walk   mouse look   E: use, pick up, get in   F1: all the controls");
}

void ARallyHUD::DrawPrompts(float Ui)
{
    UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
    const ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwningPawn());
    const ARallyCharacter* Me = Car ? Car->GetDriverCharacter() : Cast<ARallyCharacter>(GetOwningPawn());
    if (Car)
    {
        // The driver's gauges in the middle, above the quest tracker; what is damaged under them, smaller.
        if (!Car->GetReadoutText().IsEmpty())
            DrawLine(Car->GetReadoutText(), Canvas->ClipX * 0.5f, Canvas->ClipY - 164.0f * Ui, Ui, FLinearColor::White);
        if (!Car->GetDamageText().IsEmpty())
            DrawLine(Car->GetDamageText(), Canvas->ClipX * 0.5f, Canvas->ClipY - 136.0f * Ui, 0.8f * Ui, FLinearColor(1.0f, 0.7f, 0.55f));
        return;
    }
    if (!Me) return;
    // Under the crosshair: what can be done here (yellow), or what it is (white).
    const FString& Prompt = Me->GetPromptText();
    const FString& Aimed = Me->GetAimedText();
    if (!Prompt.IsEmpty() || !Aimed.IsEmpty())
        DrawLine(Prompt.IsEmpty() ? Aimed : Prompt, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f + 34.0f * Ui, Ui,
            Prompt.IsEmpty() ? FLinearColor::White : FLinearColor(1.0f, 0.85f, 0.3f));
    // The tool in hand, in a corner of the eye.
    if (!Me->GetSeatedCar())
    {
        float W = 0.0f, H = 0.0f;
        const FString Tool = Me->GetToolText();
        GetTextSize(Tool, W, H, Font, 0.8f * Ui);
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), 16.0f, 16.0f, W + 16.0f, H + 8.0f);
        DrawText(Tool, FLinearColor(0.85f, 0.85f, 0.85f), 24.0f, 20.0f, Font, 0.8f * Ui, false);
    }
}

void ARallyHUD::DrawHelp()
{
    static const TCHAR* const Lines[] = {
        TEXT("ON FOOT"),
        TEXT("WASD move   Shift run   Space jump   Ctrl or C crouch   Q lean (with A, D, S to the side or back)"),
        TEXT("E use, pick up, fit a carried part, get in   G drop   F open or shut   B bodge or strap"),
        TEXT("Aim at a bolt: hold E to do it up, hold R to undo it (with the right tool); all out, R lifts the part off"),
        TEXT("1-8 or mouse wheel: tool (hands, 10/13/17/19 mm spanners, screwdriver, pliers, hammer)"),
        TEXT("T / Shift+T adjust (damper, height collar, turbo boost)   K parts catalogue (wheel, Enter to order)"),
        TEXT("In the service yard: P on a damaged part pays the workshop to fix it (costs money and hours); M / Shift+M kitty cash"),
        TEXT("Carrying: E fits it, or puts it in the open boot, or treats with the first-aid kit; B swig or smoke"),
        TEXT("IN THE CAR"),
        TEXT("W throttle   S brake   A D steer   Space handbrake   Shift / Ctrl gear up / down (R below N)   Q clutch   I ignition   L headlamps"),
        TEXT("F door   E get out   C camera   Z sleep (everyone asleep: the night passes, the game saves)   Shift+Z sleep till the rally weekend"),
        TEXT("H call a tow back to the service yard (2 000 Sk and 2 hours; retires the car from a stage)   [ ] brake bias"),
        TEXT("RALLY"),
        TEXT("Stop behind a stage's start line: the scrutineer checks the car, then a 5 s countdown (jump start +10 s); N: recce"),
        TEXT("The co-driver reads the road book aloud; a driver alone sees the calls   J: the cup's standings   F1: this card"),
    };
    // Centred on the screen whatever its size, over everything else.
    const float Ui = FMath::Min(FMath::Max(Canvas->ClipY / 1080.0f, 0.8f), Canvas->ClipY / ((UE_ARRAY_COUNT(Lines) * 26.0f + 60.0f)));
    const float Height = (UE_ARRAY_COUNT(Lines) * 26.0f + 20.0f) * Ui;
    const float Top = (Canvas->ClipY - Height) * 0.5f + 12.0f * Ui;
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), Canvas->ClipX * 0.06f, Top - 12.0f * Ui, Canvas->ClipX * 0.88f, Height);
    UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Lines); ++I)
    {
        const bool bHeading = FCString::Strlen(Lines[I]) < 12;
        DrawText(Lines[I], bHeading ? FLinearColor(1.0f, 0.8f, 0.25f) : FLinearColor::White, Canvas->ClipX * 0.1f, Top + I * 26.0f * Ui, Font, Ui, false);
    }
}

void ARallyHUD::DrawLetter(const FString& Text, float Ui)
{
    // A sheet of paper in the middle: the text wrapped to about 70 characters a line.
    TArray<FString> Lines;
    FString Line;
    TArray<FString> Words;
    Text.ParseIntoArray(Words, TEXT(" "));
    for (const FString& Word : Words)
    {
        if (Line.Len() + Word.Len() > 70) { Lines.Add(Line); Line.Reset(); }
        Line += (Line.IsEmpty() ? TEXT("") : TEXT(" ")) + Word;
    }
    if (!Line.IsEmpty()) Lines.Add(Line);
    const float Width = 760.0f * Ui, Height = (Lines.Num() * 26.0f + 54.0f) * Ui;
    const float X = (Canvas->ClipX - Width) * 0.5f, Y = Canvas->ClipY * 0.2f;
    DrawRect(FLinearColor(0.93f, 0.9f, 0.82f, 0.95f), X, Y, Width, Height);
    UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
    for (int32 I = 0; I < Lines.Num(); ++I)
        DrawText(Lines[I], FLinearColor(0.12f, 0.1f, 0.08f), X + 20.0f * Ui, Y + (16.0f + 26.0f * I) * Ui, Font, Ui, false);
    DrawText(TEXT("Enter: put the letter away"), FLinearColor(0.35f, 0.3f, 0.25f), X + 20.0f * Ui, Y + Height - 30.0f * Ui, Font, 0.85f * Ui, false);
}

void ARallyHUD::TickStartShots()
{
    static const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("RallyStartShots"));
    static const float Times[] = { 1.5f, 6.0f, 15.0f, 26.0f, 30.0f, 45.0f };
    if (!bShots || !GetWorld()) return;
    const float Now = GetWorld()->GetTimeSeconds();
    // -RallyStartRain: a downpour from the start (the shots and the recording in the rain).
    static const bool bRain = FParse::Param(FCommandLine::Get(), TEXT("RallyStartRain"));
    if (bRain && Now > 0.5f)
        if (ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld())) if (Clock->GetRain() < 1.0) Clock->SetRainOverride(1.0f);
    // -RallyRecordAudio: ten seconds of what the player hears (RallyStart_audio.wav).
    static const bool bRecord = FParse::Param(FCommandLine::Get(), TEXT("RallyRecordAudio"));
    if (bRecord && StartRecording == 0 && Now > 3.0f) { UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 15.0f); StartRecording = 1; }
    if (bRecord && StartRecording == 1 && Now > 13.0f)
    {
        UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, TEXT("RallyStart_audio"), FString());
        StartRecording = 2;
    }
    if (StartShot < static_cast<int32>(UE_ARRAY_COUNT(Times)) && Now >= Times[StartShot])
    {
        FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("RallyStart_%02.0fs.png"), Times[StartShot]), true, false);
        UE_LOG(LogTemp, Display, TEXT("RALLY_START shot %d at %.1f s"), StartShot, Now);
        ++StartShot;
    }
    else if (StartShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) && Now > 46.0f)
    {
        // And the controls card, as F1 brings it up.
        bHelp = true;
        ++StartShot;
    }
    else if (StartShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) + 1 && Now > 47.0f)
    {
        FScreenshotRequest::RequestScreenshot(TEXT("RallyStart_help.png"), true, false);
        UE_LOG(LogTemp, Display, TEXT("RALLY_START shot help at %.1f s"), Now);
        ++StartShot;
    }
    else if (StartShot > static_cast<int32>(UE_ARRAY_COUNT(Times)) + 1 && Now > 48.5f)
    {
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyStartShots"));
    }
}

void ARallyHUD::DismissLetter()
{
    if (const ARallyTeamState* Team = ARallyTeamState::Get(GetWorld())) DismissedLetter = Team->GetLetterTime();
}

void ARallyHUD::DrawCup()
{
    const ARallyStageDirector* Director = ARallyStageDirector::Get(GetWorld());
    if (!Director) return;
    const float Ui = FMath::Max(Canvas->ClipY / 1080.0f, 0.8f);
    const float X = Canvas->ClipX * 0.5f;
    // The table shown is the latest event's season (last season's, over the winter).
    const int32 Done = Director->GetEvent(), Cups = Rally::Calendar::CupEvents;
    const int32 Year = Rally::Calendar::StartYear + (Done > 0 ? (Done - 1) / Cups : 0);
    DrawLine(FString::Printf(TEXT("Regional cup %d after %d of %d events   J: close"), Year, Done > 0 ? (Done - 1) % Cups + 1 : 0, Cups), X, 160.0f * Ui, Ui,
        FLinearColor(1.0f, 0.8f, 0.25f));
    if (Director->GetCup().IsEmpty())
        DrawLine(TEXT("no points yet: run all three stages of an event"), X, 200.0f * Ui, 0.9f * Ui, FLinearColor::White);
    for (int32 I = 0; I < Director->GetCup().Num(); ++I)
    {
        const FRallyCupEntry& Entry = Director->GetCup()[I];
        DrawLine(FString::Printf(TEXT("%d.  %s   %d"), I + 1, *Entry.Crew, Entry.Points), X, (200.0f + 30.0f * I) * Ui, 0.9f * Ui,
            Entry.Crew == TEXT("You") ? FLinearColor(1.0f, 0.8f, 0.25f) : FLinearColor::White);
    }
}
