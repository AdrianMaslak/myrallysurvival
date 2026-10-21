#include "Game/RallySessionSubsystem.h"
#include "Game/RallyMainMenu.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameMapsSettings.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

void URallySessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
    IOnlineIdentityPtr Identity = Subsystem ? Subsystem->GetIdentityInterface() : nullptr;
    bSteam = Subsystem && Subsystem->GetSubsystemName() == STEAM_SUBSYSTEM;
    Sessions = Subsystem ? Subsystem->GetSessionInterface() : nullptr;
    UE_LOG(LogTemp, Display, TEXT("RALLY_SESSION steam=%s user=%s"), bSteam ? TEXT("on") : TEXT("off"),
        Identity.IsValid() ? *Identity->GetPlayerNickname(0) : TEXT("offline"));
    if (Sessions.IsValid())
    {
        CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::Created));
        JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::Joined));
        DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::Destroyed));
        InviteHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &ThisClass::InviteAccepted));
    }
    NetworkHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::NetworkFailed);
    TravelHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::TravelFailed);
    MapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::MapLoaded);
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick));
}

void URallySessionSubsystem::Deinitialize()
{
    FTSTicker::RemoveTicker(TickHandle);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapHandle);
    GEngine->OnNetworkFailure().Remove(NetworkHandle);
    GEngine->OnTravelFailure().Remove(TravelHandle);
    if (Sessions.IsValid())
    {
        Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
        Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
        Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
        Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteHandle);
        if (Sessions->GetNamedSession(NAME_GameSession)) Sessions->DestroySession(NAME_GameSession);
        Sessions.Reset();
    }
    Super::Deinitialize();
}

bool URallySessionSubsystem::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World || !World->IsGameWorld()) return true;
    if (bReturnReady)
    {
        bReturnReady = false;
        bOpeningDefault = true;
        UE_LOG(LogTemp, Display, TEXT("RALLY_SESSION returning to default map"));
        UGameplayStatics::OpenLevel(GetGameInstance(), FName(*UGameMapsSettings::GetGameDefaultMap()), true);
        return true;
    }
    APlayerController* Player = GetGameInstance()->GetFirstLocalPlayerController();
    if (!World->HasBegunPlay() || !Player || bOpeningDefault) return true;
    if (!bStarted)
    {
        bStarted = true;
        FString Address;
        if (FParse::Value(FCommandLine::Get(), TEXT("RallyJoin="), Address))
        {
            if (!FParse::Param(FCommandLine::Get(), TEXT("nosteam"))) Failed(TEXT("RallyJoin requires -nosteam for the plain IP driver"));
            else if (Address.IsEmpty()) Failed(TEXT("RallyJoin address is empty"));
            else
            {
                UE_LOG(LogTemp, Display, TEXT("RALLY_SESSION joining IP %s"), *Address);
                Player->ClientTravel(Address, TRAVEL_Absolute);
            }
        }
        else if (FParse::Param(FCommandLine::Get(), TEXT("RallyHost")) && !PendingJoin.IsSet()) HostSession();
    }
    if (!bMenuChecked)
    {
        // A normal start on the default map begins at the menu, and so does a return there after a session ends or
        // fails; scripted runs (-unattended, -RallyHost, -RallyJoin) and editor Play do not.
        bMenuChecked = true;
        FString Join;
        const bool bScripted = FParse::Param(FCommandLine::Get(), TEXT("unattended")) || FParse::Param(FCommandLine::Get(), TEXT("RallyHost"))
            || FParse::Value(FCommandLine::Get(), TEXT("RallyJoin="), Join);
        const bool bDefaultMap = World->GetOutermost()->GetName() == FPackageName::ObjectPathToPackageName(UGameMapsSettings::GetGameDefaultMap());
        if (!bScripted && bDefaultMap && World->GetNetMode() == NM_Standalone && !World->IsPlayInEditor())
            RallyMainMenu::Show(GetGameInstance(), LastFailure);
        else if (RallyMainMenu::IsShown()) RallyMainMenu::Hide(GetGameInstance());
    }
    if (PendingJoin.IsSet() && Operation == EOperation::Idle && !bLeaving)
    {
        FOnlineSessionSearchResult Result = PendingJoin.GetValue();
        PendingJoin.Reset();
        Operation = EOperation::Joining;
        UE_LOG(LogTemp, Display, TEXT("RALLY_SESSION joining friend"));
        if (!Sessions->JoinSession(JoiningUser, NAME_GameSession, Result) && Operation == EOperation::Joining)
        {
            Operation = EOperation::Idle;
            Failed(TEXT("JoinSession did not start"));
        }
    }
    return true;
}

void URallySessionSubsystem::HostSession()
{
    if (Operation != EOperation::Idle || bLeaving || bOpeningDefault || PendingJoin.IsSet()) return;
    if (!bSteam || !Sessions.IsValid()) { Failed(TEXT("hosting requires Steam running and logged in")); return; }
    if (Sessions->GetNamedSession(NAME_GameSession)) return;
    LastFailure.Reset();
    FOnlineSessionSettings Settings;
    Settings.NumPublicConnections = MaxPlayers;
    Settings.bShouldAdvertise = true;
    Settings.bAllowJoinInProgress = true;
    Settings.bUsesPresence = true;
    Settings.bUseLobbiesIfAvailable = true;
    Settings.bAllowInvites = true;
    Settings.bAllowJoinViaPresence = true;
    Settings.bAllowJoinViaPresenceFriendsOnly = true;
    Operation = EOperation::Creating;
    UE_LOG(LogTemp, Display, TEXT("RALLY_SESSION creating friends lobby"));
    if (!Sessions->CreateSession(0, NAME_GameSession, Settings) && Operation == EOperation::Creating)
    {
        Operation = EOperation::Idle;
        Failed(TEXT("CreateSession did not start"));
    }
}

void URallySessionSubsystem::Created(FName Name, bool bSuccess)
{
    if (Name != NAME_GameSession) return;
    Operation = EOperation::Idle;
    if (bLeaving) { DestroyCurrentSession(); return; }
    if (!bSuccess) { Failed(TEXT("CreateSession failed")); return; }
    UE_LOG(LogTemp, Display, TEXT("RALLY_SESSION created; opening Countryside listen server"));
    UGameplayStatics::OpenLevel(GetGameInstance(), TEXT("/Game/Maps/Countryside/L_Countryside"), true, TEXT("listen"));
}

void URallySessionSubsystem::JoinFriend(const FOnlineSessionSearchResult& Result, int32 LocalUserNum)
{
    if (!bSteam || !Sessions.IsValid() || !Result.IsValid()) { Failed(TEXT("invalid Steam friend session")); return; }
    if (bOpeningDefault) return;
    LastFailure.Reset();
    PendingJoin = Result;
    JoiningUser = LocalUserNum;
    bLeaving = true;
    DestroyCurrentSession();
}

void URallySessionSubsystem::InviteAccepted(bool bSuccess, int32 LocalUserNum, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& Result)
{
    UE_LOG(LogTemp, Display, TEXT("RALLY_SESSION invite accepted"));
    if (!bSuccess || !UserId.IsValid()) { Failed(TEXT("invite could not be resolved")); return; }
    JoinFriend(Result, LocalUserNum);
}

void URallySessionSubsystem::Joined(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
    if (Name != NAME_GameSession) return;
    Operation = EOperation::Idle;
    if (bLeaving) { DestroyCurrentSession(); return; }
    if (Result != EOnJoinSessionCompleteResult::Success)
    {
        Failed(FString::Printf(TEXT("JoinSession failed: %s"), LexToString(Result)));
        return;
    }
    FString Address;
    APlayerController* Player = GetGameInstance()->GetFirstLocalPlayerController();
    if (!Sessions->GetResolvedConnectString(NAME_GameSession, Address) || Address.IsEmpty() || !Player)
    {
        Failed(TEXT("joined lobby but no connect string or local player"));
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("RALLY_SESSION joined; travelling to %s"), *Address);
    Player->ClientTravel(Address, TRAVEL_Absolute);
}

void URallySessionSubsystem::LeaveSession()
{
    bStarted = true;
    PendingJoin.Reset();
    bLeaving = true;
    DestroyCurrentSession();
}

void URallySessionSubsystem::DestroyCurrentSession()
{
    // A pending create/join must finish before its session can be destroyed.
    if (Operation != EOperation::Idle) return;
    if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
    {
        Destroyed(NAME_GameSession, true);
        return;
    }
    Operation = EOperation::Destroying;
    UE_LOG(LogTemp, Display, TEXT("RALLY_SESSION destroying"));
    if (!Sessions->DestroySession(NAME_GameSession) && Operation == EOperation::Destroying) Destroyed(NAME_GameSession, false);
}

void URallySessionSubsystem::Destroyed(FName Name, bool bSuccess)
{
    if (Name != NAME_GameSession) return;
    Operation = EOperation::Idle;
    bLeaving = false;
    if (!bSuccess)
    {
        PendingJoin.Reset();
        if (LastFailure.IsEmpty()) LastFailure = TEXT("DestroySession failed");
        UE_LOG(LogTemp, Warning, TEXT("RALLY_SESSION failed: DestroySession failed"));
    }
    else UE_LOG(LogTemp, Display, TEXT("RALLY_SESSION destroyed"));
    bReturnReady = !PendingJoin.IsSet();
}

void URallySessionSubsystem::Failed(const FString& Reason)
{
    LastFailure = Reason;
    UE_LOG(LogTemp, Warning, TEXT("RALLY_SESSION failed: %s"), *Reason);
    // If the default map itself cannot load, keep the reason without looping travel.
    if (!bOpeningDefault) LeaveSession();
}

void URallySessionSubsystem::NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Reason)
{
    bool bOurWorld = World && World->GetGameInstance() == GetGameInstance();
    if (!World)
    {
        // Pending connection failures have no world. The engine may already have cancelled the driver.
        const FWorldContext* Context = Driver ? GEngine->GetWorldContextFromPendingNetGameNetDriver(Driver) : nullptr;
        bOurWorld = Context ? Context->OwningGameInstance == GetGameInstance()
            : GetWorld() && GetWorld()->WorldType == EWorldType::Game;
    }
    if (bOurWorld) Failed(FString::Printf(TEXT("network %s: %s"), ENetworkFailure::ToString(Type), *Reason));
}

void URallySessionSubsystem::TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Reason)
{
    if (!World && GetWorld() && GetWorld()->WorldType == EWorldType::Game) World = GetWorld();
    if (World && World->GetGameInstance() == GetGameInstance()) Failed(FString::Printf(TEXT("travel %s: %s"), ETravelFailure::ToString(Type), *Reason));
}

void URallySessionSubsystem::MapLoaded(UWorld* World)
{
    if (World && World->GetGameInstance() == GetGameInstance())
    {
        bOpeningDefault = false;
        bMenuChecked = false;
    }
}
