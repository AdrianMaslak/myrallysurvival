#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RallyGameMode.generated.h"

// First-playable mode: players start on foot next to one team car.
UCLASS()
class RALLYSURVIVAL_API ARallyGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ARallyGameMode();

    // -RallyQuitAfter=<seconds> ends unattended host sessions (two-player tests).
    virtual void StartPlay() override;
    virtual void Logout(AController* Exiting) override;

    // The team's state (docs/SAVING.md): saved at rally milestones, loaded at the start. The slot is
    // "RallyTeam", or -RallySaveSlot=. Automated runs (-unattended) neither load nor save, except the
    // save test.
    bool SaveTeam(const FString& Why);
    bool LoadTeam();
    static FString SlotName();

private:
    // -RallySaveTest=write: puts the car in a known state and saves it; =check: loads it and compares.
    void RunSaveTest(bool bWrite);
};
