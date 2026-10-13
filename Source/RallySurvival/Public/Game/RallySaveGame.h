#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Damage/RallyBodyDeformComponent.h"
#include "Damage/RallyDamageComponent.h"
#include "Damage/RallyDebrisActor.h"
#include "Damage/RallyEngineComponent.h"
#include "Damage/RallyPanelComponent.h"
#include "Game/RallyStageDirector.h"
#include "Game/RallyTeamState.h"
#include "RallySaveGame.generated.h"

// The team's state (docs/SAVING.md): the car and everything about it, the parts lying about, the stage
// results. Written at rally milestones (a stage finished) and loaded when the game starts.
UCLASS()
class RALLYSURVIVAL_API URallySaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 Version = 1;
    UPROPERTY() FString Why;
    UPROPERTY() FDateTime SavedAt;
    UPROPERTY() FString Map;
    UPROPERTY() FTransform CarTransform;
    UPROPERTY() TArray<FRallyPartCondition> Parts;
    UPROPERTY() TArray<FRallyPanelCondition> Panels;
    UPROPERTY() int8 JackCorner = -1;
    UPROPERTY() TArray<FRallyDent> Dents;
    UPROPERTY() FRallySavedEngine Engine;
    UPROPERTY() TArray<FRallySavedPart> LooseParts;
    UPROPERTY() TArray<FRallyStageResult> Results;
    UPROPERTY() uint8 StageAttempt = 0;
    UPROPERTY() int32 Day = 1;
    UPROPERTY() float Hour = 9.0f;
    UPROPERTY() int32 KittySk = 15000;
    UPROPERTY() TArray<FRallyOrder> Orders;
    UPROPERTY() int32 QuestStep = 0;
};
