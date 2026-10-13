#include "Game/RallyHUD.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Game/RallyStageDirector.h"
#include "GameFramework/PlayerController.h"
#include "Player/RallyCharacter.h"
#include "Vehicle/RallyCarPawn.h"

void ARallyHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    DrawStage();
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
        // Pace notes: the next calls and how far to each, as the co-driver reads them from the notes.
        const double Along = Director->Progress(Run.Stage, Car->GetActorLocation(), PaceHint);
        int32 Shown = 0;
        for (const Rally::Stage::FCall& Call : Stage.Calls)
        {
            const double Ahead = Call.AtM - Along;
            if (Ahead < -5.0) continue;
            if (Ahead > 250.0 || Shown >= 2) break;
            const int32 Rounded = FMath::Max(10, FMath::RoundToInt(Ahead / 10.0) * 10);
            DrawLine(FString::Printf(TEXT("%d   %s"), Rounded, UTF8_TO_TCHAR(Rally::Stage::CallText(Call).c_str())), X,
                (80.0f + 42.0f * Shown) * Scale, (Shown == 0 ? 1.3f : 1.0f) * Scale, Shown == 0 ? Amber : White);
            ++Shown;
        }
        if (Shown == 0 && Stage.LengthM - Along < 300.0)
            DrawLine(FString::Printf(TEXT("%d   flat to the finish"), FMath::Max(10, FMath::RoundToInt((Stage.LengthM - Along) / 10.0) * 10)),
                X, 80.0f * Scale, 1.3f * Scale, Amber);
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
