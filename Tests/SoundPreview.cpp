// Renders the procedural car sounds (Source/RallySurvival/Public/Audio/CarSoundModels.h) to WAV files in
// Saved/SoundPreview, so they can be heard without the game, and checks them: finite, not clipping, and an
// engine whose loudest low note sits on its firing frequency. Run: scripts\Render-Sounds.cmd
#include "Audio/CarSoundModels.h"
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace Rally::Audio;

namespace
{
    constexpr float Fs = 48000.0f;
    int Failures = 0, Checks = 0;
    void Check(bool Pass, const std::string& Label)
    {
        ++Checks;
        if (!Pass) { ++Failures; std::cerr << "FAIL: " << Label << '\n'; }
    }

    void WriteWav(const std::string& Path, const std::vector<float>& Samples)
    {
        FILE* File = nullptr;
        if (fopen_s(&File, Path.c_str(), "wb") != 0 || !File) { std::cerr << "cannot write " << Path << '\n'; ++Failures; return; }
        const uint32_t DataBytes = static_cast<uint32_t>(Samples.size() * 2), Rate = static_cast<uint32_t>(Fs), ByteRate = Rate * 2;
        const uint32_t ChunkSize = 36 + DataBytes, FmtSize = 16;
        const uint16_t Format = 1, Channels = 1, Align = 2, Bits = 16;
        std::fwrite("RIFF", 1, 4, File); std::fwrite(&ChunkSize, 4, 1, File); std::fwrite("WAVEfmt ", 1, 8, File);
        std::fwrite(&FmtSize, 4, 1, File); std::fwrite(&Format, 2, 1, File); std::fwrite(&Channels, 2, 1, File);
        std::fwrite(&Rate, 4, 1, File); std::fwrite(&ByteRate, 4, 1, File); std::fwrite(&Align, 2, 1, File); std::fwrite(&Bits, 2, 1, File);
        std::fwrite("data", 1, 4, File); std::fwrite(&DataBytes, 4, 1, File);
        for (float S : Samples)
        {
            const int16_t V = static_cast<int16_t>(std::lround(std::clamp(S, -1.0f, 1.0f) * 32767.0f));
            std::fwrite(&V, 2, 1, File);
        }
        std::fclose(File);
    }

    struct FStats { float Peak = 0.0f, Rms = 0.0f; bool bFinite = true; };
    FStats Stats(const std::vector<float>& S, size_t From = 0, size_t To = 0)
    {
        FStats Out;
        To = To ? std::min(To, S.size()) : S.size();
        double Sum = 0.0;
        for (size_t I = From; I < To; ++I)
        {
            Out.bFinite = Out.bFinite && std::isfinite(S[I]);
            Out.Peak = std::max(Out.Peak, std::abs(S[I]));
            Sum += static_cast<double>(S[I]) * S[I];
        }
        Out.Rms = static_cast<float>(std::sqrt(Sum / static_cast<double>(std::max<size_t>(To - From, 1))));
        return Out;
    }

    // Power at one frequency over a Hann window (Goertzel).
    double Power(const std::vector<float>& S, size_t From, size_t Count, double Hz)
    {
        const double W = 2.0 * 3.14159265358979 * Hz / Fs, C = 2.0 * std::cos(W);
        double S1 = 0.0, S2 = 0.0;
        for (size_t I = 0; I < Count && From + I < S.size(); ++I)
        {
            const double Window = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * static_cast<double>(I) / static_cast<double>(Count));
            const double S0 = S[From + I] * Window + C * S1 - S2;
            S2 = S1; S1 = S0;
        }
        return S1 * S1 + S2 * S2 - C * S1 * S2;
    }

    // The strongest frequency between Lo and Hi Hz, in 1 Hz steps.
    double Strongest(const std::vector<float>& S, size_t From, size_t Count, double Lo, double Hi)
    {
        double Best = Lo, BestPower = -1.0;
        for (double Hz = Lo; Hz <= Hi; Hz += 1.0)
        {
            const double P = Power(S, From, Count, Hz);
            if (P > BestPower) { BestPower = P; Best = Hz; }
        }
        return Best;
    }

    // An engine run: Script(t, params) sets the parameters for each 10 ms block.
    std::vector<float> RenderEngine(float Seconds, const std::function<void(float, FEngineParams&)>& Script, uint32_t Seed = 1u)
    {
        FEngineModel Model(Fs, Seed);
        std::vector<float> Out(static_cast<size_t>(Seconds * Fs));
        const size_t Block = 480;
        FEngineParams P;
        for (size_t I = 0; I < Out.size(); I += Block)
        {
            Script(static_cast<float>(I) / Fs, P);
            Model.Render(Out.data() + I, static_cast<int>(std::min(Block, Out.size() - I)), P);
        }
        return Out;
    }

    float Ramp(float T, float T0, float T1, float A, float B) { return T <= T0 ? A : T >= T1 ? B : A + (B - A) * (T - T0) / (T1 - T0); }

    // Start, idle, two blips, a pull through the revs, the overrun back down, idle, switch off.
    void Drive(float T, FEngineParams& P)
    {
        P.bRunning = T >= 1.0f && T < 16.0f;
        if (T < 1.0f) { P.Rpm = 0.0f; P.Load = 0.0f; }
        else if (T < 1.8f) { P.Rpm = Ramp(T, 1.0f, 1.15f, 0.0f, 260.0f); P.Load = 0.0f; }
        else if (T < 2.3f) { P.Rpm = Ramp(T, 1.8f, 2.1f, 260.0f, 1250.0f); P.Load = 0.25f; }
        else if (T < 5.0f) { P.Rpm = Ramp(T, 2.3f, 3.0f, 1250.0f, 900.0f); P.Load = 0.0f; }
        else if (T < 7.0f)
        {
            const float B = std::fmod(T - 5.0f, 1.0f);
            P.Load = B < 0.3f ? 0.7f : 0.0f;
            P.Rpm = B < 0.3f ? Ramp(B, 0.0f, 0.3f, 900.0f, 4200.0f) : Ramp(B, 0.3f, 1.0f, 4200.0f, 900.0f);
        }
        else if (T < 12.0f) { P.Load = 1.0f; P.Rpm = Ramp(T, 7.0f, 12.0f, 1800.0f, 6700.0f); }
        else if (T < 14.0f) { P.Load = 0.0f; P.Rpm = Ramp(T, 12.0f, 14.0f, 6700.0f, 1800.0f); }
        else if (T < 16.0f) { P.Load = 0.0f; P.Rpm = Ramp(T, 14.0f, 14.6f, 1800.0f, 900.0f); }
        else { P.Load = 0.0f; P.Rpm = Ramp(T, 16.0f, 17.3f, 900.0f, 0.0f); }
    }

    size_t At(float Seconds) { return static_cast<size_t>(Seconds * Fs); }
}

int main(int argc, char** argv)
{
    const std::string Dir = argc > 1 ? argv[1] : "Saved/SoundPreview";
    const auto Save = [&](const char* Name, const std::vector<float>& S)
    {
        const FStats St = Stats(S);
        Check(St.bFinite, std::string(Name) + ": finite");
        Check(St.Peak <= 1.0f && St.Peak > 0.02f, std::string(Name) + ": audible and not clipping");
        std::printf("%-22s %5.1f s  peak %.2f  rms %.3f\n", Name, static_cast<double>(S.size()) / Fs, St.Peak, St.Rms);
        WriteWav(Dir + "/" + Name + ".wav", S);
    };

    // Engine: healthy, then damaged (worn, misfiring, exhaust blowing), exhaust off, and from the cabin.
    const auto Healthy = RenderEngine(18.0f, Drive);
    Save("engine_healthy", Healthy);
    {
        // Idle at 900 rpm fires 30 times a second; 3000 rpm at half load 100.
        const auto Idle = RenderEngine(3.0f, [](float, FEngineParams& P) { P.bRunning = true; P.Rpm = 900.0f; P.Load = 0.0f; });
        const auto Mid = RenderEngine(3.0f, [](float, FEngineParams& P) { P.bRunning = true; P.Rpm = 3000.0f; P.Load = 0.5f; });
        const double IdlePeak = Strongest(Idle, At(1.0f), At(1.0f), 20.0, 400.0);
        const double MidPeak = Strongest(Mid, At(1.0f), At(1.0f), 40.0, 600.0);
        std::printf("idle strongest %.0f Hz (firing 30), 3000 rpm strongest %.0f Hz (firing 100)\n", IdlePeak, MidPeak);
        const auto OnHarmonic = [](double Peak, double Firing)
        {
            const double K = std::round(Peak / Firing);
            return K >= 1.0 && std::abs(Peak - K * Firing) <= 3.0;
        };
        Check(OnHarmonic(IdlePeak, 30.0), "idle note is a harmonic of the firing frequency");
        Check(OnHarmonic(MidPeak, 100.0), "3000 rpm note is a harmonic of the firing frequency");
        const FStats Cranking = Stats(Healthy, At(1.2f), At(1.7f));
        const FStats Off = Stats(Healthy, At(0.1f), At(0.9f));
        const FStats Pull = Stats(Healthy, At(10.0f), At(11.5f));
        const FStats IdleSt = Stats(Healthy, At(3.5f), At(4.8f));
        std::printf("rms: off %.4f, cranking %.4f, idle %.4f, full load %.4f\n", Off.Rms, Cranking.Rms, IdleSt.Rms, Pull.Rms);
        Check(Off.Peak < 0.001f && Cranking.Rms > 0.005f, "silent while off, the starter heard while cranking");
        Check(Pull.Rms > 2.0f * IdleSt.Rms, "full load at high revs much louder than idle");
    }
    const auto Damaged = RenderEngine(18.0f, [](float T, FEngineParams& P) { Drive(T, P); P.EngineHealth = 0.25f; P.ExhaustLeak = 0.8f; P.MisfireRate = 1.5f; }, 3u);
    Save("engine_damaged", Damaged);
    const auto NoExhaust = RenderEngine(18.0f, [](float T, FEngineParams& P) { Drive(T, P); P.bExhaustOff = true; }, 5u);
    Save("engine_exhaust_off", NoExhaust);
    {
        const FStats Loud = Stats(NoExhaust, At(3.5f), At(4.8f));
        const FStats Quiet = Stats(Healthy, At(3.5f), At(4.8f));
        std::printf("idle rms: healthy %.4f, exhaust off %.4f\n", Quiet.Rms, Loud.Rms);
        Check(Loud.Rms > 1.3f * Quiet.Rms, "no exhaust is louder at idle");
    }
    Save("engine_cabin", RenderEngine(18.0f, [](float T, FEngineParams& P) { Drive(T, P); P.Interior = 1.0f; }, 7u));

    // Road: gravel, then tarmac, each accelerating, sliding, braking; then metal dragging.
    for (int Surface = 0; Surface < 2; ++Surface)
    {
        FRoadModel Road(Fs, 7u + static_cast<uint32_t>(Surface));
        std::vector<float> Out(At(16.0f));
        FRoadParams P;
        P.Loose = Surface == 0 ? 1.0f : 0.0f;
        for (size_t I = 0; I < Out.size(); I += 480)
        {
            const float T = static_cast<float>(I) / Fs;
            P.SpeedMs = T < 8.0f ? Ramp(T, 0.0f, 8.0f, 0.0f, 28.0f) : Ramp(T, 12.0f, 16.0f, 28.0f, 0.0f);
            P.Slide = T > 9.0f && T < 12.0f ? 1.8f : (T > 12.0f && T < 14.0f ? 1.2f : 0.2f);
            Road.Render(Out.data() + I, 480, P);
        }
        Save(Surface == 0 ? "road_gravel" : "road_tarmac", Out);
    }
    {
        FRoadModel Road(Fs, 9u);
        std::vector<float> Out(At(6.0f));
        FRoadParams P;
        P.Loose = 0.5f; P.SpeedMs = 10.0f; P.Scrape = 1.0f;
        for (size_t I = 0; I < Out.size(); I += 480) Road.Render(Out.data() + I, 480, P);
        Save("road_scrape", Out);
    }

    // Events, a second apart: crashes light to heavy, a hit under the floor, three gear changes, the starter
    // solenoid with a flat battery, the starter against a seized engine, the shudder of an engine stopping,
    // and the starter turning an engine with no fuel.
    {
        FEventModel Events(Fs);
        std::vector<float> Out(At(13.0f), 0.0f);
        const FEventRequest Script[] = {
            { EEvent::Impact, 0.2f }, { EEvent::Impact, 0.5f }, { EEvent::Impact, 1.0f }, { EEvent::Underside, 0.7f },
            { EEvent::GearShift, 1.0f }, { EEvent::GearShift, 1.0f }, { EEvent::GearShift, 1.0f },
            { EEvent::StarterClick, 1.0f }, { EEvent::Seized, 1.0f }, { EEvent::Shudder, 1.0f }, { EEvent::CrankNoStart, 1.0f } };
        size_t E = 0;
        for (size_t I = 0; I < Out.size(); I += 480)
        {
            if (E < sizeof(Script) / sizeof(Script[0]) && static_cast<float>(I) / Fs >= 0.3f + static_cast<float>(E)) Events.Trigger(Script[E++]);
            Events.Render(Out.data() + I, static_cast<int>(std::min<size_t>(480, Out.size() - I)));
        }
        Save("events", Out);
    }
    {
        FAmbienceModel Ambience(Fs);
        std::vector<float> Out(At(30.0f));
        for (size_t I = 0; I < Out.size(); I += 480) Ambience.Render(Out.data() + I, 480);
        Save("ambience", Out);
    }

    std::printf("%d/%d checks passed\n", Checks - Failures, Checks);
    return Failures == 0 ? 0 : 1;
}
