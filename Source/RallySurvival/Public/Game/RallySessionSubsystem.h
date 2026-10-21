#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RallySessionSubsystem.generated.h"

class UNetDriver;
class UWorld;

// Steam lobby lifetime follows the game instance, across map changes.
UCLASS()
class RALLYSURVIVAL_API URallySessionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    static constexpr int32 MaxPlayers = 4;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable)
    void HostSession();
    // A Steam session result from a friend lookup or an accepted invitation.
    void JoinFriend(const FOnlineSessionSearchResult& Result, int32 LocalUserNum = 0);
    UFUNCTION(BlueprintCallable)
    void LeaveSession();
    UFUNCTION(BlueprintPure)
    FString GetLastFailure() const { return LastFailure; }

private:
    enum class EOperation { Idle, Creating, Joining, Destroying };
    EOperation Operation = EOperation::Idle;
    IOnlineSessionPtr Sessions;
    TOptional<FOnlineSessionSearchResult> PendingJoin;
    int32 JoiningUser = 0;
    bool bSteam = false;
    bool bStarted = false;
    bool bLeaving = false;
    bool bReturnReady = false;
    bool bOpeningDefault = false;
    FString LastFailure;
    FTSTicker::FDelegateHandle TickHandle;
    FDelegateHandle CreateHandle, JoinHandle, DestroyHandle, InviteHandle;
    FDelegateHandle NetworkHandle, TravelHandle, MapHandle;

    bool Tick(float DeltaTime);
    void DestroyCurrentSession();
    void Failed(const FString& Reason);
    void Created(FName Name, bool bSuccess);
    void Joined(FName Name, EOnJoinSessionCompleteResult::Type Result);
    void Destroyed(FName Name, bool bSuccess);
    void InviteAccepted(bool bSuccess, int32 LocalUserNum, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& Result);
    void NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Reason);
    void TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Reason);
    void MapLoaded(UWorld* World);
};
