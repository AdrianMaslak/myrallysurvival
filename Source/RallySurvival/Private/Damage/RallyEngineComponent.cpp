#include "Damage/RallyEngineComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Damage/RallyDamageComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

namespace
{
    // Car frame (cm, assembly origin at ground below the car centre, then the body's -80 cm offset).
    const FVector AssemblyOffset(0.0, 0.0, -80.0);
    // Where steam and smoke come out: the radiator core behind the grille, the gap at the bonnet's front
    // edge, and the tail pipe.
    // The radiator core's front face (the 1.8T set's radiator, right of the middle).
    const FVector RadiatorFace(193.0, 16.0, 56.0);
    const FVector BonnetGap(206.0, 0.0, 80.0);
    const FVector TailPipe(-229.0, -44.0, 27.0);
    // Under the car where a leaking sump drips onto the front pipe.
    const FVector FrontPipe(110.0, -5.0, 20.0);
    constexpr int32 MaxPuffs = 96;
}

URallyEngineComponent::URallyEngineComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void URallyEngineComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(URallyEngineComponent, State);
}

void URallyEngineComponent::Initialize(URallyDamageComponent* InDamage, UInstancedStaticMeshComponent* InPuffs)
{
    Damage = InDamage;
    Puffs = InPuffs;
}

double URallyEngineComponent::GetEngineHealth() const
{
    FRallyPartCondition Condition;
    return Damage && Damage->GetPartCondition(ERallyPart::Engine, Condition) ? Condition.Health : 1.0;
}

double URallyEngineComponent::GetBatteryHealth() const
{
    FRallyPartCondition Condition;
    return Damage && Damage->GetPartCondition(ERallyPart::Battery, Condition) ? Condition.Health : 1.0;
}

double URallyEngineComponent::GetPowerScale() const
{
    return Rally::Engine::PowerScale(GetEngineHealth(), GetTempC()) * Rally::Engine::ModulePower(Modules);
}

bool URallyEngineComponent::CanRunNow() const
{
    return Rally::Engine::CanRun(GetEngineHealth(), GetBatteryHealth()) && FuelL > 0.0 && Rally::Engine::Breathes(Modules)
        && Rally::Engine::Fires(Modules) && Charge > Rally::Engine::FlatCharge;
}

double URallyEngineComponent::GetFrictionScale() const
{
    return Rally::Engine::FrictionScale(GetEngineHealth());
}

void URallyEngineComponent::TickEngine(float Dt, double PowerKw, double EngineRpm, bool bDriver, double EngineLoad)
{
    AActor* Owner = GetOwner();
    if (!Owner || Dt <= 0.0f)
    {
        return;
    }
    if (Owner->HasAuthority() && Damage)
    {
        UpdateRunning(bDriver);
        if (Rally::Engine::StepIgnition(IgnitionStability, Modules, bShouldRun, EngineRpm, EngineLoad, Dt))
        {
            UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE stalled (ignition misfire at %.0f rpm)"), EngineRpm);
            bIgnition = false;
            UpdateRunning(bDriver);
            GetOwner()->ForceNetUpdate();
        }
        // Consumers below must see a stop immediately, including the tick on which it stalled.
        PublishState();
        FRallyPartCondition Radiator;
        Damage->GetPartCondition(ERallyPart::Radiator, Radiator);
        Rally::Engine::FThermalInput In;
        In.bRunning = State.bRunning;
        In.PowerKw = PowerKw;
        In.SpeedMs = Owner->GetVelocity().Size() * 0.01;
        In.RadiatorHealth = Radiator.Health;
        const double CoolantBefore = Thermal.CoolantL;
        PendingWear += Rally::Engine::Step(Thermal, In, Dt);
        if (CoolantBefore > 0.0 && Thermal.CoolantL <= 0.0)
        {
            UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE coolant gone temp=%.0fC"), Thermal.TempC);
        }
        // Oil through a cracked sump, fuel burnt and through a holed tank; an engine short of oil wears.
        FRallyPartCondition Sump, Tank;
        Damage->GetPartCondition(ERallyPart::OilSump, Sump);
        Damage->GetPartCondition(ERallyPart::FuelTank, Tank);
        const double OilBefore = OilL;
        const double FuelBefore = FuelL;
        OilL = FMath::Max(OilL - Rally::Underside::OilLeakRate(Sump.Health, State.bRunning) * Dt, 0.0);
        FuelL = FMath::Max(FuelL - (Rally::Underside::FuelUse(PowerKw, State.bRunning) * Modules.FuelScale + Rally::Underside::FuelLeakRate(Tank.Health)) * Dt, 0.0);
        if (State.bRunning)
        {
            PendingWear += Rally::Underside::StarvationWear(OilL, EngineRpm) * Dt;
            // More boost than the computer's fuel map allows: it knocks under load.
            const double Knock = Modules.KnockPerSecond * FMath::Clamp(PowerKw / 110.0, 0.0, 1.0);
            // Power flickers with wheelspin and the limiter: one line per spell of knocking, not per flicker.
            const double Now = GetWorld()->GetTimeSeconds();
            if (Knock > 0.0 && !bKnocking && Now - LastKnockTime > 5.0) UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE knocking (boost past what the chip allows)"));
            bKnocking = Knock > 0.0;
            if (bKnocking) LastKnockTime = Now;
            PendingWear += Knock * Dt;
        }
        if (OilBefore >= Rally::Underside::OilLowL && OilL < Rally::Underside::OilLowL)
        {
            UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE oil light on (%.1f L)"), OilL);
        }
        if (FuelBefore > 0.0 && FuelL <= 0.0)
        {
            UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE out of fuel"));
            UpdateRunning(bDriver);
        }
        // The alternator keeps the battery up; without one it runs down until the ignition dies.
        const double ChargeBefore = Charge;
        Charge = FMath::Clamp(Charge + Rally::Engine::ChargeRate(Modules, State.bRunning) * Dt, 0.0, 1.0);
        if (ChargeBefore > Rally::Engine::FlatCharge && Charge <= Rally::Engine::FlatCharge)
        {
            UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE battery flat"));
            UpdateRunning(bDriver);
        }
        // Wear reaches the replicated part condition a percent at a time; the heat that wears the engine
        // cooks its ignition coils as well, half as fast again.
        if (PendingWear >= 0.01)
        {
            const double Before = GetEngineHealth();
            if (Thermal.TempC > Rally::Engine::WearStartC)
                for (const ERallyPart Coil : { ERallyPart::Coil1, ERallyPart::Coil2, ERallyPart::Coil3, ERallyPart::Coil4 })
                    Damage->ApplyPartDamage(Coil, static_cast<float>(PendingWear * 1.5));
            Damage->ApplyPartDamage(ERallyPart::Engine, static_cast<float>(PendingWear));
            PendingWear = 0.0;
            const double After = GetEngineHealth();
            if (FMath::FloorToInt(Before * 10.0) != FMath::FloorToInt(After * 10.0) || After <= 0.0)
            {
                UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE wear temp=%.0fC oil=%.1fL engine=%.2f%s"), Thermal.TempC, OilL, After,
                    After <= 0.0 ? TEXT(" SEIZED") : TEXT(""));
            }
            UpdateRunning(bDriver);
        }
        PublishState();
    }
    TickPuffs(Dt, PowerKw);
}

void URallyEngineComponent::UpdateRunning(bool bDriver)
{
    // The key stays in when the driver gets out: a running engine idles on with nobody in the car.
    bHadDriver = bDriver;
    const bool bCanRun = CanRunNow();
    const bool bRun = bIgnition && bCanRun;
    if (bRun != bShouldRun)
    {
        IgnitionStability = {};
        UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE %s (%s%s)"), bRun ? TEXT("started") : TEXT("stopped"),
            bRun ? TEXT("ignition on") : !bIgnition ? TEXT("ignition off")
            : GetEngineHealth() <= 0.0 ? TEXT("seized") : FuelL <= 0.0 ? TEXT("out of fuel")
            : !Rally::Engine::Breathes(Modules) ? TEXT("intake or engine computer missing") : !Rally::Engine::Fires(Modules) ? TEXT("no spark")
            : Charge <= Rally::Engine::FlatCharge ? TEXT("battery flat")
            : TEXT("no electrics"), bDriver ? TEXT("") : TEXT(", nobody driving"));
        // Stopped by a fault rather than the key: it stays off until someone turns the key again.
        if (!bRun && bIgnition) bIgnition = false;
        bShouldRun = bRun;
    }
}

void URallyEngineComponent::PublishState()
{
    FRallyEngineState Next;
    Next.bRunning = bShouldRun;
    Next.TempC = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Thermal.TempC), 0, 255));
    Next.CoolantDl = static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(Thermal.CoolantL * 10.0 - 0.001), 0, 255));
    Next.OilDl = static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(OilL * 10.0 - 0.001), 0, 255));
    Next.FuelL = static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(FuelL - 0.001), 0, 255));
    Next.ChargePct = static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(Charge * 100.0 - 0.001), 0, 100));
    if (Next.TempC != State.TempC || Next.CoolantDl != State.CoolantDl || Next.OilDl != State.OilDl || Next.FuelL != State.FuelL
        || Next.bRunning != State.bRunning || Next.ChargePct != State.ChargePct)
    {
        State = Next;
        OnRep_State();
    }
}

void URallyEngineComponent::OnRep_State()
{
    OnEngineChanged.Broadcast();
}

void URallyEngineComponent::MulticastStartFailed_Implementation(uint8 Reason)
{
    OnStartFailed.Broadcast(static_cast<ERallyStartFailure>(FMath::Min<uint8>(Reason, static_cast<uint8>(ERallyStartFailure::NoSpark))));
}

void URallyEngineComponent::Stall()
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !bIgnition) return;
    UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE stalled in a crash"));
    bIgnition = false;
    UpdateRunning(bHadDriver);
    PublishState();
    GetOwner()->ForceNetUpdate();
}

bool URallyEngineComponent::ToggleIgnition()
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return false;
    }
    bIgnition = !bIgnition;
    UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE ignition %s"), bIgnition ? TEXT("on") : TEXT("off"));
    if (bIgnition && !(CanRunNow() && Rally::Engine::CanCrank(Modules)))
    {
        // The key turns but it does not start: say why, to everyone in earshot.
        const ERallyStartFailure Reason = !Rally::Engine::HasPower(GetBatteryHealth()) || Charge <= Rally::Engine::FlatCharge ? ERallyStartFailure::NoPower
            : !Rally::Engine::CanCrank(Modules) ? ERallyStartFailure::NoStarter
            : GetEngineHealth() <= 0.0 ? ERallyStartFailure::Seized : FuelL <= 0.0 ? ERallyStartFailure::NoFuel
            : !Rally::Engine::Fires(Modules) ? ERallyStartFailure::NoSpark : ERallyStartFailure::WontRun;
        UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE will not start (%s)"), Reason == ERallyStartFailure::NoPower ? TEXT("no electrics")
            : Reason == ERallyStartFailure::NoStarter ? TEXT("no starter") : Reason == ERallyStartFailure::Seized ? TEXT("seized")
            : Reason == ERallyStartFailure::NoFuel ? TEXT("out of fuel") : Reason == ERallyStartFailure::NoSpark ? TEXT("no spark: coils dead or unplugged")
            : TEXT("intake or engine computer missing"));
        bIgnition = false;
        MulticastStartFailed(static_cast<uint8>(Reason));
    }
    UpdateRunning(bHadDriver);
    PublishState();
    GetOwner()->ForceNetUpdate();
    return true;
}

double URallyEngineComponent::TopUp(double Litres)
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return 0.0;
    }
    const double Used = Rally::Engine::TopUp(Thermal, Litres);
    if (Used > 0.0)
    {
        UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE topped up %.1f L, coolant %.1f L, temp %.0fC"), Used, Thermal.CoolantL, Thermal.TempC);
        PublishState();
        GetOwner()->ForceNetUpdate();
    }
    return Used;
}

double URallyEngineComponent::Refuel()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return 0.0;
    const double Litres = FMath::Max(Rally::Underside::FuelCapacityL - FuelL, 0.0);
    FuelL = Rally::Underside::FuelCapacityL;
    UpdateRunning(bHadDriver);
    PublishState();
    GetOwner()->ForceNetUpdate();
    return Litres;
}

double URallyEngineComponent::TopUpOil(double Litres)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !FMath::IsFinite(Litres))
    {
        return 0.0;
    }
    const double Used = FMath::Clamp(Litres, 0.0, FMath::Max(Rally::Underside::OilCapacityL - OilL, 0.0));
    if (Used > 0.0)
    {
        OilL += Used;
        UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE oil topped up %.1f L, now %.1f L"), Used, OilL);
        PublishState();
        GetOwner()->ForceNetUpdate();
    }
    return Used;
}

void URallyEngineComponent::Reset()
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }
    Thermal = Rally::Engine::FThermal();
    OilL = Rally::Underside::OilCapacityL;
    FuelL = Rally::Underside::FuelCapacityL;
    PendingWear = 0.0;
    Charge = 1.0;
    bIgnition = bHadDriver;
    UpdateRunning(bHadDriver);
    PublishState();
}

FRallySavedEngine URallyEngineComponent::SaveState() const
{
    FRallySavedEngine Saved;
    Saved.TempC = static_cast<float>(Thermal.TempC);
    Saved.CoolantL = static_cast<float>(Thermal.CoolantL);
    Saved.OilL = static_cast<float>(OilL);
    Saved.FuelL = static_cast<float>(FuelL);
    Saved.Charge = static_cast<float>(Charge);
    return Saved;
}

void URallyEngineComponent::RestoreState(const FRallySavedEngine& Saved)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    Thermal.TempC = FMath::Clamp<double>(Saved.TempC, -30.0, 200.0);
    Thermal.CoolantL = FMath::Clamp<double>(Saved.CoolantL, 0.0, Rally::Engine::CoolantCapacityL);
    OilL = FMath::Clamp<double>(Saved.OilL, 0.0, Rally::Underside::OilCapacityL);
    FuelL = FMath::Clamp<double>(Saved.FuelL, 0.0, Rally::Underside::FuelCapacityL);
    Charge = FMath::Clamp<double>(Saved.Charge, 0.0, 1.0);
    UpdateRunning(bHadDriver);
    PublishState();
}

void URallyEngineComponent::DebugSetCoolant(double Litres)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !FMath::IsFinite(Litres)) return;
    Thermal.CoolantL = FMath::Clamp(Litres, 0.0, Rally::Engine::CoolantCapacityL);
    PublishState();
}

void URallyEngineComponent::DebugSetTempC(double TempC)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !FMath::IsFinite(TempC)) return;
    Thermal.TempC = FMath::Clamp(TempC, -30.0, 140.0);
    PublishState();
}

void URallyEngineComponent::DebugSetOil(double Litres)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !FMath::IsFinite(Litres)) return;
    OilL = FMath::Clamp(Litres, 0.0, Rally::Underside::OilCapacityL);
    PublishState();
}

FString URallyEngineComponent::Describe() const
{
    // Like the dashboard: the temperature gauge, the fuel gauge and the oil pressure light (coolant is
    // shown too until there is a header tank to look at).
    FString Text = FString::Printf(TEXT("%d C   coolant %.1f L   fuel %d L%s"), State.TempC, GetCoolantL(), State.FuelL,
        Rally::Underside::OilLight(GetOilL()) ? TEXT("   OIL") : TEXT(""));
    if (!State.bRunning)
    {
        Text += GetEngineHealth() <= 0.0 ? TEXT("   engine seized") : State.FuelL == 0 ? TEXT("   out of fuel")
            : !Rally::Engine::HasPower(GetBatteryHealth()) ? TEXT("   no electrics") : State.ChargePct <= 5 ? TEXT("   battery flat")
            : TEXT("   engine off (I: start)");
    }
    return Text;
}

void URallyEngineComponent::TickPuffs(float Dt, double PowerKw)
{
    AActor* Owner = GetOwner();
    if (!Puffs || !Owner || GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    FRallyPartCondition Radiator;
    if (Damage) Damage->GetPartCondition(ERallyPart::Radiator, Radiator);
    const double Temp = GetTempC();
    const double Coolant = GetCoolantL();
    // Leaking coolant hits the hot core and steams out of the grille; boiling coolant blows out under the
    // bonnet; a dry engine cooking itself hazes out from under the bonnet; a worn engine burns oil and
    // smokes blue from the tail pipe.
    const double Leak = Coolant > 0.0 && Temp > 50.0
        ? Rally::Engine::LeakRate(Radiator.Health, Temp, State.bRunning) / 0.1 * FMath::Clamp((Temp - 50.0) / 40.0, 0.0, 1.0) : 0.0;
    const double Boil = Coolant > 0.0 ? FMath::Clamp((Temp - (Rally::Engine::WearStartC - 4.0)) / 16.0, 0.0, 1.0) : 0.0;
    const double Dry = Coolant <= 0.0 ? FMath::Clamp((Temp - Rally::Engine::WearStartC) / 20.0, 0.0, 1.0) : 0.0;
    const double Worn = State.bRunning ? Rally::Engine::WornSmoke(GetEngineHealth()) : 0.0;
    const double Load = FMath::Clamp(PowerKw / 100.0, 0.0, 1.0);
    FRallyPartCondition Sump;
    if (Damage) Damage->GetPartCondition(ERallyPart::OilSump, Sump);
    const double OilDrip = State.bRunning && GetOilL() > 0.0 && Temp > 60.0
        ? Rally::Underside::OilLeakRate(Sump.Health, true) / 0.05 : 0.0;
    // Burning oil shows as soon as it starts, so the first sign of wear is a warning and not a footnote.
    const double Smoke = Worn > 0.0 ? (10.0 + 20.0 * Worn) * (0.3 + 0.7 * Load) : 0.0;
    const double Rates[5] = { 14.0 * Leak, 18.0 * Boil, 10.0 * Dry, Smoke, 8.0 * OilDrip };
    for (int32 Emitter = 0; Emitter < 5; ++Emitter)
    {
        Owed[Emitter] = FMath::Min(Owed[Emitter] + static_cast<float>(Rates[Emitter]) * Dt, 4.0f);
        for (; Owed[Emitter] >= 1.0f; Owed[Emitter] -= 1.0f)
        {
            if (Emitter == 0)
                Emit(RadiatorFace + AssemblyOffset, FVector(4.0, 30.0, 14.0), FVector(60.0, 0.0, 30.0), FLinearColor(0.85f, 0.87f, 0.9f), 0.5f, 24.0f);
            else if (Emitter == 1)
                Emit(BonnetGap + AssemblyOffset, FVector(6.0, 50.0, 4.0), FVector(40.0, 0.0, 70.0), FLinearColor(0.9f, 0.91f, 0.93f), 0.6f, 28.0f);
            else if (Emitter == 2)
                Emit(BonnetGap + AssemblyOffset, FVector(6.0, 50.0, 4.0), FVector(20.0, 0.0, 50.0), FLinearColor(0.55f, 0.55f, 0.57f), 0.35f, 22.0f);
            else if (Emitter == 3)
                Emit(TailPipe + AssemblyOffset, FVector(2.0, 3.0, 2.0), FVector(-120.0, 0.0, 10.0), FLinearColor(0.42f, 0.47f, 0.56f),
                    static_cast<float>(0.45 + 0.3 * Worn), static_cast<float>(16.0 + 10.0 * Worn));
            else
                Emit(FrontPipe + AssemblyOffset, FVector(20.0, 10.0, 2.0), FVector(0.0, 0.0, 20.0), FLinearColor(0.5f, 0.53f, 0.6f), 0.35f, 16.0f);
        }
    }

    // Puffs drift with the air: they lose the car's speed quickly, rise, spread and fade.
    const float Drag = FMath::Exp(-2.5f * Dt);
    for (int32 I = PuffList.Num() - 1; I >= 0; --I)
    {
        FPuff& Puff = PuffList[I];
        Puff.Age += Dt;
        if (Puff.Age >= Puff.Life)
        {
            PuffList.RemoveAtSwap(I);
            continue;
        }
        Puff.Velocity = Puff.Velocity * Drag + FVector(0.0, 0.0, 45.0) * Dt;
        Puff.Location += Puff.Velocity * Dt;
    }
    TArray<FTransform> Transforms;
    Transforms.Reserve(PuffList.Num());
    for (const FPuff& Puff : PuffList)
    {
        const float T = Puff.Age / Puff.Life;
        // The sphere mesh is 100 cm across.
        const float Diameter = Puff.Size * (1.0f + 3.5f * T);
        Transforms.Add(FTransform(FQuat::Identity, Puff.Location, FVector(Diameter * 0.01f)));
    }
    const int32 Count = Puffs->GetInstanceCount();
    if (Count != PuffList.Num())
    {
        Puffs->ClearInstances();
        if (!Transforms.IsEmpty())
        {
            Puffs->AddInstances(Transforms, false, true);
        }
    }
    else if (Count > 0)
    {
        Puffs->BatchUpdateInstancesTransforms(0, Transforms, true, false, true);
    }
    for (int32 I = 0; I < PuffList.Num(); ++I)
    {
        const FPuff& Puff = PuffList[I];
        const float T = Puff.Age / Puff.Life;
        const float Fade = FMath::Min(T / 0.12f, 1.0f) * FMath::Pow(1.0f - T, 1.5f);
        const float Data[4] = { Puff.Opacity * Fade, Puff.Color.R, Puff.Color.G, Puff.Color.B };
        Puffs->SetCustomData(I, MakeArrayView(Data, 4), I == PuffList.Num() - 1);
    }
}

void URallyEngineComponent::Emit(const FVector& LocalPoint, const FVector& Spread, const FVector& LocalVelocity, const FLinearColor& Color, float Opacity, float Size)
{
    if (PuffList.Num() >= MaxPuffs)
    {
        return;
    }
    const FTransform Car = GetOwner()->GetActorTransform();
    const FVector Jitter(FMath::FRandRange(-1.0, 1.0) * Spread.X, FMath::FRandRange(-1.0, 1.0) * Spread.Y, FMath::FRandRange(-1.0, 1.0) * Spread.Z);
    FPuff Puff;
    Puff.Location = Car.TransformPosition(LocalPoint + Jitter);
    // It leaves with the car's motion and a push of its own, and the air soon takes it.
    Puff.Velocity = GetOwner()->GetVelocity() * 0.6 + Car.TransformVectorNoScale(LocalVelocity + FMath::VRand() * 15.0);
    Puff.Color = Color;
    Puff.Opacity = Opacity * FMath::FRandRange(0.7f, 1.0f);
    Puff.Size = Size * FMath::FRandRange(0.8f, 1.2f);
    Puff.Life = FMath::FRandRange(1.4f, 2.4f);
    PuffList.Add(Puff);
}
