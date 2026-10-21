#include "Vehicle/NetSmoothingRules.h"
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace
{
    using namespace Rally::NetSmooth;
    int Checks = 0, Failures = 0;
    void Check(bool Passed, const char* Name)
    {
        ++Checks;
        if (!Passed) { ++Failures; std::cerr << "FAIL: " << Name << '\n'; }
    }
    bool Near(double A, double B, double Epsilon = 1e-8) { return std::abs(A - B) < Epsilon; }
    FSnapshot Truth(double T)
    {
        FSnapshot S;
        S.Time = T;
        S.Position = { 20 * T, 5 * std::sin(T), 0 };
        S.Velocity = { 20, 5 * std::cos(T), 0 };
        S.Rotation = { 0, 0, std::sin(0.1 * T), std::cos(0.1 * T) };
        S.AngularVelocity = { 0, 0, 0.2 };
        return S;
    }
    void Stream(const char* Name, bool bJitter, bool bLoss, bool bBurst, bool bBunched = false)
    {
        std::uint32_t Seed = 0xC600;
        const auto Random = [&]() { Seed = Seed * 1664525u + 1013904223u; return Seed; };
        std::vector<FArrival> Packets;
        int Lost = 0;
        bool DropInBlock[100] = {};
        for (int I = 0; I < 1200; ++I)
        {
            const double Send = I * SendInterval;
            // Exactly 3% lost, with a seeded location in each block of 100 sends.
            if (bLoss && I % 100 == 0)
            {
                std::fill(std::begin(DropInBlock), std::end(DropInBlock), false);
                for (int Count = 0; Count < 3;)
                {
                    const auto Index = Random() % 100;
                    if (!DropInBlock[Index]) { DropInBlock[Index] = true; ++Count; }
                }
            }
            const bool Drop = bLoss && DropInBlock[I % 100];
            const double Jitter = bJitter ? 0.010 + 0.030 * (Random() / 4294967295.0) : 0.0;
            if (Drop) { ++Lost; continue; }
            if (bBurst && Send >= 15.0 && Send < 15.6) continue;
            double Receive = Send + 7.040 + Jitter;
            if (bBunched) Receive = std::ceil(Receive / 0.050) * 0.050;
            Packets.push_back({ Send, Receive });
        }
        std::stable_sort(Packets.begin(), Packets.end(), [](FArrival A, FArrival B) { return A.Receive < B.Receive; });
        std::vector<FArrival> Arrivals;
        std::vector<FSnapshot> Buffer;
        std::size_t Next = 0;
        double Delay = SendInterval, Playout = 0, SumError = 0, MaxError = 0;
        double SumDelay = 0, MinDelay = 1e9, MaxDelay = 0, MaxStepError = 0;
        int Frames = 0, Dry = 0, Held = 0;
        for (int Frame = 0; Frame < 40 * 120; ++Frame)
        {
            const double Now = 7.0 + Frame / 120.0;
            while (Next < Packets.size() && Packets[Next].Receive <= Now)
            {
                const auto Packet = Packets[Next++];
                if (!Arrivals.empty() && Packet.Send <= Arrivals.back().Send) continue;
                Arrivals.push_back(Packet);
                Buffer.push_back(Truth(Packet.Send));
                if (Buffer.size() > 32) Buffer.erase(Buffer.begin());
            }
            if (Buffer.empty()) continue;
            const double Offset = EstimateOffset(Arrivals);
            Delay = Slew(Delay, ChooseDelay(Arrivals, Offset), 1.0 / 120.0);
            if (Playout == 0) Playout = Now - Offset - Delay;
            else
            {
                const double Previous = Playout;
                Playout = AdvancePlayout(Playout, Now, Offset, Delay, 1.0 / 120.0);
                MaxStepError = std::max(MaxStepError, std::abs((Playout - Previous) * 120 - 1));
            }
            const auto Result = Sample(Buffer, Playout);
            if (Frame < 5 * 120) continue;
            const double Error = Length(Result.State.Position - Truth(Playout).Position);
            SumError += Error;
            MaxError = std::max(MaxError, Error);
            SumDelay += Delay;
            MinDelay = std::min(MinDelay, Delay);
            MaxDelay = std::max(MaxDelay, Delay);
            ++Frames;
            if (Result.Mode == ESample::Extrapolated) ++Dry;
            if (Result.Mode == ESample::Held) ++Held;
        }
        Check(Frames == 4200, "stream measured frames");
        Check(MaxStepError <= SlewRate + 1e-9, "playout speed within 1 percent");
        Check(MaxError < (bBurst ? 12.0 : 0.1), "stream maximum position error");
        Check(SumError / Frames < (bBurst ? 0.15 : 0.01), "stream mean position error");
        Check(bBurst ? Held > 0 : Held == 0, "hold only during burst");
        Check(bLoss ? Lost == 36 : Lost == 0, "loss count");
        Check(Delay >= SendInterval && Delay < 0.160, "delay bounds");
        std::cout << Name << ": error mean/max " << SumError / Frames << '/' << MaxError
            << " m, delay mean/min/max " << 1000 * SumDelay / Frames << '/' << 1000 * MinDelay << '/' << 1000 * MaxDelay
            << " ms, extrapolated/held " << Dry << '/' << Held << " of " << Frames << ", lost " << Lost << '\n';
    }
}

int main()
{
    using namespace Rally::NetSmooth;
    std::cout << std::fixed << std::setprecision(6);
    Check(Sample({}, 0).Mode == ESample::Empty, "empty buffer");
    std::vector<FArrival> Arrivals = { { 0, 7.080 }, { 1, 8.060 }, { 2, 9.040 } };
    Check(Near(EstimateOffset(Arrivals), 7.040), "offset converges to minimum transit");
    for (int I = 3; I < 123; ++I) Arrivals.push_back({ double(I), I + 7.050 });
    Check(Near(EstimateOffset(Arrivals), 7.050), "old minimum expires");
    std::cout << "Clock offset: true 7.000 s, estimated 7.040 s (40 ms baseline latency); expired window 7.050 s\n";
    Check(Near(ChooseDelay({}, 0), SendInterval), "startup delay");
    Arrivals.clear();
    for (int I = 0; I < 120; ++I) Arrivals.push_back({ I * SendInterval, I * SendInterval + 7.040 });
    Check(Near(ChooseDelay(Arrivals, EstimateOffset(Arrivals)), SendInterval), "clean minimum delay");
    Arrivals.back().Receive += 0.5;
    Check(Near(ChooseDelay(Arrivals, EstimateOffset(Arrivals)), SendInterval), "99th percentile ignores one outlier");
    Arrivals[118].Receive += 0.4;
    Check(Near(ChooseDelay(Arrivals, EstimateOffset(Arrivals)), SendInterval + 0.4), "99th percentile rank");
    Check(Near(Slew(0.1, 0.2, 1), 0.11) && Near(Slew(0.1, 0, 1), 0.09), "delay slew both ways");
    FSnapshot A, B;
    A.Velocity = { 1, 0, 0 };
    B.Time = 1;
    B.Position = { 2, 0, 0 };
    B.Velocity = { 3, 0, 0 };
    B.Rotation = { 0, 0, 1, 0 };
    auto Result = Sample({ A, B }, 0.5);
    Check(Near(Result.State.Position.X, 0.75), "Hermite acceleration position");
    Check(Near(Result.State.Velocity.X, 2), "Hermite derivative velocity");
    Check(Near(Result.State.Rotation.Z, std::sqrt(0.5)), "rotation slerp");
    Check(Near(Slerp({}, { 0, 0, 0, -1 }, 0.5).W, 1), "quaternion shortest arc");
    Check(Near(Sample({ A, B }, 0).State.Position.X, 0) && Near(Sample({ A, B }, 1).State.Position.X, 2), "sample endpoints");
    Check(Sample({ A }, -1).Mode == ESample::Held, "before oldest");
    A.AngularVelocity = { 0, 0, 2 };
    Result = Sample({ A }, 0.05);
    Check(Near(Result.State.Position.X, 0.05) && Near(Result.State.Rotation.Z, std::sin(0.05)), "linear and angular extrapolation");
    Result = Sample({ A }, 10);
    Check(Result.Mode == ESample::Held && Near(Result.State.Position.X, 0.1) && Near(Length(Result.State.Velocity), 0), "capped hold");
    Check(Near(Result.State.Rotation.Z, std::sin(0.1)) && Near(Length(Result.State.AngularVelocity), 0), "angular hold");
    auto Correction = BeginCorrection(B, A);
    Check(!Correction.bSnap && Near(ApplyCorrection(A, Correction, 0).Position.X, 2), "correction starts continuously");
    Check(Near(ApplyCorrection(A, Correction, 0.125).Position.X, 1), "correction halfway");
    Check(Near(ApplyCorrection(A, Correction, 0.25).Position.X, 0), "correction completes");
    Check(Near(ApplyCorrection(A, Correction, 0).Rotation.Z, 1) && Near(ApplyCorrection(A, Correction, 0.25).Rotation.W, 1), "rotation correction");
    Check(ApplyCorrection(A, Correction, 1e-6).Position.X > 2 - 1e-9, "correction smooth start");
    Check(ApplyCorrection(A, Correction, CorrectionDuration - 1e-6).Position.X < 1e-9, "correction smooth end");
    const auto Shown = ApplyCorrection(A, Correction, 0.1);
    const auto Rebased = BeginCorrection(Shown, B);
    Check(Near(ApplyCorrection(B, Rebased, 0).Position.X, Shown.Position.X), "correction rebase continuous");
    B.Position.X = SnapDistance;
    Correction = BeginCorrection(A, B);
    Check(Correction.bSnap && Near(ApplyCorrection(B, Correction, 0).Position.X, SnapDistance), "teleport snap");
    Check(BeginCorrection(A, A, true).bSnap, "explicit reset snaps");
    Stream("clean", false, false, false);
    Stream("jitter 10-40 ms", true, false, false);
    Stream("jitter + 3% loss", true, true, false);
    Stream("jitter + 600 ms burst gap", true, false, true);
    Stream("jitter + 50 ms arrival batches", true, false, false, true);
    std::cout << Checks << " checks, " << Failures << " failures\n";
    return Failures == 0 ? 0 : 1;
}
