#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Internet replay rules. Seconds, metres, m/s, world-space rad/s and unit quaternions.
// Callers own the buffers. Inputs are finite; snapshots have strictly increasing sender times.
namespace Rally::NetSmooth
{
    constexpr std::size_t WindowSize = 120;
    constexpr double SendInterval = 1.0 / 30.0;
    constexpr double Coverage = 0.99;
    constexpr double SlewRate = 0.01;
    constexpr double ExtrapolationLimit = 0.100;
    constexpr double CorrectionDuration = 0.250;
    constexpr double SnapDistance = 30.0;

    struct FArrival { double Send = 0.0, Receive = 0.0; };
    // Minimum transit includes the unknown minimum one-way latency. It is not a clock sync protocol.
    inline double EstimateOffset(const std::vector<FArrival>& Arrivals)
    {
        if (Arrivals.empty()) return 0.0;
        const std::size_t Begin = Arrivals.size() > WindowSize ? Arrivals.size() - WindowSize : 0;
        double Offset = Arrivals[Begin].Receive - Arrivals[Begin].Send;
        for (std::size_t I = Begin + 1; I < Arrivals.size(); ++I)
            Offset = std::min(Offset, Arrivals[I].Receive - Arrivals[I].Send);
        return Offset;
    }

    // Delay required just before each arrival: the previous snapshot must still bracket playout.
    // This measures transit jitter AND missing send intervals. Nearest-rank empirical percentile.
    inline double ChooseDelay(const std::vector<FArrival>& Arrivals, double Offset)
    {
        if (Arrivals.size() < 2) return SendInterval;
        const std::size_t Begin = Arrivals.size() > WindowSize ? Arrivals.size() - WindowSize : 1;
        std::vector<double> Required;
        for (std::size_t I = Begin; I < Arrivals.size(); ++I)
            Required.push_back(std::max(SendInterval, Arrivals[I].Receive - Offset - Arrivals[I - 1].Send));
        std::sort(Required.begin(), Required.end());
        return Required[static_cast<std::size_t>(std::ceil(Coverage * Required.size())) - 1];
    }

    inline double Slew(double Current, double Target, double Dt)
    {
        const double Step = SlewRate * std::max(Dt, 0.0);
        return Current + std::clamp(Target - Current, -Step, Step);
    }
    // Initialize once to ReceiveNow - Offset - Delay. Never jump on subsequent estimator changes.
    inline double AdvancePlayout(double Current, double ReceiveNow, double Offset, double Delay, double Dt)
    {
        return Slew(Current + std::max(Dt, 0.0), ReceiveNow - Offset - Delay, Dt);
    }

    struct FVector { double X = 0.0, Y = 0.0, Z = 0.0; };
    inline FVector operator+(FVector A, FVector B) { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
    inline FVector operator-(FVector A, FVector B) { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
    inline FVector operator*(FVector A, double S) { return { A.X * S, A.Y * S, A.Z * S }; }
    inline double Length(FVector V) { return std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z); }
    struct FQuat { double X = 0.0, Y = 0.0, Z = 0.0, W = 1.0; };
    inline FQuat Normalize(FQuat Q)
    {
        const double N = std::sqrt(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
        return { Q.X / N, Q.Y / N, Q.Z / N, Q.W / N };
    }
    inline FQuat Multiply(FQuat A, FQuat B)
    {
        return { A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y,
            A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X,
            A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W,
            A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z };
    }
    inline FQuat Slerp(FQuat A, FQuat B, double T)
    {
        double Dot = A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
        if (Dot < 0.0) { B = { -B.X, -B.Y, -B.Z, -B.W }; Dot = -Dot; }
        double U = 1.0 - T, V = T;
        if (Dot < 0.9995)
        {
            const double Angle = std::acos(std::clamp(Dot, 0.0, 1.0));
            U = std::sin((1.0 - T) * Angle) / std::sin(Angle);
            V = std::sin(T * Angle) / std::sin(Angle);
        }
        return Normalize({ A.X * U + B.X * V, A.Y * U + B.Y * V, A.Z * U + B.Z * V, A.W * U + B.W * V });
    }
    struct FSnapshot
    {
        double Time = 0.0;
        FVector Position;
        FQuat Rotation;
        FVector Velocity;
        FVector AngularVelocity;
    };
    enum class ESample { Empty, Held, Interpolated, Extrapolated };
    struct FSample { FSnapshot State; ESample Mode = ESample::Empty; };

    inline FSample Sample(const std::vector<FSnapshot>& Buffer, double Time)
    {
        if (Buffer.empty()) return {};
        if (Time < Buffer.front().Time) return { Buffer.front(), ESample::Held };
        for (std::size_t I = 1; I < Buffer.size(); ++I)
        {
            const auto& A = Buffer[I - 1];
            const auto& B = Buffer[I];
            if (Time > B.Time) continue;
            const double Span = B.Time - A.Time, T = (Time - A.Time) / Span;
            const double T2 = T * T, T3 = T2 * T;
            FSnapshot Out;
            Out.Time = Time;
            Out.Position = A.Position * (2 * T3 - 3 * T2 + 1) + A.Velocity * ((T3 - 2 * T2 + T) * Span)
                + B.Position * (-2 * T3 + 3 * T2) + B.Velocity * ((T3 - T2) * Span);
            Out.Velocity = A.Position * ((6 * T2 - 6 * T) / Span) + A.Velocity * (3 * T2 - 4 * T + 1)
                + B.Position * ((-6 * T2 + 6 * T) / Span) + B.Velocity * (3 * T2 - 2 * T);
            Out.Rotation = Slerp(A.Rotation, B.Rotation, T);
            Out.AngularVelocity = A.AngularVelocity * (1 - T) + B.AngularVelocity * T;
            return { Out, ESample::Interpolated };
        }
        FSnapshot Out = Buffer.back();
        const double Ahead = Time - Out.Time, Step = std::min(Ahead, ExtrapolationLimit);
        Out.Position = Out.Position + Out.Velocity * Step;
        const double Speed = Length(Out.AngularVelocity);
        if (Speed > 0.0)
        {
            const FVector Axis = Out.AngularVelocity * (std::sin(Speed * Step * 0.5) / Speed);
            Out.Rotation = Normalize(Multiply({ Axis.X, Axis.Y, Axis.Z, std::cos(Speed * Step * 0.5) }, Out.Rotation));
        }
        Out.Time += Step;
        if (Ahead >= ExtrapolationLimit) { Out.Velocity = {}; Out.AngularVelocity = {}; }
        return { Out, Ahead >= ExtrapolationLimit ? ESample::Held : ESample::Extrapolated };
    }

    struct FCorrection { FVector Position; FQuat Rotation; bool bSnap = false; };
    inline FCorrection BeginCorrection(const FSnapshot& Shown, const FSnapshot& Target, bool bReset = false)
    {
        const FVector Error = Shown.Position - Target.Position;
        if (bReset || Length(Error) >= SnapDistance) return { {}, {}, true };
        const FQuat Inverse = { -Target.Rotation.X, -Target.Rotation.Y, -Target.Rotation.Z, Target.Rotation.W };
        return { Error, Normalize(Multiply(Shown.Rotation, Inverse)), false };
    }
    // Apply to the newly sampled pose, not a frozen target. Rebase from the displayed pose on a new correction.
    // Velocity remains the physical sample velocity; this is a visual correction only.
    inline FSnapshot ApplyCorrection(FSnapshot Target, const FCorrection& Correction, double Age)
    {
        if (Correction.bSnap) return Target;
        const double T = std::clamp(Age / CorrectionDuration, 0.0, 1.0);
        const double Weight = 1.0 - T * T * (3.0 - 2.0 * T);
        Target.Position = Target.Position + Correction.Position * Weight;
        Target.Rotation = Normalize(Multiply(Slerp({}, Correction.Rotation, Weight), Target.Rotation));
        return Target;
    }
}
