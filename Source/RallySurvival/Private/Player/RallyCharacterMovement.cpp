#include "Player/RallyCharacterMovement.h"
#include "Player/RallyCharacter.h"
#include "Player/BodyRules.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace
{
    class FProneMove final : public FSavedMove_Character
    {
    public:
        bool bProne = false;
        bool bBack = false;
        virtual void Clear() override { Super::Clear(); bProne = false; bBack = false; }
        virtual uint8 GetCompressedFlags() const override
        {
            return Super::GetCompressedFlags() | (bProne ? FLAG_Custom_0 : 0) | (bBack ? FLAG_Custom_1 : 0);
        }
        virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
        {
            return bProne == static_cast<const FProneMove*>(NewMove.Get())->bProne
                && bBack == static_cast<const FProneMove*>(NewMove.Get())->bBack
                && Super::CanCombineWith(NewMove, Character, MaxDelta);
        }
        virtual void SetMoveFor(ACharacter* Character, float Dt, const FVector& Accel,
            FNetworkPredictionData_Client_Character& Data) override
        {
            Super::SetMoveFor(Character, Dt, Accel, Data);
            bProne = CastChecked<URallyCharacterMovement>(Character->GetCharacterMovement())->bWantsToProne;
            bBack = CastChecked<URallyCharacterMovement>(Character->GetCharacterMovement())->bWantsBackPose;
        }
        virtual void PrepMoveFor(ACharacter* Character) override
        {
            Super::PrepMoveFor(Character);
            CastChecked<URallyCharacterMovement>(Character->GetCharacterMovement())->bWantsToProne = bProne;
            CastChecked<URallyCharacterMovement>(Character->GetCharacterMovement())->bWantsBackPose = bBack;
        }
    private:
        using Super = FSavedMove_Character;
    };

    class FPronePrediction final : public FNetworkPredictionData_Client_Character
    {
    public:
        explicit FPronePrediction(const UCharacterMovementComponent& Movement)
            : FNetworkPredictionData_Client_Character(Movement) {}
        virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FProneMove()); }
    };
}

FNetworkPredictionData_Client* URallyCharacterMovement::GetPredictionData_Client() const
{
    if (!ClientPredictionData)
        const_cast<URallyCharacterMovement*>(this)->ClientPredictionData = new FPronePrediction(*this);
    return ClientPredictionData;
}

void URallyCharacterMovement::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);
    bWantsToProne = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
    bWantsBackPose = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

bool URallyCharacterMovement::SetProne(bool bProne, bool bProxy, bool bForce)
{
    ARallyCharacter* Crew = Cast<ARallyCharacter>(CharacterOwner);
    if (!Crew) return false;
    UCapsuleComponent* Capsule = Crew->GetCapsuleComponent();
    const ACharacter* Defaults = Crew->GetClass()->GetDefaultObject<ACharacter>();
    const float Radius = bProne ? ProneHalfHeight : Defaults->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
    const float Height = bProne ? ProneHalfHeight : Crew->bIsCrouched ? GetCrouchedHalfHeight()
        : Defaults->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    const float Delta = (Height - Capsule->GetUnscaledCapsuleHalfHeight()) * Capsule->GetShapeScale();
    const FVector Destination = UpdatedComponent->GetComponentLocation() + FVector(0, 0, Delta);
    if (!bProxy && !bForce)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ProneClearance), false, Crew);
        FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
        if (GetWorld()->OverlapBlockingTestByChannel(Destination, FQuat::Identity,
            Capsule->GetCollisionObjectType(),
            FCollisionShape::MakeCapsule(Radius * Capsule->GetShapeScale(), Height * Capsule->GetShapeScale()),
            Query, Response)) return false;
    }
    Capsule->SetCapsuleSize(Radius, Height, true);
    MaxStepHeight = bProne ? 8.0f : GetClass()->GetDefaultObject<URallyCharacterMovement>()->MaxStepHeight;
    if (!bProxy)
    {
        // Preserve the feet's world height. The destination capsule has already passed clearance.
        UpdatedComponent->MoveComponent(FVector(0, 0, Delta), UpdatedComponent->GetComponentQuat(), false);
        bForceNextFloorCheck = true;
    }
    Crew->ApplyPronePose(bProne);
    return true;
}

void URallyCharacterMovement::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
    ARallyCharacter* Crew = Cast<ARallyCharacter>(CharacterOwner);
    if (!Crew) { Super::UpdateCharacterStateBeforeMovement(DeltaSeconds); return; }
    if (Crew->GetLocalRole()==ROLE_SimulatedProxy)
    {
        // Remote actors consume the replicated stance, never the local input-intent flags.
        Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
        return;
    }
    if (Crew->GetSeatedCar()) bWantsToProne = false;
    if (bWantsToProne != Crew->IsProne() && (!bWantsToProne || IsMovingOnGround()))
        SetProne(bWantsToProne);
    if (!Crew->IsProne()) bWantsBackPose = false;
    Crew->SetBackPose(Crew->IsProne() && bWantsBackPose);
    // Keep the prior crouch state while prone; stand/crouch resumes only after clearance succeeds.
    const bool bWantedCrouch = bWantsToCrouch;
    if (Crew->IsProne()) bWantsToCrouch = Crew->bIsCrouched;
    Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
    bWantsToCrouch = bWantedCrouch;
}

float URallyCharacterMovement::GetMaxSpeed() const
{
    const ARallyCharacter* Crew = Cast<ARallyCharacter>(CharacterOwner);
    return Crew && Crew->IsProne() ? 65.0f * Rally::Body::WalkScale(Crew->GetInjury()) : Super::GetMaxSpeed();
}

bool URallyCharacterMovement::CanAttemptJump() const
{
    const ARallyCharacter* Crew = Cast<ARallyCharacter>(CharacterOwner);
    return (!Crew || !Crew->IsProne()) && Super::CanAttemptJump();
}
