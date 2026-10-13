#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "RallyTeamState.generated.h"

// A part ordered from the catalogue (Game/EconomyRules.h), on its way.
USTRUCT()
struct FRallyOrder
{
    GENERATED_BODY()
    UPROPERTY() int32 Item = INDEX_NONE;
    UPROPERTY() int32 DueDay = 1;
    UPROPERTY() float DueHour = 8.0f;
};

// The team's money and post (docs/MONEY.md): the kitty, the cash box in the workshop that prizes go into
// and parts are paid from, and the catalogue orders on their way. The server owns it; everyone sees it.
UCLASS()
class RALLYSURVIVAL_API ARallyTeamState : public AInfo
{
    GENERATED_BODY()

public:
    ARallyTeamState();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void Tick(float DeltaSeconds) override;
    static ARallyTeamState* Get(const UWorld* World);

    int32 GetKittySk() const { return KittySk; }
    const TArray<FRallyOrder>& GetOrders() const { return Orders; }
    // Server only: money in (positive) or out, and why.
    void AddMoney(int32 Sk, const FString& Why);
    // Server only: orders a catalogue item, paid from the kitty; false if it cannot be afforded.
    bool Order(int32 Item);
    // Where the post leaves parcels: beside the service yard (set by the game mode).
    void SetDeliveryPoint(const FVector& Point) { DeliveryPoint = Point; }
    // Saving (docs/SAVING.md), server only.
    void Restore(int32 Kitty, const TArray<FRallyOrder>& InOrders);

private:
    void Deliver(const FRallyOrder& Order);
    UPROPERTY(Replicated) int32 KittySk = 0;
    UPROPERTY(Replicated) TArray<FRallyOrder> Orders;
    FVector DeliveryPoint = FVector::ZeroVector;
};
