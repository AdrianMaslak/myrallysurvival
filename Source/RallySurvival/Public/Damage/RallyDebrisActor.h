#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RallyDebrisActor.generated.h"

class ARallyCarPawn;
class ARallyCharacter;
class UStaticMesh;
class UStaticMeshComponent;
class URallyBodyDeformComponent;
struct FRallyDent;

USTRUCT()
struct FRallyDebrisSource
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<ARallyCarPawn> Car;
    UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY() int32 Panel = INDEX_NONE;
    UPROPERTY() FTransform MeshToCar;
    UPROPERTY() float MassKg = 15.0f;
    // The condition it brings when fitted: a wheel's tyre, a suspension part's own health.
    UPROPERTY() float Health = 1.0f;
    // A spare from the boot kit rather than a part that came off the car.
    UPROPERTY() bool bSpare = false;
};

// Where a part rides when nobody carries it and it is not loose on the ground.
UENUM()
enum class ERallyPartMount : uint8
{
    Loose,
    // Stowed in its car's boot: reachable with the boot lid open.
    Stowed,
    // A jack standing under its car, holding a corner up.
    UnderCar,
};

// Server-simulated persistent part. Its own replicated dent snapshot is independent of the car.
UCLASS()
class RALLYSURVIVAL_API ARallyDebrisActor : public AActor
{
    GENERATED_BODY()
public:
    ARallyDebrisActor();
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void Launch(const FRallyDebrisSource& InSource, const TArray<FRallyDent>& Dents,
        const FVector& Velocity, const FVector& AngularVelocityDeg);
    bool PickUp(ARallyCharacter* Character);
    void Drop();
    bool CanPickUp(const ARallyCharacter* Character) const;
    // Server only: ride with the car at this relative pose (stowed, or a jack under it), or go loose.
    void Mount(ERallyPartMount How, const FVector& Location = FVector::ZeroVector, const FRotator& Rotation = FRotator::ZeroRotator);
    ERallyPartMount GetMount() const { return MountKind; }
    float GetHealth() const { return Source.Health; }
    // Server only: what it brings has changed (a water can pouring out).
    void SetHealth(float Health);
    bool IsSpare() const { return Source.bSpare; }
    ARallyCarPawn* GetSourceCar() const { return Source.Car; }
    int32 GetPanel() const { return Source.Panel; }
    ARallyCharacter* GetCarrier() const { return Carrier; }
    float GetMassKg() const { return Source.MassKg; }
    float GetMaxDentCm() const;

private:
    UFUNCTION() void OnRep_Source();
    UFUNCTION() void OnRep_Carrier();
    void ApplyCarryState();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Shape;
    UPROPERTY(VisibleAnywhere) TObjectPtr<URallyBodyDeformComponent> Deform;
    UPROPERTY(ReplicatedUsing=OnRep_Source) FRallyDebrisSource Source;
    UPROPERTY(ReplicatedUsing=OnRep_Carrier) TObjectPtr<ARallyCharacter> Carrier;
    UPROPERTY(ReplicatedUsing=OnRep_Carrier) ERallyPartMount MountKind = ERallyPartMount::Loose;
    UPROPERTY(ReplicatedUsing=OnRep_Carrier) FVector_NetQuantize10 MountLocation;
    UPROPERTY(ReplicatedUsing=OnRep_Carrier) FRotator MountRotation;
    bool bBuilt = false;
    // It comes off overlapping its car's body box, so cars pass through it until it is clear of that box.
    bool bIgnoringCars = true;
};
