#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Damage/PanelRules.h"
#include "RallyPanelComponent.generated.h"

class ARallyCharacter;
class ARallyDebrisActor;
class UStaticMeshComponent;
class URallyBodyDeformComponent;
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
    Rally::Panel::FFixings Fixings() const { return { HingeA, HingeB, Latch, Strap }; }
    void Set(const Rally::Panel::FFixings& F)
    {
        HingeA = static_cast<uint8>(F.A); HingeB = static_cast<uint8>(F.B);
        Latch = static_cast<uint8>(F.Latch); Strap = static_cast<uint8>(F.Strap);
    }
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
    static constexpr int32 NumSlots = GearboxSlot + 1;
    static bool IsBayPoint(int32 Part) { return Part == RadiatorSlot || Part == BatterySlot || Part == OilFillerSlot; }
    static bool IsUnderCar(int32 Part) { return Part == SumpSlot || Part == TankSlot || Part == ExhaustSlot || Part == GearboxSlot; }
    static bool IsServicePoint(int32 Part) { return IsBayPoint(Part) || IsUnderCar(Part); }
    // Working under the car: the jack holds up the front (sump, gearbox) or the rear (tank, exhaust).
    bool IsJackedFor(int32 Part) const
    {
        return JackCorner >= 0 && ((Part == SumpSlot || Part == GearboxSlot) == (JackCorner < 2));
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
        UStaticMeshComponent* InExhaust, URallyBodyDeformComponent* InDeform);
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
    bool CanReach(const ARallyCharacter* Character, int32 Panel) const;
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
