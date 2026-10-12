#include "Audio/RallySynthComponent.h"
#include "Sound/SoundGenerator.h"

namespace
{
    // Renders one voice on the audio thread from the latest shared parameters.
    class FRallySoundGenerator : public ISoundGenerator
    {
    public:
        FRallySoundGenerator(ERallySoundKind InKind, float SampleRate, TSharedPtr<FRallySoundShared, ESPMode::ThreadSafe> InShared)
            : Kind(InKind), Shared(MoveTemp(InShared)), Engine(SampleRate), Road(SampleRate), Events(SampleRate), Ambience(SampleRate)
        {
        }

        virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
        {
            Rally::Audio::FEngineParams EngineParams;
            Rally::Audio::FRoadParams RoadParams;
            float Volume = 1.0f;
            {
                FScopeLock Lock(&Shared->Lock);
                EngineParams = Shared->Engine;
                RoadParams = Shared->Road;
                Volume = Shared->Volume;
                Pending = MoveTemp(Shared->Events);
                Shared->Events.Reset();
            }
            switch (Kind)
            {
            case ERallySoundKind::Engine: Engine.Render(OutAudio, NumSamples, EngineParams); break;
            case ERallySoundKind::Road: Road.Render(OutAudio, NumSamples, RoadParams); break;
            case ERallySoundKind::Events:
                for (const Rally::Audio::FEventRequest& Request : Pending) Events.Trigger(Request);
                Events.Render(OutAudio, NumSamples, Volume);
                break;
            default: Ambience.Render(OutAudio, NumSamples, Volume); break;
            }
            Pending.Reset();
            return NumSamples;
        }

        virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return 512; }

    private:
        ERallySoundKind Kind;
        TSharedPtr<FRallySoundShared, ESPMode::ThreadSafe> Shared;
        TArray<Rally::Audio::FEventRequest> Pending;
        Rally::Audio::FEngineModel Engine;
        Rally::Audio::FRoadModel Road;
        Rally::Audio::FEventModel Events;
        Rally::Audio::FAmbienceModel Ambience;
    };
}

URallySynthComponent::URallySynthComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bAutoActivate = false;
    Shared = MakeShared<FRallySoundShared, ESPMode::ThreadSafe>();
}

void URallySynthComponent::Configure(ERallySoundKind InKind, float AudibleMetres, float InnerMetres)
{
    Kind = InKind;
    bAllowSpatialization = Kind != ERallySoundKind::Ambience;
    bOverrideAttenuation = true;
    AttenuationOverrides.bAttenuate = bAllowSpatialization;
    AttenuationOverrides.bSpatialize = bAllowSpatialization;
    AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
    AttenuationOverrides.AttenuationShapeExtents = FVector(InnerMetres * 100.0f, 0.0f, 0.0f);
    AttenuationOverrides.FalloffDistance = FMath::Max(AudibleMetres - InnerMetres, 1.0f) * 100.0f;
    AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
}

bool URallySynthComponent::Init(int32& SampleRate)
{
    NumChannels = 1;
    return true;
}

ISoundGeneratorPtr URallySynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
    return ISoundGeneratorPtr(new FRallySoundGenerator(Kind, InParams.SampleRate, Shared));
}

void URallySynthComponent::SetEngine(const Rally::Audio::FEngineParams& Params)
{
    FScopeLock Lock(&Shared->Lock);
    Shared->Engine = Params;
}

void URallySynthComponent::SetRoad(const Rally::Audio::FRoadParams& Params)
{
    FScopeLock Lock(&Shared->Lock);
    Shared->Road = Params;
}

void URallySynthComponent::SetVolume(float InVolume)
{
    FScopeLock Lock(&Shared->Lock);
    Shared->Volume = InVolume;
}

void URallySynthComponent::Trigger(const Rally::Audio::FEventRequest& Request)
{
    FScopeLock Lock(&Shared->Lock);
    if (Shared->Events.Num() < 32) Shared->Events.Add(Request);
}
