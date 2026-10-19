#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Audio/CarSoundModels.h"
#include "RallySynthComponent.generated.h"

UENUM()
enum class ERallySoundKind : uint8
{
    Engine,
    Road,
    Events,
    Ambience,
};

// Parameters handed from the game thread to the audio thread.
struct FRallySoundShared
{
    FCriticalSection Lock;
    Rally::Audio::FEngineParams Engine;
    Rally::Audio::FRoadParams Road;
    TArray<Rally::Audio::FEventRequest> Events;
    float Volume = 1.0f;
    // The ambience's weather: rain 0..1, and the listener under a car's roof (0..1).
    float Rain = 0.0f;
    float UnderRoof = 0.0f;
};

// One procedural voice from Audio/CarSoundModels.h as a mono synth (docs/SOUND.md). The game thread sets
// its parameters every frame or queues one-shot events; the audio thread renders. Spatialised in the
// world, except the ambience, which plays around the listener.
UCLASS(ClassGroup = (Rally))
class RALLYSURVIVAL_API URallySynthComponent : public USynthComponent
{
    GENERATED_BODY()

public:
    URallySynthComponent(const FObjectInitializer& ObjectInitializer);

    // Before Start(): which voice, and how far away it can be heard (m).
    void Configure(ERallySoundKind InKind, float AudibleMetres, float InnerMetres = 3.0f);
    void SetEngine(const Rally::Audio::FEngineParams& Params);
    void SetRoad(const Rally::Audio::FRoadParams& Params);
    void SetVolume(float InVolume);
    void SetWeather(float Rain, float UnderRoof);
    void Trigger(const Rally::Audio::FEventRequest& Request);

protected:
    virtual bool Init(int32& SampleRate) override;
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
    ERallySoundKind Kind = ERallySoundKind::Engine;
    TSharedPtr<FRallySoundShared, ESPMode::ThreadSafe> Shared;
};
