#include "Game/RallyDayClock.h"
#include "Game/CalendarRules.h"
#include "Game/WeatherRules.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Player/RallyCharacter.h"
#include "Vehicle/RallyCarPawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

ARallyDayClock::ARallyDayClock()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true;
    SetNetUpdateFrequency(2.0f);
    // Rain streaks: thin translucent sticks, one instance each, placed in world space (UpdateRain). An info
    // actor is hidden in game, which would hide them too.
    SetHidden(false);
    RainDrops = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RainDrops"));
    SetRootComponent(RainDrops);
    RainDrops->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RainDrops->SetGenerateOverlapEvents(false);
    RainDrops->SetCastShadow(false);
    RainDrops->NumCustomDataFloats = 4;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Stick(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Stick.Succeeded()) RainDrops->SetStaticMesh(Stick.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Wet(TEXT("/Game/Vehicles/Octavia/Prototype/M_Prototype_Steam.M_Prototype_Steam"));
    if (Wet.Succeeded()) RainDrops->SetMaterial(0, Wet.Object);
}

void ARallyDayClock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyDayClock, StartDay);
    DOREPLIFETIME(ARallyDayClock, StartHour);
    DOREPLIFETIME(ARallyDayClock, StartServerTime);
    DOREPLIFETIME(ARallyDayClock, RainOverride);
}

void ARallyDayClock::UpdateRain(float DeltaSeconds, double Rain, double Daylight)
{
    // Overcast: the sky's light and colour down, hazier, the air grey (only when the rain changes).
    if (FMath::Abs(Rain - AppliedRain) > 0.005)
    {
        AppliedRain = Rain;
        const float Grey = static_cast<float>(Rally::Weather::SkyScale(Rain));
        if (USkyAtmosphereComponent* Atmosphere = Sky.Get())
        {
            Atmosphere->SetSkyLuminanceFactor(SkyLuminance * Grey);
            Atmosphere->SetMieScatteringScale(SkyMie * static_cast<float>(Rally::Weather::HazeScale(Rain)));
        }
        if (USkyLightComponent* Ambient = SkyLight.Get()) Ambient->SetIntensity(SkyLightIntensity * Grey);
        // A blue sky does not go grey by dimming: the whole picture loses colour instead.
        if (!Overcast.IsValid())
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (APostProcessVolume* Volume = GetWorld()->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, Params))
            {
                Volume->SetReplicates(false);
                Volume->bUnbound = true;
                Volume->Priority = -1.0f;
                Overcast = Volume;
            }
        }
        if (APostProcessVolume* Volume = Overcast.Get())
        {
            const float Wet = static_cast<float>(FMath::Clamp(Rain, 0.0, 1.0));
            Volume->Settings.bOverride_ColorSaturation = true;
            Volume->Settings.ColorSaturation = FVector4(1.0 - 0.6 * Wet, 1.0 - 0.6 * Wet, 1.0 - 0.6 * Wet, 1.0);
            Volume->Settings.bOverride_ColorGain = true;
            Volume->Settings.ColorGain = FVector4(1.0 - 0.2 * Wet, 1.0 - 0.18 * Wet, 1.0 - 0.15 * Wet, 1.0);
            Volume->BlendWeight = Wet > 0.0f ? 1.0f : 0.0f;
        }
        if (AExponentialHeightFog* Air = Fog.Get())
            Air->GetComponent()->SetFogInscatteringColor(FMath::Lerp(FogColor, FLinearColor(0.3f, 0.32f, 0.35f), static_cast<float>(FMath::Clamp(Rain, 0.0, 1.0))));
    }
    // Streaks round the camera of this machine's player: falling, slanted a little by the wind, and
    // started again above when they pass below or are left behind.
    const APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!RainDrops || !PC || !PC->PlayerCameraManager) return;
    const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
    const int32 Want = Rally::Weather::RainDrops(Rain);
    const FVector Fall(150.0, 60.0, -Rally::Weather::RainFallCmS);
    // Close round the eye, where streaks can be seen; further off the grey air stands for them.
    const auto Above = [&Eye](bool bAnyHeight) {
        return Eye + FVector(FMath::FRandRange(-700.0, 700.0), FMath::FRandRange(-700.0, 700.0),
            bAnyHeight ? FMath::FRandRange(-250.0, 450.0) : FMath::FRandRange(350.0, 450.0));
    };
    while (Drops.Num() < Want) Drops.Add(Above(true));
    if (Drops.Num() > Want) Drops.SetNum(Want);
    // The car the player sits in keeps it out: nothing falls inside its body.
    const ARallyCarPawn* Car = Cast<ARallyCarPawn>(PC->GetPawn());
    if (const ARallyCharacter* Seated = Cast<ARallyCharacter>(PC->GetPawn())) Car = Seated->GetSeatedCar();
    const FTransform CarFrame = Car ? Car->GetActorTransform() : FTransform::Identity;
    const FQuat Along = FRotationMatrix::MakeFromZ(-Fall.GetSafeNormal()).ToQuat();
    TArray<FTransform> Transforms;
    Transforms.Reserve(Drops.Num());
    for (FVector& Drop : Drops)
    {
        Drop += Fall * DeltaSeconds;
        if (Drop.Z < Eye.Z - 250.0 || FVector::Dist2D(Drop, Eye) > 1000.0) Drop = Above(false);
        const FVector Inside = CarFrame.InverseTransformPosition(Drop);
        // Nor right in front of the eye, where a streak would be a thick bar across the view.
        const bool bInCar = (Car && FMath::Abs(Inside.X) < 235.0 && FMath::Abs(Inside.Y) < 95.0 && Inside.Z > -70.0 && Inside.Z < 75.0)
            || FVector::Dist(Drop, Eye) < 150.0;
        // A cube 100 cm across: under a centimetre thick, 45 cm long (a drop as the eye smears it).
        Transforms.Add(FTransform(Along, Drop, bInCar ? FVector::ZeroVector : FVector(0.008, 0.008, 0.45)));
    }
    // Faint and grey against the day, dimmer at dusk and at night.
    const float Brightness = static_cast<float>(0.1 + 0.4 * FMath::Clamp(Daylight, 0.0, 1.0));
    if (RainDrops->GetInstanceCount() != Transforms.Num())
    {
        RainDrops->ClearInstances();
        if (!Transforms.IsEmpty()) RainDrops->AddInstances(Transforms, false, true);
        DropBrightness = -1.0f;
    }
    else if (!Transforms.IsEmpty())
    {
        RainDrops->BatchUpdateInstancesTransforms(0, Transforms, true, true, false);
    }
    if (!Transforms.IsEmpty() && FMath::Abs(Brightness - DropBrightness) > 0.02f)
    {
        DropBrightness = Brightness;
        const float Data[4] = { 0.2f, 0.8f * Brightness, 0.83f * Brightness, 0.88f * Brightness };
        for (int32 I = 0; I < Transforms.Num(); ++I) RainDrops->SetCustomData(I, MakeArrayView(Data, 4), I == Transforms.Num() - 1);
    }
}

ARallyDayClock* ARallyDayClock::Get(const UWorld* World)
{
    if (!World) return nullptr;
    TActorIterator<ARallyDayClock> It(const_cast<UWorld*>(World));
    return It ? *It : nullptr;
}

double ARallyDayClock::ServerNow() const
{
    const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
    return State ? State->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

int32 ARallyDayClock::GetDay() const
{
    const double Hours = StartHour + (ServerNow() - StartServerTime) * 24.0 / DayLengthS;
    return StartDay + FMath::FloorToInt(Hours / 24.0);
}

double ARallyDayClock::GetHour() const
{
    const double Hours = StartHour + (ServerNow() - StartServerTime) * 24.0 / DayLengthS;
    return FMath::Fmod(FMath::Max(Hours, 0.0), 24.0);
}

double ARallyDayClock::GetRain() const
{
    return RainOverride >= 0.0f ? FMath::Clamp(static_cast<double>(RainOverride), 0.0, 1.0) : Rally::Weather::Rain(GetDay(), GetHour());
}

FString ARallyDayClock::Describe() const
{
    const double Hour = GetHour();
    return FString::Printf(TEXT("Day %d  %02d:%02d"), GetDay(), FMath::FloorToInt(Hour), FMath::FloorToInt(FMath::Fmod(Hour, 1.0) * 60.0));
}

FString ARallyDayClock::DateText(int32 Day)
{
    const Rally::Calendar::FDate Date = Rally::Calendar::DateOf(Day);
    return FString::Printf(TEXT("%s %d %s %d"), UTF8_TO_TCHAR(Rally::Calendar::WeekdayName(Date.Weekday)), Date.Date,
        UTF8_TO_TCHAR(Rally::Calendar::MonthName(Date.Month)), Date.Year);
}

FString ARallyDayClock::DescribeDate() const
{
    const double Hour = GetHour();
    return FString::Printf(TEXT("%s  %02d:%02d"), *DateText(GetDay()), FMath::FloorToInt(Hour), FMath::FloorToInt(FMath::Fmod(Hour, 1.0) * 60.0));
}

FString ARallyDayClock::HourText(double AbsoluteHours)
{
    const double Hour = FMath::Fmod(FMath::Max(AbsoluteHours, 0.0), 24.0);
    return FString::Printf(TEXT("%02d:%02d"), FMath::FloorToInt(Hour), FMath::FloorToInt(FMath::Fmod(Hour, 1.0) * 60.0));
}

void ARallyDayClock::SetTime(int32 Day, double Hour)
{
    if (!HasAuthority()) return;
    StartDay = FMath::Max(Day, 1);
    StartHour = static_cast<float>(FMath::Clamp(Hour, 0.0, 23.999));
    StartServerTime = static_cast<float>(ServerNow());
    ForceNetUpdate();
}

void ARallyDayClock::SleepUntilMorning()
{
    const double Hour = GetHour();
    const int32 Day = GetDay();
    SetTime(Hour < WakeHour ? Day : Day + 1, WakeHour);
    UE_LOG(LogTemp, Display, TEXT("RALLY_DAY slept until %s"), *Describe());
}

void ARallyDayClock::SleepUntilRally()
{
    SetTime(Rally::Calendar::WakeDay(GetDay(), GetHour(), WakeHour, true), WakeHour);
    UE_LOG(LogTemp, Display, TEXT("RALLY_DAY slept till the rally weekend: %s"), *Describe());
}

void ARallyDayClock::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateSun();
}

void ARallyDayClock::UpdateSun()
{
    if (!bLookedForSun)
    {
        bLookedForSun = true;
        for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
            if (It->GetLightComponent() && It->GetLightComponent()->Mobility == EComponentMobility::Movable) { Sun = *It; break; }
        if (Sun.IsValid()) SunIntensity = Sun->GetLightComponent()->Intensity;
        for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
            if (It->GetComponent())
            {
                // Movable, so the rain can thicken it.
                It->GetComponent()->SetMobility(EComponentMobility::Movable);
                Fog = *It;
                FogDensity = It->GetComponent()->FogDensity;
                break;
            }
        // The sky and its light, as they are without rain.
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            if (USkyAtmosphereComponent* Atmosphere = It->FindComponentByClass<USkyAtmosphereComponent>(); Atmosphere && !Sky.IsValid())
            {
                Sky = Atmosphere;
                SkyLuminance = Atmosphere->SkyLuminanceFactor;
                SkyMie = Atmosphere->MieScatteringScale;
            }
            if (USkyLightComponent* Ambient = It->FindComponentByClass<USkyLightComponent>(); Ambient && !SkyLight.IsValid() && Ambient->Mobility != EComponentMobility::Static)
            {
                SkyLight = Ambient;
                SkyLightIntensity = Ambient->Intensity;
            }
        }
        if (AExponentialHeightFog* Air = Fog.Get()) FogColor = Air->GetComponent()->FogInscatteringLuminance;
        if (Sun.IsValid() && GetNetMode() != NM_DedicatedServer)
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (ADirectionalLight* Light = GetWorld()->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform::Identity, Params))
            {
                Light->SetReplicates(false);
                Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
                Light->GetLightComponent()->SetIntensity(0.0f);
                Light->GetLightComponent()->SetCastShadows(false);
                Light->GetLightComponent()->SetLightColor(FLinearColor(0.55f, 0.65f, 1.0f));
                // Not a sun for the sky: the sky follows the real sun and goes dark at night.
                if (UDirectionalLightComponent* Component = Cast<UDirectionalLightComponent>(Light->GetLightComponent()))
                    Component->SetAtmosphereSunLight(false);
                Moon = Light;
            }
        }
    }
    // Rain thickens the air.
    const double Rain = GetRain();
    if (AExponentialHeightFog* Air = Fog.Get())
        if (Air->GetComponent()) Air->GetComponent()->SetFogDensity(static_cast<float>(FogDensity * Rally::Weather::FogScale(Rain)));
    ADirectionalLight* Light = Sun.Get();
    if (!Light) return;
    // Up at six, highest (60 degrees) at noon, down at six: below the horizon the sky goes dark with it,
    // and the ground is lit only by a low, faint moon.
    const double Hour = GetHour();
    const double Elevation = 60.0 * FMath::Sin(UE_DOUBLE_PI * (Hour - 6.0) / 12.0);
    const double Azimuth = 90.0 + (Hour - 6.0) * 15.0;
    Light->SetActorRotation(FRotator(-Elevation, Azimuth + 180.0, 0.0));
    const double Strength = FMath::Clamp((Elevation + 3.0) / 15.0, 0.0, 1.0);
    Light->GetLightComponent()->SetIntensity(static_cast<float>(SunIntensity * Strength * Rally::Weather::SunScale(Rain)));
    if (GetNetMode() != NM_DedicatedServer) UpdateRain(GetWorld()->GetDeltaSeconds(), Rain, Strength);
    if (ADirectionalLight* Night = Moon.Get())
    {
        Night->SetActorRotation(FRotator(-30.0, Azimuth, 0.0));
        Night->GetLightComponent()->SetIntensity(static_cast<float>(SunIntensity * 0.035 * FMath::Clamp((1.0 - Strength) * 2.0 - 1.0, 0.0, 1.0)));
    }
}
