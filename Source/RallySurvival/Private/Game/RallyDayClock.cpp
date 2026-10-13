#include "Game/RallyDayClock.h"
#include "Game/CalendarRules.h"
#include "Game/WeatherRules.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
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
}

void ARallyDayClock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARallyDayClock, StartDay);
    DOREPLIFETIME(ARallyDayClock, StartHour);
    DOREPLIFETIME(ARallyDayClock, StartServerTime);
    DOREPLIFETIME(ARallyDayClock, RainOverride);
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
            if (It->GetComponent()) { Fog = *It; FogDensity = It->GetComponent()->FogDensity; break; }
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
        if (Air->GetComponent() && Air->GetComponent()->Mobility == EComponentMobility::Movable)
            Air->GetComponent()->SetFogDensity(static_cast<float>(FogDensity * Rally::Weather::FogScale(Rain)));
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
    if (ADirectionalLight* Night = Moon.Get())
    {
        Night->SetActorRotation(FRotator(-30.0, Azimuth, 0.0));
        Night->GetLightComponent()->SetIntensity(static_cast<float>(SunIntensity * 0.035 * FMath::Clamp((1.0 - Strength) * 2.0 - 1.0, 0.0, 1.0)));
    }
}
