#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RallyDebrisActor.generated.h"

class ARallyCarPawn;
class UDynamicMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

// A part that came off a car: bonnet, door, bumper, boot lid or wheel. The server spawns and simulates
// it; movement replicates like any physics actor. Each machine draws it with its own copy of the car's
// dented mesh for that part, so it lands looking as it did on the car. It does not collide with cars.
UCLASS()
class RALLYSURVIVAL_API ARallyDebrisActor : public AActor
{
    GENERATED_BODY()

public:
    ARallyDebrisActor();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Server only, right after spawning. Panel is the car's panel index, or INDEX_NONE for a wheel.
    void Launch(ARallyCarPawn* Car, UStaticMesh* Mesh, int32 Panel, float MassKg, float AirDamping,
        const FVector& Velocity, const FVector& AngularVelocityDeg);

    ARallyCarPawn* GetSourceCar() const { return SourceCar; }
    int32 GetPanel() const { return Panel; }

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnRep_Source();

    void BuildVisual();

    // Physics shape (the part's convex hull); hidden when the dented copy is drawn instead.
    UPROPERTY(VisibleAnywhere, Category = "Debris")
    TObjectPtr<UStaticMeshComponent> Shape;

    UPROPERTY(Transient)
    TObjectPtr<UDynamicMeshComponent> Dented;

    UPROPERTY(ReplicatedUsing = OnRep_Source)
    TObjectPtr<ARallyCarPawn> SourceCar;

    UPROPERTY(ReplicatedUsing = OnRep_Source)
    TObjectPtr<UStaticMesh> PartMesh;

    UPROPERTY(ReplicatedUsing = OnRep_Source)
    int8 Panel = INDEX_NONE;
};
