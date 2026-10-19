#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Fixings: the bolts, nuts, clips and screws that hold a part on, each turned with one tool. A part goes
// on with its fixings started by hand (out) and is held only once they are turned home; one left out
// lets it rattle, and driving works the others loose until it falls off. No engine dependencies.
namespace Rally::Fixing
{
    // What a player holds. Spanners by size; a screwdriver for screws and hose clamps; pliers for spring
    // clips; a hammer for knocking bent parts roughly straight (a bodge).
    enum class ETool : uint8_t { Hands, Spanner10, Spanner13, Spanner17, Spanner19, Screwdriver, Pliers, Hammer, Count };
    enum class EKind : uint8_t { Bolt, Nut, Screw, Clip };

    inline const char* ToolName(ETool Tool)
    {
        switch (Tool)
        {
        case ETool::Hands: return "bare hands";
        case ETool::Spanner10: return "10 mm spanner";
        case ETool::Spanner13: return "13 mm spanner";
        case ETool::Spanner17: return "17 mm spanner";
        case ETool::Spanner19: return "19 mm spanner";
        case ETool::Screwdriver: return "screwdriver";
        case ETool::Pliers: return "pliers";
        case ETool::Hammer: return "hammer";
        default: return "nothing";
        }
    }

    // What a fixing is called, by kind and the tool that turns it ("13 mm bolt", "screw").
    inline const char* FixingName(EKind Kind, ETool Tool)
    {
        if (Kind == EKind::Screw) return "screw";
        if (Kind == EKind::Clip) return "clip";
        const bool Nut = Kind == EKind::Nut;
        switch (Tool)
        {
        case ETool::Spanner10: return Nut ? "10 mm nut" : "10 mm bolt";
        case ETool::Spanner13: return Nut ? "13 mm nut" : "13 mm bolt";
        case ETool::Spanner17: return Nut ? "17 mm nut" : "17 mm bolt";
        case ETool::Spanner19: return Nut ? "19 mm nut" : "19 mm bolt";
        default: return Nut ? "nut" : "bolt";
        }
    }

    // Holding the key this long turns one fixing out, or home.
    constexpr double TurnSeconds = 0.5;
    // The server takes one fixing per player at most this often, whatever a client sends.
    constexpr double MinTurnInterval = 0.4;
    // How far a fixing that is out stands proud of the part (cm), and the radius it is aimed at by.
    constexpr double OutStandCm = 1.4;
    constexpr double AimRadiusCm = 3.0;
    // Aim assist: with the crosshair on a part within arm's length whose fixings can be got at, the nearest
    // of them within this distance of the crosshair is the one aimed at.
    constexpr double AssistRadiusCm = 7.0;
    constexpr double AssistReachCm = 130.0;

    // What one group of fixings (one hinge's bolts, one end's clips) still holds, as fixing health
    // 0..255: all home 255; some out 150 (loose: it rattles, a latch only half catches); all out 20 (it sits
    // in place, held by nothing, and any jolt or wind takes it).
    inline int GroupHold(int Out, int Total)
    {
        if (Total <= 0 || Out <= 0) return 255;
        return Out >= Total ? 20 : 150;
    }

    // A running-gear part's health as the car feels it: its own, but while fixings are out at most 0.5
    // (it shifts on its mounts), with none home 0.15 (it barely sits there).
    inline double EffectiveHealth(double Health, int Home, int Total)
    {
        if (!std::isfinite(Health)) return 0.0;
        const double H = std::clamp(Health, 0.0, 1.0);
        if (Total <= 0 || Home >= Total) return H;
        return std::min(H, Home <= 0 ? 0.15 : 0.5);
    }

    // Driving with fixings out works the rest loose. Returns how many more come out this step; the
    // fraction carries between frames. About one a minute at 60 km/h with half of them out, much faster
    // over bumps; nothing while standing still or with all home.
    inline int WorkLoose(int Home, int Total, double& Remainder, double Dt, double Kmh, double JoltG)
    {
        if (Total <= 0 || Home >= Total || Home <= 0 || !std::isfinite(Dt) || !std::isfinite(Kmh) || !std::isfinite(JoltG) || Dt <= 0)
            return 0;
        const double OutShare = static_cast<double>(Total - Home) / Total;
        const double Rate = OutShare * (std::min(std::abs(Kmh), 200.0) / 1800.0 + std::max(0.0, std::min(JoltG, 8.0) - 1.2) * 0.4);
        Remainder += Rate * std::min(Dt, 0.25);
        const int Count = std::min(static_cast<int>(Remainder), Home);
        Remainder -= Count;
        return Count;
    }

    // A part whose fixings are all out falls off once the car moves faster than walking pace.
    constexpr double FallOffKmh = 6.0;

    // How far a bolted-on engine part sits out of true as its fixings go, as seen with the bonnet up: 3 to
    // 6 degrees and up to 1 cm while some are home; with none home it hangs on its hoses and wiring, 14
    // degrees over and 4 cm down.
    inline double LooseTiltDeg(int Home, int Total)
    {
        if (Total <= 0 || Home >= Total) return 0.0;
        if (Home <= 0) return 14.0;
        return 3.0 + 3.0 * static_cast<double>(Total - Home) / Total;
    }
    inline double LooseDropCm(int Home, int Total)
    {
        if (Total <= 0 || Home >= Total) return 0.0;
        return Home <= 0 ? 4.0 : static_cast<double>(Total - Home) / Total;
    }
    // How hard loose parts rattle, 0..1: the loosest one's share of fixings out, shaken by the running
    // engine (half) or by the road (full from 40 km/h); silent standing with the engine off.
    inline double RattleLevel(double OutShare, bool bEngineRunning, double Kmh)
    {
        if (!std::isfinite(OutShare) || !std::isfinite(Kmh)) return 0.0;
        const double Shake = std::max(bEngineRunning ? 0.5 : 0.0, std::min(std::abs(Kmh) / 40.0, 1.0));
        return std::clamp(OutShare, 0.0, 1.0) * Shake;
    }
}
