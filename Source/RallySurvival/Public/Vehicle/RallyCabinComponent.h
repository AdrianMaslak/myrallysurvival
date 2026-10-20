#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Vehicle/CabinRules.h"
#include "RallyCabinComponent.generated.h"

class ARallyCarPawn;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USpotLightComponent;
class UStaticMeshComponent;

// The cabin and the lamps (docs/OCTAVIA-VEHICLE.md, Cabin and lamps). The driver's switches are replicated here
// (indicators and hazards, high beam, wipers, glove box, sun visor); everything else is drawn on each machine from
// what the car already replicates: the gauge needles, the warning lamps and the dial lighting, the pedals, gear
// lever and handbrake, the stalks, the wipers sweeping, and every lamp of the body glowing (dipped and high beam,
// side and tail lamps, brake lamps from the pedal, the reversing lamp in reverse, the indicators flashing), with
// real lights for the beams, the brake lamps and the reversing lamp. Lamps need the battery, and a lamp unit off the
// car stays dark.
UCLASS()
class RALLYSURVIVAL_API URallyCabinComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URallyCabinComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // The moving parts, each its own mesh at its pivot (assembly frame, cm) turning about Axis (FQuat axis) from
    // its rest: Art/Vehicles/Octavia/Body_v2/Export/Game/game_parts.json. The car makes a mesh per mount.
    enum class EPart : uint8
    {
        WiperL, WiperR, NeedleSpeedo, NeedleTacho, NeedleTemp, NeedleFuel, PedalClutch, PedalBrake, PedalGas,
        GearLever, Handbrake, StalkSignal, StalkWiper, GloveBox, SunVisorL, SunVisorR, Count
    };
    struct FMount
    {
        const TCHAR* Name;
        FVector Pivot;
        FVector Axis;
        // Full travel (degrees): the wipers' sweep, a pedal pressed, the handbrake pulled, a lid open.
        double RangeDeg;
    };
    static const FMount& Mount(EPart Part);

    // Before the dent-able copies take the materials over (ARallyCarPawn::BeginPlay): the moving meshes in EPart
    // order, the panels in URallyPanelComponent order (for their lamps), the interior (the cluster), the body the
    // lights hang from and the car's two headlamp beams.
    void Initialize(const TArray<UStaticMeshComponent*>& InParts, const TArray<UStaticMeshComponent*>& InPanels,
        UStaticMeshComponent* InInterior, UStaticMeshComponent* InWindscreen, const TArray<UStaticMeshComponent*>& InPainted,
        USceneComponent* InBody, const FVector& InAssemblyOffset, USpotLightComponent* InBeamLeft, USpotLightComponent* InBeamRight);
    // Every machine, every frame (the car's Tick).
    void TickCabin(float Dt);

    // Server: the driver's switches. LightSwitch is the car's headlamps (ARallyCarPawn handles it).
    enum class EControl : uint8
    {
        HighBeam, IndicatorLeft, IndicatorRight, Hazard, Wipers, GloveBox, SunVisor, IndicatorStalk, Radio, DomeLight,
        LightSwitch, Horn, Count
    };
    void Operate(EControl Control);
    // Server: the horn held or let go.
    void SetHorn(bool bOn);

    // The controls the driver can reach for and click (docs/OCTAVIA-VEHICLE.md, Cabin and lamps): which one the
    // view (eye and direction, assembly frame) is on, or -1; its name and what it does.
    int32 FindAimedControl(const FVector& Eye, const FVector& Forward) const;
    static FString ControlLabel(int32 Index);
    static EControl ControlOf(int32 Index);
    Rally::Cabin::EIndicator GetIndicator() const { return static_cast<Rally::Cabin::EIndicator>(Indicator); }
    Rally::Cabin::EWiper GetWiper() const { return static_cast<Rally::Cabin::EWiper>(Wiper); }
    bool IsHighBeam() const { return bHighBeam; }
    // As drawn here (tests): the needles' angles, the lit warning lamps, the wipers' sweep 0..1.
    double GetNeedleDeg(EPart Needle) const;
    uint32 GetWarningLamps() const { return LitWarnings; }
    double GetWiperSweep() const { return WiperSweep; }
    // Rain on the windscreen as drawn here (0 dry .. 1 streaming), and mud on the paint (0 clean .. 1 caked).
    double GetWindscreenWet() const { return WindscreenWet; }
    double GetMud() const { return Mud; }
    bool AreBrakeLampsLit() const { return bBrakeLit; }
    // Where the driver's ankles go (world): the left over the clutch's pad while it is down or else the footrest,
    // the right over the brake's pad while braking or else the throttle's, each pad where its pedal has swung to.
    FVector GetFootTarget(bool bLeft) const;

private:
    UPROPERTY(Replicated) uint8 Indicator = 0;
    UPROPERTY(Replicated) uint8 Wiper = 0;
    UPROPERTY(Replicated) bool bHighBeam = false;
    UPROPERTY(Replicated) bool bGloveBoxOpen = false;
    UPROPERTY(Replicated) bool bSunVisorDown = false;
    UPROPERTY(Replicated) bool bRadioOn = true;
    UPROPERTY(Replicated) bool bDomeLight = false;
    UPROPERTY(Replicated) bool bHorn = false;
    double HornNext = 0.0;
    // Server: the furthest the wheel has turned the indicated way (Rally::Cabin::SelfCancel).
    double CancelFurthest = 0.0;

    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Panels;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> Body;
    FVector AssemblyOffset = FVector::ZeroVector;
    // The pedals as last drawn (0..1), for the feet.
    float LastClutch = 0.0f, LastBrake = 0.0f, LastThrottle = 0.0f;
    UPROPERTY(Transient) TObjectPtr<USpotLightComponent> BeamLeft;
    UPROPERTY(Transient) TObjectPtr<USpotLightComponent> BeamRight;
    UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> BrakeLights;
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> ReverseLight;
    // The interior lamp in the headliner: a small warm light and its lens.
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> DomeLight;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DomeLook;
    float ShownDome = -1.0f;

    // A lamp's glowing surfaces: what it is, which panel carries it (dark when that panel is off), and its looks.
    enum class ELamp : uint8
    {
        LowBeam, HighBeam, Fog, HeadLens, FrontIndicator, SideRepeater, SideLens, Tail, TailLens, Brake, Reverse,
        RearIndicator, Plate, Count
    };
    struct FLampLook
    {
        ELamp Lamp;
        // -1 left, 1 right, 0 middle.
        int32 Side;
        int32 Panel;
        TWeakObjectPtr<UMaterialInstanceDynamic> Look;
        float Shown = -1.0f;
    };
    TArray<FLampLook> LampLooks;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> LampLookRefs;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DialLook;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WarningLook;
    // The radio's and trip meter's displays and the buttons' labels (scripts/interior_details.py): lit on the ignition.
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> LabelLook;
    // Rain on the windscreen (M_Windscreen's Wet), from the day clock's rain, cleared by the wipers.
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WindscreenLook;
    TWeakObjectPtr<class ARallyDayClock> DayClock;
    double NextClockSearch = 0.0;
    double WindscreenWet = 0.0;
    float ShownWet = -1.0f;
    // Mud on the paint (M_Body_PaintMud on the shell and panels): thrown up on loose ground, washed by rain.
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PaintLooks;
    double Mud = 0.0;
    float ShownMud = -1.0f;
    double GroundLooseness = 0.0;
    double NextGroundCheck = 0.0;
    void UpdateMud(double Now, double Dt, double Rain);
    float ShownLabels = -1.0f;

    double NeedleShown[4] = { 0.0, 0.0, 0.0, 0.0 };
    double WiperPhase = 0.0;
    double WiperPause = 0.0;
    double WiperSweep = 0.0;
    double PartShown[static_cast<int32>(EPart::Count)] = {};
    // The gear lever leans two ways: PartShown holds fore and aft, this across.
    double LeverAcrossShown = 0.0;
    double IgnitionOnSince = -1.0;
    uint32 LitWarnings = 0;
    bool bBrakeLit = false;
    bool bWasFlashOn = false;
    float ShownBacklight = -1.0f;
    float ShownWarn[Rally::Cabin::WarningCount] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    int32 ShownBeam = -1;

    ARallyCarPawn* GetCar() const;
    void AddLampLooks(UStaticMeshComponent* Mesh, int32 Panel, int32 Side);
    void TurnPart(EPart Part, double Deg, double Dt, double Rate);
    void UpdateLamps(bool bPower, double Charge, bool bLightsOn, bool bBrake, bool bReverse, bool bLeftFlash, bool bRightFlash);
};
