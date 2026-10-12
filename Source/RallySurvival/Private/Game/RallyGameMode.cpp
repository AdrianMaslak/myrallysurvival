#include "Game/RallyGameMode.h"
#include "Game/RallyHUD.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include "Player/RallyCharacter.h"
#include "Vehicle/RallyCarPawn.h"

ARallyGameMode::ARallyGameMode()
{
    DefaultPawnClass = ARallyCharacter::StaticClass();
    HUDClass = ARallyHUD::StaticClass();
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
    // One team car on the gravel road, 7 m ahead of the player starts. The crash test puts it in line
    // with the impact barrier (82 m, -14 m), 30 m short of it.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    const bool bCrashTest = FParse::Param(FCommandLine::Get(), TEXT("RallyCrashTest"));
    FVector CarStart = bCrashTest ? FVector(5000.0, -1400.0, 120.0) : FVector(1000.0, 0.0, 120.0);
    FRotator CarRotation = FRotator::ZeroRotator;
    // -RallyCarStart=X,Y,Z,Yaw (cm, degrees) puts it elsewhere, for tests on other maps.
    FString Start;
    if (FParse::Value(FCommandLine::Get(), TEXT("RallyCarStart="), Start, false))
    {
        TArray<FString> Values;
        Start.ParseIntoArray(Values, TEXT(","));
        if (Values.Num() >= 3)
        {
            CarStart = FVector(FCString::Atod(*Values[0]), FCString::Atod(*Values[1]), FCString::Atod(*Values[2]));
            CarRotation.Yaw = Values.Num() >= 4 ? FCString::Atod(*Values[3]) : 0.0;
        }
    }
    GetWorld()->SpawnActor<ARallyCarPawn>(ARallyCarPawn::StaticClass(), CarStart, CarRotation, Params);
    float QuitAfter = 0.0f;
    if (FParse::Value(FCommandLine::Get(), TEXT("RallyQuitAfter="), QuitAfter) && QuitAfter > 0.0f)
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [] { FGenericPlatformMisc::RequestExit(false, TEXT("RallyQuitAfter")); }, QuitAfter, false);
    }
}
