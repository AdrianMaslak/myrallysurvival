#pragma once
#include <algorithm>
#include <cmath>

// The crew's bodies (docs/BODY.md): injuries from hard crashes and the first-aid kit. No engine dependencies.
namespace Rally::Body
{
    // A crash's change of speed (m/s) hurts whoever is in the car from about 30 km/h; a 50 km/h wall about
    // halfway (0..1, adding up).
    inline double InjuryFromImpact(double DeltaV)
    {
        if (!std::isfinite(DeltaV)) return 0.0;
        return std::clamp((DeltaV - 8.5) / 18.0, 0.0, 1.0);
    }
    // Hurt, the edges of sight darken and blur and walking slows.
    inline double WalkScale(double Injury) { return 1.0 - 0.45 * std::clamp(Injury, 0.0, 1.0); }
    inline double Vignette(double Injury) { return 0.4 + 1.1 * std::clamp(Injury, 0.0, 1.0); }
    inline double Fringe(double Injury) { return 5.0 * std::clamp(Injury, 0.0, 1.0); }
    // A first-aid kit treats three times; each treatment takes the injury away.
    constexpr int FirstAidUses = 3;
    // Hurt this badly a player is taken to hospital: they wake in the service yard, still sore, and the
    // bill comes out of the kitty.
    constexpr double HospitalInjury = 1.0;
    constexpr double AfterHospital = 0.25;
    constexpr int HospitalBillSk = 3000;

    // A summer's day in the hills (C): 6 before dawn, 22 mid-afternoon.
    inline double AmbientC(double Hour)
    {
        if (!std::isfinite(Hour)) return 15.0;
        return 14.0 - 8.0 * std::cos(3.14159265358979323846 * (Hour - 3.0) / 12.0);
    }
    // Cold (0..1) per second: below 12 C outside it builds (at 6 C full in about ten minutes); a running
    // engine's heater warms the cabin quickly, a cold cabin slowly, and a warm day too.
    inline double ColdRate(double AmbientC, bool bInCar, bool bHeater)
    {
        if (!std::isfinite(AmbientC)) return 0.0;
        if (bInCar) return bHeater ? -1.0 / 60.0 : (AmbientC < 12.0 ? -1.0 / 900.0 : -1.0 / 300.0);
        return AmbientC < 12.0 ? (12.0 - AmbientC) / 12.0 / 300.0 : -1.0 / 240.0;
    }
    // Cold, the hands work slower (a fixing takes up to twice as long) and the world looks blue; a smoke
    // calms them, halving that.
    inline double HandSlowdown(double Cold, double Calm = 0.0)
    {
        return 1.0 + std::clamp(Cold, 0.0, 1.0) * (1.0 - 0.5 * std::clamp(Calm, 0.0, 1.0));
    }

    // Slivovica: four swigs to a bottle. Each makes you a quarter drunker and takes some of the cold away;
    // it wears off over ten minutes a quarter. Drunk, the view sways and blurs and the steering wanders.
    constexpr int SwigsPerBottle = 4;
    constexpr double SwigDrunk = 0.25;
    constexpr double SwigWarmth = 0.3;
    constexpr double SoberPerSecond = 0.25 / 600.0;
    inline double SwayDeg(double Drunk) { return 4.0 * std::clamp(Drunk, 0.0, 1.0); }
    inline double SteerWander(double Drunk) { return 0.12 * std::clamp(Drunk, 0.0, 1.0); }
    // Cigarettes: twenty to a carton. One calms you for five minutes and warms you a little.
    constexpr int CigarettesPerCarton = 20;
    constexpr double CalmSeconds = 300.0;
    constexpr double SmokeWarmth = 0.05;
}
