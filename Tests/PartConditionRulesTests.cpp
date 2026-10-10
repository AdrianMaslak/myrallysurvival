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

    std::cout << (Checks - Failures) << "/" << Checks << " checks passed\n";
    return Failures == 0 ? 0 : 1;
}
