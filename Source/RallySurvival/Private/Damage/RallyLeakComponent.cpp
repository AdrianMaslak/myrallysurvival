#include "Damage/RallyLeakComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Damage/EngineRules.h"
#include "Damage/RallyDamageComponent.h"
#include "Damage/RallyEngineComponent.h"
#include "Damage/RallyPuddleField.h"
#include "Damage/UndersideRules.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    using Rally::Leak::ELiquid;
    // Car frame (cm, ground below the car's middle at 0, then the body's -80 cm offset): where each leak
    // drips from. The sump's lowest point, the bottom of the radiator core, the fuel tank's seam.
    const FVector AssemblyOffset(0.0, 0.0, -80.0);
    const FVector SumpDrip(151.0, 16.0, 15.0);
    // Under the radiator's lower outlet (the 1.8T set's Radiator_LeakPoint).
    const FVector RadiatorDrip(190.5, 42.4, 38.3);
    const FVector TankDrip(-76.0, 6.0, 18.0);
    constexpr int32 MaxDrops = 64;
    // How each drop is drawn: colour and opacity (M_Prototype_Steam's custom data).
    const FLinearColor DropColors[Rally::Leak::NumLiquids] = {
        FLinearColor(0.05f, 0.035f, 0.02f), FLinearColor(0.35f, 0.75f, 0.45f), FLinearColor(0.65f, 0.6f, 0.35f) };
}

URallyLeakComponent::URallyLeakComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void URallyLeakComponent::Initialize(URallyDamageComponent* InDamage, URallyEngineComponent* InEngine, UInstancedStaticMeshComponent* InDrops,
    const TArray<UMaterialInstanceDynamic*>& InEngineLooks, UMaterialInstanceDynamic* InRadiatorLook)
{
    Damage = InDamage;
    Engine = InEngine;
    DropMeshes = InDrops;
    EngineLooks.Reset();
    for (UMaterialInstanceDynamic* Look : InEngineLooks) EngineLooks.Add(Look);
    RadiatorLook = InRadiatorLook;
    // Oil soaks in near black; coolant only darkens a little and shines.
    for (UMaterialInstanceDynamic* Look : EngineLooks)
        if (Look) Look->SetVectorParameterValue(TEXT("WetTint"), FLinearColor(0.008f, 0.006f, 0.004f));
    if (RadiatorLook)
    {
        RadiatorLook->SetVectorParameterValue(TEXT("WetTint"), FLinearColor(0.02f, 0.03f, 0.025f));
        RadiatorLook->SetScalarParameterValue(TEXT("WetHeight"), 1000.0f);
    }
}

void URallyLeakComponent::Release(ELiquid Liquid, const FVector& CarPoint, const FVector& Spread, double Litres)
{
    AActor* Owner = GetOwner();
    UWorld* World = GetWorld();
    if (!Owner || !World || Drops.Num() >= MaxDrops) return;
    const FTransform Car = Owner->GetActorTransform();
    FDrop Drop;
    Drop.Liquid = Liquid;
    Drop.Litres = Litres;
    Drop.Location = Car.TransformPosition(AssemblyOffset + CarPoint + FVector(FMath::FRandRange(-1.0, 1.0) * Spread.X,
        FMath::FRandRange(-1.0, 1.0) * Spread.Y, FMath::FRandRange(-1.0, 1.0) * Spread.Z));
    // It leaves with the car's motion and falls; where it will land, allowing for that motion.
    Drop.Velocity = Owner->GetVelocity();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyLeakDrop), false, Owner);
    FHitResult Hit;
    const auto GroundBelow = [&](const FVector& From, FVector& OutGround)
    {
        if (!World->LineTraceSingleByChannel(Hit, From, From - FVector(0.0, 0.0, 400.0), ECC_Visibility, Params)) return false;
        OutGround = Hit.ImpactPoint;
        return true;
    };
    FVector Ground;
    if (!GroundBelow(Drop.Location, Ground)) return;
    const double Fall = FMath::Sqrt(2.0 * FMath::Max(Drop.Location.Z - Ground.Z, 0.0) / 980.0);
    const FVector Ahead = Drop.Location + FVector(Drop.Velocity.X, Drop.Velocity.Y, 0.0) * Fall;
    if (!GroundBelow(FVector(Ahead.X, Ahead.Y, Drop.Location.Z), Drop.Ground)) Drop.Ground = Ground;
    Drops.Add(Drop);
}

void URallyLeakComponent::TickLeaks(float Dt)
{
    if (!Damage || !Engine || !GetWorld() || GetNetMode() == NM_DedicatedServer || Dt <= 0.0f) return;
    FRallyPartCondition Sump, Radiator, Tank;
    Damage->GetPartCondition(ERallyPart::OilSump, Sump);
    Damage->GetPartCondition(ERallyPart::Radiator, Radiator);
    Damage->GetPartCondition(ERallyPart::FuelTank, Tank);
    const bool bRunning = Engine->IsRunning();
    // What each leak loses now (the rules the engine drains by), while there is any left to lose.
    const double Rates[Rally::Leak::NumLiquids] = {
        Engine->GetOilL() > 0.0 ? Rally::Underside::OilLeakRate(Sump.Health, bRunning) : 0.0,
        Engine->GetCoolantL() > 0.0 ? Rally::Engine::LeakRate(Radiator.Health, Engine->GetTempC(), bRunning) : 0.0,
        Engine->GetFuelL() > 0.0 ? Rally::Underside::FuelLeakRate(Tank.Health) : 0.0 };
    const FVector Points[Rally::Leak::NumLiquids] = { SumpDrip, RadiatorDrip, TankDrip };
    const FVector Spreads[Rally::Leak::NumLiquids] = { FVector(4.0, 4.0, 0.0), FVector(2.0, 25.0, 0.0), FVector(8.0, 8.0, 0.0) };
    for (int32 L = 0; L < Rally::Leak::NumLiquids; ++L)
    {
        const double PerSecond = Rally::Leak::DropsPerSecondMoving(Rates[L], GetOwner()->GetVelocity().Size() * 0.01);
        if (PerSecond <= 0.0) { Owed[L] = 0.0; continue; }
        Leaked[L] += Rates[L] * Dt;
        Owed[L] = FMath::Min(Owed[L] + PerSecond * Dt, 4.0);
        for (; Owed[L] >= 1.0; Owed[L] -= 1.0)
            Release(static_cast<ELiquid>(L), Points[L], Spreads[L], Rates[L] / PerSecond);
    }

    // Falling: once a drop reaches the ground it joins a puddle there.
    ARallyPuddleField* Field = nullptr;
    for (int32 I = Drops.Num() - 1; I >= 0; --I)
    {
        FDrop& Drop = Drops[I];
        Drop.Velocity.Z -= 980.0 * Dt;
        Drop.Location += Drop.Velocity * Dt;
        if (Drop.Location.Z > Drop.Ground.Z) continue;
        if (!Field) Field = ARallyPuddleField::Get(GetWorld());
        if (Field) Field->AddDrop(Drop.Liquid, Drop.Ground + FVector(0.0, 0.0, 1.0), Drop.Litres);
        Drops.RemoveAtSwap(I);
    }
    DrawDrops();

    // Oil runs down the block from the sump gasket up: the more lost, the higher and blacker it goes (the
    // engine's own space is centred on the crank; the sump's bottom is about 21 cm below it).
    const double Soak = GetOilSoak();
    for (UMaterialInstanceDynamic* Look : EngineLooks)
        if (Look)
        {
            Look->SetScalarParameterValue(TEXT("Wet"), static_cast<float>(FMath::Min(Soak * 2.0, 1.0)));
            Look->SetScalarParameterValue(TEXT("WetHeight"), static_cast<float>(FMath::Lerp(-25.0, 0.0, Soak)));
        }
    // Coolant wets the radiator while it leaks and dries off over a couple of minutes.
    const double Target = Rates[static_cast<int>(ELiquid::Coolant)] > 0.0 ? 1.0 : 0.0;
    RadiatorWet += (Target - RadiatorWet) * (1.0 - FMath::Exp(-Dt / (Target > RadiatorWet ? 5.0 : 120.0)));
    if (RadiatorLook) RadiatorLook->SetScalarParameterValue(TEXT("Wet"), static_cast<float>(RadiatorWet));
}

void URallyLeakComponent::DrawDrops()
{
    if (!DropMeshes) return;
    TArray<FTransform> Transforms;
    Transforms.Reserve(Drops.Num());
    // The sphere mesh is 100 cm across: a drop 1.2 cm wide, drawn 3 cm long as it falls.
    for (const FDrop& Drop : Drops) Transforms.Add(FTransform(FQuat::Identity, Drop.Location, FVector(0.012, 0.012, 0.03)));
    if (DropMeshes->GetInstanceCount() != Drops.Num())
    {
        DropMeshes->ClearInstances();
        if (!Transforms.IsEmpty()) DropMeshes->AddInstances(Transforms, false, true);
    }
    else if (!Transforms.IsEmpty())
    {
        DropMeshes->BatchUpdateInstancesTransforms(0, Transforms, true, false, true);
    }
    for (int32 I = 0; I < Drops.Num(); ++I)
    {
        const FLinearColor& Color = DropColors[static_cast<int>(Drops[I].Liquid)];
        const float Data[4] = { 0.95f, Color.R, Color.G, Color.B };
        DropMeshes->SetCustomData(I, MakeArrayView(Data, 4), I == Drops.Num() - 1);
    }
}
