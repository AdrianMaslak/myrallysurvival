#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RallyHUD.generated.h"

// A dot in the middle of the screen while on foot, so players see what they are pointing at; it turns
// yellow and grows when what is in view can be used (the prompt line says how).
UCLASS()
class RALLYSURVIVAL_API ARallyHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    // The controls card: shown for the first moments of play, then on F1.
    void ToggleHelp() { bHelp = !bHelp; bHelpShownOnce = true; }

private:
    // Special stages (ARallyStageDirector): what the stage is near its start, the countdown, the clock and
    // pace notes while running, the results at the end.
    void DrawStage();
    void DrawLine(const FString& Text, float CentreX, float Y, float Size, const FLinearColor& Color);
    int32 PaceHint = 0;
    void DrawHelp();
    bool bHelp = false;
    bool bHelpShownOnce = false;
};
