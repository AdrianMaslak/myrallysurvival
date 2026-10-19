#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Damage/FixingRules.h"
#include "Damage/RallyDebrisActor.h"
#include "Damage/PanelRules.h"
#include "Damage/PartGradeRules.h"
#include "RallyPanelComponent.generated.h"

class ARallyCharacter;
class ARallyDebrisActor;
class UStaticMeshComponent;
class URallyBodyDeformComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
enum class ERallyPart : uint8;

USTRUCT()
struct FRallyPanelCondition
{
    GENERATED_BODY()
    UPROPERTY() uint8 HingeA = 255;
    UPROPERTY() uint8 HingeB = 255;
    UPROPERTY() uint8 Latch = 255;
    UPROPERTY() uint8 Strap = 0;
    UPROPERTY() bool bJammed = false;
    // Running gear slots: the part has been taken off and nothing is fitted there.
    UPROPERTY() bool bRemoved = false;
    // Opened by hand: unlatched and held open (door check strap, bonnet prop, boot struts) until a
    // player shuts it.
    UPROPERTY() bool bOpen = false;
    // How it went back on after coming off: shift (cm) and skew about its hinge line. Zero from the factory.
    UPROPERTY() FVector_NetQuantize10 FitOffset = FVector::ZeroVector;
    UPROPERTY() FRotator FitRotation = FRotator::ZeroRotator;
    UPROPERTY() TObjectPtr<ARallyDebrisActor> Debris;
    // Fixings turned out, a bit each (Rally::Fixing). On a body panel bits 0-1 are its first hinge's (or
    // end's) and 2-3 its second's, and what they still hold caps that hinge's health.
    // Sixteen bits since the 1.8T engine's modules (thirteen nuts on the exhaust manifold); older saves
    // stored eight and load into the low bits.
    UPROPERTY() uint16 Out = 0;
    // The fitted part's grade (Rally::Grade::EGrade) and its hand settings: damper clicks or boost in
    // tenths of a bar (0: as delivered), and a rally strut's height collar (cm, positive raises the car).
    UPROPERTY() uint8 Grade = 1;
    UPROPERTY() uint8 SetA = 0;
    UPROPERTY() int8 SetB = 0;
    Rally::Panel::FFixings Fixings() const
    {
        const auto Hold = [this](int32 Shift) { return Rally::Fixing::GroupHold(((Out >> Shift) & 1) + ((Out >> (Shift + 1)) & 1), 2); };
        return { FMath::Min<int>(HingeA, Hold(0)), FMath::Min<int>(HingeB, Hold(2)), Latch, Strap };
    }
    // Damage and wear, measured against Fixings(), come off the hinges themselves (what loose fixings take
    // comes back when they are done up); taken to nothing, the hinge is gone.
    void Set(const Rally::Panel::FFixings& F)
    {
        const Rally::Panel::FFixings Now = Fixings();
        HingeA = static_cast<uint8>(F.A <= 0 ? 0 : FMath::Clamp<int>(HingeA + F.A - Now.A, 0, 255));
        HingeB = static_cast<uint8>(F.B <= 0 ? 0 : FMath::Clamp<int>(HingeB + F.B - Now.B, 0, 255));
        Latch = static_cast<uint8>(F.Latch); Strap = static_cast<uint8>(F.Strap);
    }
    // Off the car: nothing holds it.
    void Clear() { HingeA = HingeB = Latch = Strap = 0; Out = 0; }
};

// Authoritative fixings; local cosmetic hinge motion. Indices match the car's panel array
// (Rally::Crush::KindOfPanel): 0-3 doors, 4 bonnet, 5 boot lid, 6 radiator (which stays in the mechanical
// damage system), then parts held by two clips or bolts and no latch: 7-8 bumpers, 9-10 front wings,
// 11-12 headlamps, 13-14 tail lamps. Indices 15-18 are the road wheels (front left, front right, rear
// left, rear right): they have no fixings here (whether a wheel is on is its hub part in
// URallyDamageComponent), only the loose wheel once it has come off. Then the running gear of each
// corner (struts, arms, brake discs; tie rods and driveshafts at the front), whose condition is its
// ERallyPart in URallyDamageComponent, the jack, the water can, the battery, the oil bottle, the oil filler,
// the sump, the fuel tank and the exhaust. The car's boot carries a spares kit. The radiator (6), battery
// and oil filler are places in the engine bay, worked on with the bonnet open or gone (water, sealant,
// battery terminals, oil); the sump, gearbox, tank and exhaust are under the car and need that end of it
// on the jack (putty, the gear linkage, a strap round the exhaust), except pulling a hanging exhaust off.
// Every machine: a fixing of this slot worked loose on the move; bNoneLeft when it was the last one home
// (the part now hangs loose, or has dropped off).
DECLARE_MULTICAST_DELEGATE_TwoParams(FRallyFixingLoose, int32 /*Slot*/, bool /*bNoneLeft*/);

UCLASS()
class RALLYSURVIVAL_API URallyPanelComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    static constexpr int32 NumPanels = 15;
    static constexpr int32 FirstClipped = 7;
    static constexpr int32 FirstWheel = NumPanels;
    static bool IsWheel(int32 Part) { return Part >= FirstWheel && Part < FirstWheel + 4; }
    // Running gear: struts 19-22, arms 23-26 (front lower arm, rear trailing arm), brake discs 27-30
    // (corners FL, FR, RL, RR), tie rods 31-32 and driveshafts 33-34 (front left, front right).
    enum class EGear : uint8 { Strut, Arm, Disc, TieRod, Driveshaft };
    // The tie rods' slots are kept so saves line up, but are not parts any more: a front arm carries its
    // track rod (no fixings, nothing to aim at, nothing to break).
    static bool IsRetired(int32 Part) { return IsGear(Part) && GearKind(Part) == EGear::TieRod; }
    // What a part's condition looks like, for its prompt ("bent 40%", "spring broken"); empty when sound.
    FString PartState(int32 Part) const;
    static constexpr int32 FirstGear = FirstWheel + 4;
    static constexpr int32 NumGear = 16;
    static constexpr int32 JackSlot = FirstGear + NumGear;
    // A water can from the boot kit; its part health is how full it is.
    static constexpr int32 WaterSlot = JackSlot + 1;
    static constexpr double WaterCanLitres = 5.0;
    static constexpr int32 RadiatorSlot = 6;
    static constexpr int32 BatterySlot = WaterSlot + 1;
    // A bottle of engine oil from the boot kit, its part health how full it is.
    static constexpr int32 OilBottleSlot = BatterySlot + 1;
    static constexpr double OilBottleLitres = 4.0;
    static constexpr int32 OilFillerSlot = OilBottleSlot + 1;
    // Under the car: the sump (from in front), the fuel tank (from the left) and the exhaust (from behind;
    // it can be pulled off, and the slot then holds it as a loose part).
    static constexpr int32 SumpSlot = OilFillerSlot + 1;
    static constexpr int32 TankSlot = SumpSlot + 1;
    static constexpr int32 ExhaustSlot = TankSlot + 1;
    static constexpr int32 GearboxSlot = ExhaustSlot + 1;
    // Engine modules that bolt on and off in the bay (docs/DAMAGE.md, Engine modules). Each has its own
    // ERallyPart (PartOfModule) except the engine cover, which only keeps the dirt off.
    enum class EModule : uint8 { Cover, AirFilter, Intake, ExhaustManifold, Turbo, Intercooler, Alternator, Starter, Ecu };
    static constexpr int32 FirstModule = GearboxSlot + 1;
    static constexpr int32 NumModules = 9;
    // A first-aid kit from the boot kit (docs/BODY.md); its part health is how much is left in it.
    static constexpr int32 FirstAidSlot = FirstModule + NumModules;
    // The village's vices (docs/BODY.md): a bottle of slivovica, a carton of cigarettes. Health is what is left.
    static constexpr int32 DrinkSlot = FirstAidSlot + 1;
    static constexpr int32 SmokeSlot = DrinkSlot + 1;
    static constexpr int32 NumSlots = SmokeSlot + 1;
    static bool IsModule(int32 Part) { return Part >= FirstModule && Part < FirstModule + NumModules; }
    static EModule ModuleKind(int32 Part) { return static_cast<EModule>(Part - FirstModule); }
    static int32 ModuleSlot(EModule Kind) { return FirstModule + static_cast<int32>(Kind); }
    static ERallyPart PartOfModule(int32 Slot);
    // The slot a kind of spare is for, by the catalogue's names (strut, rearstrut, disc, reardisc, turbo, ecu,
    // intercooler); INDEX_NONE if unknown.
    static int32 SlotForKind(const FString& Kind);
    static bool IsBayPoint(int32 Part) { return Part == RadiatorSlot || Part == BatterySlot || Part == OilFillerSlot; }
    static bool IsUnderCar(int32 Part) { return Part == SumpSlot || Part == TankSlot || Part == ExhaustSlot || Part == GearboxSlot; }
    static bool IsServicePoint(int32 Part) { return IsBayPoint(Part) || IsUnderCar(Part); }
    // Working under the car: the jack holds up the front (sump, gearbox, the starter's bolts from below) or
    // the rear (tank, exhaust).
    bool IsJackedFor(int32 Part) const
    {
        return JackCorner >= 0 && ((Part == SumpSlot || Part == GearboxSlot || Part == ModuleSlot(EModule::Starter)) == (JackCorner < 2));
    }
    // Kit items poured into the car rather than fitted.
    static bool IsFluid(int32 Part) { return Part == WaterSlot || Part == OilBottleSlot; }
    // The engine bay can be reached: the bonnet is open or gone.
    bool IsBayOpen() const { return IsOpen(4) || IsDetached(4); }
    static bool IsGear(int32 Part) { return Part >= FirstGear && Part < FirstGear + NumGear; }
    static EGear GearKind(int32 Part);
    static int32 GearCorner(int32 Part);
    static int32 GearSlot(EGear Kind, int32 Corner);

    URallyPanelComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    // Gear: the running-gear meshes in slot order (FirstGear onwards).
    void Initialize(const TArray<TObjectPtr<UStaticMeshComponent>>& InMeshes,
        const TArray<TObjectPtr<UStaticMeshComponent>>& InWheels, const TArray<TObjectPtr<UStaticMeshComponent>>& InGear,
        UStaticMeshComponent* InExhaust, const TArray<TObjectPtr<UStaticMeshComponent>>& InModules, URallyBodyDeformComponent* InDeform);
    void TickPanels(float Dt, uint8 DoorsOpen);
    void ApplyImpact(const FBox& Patch, const FVector& Direction, float DeltaV);
    void RepairAll();
    // Test harness, server only: every fixing gives way and the panel comes off now, keeping its dents.
    void DebugDetach(int32 Panel);
    // Test harness, server only: the latch gives way, both hinges hold.
    void DebugBreakLatch(int32 Panel);
    // Server only: the wheel (0-3) comes off as a part: thrown out when the hub tears, or simply off its
    // hub when a player takes it off.
    void DetachWheel(int32 Wheel, bool bThrown = true);
    // Server only: whoever stands at the jacked corner lowers the jack; true if they did.
    bool LowerJack(ARallyCharacter* Character);
    // Test harness, server only: the jack stands under this corner (0-3), or comes down (-1).
    void DebugPlaceJack(int32 Corner);
    int32 GetJackCorner() const { return JackCorner; }
    // Where a carried part would be fitted: the free slot in reach it fits (a jack: the corner's wheel
    // slot), or INDEX_NONE.
    int32 FitTarget(const ARallyCharacter* Character, const ARallyDebrisActor* Part) const;
    bool IsRemoved(int32 Part) const { return Conditions.IsValidIndex(Part) && Conditions[Part].bRemoved; }
    // What a carried part is called: a slot's name, or "spare front strut" and the like.
    static FString CarriedName(int32 Part, bool bSpare);
    bool IsDetached(int32 Panel) const;
    bool IsOpen(int32 Panel) const { return Conditions.IsValidIndex(Panel) && Conditions[Panel].bOpen; }
    Rally::Panel::EStage GetStage(int32 Panel) const
    {
        return Conditions.IsValidIndex(Panel) ? Rally::Panel::Stage(Conditions[Panel].Fixings(), Panel >= FirstClipped && Panel < NumPanels)
            : Rally::Panel::EStage::Intact;
    }
    bool IsDoorJammed(int32 Panel) const;
    uint32 AttachedDentMask() const;
    FVector GetRefitLocation(int32 Panel) const;
    // Within arm's reach of the player's eyes, nothing in between, and the part there to work on (a bay
    // with the bonnet open, a car standing still).
    bool CanReach(const ARallyCharacter* Character, int32 Panel) const;
    static constexpr double ReachCm = 230.0;
    // How far a leaning head can be from where the server puts the eyes; reach allows for it.
    static constexpr double LeanCm = 45.0;
    // The part of this car exactly under a ray (the crosshair): a panel, wheel or running-gear part by its
    // collision hull, or a place without a mesh of its own (radiator filler, battery, dipstick, and under
    // the car the sump, gearbox and tank) by a small sphere. Under-car places only count crouched, and
    // then before anything in front of them. INDEX_NONE if the ray finds nothing within Length.
    // OutFixing, if given, is the fixing of that part the ray meets on the way (Rally::Fixing), or INDEX_NONE.
    // Holding the socket a fixing takes (HeldTool), its extension reaches it a little behind a part in the way.
    int32 FindTargetAlongRay(const FVector& Start, const FVector& Direction, double Length, bool bCrouched, double& OutDistance,
        int32* OutFixing = nullptr, Rally::Fixing::ETool HeldTool = Rally::Fixing::ETool::Count) const;
    // Fixings (Rally::Fixing): each panel, wheel and running-gear part on the car has a few, each turned
    // with one tool. A part goes on with them out and is held once they are home.
    int32 NumFixings(int32 Part) const { return SpotStart.IsValidIndex(Part + 1) ? SpotStart[Part + 1] - SpotStart[Part] : 0; }
    bool IsFixingOut(int32 Part, int32 Fixing) const
    {
        return Conditions.IsValidIndex(Part) && Fixing >= 0 && Fixing < NumFixings(Part) && ((Conditions[Part].Out >> Fixing) & 1);
    }
    int32 CountOut(int32 Part) const;
    bool AllHome(int32 Part) const { return CountOut(Part) == 0; }
    Rally::Fixing::ETool FixingTool(int32 Part, int32 Fixing) const;
    FString FixingName(int32 Part, int32 Fixing) const;
    FVector GetFixingLocation(int32 Part, int32 Fixing) const;
    // Fixings home and in all, for the car's handling (a part on loose bolts acts damaged).
    void GetFixingHold(int32 Part, int32& OutHome, int32& OutTotal) const;
    // Server only: the player turns one fixing home or out with the tool in hand. Checks reach, the tool,
    // what is in the way (a shut door, a wheel still on) and the pace (Rally::Fixing::MinTurnInterval).
    bool TurnFixing(ARallyCharacter* Character, int32 Part, int32 Fixing, bool bHome);
    // What a player aiming at a fixing sees: its name, and what E or R would do with the tool in hand.
    FString FixingPrompt(const ARallyCharacter* Character, int32 Part, int32 Fixing) const;

    // Grades and settings (Rally::Grade).
    Rally::Grade::EGrade GetGrade(int32 Part) const
    {
        return Conditions.IsValidIndex(Part) ? static_cast<Rally::Grade::EGrade>(FMath::Min<uint8>(Conditions[Part].Grade, 3)) : Rally::Grade::EGrade::Stock;
    }
    int32 GetClicks(int32 Part) const
    {
        return Conditions.IsValidIndex(Part) && Conditions[Part].SetA ? Conditions[Part].SetA : Rally::Grade::DefaultClicks;
    }
    int32 GetBoostTenths(int32 Part) const
    {
        return Conditions.IsValidIndex(Part) && Conditions[Part].SetA ? Conditions[Part].SetA : Rally::Grade::DefaultBoostTenths;
    }
    int32 GetCollarCm(int32 Part) const { return Conditions.IsValidIndex(Part) ? Conditions[Part].SetB : 0; }
    // The engine computer's map (Rally::Grade::EngineMap).
    int32 GetEngineMap(int32 Part) const
    {
        return Conditions.IsValidIndex(Part) && Conditions[Part].SetA ? Conditions[Part].SetA : Rally::Grade::DefaultMap;
    }
    // A slot's name with its fitted part's grade ("rally front left strut").
    FString SlotLabel(int32 Part) const;
    static FString GradedName(int32 Part, bool bSpare, uint8 Grade);
    // Server only: one step up or down of a setting, chosen by the tool in hand: damper clicks
    // (screwdriver) or the height collar (19 mm) on a rally strut, the boost at the turbo's wastegate
    // (10 mm). Same reach, access and pace as turning a fixing.
    bool Adjust(ARallyCharacter* Character, int32 Part, int32 Step);
    // What the tool in hand would set on this part and where it is now, or empty.
    FString AdjustPrompt(const ARallyCharacter* Character, int32 Part) const;
    // Test harness and the RallyPart console command, server only: a part of a slot's kind and grade,
    // loose at Location.
    ARallyDebrisActor* DebugSpawnPart(int32 Slot, uint8 Grade, const FVector& Location);
    // The mesh a part of this slot is drawn with, whatever it is.
    UStaticMesh* MeshForSlot(int32 Slot) const;
    // The boot (docs/BOOT.md): a carried part put in with E while the boot lid is open (or gone), up to
    // BootCapacityKg; what is in it now; what the kit weighed when packed, which the car's own mass includes.
    static constexpr double BootCapacityKg = 150.0;
    bool CanStow(const ARallyCharacter* Character, const ARallyDebrisActor* Part) const;
    // Server only. Character may be null (test harness).
    bool Stow(ARallyCharacter* Character, ARallyDebrisActor* Part);
    // Where a part goes in the boot (relative to the car, for ERallyPartMount::Stowed): laid flat with its
    // longest side across the car, at the first place on the load floor, from the back, that nothing
    // stowed already covers; once the floor is full, on top. False if it will not go in.
    bool BootPlace(const UStaticMesh* Mesh, const ARallyDebrisActor* Ignore, FVector& OutLocation, FRotator& OutTurn) const;
    // Each kit item's own place in the boot (where the kit is packed when the car is new; the spare wheel's
    // is its well): the free one this part belongs in (a kit item's kind, a front strut's for either front
    // strut), or INDEX_NONE. Stowing puts a part there first, strapped down.
    int32 FreeHomeFor(const ARallyDebrisActor* Part) const;
    // The carrier is looking into the boot (G puts a part in rather than dropping it through the car).
    bool FacesBoot(const ARallyCharacter* Character) const;
    // A mount location under the load floor: the spare wheel's well.
    static bool InBootWell(const FVector& MountLocation);
    // Every machine, from the car's tick: with the boot open, an outline of each kit item whose place is
    // empty, brighter for the one in the local player's hands.
    void UpdateBootGhosts();
    // The local player turning a fixing (cosmetic, this machine only): how far the hold has got (0..1) and
    // which way; the fixing spins and rises (or sinks) with it. INDEX_NONE stops showing it.
    void ShowFixingTurn(int32 Slot, int32 Fixing, float Progress, bool bHome);
    // Every machine, each frame before TickPanels: how the engine has rolled on its mounts (a transform in the
    // car's frame); the modules bolted to the engine (all but the air box, intercooler and computer) follow it.
    void SetEngineMotion(const FTransform& Motion) { EngineMotion = Motion; bEngineMoving = true; }
    static bool IsEngineMounted(EModule Kind) { return Kind != EModule::AirFilter && Kind != EModule::Intercooler && Kind != EModule::Ecu; }
    // Test harness: where a kit slot's place is (relative to the car), and whether its outline shows.
    bool GetBootHome(int32 Slot, FVector& OutLocation) const;
    bool IsBootGhostShown(int32 Slot) const;
    double GetBootLoadKg() const;
    double GetBootKitKg() const { return BootKitKg; }
    // Workshop jobs in the service yard (docs/MONEY.md): the job the part in view needs (Rally::Economy::EJob
    // as a number), or -1; a prompt for it; and, server only, paying for it and waiting while it is done.
    int32 WorkshopJob(int32 Part) const;
    FString WorkshopPrompt(int32 Part) const;
    bool WorkshopRepair(ARallyCharacter* Character, int32 Part);
    // Test harness: which of a part's fixings the crosshair can land on, with the socket each takes, from
    // somewhere a player can put their eyes round the open engine bay (in front, beside each wing, standing
    // and leaning in). Returns how many; OutUnreached lists the others.
    int32 DebugReachableFixings(int32 Slot, TArray<int32>& OutUnreached) const;
    // Test harness, server only: a module taken off, or back on, without carrying it anywhere.
    void DebugSetRemoved(int32 Slot, bool bRemoved);
    // Test harness, server only: a fixing left out (or turned home again).
    void DebugSetFixingOut(int32 Part, int32 Fixing, bool bOut = true);
    // Test harness: where the fixings sit worked out again (after a mesh's sockets changed).
    void DebugRebuildFixings() { BuildFixings(); }
    // Test harness: each fixing as drawn (shown, its material, how far the head is from where it should be).
    FString DebugFixingLooks(int32 Part) const;
    FRallyFixingLoose OnFixingLoose;
    // The local player's aim (cosmetic, each machine its own): the fixings of the part in view glow amber
    // and are drawn bigger so they can be found, the one that would turn bright yellow and biggest.
    // INDEX_NONE clears.
    void SetAimHighlight(int32 Slot, int32 Fixing);
    // The loosest bolted-on part's share of fixings out (wheels, running gear, engine modules), 0..1: what
    // rattles (Rally::Fixing::RattleLevel).
    double GetLoosestShare() const;
    // How far an engine module sits out of true now (degrees, Rally::Fixing::LooseTiltDeg).
    double GetModuleTiltDeg(int32 Slot) const { return ModuleTilt.IsValidIndex(Slot - FirstModule) ? ModuleTilt[Slot - FirstModule] : 0.0; }
    // Test harness, server only: the fitted part's grade and settings.
    void DebugSetGrade(int32 Part, uint8 Grade, uint8 SetA, int8 SetB);
    // Test harness, server only: a door, bonnet or boot lid opened or shut.
    void DebugSetOpen(int32 Panel, bool bOpen);

    // Saving (docs/SAVING.md), server only. Conditions without their loose parts, and every part of this
    // car lying about, carried or in its boot.
    TArray<FRallyPanelCondition> SaveConditions() const;
    TArray<FRallySavedPart> SaveParts() const;
    // Every part as saved: conditions, the jack, and the loose parts (all existing ones go first).
    void Restore(const TArray<FRallyPanelCondition>& Saved, int8 InJackCorner, const TArray<FRallySavedPart>& Parts);
    // A point to aim at from Eye that the ray hits this part first (test harness).
    FVector GetAimPoint(int32 Part, const FVector& Eye, bool bCrouched) const;
    FString InteractionPrompt(const ARallyCharacter* Character, int32 Panel) const;
    bool Refit(ARallyCharacter* Character, ARallyDebrisActor* Part, bool bBodge);
    bool StrapPanel(ARallyCharacter* Character, int32 Panel);
    // Server only: opens a shut door, bonnet or boot lid by hand, or shuts it again.
    bool ToggleOpen(ARallyCharacter* Character, int32 Panel);
    // Server only: someone in a seat opens or pulls shut their own door.
    bool ToggleOpenFromInside(int32 Door);
    // Server only: one action towards taking a part off by hand (Rally::Panel: unbolt, pull off, cut the
    // strap, or take a wheel off). On the last one the part comes off into the player's hands.
    bool Remove(ARallyCharacter* Character, int32 Part);
    // Where the part is now, in the world: what a player looks at to pick it.
    FVector GetPartLocation(int32 Part) const;
    ARallyDebrisActor* GetDebris(int32 Panel) const;
    static FString PanelName(int32 Panel);

private:
    UFUNCTION() void OnRep_Conditions();
    void Detach(int32 Panel);
    void LogState(int32 Panel, Rally::Panel::EStage Before) const;
    // Cosmetic hinge motion of one attached panel, on every machine.
    void MovePanel(int32 Panel, float Dt, float Kmh, const FTransform& Car, const FVector& CarVelocity, uint8 DoorsOpen);
    // The panel's transform relative to the car at these hinge angles, including how it was refitted.
    FTransform PanelPose(int32 Panel, double SwingDeg, double DroopDeg) const;
    bool PoseBlocked(int32 Panel, const FTransform& Car, const FTransform& Pose, const FVector& Anchor) const;
    // Server only: the panel struck something at Speed (m/s) at PointCar, pushed along PushCar (car frame).
    void SwingHit(int32 Panel, const FVector& PointCar, const FVector& PushCar, double Speed, bool bForcedOpen);
    // Server only: both hinges lose this much health (0..1); an open panel whose hinges give way comes off.
    void StrainHinges(int32 Panel, double Loss, const TCHAR* Why);
    bool WheelOn(int32 Part) const;
    // A wheel can go on a corner once its strut, arm and brake disc are all there.
    bool CornerReady(int32 Corner) const;
    ERallyPart PartOfGear(int32 Slot) const;
    // Server only: the boot's spares kit (spare wheel, jack, suspension spares), stowed.
    void SpawnSpares();
    UFUNCTION() void OnRep_Jack();
    UPROPERTY(ReplicatedUsing=OnRep_Conditions) TArray<FRallyPanelCondition> Conditions;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Meshes;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
    UPROPERTY(Transient) TObjectPtr<URallyBodyDeformComponent> Deform;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Gear;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Exhaust;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Modules;
    double BootKitKg = 0.0;
    // The boot kit's places (relative to the car): which kit slot, laid how, the box it takes up.
    struct FBootHome { int32 Slot; FVector Location; FRotator Turn; FBox Box; };
    TArray<FBootHome> BootHomes;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> BootGhosts;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> BootGhostLooks;
    double NextGhostUpdate = 0.0;
    // The mesh a boot kit item is drawn with.
    UStaticMesh* KitMesh(int32 Slot) const;
    void BuildBootHomes();
    bool HomeFree(int32 Home, const ARallyDebrisActor* Ignore) const;
    // Where a part with this mesh lies in that place.
    void HomePlace(int32 Home, const UStaticMesh* Mesh, FVector& OutLocation, FRotator& OutTurn) const;
    static bool HomeFits(int32 HomeSlot, int32 PartSlot);
    // Where each fixing sits on its part, in the frame of the mesh it rides on, and which way it faces.
    struct FFixingSpot
    {
        Rally::Fixing::EKind Kind;
        Rally::Fixing::ETool Tool;
        FVector Local;
        FVector Facing;
    };
    TArray<FFixingSpot> Spots;
    // First spot of each slot; one more entry than slots.
    TArray<int32> SpotStart;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> FixingMeshes;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FixingSteel;
    // Fixing heads by kind (Rally::Fixing::EKind order), from the 1.8T model; the steel cylinder if missing.
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> FixingHeads;
    // A bolt backed out shows its thread between the part and its head (one per fixing; null for nuts,
    // screws and clips).
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> FixingShanks;
    // The lit part's fixings show through what is in front of them: a faint amber copy of each head drawn
    // over everything (M_Prototype_Ghost), from a small pool.
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ThroughHeads;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ThroughLook;
    void UpdateThroughHeads();
    // Each fixing as drawn (every machine): how far it stands out (0 home, 1 out) and how far it has spun
    // (degrees); a change of state eases in, spinning.
    TArray<float> FixingTravel;
    TArray<float> FixingSpin;
    int32 TurnShowSlot = INDEX_NONE;
    int32 TurnShowFixing = INDEX_NONE;
    float TurnShowProgress = 0.0f;
    bool bTurnShowHome = false;
    void AnimateFixings(float Dt);
    void PlaceFixing(int32 Slot, int32 F);
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GlowAimed;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GlowPart;
    int32 LitSlot = INDEX_NONE;
    int32 LitFixing = INDEX_NONE;
    // Fixings working loose while driving, per slot (Rally::Fixing::WorkLoose).
    TArray<double> LooseCarry;
    // A part just offered up: how squarely it was offered (degrees off its mount) and the pace of its
    // fixings going home, which decide how crooked it sits once they are all in.
    TArray<double> OfferAimDeg;
    TArray<double> FitFirstTurn;
    TArray<double> FitLastTurn;
    TArray<int32> FitTurns;
    void BuildFixings();
    void UpdateFixingVisuals();
    UStaticMeshComponent* FixingParent(int32 Slot) const;
    // The part is on the car, so its fixings are there to turn.
    bool FixingsPresent(int32 Slot) const;
    // Why its fixings cannot be reached now ("open the door"), or empty.
    FString FixingAccess(int32 Slot) const;
    // All fixings of a freshly offered part home: how crooked it ends up.
    void FinishFit(int32 Slot, ARallyCharacter* Character);
    // Server only: a wheel or running-gear part whose fixings have all worked out drops off the moving car.
    void FallOff(int32 Slot);
    UFUNCTION(NetMulticast, Unreliable) void MulticastFixingLoose(uint8 Slot, bool bNoneLeft);
    // Engine modules as fitted, and how crooked each sits now (every machine): driving with fixings out
    // shakes it out of true over a few seconds (0..1, ModuleShake), and it chatters on the move; one
    // unbolted on a parked car stays put to be lifted off. Straight again with every fixing home or off.
    TArray<FTransform> ModuleRest;
    TArray<double> ModuleTilt;
    TArray<double> ModuleShake;
    double ShakeTime = 0.0;
    void PoseModules(float Dt, float Kmh);
    // How the engine sits on its mounts now (ARallyCarPawn::UpdateEngineMotion), in the car's frame: the
    // modules bolted to it move with it.
    FTransform EngineMotion = FTransform::Identity;
    bool bEngineMoving = false;
    // Every fixing of the slot out (a part just offered up).
    uint16 AllOutMask(int32 Slot) const { return static_cast<uint16>((1u << NumFixings(Slot)) - 1u); }
    // The corner the jack holds up, or -1.
    UPROPERTY(ReplicatedUsing=OnRep_Jack) int8 JackCorner = -1;
    TArray<FTransform> Rest;
    TArray<double> WearRemainder;
    // Swing about the hinge line, and droop about a lone surviving hinge or bumper clip.
    TArray<Rally::Panel::FHinge> Swing;
    TArray<Rally::Panel::FHinge> Droop;
    // Last hinge angles (swing, droop) at which the panel touched nothing.
    TArray<FVector2D> LastClear;
    // Mesh-space points each panel touches things with, and how far the farthest is from its hinge line (m).
    TArray<TArray<FVector>> Probes;
    TArray<double> EdgeLever;
    TArray<double> LastSwingHit;
    // Angle a hand, prop or latch is taking each panel to, moving at hand pace.
    TArray<double> HandTarget;
    // Time left for the hand guiding a panel it opened; whether it was left open last frame.
    TArray<double> GuideTime;
    TArray<bool> WasLeftOpen;
    FTransform PreviousCar = FTransform::Identity;
    bool bHavePreviousCar = false;
    FVector PreviousVelocity = FVector::ZeroVector;
    // Car acceleration in its own frame, in g; smoothed for the panels so frame noise does not rattle them.
    FVector SmoothedAccel = FVector::ZeroVector;
    bool bHaveVelocity = false;
};
