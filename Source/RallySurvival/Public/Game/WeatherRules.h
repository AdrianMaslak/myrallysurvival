#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// The weather (docs/DAYS.md): showers on some afternoons. Worked out from the day and hour alone, so every
// machine agrees without anything replicated. No engine dependencies.
namespace Rally::Weather
{
    // Rain (0..1) at an hour of a day. The first day is dry; after it two days in five bring a shower that
    // starts between noon and 17:00, lasts 1.5 to 3.5 hours, and comes on and eases off over half an hour.
    inline double Rain(int Day, double Hour)
    {
        if (Day <= 1 || !std::isfinite(Hour)) return 0.0;
        const std::uint32_t Seed = static_cast<std::uint32_t>(Day) * 2654435761u;
        if ((Seed >> 7) % 5 >= 2) return 0.0;
        const double Start = 12.0 + static_cast<double>((Seed >> 13) % 6);
        const double Length = 1.5 + static_cast<double>((Seed >> 17) % 3);
        const double Into = Hour - Start;
        if (Into <= 0.0 || Into >= Length) return 0.0;
        return std::clamp(std::min(Into, Length - Into) / 0.5, 0.0, 1.0);
    }
    // Wet roads: tyres grip up to 15% less in a downpour.
    inline double GripScale(double Rain) { return 1.0 - 0.15 * std::clamp(std::isfinite(Rain) ? Rain : 0.0, 0.0, 1.0); }
    // The sky darkens and the air thickens with it.
    inline double SunScale(double Rain) { return 1.0 - 0.6 * std::clamp(std::isfinite(Rain) ? Rain : 0.0, 0.0, 1.0); }
    inline double FogScale(double Rain) { return 1.0 + 4.0 * std::clamp(std::isfinite(Rain) ? Rain : 0.0, 0.0, 1.0); }
}
