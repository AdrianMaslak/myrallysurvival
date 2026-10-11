#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RallyCharacter.generated.h"

class ARallyCarPawn;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UStaticMeshComponent;
struct FInputActionValue;

// First-person crew member on foot. Walks to a car door and presses Interact to take a seat:
// the driver hands control to the car pawn; the co-driver stays this pawn, fixed in the seat with
// free look. The server decides every seat change (ARallyCarPawn::ServerEnterSeat / ServerExitSeat).
UCLASS()
class RALLYSURVIVAL_API ARallyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ARallyCharacter();

    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void NotifyControllerChanged() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Server only: attach to or detach from a seat. ExitLocation is used when leaving.
    void SetSeat(ARallyCarPawn* Car, int32 Seat);
    void ClearSeat(const FVector& ExitLocation, const FRotator& ExitRotation);

    ARallyCarPawn* GetSeatedCar() const { return SeatedCar; }
    int32 GetSeatIndex() const { return SeatIndex; }

protected:
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    // Placeholder body other players see.
    UPROPERTY(VisibleAnywhere, Category = "Body")
    TObjectPtr<UStaticMeshComponent> BodyMesh;

    UPROPERTY(EditAnywhere, Category = "Movement")
    float WalkSpeed = 350.0f;

    UPROPERTY(EditAnywhere, Category = "Movement")
    float SprintSpeed = 600.0f;

    UPROPERTY(EditAnywhere, Category = "Camera")
    float MouseSensitivity = 0.15f;

private:
    UFUNCTION(Server, Reliable)
    void ServerInteract();

    UFUNCTION(Server, Reliable)
    void ServerSetSprint(bool bSprint);

    UFUNCTION()
    void OnRep_Seat();

    void ApplySeatState();
    void CreateInputObjects();
    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);
    void OnStickLook(const FInputActionValue& Value);
    void OnJump(const FInputActionValue& Value);
    void OnInteract(const FInputActionValue& Value);
    void OnSprint(const FInputActionValue& Value);
    void UpdatePrompt();
    void TickAutoSeat(float DeltaSeconds);
    void SetSprint(bool bSprint);

    UPROPERTY(ReplicatedUsing = OnRep_Seat)
    TObjectPtr<ARallyCarPawn> SeatedCar;

    UPROPERTY(ReplicatedUsing = OnRep_Seat)
    int8 SeatIndex = -1;

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> OnFootContext;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MouseLookAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> StickLookAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> InteractAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SprintAction;

    // Seated look, relative to the car.
    FRotator SeatLook = FRotator::ZeroRotator;
    FVector2D StickLook = FVector2D::ZeroVector;
    bool bAutoSeatDone = false;
    int32 AutoSeatWaypoint = 0;
    float AutoSeatShotTimer = -1.0f;
};
