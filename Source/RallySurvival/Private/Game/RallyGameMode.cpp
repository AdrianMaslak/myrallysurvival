#include "Game/RallyGameMode.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include "Vehicle/RallyCarPawn.h"

ARallyGameMode::ARallyGameMode()
{
    DefaultPawnClass = ARallyCarPawn::StaticClass();
}

void ARallyGameMode::StartPlay()
{
    Super::StartPlay();
    float QuitAfter = 0.0f;
    if (FParse::Value(FCommandLine::Get(), TEXT("RallyQuitAfter="), QuitAfter) && QuitAfter > 0.0f)
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [] { FGenericPlatformMisc::RequestExit(false, TEXT("RallyQuitAfter")); }, QuitAfter, false);
    }
}
