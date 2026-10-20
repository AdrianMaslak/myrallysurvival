#include "Damage/CrushProfiles.h"
#include "Damage/EngineRules.h"
#include "Damage/FixingRules.h"
#include "Damage/ImpactRules.h"
#include "Damage/LeakRules.h"
#include "Damage/PanelRules.h"
#include "Damage/PartGradeRules.h"
#include "Damage/PartConditionRules.h"
#include "Damage/SuspensionRules.h"
#include "Damage/UndersideRules.h"
#include "Game/CalendarRules.h"
#include "Game/EconomyRules.h"
#include "Game/StageRules.h"
#include "Game/WeatherRules.h"
#include "Player/BodyRules.h"
#include "Vehicle/CabinRules.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

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
    Check(!TearsWheelOff(17.1, 1.0) && !TearsWheelOff(17.1, 0.7), "50 km/h barrier hit keeps a healthy wheel on");
    Check(TearsWheelOff(17.1, 0.0) && TearsWheelOff(6.0, 0.0), "hit on an already broken corner tears the wheel off");
    Check(!TearsWheelOff(4.0, 0.0), "kerb knock on a broken corner keeps the wheel");
    Check(TearsWheelOff(22.0, 1.0), "huge direct hit tears even a healthy wheel off");
    Check(!TearsWheelOff(60.0, 0.0) && !TearsWheelOff(std::numeric_limits<double>::quiet_NaN(), 0.0)
        && !TearsWheelOff(10.0, std::numeric_limits<double>::quiet_NaN()), "invalid wheel-off input is rejected");

    // Panels: a crash weakens a fixing; motion causes the delayed failure.
    using namespace Rally::Panel;
    FFixings Panel;
    Check(Stage(Panel, false) == EStage::Intact, "new panel intact");
    Panel.Latch = 100;
    Check(Stage(Panel, false) == EStage::Loose, "damaged latch is loose, not open");
    double Remainder = 0;
    for (int I = 0; I < 300; ++I) Advance(Panel,false,Remainder,0.1,70,0);
    Check(Panel.Latch == 100, "loose latch survives below 80 km/h without jolts");
    for (int I = 0; I < 490; ++I) Advance(Panel,false,Remainder,0.1,100,0);
    Check(Panel.Latch > 0, "loose bonnet does not fly up immediately");
    for (int I = 0; I < 20; ++I) Advance(Panel,false,Remainder,0.1,100,0);
    Check(Stage(Panel,false) == EStage::Hanging && Panel.A == 255, "speed eventually opens latch with hinges still attached");
    FFixings Fine{255,255,100,0}, Coarse = Fine;
    double RFine=0, RCoarse=0;
    for (int I=0; I<6000; ++I) Advance(Fine,false,RFine,0.01,100,0);
    for (int I=0; I<600; ++I) Advance(Coarse,false,RCoarse,0.1,100,0);
    Check(std::abs(Fine.A-Coarse.A)<=1 && Fine.Latch==Coarse.Latch, "panel wear independent of frame rate");
    FFixings Pristine;
    for (int I=0; I<10000; ++I) Advance(Pristine,false,Remainder,0.1,160,2);
    Check(Pristine.A==255 && Pristine.Latch==255, "normal use cannot wear undamaged fixings");
    FFixings Clip{0,200,255,0};
    Check(Stage(Clip,true)==EStage::Hanging, "one bumper clip leaves it hanging");
    Clip.B=0;
    Check(Stage(Clip,true)==EStage::Detached, "both bumper clips gone detaches despite unused latch");
    FFixings Strapped{0,0,0,180};
    Check(Stage(Strapped,false)==EStage::Loose, "strap holds a detached panel on");
    for(int I=0;I<100;++I) Advance(Strapped,false,Remainder,0.1,120,0);
    Check(Strapped.Strap==180, "strap survives steady speed");
    for(int I=0;I<100;++I) Advance(Strapped,false,Remainder,0.1,0,4);
    Check(Stage(Strapped,false)==EStage::Detached, "big jolts can break a bodge");
    Check(Damage(255,-1)==255 && Damage(255,std::numeric_limits<double>::quiet_NaN())==255, "invalid fixing damage rejected");
    Check(Damage(255,100)==0 && ImpactLoss(200,true)==0, "fixing damage bounded");
    Check(CanRefit(15) && !CanRefit(15.01) && !CanRefit(std::numeric_limits<double>::quiet_NaN()), "crushed and unknown panels cannot be bolted on");
    const FFixings BeforeInvalid = Fine;
    Advance(Fine,false,RFine,0.1,std::numeric_limits<double>::quiet_NaN(),0);
    Check(Fine.A==BeforeInvalid.A, "invalid wear input cannot corrupt fixing health");

    // Opening by hand.
    Check(CanOpenByHand(FFixings{}, false, false) && CanOpenByHand(FFixings{255,255,100,0}, false, false), "shut panel on a working latch opens");
    Check(!CanOpenByHand(FFixings{}, true, false), "bumpers do not open");
    Check(!CanOpenByHand(FFixings{}, false, true), "jammed door does not open");
    Check(!CanOpenByHand(FFixings{255,255,255,180}, false, false), "strapped panel stays shut");
    Check(!CanOpenByHand(FFixings{255,255,0,0}, false, false) && !CanOpenByHand(FFixings{0,255,255,0}, false, false),
        "hanging panel cannot be latched open or shut");

    // Refit fit.
    Check(FitSloppiness(0, 0, 0, false) > 0.0 && FitSloppiness(0, 0, 0, false) < HalfCaughtSloppiness, "careful bolt-on is close but never factory-perfect");
    Check(FitSloppiness(1, 0, 0, false) > FitSloppiness(0, 0, 0, false) && FitSloppiness(0, 30, 0, false) > FitSloppiness(0, 0, 0, false)
        && FitSloppiness(0, 0, 15, false) > FitSloppiness(0, 0, 0, false), "rushing, poor aim and a bent panel each fit worse");
    Check(FitSloppiness(1, 30, 15, false) >= HalfCaughtSloppiness, "rushed bolt-on of a bent panel only half catches");
    Check(FitSloppiness(0, 0, 0, true) > FitSloppiness(1, 30, 15, false), "strap job is cruder than any bolt-on");
    Check(FitSloppiness(5, 500, 500, false) == FitSloppiness(1, 30, 15, false), "fit inputs are bounded");
    Check(FitSloppiness(std::numeric_limits<double>::quiet_NaN(), 0, 0, false) >= HalfCaughtSloppiness, "invalid fit input fits badly");
    Check(RefitRush(0.75) == 1.0 && RefitRush(2.5) == 0.0 && RefitRush(1.375) > 0.49 && RefitRush(1.375) < 0.51, "rush follows bolting pace");

    // Hinge motion.
    FHinge Swing;
    for (int I = 0; I < 2400; ++I) StepHinge(Swing, 5.0, 2.0, 3.0, 0.0, 70.0, 0.3, 1.0 / 240.0);
    Check(Swing.Angle == 70.0, "steady torque swings a panel to its stop and holds it there");
    FHinge Bounce{69.9, 200.0};
    StepHinge(Bounce, 0.0, 2.0, 0.0, 0.0, 70.0, 0.3, 1.0 / 240.0);
    Check(Bounce.Angle == 70.0 && std::abs(Bounce.Speed + 60.0) < 0.001, "stop bounces the panel back with restitution");
    FHinge Coast{30.0, 100.0};
    for (int I = 0; I < 2400; ++I) StepHinge(Coast, 0.0, 2.0, 3.0, -90.0, 90.0, 0.3, 1.0 / 240.0);
    Check(std::abs(Coast.Speed) < 0.01 && Coast.Angle > 30.0 && Coast.Angle < 90.0, "hinge friction stops a free swing");
    FHinge Slam{69.9, 200.0};
    Check(std::abs(StepHinge(Slam, 0.0, 2.0, 0.0, 0.0, 70.0, 0.3, 1.0 / 240.0) - 200.0) < 0.001, "hinge reports how hard it struck its stop");
    FHinge Free{30.0, 10.0};
    Check(StepHinge(Free, 0.0, 2.0, 0.0, 0.0, 70.0, 0.3, 1.0 / 240.0) == 0.0, "swinging between stops strikes nothing");
    FHinge Kept{10.0, 5.0};
    StepHinge(Kept, std::numeric_limits<double>::quiet_NaN(), 2.0, 3.0, 0.0, 70.0, 0.3, 0.01);
    StepHinge(Kept, 1.0, 0.0, 3.0, 0.0, 70.0, 0.3, 0.01);
    Check(Kept.Angle == 10.0 && Kept.Speed == 5.0, "invalid hinge input is ignored");

    // Swing damage.
    Check(SwingHitLoss(2.5, true) == 0.0 && SwingHitLoss(2.5, false) == 0.0, "nudging a panel into something does not hurt the hinges");
    Check(SwingHitLoss(8.0, true) > SwingHitLoss(8.0, false), "forcing a panel past its stop hurts more than slamming it shut");
    Check(SwingHitLoss(7.5, true) >= 1.0 && SwingHitLoss(5.0, true) < 0.5, "27 km/h past the stop tears the hinges, 18 km/h bends them");
    Check(SwingHitLoss(12.0, false) < 0.5, "slamming shut alone does not tear a panel off");
    Check(SwingHitLoss(100.0, true) == 0.0 && SwingHitLoss(std::numeric_limits<double>::quiet_NaN(), true) == 0.0, "invalid swing hit is ignored");
    Check(OverextendLoss(2.0) == 0.0 && OverextendLoss(9.0) > 0.4 && OverextendLoss(100.0) == 1.0, "slamming against the open stop strains the hinges");
    Check(UnboltActions > PullOffActions && PullOffActions > CutStrapActions && CutStrapActions >= 1, "unbolting takes longer than pulling a hanging panel off");

    // Crush profiles: each part gives its own way.
    using namespace Rally::Crush;
    Check(ForKind(EKind::Bumper).Springback > 0.3 && ForKind(EKind::Bonnet).Springback == 0.0, "plastic bumper springs back, steel bonnet does not");
    Check(ForKind(EKind::Bonnet).Folding > ForKind(EKind::Bumper).Folding && ForKind(EKind::Bonnet).FoldWavelength < ForKind(EKind::Bumper).FoldWavelength, "bonnet creases, bumper warps");
    Check(ForKind(EKind::Door).Give < ForKind(EKind::Shell).Give && ForKind(EKind::Door).Give < ForKind(EKind::Bonnet).Give, "doors with beams give less than shell and bonnet");
    Check(ForKind(EKind::Radiator).FoldWavelength < ForKind(EKind::Shell).FoldWavelength, "radiator crushes finely");
    Check(ForKind(EKind::Fender).Give > ForKind(EKind::Shell).Give, "bolt-on wing crumples more readily than the shell");
    Check(ForKind(EKind::Glass).Give < ForKind(EKind::Roof).Give && ForKind(EKind::Glass).Scuff > ForKind(EKind::Shell).Scuff, "windscreen crazes rather than bends");
    Check(ForKind(EKind::Lamp).Folding < 0.5 && ForKind(EKind::Lamp).Scuff > 2.0, "lamps crack rather than crease");
    Check(ForKind(EKind::Engine).Give < ForKind(EKind::Interior).Give && ForKind(EKind::Interior).Give < ForKind(EKind::BumperBar).Give, "engine castings barely move, the cabin a little");
    Check(KindOfPanel(1) == EKind::Door && KindOfPanel(4) == EKind::Bonnet && KindOfPanel(8) == EKind::Bumper && KindOfPanel(10) == EKind::Fender
        && KindOfPanel(13) == EKind::Lamp && KindOfPanel(-1) == EKind::Shell && KindOfPanel(99) == EKind::Shell, "panel layout maps to kinds");
    bool AllSane = true;
    for (int Kind = 0; Kind <= static_cast<int>(EKind::Engine); ++Kind)
    {
        const FProfile P = ForKind(static_cast<EKind>(Kind));
        AllSane = AllSane && P.Give > 0 && P.FoldWavelength > 0 && P.Springback >= 0 && P.Springback < 1 && P.MaxCrushCm > 0 && P.Scuff > 0;
    }
    Check(AllSane, "every profile is physical");

    // Running gear.
    {
        using namespace Rally::Suspension;
        Check(SpringRateScale(1.0) == 1.0 && DampingScale(1.0) == 1.0, "healthy strut is unchanged");
        Check(SpringRateScale(0.0) == 0.5 && DampingScale(0.0) < 0.25, "broken strut sags and bounces");
        Check(Steers(0.01) && !Steers(0.0), "only a snapped front arm disconnects the steering");
        Check(Drives(0.2) && !Drives(0.0), "only a broken driveshaft stops the drive");
        Check(BrakeScale(1.0) == 1.0 && BrakeScale(0.0) > 0.1 && BrakeScale(0.0) < 0.2, "damaged brake still brakes a little");
        Check(StrutDamage(3.0, 1.0) == 0.0 && StrutDamage(17.1, 1.0) > 0.9 && StrutDamage(17.1, 0.0) == 0.0, "struts suffer from hard direct hits only");
        Check(DriveshaftDamage(17.1, 1.0) < StrutDamage(17.1, 1.0) && DriveshaftDamage(5.0, 1.0) == 0.0, "driveshafts take a harder hit");
        Check(BrakeDamage(17.1, 1.0) < StrutDamage(17.1, 1.0), "discs are better protected than struts");
        Check(StrutDamage(std::numeric_limits<double>::quiet_NaN(), 1.0) == 0.0 && SpringRateScale(std::numeric_limits<double>::quiet_NaN()) == 1.0,
            "invalid running-gear input is ignored");
    }

    // Engine, cooling and battery.
    {
        using namespace Rally::Engine;
        FModules Sparks;
        Check(CoilMisfire(1.0, 0.8, 1.1, 1.0) == 0.0 && CoilMisfire(0.0, 0.8, 1.1, 0.0) == 1.0 && CoilMisfire(0.3, 0.8, 1.1, 0.0) > 0.25
            && CoilMisfire(1.0, 1.4, 1.1, 1.0) > 0.2 && CoilMisfire(1.0, 1.4, 1.1, 0.0) == 0.0 && CoilMisfire(1.0, 1.4, 1.5, 1.0) == 0.0
            && CoilMisfire(std::numeric_limits<double>::quiet_NaN(), 0.8, 1.1, 1.0) == 1.0,
            "a coil misfires when weak, or under load at boost past what it holds; a dead one never fires");
        Sparks.Coils[2] = 0.0;
        const double OneDeadPower = ModulePower(Sparks);
        Sparks.Coils[1] = 0.0;
        const bool bTwoRun = Fires(Sparks);
        Sparks.Coils[0] = 0.0;
        Check(FiringCylinders(Sparks) == 1 && !Fires(Sparks) && bTwoRun && std::abs(OneDeadPower - 0.75) < 1e-9,
            "a dead coil takes its cylinder's quarter of the power; on one cylinder it will not run");
        FModules Boosted;
        Boosted.BoostBar = 1.4;
        Check(MisfireShare(Boosted, 1.0) > 0.2 && MisfireShare(Boosted, 0.0) == 0.0, "stock coils misfire at 1.4 bar under load");
        for (double& Limit : Boosted.CoilBoostLimit) Limit = Rally::Grade::CoilBoostLimit(Rally::Grade::EGrade::Sport);
        Check(MisfireShare(Boosted, 1.0) == 0.0, "sport coils hold 1.4 bar");
        FModules Stock;
        Check(ModulePower(Stock) == 1.0 && EffectiveBoost(Stock) == 0.8, "complete stock engine retains factory power and boost");
        for (double FModules::* Part : { &FModules::Turbo, &FModules::ExhaustManifold, &FModules::Intercooler })
        {
            FModules Missing;
            Missing.*Part = 0.0;
            Missing.BoostBar = 1.4;
            Missing.Tune = 1.5;
            Check(std::abs(ModulePower(Missing) - 0.65) < 1e-9 && EffectiveBoost(Missing) == 0.0
                && MisfireShare(Missing, 1.0) == 0.0, "broken boost path loses boost and turbo upgrades cannot restore it");
            Missing.*Part = 0.5;
            const double HalfPower = ModulePower(Missing);
            Missing.*Part = 1.0;
            Check(HalfPower > 0.65 && HalfPower < ModulePower(Missing), "damaged boost components progressively reduce power");
        }
        FModules Mixed;
        Mixed.Coils[0] = 0.0;
        Mixed.Coils[1] = 0.3;
        Check(std::abs(ModulePower(Mixed) * (1.0 - IntermittentPowerLoss(Mixed, 0.0))
            - (1.0 - IgnitionLoss(Mixed, 0.0))) < 1e-9, "dead and weak cylinders each remove exactly their share of torque");
        const auto StallAfter = [](const FModules& M, double Rpm, double Dt) {
            FIgnitionStability State;
            for (int I = 0; I < static_cast<int>(60.0 / Dt); ++I)
                if (StepIgnition(State, M, true, Rpm, 0.0, Dt)) return (I + 1) * Dt;
            return 0.0;
        };
        FModules OneDead;
        OneDead.Coils[0] = 0.0;
        FModules TwoDead = OneDead;
        TwoDead.Coils[1] = 0.0;
        FModules Weak;
        for (double& Coil : Weak.Coils) Coil = 0.15;
        Check(StallAfter(Stock, 500.0, 0.02) == 0.0 && StallAfter(OneDead, 750.0, 0.02) == 0.0,
            "healthy engine and three sound cylinders sustain normal idle");
        Check(StallAfter(OneDead, 500.0, 0.02) > 2.0 && StallAfter(TwoDead, 600.0, 0.02) > 2.0
            && StallAfter(Weak, 650.0, 0.02) > 2.0, "low revs with missing or weak coils cause a stall after startup grace");
        Check(StallAfter(TwoDead, 2200.0, 0.02) == 0.0
            && std::abs(StallAfter(Weak, 650.0, 0.01) - StallAfter(Weak, 650.0, 0.1)) < 0.11,
            "holding revs sustains a damaged engine and stall timing is frame-rate independent");
        FIgnitionStability Recover;
        StepIgnition(Recover, TwoDead, true, 600.0, 0.0, 3.0);
        const double Deficit = Recover.Deficit;
        StepIgnition(Recover, TwoDead, true, 2200.0, 0.5, 1.0);
        Check(Deficit > 0.0 && Recover.Deficit == 0.0, "raising revs clears accumulated idle instability");
        StepIgnition(Recover, TwoDead, false, 0.0, 0.0, 0.1);
        Check(Recover.RunningSeconds == 0.0 && !StepIgnition(Recover, TwoDead, true, 300.0, 0.0, 1.0),
            "restart gets a fresh cranking grace period");
        OneDead.Coils[1] = std::numeric_limits<double>::quiet_NaN();
        Check(FiringCylinders(OneDead) == 2 && std::isfinite(IgnitionLoss(OneDead, 1.0)),
            "invalid coil health consistently counts as a dead cylinder");
        Check(HandThrottle(830.0) == 1.0 && HandThrottle(HandRevRpm) == 0.0 && HandThrottle(7000.0) == 0.0
            && HandThrottle(3000.0) > HandThrottle(3500.0) && HandThrottle(std::numeric_limits<double>::quiet_NaN()) == 0.0,
            "a hand on the throttle opens it from idle and eases off near 4000 rpm");
        // Runs a steady state for Seconds at 10 Hz; returns engine health left.
        const auto Run = [](FThermal& State, double PowerKw, double SpeedMs, double Radiator, double Seconds, bool bRunning = true) {
            double Health = 1.0;
            for (int Tick = 0; Tick < static_cast<int>(Seconds * 10.0); ++Tick)
            {
                FThermalInput In;
                In.bRunning = bRunning && Health > 0.0;
                In.PowerKw = PowerKw * PowerScale(Health, State.TempC); In.SpeedMs = SpeedMs; In.RadiatorHealth = Radiator;
                Health = std::max(Health - Step(State, In, 0.1), 0.0);
            }
            return Health;
        };
        FThermal Healthy;
        Check(Run(Healthy, 130.0, 27.8, 1.0, 300.0) == 1.0 && Healthy.TempC < 95.0 && Healthy.CoolantL == CoolantCapacityL,
            "healthy engine at full power and 100 km/h stays below 95 C");
        FThermal Climb;
        Check(Run(Climb, 130.0, 8.0, 1.0, 300.0) == 1.0 && Climb.TempC < WearStartC, "full power at 30 km/h runs warm but never wears");
        FThermal Idle;
        Run(Idle, 0.0, 0.0, 1.0, 600.0);
        Check(Idle.TempC > 75.0 && Idle.TempC < 90.0, "idling holds the thermostat temperature");
        FThermal Knock;
        Run(Knock, 45.0, 16.0, 0.8, 600.0);
        Check(Knock.CoolantL == CoolantCapacityL, "a lightly dented radiator does not leak");
        FThermal Crushed;
        const double AfterMinute = Run(Crushed, 45.0, 16.0, 0.04, 60.0);
        Check(AfterMinute == 1.0 && Crushed.CoolantL < 1.5 && Crushed.CoolantL > 0.0, "a crushed radiator empties in about a minute");
        const double AfterThree = Run(Crushed, 45.0, 16.0, 0.04, 120.0);
        Check(Crushed.TempC > WearStartC && AfterThree < 0.7, "with the coolant gone the engine overheats and wears");
        FThermal Seize;
        Seize.CoolantL = 0.0;
        Check(Run(Seize, 45.0, 16.0, 1.0, 400.0) == 0.0, "driving on with no coolant seizes the engine");
        FThermal Cool;
        Cool.TempC = 130.0; Cool.CoolantL = 0.0;
        Run(Cool, 0.0, 0.0, 1.0, 180.0, false);
        Check(Cool.TempC < 105.0 && Cool.TempC > AmbientC, "a stopped engine cools over a few minutes");
        FThermal Sealed;
        Run(Sealed, 45.0, 16.0, 0.6, 600.0);
        Check(Sealed.CoolantL > 2.5 && Sealed.TempC < 100.0, "a sealed radiator still seeps but lasts ten minutes");
        FThermal Pour;
        Pour.TempC = 130.0; Pour.CoolantL = 0.0;
        Check(std::abs(TopUp(Pour, 5.0) - 5.0) < 1e-9 && Pour.TempC < 105.0 && Pour.CoolantL == 5.0, "cold water cools a hot engine");
        Check(std::abs(TopUp(Pour, 5.0) - 1.0) < 1e-9 && Pour.CoolantL == CoolantCapacityL && TopUp(Pour, 1.0) == 0.0, "a full system takes no more");
        Check(TopUp(Pour, std::numeric_limits<double>::quiet_NaN()) == 0.0 && TopUp(Pour, -2.0) == 0.0, "invalid top-up is ignored");
        Check(PowerScale(1.0, WarmC) == 1.0 && PowerScale(0.0, WarmC) < 0.45 && PowerScale(1.0, 150.0) < 0.45, "worn or overheating engine loses power");
        Check(FrictionScale(1.0) == 1.0 && FrictionScale(0.0) > 2.0, "worn engine drags");
        Check(LeakRate(0.0, 90.0, true) > LeakRate(0.0, 90.0, false) && LeakRate(0.0, 90.0, false) > 0.0, "a stopped engine only drips");
        Check(CutOutRate(1.0) == 0.0 && CutOutRate(0.5) == 0.0 && CutOutRate(0.1) > 0.4 && CutOutRate(0.0) == 0.0, "a damaged battery cuts out now and then");
        Check(CanRun(1.0, 0.01) && !CanRun(1.0, 0.0) && !CanRun(0.0, 1.0), "a seized engine or flat battery stops the car");
        Check(EngineDamage(17.1, 0.2) < 0.1 && EngineDamage(30.0, 1.0) > 0.7 && BatteryDamage(17.1, 1.0) > 0.5 && BatteryDamage(5.0, 1.0) == 0.0,
            "castings take a huge hit, the battery a hard one");
        FThermal Bad;
        Bad.TempC = std::numeric_limits<double>::quiet_NaN();
        FThermalInput BadIn;
        BadIn.bRunning = true; BadIn.PowerKw = std::numeric_limits<double>::infinity();
        Check(Step(Bad, BadIn, 0.1) == 0.0 && std::isfinite(Bad.TempC) && Step(Bad, BadIn, -1.0) == 0.0, "invalid thermal input is ignored");
    }

    // Under the floor.
    {
        using namespace Rally::Underside;
        Check(OilLeakRate(0.9, true) == 0.0 && OilLeakRate(0.0, true) > OilLeakRate(0.0, false) && OilCapacityL / OilLeakRate(0.0, true) < 120.0,
            "a cracked sump empties in a minute or two, slower when stopped");
        Check(!OilLight(OilCapacityL) && OilLight(1.0) && OilLight(std::numeric_limits<double>::quiet_NaN()), "oil light below 1.5 L");
        Check(StarvationWear(2.0, 6000.0) == 0.0 && StarvationWear(0.0, 6000.0) > StarvationWear(0.0, 900.0)
            && 1.0 / StarvationWear(0.0, 6000.0) < 60.0, "no oil seizes a revving engine within a minute");
        Check(std::abs(FuelUse(40.0, true) * 3600.0 - 14.0) < 0.5 && FuelUse(40.0, false) == 0.0, "about 14 L an hour at 40 kW");
        Check(FuelLeakRate(0.8) == 0.0 && FuelCapacityL / FuelLeakRate(0.0) < 3600.0, "a holed tank drains within the hour");
        Check(!ExhaustHanging(0.6) && ExhaustHanging(0.2), "a badly hit exhaust hangs");
        Check(HasGear(5, 1.0) && !HasGear(5, 0.4) && HasGear(4, 0.4) && !HasGear(4, 0.2) && !HasGear(3, 0.1) && HasGear(2, 0.1)
            && !HasGear(2, 0.0) && !HasGear(-1, 0.0) && HasGear(-1, 0.1), "gears go from the top; at 0 none");
        Check(PopOutRate(0.8) == 0.0 && PopOutRate(0.1) > 0.15, "a damaged box jumps out of gear");
        Check(SumpDamage(0.3, 1.0) == 0.0 && SumpDamage(3.3, 1.0) >= 1.0 && SumpDamage(3.3, 0.0) == 0.0, "a hard floor hit splits the sump");
        Check(GearboxDamage(2.0, 1.0) < SumpDamage(2.0, 1.0) && ExhaustDamage(1.0, 1.0) > SumpDamage(1.0, 1.0), "exhaust most exposed, gearbox casing least");
        Check(TankDamage(std::numeric_limits<double>::quiet_NaN(), 1.0) == 0.0 && SumpDamage(60.0, 1.0) == 0.0, "invalid floor hits are ignored");
        Check(Concentration(27.0, 0.0) == 1.0 && Concentration(53.0, 41.0) == 1.0 && Concentration(225.0, 86.0) == 0.25,
            "a rock does full damage, the whole floor landing flat a quarter");
    }

    // Fixings.
    {
        using namespace Rally::Fixing;
        Check(GroupHold(0, 2) == 255 && GroupHold(1, 2) == 150 && GroupHold(2, 2) == 20 && GroupHold(0, 0) == 255,
            "a group holds fully, loose with one out, barely with all out");
        Check(Rally::Panel::Stage({GroupHold(1, 2), 255, 255, 0}, false) == Rally::Panel::EStage::Loose
            && Rally::Panel::Stage({GroupHold(2, 2), GroupHold(2, 2), 0, 0}, true) == Rally::Panel::EStage::Loose,
            "a panel with fixings out is loose, not off");
        Check(EffectiveHealth(1.0, 3, 3) == 1.0 && EffectiveHealth(1.0, 2, 3) == 0.5 && EffectiveHealth(0.3, 2, 3) == 0.3
            && EffectiveHealth(1.0, 0, 3) == 0.15 && EffectiveHealth(std::numeric_limits<double>::quiet_NaN(), 3, 3) == 0.0,
            "a part with fixings out acts damaged");
        double Carry = 0.0;
        int Out = 0;
        for (int Step = 0; Step < 600; ++Step) Out += WorkLoose(2 - Out, 4, Carry, 0.1, 60.0, 0.0);
        Check(Out == 1, "half the wheel bolts out: one more works loose in a minute at 60 km/h");
        Carry = 0.0;
        Check(WorkLoose(4, 4, Carry, 1.0, 100.0, 5.0) == 0 && WorkLoose(2, 4, Carry, 1.0, 0.0, 0.0) == 0,
            "nothing works loose with all home or standing still");
        Carry = 0.0;
        int Bumpy = 0;
        for (int Step = 0; Step < 100; ++Step) Bumpy += WorkLoose(3 - Bumpy, 4, Carry, 0.1, 40.0, 3.0);
        Check(Bumpy >= 1, "bumps shake a loose part's fixings out within seconds");
        Check(WorkLoose(2, 4, Carry, std::numeric_limits<double>::quiet_NaN(), 60.0, 0.0) == 0, "invalid step is ignored");
        Check(std::string(FixingName(Rally::Fixing::EKind::Bolt, Rally::Fixing::ETool::Spanner13)) == "13 mm bolt" && std::string(FixingName(Rally::Fixing::EKind::Screw, Rally::Fixing::ETool::Screwdriver)) == "screw"
            && std::string(ToolName(Rally::Fixing::ETool::Spanner17)) == "17 mm socket", "fixings and tools are named by size");
    }

    // Grades and settings.
    {
        using namespace Rally::Grade;
        Check(StrutGrade(EGrade::Stock).Spring == 1.0 && StrutGrade(EGrade::Rally).Spring > StrutGrade(EGrade::Sport).Spring
            && StrutGrade(EGrade::Sport).LowerCm > 0.0 && StrutGrade(EGrade::Worn).Damping < 1.0, "better struts are stiffer and lower, worn ones soft");
        Check(ClickDamping(DefaultClicks) == 1.0 && ClickDamping(1) < 1.0 && ClickDamping(MaxClicks) > 1.2 && ClickDamping(40) == ClickDamping(MaxClicks),
            "damper clicks from soft to hard, five as delivered");
        Check(BrakeGrade(EGrade::Rally) > BrakeGrade(EGrade::Sport) && BrakeGrade(EGrade::Worn) < 1.0, "brakes by grade");
        Check(BoostPower(DefaultBoostTenths) == 1.0 && std::abs(BoostPower(10) - 1.1) < 1e-9 && BoostPower(99) == BoostPower(MaxBoostTenths),
            "boost: a tenth of a bar is five percent");
        Check(KnockWear(9, EGrade::Stock, 1.0) == 0.0 && KnockWear(12, EGrade::Stock, 1.0) > 0.0 && KnockWear(12, EGrade::Rally, 1.0) == 0.0
            && KnockWear(12, EGrade::Stock, 0.0) == 0.0 && KnockWear(12, EGrade::Stock, std::numeric_limits<double>::quiet_NaN()) == 0.0,
            "too much boost knocks on a stock chip under load");
        Check(1.0 / KnockWear(12, EGrade::Stock, 1.0) > 200.0, "an over-boosted engine lasts minutes at full load, not seconds");
        Check(std::string(GradeName(EGrade::Rally)) == "rally" && std::string(GradeName(EGrade::Stock)).empty(), "grades are named, stock is not");
        Check(EngineMap(2).Power == 1.0 && EngineMap(3).Power > 1.0 && EngineMap(1).Fuel < 1.0 && EngineMap(9).Power == EngineMap(3).Power
            && KnockWear(9, EGrade::Stock, 1.0, 3) > 0.0 && KnockWear(10, EGrade::Stock, 1.0, 1) == 0.0, "the chip's maps: power, fuel, knock");
        Check(BiasScale(0, true) == 1.0 && BiasScale(0, false) == 1.0 && BiasPct(3) == 68.5 && BiasPct(99) == BiasPct(MaxBiasSteps)
            && BiasScale(3, true) > 1.0 && BiasScale(3, false) < 1.0, "brake bias moves braking between the axles");
    }

    // Stages.
    {
        using namespace Rally::Stage;
        // 100 m straight, a 90 degree right of 30 m radius, 60 m straight, a 180 degree left hairpin of 12 m, 60 m.
        std::vector<FPoint> Route;
        double X = 0.0, Y = 0.0, H = 0.0;
        const auto Run = [&](double Length, double Radius, int Sign) {
            for (double S = 0.0; S < Length; S += 3.0)
            {
                Route.push_back({ X, Y, 0.0 });
                X += 3.0 * std::cos(H); Y += 3.0 * std::sin(H);
                if (Radius > 0.0) H += Sign * 3.0 / Radius;
            }
        };
        Run(100.0, 0.0, 0);
        Run(30.0 * Pi / 2.0, 30.0, 1);
        Run(60.0, 0.0, 0);
        Run(12.0 * Pi, 12.0, -1);
        Run(60.0, 0.0, 0);
        const std::vector<FCall> Calls = PaceNotes(Route);
        Check(Calls.size() == 2, "two corners, two calls");
        Check(Calls.size() == 2 && Calls[0].Direction == 1 && Calls[0].Grade == 2 && Calls[0].AtM > 85.0 && Calls[0].AtM < 105.0,
            "a 30 m right is a right 2, called where it starts");
        Check(Calls.size() == 2 && Calls[1].Direction == -1 && Calls[1].bHairpin && CallText(Calls[1]) == "hairpin left", "a tight 180 left is a hairpin");
        Check(GradeForRadius(15.0) == 1 && GradeForRadius(60.0) == 4 && GradeForRadius(300.0) == 6, "grades from radius");
        Check(PaceNotes(std::vector<FPoint>(3)).empty(), "too short a route has no calls");
        Check(std::abs(ParSeconds(1700.0, "Gravel") - 100.0) < 1e-9 && MaxSeconds(100.0) == 310.0 && ParSeconds(-5.0, "Gravel") == 0.0,
            "reference and maximum times");
        bool Retired = false;
        const double A = RivalTime(Rivals()[0], 100.0, 42u, Retired);
        bool Retired2 = false;
        Check(A == RivalTime(Rivals()[0], 100.0, 42u, Retired2) && Retired == Retired2, "rivals' times are the same on every machine");
        int Outs = 0;
        double Sum = 0.0;
        for (uint32_t Seed = 1; Seed <= 400; ++Seed)
        {
            bool Out = false;
            const double T = RivalTime(Rivals()[1], 100.0, Seed * 7919u, Out);
            Outs += Out ? 1 : 0;
            if (!Out) Sum += T;
        }
        Check(Outs > 5 && Outs < 50 && Sum / (400 - Outs) > 95.0 && Sum / (400 - Outs) < 103.0, "a rival's pace and occasional retirements");
        Check(Clock(83.44) == "1:23.4" && Clock(-3.0) == "0:00.0", "stage clock reads m:ss.t");
        Check(CupPoints(1) == 10 && CupPoints(7) == 2 && CupPoints(8) == 0 && CupPoints(0) == 0, "cup points by place");
        Check(LatePenaltyS(-5.0) == 0.0 && LatePenaltyS(0.2) == 10.0 && LatePenaltyS(3.0) == 30.0 && LatePenaltyS(500.0) == 300.0
            && LatePenaltyS(std::numeric_limits<double>::quiet_NaN()) == 0.0, "10 s a minute late at a time control, at most 5 minutes");
    }

    // The crew's bodies.
    {
        using namespace Rally::Body;
        Check(InjuryFromImpact(5.0) == 0.0 && InjuryFromImpact(17.1) > 0.4 && InjuryFromImpact(17.1) < 0.55 && InjuryFromImpact(60.0) == 1.0
            && InjuryFromImpact(std::numeric_limits<double>::quiet_NaN()) == 0.0, "a 50 km/h wall hurts about halfway, a tap not at all");
        Check(WalkScale(0.0) == 1.0 && WalkScale(1.0) < 0.6 && Vignette(1.0) > Vignette(0.0) && Fringe(0.0) == 0.0, "hurt, sight and walking suffer");
        Check(std::abs(AmbientC(3.0) - 6.0) < 1e-9 && std::abs(AmbientC(15.0) - 22.0) < 1e-9, "nights at 6 C, afternoons at 22");
        Check(ColdRate(6.0, false, false) > 0.0 && 1.0 / ColdRate(6.0, false, false) < 700.0 && ColdRate(20.0, false, false) < 0.0
            && ColdRate(6.0, true, true) < ColdRate(6.0, true, false) && ColdRate(6.0, true, false) < 0.0, "cold outside at night, warm by the heater");
        Check(HandSlowdown(0.0) == 1.0 && HandSlowdown(1.0) == 2.0 && HandSlowdown(1.0, 1.0) == 1.5, "cold hands are slow, a smoke steadies them");
        Check(SwigDrunk * SwigsPerBottle == 1.0 && SwayDeg(0.0) == 0.0 && SwayDeg(1.0) > 0.0 && SteerWander(0.5) > 0.0
            && SwigDrunk / SoberPerSecond == 600.0, "a bottle makes you drunk; a swig wears off in ten minutes");
        Check(WetRate(1.0, 15.0, false, false) == 1.0 / 120.0 && WetRate(0.0, 20.0, false, false) < 0.0 && WetRate(1.0, 15.0, true, true) < WetRate(1.0, 15.0, true, false)
            && WetRate(1.0, 15.0, true, false) < 0.0, "soaked in a downpour in two minutes, dried by the heater");
        Check(FeltC(14.0, 0.0) == 14.0 && FeltC(14.0, 1.0) == 6.0 && ColdRate(FeltC(16.0, 1.0), false, false) > 0.0, "wet through, a mild evening is cold");
    }

    // The weather.
    {
        using namespace Rally::Weather;
        bool bDryFirst = true, bInRange = true, bSomeRain = false, bSomeDry = false;
        for (int Day = 1; Day <= 30; ++Day)
        {
            bool bRained = false;
            for (double Hour = 0.0; Hour < 24.0; Hour += 0.25)
            {
                const double Wet = Rain(Day, Hour);
                bInRange = bInRange && Wet >= 0.0 && Wet <= 1.0;
                if (Day == 1 && Wet > 0.0) bDryFirst = false;
                if (Wet > 0.0) bRained = true;
                if (Wet > 0.0 && (Hour < 12.0 || Hour > 20.5)) bInRange = false;
            }
            if (Day > 1) { bSomeRain = bSomeRain || bRained; bSomeDry = bSomeDry || !bRained; }
        }
        Check(bDryFirst && bInRange && bSomeRain && bSomeDry && Rain(5, std::numeric_limits<double>::quiet_NaN()) == 0.0,
            "a dry first day, then showers on some afternoons");
        Check(GripScale(0.0) == 1.0 && std::abs(GripScale(1.0) - 0.85) < 1e-9 && SunScale(1.0) < 0.5 && FogScale(1.0) > FogScale(0.0), "wet roads grip less, the sky darkens");
        Check(RainDrops(0.0) == 0 && RainDrops(1.0) == 700 && RainDrops(0.5) == 350 && SkyScale(1.0) < 0.5 && HazeScale(1.0) > HazeScale(0.0),
            "a downpour is overcast and falls in streaks");
    }

    // The calendar.
    {
        using namespace Rally::Calendar;
        const FDate First = DateOf(1), Leap = DateOf(1 + 366 - 97 + 1 + 59), NewYear = DateOf(1 + 366 - 97 + 1);
        Check(First.Year == 1996 && First.Month == 4 && First.Date == 6 && std::string(WeekdayName(First.Weekday)) == "Sat"
            && std::string(WeekdayName(DateOf(3).Weekday)) == "Mon", "day 1 is Saturday 6 April 1996");
        Check(NewYear.Year == 1997 && NewYear.Month == 1 && NewYear.Date == 1 && std::string(WeekdayName(NewYear.Weekday)) == "Wed"
            && DateOf(1 + 366 - 97 + 1 + 58).Month == 2 && Leap.Month == 3 && Leap.Date == 1, "1 January 1997 is a Wednesday; 1997 has no 29 February");
        Check(IsRallyDay(1) && IsRallyDay(2) && !IsRallyDay(3) && !IsRallyDay(8) && IsRallyDay(15) && IsRallyDay(16) && !IsRallyDay(0)
            && NextRallyDay(1) == 1 && NextRallyDay(3) == 15 && NextRallyDay(14) == 15 && NextRallyDay(17) == 29, "a rally weekend every second weekend");
        Check(WeekendsOver(1) == 0 && WeekendsOver(2) == 0 && WeekendsOver(3) == 1 && WeekendsOver(16) == 1 && WeekendsOver(17) == 2,
            "a weekend is over once its Sunday has passed");
        Check(std::string(WeekdayName(DateOf(15).Weekday)) == "Sat" && DateOf(1 + (CupEvents - 1) * WeekendEvery).Month == 9, "the cup runs April to September");
        const FDate Next = DateOf(1 + SeasonDays);
        Check(IsRallyDay(1 + 11 * 14) && !IsRallyDay(1 + 12 * 14) && IsOffSeason(1 + 12 * 14) && !IsOffSeason(1 + 11 * 14) && NextRallyDay(1 + 12 * 14) == 365
            && NextRallyDay(300) == 365 && IsRallyDay(365) && Next.Year == 1997 && Next.Month == 4 && Next.Date == 5 && std::string(WeekdayName(Next.Weekday)) == "Sat"
            && WeekendsOver(200) == 12 && WeekendsOver(365) == 12 && WeekendsOver(367) == 13 && SeasonOf(365) == 1,
            "after September's last event the off-season, then next April's cup");
        Check(WakeDay(1, 22.0, 6.5, false) == 2 && WakeDay(2, 3.0, 6.5, false) == 2 && WakeDay(2, 22.0, 6.5, true) == 15 && WakeDay(1, 22.0, 6.5, true) == 2
            && WakeDay(15, 5.0, 6.5, true) == 15 && WakeDay(9, 12.0, 6.5, true) == 15, "sleep to the next morning, or to the rally weekend's");
    }

    // Money.
    {
        using namespace Rally::Economy;
        Check(PrizeSk(1, false) == 6000 && PrizeSk(7, false) == 500 && PrizeSk(1, true) == 0 && PrizeSk(0, false) == 0, "prize money by place");
        Check(Money(15000) == "15 000 Sk" && Money(500) == "500 Sk" && Money(-2500) == "-2 500 Sk", "money reads in thousands");
        Check(MissedPaymentSk(LoanPaymentSk) == 2750 && StartingLoanSk / LoanPaymentSk == 24, "the loan: 24 payments, a missed one costs a tenth more");
        Check(PersonalItem("slivovica") && !PersonalItem("strut"), "vices are bought from one's own pocket");
    }

    {
        using namespace Rally;
        Check(Engine::WornSmoke(1.0) == 0.0 && Engine::WornSmoke(0.9) == 0.0 && Engine::WornSmoke(0.6) > 0.3 && Engine::WornSmoke(0.0) == 1.0,
            "blue smoke from 90% engine health, thicker as it wears");
        Check(Engine::Detonation(0.0) == 0.0 && Engine::Detonation(0.001) > 0.4 && Engine::Detonation(0.005) == 1.0 && Engine::Detonation(0.02) == 1.0,
            "pinking heard a tenth of a bar over, full by half a bar");
        Check(Fixing::LooseTiltDeg(4, 4) == 0.0 && Fixing::LooseTiltDeg(3, 4) >= 3.0 && Fixing::LooseTiltDeg(1, 4) > Fixing::LooseTiltDeg(3, 4)
            && Fixing::LooseTiltDeg(0, 4) == 14.0 && Fixing::LooseDropCm(0, 4) == 4.0 && Fixing::LooseDropCm(4, 4) == 0.0,
            "a part sits crooked as its fixings go and hangs with none home");
        Check(Fixing::RattleLevel(0.5, false, 0.0) == 0.0 && Fixing::RattleLevel(0.5, true, 0.0) == 0.25 && Fixing::RattleLevel(1.0, false, 80.0) == 1.0
            && Fixing::RattleLevel(0.0, true, 80.0) == 0.0, "loose parts rattle with the engine running or on the move");
    }
    {
        using namespace Rally::Fixing;
        Check(TurnSecondsFor(2) == TurnSeconds && TurnSecondsFor(4) == TurnSeconds && std::abs(TurnSecondsFor(8) - 0.25) < 1e-12
            && TurnSecondsFor(13) == 0.25 && MinTurnIntervalFor(13) == 0.2 && MinTurnIntervalFor(4) == MinTurnInterval,
            "a part with many fixings is worked quicker each, down to half the time");
        Check(IsSocket(ETool::Spanner13) && !IsSocket(ETool::Screwdriver) && !IsSocket(ETool::Hands) && ExtensionCm > 20.0,
            "sockets reach on an extension, a screwdriver or hands do not");
        Rally::Fixing::EKind Kind = Rally::Fixing::EKind::Clip;
        Rally::Fixing::ETool Tool = Rally::Fixing::ETool::Hands;
        Check(ParseTag("Nut", "Spanner13", Kind, Tool) && Kind == Rally::Fixing::EKind::Nut && Tool == Rally::Fixing::ETool::Spanner13
            && ParseTag("Screw", "Screwdriver", Kind, Tool) && Kind == Rally::Fixing::EKind::Screw && Tool == Rally::Fixing::ETool::Screwdriver
            && !ParseTag("Banjo", "Spanner13", Kind, Tool) && !ParseTag("Bolt", "Spanner16", Kind, Tool), "a socket's tag names its fixing and tool");
    }
    {
        using namespace Rally;
        Check(Leak::DropsPerSecond(0.0) == 0.0 && Leak::DropsPerSecond(0.001) == 0.5 && Leak::DropsPerSecond(0.05) == 10.0 && Leak::DropsPerSecond(1.0) == 12.0,
            "a seep drips every two seconds, a cracked sump ten times a second");
        Check(Leak::DropsPerSecondMoving(0.05, 0.0) == 10.0 && std::abs(Leak::DropsPerSecondMoving(0.05, 10.0) - 4.0) < 1e-9
            && Leak::DropsPerSecondMoving(0.05, 100.0) == 10.0, "on the move a spot about every 2.5 m");
        Check(Leak::PuddleRadiusCm(0.0) == Leak::MinRadiusCm && std::abs(Leak::PuddleRadiusCm(1.0) - 46.07) < 0.1
            && Leak::PuddleRadiusCm(4.5) < Leak::PuddleRadiusCm(10.0) + 1e-9 && Leak::PuddleRadiusCm(1000.0) == Leak::MaxRadiusCm,
            "a litre spreads to about 46 cm round, a drained sump to most of a metre");
        Check(Leak::DryRate(Leak::ELiquid::Fuel) > Leak::DryRate(Leak::ELiquid::Coolant) && Leak::DryRate(Leak::ELiquid::Coolant) > Leak::DryRate(Leak::ELiquid::Oil),
            "fuel dries first, then coolant; oil stays");
        Check(Leak::OilSoak(0.0) == 0.0 && Leak::OilSoak(0.75) == 0.5 && Leak::OilSoak(5.0) == 1.0, "the block soaks as a litre and a half runs past");
    }
    {
        // The cabin: gauges, warning lamps, indicators, wipers and the gear lever (Vehicle/CabinRules.h).
        using namespace Rally::Cabin;
        auto NearD = [](double A, double B) { return std::abs(A - B) < 1e-6; };
        Check(NearD(SpeedoDeg(110.0, true), 120.0) && NearD(SpeedoDeg(-30.0, true), SpeedoDeg(30.0, true))
            && SpeedoDeg(400.0, true) == 240.0 && SpeedoDeg(100.0, false) == 0.0, "the speedometer reads 110 straight up, backwards too, and lies at 0 with the key off");
        Check(NearD(TachoDeg(3500.0, true), 120.0) && TachoDeg(9000.0, true) == 240.0 && TachoDeg(std::numeric_limits<double>::quiet_NaN(), true) == 0.0,
            "the tachometer reads 3500 straight up and stops at its peg");
        Check(NearD(TempDeg(90.0, true), 45.0) && TempDeg(20.0, true) == 0.0 && TempDeg(140.0, true) == 90.0, "90 C is the temperature gauge's middle");
        Check(NearD(FuelDeg(27.5, 55.0, true), 45.0) && FuelDeg(27.5, 55.0, false) == 0.0 && FuelDeg(10.0, 0.0, true) == 0.0, "half a tank is half a gauge");
        double Needle = 0.0;
        for (int I = 0; I < 60; ++I) Needle = FollowNeedle(Needle, 100.0, 8.0, 1.0 / 60.0);
        Check(Needle > 99.0 && Needle < 100.0, "a needle is nearly there after a second");
        Check(FlashOn(0.1) && !FlashOn(0.5) && FlashOn(0.7), "indicators flash at 1.5 Hz");
        Check(LeftLit(EIndicator::Hazard, 0.1) && RightLit(EIndicator::Hazard, 0.1) && !RightLit(EIndicator::Left, 0.1), "hazards flash both sides");
        double Furthest = 0.0;
        EIndicator Mode = EIndicator::Right;
        Mode = SelfCancel(Mode, 40.0, Furthest);
        Mode = SelfCancel(Mode, 5.0, Furthest);
        Check(Mode == EIndicator::Right, "a small wiggle does not cancel the indicator");
        Mode = SelfCancel(Mode, 200.0, Furthest);
        Mode = SelfCancel(Mode, 60.0, Furthest);
        Check(Mode == EIndicator::Right, "it stays on through the turn");
        Mode = SelfCancel(Mode, 10.0, Furthest);
        Check(Mode == EIndicator::Off, "it cancels as the wheel comes back from a right turn");
        Mode = SelfCancel(EIndicator::Left, 300.0, Furthest);
        Mode = SelfCancel(Mode, 0.0, Furthest);
        Check(Mode == EIndicator::Left && SelfCancel(EIndicator::Hazard, -500.0, Furthest) == EIndicator::Hazard, "turning the other way or hazards never cancel");
        double Phase = 0.0, Pause = 0.0, Sweep = 0.0, MaxSweep = 0.0;
        for (int I = 0; I < 90; ++I) { Sweep = AdvanceWiper(EWiper::Slow, Phase, Pause, 1.0 / 60.0); MaxSweep = std::max(MaxSweep, Sweep); }
        Check(MaxSweep > 0.99 && Sweep < 0.5, "a slow wipe goes out and comes back in a second and a half");
        double Parked = 1.0;
        for (int I = 0; I < 200; ++I) Parked = AdvanceWiper(EWiper::Off, Phase, Pause, 1.0 / 60.0);
        Check(Parked == 0.0 && Phase == 0.0, "switched off, the wipers finish the wipe and park");
        Phase = 0.0; Pause = 0.0;
        int Wipes = 0; double Last = 0.0;
        for (int I = 0; I < 60 * 12; ++I) { const double S = AdvanceWiper(EWiper::Interval, Phase, Pause, 1.0 / 60.0); if (S > 0.9 && Last <= 0.9) ++Wipes; Last = S; }
        Check(Wipes == 3, "intermittent wipes about every 5.5 s");
        Check(NextWiper(EWiper::Fast) == EWiper::Off && WiperStalkDeg(EWiper::Fast) == 15.0, "the stalk steps through four positions");
        FWarningInputs In;
        Check(WarningLamps(In) == 0u, "nothing lit with the key out");
        In.bIgnition = true;
        Check(WarningLamps(In) == 0xFFu, "every lamp lights for the bulb check");
        In.IgnitionSeconds = 3.0;
        Check(WarningLamps(In) == ((1u << Oil) | (1u << Battery) | (1u << Belt)), "oil, battery and belt with the engine not running");
        In.bRunning = true; In.IgnitionSeconds = 10.0; In.bHandbrake = true;
        Check(WarningLamps(In) == (1u << Brake), "running, only the handbrake lamp");
        In.bCharging = false; In.OilL = 1.0;
        Check((WarningLamps(In) & (1u << Battery)) && (WarningLamps(In) & (1u << Oil)), "no alternator and low oil light their lamps");
        In.bIgnition = false; In.bLeftFlash = true; In.bHighBeam = true;
        Check(WarningLamps(In) == ((1u << TurnLeft) | (1u << HighBeam)), "indicator and high-beam lamps need no ignition");
        Check(GearLever(1).ForeDeg > 0.0 && GearLever(1).AcrossDeg < 0.0 && GearLever(4).AcrossDeg == 0.0 && GearLever(-1).ForeDeg < 0.0
            && GearLever(0).ForeDeg == 0.0, "the lever follows the H-gate");
        Check(SupplyScale(1.0) == 1.0 && SupplyScale(0.0) < 0.5, "a weak battery dims the lamps");
        const FVec Eye{ -2.0, -37.0, 114.0 };
        const FVec Points[] = { { 61.3, -67.0, 77.0 }, { 64.1, 0.0, 87.5 }, { 57.0, 40.0, 60.0 } };
        const auto Toward = [&Eye](const FVec& P) { return FVec{ P.X - Eye.X, P.Y - Eye.Y, P.Z - Eye.Z }; };
        Check(AimedControl(Eye, Toward(Points[1]), Points, 3) == 1, "looking at the hazard button picks it");
        const FVec NearlyHazard{ Toward(Points[1]).X, Toward(Points[1]).Y + 4.0, Toward(Points[1]).Z };
        Check(AimedControl(Eye, NearlyHazard, Points, 3) == 1, "a few degrees off still picks it");
        Check(AimedControl(Eye, FVec{ 1.0, 0.0, 0.0 }, Points, 3) == -1, "looking down the road picks nothing");
        Check(AimedControl(Eye, Toward(Points[2]), Points, 3) == 2 && AimedControl(Eye, FVec{}, Points, 3) == -1,
            "the glove box is in reach; no view direction, no control");
        const FVec Far[] = { { 400.0, 0.0, 114.0 } };
        Check(AimedControl(Eye, Toward(Far[0]), Far, 1) == -1, "out of reach is nothing");
        Check(NextIndicator(EIndicator::Off) == EIndicator::Left && NextIndicator(EIndicator::Left) == EIndicator::Right
            && NextIndicator(EIndicator::Right) == EIndicator::Off && NextIndicator(EIndicator::Hazard) == EIndicator::Off,
            "clicking the stalk goes left, right, off");
        double Wet = 0.0;
        for (int I = 0; I < 8 * 60; ++I) Wet = WetAfter(Wet, 1.0, 1.0 / 60.0);
        Check(Wet > 0.9, "a downpour wets the windscreen in about 8 s");
        double Light = 0.0;
        for (int I = 0; I < 60 * 60; ++I) Light = WetAfter(Light, 0.3, 1.0 / 60.0);
        Check(Light < 0.6 && Light > 0.5, "a light shower wets it only part way");
        for (int I = 0; I < 60 * 60; ++I) Wet = WetAfter(Wet, 0.0, 1.0 / 60.0);
        Check(Wet < 0.6 && Wet > 0.4, "it dries over a couple of minutes");
        Check(BladePassed(0.4, 0.6) && BladePassed(0.6, 0.4) && !BladePassed(0.1, 0.3), "a blade passes the middle both ways");
        double Mud = 0.0;
        for (int I = 0; I < 300; ++I) Mud = MudAfter(Mud, 80.0, 1.0, 0.0, 1.0);
        Check(Mud > 0.9, "five minutes at 80 on dry gravel cakes the car");
        double WetMud = 0.0;
        for (int I = 0; I < 120; ++I) WetMud = MudAfter(WetMud, 80.0, 1.0, 1.0, 1.0);
        Check(WetMud > 0.9, "in the rain it takes under two");
        Check(MudAfter(0.5, 80.0, 0.0, 0.0, 10.0) == 0.5, "tarmac throws no mud");
        double Washed = 1.0;
        for (int I = 0; I < 340; ++I) Washed = MudAfter(Washed, 0.0, 1.0, 1.0, 1.0);
        Check(Washed == 0.0, "a downpour washes a parked car clean in about five minutes");
    }
    std::cout << (Checks - Failures) << "/" << Checks << " checks passed\n";
    return Failures == 0 ? 0 : 1;
}
