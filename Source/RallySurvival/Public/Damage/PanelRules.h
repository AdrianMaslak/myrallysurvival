#pragma once
#include <algorithm>
#include <cmath>

// Prototype tuning; health is quantised to one byte. No engine dependencies.
namespace Rally::Panel
{
    enum class EStage { Intact, Loose, Hanging, Detached };
    struct FFixings
    {
        int A = 255, B = 255, Latch = 255, Strap = 0;
    };

    inline EStage Stage(const FFixings& F, bool Bumper)
    {
        if (F.Strap > 0) return EStage::Loose;
        if (F.A <= 0 && F.B <= 0 && (Bumper || F.Latch <= 0)) return EStage::Detached;
        if (F.A <= 0 || F.B <= 0 || (!Bumper && F.Latch <= 0)) return EStage::Hanging;
        if (F.A < 170 || F.B < 170 || (!Bumper && F.Latch < 170)) return EStage::Loose;
        return EStage::Intact;
    }

    inline int Damage(int Health, double Amount)
    {
        if (!std::isfinite(Amount) || Amount <= 0) return Health;
        return std::max(0, Health - static_cast<int>(std::ceil(std::min(Amount, 1.0) * 255.0)));
    }

    inline double ImpactLoss(double DeltaV, bool LatchOrClip)
    {
        if (!std::isfinite(DeltaV) || DeltaV < 2 || DeltaV > 45) return 0;
        return std::clamp((DeltaV - 3.0) / (LatchOrClip ? 13.0 : 28.0), 0.0, 1.0);
    }

    // Persistent sub-byte wear: frame rate cannot round small increments away. Undamaged fixings
    // do not fatigue. Loose fixings can survive for minutes before giving way; jolts accelerate it.
    inline void Advance(FFixings& F, bool Bumper, double& Remainder, double Dt, double Kmh, double JoltG)
    {
        if (!std::isfinite(Dt) || !std::isfinite(Kmh) || !std::isfinite(JoltG) || Dt <= 0) return;
        const EStage S = Stage(F, Bumper);
        if (S == EStage::Intact || S == EStage::Detached) return;
        const double Wind = std::pow(std::clamp((std::abs(Kmh) - 80.0) / 40.0, 0.0, 3.0), 2.0);
        const double Jolt = std::max(0.0, std::min(JoltG, 8.0) - 1.5);
        const double Rate = F.Strap > 0 ? Jolt * 100.0 : Wind * (S == EStage::Hanging ? 0.9 : 8.0) + Jolt * 12.0;
        Remainder += Rate * std::min(Dt, 0.25);
        const int Wear = static_cast<int>(Remainder);
        Remainder -= Wear;
        if (F.Strap > 0) F.Strap = std::max(0, F.Strap - Wear);
        else if (!Bumper && F.Latch > 0) F.Latch = std::max(0, F.Latch - Wear);
        else
        {
            F.A = std::max(0, F.A - Wear);
            F.B = std::max(0, F.B - Wear);
        }
    }

    // Opening by hand needs a panel shut on a working latch: not a bumper, jammed, strapped,
    // hanging or gone. The same holds for closing it again.
    inline bool CanOpenByHand(const FFixings& F, bool Bumper, bool Jammed)
    {
        const EStage S = Stage(F, Bumper);
        return !Bumper && !Jammed && F.Strap <= 0 && (S == EStage::Intact || S == EStage::Loose);
    }

    // How crooked a panel goes back on; 0 would be as it left the factory, which never happens.
    // Rushed bolting (Rush 0 careful .. 1 as fast as allowed), not facing the part squarely (AimDeg off
    // the mount) and a bent panel (peak dent, cm) all add; a strap job hangs crooked whatever you do.
    inline double FitSloppiness(double Rush, double AimDeg, double BentCm, bool Bodge)
    {
        if (!std::isfinite(Rush) || !std::isfinite(AimDeg) || !std::isfinite(BentCm))
            return Bodge ? 2.9 : 1.4;
        const double S = 0.25 + 0.45 * std::clamp(Rush, 0.0, 1.0) + 0.3 * std::clamp(AimDeg / 30.0, 0.0, 1.0)
            + 0.4 * std::clamp(BentCm / 15.0, 0.0, 1.0);
        return Bodge ? 1.5 + S : S;
    }
    // Shift (cm) and skew (degrees) at sloppiness 1: a careful bolt-on leaves a few millimetres, a rushed
    // one on a bent panel well over a centimetre, a strap job two to three.
    constexpr double FitGapCm = 1.0;
    constexpr double FitSkewDeg = 1.2;
    // Hung more crooked than this, a bolted panel's latch (or a bumper's clips) only half catches: it
    // sits loose, rattles and can work free at speed.
    constexpr double HalfCaughtSloppiness = 0.8;
    constexpr int HalfCaughtHealth = 150;
    // Rush from the average time between bolt actions: the 0.75 s minimum is 1, two seconds or more 0.
    inline double RefitRush(double AverageIntervalS)
    {
        return std::isfinite(AverageIntervalS) ? std::clamp((2.0 - AverageIntervalS) / 1.25, 0.0, 1.0) : 1.0;
    }

    // One hinge degree of freedom, degrees and degrees per second: Torque (N m) on Inertia (kg m^2)
    // with viscous Damping (N m s per radian), between stops that bounce it back with Restitution.
    // Semi-implicit Euler; callers re-evaluate the torque each short step. Returns how fast (degrees per
    // second) it struck a stop this step, 0 if it struck none.
    struct FHinge
    {
        double Angle = 0.0;
        double Speed = 0.0;
    };

    inline double StepHinge(FHinge& H, double Torque, double Inertia, double Damping,
        double Min, double Max, double Restitution, double Dt)
    {
        if (!std::isfinite(Torque) || !std::isfinite(Inertia) || !std::isfinite(Dt) || Inertia <= 0 || Dt <= 0)
            return 0.0;
        constexpr double Rad = 3.14159265358979323846 / 180.0;
        const double Omega = H.Speed * Rad + (Torque - Damping * H.Speed * Rad) / Inertia * Dt;
        H.Speed = Omega / Rad;
        H.Angle += H.Speed * Dt;
        double Struck = 0.0;
        if (H.Angle < Min)
        {
            H.Angle = Min;
            if (H.Speed < 0) { Struck = -H.Speed; H.Speed = -H.Speed * Restitution; }
        }
        else if (H.Angle > Max)
        {
            H.Angle = Max;
            if (H.Speed > 0) { Struck = H.Speed; H.Speed = -H.Speed * Restitution; }
        }
        return Struck;
    }

    // An open or swinging panel hitting something (ground, wall, tree, another car) at Speed m/s, measured
    // at the point that hit. From 3 m/s (a hand never swings one that hard) it dents there and, pushed
    // towards shut, slams shut, catching its latch if that works, and the hinges barely notice; forced
    // the other way, past its stop, the hinges bend: health lost per hit, 0..1. About 27 km/h past the
    // stop tears a door off its hinges.
    constexpr double SwingDentSpeed = 3.0;
    // An open panel swinging back into its frame catches its latch (if that works) when its free edge
    // arrives at least this fast (m/s), or when something keeps pressing it into the frame this hard
    // (N m: wind at speed, hard acceleration); otherwise it rests there ajar and can swing open again.
    constexpr double LatchCatchSpeed = 1.0;
    constexpr double LatchPressTorque = 15.0;
    constexpr double SlamShutSpeed = 3.0;
    inline double SwingHitLoss(double Speed, bool ForcedOpen)
    {
        if (!std::isfinite(Speed) || Speed <= 0 || Speed > 45) return 0.0;
        return std::clamp((Speed - 3.0) / 9.0, 0.0, 1.0) * (ForcedOpen ? 2.0 : 0.35);
    }
    // A free panel flung against its own open stop (wind, braking, a hard corner) strains its hinges
    // once its free edge arrives faster than about 3 m/s.
    inline double OverextendLoss(double EdgeSpeed)
    {
        return std::isfinite(EdgeSpeed) ? std::clamp((EdgeSpeed - 3.0) / 12.0, 0.0, 1.0) : 0.0;
    }

    // Taking a part off by hand: actions needed (each at least 0.75 s apart, as bolting). Bolted panels
    // need the spanner and, for doors, bonnet and boot lid, to be open so the hinge bolts can be reached;
    // a hanging one is pulled off without tools; a strapped one needs its strap cut.
    constexpr int UnboltActions = 3;
    constexpr int PullOffActions = 2;
    constexpr int CutStrapActions = 1;

    constexpr double MaxRefitDentCm = 15.0;
    inline bool CanRefit(double MaxDentCm)
    {
        return std::isfinite(MaxDentCm) && MaxDentCm >= 0 && MaxDentCm <= MaxRefitDentCm;
    }
}
