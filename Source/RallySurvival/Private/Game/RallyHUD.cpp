#include "Game/RallyHUD.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Game/RallyStageDirector.h"
#include "Game/RallyDayClock.h"
#include "Game/EconomyRules.h"
#include "Game/RallyTeamState.h"
#include "GameFramework/PlayerController.h"
#include "Player/RallyCharacter.h"
#include "Vehicle/RallyCarPawn.h"

void ARallyHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    DrawStage();
    // The first 25 seconds of play show the controls once; F1 brings them back.
    if (!bHelpShownOnce && GetWorld()->GetTimeSeconds() > 2.0 && GetWorld()->GetTimeSeconds() < 27.0) DrawHelp();
    else if (bHelp) DrawHelp();
    // The time, in a corner; asleep, the screen goes dark until everyone sleeps.
    if (const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld()))
        DrawLine(Clock->Describe(), Canvas->ClipX - 110.0f * FMath::Max(Canvas->ClipY / 1080.0f, 0.8f), 20.0f, FMath::Max(Canvas->ClipY / 1080.0f, 0.8f),
            FLinearColor(0.9f, 0.9f, 0.9f));
    const float Ui = FMath::Max(Canvas->ClipY / 1080.0f, 0.8f);
    if (const ARallyTeamState* Team = ARallyTeamState::Get(GetWorld()))
    {
        DrawLine(FString::Printf(TEXT("kitty %s%s"), UTF8_TO_TCHAR(Rally::Economy::Money(Team->GetKittySk()).c_str()),
            Team->GetOrders().IsEmpty() ? TEXT("") : *FString::Printf(TEXT("   %d on order"), Team->GetOrders().Num())),
            Canvas->ClipX - 130.0f * Ui, 52.0f, Ui, FLinearColor(0.9f, 0.9f, 0.9f));
        // The catalogue: phoned through, paid from the kitty, delivered next morning.
        const ARallyCharacter* Reader = Cast<ARallyCharacter>(GetOwningPawn());
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
        const ARallyCharacter* Sleeper = Cast<ARallyCharacter>(GetOwningPawn());
        if (const ARallyCarPawn* Driven = Cast<ARallyCarPawn>(GetOwningPawn())) Sleeper = Driven->GetDriverCharacter();
        if (Sleeper && Sleeper->IsSleeping())
        {
            DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
            DrawLine(TEXT("Asleep: the night passes when everyone sleeps   Z: wake up"), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f, 1.0f, FLinearColor::White);
        }
    }
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
        // Near a start: what the stage is and how to begin it.
        for (int32 I = 0; I < Director->NumStages(); ++I)
        {
            double Side = 0.0;
            const double Past = Director->PastLine(I, false, Car->GetActorLocation(), Side);
            if (Past < -60.0 || Past > 5.0 || Side > 25.0) continue;
            const ARallyStageDirector::FStage& Stage = Director->GetStage(I);
            DrawLine(FString::Printf(TEXT("%s %s   %.1f km %s   stop at the start line to be counted down"), *Stage.Id, *Stage.Name,
                Stage.LengthM / 1000.0, *Stage.Surface.ToLower()), X, 90.0f * Scale, 1.1f * Scale, White);
            const FString Refusal = ARallyStageDirector::Scrutineer(Car);
            const ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
            if (!Refusal.IsEmpty()) DrawLine(FString::Printf(TEXT("Scrutineer: %s"), *Refusal), X, 130.0f * Scale, 1.0f * Scale, Red);
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
    if (Phase == EPhase::Running)
    {
        if (Director->GetClock() < 1.0) DrawLine(TEXT("GO"), X, Canvas->ClipY * 0.3f, 4.0f * Scale, Amber);
        DrawLine(FString::Printf(TEXT("%s   %s%s"), *Stage.Id, *Clock, *Penalty), X, 40.0f * Scale, 1.4f * Scale, White);
        if (!bInCar) return;
        // Pace notes. With a co-driver aboard they are theirs, a page of the road book to read aloud; the
        // driver sees them only when alone in the car.
        const bool bCoDriver = Character && Character->GetSeatIndex() == 1;
        const bool bSolo = Car->IsSeatFree(1);
        if (!bCoDriver && !bSolo) return;
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
        TEXT("In the service yard: P on a damaged part pays the workshop to fix it (costs money and hours)"),
        TEXT("IN THE CAR"),
        TEXT("W throttle   S brake (reverse when stopped)   A D steer   Space handbrake   I ignition   L headlamps"),
        TEXT("F door   E get out   C camera   Z sleep (everyone asleep: the night passes, the game saves)"),
        TEXT("RALLY"),
        TEXT("Stop behind a stage's start line: the scrutineer checks the car, then a 5 s countdown (jump start +10 s)"),
        TEXT("The co-driver reads the road book aloud; a driver alone sees the calls   F1: this card"),
    };
    const float Ui = FMath::Max(Canvas->ClipY / 1080.0f, 0.8f);
    const float Top = Canvas->ClipY * 0.52f;
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), Canvas->ClipX * 0.08f, Top - 12.0f * Ui, Canvas->ClipX * 0.84f, (UE_ARRAY_COUNT(Lines) * 26.0f + 20.0f) * Ui);
    UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Lines); ++I)
    {
        const bool bHeading = FCString::Strlen(Lines[I]) < 12;
        DrawText(Lines[I], bHeading ? FLinearColor(1.0f, 0.8f, 0.25f) : FLinearColor::White, Canvas->ClipX * 0.1f, Top + I * 26.0f * Ui, Font, Ui, false);
    }
}
