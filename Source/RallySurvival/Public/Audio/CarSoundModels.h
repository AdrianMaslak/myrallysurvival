#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Procedural car sound (docs/SOUND.md): the engine and exhaust, the road, one-shot events and the
// countryside, synthesised sample by sample from what the car is doing. No engine dependencies, so the
// standalone preview renderer (Tests/SoundPreview.cpp) writes the same sounds to WAV files.
// Mono float samples in -1..1; every model runs at the sample rate it is constructed with.
namespace Rally::Audio
{
    constexpr float Pi = 3.14159265358979f;

    inline float Clamp01(float X) { return std::isfinite(X) ? std::clamp(X, 0.0f, 1.0f) : 0.0f; }
    inline float Smooth(float Edge0, float Edge1, float X)
    {
        const float T = Clamp01((X - Edge0) / (Edge1 - Edge0));
        return T * T * (3.0f - 2.0f * T);
    }

    // Xorshift: cheap, repeatable noise.
    struct FRandom
    {
        uint32_t State;
        explicit FRandom(uint32_t Seed = 0x9E3779B9u) : State(Seed ? Seed : 1u) {}
        uint32_t Next() { State ^= State << 13; State ^= State >> 17; State ^= State << 5; return State; }
        float Uniform() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }
        float Bipolar() { return Uniform() * 2.0f - 1.0f; }
        float Range(float Lo, float Hi) { return Lo + (Hi - Lo) * Uniform(); }
    };

    struct FOnePole
    {
        float Y = 0.0f;
        float A = 1.0f;
        void SetCutoff(float Hz, float SampleRate) { A = 1.0f - std::exp(-2.0f * Pi * std::max(Hz, 1.0f) / SampleRate); }
        float Low(float X) { Y += A * (X - Y); return Y; }
        float High(float X) { return X - Low(X); }
    };

    // RBJ cookbook biquad, transposed direct form II.
    struct FBiquad
    {
        float B0 = 1.0f, B1 = 0.0f, B2 = 0.0f, A1 = 0.0f, A2 = 0.0f, Z1 = 0.0f, Z2 = 0.0f;
        void Set(float b0, float b1, float b2, float a0, float a1, float a2)
        {
            B0 = b0 / a0; B1 = b1 / a0; B2 = b2 / a0; A1 = a1 / a0; A2 = a2 / a0;
        }
        // Peak gain 1 at the centre.
        void BandPass(float Hz, float Q, float Fs)
        {
            const float W = 2.0f * Pi * std::clamp(Hz, 10.0f, Fs * 0.45f) / Fs, Alpha = std::sin(W) / (2.0f * Q), C = std::cos(W);
            Set(Alpha, 0.0f, -Alpha, 1.0f + Alpha, -2.0f * C, 1.0f - Alpha);
        }
        void LowPass(float Hz, float Q, float Fs)
        {
            const float W = 2.0f * Pi * std::clamp(Hz, 10.0f, Fs * 0.45f) / Fs, Alpha = std::sin(W) / (2.0f * Q), C = std::cos(W);
            Set((1.0f - C) * 0.5f, 1.0f - C, (1.0f - C) * 0.5f, 1.0f + Alpha, -2.0f * C, 1.0f - Alpha);
        }
        void HighPass(float Hz, float Q, float Fs)
        {
            const float W = 2.0f * Pi * std::clamp(Hz, 10.0f, Fs * 0.45f) / Fs, Alpha = std::sin(W) / (2.0f * Q), C = std::cos(W);
            Set((1.0f + C) * 0.5f, -(1.0f + C), (1.0f + C) * 0.5f, 1.0f + Alpha, -2.0f * C, 1.0f - Alpha);
        }
        float Process(float X)
        {
            const float Out = B0 * X + Z1;
            Z1 = B1 * X - A1 * Out + Z2;
            Z2 = B2 * X - A2 * Out;
            return Out;
        }
    };

    struct FDelayLine
    {
        std::vector<float> Buffer;
        int Write = 0;
        void Resize(int Samples) { Buffer.assign(std::max(Samples, 4), 0.0f); Write = 0; }
        void Push(float X) { Buffer[Write] = X; Write = (Write + 1) % static_cast<int>(Buffer.size()); }
        // Linear interpolation, Delay in samples (1 = the last one pushed).
        float Tap(float Delay) const
        {
            const int Size = static_cast<int>(Buffer.size());
            const float D = std::clamp(Delay, 1.0f, static_cast<float>(Size - 2));
            const int I = static_cast<int>(D);
            const float F = D - static_cast<float>(I);
            const float A = Buffer[(Write - I + Size) % Size], B = Buffer[(Write - I - 1 + Size) % Size];
            return A + (B - A) * F;
        }
    };

    // A decaying burst: noise or a ringing resonance, for clicks, pops and impacts.
    struct FBurst
    {
        float Level = 0.0f;
        float Decay = 0.0f;
        FBiquad Filter;
        bool bNoise = true;
        float Phase = 0.0f, Step = 0.0f;
        bool Active() const { return Level > 1e-4f; }
        float Next(FRandom& Random)
        {
            if (!Active()) return 0.0f;
            float Source;
            if (bNoise) Source = Random.Bipolar();
            else { Phase += Step; Source = std::sin(Phase); }
            const float Out = Filter.Process(Source) * Level;
            Level *= Decay;
            return Out;
        }
        void StartNoise(float Amplitude, float Seconds, float Hz, float Q, float Fs)
        {
            bNoise = true; Level = Amplitude; Decay = std::exp(-6.9f / (std::max(Seconds, 0.001f) * Fs));
            Filter = FBiquad(); Filter.BandPass(Hz, Q, Fs);
        }
        void StartTone(float Amplitude, float Seconds, float Hz, float Fs)
        {
            bNoise = false; Level = Amplitude; Decay = std::exp(-6.9f / (std::max(Seconds, 0.001f) * Fs));
            Phase = 0.0f; Step = 2.0f * Pi * Hz / Fs; Filter = FBiquad();
        }
    };

    // ---------------------------------------------------------------------------------------------------
    // Engine and exhaust: a four-cylinder four-stroke (Octavia 1.8T). Every cylinder firing sends a pressure
    // pulse into the exhaust; the pulses ring a pipe (a delay line with an open-end reflection) and pass the
    // silencer, which a damaged or missing exhaust bypasses. The intake roars with load, the turbo whistles
    // and blows off, the valve train ticks, a worn engine knocks, misfires leave gaps and unburnt fuel pops
    // in the exhaust on the overrun. Below 450 rpm with the ignition on it is the starter turning it over.

    struct FEngineParams
    {
        float Rpm = 0.0f;
        // Share of full power being made, 0..1 (what the throttle is asking of it).
        float Load = 0.0f;
        bool bRunning = false;
        float EngineHealth = 1.0f;
        // 0 sealed .. 1 blowing badly (hanging, holed); bExhaustOff: no silencer at all.
        float ExhaustLeak = 0.0f;
        bool bExhaustOff = false;
        // Random misfires per second (a failing battery or engine) and a cut-out happening now.
        float MisfireRate = 0.0f;
        bool bCutOut = false;
        // 0 outside, 1 in the cabin: the firewall and bulkheads take the top off.
        float Interior = 0.0f;
        float Volume = 1.0f;
    };

    class FEngineModel
    {
    public:
        explicit FEngineModel(float InSampleRate = 48000.0f, uint32_t Seed = 1u) : Fs(InSampleRate), Random(Seed)
        {
            // Header to collector and the front pipe: round trips of about 1.1 m and 3.6 m at 520 m/s.
            Header.Resize(static_cast<int>(Fs * 0.02f));
            Pipe.Resize(static_cast<int>(Fs * 0.03f));
            HeaderDelay = 2.0f * 1.1f / 520.0f * Fs;
            PipeDelay = 2.0f * 3.6f / 520.0f * Fs;
            HeaderLoss.SetCutoff(2600.0f, Fs);
            PipeLoss.SetCutoff(1400.0f, Fs);
            Body1.BandPass(115.0f, 1.6f, Fs);
            Body2.BandPass(260.0f, 2.5f, Fs);
            Body3.BandPass(540.0f, 3.0f, Fs);
            SilencerLow.LowPass(520.0f, 0.75f, Fs);
            SilencerLow2.LowPass(1100.0f, 0.7f, Fs);
            SilencerCavity.BandPass(85.0f, 2.0f, Fs);
            TailHigh.HighPass(28.0f, 0.7f, Fs);
            Rasp.HighPass(900.0f, 0.7f, Fs);
            Intake.BandPass(420.0f, 3.0f, Fs);
            IntakeNoise.BandPass(1300.0f, 0.9f, Fs);
            Ticks.BandPass(4200.0f, 2.5f, Fs);
            WhistleHiss.BandPass(5500.0f, 1.2f, Fs);
            BlowOffFilter.BandPass(2600.0f, 0.8f, Fs);
            CabinExhaust.LowPass(700.0f, 0.7f, Fs);
            CabinOther.LowPass(1800.0f, 0.7f, Fs);
            DcBlock.HighPass(20.0f, 0.7f, Fs);
            FlowNoise.BandPass(1700.0f, 0.6f, Fs);
            IntakeSmooth.SetCutoff(1500.0f, Fs);
            for (int C = 0; C < 4; ++C) CylinderGain[C] = 1.0f + 0.07f * Random.Bipolar();
        }

        void Render(float* Out, int Count, const FEngineParams& Target)
        {
            for (int N = 0; N < Count; ++N)
            {
                // Glide towards the target so block-rate parameter steps do not click.
                const float Glide = 1.0f - std::exp(-1.0f / (0.012f * Fs));
                P.Rpm += (std::max(Target.Rpm, 0.0f) - P.Rpm) * Glide;
                P.Load += (Clamp01(Target.Load) - P.Load) * Glide;
                P.ExhaustLeak += (Clamp01(Target.ExhaustLeak) - P.ExhaustLeak) * Glide;
                P.Interior += (Clamp01(Target.Interior) - P.Interior) * Glide;
                P.Volume += (Clamp01(Target.Volume) - P.Volume) * Glide;
                P.EngineHealth = Clamp01(Target.EngineHealth);
                P.MisfireRate = std::max(Target.MisfireRate, 0.0f);
                P.bRunning = Target.bRunning;
                P.bExhaustOff = Target.bExhaustOff;
                P.bCutOut = Target.bCutOut;
                Out[N] = Sample();
            }
        }

    private:
        float Fs;
        FRandom Random;
        FEngineParams P;
        double Crank = 0.0;         // revolutions
        int NextFiring = 1;         // in half revolutions
        float CylinderGain[4] = {};
        struct FPulse { float Age = 1e9f; float Width = 1.0f; float Amplitude = 0.0f; };
        FPulse Pulses[6];
        int PulseSlot = 0;
        FDelayLine Header, Pipe;
        float HeaderDelay = 1.0f, PipeDelay = 1.0f;
        FOnePole HeaderLoss, PipeLoss;
        FBiquad Body1, Body2, Body3, SilencerLow, SilencerLow2, SilencerCavity, TailHigh, Rasp, Intake, IntakeNoise, Ticks;
        FBiquad WhistleHiss, BlowOffFilter, CabinExhaust, CabinOther, DcBlock, FlowNoise;
        FOnePole IntakeSmooth;
        float FlowEnvelope = 0.0f;
        FBurst Pops[4], Knock[2], Tick[2], Crack[4], Thump[4];
        float TurboSpeed = 0.0f, WhistlePhase = 0.0f, StarterPhase = 0.0f, WhinePhase = 0.0f;
        float LoadHistory = 0.0f, BlowOff = 0.0f, Overrun = 0.0f;
        float PopEnergy = 0.0f;      // unburnt fuel waiting in the exhaust
        float IntakeEnvelope = 0.0f;
        int BurstSlot = 0;

        void AddPulse(float Amplitude, float Width)
        {
            Pulses[PulseSlot] = { 0.0f, std::max(Width, 8.0f), Amplitude };
            PulseSlot = (PulseSlot + 1) % 6;
        }

        // Exhaust pressure pulse: a fast rise and a slower fall, with a small rebound.
        static float PulseShape(float T)
        {
            if (T < 0.0f || T > 1.0f) return 0.0f;
            const float Rise = 0.18f;
            const float Main = T < Rise ? std::sin(0.5f * Pi * T / Rise) : std::exp(-4.0f * (T - Rise) / (1.0f - Rise));
            return Main - 0.25f * std::sin(Pi * T) * T;
        }

        void Pop(float Strength)
        {
            // Unburnt fuel lighting off in the exhaust: a sharp crack, a thump, and a burst into the pipe.
            FBurst& Crack0 = Crack[BurstSlot % 4];
            Crack0.StartNoise(0.55f * Strength, Random.Range(0.006f, 0.014f), Random.Range(1800.0f, 3200.0f), 0.6f, Fs);
            FBurst& Thump0 = Thump[BurstSlot % 4];
            Thump0.StartTone(0.45f * Strength, Random.Range(0.04f, 0.07f), Random.Range(55.0f, 80.0f), Fs);
            ++BurstSlot;
            AddPulse(2.2f * Strength, 0.0016f * Fs);
        }

        float Sample()
        {
            const float Dt = 1.0f / Fs;
            const float Rpm = P.Rpm;
            const bool bCranking = P.bRunning && Rpm > 40.0f && Rpm < 450.0f;
            const bool bFiring = P.bRunning && Rpm >= 450.0f;
            const float Damage = 1.0f - P.EngineHealth;

            // Overrun: closed throttle at speed. Fuel is mostly cut; some of what gets through burns late.
            LoadHistory += (P.Load - LoadHistory) * (Dt / 0.15f);
            const bool bLifted = LoadHistory - P.Load > 0.35f;
            Overrun += ((bFiring && P.Load < 0.06f && Rpm > 2600.0f ? 1.0f : 0.0f) - Overrun) * (Dt / 0.05f);

            // Turbo: spools with load and revs, slowly; a sudden lift dumps the boost through the blow-off valve.
            const float Boost = Clamp01(P.Load * Smooth(1500.0f, 4200.0f, Rpm));
            TurboSpeed += (Boost - TurboSpeed) * (Dt / (Boost > TurboSpeed ? 0.6f : 1.6f));
            if (bLifted && TurboSpeed > 0.45f && BlowOff < 0.05f) BlowOff = TurboSpeed;
            BlowOff *= std::exp(-Dt / 0.22f);

            // Firing events every half revolution, cylinders in firing order 1-3-4-2.
            Crank += static_cast<double>(Rpm) / 60.0 * Dt;
            const float Interval = Rpm > 1.0f ? 30.0f / Rpm * Fs : Fs;
            while (Crank * 2.0 >= static_cast<double>(NextFiring))
            {
                const int Cylinder = NextFiring % 4;
                ++NextFiring;
                const float Jitter = 1.0f + (0.06f + 0.25f * Damage * Damage) * Random.Bipolar();
                if (bFiring)
                {
                    const bool bMisfire = P.bCutOut || Random.Uniform() < (P.MisfireRate + 1.5f * Damage * Damage) * (30.0f / std::max(Rpm, 300.0f));
                    if (bMisfire)
                    {
                        // Only compression leaves the cylinder; the charge goes into the hot exhaust.
                        AddPulse(0.12f * Jitter, 0.5f * Interval);
                        PopEnergy += 0.6f;
                    }
                    else
                    {
                        const float Strength = (0.22f + 0.78f * P.Load) * (1.0f - 0.55f * Overrun) * CylinderGain[Cylinder] * Jitter;
                        AddPulse(Strength, 0.42f * Interval * (1.0f + 0.1f * Random.Bipolar()));
                        IntakeEnvelope = std::max(IntakeEnvelope, 0.4f + 0.6f * P.Load);
                        Tick[Cylinder % 2].StartNoise(0.05f, 0.0012f, 4200.0f, 2.0f, Fs);
                    }
                    // Late burning on the overrun: rare on a healthy car, common with a damaged engine or a
                    // leaking exhaust that lets air in.
                    const float PopChance = Overrun * (0.0008f + 0.03f * P.ExhaustLeak + (P.bExhaustOff ? 0.04f : 0.0f) + 0.04f * Damage);
                    if (Random.Uniform() < PopChance) PopEnergy += 1.0f;
                    if (PopEnergy > 0.9f && Random.Uniform() < 0.5f)
                    {
                        Pop(std::min(PopEnergy, 1.6f) * Random.Range(0.6f, 1.0f));
                        PopEnergy = 0.0f;
                    }
                    // A worn big end knocks once a revolution, louder under load.
                    if (Damage > 0.35f && Cylinder == 0)
                        Knock[NextFiring % 2].StartNoise(0.35f * Smooth(0.35f, 0.9f, Damage) * (0.4f + 0.6f * P.Load), 0.02f, 1150.0f, 9.0f, Fs);
                }
                else if (P.Rpm > 40.0f)
                {
                    // Turning without firing (starter, or running down after switching off): compression only.
                    AddPulse((bCranking ? 0.22f : 0.08f) * Jitter * std::min(Rpm / 300.0f, 1.0f), 0.5f * Interval);
                }
            }
            PopEnergy *= std::exp(-Dt / 0.4f);

            float Excitation = 0.0f;
            for (FPulse& Pulse : Pulses)
            {
                if (Pulse.Age < Pulse.Width)
                {
                    Excitation += Pulse.Amplitude * PulseShape(Pulse.Age / Pulse.Width);
                    Pulse.Age += 1.0f;
                }
            }
            // Gas rushing out is turbulent: noise riding on each pulse, breathier with load.
            FlowEnvelope += (std::abs(Excitation) - FlowEnvelope) * (Dt / 0.002f);
            Excitation += FlowEnvelope * (0.35f + 0.3f * P.Load) * FlowNoise.Process(Random.Bipolar());

            // Header: the pulse rings between the valve and the collector (open end, inverted reflection).
            const float HeaderOut = Excitation - 0.42f * HeaderLoss.Low(Header.Tap(HeaderDelay));
            Header.Push(HeaderOut);
            // Front pipe to the silencer, a weaker reflection.
            const float PipeOut = HeaderOut - 0.3f * PipeLoss.Low(Pipe.Tap(PipeDelay));
            Pipe.Push(PipeOut);
            const float Pipework = PipeOut + 0.7f * Body1.Process(PipeOut) + 0.5f * Body2.Process(PipeOut) + 0.3f * Body3.Process(PipeOut);
            // Silencer: takes the top off and adds its own boom; a leak lets the raw pipe through.
            const float Silenced = SilencerLow2.Process(SilencerLow.Process(Pipework)) * 0.75f + 0.5f * SilencerCavity.Process(Pipework);
            const float Leak = P.bExhaustOff ? 1.0f : 0.8f * P.ExhaustLeak;
            float Exhaust = (1.0f - Leak) * Silenced + Leak * (2.0f * Pipework + 1.2f * Rasp.Process(Pipework));
            for (FBurst& B : Crack) Exhaust += B.Next(Random);
            for (FBurst& B : Thump) Exhaust += B.Next(Random);
            Exhaust = TailHigh.Process(Exhaust);

            // Intake roar and the airbox, with load.
            IntakeEnvelope *= std::exp(-Dt / std::max(Interval / Fs * 0.6f, 0.002f));
            const float IntakeSound = 0.12f * Intake.Process(IntakeSmooth.Low(Excitation)) * (0.3f + P.Load)
                + 0.05f * IntakeNoise.Process(Random.Bipolar()) * IntakeEnvelope * P.Load;

            // Mechanical: valve train ticks, alternator whine, knocking.
            float Mechanical = 0.0f;
            for (FBurst& B : Tick) Mechanical += B.Next(Random);
            for (FBurst& B : Knock) Mechanical += B.Next(Random);
            WhinePhase += 2.0f * Pi * (Rpm / 60.0f * 2.7f * 6.0f) * Dt;
            if (WhinePhase > 2.0f * Pi) WhinePhase -= 2.0f * Pi;
            Mechanical += 0.004f * std::sin(WhinePhase) * Smooth(300.0f, 1200.0f, Rpm);

            // Turbo whistle and hiss; the blow-off valve's rush.
            WhistlePhase += 2.0f * Pi * (2400.0f + 6500.0f * TurboSpeed) * Dt;
            if (WhistlePhase > 2.0f * Pi) WhistlePhase -= 2.0f * Pi;
            const float Turbo = 0.022f * TurboSpeed * TurboSpeed * std::sin(WhistlePhase)
                + 0.03f * TurboSpeed * P.Load * WhistleHiss.Process(Random.Bipolar())
                + 0.35f * BlowOff * BlowOffFilter.Process(Random.Bipolar());

            // Starter motor: a geared whine that labours on each compression.
            float Starter = 0.0f;
            if (bCranking)
            {
                const float Labour = 0.5f + 0.5f * std::cos(static_cast<float>(Crank * 4.0 * Pi));
                StarterPhase += 2.0f * Pi * (160.0f - 35.0f * Labour) * Dt;
                if (StarterPhase > 2.0f * Pi) StarterPhase -= 2.0f * Pi;
                const float Saw = StarterPhase / Pi - 1.0f;
                Starter = 0.06f * (0.6f * Saw + 0.4f * std::sin(3.0f * StarterPhase)) * (0.7f + 0.3f * Labour);
            }

            // In the cabin the exhaust is muffled and what comes through the bulkhead dominates.
            const float ExhaustGain = 1.0f - 0.55f * P.Interior;
            const float CabinMix = P.Interior;
            const float ExhaustHeard = (1.0f - CabinMix) * Exhaust + CabinMix * CabinExhaust.Process(Exhaust);
            const float Under = IntakeSound + Mechanical + Turbo + Starter;
            const float UnderHeard = (1.0f - CabinMix) * Under + CabinMix * 1.2f * CabinOther.Process(Under);
            float Mix = 0.55f * ExhaustGain * ExhaustHeard + UnderHeard;
            Mix = DcBlock.Process(Mix) * P.Volume;
            // Soft limit so pops and full load never clip harshly.
            return std::tanh(Mix * 1.2f) / 1.2f;
        }
    };

    // ---------------------------------------------------------------------------------------------------
    // Road: tyres rolling (gravel crunch or tarmac roar), sliding (gravel spray, tarmac squeal), stones
    // hitting the floor, wind, and something metal dragging on the ground.

    struct FRoadParams
    {
        float SpeedMs = 0.0f;
        // Share of wheels on the ground, and how loose their surface is (0 tarmac .. 1 gravel).
        float Contact = 1.0f;
        float Loose = 0.0f;
        // Sliding: worst wheel's slip as a share of the tyre's peak (1 = at the limit).
        float Slide = 0.0f;
        // Metal on the ground (a hanging exhaust, a hub without its wheel), 0..1.
        float Scrape = 0.0f;
        float Interior = 0.0f;
        float Volume = 1.0f;
    };

    class FRoadModel
    {
    public:
        explicit FRoadModel(float InSampleRate = 48000.0f, uint32_t Seed = 7u) : Fs(InSampleRate), Random(Seed)
        {
            Crunch.BandPass(2100.0f, 0.8f, Fs);
            CrunchLow.BandPass(650.0f, 1.0f, Fs);
            Rumble.LowPass(140.0f, 0.8f, Fs);
            Roar.BandPass(850.0f, 0.9f, Fs);
            Spray.HighPass(3200.0f, 0.7f, Fs);
            SquealNoise.BandPass(1500.0f, 4.0f, Fs);
            for (int I = 0; I < 3; ++I) ScrapeRing[I].BandPass(1100.0f * (I + 1) + 230.0f * I, 14.0f, Fs);
            ScrapeHigh.HighPass(1800.0f, 0.7f, Fs);
            Cabin.LowPass(2300.0f, 0.7f, Fs);
            WindTone.SetCutoff(400.0f, Fs);
        }

        void Render(float* Out, int Count, const FRoadParams& Target)
        {
            for (int N = 0; N < Count; ++N)
            {
                const float Glide = 1.0f - std::exp(-1.0f / (0.03f * Fs));
                P.SpeedMs += (std::max(Target.SpeedMs, 0.0f) - P.SpeedMs) * Glide;
                P.Contact += (Clamp01(Target.Contact) - P.Contact) * Glide;
                P.Loose += (Clamp01(Target.Loose) - P.Loose) * Glide;
                P.Slide += (std::clamp(Target.Slide, 0.0f, 3.0f) - P.Slide) * Glide;
                P.Scrape += (Clamp01(Target.Scrape) - P.Scrape) * Glide;
                P.Interior += (Clamp01(Target.Interior) - P.Interior) * Glide;
                P.Volume += (Clamp01(Target.Volume) - P.Volume) * Glide;
                Out[N] = Sample();
            }
        }

    private:
        float Fs;
        FRandom Random;
        FRoadParams P;
        FBiquad Crunch, CrunchLow, Rumble, Roar, Spray, SquealNoise, ScrapeRing[3], ScrapeHigh, Cabin;
        FOnePole WindTone;
        FBurst Grains[8], Pings[4];
        int GrainSlot = 0, PingSlot = 0;
        float SquealPhase[3] = {}, SquealDrift[3] = {}, ScrapeLevel = 0.0f;

        float Sample()
        {
            const float Dt = 1.0f / Fs;
            const float Speed = P.SpeedMs;
            const float Rolling = Smooth(0.3f, 3.0f, Speed) * P.Contact;
            const float Slide = Smooth(0.7f, 1.6f, P.Slide) * P.Contact;

            // Gravel: individual stones crunching under the tread, faster with speed and sliding.
            const float GrainRate = (300.0f + 140.0f * std::min(Speed, 30.0f)) * (1.0f + 2.5f * Slide) * Rolling * P.Loose;
            if (Random.Uniform() < GrainRate * Dt)
            {
                Grains[GrainSlot].StartNoise(Random.Range(0.1f, 0.35f) * (0.6f + 0.6f * Slide), Random.Range(0.0008f, 0.003f),
                    Random.Range(900.0f, 3800.0f), 1.2f, Fs);
                GrainSlot = (GrainSlot + 1) % 8;
            }
            float Gravel = 0.0f;
            for (FBurst& G : Grains) Gravel += G.Next(Random);
            const float Noise = Random.Bipolar();
            Gravel += 0.08f * CrunchLow.Process(Noise) * Rolling * P.Loose * Smooth(0.0f, 25.0f, Speed);
            // Stones flicked up against the floor.
            const float PingRate = Speed * 0.35f * P.Loose * (0.2f + Slide) * P.Contact;
            if (Random.Uniform() < PingRate * Dt)
            {
                Pings[PingSlot].StartTone(Random.Range(0.03f, 0.09f), Random.Range(0.02f, 0.06f), Random.Range(1800.0f, 5200.0f), Fs);
                PingSlot = (PingSlot + 1) % 4;
            }
            for (FBurst& B : Pings) Gravel += B.Next(Random);
            const float SprayHiss = 0.12f * Spray.Process(Noise) * Slide * P.Loose;

            // Tarmac: a smooth roar, and a squeal at the limit.
            const float RoarLevel = std::pow(Smooth(0.0f, 35.0f, Speed), 1.5f) * Rolling * (1.0f - P.Loose);
            const float Tarmac = 0.22f * Roar.Process(Noise) * RoarLevel;
            float Squeal = 0.0f;
            const float SquealLevel = Slide * (1.0f - P.Loose) * Smooth(1.0f, 6.0f, Speed);
            if (SquealLevel > 0.001f)
            {
                static const float Base[3] = { 880.0f, 1320.0f, 1790.0f };
                for (int I = 0; I < 3; ++I)
                {
                    SquealDrift[I] += (Random.Bipolar() * 60.0f - SquealDrift[I]) * (Dt / 0.08f);
                    SquealPhase[I] += 2.0f * Pi * (Base[I] + SquealDrift[I]) * Dt;
                    if (SquealPhase[I] > 2.0f * Pi) SquealPhase[I] -= 2.0f * Pi;
                    Squeal += std::sin(SquealPhase[I]) * (I == 0 ? 0.5f : 0.3f);
                }
                Squeal = (0.06f * Squeal + 0.05f * SquealNoise.Process(Noise)) * SquealLevel;
            }
            const float Low = 0.25f * Rumble.Process(Noise) * Smooth(0.0f, 30.0f, Speed) * P.Contact * (0.5f + 0.5f * P.Loose);

            // Wind over the body, rising with the square of speed and brightening.
            WindTone.SetCutoff(250.0f + 45.0f * Speed, Fs);
            const float WindLevel = std::min(Speed * Speed / 1600.0f, 1.2f);
            const float Wind = 0.18f * WindTone.Low(Random.Bipolar()) * WindLevel * (1.0f - 0.6f * P.Interior);

            // Metal dragging: rough, ringing, flickering with the ground.
            ScrapeLevel += ((Random.Uniform() < 0.5f ? 1.0f : 0.25f) - ScrapeLevel) * (Dt / 0.004f);
            float Scrape = 0.0f;
            const float ScrapeAmount = P.Scrape * Smooth(0.2f, 4.0f, Speed);
            if (ScrapeAmount > 0.001f)
            {
                const float Grit = Random.Bipolar() * ScrapeLevel;
                for (FBiquad& Ring : ScrapeRing) Scrape += 0.6f * Ring.Process(Grit);
                Scrape += 0.25f * ScrapeHigh.Process(Grit);
                Scrape *= 0.5f * ScrapeAmount;
            }

            float Mix = 0.7f * Gravel + SprayHiss + Tarmac + Squeal + Low + Scrape;
            Mix = (1.0f - P.Interior) * Mix + P.Interior * 0.8f * Cabin.Process(Mix);
            Mix = (Mix + Wind) * P.Volume;
            return std::tanh(Mix * 1.2f) / 1.2f;
        }
    };

    // ---------------------------------------------------------------------------------------------------
    // One-shot events: crashes, hits under the floor, gear changes, the starter solenoid.

    // CrankNoStart: the starter turning an engine that will not catch (no fuel).
    enum class EEvent : uint8_t { Impact, Underside, GearShift, StarterClick, Seized, Shudder, CrankNoStart };

    struct FEventRequest
    {
        EEvent Kind = EEvent::Impact;
        // 0..1: how hard.
        float Strength = 1.0f;
    };

    class FEventModel
    {
    public:
        explicit FEventModel(float InSampleRate = 48000.0f, uint32_t Seed = 11u) : Fs(InSampleRate), Random(Seed) {}

        void Trigger(const FEventRequest& Request)
        {
            const float S = Clamp01(Request.Strength);
            switch (Request.Kind)
            {
            case EEvent::Impact:
                // Body thump, crunching sheet metal ringing at random panel modes, crackles of crumpling, and
                // glass in a big one.
                Start(Thump(), 0.9f * S, 0.12f + 0.25f * S, Random.Range(50.0f, 75.0f), true);
                for (int I = 0; I < 5; ++I)
                    StartNoise(0.25f * S * Random.Range(0.6f, 1.0f), 0.05f + 0.3f * S * Random.Range(0.5f, 1.0f),
                        Random.Range(180.0f, 2600.0f), Random.Range(6.0f, 18.0f));
                StartNoise(0.5f * S, 0.03f + 0.08f * S, 1500.0f, 0.5f);
                Crumple = std::max(Crumple, S);
                CrumpleTime = 0.08f + 0.35f * S;
                if (S > 0.6f) GlassLeft = static_cast<int>(10 + 25 * (S - 0.6f) / 0.4f);
                break;
            case EEvent::Underside:
                Start(Thump(), 0.7f * S, 0.12f, Random.Range(40.0f, 55.0f), true);
                StartNoise(0.35f * S, 0.12f, 1900.0f, 6.0f);
                StartNoise(0.3f * S, 0.05f, 900.0f, 0.8f);
                break;
            case EEvent::GearShift:
                Start(Thump(), 0.12f * S, 0.05f, 95.0f, true);
                StartNoise(0.08f * S, 0.012f, 2400.0f, 3.0f);
                break;
            case EEvent::StarterClick:
                StartNoise(0.3f, 0.004f, 3000.0f, 0.8f);
                StartNoise(0.12f, 0.03f, 2350.0f, 20.0f);
                break;
            case EEvent::Seized:
                StartNoise(0.3f, 0.004f, 3000.0f, 0.8f);
                Start(Thump(), 0.5f, 0.18f, 48.0f, true);
                StrainLeft = 0.45f;
                break;
            case EEvent::Shudder:
                Start(Thump(), 0.25f * S, 0.25f, 18.0f + 10.0f * S, true);
                break;
            case EEvent::CrankNoStart:
                StartNoise(0.2f, 0.004f, 3000.0f, 0.8f);
                CrankLeft = 1.6f;
                CrankAngle = 0.0f;
                break;
            }
        }

        void Render(float* Out, int Count, float Volume = 1.0f)
        {
            const float Dt = 1.0f / Fs;
            for (int N = 0; N < Count; ++N)
            {
                float Mix = 0.0f;
                for (FBurst& B : Bursts) Mix += B.Next(Random);
                if (CrumpleTime > 0.0f)
                {
                    CrumpleTime -= Dt;
                    if (Random.Uniform() < (60.0f + 300.0f * Crumple) * Dt)
                        StartNoise(Random.Range(0.05f, 0.2f) * Crumple, Random.Range(0.002f, 0.01f), Random.Range(600.0f, 4000.0f), 3.0f);
                }
                if (GlassLeft > 0 && Random.Uniform() < 60.0f * Dt)
                {
                    --GlassLeft;
                    Start(Next(), Random.Range(0.03f, 0.1f), Random.Range(0.03f, 0.15f), Random.Range(3200.0f, 8200.0f), true);
                }
                if (CrankLeft > 0.0f)
                {
                    // Starter whine labouring on each compression, and the chuff of each one.
                    CrankLeft -= Dt;
                    const float Previous = CrankAngle;
                    CrankAngle += 4.4f * Dt * 2.0f;
                    if (std::floor(CrankAngle) != std::floor(Previous))
                        StartNoise(0.12f, 0.06f, Random.Range(140.0f, 220.0f), 1.5f);
                    const float Labour = 0.5f + 0.5f * std::cos(2.0f * Pi * CrankAngle);
                    StrainPhase += 2.0f * Pi * (160.0f - 35.0f * Labour) * Dt;
                    if (StrainPhase > 2.0f * Pi) StrainPhase -= 2.0f * Pi;
                    Mix += 0.06f * (0.6f * (StrainPhase / Pi - 1.0f) + 0.4f * std::sin(3.0f * StrainPhase)) * (0.7f + 0.3f * Labour)
                        * std::min(CrankLeft * 5.0f, 1.0f);
                }
                if (StrainLeft > 0.0f)
                {
                    StrainLeft -= Dt;
                    StrainPhase += 2.0f * Pi * 70.0f * Dt;
                    if (StrainPhase > 2.0f * Pi) StrainPhase -= 2.0f * Pi;
                    Mix += 0.05f * (StrainPhase / Pi - 1.0f) * std::min(StrainLeft * 4.0f, 1.0f);
                }
                Out[N] = std::tanh(Mix * Volume * 1.3f) / 1.3f;
            }
        }

    private:
        float Fs;
        FRandom Random;
        FBurst Bursts[24];
        int Slot = 0;
        float Crumple = 0.0f, CrumpleTime = 0.0f, StrainLeft = 0.0f, StrainPhase = 0.0f, CrankLeft = 0.0f, CrankAngle = 0.0f;
        int GlassLeft = 0;

        FBurst& Next() { FBurst& B = Bursts[Slot]; Slot = (Slot + 1) % 24; return B; }
        FBurst& Thump() { return Next(); }
        void Start(FBurst& B, float Amplitude, float Seconds, float Hz, bool bTone)
        {
            if (bTone) B.StartTone(Amplitude, Seconds, Hz, Fs);
            else B.StartNoise(Amplitude, Seconds, Hz, 1.0f, Fs);
        }
        void StartNoise(float Amplitude, float Seconds, float Hz, float Q) { Next().StartNoise(Amplitude, Seconds, Hz, Q, Fs); }
    };

    // ---------------------------------------------------------------------------------------------------
    // The countryside around the listener: wind that gusts through grass and trees, and birds now and then.

    class FAmbienceModel
    {
    public:
        explicit FAmbienceModel(float InSampleRate = 48000.0f, uint32_t Seed = 23u) : Fs(InSampleRate), Random(Seed)
        {
            Leaves.HighPass(2200.0f, 0.7f, Fs);
            Air.SetCutoff(400.0f, Fs);
            NextSong = Random.Range(1.0f, 4.0f);
        }

        void Render(float* Out, int Count, float Volume = 1.0f)
        {
            const float Dt = 1.0f / Fs;
            for (int N = 0; N < Count; ++N)
            {
                // Gusts: a slow random walk of strength.
                GustTarget += (Random.Bipolar() * 0.6f) * Dt;
                GustTarget = std::clamp(GustTarget, 0.15f, 1.0f);
                Gust += (GustTarget - Gust) * (Dt / 1.5f);
                Air.SetCutoff(180.0f + 520.0f * Gust, Fs);
                const float Noise = Random.Bipolar();
                float Mix = 0.09f * Gust * Air.Low(Noise) + 0.018f * Gust * Gust * Leaves.Process(Noise);

                // Birds: a phrase of chirps every few seconds.
                NextSong -= Dt;
                if (NextSong <= 0.0f)
                {
                    NotesLeft = static_cast<int>(Random.Range(2.0f, 8.0f));
                    SongPitch = Random.Range(2600.0f, 5200.0f);
                    SongSweep = Random.Range(-0.35f, 0.45f);
                    SongLevel = Random.Range(0.012f, 0.035f);
                    NoteGap = 0.0f;
                    NextSong = Random.Range(3.0f, 12.0f);
                }
                if (NoteLeft > 0.0f)
                {
                    NoteLeft -= Dt;
                    const float T = 1.0f - NoteLeft / NoteLength;
                    const float Pitch = SongPitch * (1.0f + SongSweep * T) * (1.0f + 0.05f * std::sin(2.0f * Pi * 38.0f * T * NoteLength));
                    BirdPhase += 2.0f * Pi * Pitch * Dt;
                    if (BirdPhase > 2.0f * Pi) BirdPhase -= 2.0f * Pi;
                    Mix += SongLevel * std::sin(BirdPhase) * std::sin(Pi * T);
                }
                else if (NotesLeft > 0)
                {
                    NoteGap -= Dt;
                    if (NoteGap <= 0.0f)
                    {
                        --NotesLeft;
                        NoteLength = Random.Range(0.04f, 0.16f);
                        NoteLeft = NoteLength;
                        NoteGap = Random.Range(0.03f, 0.12f);
                        SongPitch *= Random.Range(0.92f, 1.1f);
                    }
                }
                Out[N] = Mix * Volume;
            }
        }

    private:
        float Fs;
        FRandom Random;
        FBiquad Leaves;
        FOnePole Air;
        float Gust = 0.4f, GustTarget = 0.4f;
        float NextSong = 2.0f, NoteLeft = 0.0f, NoteLength = 0.1f, NoteGap = 0.0f, BirdPhase = 0.0f;
        float SongPitch = 3500.0f, SongSweep = 0.0f, SongLevel = 0.02f;
        int NotesLeft = 0;
    };
}
