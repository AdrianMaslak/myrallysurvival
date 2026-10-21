#include "Vehicle/RallyCabinComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Damage/RallyEngineComponent.h"
#include "Damage/RallyPanelComponent.h"
#include "Damage/UndersideRules.h"
#include "Engine/World.h"
#include "Game/RallyDayClock.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Vehicle/RallyCarPawn.h"

namespace
{
    using Rally::Cabin::EIndicator;
    using Rally::Cabin::EWiper;
    using EPart = URallyCabinComponent::EPart;

    // game_parts.json's pivots (assembly frame, cm) and FQuat axes; GearLever leans by FRotator instead.
    const URallyCabinComponent::FMount Mounts[] =
    {
        { TEXT("Wiper_L"), FVector(102.772, -60.937, 90.69), FVector(0.49908, 0.0, 0.86656), 92.0 },
        { TEXT("Wiper_R"), FVector(113.429, 3.256, 91.617), FVector(0.49908, 0.0, 0.86656), 96.0 },
        { TEXT("Needle_Speedo"), FVector(71.34, -29.35, 88.507), FVector(-1.0, 0.0, 0.0), 240.0 },
        { TEXT("Needle_Tacho"), FVector(71.32, -46.71, 88.507), FVector(-1.0, 0.0, 0.0), 240.0 },
        { TEXT("Needle_Temp"), FVector(72.14, -40.03, 93.233), FVector(-1.0, 0.0, 0.0), 90.0 },
        { TEXT("Needle_Fuel"), FVector(72.14, -35.95, 93.233), FVector(-1.0, 0.0, 0.0), 90.0 },
        { TEXT("Pedal_Clutch"), FVector(101.733, -48.636, 54.789), FVector(0.0, -1.0, 0.0), 18.0 },
        { TEXT("Pedal_Brake"), FVector(101.733, -35.918, 54.789), FVector(0.0, -1.0, 0.0), 18.0 },
        { TEXT("Pedal_Gas"), FVector(98.13, -21.904, 53.672), FVector(0.0, -1.0, 0.0), 14.0 },
        { TEXT("GearLever"), FVector(38.61, 0.02, 50.07), FVector(0.0, 1.0, 0.0), 11.0 },
        { TEXT("Handbrake"), FVector(-5.755, -2.687, 44.996), FVector(0.0, -1.0, 0.0), 22.0 },
        { TEXT("Stalk_Signal"), FVector(58.45, -45.298, 82.656), FVector(0.0, -1.0, 0.0), 12.0 },
        { TEXT("Stalk_Wiper"), FVector(57.923, -29.899, 83.15), FVector(0.0, -1.0, 0.0), 15.0 },
    };
    static_assert(static_cast<int32>(UE_ARRAY_COUNT(Mounts)) == static_cast<int32>(EPart::Count), "a mount per part");

    // What the driver can reach for (assembly frame, cm): measured on the v2 interior (the light switch's knob left of
    // the wheel, the hazard button over the stack, the radio's middle, the stalks' tips, the interior lamp, the wheel's hub for the horn).
    struct FControlPoint { const TCHAR* Label; FVector Point; URallyCabinComponent::EControl Control; };
    const FControlPoint ControlPoints[] =
    {
        { TEXT("Light switch"), FVector(61.3, -67.0, 77.0), URallyCabinComponent::EControl::LightSwitch },
        { TEXT("Hazards"), FVector(64.1, 0.0, 87.5), URallyCabinComponent::EControl::Hazard },
        { TEXT("Radio"), FVector(56.1, 1.5, 68.5), URallyCabinComponent::EControl::Radio },
        { TEXT("Indicators"), FVector(56.0, -52.0, 84.0), URallyCabinComponent::EControl::IndicatorStalk },
        { TEXT("Wipers"), FVector(56.0, -20.5, 85.0), URallyCabinComponent::EControl::Wipers },
        { TEXT("Interior lamp"), FVector(32.7, 0.0, 131.9), URallyCabinComponent::EControl::DomeLight },
        { TEXT("Horn"), FVector(47.3, -38.6, 87.4), URallyCabinComponent::EControl::Horn },
    };
    constexpr int32 NumControls = static_cast<int32>(UE_ARRAY_COUNT(ControlPoints));
    const FVector DomeLightPoint(32.7, 0.0, 127.0);
    // The pedals' pads and the footrest (assembly frame, cm: the lowest third of each mesh), and where the ankle
    // sits from the ball of the foot on a pad: back along the car and up.
    const FVector ClutchPad(86.3, -49.2, 36.9), BrakePad(86.8, -36.0, 37.2), ThrottlePad(85.2, -21.7, 34.6);
    const FVector FootrestPad(78.8, -60.8, 35.2);
    const FVector AnkleFromPad(-13.0, 0.0, 8.0);

    // URallyPanelComponent's order: the lamp units and the panels with lamps in them.
    constexpr int32 TrunkPanel = 5;
    constexpr int32 FenderLeft = 9, FenderRight = 10, HeadlightLeft = 11, HeadlightRight = 12, TaillightLeft = 13, TaillightRight = 14;
    // Where the real lights sit, assembly frame (in the taillights' and the boot lid's lamps).
    const FVector BrakeLightPoints[2] = { FVector(-224.0, -60.0, 86.0), FVector(-224.0, 60.0, 86.0) };
    const FVector ReverseLightPoint(-224.0, -58.0, 80.0);
    // The beams: dipped a little down and out, high further and narrower.
    constexpr float LowBeamIntensity = 9000.0f, HighBeamIntensity = 16000.0f;
    constexpr float LowBeamReach = 6500.0f, HighBeamReach = 12000.0f;
    constexpr float LowBeamCone = 34.0f, HighBeamCone = 22.0f;
    constexpr float LowBeamPitch = -4.0f, HighBeamPitch = -1.0f;

    // Which lamp a surface of a panel is (its material, MI_Body_<surface>, scripts/import_body_v2.py).
    bool LampOf(const FString& Material, int32 Panel, uint8& OutLamp)
    {
        using ELamp = uint8;
        const bool bHead = Panel == HeadlightLeft || Panel == HeadlightRight;
        const bool bTail = Panel == TaillightLeft || Panel == TaillightRight;
        const bool bFender = Panel == FenderLeft || Panel == FenderRight;
        const bool bTrunk = Panel == TrunkPanel;
        auto Is = [&Material](const TCHAR* Surface) { return Material.EndsWith(Surface); };
        // Values are URallyCabinComponent::ELamp's.
        ELamp Lamp = 255;
        if (bHead && Is(TEXT("lamp_lowbeam"))) Lamp = 0;
        else if (bHead && Is(TEXT("lamp_highbeam"))) Lamp = 1;
        else if (bHead && Is(TEXT("lamp_fog"))) Lamp = 2;
        else if (bHead && Is(TEXT("lens_clear"))) Lamp = 3;
        else if (bHead && Is(TEXT("lamp_indicator"))) Lamp = 4;
        else if (bFender && Is(TEXT("lamp_indicator"))) Lamp = 5;
        else if (bFender && Is(TEXT("lens_amber"))) Lamp = 6;
        else if (bTail && Is(TEXT("lamp_tail"))) Lamp = 7;
        else if (bTail && Is(TEXT("lens_red"))) Lamp = 8;
        else if ((bTail || bTrunk) && Is(TEXT("lamp_brake"))) Lamp = 9;
        else if (bTail && Is(TEXT("lamp_reverse"))) Lamp = 10;
        else if (bTail && Is(TEXT("lamp_indicator"))) Lamp = 11;
        else if (bTrunk && Is(TEXT("lamp_plate"))) Lamp = 12;
        OutLamp = Lamp;
        return Lamp != 255;
    }
}

URallyCabinComponent::URallyCabinComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void URallyCabinComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(URallyCabinComponent, Indicator);
    DOREPLIFETIME(URallyCabinComponent, Wiper);
    DOREPLIFETIME(URallyCabinComponent, bHighBeam);
    DOREPLIFETIME(URallyCabinComponent, bRadioOn);
    DOREPLIFETIME(URallyCabinComponent, bDomeLight);
    DOREPLIFETIME(URallyCabinComponent, bHorn);
    DOREPLIFETIME(URallyCabinComponent, MudLevel);
}

const URallyCabinComponent::FMount& URallyCabinComponent::Mount(EPart Part)
{
    return Mounts[FMath::Clamp(static_cast<int32>(Part), 0, static_cast<int32>(EPart::Count) - 1)];
}

ARallyCarPawn* URallyCabinComponent::GetCar() const
{
    return Cast<ARallyCarPawn>(GetOwner());
}

void URallyCabinComponent::Initialize(const TArray<UStaticMeshComponent*>& InParts, const TArray<UStaticMeshComponent*>& InPanels,
    UStaticMeshComponent* InInterior, UStaticMeshComponent* InWindscreen, const TArray<UStaticMeshComponent*>& InPainted,
    USceneComponent* InBody, const FVector& InAssemblyOffset, USpotLightComponent* InBeamLeft, USpotLightComponent* InBeamRight)
{
    PaintLooks.Reset();
    for (UStaticMeshComponent* Mesh : InPainted)
        for (int32 Slot = 0; Mesh && Slot < Mesh->GetNumMaterials(); ++Slot)
        {
            const UMaterialInterface* Material = Mesh->GetMaterial(Slot);
            if (Material && Material->GetName() == TEXT("M_Body_PaintMud"))
                if (UMaterialInstanceDynamic* Look = Mesh->CreateDynamicMaterialInstance(Slot)) PaintLooks.Add(Look);
        }
    for (int32 Slot = 0; InWindscreen && Slot < InWindscreen->GetNumMaterials(); ++Slot)
    {
        const UMaterialInterface* Material = InWindscreen->GetMaterial(Slot);
        if (Material && Material->GetName().EndsWith(TEXT("Windscreen"))) WindscreenLook = InWindscreen->CreateDynamicMaterialInstance(Slot);
    }
    Parts.Reset();
    for (UStaticMeshComponent* Part : InParts) Parts.Add(Part);
    Panels.Reset();
    for (UStaticMeshComponent* Panel : InPanels) Panels.Add(Panel);
    BeamLeft = InBeamLeft;
    BeamRight = InBeamRight;
    Body = InBody;
    AssemblyOffset = InAssemblyOffset;

    LampLooks.Reset();
    LampLookRefs.Reset();
    for (int32 Panel = 0; Panel < Panels.Num(); ++Panel)
    {
        const bool bLeft = Panel == FenderLeft || Panel == HeadlightLeft || Panel == TaillightLeft;
        const bool bRight = Panel == FenderRight || Panel == HeadlightRight || Panel == TaillightRight;
        if (bLeft || bRight || Panel == TrunkPanel) AddLampLooks(Panels[Panel], Panel, bLeft ? -1 : bRight ? 1 : 0);
    }
    // The cluster: the dials' lighting and the warning strip (M_Body_Dash).
    for (int32 Slot = 0; InInterior && Slot < InInterior->GetNumMaterials(); ++Slot)
    {
        const UMaterialInterface* Material = InInterior->GetMaterial(Slot);
        const FString Name = Material ? Material->GetName() : FString();
        if (Name.EndsWith(TEXT("gauge_faces"))) DialLook = InInterior->CreateDynamicMaterialInstance(Slot);
        else if (Name.EndsWith(TEXT("gauge_warning"))) WarningLook = InInterior->CreateDynamicMaterialInstance(Slot);
        else if (Name.EndsWith(TEXT("dash_labels"))) LabelLook = InInterior->CreateDynamicMaterialInstance(Slot);
        else if (Name.EndsWith(TEXT("lens_clear"))) DomeLook = InInterior->CreateDynamicMaterialInstance(Slot);
    }

    // Real light from the brake lamps and the reversing lamp: small, red and white, no shadows.
    AActor* Owner = GetOwner();
    if (Owner && InBody && BrakeLights.IsEmpty())
    {
        auto MakeLight = [&](const TCHAR* Name, const FVector& Point, const FLinearColor& Colour, float Intensity, float Radius) {
            UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner, Name);
            Light->SetupAttachment(InBody);
            Light->SetRelativeLocation(InAssemblyOffset + Point);
            Light->SetLightColor(Colour);
            Light->SetIntensity(Intensity);
            Light->SetAttenuationRadius(Radius);
            Light->SetCastShadows(false);
            Light->SetVisibility(false);
            Light->RegisterComponent();
            return Light;
        };
        BrakeLights.Add(MakeLight(TEXT("BrakeLightLeft"), BrakeLightPoints[0], FLinearColor(1.0f, 0.05f, 0.03f), 900.0f, 450.0f));
        BrakeLights.Add(MakeLight(TEXT("BrakeLightRight"), BrakeLightPoints[1], FLinearColor(1.0f, 0.05f, 0.03f), 900.0f, 450.0f));
        ReverseLight = MakeLight(TEXT("ReverseLight"), ReverseLightPoint, FLinearColor(1.0f, 0.95f, 0.88f), 1500.0f, 700.0f);
        DomeLight = MakeLight(TEXT("InteriorLamp"), DomeLightPoint, FLinearColor(1.0f, 0.85f, 0.6f), 120.0f, 220.0f);
    }
    UE_LOG(LogTemp, Display, TEXT("RALLY_CABIN parts=%d lamp looks=%d dial=%d warning=%d labels=%d"), Parts.Num(), LampLooks.Num(),
        DialLook ? 1 : 0, WarningLook ? 1 : 0, LabelLook ? 1 : 0);
}

void URallyCabinComponent::AddLampLooks(UStaticMeshComponent* Mesh, int32 Panel, int32 Side)
{
    for (int32 Slot = 0; Mesh && Slot < Mesh->GetNumMaterials(); ++Slot)
    {
        const UMaterialInterface* Material = Mesh->GetMaterial(Slot);
        uint8 Lamp = 0;
        if (!Material || !LampOf(Material->GetName(), Panel, Lamp)) continue;
        UMaterialInstanceDynamic* Look = Mesh->CreateDynamicMaterialInstance(Slot);
        if (!Look) continue;
        LampLookRefs.Add(Look);
        LampLooks.Add({ static_cast<ELamp>(Lamp), Side, Panel, Look });
    }
}

void URallyCabinComponent::Operate(EControl Control)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    const EIndicator Mode = GetIndicator();
    switch (Control)
    {
    case EControl::HighBeam: bHighBeam = !bHighBeam; break;
    case EControl::IndicatorLeft: Indicator = static_cast<uint8>(Mode == EIndicator::Left ? EIndicator::Off : EIndicator::Left); break;
    case EControl::IndicatorRight: Indicator = static_cast<uint8>(Mode == EIndicator::Right ? EIndicator::Off : EIndicator::Right); break;
    case EControl::Hazard: Indicator = static_cast<uint8>(Mode == EIndicator::Hazard ? EIndicator::Off : EIndicator::Hazard); break;
    case EControl::Wipers: Wiper = static_cast<uint8>(Rally::Cabin::NextWiper(GetWiper())); break;
    case EControl::IndicatorStalk: Indicator = static_cast<uint8>(Rally::Cabin::NextIndicator(Mode)); break;
    case EControl::Radio: bRadioOn = !bRadioOn; break;
    case EControl::DomeLight: bDomeLight = !bDomeLight; break;
    // The car's own (ARallyCarPawn::ServerCabinControl), and the horn is held (SetHorn).
    case EControl::LightSwitch: case EControl::Horn: case EControl::Count: break;
    }
    CancelFurthest = 0.0;
    GetOwner()->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("RALLY_CABIN control=%d indicator=%d wipers=%d highbeam=%d"), static_cast<int32>(Control),
        static_cast<int32>(Indicator), static_cast<int32>(Wiper), bHighBeam ? 1 : 0);
}

void URallyCabinComponent::SetHorn(bool bOn)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || bHorn == bOn) return;
    bHorn = bOn;
    GetOwner()->ForceNetUpdate();
}

int32 URallyCabinComponent::FindAimedControl(const FVector& Eye, const FVector& Forward) const
{
    Rally::Cabin::FVec Points[NumControls];
    for (int32 I = 0; I < NumControls; ++I)
        Points[I] = { ControlPoints[I].Point.X, ControlPoints[I].Point.Y, ControlPoints[I].Point.Z };
    return Rally::Cabin::AimedControl({ Eye.X, Eye.Y, Eye.Z }, { Forward.X, Forward.Y, Forward.Z }, Points, NumControls);
}

FString URallyCabinComponent::ControlLabel(int32 Index)
{
    return Index >= 0 && Index < NumControls ? FString(ControlPoints[Index].Label) : FString();
}

URallyCabinComponent::EControl URallyCabinComponent::ControlOf(int32 Index)
{
    return Index >= 0 && Index < NumControls ? ControlPoints[Index].Control : EControl::Count;
}

void URallyCabinComponent::SetMud(double InMud)
{
    Mud = FMath::Clamp(InMud, 0.0, 1.0);
    MudLevel = static_cast<uint8>(FMath::RoundToInt(Mud * 255.0));
}

void URallyCabinComponent::SetPaintTint(const FLinearColor& Tint)
{
    for (UMaterialInstanceDynamic* Look : PaintLooks)
        if (Look) Look->SetVectorParameterValue(TEXT("PaintTint"), Tint);
}

void URallyCabinComponent::UpdateMud(double Now, double Dt, double Rain)
{
    ARallyCarPawn* Car = GetCar();
    UWorld* World = GetWorld();
    if (!Car || !World || !Body) return;
    // What the car stands on, twice a second: loose ground throws mud (URallyPhysicalMaterial's Looseness).
    if (Now >= NextGroundCheck)
    {
        NextGroundCheck = Now + 0.5;
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyCabinGround), false, Car);
        Params.bReturnPhysicalMaterial = true;
        const FVector From = Body->GetComponentLocation();
        GroundLooseness = 0.0;
        if (World->LineTraceSingleByChannel(Hit, From, From - FVector(0.0, 0.0, 300.0), ECC_Visibility, Params))
            if (const URallyPhysicalMaterial* Ground = Cast<URallyPhysicalMaterial>(Hit.PhysMaterial.Get())) GroundLooseness = Ground->Looseness;
    }
    // The server keeps the mud and everyone draws its; clients follow what it sends.
    if (Car->HasAuthority())
    {
        Mud = Rally::Cabin::MudAfter(Mud, Car->GetForwardSpeedKmh(), GroundLooseness, Rain, Dt);
        MudLevel = static_cast<uint8>(FMath::RoundToInt(Mud * 255.0));
    }
    else Mud = MudLevel / 255.0;
    // The paint finds the ground by the car's own frame, so the mud line stays on the car as it rolls.
    const FTransform Frame = FTransform(AssemblyOffset) * Body->GetComponentTransform();
    const FLinearColor Origin(Frame.GetLocation());
    const FLinearColor Up(Frame.GetUnitAxis(EAxis::Z));
    const bool bMudChanged = FMath::Abs(static_cast<float>(Mud) - ShownMud) > 0.005f;
    if (bMudChanged) ShownMud = static_cast<float>(Mud);
    for (UMaterialInstanceDynamic* Look : PaintLooks)
    {
        if (!Look) continue;
        Look->SetVectorParameterValue(TEXT("CarOrigin"), Origin);
        Look->SetVectorParameterValue(TEXT("CarUp"), Up);
        if (bMudChanged) Look->SetScalarParameterValue(TEXT("Mud"), ShownMud);
    }
}

FVector URallyCabinComponent::GetFootTarget(bool bLeft) const
{
    // The pad turned with its pedal about the pedal's pivot, as the pedal's mesh is.
    auto PadOf = [this](EPart Pedal, const FVector& Pad) {
        const FMount& M = Mount(Pedal);
        const double Deg = PartShown[static_cast<int32>(Pedal)];
        return M.Pivot + FQuat(M.Axis.GetSafeNormal(), FMath::DegreesToRadians(Deg)).RotateVector(Pad - M.Pivot);
    };
    FVector Pad;
    if (bLeft) Pad = LastClutch > 0.05f ? PadOf(EPart::PedalClutch, ClutchPad) : FootrestPad;
    else Pad = LastBrake > 0.05f && LastBrake >= LastThrottle ? PadOf(EPart::PedalBrake, BrakePad) : PadOf(EPart::PedalGas, ThrottlePad);
    const FTransform Frame = Body ? FTransform(AssemblyOffset) * Body->GetComponentTransform() : FTransform(AssemblyOffset);
    return Frame.TransformPosition(Pad + AnkleFromPad);
}

double URallyCabinComponent::GetNeedleDeg(EPart Needle) const
{
    const int32 Index = static_cast<int32>(Needle) - static_cast<int32>(EPart::NeedleSpeedo);
    return Index >= 0 && Index < 4 ? NeedleShown[Index] : 0.0;
}

void URallyCabinComponent::TurnPart(EPart Part, double Deg, double Dt, double Rate)
{
    const int32 Index = static_cast<int32>(Part);
    if (!Parts.IsValidIndex(Index) || !Parts[Index]) return;
    double& Shown = PartShown[Index];
    const double Next = Rate > 0.0 ? Rally::Cabin::FollowNeedle(Shown, Deg, Rate, Dt) : Deg;
    if (FMath::Abs(Next - Shown) < 0.01 && Rate > 0.0) return;
    Shown = Next;
    if (Part == EPart::GearLever) return;
    Parts[Index]->SetRelativeRotation(FQuat(Mount(Part).Axis.GetSafeNormal(), FMath::DegreesToRadians(Shown)));
}

void URallyCabinComponent::TickCabin(float Dt)
{
    ARallyCarPawn* Car = GetCar();
    URallyEngineComponent* Engine = Car ? Car->GetEngine() : nullptr;
    if (!Car || !Engine) return;
    const UWorld* World = GetWorld();
    const double Now = World ? World->GetTimeSeconds() : 0.0;
    const ARallyCarPawn::FCabinReadout Readout = Car->GetCabinReadout();

    // Server: a turn indicator cancels as the wheel comes back.
    if (Car->HasAuthority())
    {
        const EIndicator Before = GetIndicator();
        const EIndicator After = Rally::Cabin::SelfCancel(Before, Readout.SteeringWheelDeg, CancelFurthest);
        if (After != Before) { Indicator = static_cast<uint8>(After); Car->ForceNetUpdate(); }
    }

    const bool bIgnition = Engine->IsIgnitionOn();
    if (!bIgnition) IgnitionOnSince = -1.0;
    else if (IgnitionOnSince < 0.0) IgnitionOnSince = Now;
    const bool bPower = Car->HasLampPower();
    const double Charge = Engine->GetCharge();

    // The dials (the needles move on the ignition's power too).
    using namespace Rally::Cabin;
    const bool bDials = bIgnition && bPower;
    const double Targets[4] = {
        SpeedoDeg(Car->GetForwardSpeedKmh(), bDials), TachoDeg(Car->GetEngineRpm(), bDials), TempDeg(Engine->GetTempC(), bDials),
        FuelDeg(Engine->GetFuelL(), Rally::Underside::FuelCapacityL, bDials) };
    const double Rates[4] = { 6.0, 12.0, 1.5, 1.0 };
    for (int32 I = 0; I < 4; ++I)
    {
        NeedleShown[I] = FollowNeedle(NeedleShown[I], Targets[I], Rates[I], Dt);
        TurnPart(static_cast<EPart>(static_cast<int32>(EPart::NeedleSpeedo) + I), NeedleShown[I], Dt, 0.0);
    }

    // Feet, hands and the stalks.
    LastClutch = Readout.Clutch;
    LastBrake = Readout.Brake;
    LastThrottle = Readout.Throttle;
    TurnPart(EPart::PedalClutch, Readout.Clutch * Mount(EPart::PedalClutch).RangeDeg, Dt, 25.0);
    TurnPart(EPart::PedalBrake, Readout.Brake * Mount(EPart::PedalBrake).RangeDeg, Dt, 25.0);
    TurnPart(EPart::PedalGas, Readout.Throttle * Mount(EPart::PedalGas).RangeDeg, Dt, 25.0);
    TurnPart(EPart::Handbrake, Readout.bHandbrake ? Mount(EPart::Handbrake).RangeDeg : 0.0, Dt, 18.0);
    TurnPart(EPart::StalkSignal, SignalStalkDeg(GetIndicator()), Dt, 20.0);
    TurnPart(EPart::StalkWiper, WiperStalkDeg(GetWiper()), Dt, 20.0);
    {
        const int32 Lever = static_cast<int32>(EPart::GearLever);
        const FLeverPose Pose = GearLever(Readout.Gear);
        PartShown[Lever] = FollowNeedle(PartShown[Lever], Pose.ForeDeg, 20.0, Dt);
        LeverAcrossShown = FollowNeedle(LeverAcrossShown, Pose.AcrossDeg, 20.0, Dt);
        // Forward tips the knob to +X (negative pitch); to the right tips it to +Y (positive roll).
        if (Parts.IsValidIndex(Lever) && Parts[Lever])
            Parts[Lever]->SetRelativeRotation(FRotator(-PartShown[Lever], 0.0, LeverAcrossShown));
    }

    // The wipers run on the ignition, and clear the rain from the glass as each blade passes.
    const double SweepBefore = WiperSweep;
    WiperSweep = AdvanceWiper(bIgnition && bPower ? GetWiper() : EWiper::Off, WiperPhase, WiperPause, Dt);
    if (!DayClock.IsValid() && Now >= NextClockSearch)
    {
        DayClock = ARallyDayClock::Get(World);
        NextClockSearch = Now + 2.0;
    }
    const double Rain = DayClock.IsValid() ? DayClock->GetRain() : 0.0;
    WindscreenWet = WetAfter(WindscreenWet, Rain, Dt);
    UpdateMud(Now, Dt, Rain);
    if (BladePassed(SweepBefore, WiperSweep)) WindscreenWet = FMath::Min(WindscreenWet, WipedWet);
    if (WindscreenLook && FMath::Abs(static_cast<float>(WindscreenWet) - ShownWet) > 0.005f)
    {
        ShownWet = static_cast<float>(WindscreenWet);
        WindscreenLook->SetScalarParameterValue(TEXT("Wet"), ShownWet);
    }
    TurnPart(EPart::WiperL, WiperSweep * Mount(EPart::WiperL).RangeDeg, Dt, 0.0);
    TurnPart(EPart::WiperR, WiperSweep * Mount(EPart::WiperR).RangeDeg, Dt, 0.0);

    // Lamps.
    const bool bFlash = FlashOn(Now);
    const bool bLeftFlash = bPower && LeftLit(GetIndicator(), Now);
    const bool bRightFlash = bPower && RightLit(GetIndicator(), Now);
    if (GetIndicator() != EIndicator::Off && bPower && bFlash != bWasFlashOn) Car->PlayCabinClick(bFlash ? 0.12f : 0.08f);
    bWasFlashOn = bFlash;
    const bool bBrake = bIgnition && Readout.Brake > 0.05f;
    const bool bReverse = bIgnition && Readout.Gear < 0;
    UpdateLamps(bPower, Charge, Car->AreLightsOn(), bBrake, bReverse, bLeftFlash, bRightFlash);

    // The cluster: the dials lit with the lamps, and the warning strip.
    FWarningInputs In;
    In.bIgnition = bIgnition && bPower;
    In.bRunning = Engine->IsRunning();
    In.IgnitionSeconds = IgnitionOnSince >= 0.0 ? Now - IgnitionOnSince : 0.0;
    In.OilL = Engine->GetOilL();
    In.Charge = Charge;
    In.bCharging = Car->IsAlternatorCharging();
    In.bHandbrake = Readout.bHandbrake;
    In.bLeftFlash = bLeftFlash;
    In.bRightFlash = bRightFlash;
    In.bHighBeam = bPower && bHighBeam && Car->AreLightsOn();
    LitWarnings = WarningLamps(In);
    const float Supply = static_cast<float>(SupplyScale(Charge));
    if (DialLook)
    {
        const float Backlight = bPower && Car->AreLightsOn() ? 0.6f * Supply : 0.0f;
        if (Backlight != ShownBacklight) { DialLook->SetScalarParameterValue(TEXT("Backlight"), Backlight); ShownBacklight = Backlight; }
    }
    // The horn, held, on every machine; it needs the battery, not the ignition.
    if (bHorn && bPower && Now >= HornNext)
    {
        Car->PlayHorn();
        HornNext = Now + HornRepeatS;
    }
    // The radio plays on the ignition with its switch on.
    {
        const float Radio = In.bIgnition && bRadioOn ? 0.45f * Supply : 0.0f;
        if (Radio != ShownRadio) { Car->SetRadioVolume(Radio); ShownRadio = Radio; }
    }
    // The interior lamp.
    {
        // On by its switch, or while a door is open, as the car's own was.
        const float Dome = bPower && (bDomeLight || Car->IsADoorOpen()) ? Supply : 0.0f;
        if (Dome != ShownDome)
        {
            if (DomeLight) DomeLight->SetVisibility(Dome > 0.0f);
            if (DomeLook) DomeLook->SetScalarParameterValue(TEXT("EmissiveStrength"), 6.0f * Dome);
            ShownDome = Dome;
        }
    }
    if (LabelLook)
    {
        // The displays and the labels on the ignition; brighter at night with the dial lamps.
        const float Labels = In.bIgnition ? (Car->AreLightsOn() ? 2.0f : 1.4f) * Supply : 0.0f;
        if (Labels != ShownLabels) { LabelLook->SetScalarParameterValue(TEXT("EmissiveStrength"), Labels); ShownLabels = Labels; }
    }
    if (WarningLook)
    {
        for (int32 Lamp = 0; Lamp < WarningCount; ++Lamp)
        {
            const float Value = (LitWarnings & (1u << Lamp)) ? 2.5f * Supply : 0.0f;
            if (Value == ShownWarn[Lamp]) continue;
            WarningLook->SetScalarParameterValue(*FString::Printf(TEXT("Warn%d"), Lamp), Value);
            ShownWarn[Lamp] = Value;
        }
    }
}

void URallyCabinComponent::UpdateLamps(bool bPower, double Charge, bool bLightsOn, bool bBrake, bool bReverse, bool bLeftFlash, bool bRightFlash)
{
    using namespace Rally::Cabin;
    ARallyCarPawn* Car = GetCar();
    const URallyPanelComponent* PanelState = Car ? Car->GetPanels() : nullptr;
    const float Supply = bPower ? static_cast<float>(SupplyScale(Charge)) : 0.0f;
    const bool bHigh = bLightsOn && bHighBeam;
    bBrakeLit = bPower && bBrake;
    for (FLampLook& Entry : LampLooks)
    {
        UMaterialInstanceDynamic* Look = Entry.Look.Get();
        if (!Look) continue;
        const bool bFlashing = Entry.Side < 0 ? bLeftFlash : Entry.Side > 0 ? bRightFlash : false;
        double Glow = 0.0;
        switch (Entry.Lamp)
        {
        case ELamp::LowBeam: Glow = bLightsOn ? HeadGlow : 0.0; break;
        case ELamp::HighBeam: Glow = bHigh ? HeadGlow : 0.0; break;
        case ELamp::Fog: Glow = 0.0; break;
        // The headlamp's lens glows over whichever beam is on, a little.
        case ELamp::HeadLens: Glow = bLightsOn ? (bHigh ? 2.4 : 1.5) : 0.0; break;
        case ELamp::FrontIndicator: case ELamp::SideRepeater: case ELamp::RearIndicator: Glow = bFlashing ? IndicatorGlow : 0.0; break;
        case ELamp::SideLens: Glow = bFlashing ? 4.0 : 0.0; break;
        case ELamp::Tail: Glow = bLightsOn ? TailGlow : 0.0; break;
        // The red lens over the tail and brake lamps: dim with the lamps on, bright under braking.
        case ELamp::TailLens: Glow = bBrakeLit ? 12.0 : bLightsOn ? 4.0 : 0.0; break;
        case ELamp::Brake: Glow = bBrakeLit ? BrakeGlow : 0.0; break;
        case ELamp::Reverse: Glow = bReverse ? HeadGlow : 0.0; break;
        case ELamp::Plate: Glow = bLightsOn ? TailGlow : 0.0; break;
        default: break;
        }
        // A lamp unit lying on the ground is dark.
        if (PanelState && PanelState->IsDetached(Entry.Panel)) Glow = 0.0;
        const float Value = static_cast<float>(Glow) * Supply;
        if (Value == Entry.Shown) continue;
        Look->SetScalarParameterValue(TEXT("EmissiveStrength"), Value);
        Entry.Shown = Value;
    }
    for (int32 Side = 0; Side < BrakeLights.Num(); ++Side)
    {
        const int32 Unit = Side == 0 ? TaillightLeft : TaillightRight;
        if (BrakeLights[Side]) BrakeLights[Side]->SetVisibility(bBrakeLit && !(PanelState && PanelState->IsDetached(Unit)));
    }
    if (ReverseLight) ReverseLight->SetVisibility(bPower && bReverse && !(PanelState && PanelState->IsDetached(TaillightLeft)));
    // The beams themselves are switched by the car (ARallyCarPawn::UpdateLights); here, how far they reach.
    const int32 Beam = bHigh ? 1 : 0;
    if (Beam != ShownBeam)
    {
        for (USpotLightComponent* Lamp : { BeamLeft.Get(), BeamRight.Get() })
        {
            if (!Lamp) continue;
            Lamp->SetIntensity(bHigh ? HighBeamIntensity : LowBeamIntensity);
            Lamp->SetAttenuationRadius(bHigh ? HighBeamReach : LowBeamReach);
            Lamp->SetOuterConeAngle(bHigh ? HighBeamCone : LowBeamCone);
            FRotator Aim = Lamp->GetRelativeRotation();
            Aim.Pitch = bHigh ? HighBeamPitch : LowBeamPitch;
            Lamp->SetRelativeRotation(Aim);
        }
        ShownBeam = Beam;
    }
}
