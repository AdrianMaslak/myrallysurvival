#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RallyCharacterMovement.generated.h"

// Prone intent travels with movement packets, including saved moves replayed after corrections.
UCLASS()
class RALLYSURVIVAL_API URallyCharacterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    static constexpr float ProneHalfHeight = 14.0f;
    bool bWantsToProne = false;
    bool bWantsBackPose = false;
    virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    virtual float GetMaxSpeed() const override;
    virtual bool CanAttemptJump() const override;
    bool SetProne(bool bProne, bool bProxy = false, bool bForce = false);
};
