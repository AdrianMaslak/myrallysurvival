#pragma once

#include "CoreMinimal.h"

class UGameInstance;

// The start screen of a normal (not scripted) start, and after a session ends or fails: continue, a new game,
// joining a friend through Steam, quit (docs/STEAM-SETUP.md).
namespace RallyMainMenu
{
    void Show(UGameInstance* GameInstance, const FString& Reason);
    void Hide(UGameInstance* GameInstance);
    bool IsShown();
    // Renames the team's save with the time (never deletes it); false if there was none.
    bool MoveSaveAside();
}
