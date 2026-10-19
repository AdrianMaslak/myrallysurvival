#include "Damage/RallyPuddleField.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
    // At most this many puddles; the smallest go first (a parked car's puddle outlasts the spots of a trail).
    constexpr int32 MaxPuddles = 200;
    // Each liquid's look: colour, how shiny (roughness) and how much of the ground it covers.
    struct FLook { FLinearColor Color; float Roughness; float Opacity; };
    const FLook Looks[Rally::Leak::NumLiquids] = {
        { FLinearColor(0.008f, 0.006f, 0.004f), 0.05f, 0.92f },
        { FLinearColor(0.04f, 0.11f, 0.06f), 0.08f, 0.65f },
        { FLinearColor(0.03f, 0.028f, 0.02f), 0.1f, 0.4f },
    };
}

ARallyPuddleField::ARallyPuddleField()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.25f;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    bReplicates = false;
}

ARallyPuddleField* ARallyPuddleField::Get(UWorld* World)
{
    if (!World) return nullptr;
    for (TActorIterator<ARallyPuddleField> It(World); It; ++It) return *It;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<ARallyPuddleField>(ARallyPuddleField::StaticClass(), FTransform::Identity, Params);
}

void ARallyPuddleField::AddDrop(Rally::Leak::ELiquid Liquid, const FVector& Ground, double Litres)
{
    if (!(Litres > 0.0)) return;
    // Into the puddle of that liquid it lands in (or on its edge).
    for (FPuddle& Puddle : Puddles)
    {
        if (Puddle.Liquid != Liquid) continue;
        const double Reach = Rally::Leak::PuddleRadiusCm(Puddle.Litres) * 0.8 + 3.0;
        if (FVector::DistSquared2D(Puddle.Centre, Ground) < Reach * Reach && FMath::Abs(Puddle.Centre.Z - Ground.Z) < 30.0)
        {
            Puddle.Litres += Litres;
            Shape(Puddle);
            return;
        }
    }
    if (Puddles.Num() >= MaxPuddles)
    {
        int32 Smallest = 0;
        for (int32 I = 1; I < Puddles.Num(); ++I)
            if (Puddles[I].Litres < Puddles[Smallest].Litres) Smallest = I;
        Remove(Smallest);
    }
    if (!PuddleMaterial)
        PuddleMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/M_Puddle.M_Puddle"));
    FPuddle Puddle;
    Puddle.Liquid = Liquid;
    Puddle.Centre = Ground;
    Puddle.Litres = Litres;
    // A decal pointing down onto the ground, turned at random so no two spots look the same.
    Puddle.Decal = NewObject<UDecalComponent>(this);
    Puddle.Decal->SetupAttachment(GetRootComponent());
    Puddle.Decal->SetUsingAbsoluteLocation(true);
    Puddle.Decal->SetUsingAbsoluteRotation(true);
    Puddle.Decal->SetWorldLocationAndRotation(Ground, FRotator(-90.0f, 0.0f, FMath::FRandRange(0.0f, 360.0f)));
    Puddle.Decal->SetFadeScreenSize(0.002f);
    if (PuddleMaterial)
    {
        Puddle.Look = UMaterialInstanceDynamic::Create(PuddleMaterial, this);
        const FLook& Look = Looks[static_cast<int>(Liquid)];
        Puddle.Look->SetVectorParameterValue(TEXT("Color"), Look.Color);
        Puddle.Look->SetScalarParameterValue(TEXT("Roughness"), Look.Roughness);
        Puddle.Look->SetScalarParameterValue(TEXT("Seed"), FMath::FRandRange(0.0f, 100.0f));
        Puddle.Decal->SetDecalMaterial(Puddle.Look);
    }
    Puddle.Decal->RegisterComponent();
    DecalRefs.Add(Puddle.Decal);
    Shape(Puddle);
    Puddles.Add(MoveTemp(Puddle));
}

void ARallyPuddleField::Shape(FPuddle& Puddle)
{
    const double Radius = Rally::Leak::PuddleRadiusCm(Puddle.Litres);
    // Projected 15 cm deep, so it lies on uneven ground and stays off the car above it.
    Puddle.Decal->DecalSize = FVector(15.0, Radius, Radius);
    Puddle.Decal->MarkRenderStateDirty();
    if (Puddle.Look)
    {
        // The last of it fades rather than vanishing.
        const float Fade = static_cast<float>(FMath::Clamp(Puddle.Litres / 0.002, 0.25, 1.0));
        Puddle.Look->SetScalarParameterValue(TEXT("Opacity"), Looks[static_cast<int>(Puddle.Liquid)].Opacity * Fade);
    }
}

void ARallyPuddleField::Remove(int32 Index)
{
    if (!Puddles.IsValidIndex(Index)) return;
    if (Puddles[Index].Decal)
    {
        DecalRefs.Remove(Puddles[Index].Decal);
        Puddles[Index].Decal->DestroyComponent();
    }
    Puddles.RemoveAt(Index);
}

void ARallyPuddleField::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Drying: by the area of each puddle.
    for (int32 I = Puddles.Num() - 1; I >= 0; --I)
    {
        FPuddle& Puddle = Puddles[I];
        const double Radius = Rally::Leak::PuddleRadiusCm(Puddle.Litres) * 0.01;
        const double Before = Puddle.Litres;
        Puddle.Litres -= Rally::Leak::DryRate(Puddle.Liquid) * PI * Radius * Radius * DeltaSeconds;
        if (Puddle.Litres < Rally::Leak::GoneLitres) Remove(I);
        else if (Rally::Leak::PuddleRadiusCm(Puddle.Litres) < Rally::Leak::PuddleRadiusCm(Before) - 0.5 || Puddle.Litres < 0.002) Shape(Puddle);
    }
}

int32 ARallyPuddleField::CountPuddles(Rally::Leak::ELiquid Liquid, double* OutLitres) const
{
    int32 Count = 0;
    double Litres = 0.0;
    for (const FPuddle& Puddle : Puddles)
        if (Puddle.Liquid == Liquid) { ++Count; Litres += Puddle.Litres; }
    if (OutLitres) *OutLitres = Litres;
    return Count;
}
