#include "Damage/ImpactRules.h"
#include "Damage/PartConditionRules.h"
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    int Failures = 0;
    int Checks = 0;
    auto Check = [&](bool Pass, const char* Label) {
        ++Checks;
        if (!Pass) { ++Failures; std::cerr << "FAIL: " << Label << '\n'; }
    };
    auto Near = [](float A, float B) { return std::abs(A - B) < 0.00001f; };
    float Health = 1.0f;
    bool Bodged = false;

    Check(!Rally::Damage::ApplyDamage(Health, -0.5f) && Health == 1.0f, "negative damage cannot heal");
    Check(!Rally::Damage::ApplyDamage(Health, 0.0f), "zero damage is a no-op");
    Check(!Rally::Damage::ApplyDamage(Health, std::numeric_limits<float>::quiet_NaN()) && Health == 1.0f, "NaN is rejected");
    Check(!Rally::Damage::ApplyDamage(Health, std::numeric_limits<float>::infinity()) && Health == 1.0f, "infinity is rejected");
    Check(Rally::Damage::ApplyDamage(Health, 0.25f) && Near(Health, 0.75f), "partial damage");
    Check(!Rally::Damage::Repair(Health, Bodged, false) && Near(Health, 0.75f) && !Bodged, "bodge cannot worsen healthy part");
    Check(Rally::Damage::ApplyDamage(Health, 100.0f) && Health == 0.0f, "puncture clamps to failed");
    Check(!Rally::Damage::ApplyDamage(Health, 0.1f), "failed part stays failed");
    Check(Rally::Damage::Repair(Health, Bodged, false) && Near(Health, 0.6f) && Bodged, "bodge restores partial health");
    Check(!Rally::Damage::Repair(Health, Bodged, false), "duplicate bodge is rejected");
    Check(Rally::Damage::ApplyDamage(Health, 0.2f) && Near(Health, 0.4f), "bodged part can take damage");
    Check(!Rally::Damage::Repair(Health, Bodged, false) && Near(Health, 0.4f), "repeated bodge cannot replenish health");
    Check(Rally::Damage::Repair(Health, Bodged, true) && Health == 1.0f && !Bodged, "proper fix restores and clears bodge");
    Check(!Rally::Damage::Repair(Health, Bodged, true), "duplicate proper fix is rejected");

    for (int I = 0; I < 1000; ++I)
        Rally::Damage::ApplyDamage(Health, 0.007f);
    Check(Health == 0.0f, "repeated damage never makes health negative");
    Check(Rally::Damage::Repair(Health, Bodged, true) && Health == 1.0f, "failed part can be properly repaired");

    // Crash rules.
    using namespace Rally::Impact;
    Check(DentFor(1.5).Depth == 0.0 && TyreDamage(1.5) == 0.0, "walking-pace knock leaves no mark");
    Check(DentFor(5.6).Depth > 5.0 && DentFor(5.6).Depth < 12.0 && TyreDamage(5.6) < 0.25, "20 km/h tap dents without wrecking a tyre");
    Check(DentFor(13.9).Depth > 18.0 && TyreDamage(13.9) >= 1.0 && RadiatorDamage(13.9) >= 1.0, "50 km/h into a wall crumples, flattens and breaks the radiator");
    Check(DentFor(200.0).Depth == 0.0 && SuspensionDamage(200.0) == 0.0, "absurd impact is rejected");
    Check(DentFor(std::numeric_limits<double>::quiet_NaN()).Depth == 0.0, "NaN impact is rejected");
    Check(DentFor(40.0).Depth <= 28.0 && DentFor(40.0).Radius <= 85.0, "dent size is capped");
    Check(ReachFactor(50.0, WheelReachCm, 40.0) == 1.0 && ReachFactor(90.0, WheelReachCm, 40.0) == 0.5
        && ReachFactor(120.0, WheelReachCm, 40.0) == 0.0 && ReachFactor(71.0, WheelReachCm, 0.0) == 0.0, "reach fades over the crush distance");
    Check(ToeOutDeg(1.0) == 0.0 && ToeOutDeg(0.0) == MaxToeOutDeg && ToeOutDeg(-3.0) == MaxToeOutDeg, "toe follows suspension health");

    std::cout << (Checks - Failures) << "/" << Checks << " checks passed\n";
    return Failures == 0 ? 0 : 1;
}
