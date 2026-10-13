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
    // Server: when every player is asleep in the car, the night passes and the team is saved.
    void TrySleep();
    // Server: the car towed back to the service yard (the driver's H): money, hours, and retired from a stage.
    bool Tow(class ARallyCarPawn* Car);

private:
    // -RallySaveTest=write: puts the car in a known state and saves it; =check: loads it and compares.
    void RunSaveTest(bool bWrite);
    // -RallyDayTest: evening, headlamps on, everyone asleep in the car, the next morning (docs/DAYS.md).
    void RunDayTest(int32 Step);
    bool DayTestMorning = false;
    TWeakObjectPtr<class ARallyDebrisActor> DayTestBottle;
    TWeakObjectPtr<class ARallyDebrisActor> DayTestCarton;
    // -RallyShopTest: order from the catalogue, the kitty pays, the post brings it next morning, prize money.
    void RunShopTest(int32 Step);
};
