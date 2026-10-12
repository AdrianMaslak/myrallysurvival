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
};
