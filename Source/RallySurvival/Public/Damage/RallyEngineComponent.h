#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Damage/EngineRules.h"
#include "Damage/UndersideRules.h"
#include "RallyEngineComponent.generated.h"

class UInstancedStaticMeshComponent;
class URallyDamageComponent;

// Compact engine state from the server: temperature, coolant, oil, fuel and whether it runs.
USTRUCT()
struct FRallyEngineState
{
    GENERATED_BODY()
    UPROPERTY() uint8 TempC = static_cast<uint8>(Rally::Engine::WarmC);
    // Tenths of a litre.
    UPROPERTY() uint8 CoolantDl = static_cast<uint8>(Rally::Engine::CoolantCapacityL * 10.0);
    UPROPERTY() uint8 OilDl = static_cast<uint8>(Rally::Underside::OilCapacityL * 10.0);
    // Whole litres, rounded up: 0 only once it is empty.
    UPROPERTY() uint8 FuelL = static_cast<uint8>(Rally::Underside::FuelCapacityL);
    UPROPERTY() bool bRunning = false;
    // Battery charge, percent.
    UPROPERTY() uint8 ChargePct = 100;
};

USTRUCT()
struct FRallySavedEngine
{
    GENERATED_BODY()
    UPROPERTY() float TempC = 90.0f;
    UPROPERTY() float CoolantL = 6.0f;
    UPROPERTY() float OilL = 4.5f;
    UPROPERTY() float FuelL = 55.0f;
    UPROPERTY() float Charge = 1.0f;
};

DECLARE_MULTICAST_DELEGATE(FRallyEngineChanged);

// Why turning the key did not start it.
enum class ERallyStartFailure : uint8 { NoPower, Seized, NoFuel, NoStarter, WontRun, NoSpark };
DECLARE_MULTICAST_DELEGATE_OneParam(FRallyStartFailed, ERallyStartFailure);

// Cooling, oil, fuel, engine wear and ignition (Damage/EngineRules.h, Damage/UndersideRules.h, docs/DAMAGE.md
// "Engine, cooling and battery" and "Underside"). The server runs the temperature, coolant, oil and fuel
// from what the engine is doing on whichever machine drives, wears the engine (its ERallyPart) when it
// overheats or runs short of oil, and decides whether it runs: the key turned (I, from the driver's seat or
// reaching in at the driver's door), the engine not seized, the battery working and fuel in the tank. The
// key stays in when the driver gets out, so a running engine keeps idling. Every machine shows the steam
// and smoke and hears a start that fails.
UCLASS(ClassGroup = (Rally))
class RALLYSURVIVAL_API URallyEngineComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URallyEngineComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Puffs: a world-space instanced sphere, one instance per puff, custom data opacity then colour.
    void Initialize(URallyDamageComponent* InDamage, UInstancedStaticMeshComponent* InPuffs);
    // From the car's Tick on every machine. PowerKw is what the engine makes at the crank now; bDriver
    // whether someone is in the driver's seat (server only).
    void TickEngine(float Dt, double PowerKw, double EngineRpm, bool bDriver, double EngineLoad);

    bool IsRunning() const { return State.bRunning; }
    double GetTempC() const { return State.TempC; }
    double GetCoolantL() const { return State.CoolantDl * 0.1; }
    double GetOilL() const { return State.OilDl * 0.1; }
    double GetFuelL() const { return State.FuelL; }
    // Share of full torque the physics may use, from engine health and temperature.
    double GetPowerScale() const;
    double GetFrictionScale() const;
    double GetBatteryHealth() const;
    double GetEngineHealth() const;
    double GetCharge() const { return State.ChargePct * 0.01; }
    // Every machine: the bolt-on modules as the engine feels them (from the car's part conditions).
    void SetModules(const Rally::Engine::FModules& InModules) { Modules = InModules; }
    const Rally::Engine::FModules& GetModules() const { return Modules; }

    // Server only. The key is turned: off, or an attempt to start. True if anything changed.
    bool ToggleIgnition();
    bool IsIgnitionOn() const { return bIgnition; }
    // Server only: pours up to Litres of water into the cooling system, or of oil into the engine; returns
    // what went in.
    double TopUp(double Litres);
    double TopUpOil(double Litres);
    // Server only: fills the tank; returns the litres that went in.
    double Refuel();
    // Server only: pours up to Litres of petrol into the tank (the fuel can); returns what went in.
    double AddFuel(double Litres);
    // Server only (F9): full coolant, warm engine, ignition on.
    void Reset();
    // Server only: a crash knocks a running engine off (Rally::Engine::CrashStalls); the key starts it again.
    void Stall();
    // Test harness, server only.
    void DebugSetCoolant(double Litres);
    void DebugSetOil(double Litres);
    void DebugSetTempC(double TempC);
    // Saving (docs/SAVING.md), server only.
    FRallySavedEngine SaveState() const;
    void RestoreState(const FRallySavedEngine& Saved);
    // Readout: temperature, coolant and what stops it running.
    FString Describe() const;

    FRallyEngineChanged OnEngineChanged;
    // Every machine: a start failed (the starter clicks, strains against a seized engine, or turns an engine
    // that will not catch).
    FRallyStartFailed OnStartFailed;

private:
    UFUNCTION() void OnRep_State();
    UFUNCTION(NetMulticast, Unreliable) void MulticastStartFailed(uint8 Reason);
    void UpdateRunning(bool bDriver);
    void PublishState();
    void TickPuffs(float Dt, double PowerKw);
    void Emit(const FVector& LocalPoint, const FVector& Spread, const FVector& LocalVelocity, const FLinearColor& Color, float Opacity, float Size);

    UPROPERTY(ReplicatedUsing = OnRep_State) FRallyEngineState State;
    UPROPERTY(Transient) TObjectPtr<URallyDamageComponent> Damage;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Puffs;

    // Server: unquantised state behind State.
    Rally::Engine::FThermal Thermal;
    double OilL = Rally::Underside::OilCapacityL;
    double FuelL = Rally::Underside::FuelCapacityL;
    double PendingWear = 0.0;
    double Charge = 1.0;
    bool bKnocking = false;
    double LastKnockTime = -1e9;
    Rally::Engine::FModules Modules;
    Rally::Engine::FIgnitionStability IgnitionStability;
    // Whether it can run now, all things considered.
    bool CanRunNow() const;
    bool bIgnition = false;
    bool bHadDriver = false;
    // What UpdateRunning decided; State.bRunning once published.
    bool bShouldRun = false;

    struct FPuff
    {
        FVector Location = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
        FLinearColor Color = FLinearColor::White;
        float Age = 0.0f;
        float Life = 1.0f;
        float Size = 0.2f;
        float Opacity = 0.5f;
    };
    TArray<FPuff> PuffList;
    // Fractional puffs owed to each emitter: radiator leak, boiling, a dry engine's haze, exhaust smoke, oil
    // dripping onto the hot exhaust.
    float Owed[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
};
