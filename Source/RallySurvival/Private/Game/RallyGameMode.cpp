#include "Game/RallyGameMode.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include "Player/RallyCharacter.h"
#include "Vehicle/RallyCarPawn.h"

ARallyGameMode::ARallyGameMode()
{
    DefaultPawnClass = ARallyCharacter::StaticClass();
}

void ARallyGameMode::Logout(AController* Exiting)
{
    // The default would destroy the leaving player's pawn, which for a driver is the team car.
    if (ARallyCarPawn* Car = Exiting ? Cast<ARallyCarPawn>(Exiting->GetPawn()) : nullptr)
    {
        Car->ServerDriverLeaving();
    }
    Super::Logout(Exiting);
}

void ARallyGameMode::StartPlay()
{
    Super::StartPlay();
    // One team car on the gravel road, 7 m ahead of the player starts.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    GetWorld()->SpawnActor<ARallyCarPawn>(ARallyCarPawn::StaticClass(), FVector(1000.0, 0.0, 120.0), FRotator::ZeroRotator, Params);
    float QuitAfter = 0.0f;
    if (FParse::Value(FCommandLine::Get(), TEXT("RallyQuitAfter="), QuitAfter) && QuitAfter > 0.0f)
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [] { FGenericPlatformMisc::RequestExit(false, TEXT("RallyQuitAfter")); }, QuitAfter, false);
    }
}
