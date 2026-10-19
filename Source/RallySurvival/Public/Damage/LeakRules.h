#pragma once
#include <algorithm>
#include <cmath>

// What leaks out of a damaged car and what it leaves on the ground (docs/DAMAGE.md, "Leaks"): drops falling
// from the sump, the radiator and the fuel tank, and the puddles they make. Litres, seconds, cm. Prototype
// tuning, no engine dependencies; shared with standalone tests.
namespace Rally::Leak
{
    enum class ELiquid : int { Oil, Coolant, Fuel };
    constexpr int NumLiquids = 3;

    // Drops a leak is drawn as each second: one every two seconds for a seep, up to a steady stream; each
    // carries its share of what is lost, so the puddles add up to the litres gone.
    inline double DropsPerSecond(double LitresPerSecond)
    {
        if (!std::isfinite(LitresPerSecond) || LitresPerSecond <= 0.0)
            return 0.0;
        return std::clamp(LitresPerSecond * 200.0, 0.5, 12.0);
    }

    // On the move the drops gather into spots about every 2.5 m along the road (each carrying more), so a
    // trail does not cover the road in drops: at most this many a second at a speed (m/s); parked, as above.
    inline double DropsPerSecondMoving(double LitresPerSecond, double SpeedMs)
    {
        const double Parked = DropsPerSecond(LitresPerSecond);
        if (Parked <= 0.0 || !std::isfinite(SpeedMs) || SpeedMs < 0.5)
            return Parked;
        return std::min(Parked, std::max(SpeedMs / 2.5, 0.5));
    }

    // A puddle spreads to a film about 1.5 mm thick: a litre covers about 0.67 m^2 (46 cm across the
    // middle), a drained sump nearly a metre; a single drop still shows as a small spot.
    constexpr double FilmMetres = 0.0015;
    constexpr double MinRadiusCm = 3.0;
    constexpr double MaxRadiusCm = 120.0;
    inline double PuddleRadiusCm(double Litres)
    {
        const double L = std::isfinite(Litres) ? std::max(Litres, 0.0) : 0.0;
        const double Radius = 100.0 * std::sqrt(L * 0.001 / FilmMetres / 3.14159265358979);
        return std::clamp(Radius, MinRadiusCm, MaxRadiusCm);
    }

    // How fast a puddle goes, litres per second per square metre of it: fuel evaporates in a minute or two,
    // coolant (mostly water) dries over most of a game day, oil stays.
    inline double DryRate(ELiquid Liquid)
    {
        switch (Liquid)
        {
        case ELiquid::Fuel: return 0.01;
        case ELiquid::Coolant: return 0.0006;
        default: return 0.00002;
        }
    }
    // A puddle is gone below this.
    constexpr double GoneLitres = 0.0003;

    // Oil that has run over the engine: the lower block soaks (0..1) as a litre and a half goes past it.
    inline double OilSoak(double LitresLeaked)
    {
        return std::isfinite(LitresLeaked) ? std::clamp(LitresLeaked / 1.5, 0.0, 1.0) : 0.0;
    }
}
