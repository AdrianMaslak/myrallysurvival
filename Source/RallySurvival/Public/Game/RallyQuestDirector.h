#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "RallyQuestDirector.generated.h"

// The first day's quests on the countryside map (docs/QUESTS.md): a chain of objectives, each checked from
// what the game already knows (where the car and players are, the engine, stage results, the kitty's
// orders, the clock). The server moves the chain on; everyone sees the tracker.
UCLASS()
class RALLYSURVIVAL_API ARallyQuestDirector : public AInfo
{
    GENERATED_BODY()

public:
    ARallyQuestDirector();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void Tick(float DeltaSeconds) override;
    static ARallyQuestDirector* Get(const UWorld* World);

    // The quest and objective now, for the tracker; empty once the chain is done.
    FString QuestTitle() const;
    FString ObjectiveText() const;
    int32 GetStep() const { return Step; }
    // Saving (docs/SAVING.md), server only.
    void SetStep(int32 InStep) { Step = InStep; ForceNetUpdate(); }

private:
    bool IsDone(int32 Objective) const;
    // Index into the chain of objectives (every quest's in order).
    UPROPERTY(Replicated) int32 Step = 0;
    // The day the chain's night began counting from.
    int32 StartDay = 1;
};
