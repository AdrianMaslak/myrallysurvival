#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Money (docs/MONEY.md): Slovak koruna, a team kitty, prize money and a mail-order parts catalogue. Prices
// are a privateer's in the late 90s. No engine dependencies.
namespace Rally::Economy
{
    constexpr int StartingKittySk = 15000;
    // Each player's own cash, and what M takes from (or Shift+M puts back in) the kitty at a time.
    constexpr int StartingPocketSk = 500;
    constexpr int KittyStepSk = 500;
    // The loan for the car from Varga Holding (GAME-PLAN.md, Story): paid off every seventh day at 08:00
    // from the kitty; a missed payment is added to the debt with a tenth on top.
    constexpr int StartingLoanSk = 60000;
    constexpr int LoanPaymentSk = 2500;
    constexpr int LoanEveryDays = 7;
    inline int MissedPaymentSk(int Payment) { return Payment + Payment / 10; }
    // Bought for oneself, paid from one's own pocket: the village's vices.
    inline bool PersonalItem(const char* Kind) { return std::string(Kind) == "slivovica" || std::string(Kind) == "cigarettes"; }
    // Each start of a special stage is paid at the line.
    constexpr int StageEntrySk = 1000;
    // What a stage result pays: the first six places, then something for finishing at all.
    inline int PrizeSk(int Position, bool bRetired)
    {
        if (bRetired || Position < 1) return 0;
        static const int Table[] = { 6000, 4000, 3000, 2000, 1500, 1000 };
        return Position <= 6 ? Table[Position - 1] : 500;
    }

    // What the catalogue sells: a kind of part (the game's slot kind name), its grade (Rally::Grade::EGrade
    // as a number) and price.
    struct FItem { const char* Kind; uint8_t Grade; int PriceSk; const char* Name; };
    inline const std::vector<FItem>& Catalogue()
    {
        static const std::vector<FItem> Items = {
            { "strut", 1, 2500, "front strut" },
            { "strut", 2, 5500, "sport front strut" },
            { "strut", 3, 9000, "rally front strut (adjustable)" },
            { "rearstrut", 2, 5000, "sport rear strut" },
            { "rearstrut", 3, 8000, "rally rear strut (adjustable)" },
            { "disc", 1, 1500, "front brake disc" },
            { "disc", 2, 4000, "sport front disc and caliper" },
            { "disc", 3, 6500, "rally front disc and caliper" },
            { "reardisc", 2, 3500, "sport rear disc and caliper" },
            { "turbo", 1, 12000, "turbo" },
            { "turbo", 2, 18000, "sport turbo" },
            { "turbo", 3, 30000, "rally turbo" },
            { "ecu", 2, 6000, "sport chip engine computer" },
            { "ecu", 3, 12000, "rally engine computer" },
            { "intercooler", 1, 3500, "intercooler" },
            { "coil", 1, 450, "ignition coil" },
            { "coil", 2, 900, "sport ignition coil (red)" },
            { "firstaid", 1, 350, "first-aid kit" },
            { "slivovica", 1, 180, "bottle of slivovica" },
            { "cigarettes", 1, 400, "carton of cigarettes" },
        };
        return Items;
    }
    // Workshop jobs in the service yard: what the roadside cannot do, at a price and in hours of the day.
    struct FJob { const char* Name; int PriceSk; double Hours; };
    enum class EJob { Radiator, Battery, Sump, Tank, Exhaust, Gearbox, Engine, Body, Fuel };
    inline FJob Job(EJob Kind)
    {
        switch (Kind)
        {
        case EJob::Radiator: return { "a new radiator", 2500, 0.5 };
        case EJob::Battery: return { "a new battery", 1500, 0.2 };
        case EJob::Sump: return { "the sump welded", 1200, 0.5 };
        case EJob::Tank: return { "the fuel tank welded", 1500, 0.7 };
        case EJob::Exhaust: return { "the exhaust welded and hung", 1800, 0.5 };
        case EJob::Gearbox: return { "the gearbox rebuilt", 8000, 2.0 };
        case EJob::Engine: return { "the engine rebuilt", 15000, 4.0 };
        case EJob::Fuel: return { "the tank filled", 0, 0.1 };
        default: return { "the body straightened and its fixings renewed", 4000, 2.0 };
        }
    }
    // Fuel at the service yard's pump.
    constexpr int FuelSkPerLitre = 30;
    // A tow back to the service yard: the last resort when the car cannot go on.
    constexpr int TowSk = 2000;
    constexpr double TowHours = 2.0;
    // Within this far (m) of the service yard's middle.
    constexpr double ServiceYardM = 25.0;

    // Ordered by phone, it comes to the service yard with the next morning's post (08:00).
    constexpr double DeliveryHour = 8.0;

    // "15 000 Sk"
    inline std::string Money(int Sk)
    {
        std::string Digits = std::to_string(Sk < 0 ? -Sk : Sk);
        std::string Out;
        for (size_t I = 0; I < Digits.size(); ++I)
        {
            if (I > 0 && (Digits.size() - I) % 3 == 0) Out += ' ';
            Out += Digits[I];
        }
        return (Sk < 0 ? "-" : "") + Out + " Sk";
    }
}
