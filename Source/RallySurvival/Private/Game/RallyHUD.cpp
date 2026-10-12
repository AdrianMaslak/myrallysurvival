#include "Game/RallyHUD.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "Player/RallyCharacter.h"

void ARallyHUD::DrawHUD()
{
    Super::DrawHUD();
    const ARallyCharacter* Character = Cast<ARallyCharacter>(GetOwningPawn());
    if (!Canvas || !Character || Character->GetSeatedCar())
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
