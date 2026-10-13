#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "RallyDayClock.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;

// The time of day (docs/DAYS.md): a day passes in 45 real minutes, the sun crosses the sky and sets, and the
// night is dark. Sleeping (everyone in the car's seats, Z) skips to the next morning. The server owns it;
// every machine works out the hour from the replicated start and moves its own sun.
UCLASS()
class RALLYSURVIVAL_API ARallyDayClock : public AInfo
{
    GENERATED_BODY()

public:
    static constexpr double DayLengthS = 45.0 * 60.0;
    static constexpr double WakeHour = 6.5;

    ARallyDayClock();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void Tick(float DeltaSeconds) override;
    static ARallyDayClock* Get(const UWorld* World);

    // Day 1 onwards, and the hour of that day (0 .. 24).
    int32 GetDay() const;
    double GetHour() const;
    // Hours since the start of day 1 (for deadlines across midnight).
    double GetAbsoluteHours() const { return (GetDay() - 1) * 24.0 + GetHour(); }
    // "Day 2  06:30"
    FString Describe() const;
    // "11:40" for an absolute hour.
    static FString HourText(double AbsoluteHours);
    // Rain now (0..1): showers on some afternoons (Game/WeatherRules.h), or a forced value.
    double GetRain() const;
    // Server only, for tests and development: force the rain (0..1), or a negative value for the weather's own.
    void SetRainOverride(float Rain) { if (HasAuthority()) { RainOverride = Rain; ForceNetUpdate(); } }
    // Server only: set the clock (a save, a night's sleep).
    void SetTime(int32 Day, double Hour);
    // Server only: on to WakeHour the next morning (or this morning, if it is still before it).
    void SleepUntilMorning();

private:
    double ServerNow() const;
    void UpdateSun();
    // Day and hour at the server time below.
    UPROPERTY(Replicated) int32 StartDay = 1;
    UPROPERTY(Replicated) float StartHour = 9.0f;
    UPROPERTY(Replicated) float StartServerTime = 0.0f;
    UPROPERTY(Replicated) float RainOverride = -1.0f;
    TWeakObjectPtr<AExponentialHeightFog> Fog;
    float FogDensity = -1.0f;
    TWeakObjectPtr<ADirectionalLight> Sun;
    // A faint moon of this machine's own, so the night is dark but not black; the sky stays the sun's.
    TWeakObjectPtr<ADirectionalLight> Moon;
    float SunIntensity = -1.0f;
    bool bLookedForSun = false;
};
