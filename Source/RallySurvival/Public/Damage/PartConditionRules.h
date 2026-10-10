#pragma once
#include <cmath>

// Engine-independent rules shared by the component and standalone tests.
// Health is normalized: 0 = failed, 1 = fully repaired.
namespace Rally::Damage
{
    constexpr float BodgeHealth = 0.6f; // Prototype tuning value, not final balance.

    inline bool ApplyDamage(float& Health, float Amount)
    {
        if (!std::isfinite(Amount) || Amount <= 0.0f || Health <= 0.0f)
            return false;
        const float Next = Amount >= Health ? 0.0f : Health - Amount;
        if (Next == Health)
            return false;
        Health = Next;
        return true;
    }

    inline bool Repair(float& Health, bool& IsBodged, bool ProperFix)
    {
        const float Target = ProperFix ? 1.0f : BodgeHealth;
        // A bodge must never worsen a healthier part or replenish it repeatedly.
        if (ProperFix ? (Health >= 1.0f && !IsBodged) : (IsBodged || Health >= Target))
            return false;
        Health = Target;
        IsBodged = !ProperFix;
        return true;
    }
}
