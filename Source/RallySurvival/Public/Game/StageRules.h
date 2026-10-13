#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Special stages: timing, pace notes from the route, and the rivals' times (docs/STAGES.md). No engine
// dependencies.
namespace Rally::Stage
{
    constexpr double Pi = 3.14159265358979323846;
    struct FPoint { double X = 0.0, Y = 0.0, Z = 0.0; };

    // Timing. A car stopped behind the start line for StageSeconds is counted down from CountdownSeconds;
    // leaving before zero is a jump start. The clock runs from zero to the flying finish; a stage still
    // running at its maximum time closes and the car is retired from it with that time.
    constexpr double StageSeconds = 1.5;
    constexpr int CountdownSeconds = 5;
    constexpr double JumpStartPenaltyS = 10.0;
    constexpr double StartBoxM = 15.0;
    constexpr double GateHalfWidthExtraM = 4.0;
    // A recce: the stage driven before the rally at no more than this, to see the notes; not timed.
    constexpr double RecceLimitKmh = 50.0;

    // A stage's reference time: its length at a brisk privateer pace for the surface (m/s).
    inline double ParSeconds(double LengthM, const std::string& Surface)
    {
        const double Speed = Surface == "Asphalt" ? 21.0 : Surface == "Gravel" ? 17.0 : 19.0;
        return std::isfinite(LengthM) && LengthM > 0.0 ? LengthM / Speed : 0.0;
    }
    inline double MaxSeconds(double ParS) { return ParS * 2.5 + 60.0; }

    // Distance along the route at each sample (m).
    inline std::vector<double> Distances(const std::vector<FPoint>& Route)
    {
        std::vector<double> D(Route.size(), 0.0);
        for (size_t I = 1; I < Route.size(); ++I)
            D[I] = D[I - 1] + std::hypot(Route[I].X - Route[I - 1].X, Route[I].Y - Route[I - 1].Y);
        return D;
    }

    // One pace note: a corner graded 1 (hairpin) to 6 (nearly flat) from its tightest radius, its direction
    // (+1 right, -1 left in the Unreal frame, where Y is right), whether it tightens or opens, and where it
    // starts (m along the route). Into: the next corner follows within 30 m.
    struct FCall
    {
        double AtM = 0.0;
        double EndM = 0.0;
        double AngleDeg = 0.0;
        double MinRadiusM = 0.0;
        int Grade = 6;
        int Direction = 1;
        bool bHairpin = false;
        bool bTightens = false;
        bool bOpens = false;
        bool bInto = false;
    };

    inline int GradeForRadius(double RadiusM)
    {
        return RadiusM < 20.0 ? 1 : RadiusM < 32.0 ? 2 : RadiusM < 50.0 ? 3 : RadiusM < 75.0 ? 4 : RadiusM < 110.0 ? 5 : 6;
    }

    // Corners from the route's curvature: heading change over about 9 m each side of every sample; a run of
    // samples turning the same way tighter than a 200 m radius is a corner, and one turning less than 15
    // degrees in all is not called.
    inline std::vector<FCall> PaceNotes(const std::vector<FPoint>& Route)
    {
        std::vector<FCall> Calls;
        const size_t N = Route.size();
        if (N < 8) return Calls;
        const std::vector<double> D = Distances(Route);
        std::vector<double> Heading(N, 0.0);
        for (size_t I = 0; I + 1 < N; ++I) Heading[I] = std::atan2(Route[I + 1].Y - Route[I].Y, Route[I + 1].X - Route[I].X);
        Heading[N - 1] = Heading[N - 2];
        const auto Wrap = [](double A) { while (A > Pi) A -= 2.0 * Pi; while (A < -Pi) A += 2.0 * Pi; return A; };
        std::vector<double> Curvature(N, 0.0);
        constexpr int W = 3;
        for (size_t I = W; I + W < N; ++I)
        {
            const double Arc = D[I + W] - D[I - W];
            if (Arc > 0.1) Curvature[I] = Wrap(Heading[I + W] - Heading[I - W]) / Arc;
        }
        constexpr double Straight = 1.0 / 200.0;
        size_t I = 0;
        while (I < N)
        {
            if (std::abs(Curvature[I]) < Straight) { ++I; continue; }
            const int Sign = Curvature[I] > 0.0 ? 1 : -1;
            size_t J = I;
            // Through brief straightening of the same corner (up to 3 samples).
            while (J + 1 < N)
            {
                size_t K = J + 1;
                while (K < N && K <= J + 3 && std::abs(Curvature[K]) < Straight) ++K;
                if (K < N && K <= J + 3 && Curvature[K] * Sign >= Straight) J = K;
                else break;
            }
            FCall Call;
            Call.AtM = D[I];
            Call.EndM = D[J];
            Call.Direction = Sign;
            double Peak = 0.0, FirstHalf = 0.0, SecondHalf = 0.0;
            const size_t Mid = (I + J) / 2;
            for (size_t K = I; K <= J; ++K)
            {
                const double C = std::abs(Curvature[K]);
                Peak = std::max(Peak, C);
                (K <= Mid ? FirstHalf : SecondHalf) = std::max(K <= Mid ? FirstHalf : SecondHalf, C);
            }
            Call.AngleDeg = std::abs(Wrap(Heading[std::min(J + 1, N - 1)] - Heading[I > 0 ? I - 1 : 0])) * 180.0 / Pi;
            Call.MinRadiusM = Peak > 0.0 ? 1.0 / Peak : 1e9;
            Call.Grade = GradeForRadius(Call.MinRadiusM);
            Call.bHairpin = Call.Grade == 1 && Call.AngleDeg > 110.0;
            Call.bTightens = J > I + 2 && SecondHalf > FirstHalf * 1.4;
            Call.bOpens = J > I + 2 && FirstHalf > SecondHalf * 1.4;
            if (Call.AngleDeg >= 15.0) Calls.push_back(Call);
            I = J + 1;
        }
        for (size_t K = 0; K + 1 < Calls.size(); ++K)
            Calls[K].bInto = Calls[K + 1].AtM - Calls[K].EndM < 30.0;
        return Calls;
    }

    // As a co-driver reads it: "left 4 tightens", "hairpin right", "right 3 into".
    inline std::string CallText(const FCall& Call)
    {
        std::string Text = Call.bHairpin ? std::string("hairpin ") + (Call.Direction > 0 ? "right" : "left")
            : std::string(Call.Direction > 0 ? "right " : "left ") + std::to_string(Call.Grade);
        if (Call.bTightens) Text += " tightens";
        else if (Call.bOpens) Text += " opens";
        if (Call.bInto) Text += " into";
        return Text;
    }

    // Rival crews, simulated: pace against the stage's reference time and how often they go off.
    struct FRival { const char* Crew; double Pace; double Risk; };
    inline const std::vector<FRival>& Rivals()
    {
        static const std::vector<FRival> List = {
            { "Varga / Szabo (works team)", 0.90, 0.03 },
            { "Kovac / Hudec", 0.97, 0.06 },
            { "Mraz / Bielik", 1.01, 0.05 },
            { "Nowak / Wojcik", 1.06, 0.08 },
            { "Horak / Benda", 1.12, 0.04 },
            { "Lukac / Toth", 1.20, 0.10 },
        };
        return List;
    }
    // Small deterministic generator, so every machine agrees on the rivals' times.
    inline double Random01(uint32_t& Seed)
    {
        Seed = Seed * 1664525u + 1013904223u;
        return static_cast<double>((Seed >> 8) & 0xFFFFFF) / static_cast<double>(0x1000000);
    }
    // A rival's time on a stage for one attempt; retired from the stage gets the maximum time.
    inline double RivalTime(const FRival& Rival, double ParS, uint32_t Seed, bool& bRetired)
    {
        for (int Warm = 0; Warm < 3; ++Warm) Random01(Seed);
        bRetired = Random01(Seed) < Rival.Risk;
        if (bRetired) return MaxSeconds(ParS);
        return ParS * Rival.Pace * (0.97 + 0.08 * Random01(Seed));
    }

    // m:ss.t
    inline std::string Clock(double Seconds)
    {
        if (!std::isfinite(Seconds) || Seconds < 0.0) Seconds = 0.0;
        const int Tenths = static_cast<int>(std::floor(Seconds * 10.0 + 0.5));
        char Buffer[32];
        std::snprintf(Buffer, sizeof(Buffer), "%d:%02d.%d", Tenths / 600, (Tenths / 10) % 60, Tenths % 10);
        return Buffer;
    }
}
