#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RallyGameMode.generated.h"

// First-playable mode: every player spawns into their own car until seats exist.
UCLASS()
class RALLYSURVIVAL_API ARallyGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ARallyGameMode();

    // -RallyQuitAfter=<seconds> ends unattended host sessions (two-player tests).
    virtual void StartPlay() override;
};
