#include "Damage/RallyPanelComponent.h"
#include "Damage/EngineRules.h"
#include "Damage/UndersideRules.h"
#include "Damage/ImpactRules.h"
#include "Damage/RallyBodyDeformComponent.h"
#include "Damage/PartConditionRules.h"
#include "Damage/RallyDamageComponent.h"
#include "Damage/RallyDebrisActor.h"
#include "Damage/RallyEngineComponent.h"
#include "Chaos/Convex.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "Net/UnrealNetwork.h"
#include "Player/RallyCharacter.h"
#include "Game/EconomyRules.h"
#include "Game/RallyDayClock.h"
#include "Game/RallyTeamState.h"
#include "Vehicle/RallyCarPawn.h"

namespace
{
    const FVector AssemblyOffset(0, 0, -80);
    // The boot's load floor in the assembly frame (cm, ground at 0), measured off the body mesh: from the
    // tailgate lip to the rear seat backs and between the wheel arches, at 59 cm; the spare wheel's well
    // under it reaches down to the floor pan at 35 cm.
    constexpr double BootBackX = -212.0, BootFrontX = -142.0, BootHalfWidth = 44.0, BootFloorZ = 59.5;

    // A part laid flat: its longest side across the car, its shortest upright.
    FRotator LieFlat(const FBox& Local)
    {
        const FVector Size = Local.GetSize();
        int32 Long = 0, Short = 0;
        for (int32 A = 1; A < 3; ++A)
        {
            if (Size[A] > Size[Long]) Long = A;
            if (Size[A] < Size[Short]) Short = A;
        }
        if (Long == Short) return FRotator::ZeroRotator;
        const auto Target = [Long, Short](int32 A) {
            return A == Long ? FVector::YAxisVector : A == Short ? FVector::ZAxisVector : FVector::XAxisVector;
        };
        return FRotationMatrix::MakeFromXZ(Target(0), Target(2)).Rotator();
    }
    struct FMount
    {
        // Hinges (bumper clips) and latch; Axis opens the panel for positive angles, up to MaxAngle.
        FVector A, B, Latch, Axis;
        float MaxAngle, Mass;
        // Angle held when opened by hand.
        float OpenAngle;
        // Outer face direction when shut, and area (m^2) the wind presses on.
        FVector Normal;
        float Area;
        // Wind getting under a free leading edge (the bonnet's front) lifts it: share of dynamic pressure.
        float Lift;
        // With one hinge or clip gone, the panel droops about this axis through the survivor.
        FVector DroopAxis;
        float MaxDroop;
        // Left open, its door check, bonnet prop or boot struts hold it within this many degrees of its
        // open angle, up to this torque (N m); beyond either it swings free.
        float CheckRange, CheckTorque;
    };
    // Assembly frame; real hinge/clip locations, independent of the mesh's bounds-centre origin.
    const FMount Mounts[] = {
        {{85,-80,55},{85,-80,110},{-26,-80,80},{0,0,1}, 70,28, 65,{0,-1,0},1.0f,0,{0,1,0},20, 20,30},
        {{85,80,55},{85,80,110},{-26,80,80},{0,0,-1},70,28, 65,{0,1,0},1.0f,0,{0,1,0},20, 20,30},
        {{-22,-78,55},{-22,-78,110},{-130,-78,80},{0,0,1},70,24, 65,{0,-1,0},0.9f,0,{0,1,0},20, 20,25},
        {{-22,78,55},{-22,78,110},{-130,78,80},{0,0,-1},70,24, 65,{0,1,0},0.9f,0,{0,1,0},20, 20,25},
        {{101,-58,95},{101,58,95},{208,0,75},{0,-1,0},115,18, 55,{0,0,1},1.3f,0.6f,{1,0,0},12, 8,120},
        {{-122,-50,133},{-122,50,133},{-228,0,75},{0,1,0},75,24, 70,{0,0,1},1.0f,0.1f,{1,0,0},12, 40,140},
        {},
        {{197,-78,49},{197,78,49},{197,0,49},{0,0,0},0,8, 0,{1,0,0},0.4f,0,{1,0,0},35},
        {{-206,-77,46},{-206,77,46},{-206,0,46},{0,0,0},0,7, 0,{-1,0,0},0.4f,0,{1,0,0},35},
        // Front wings: bolted along the top edge, front and rear; hang from the other end when one goes.
        {{180,-70,88},{100,-78,90},{140,-74,89},{0,0,0},0,4, 0,{0,-1,0},0.5f,0,{0,1,0},25},
        {{180,70,88},{100,78,90},{140,74,89},{0,0,0},0,4, 0,{0,1,0},0.5f,0,{0,1,0},25},
        // Headlamp units, inner and outer clips; dangle on their wiring.
        {{200,-36,64},{198,-74,64},{199,-55,64},{0,0,0},0,2.5f, 0,{1,0,0},0.1f,0,{1,0,0},40},
        {{200,36,64},{198,74,64},{199,55,64},{0,0,0},0,2.5f, 0,{1,0,0},0.1f,0,{1,0,0},40},
        // Tail lamps, upper inner and lower outer fixings.
        {{-218,-52,92},{-218,-68,78},{-218,-60,85},{0,0,0},0,1, 0,{-1,0,0},0.06f,0,{1,0,0},40},
        {{-218,52,92},{-218,68,78},{-218,60,85},{0,0,0},0,1, 0,{-1,0,0},0.06f,0,{1,0,0},40}
    };
    constexpr double WheelMassKg = 20.0;
    constexpr double JackMassKg = 8.0;
    constexpr double WaterCanMassKg = 6.0;
    constexpr double OilBottleMassKg = 4.0;
    constexpr double ExhaustMassKg = 14.0;
    // The radiator's filler and the oil filler on the engine cover, assembly frame.
    const FVector RadiatorFiller(192.0, 0.0, 74.0);
    const FVector OilFiller(150.0, 0.0, 89.0);
    const TCHAR* const CornerNames[] = { TEXT("front left"), TEXT("front right"), TEXT("rear left"), TEXT("rear right") };

    // Physics substep for the hinges, s; restitution against the stops; hinge friction per unit inertia.
    constexpr double HingeStep = 1.0 / 240.0;
    constexpr double HingeBounce = 0.3;
    constexpr double HingeFriction = 1.5;
    // Prop, struts or door check holding a panel open, per unit inertia: about 3 Hz, half damped.
    constexpr double HoldStiffness = 400.0;
    constexpr double HoldDamping = 20.0;
    // A hand opens or shuts a panel at about this pace (degrees per second): well under a metre or two
    // per second at the free edge, too gentle to hurt anything it meets.
    constexpr double HandSwingSpeed = 90.0;
    // Stiffness of a door check, prop or strut, per unit inertia, up to its holding torque.
    constexpr double CheckStiffness = 200.0;
    // How long a hand keeps guiding a panel it is opening before letting go (s).
    constexpr double GuideSeconds = 1.5;
    bool ValidPanel(int32 I) { return I >= 0 && I < URallyPanelComponent::NumPanels && I != 6; }
    // Held by two clips or bolts and no latch: bumpers, wings and lamps. Never swing open.
    bool Clipped(int32 I) { return I >= URallyPanelComponent::FirstClipped && I < URallyPanelComponent::NumPanels; }
    const TCHAR* StageName(Rally::Panel::EStage S)
    {
        switch(S) {
        case Rally::Panel::EStage::Intact: return TEXT("intact");
        case Rally::Panel::EStage::Loose: return TEXT("loose");
        case Rally::Panel::EStage::Hanging: return TEXT("hanging");
        default: return TEXT("detached");
        }
    }
    // A thin plate swinging about an edge: 4/3 m r^2 from its centre's distance r to the axis (m).
    double PlateInertia(double Mass, const FVector& Centre, const FVector& Pivot, const FVector& Axis)
    {
        const FVector R = (Centre - Pivot) * 0.01;
        const FVector Across = R - Axis * FVector::DotProduct(R, Axis);
        return FMath::Max(4.0 / 3.0 * Mass * Across.SizeSquared(), 0.05);
    }
    // Damped spring towards Target for latched, strapped or held panels (degrees).
    void Spring(Rally::Panel::FHinge& H, double Target, double Dt)
    {
        double Remaining = FMath::Min(Dt, 0.1);
        while (Remaining > 0.0)
        {
            const double Step = FMath::Min(Remaining, 1.0 / 120.0);
            H.Speed += ((Target - H.Angle) * 35.0 - H.Speed * 9.0) * Step;
            H.Angle += H.Speed * Step;
            Remaining -= Step;
        }
    }
}

URallyPanelComponent::EGear URallyPanelComponent::GearKind(int32 Part)
{
    const int32 R = Part - FirstGear;
    return R < 4 ? EGear::Strut : R < 8 ? EGear::Arm : R < 12 ? EGear::Disc : R < 14 ? EGear::TieRod : EGear::Driveshaft;
}

int32 URallyPanelComponent::GearCorner(int32 Part)
{
    const int32 R = Part - FirstGear;
    return R < 12 ? R % 4 : (R - 12) % 2;
}

int32 URallyPanelComponent::GearSlot(EGear Kind, int32 Corner)
{
    if (Corner < 0 || Corner > 3) return INDEX_NONE;
    switch (Kind)
    {
    case EGear::Strut: return FirstGear + Corner;
    case EGear::Arm: return FirstGear + 4 + Corner;
    case EGear::Disc: return FirstGear + 8 + Corner;
    case EGear::TieRod: return Corner < 2 ? FirstGear + 12 + Corner : INDEX_NONE;
    default: return Corner < 2 ? FirstGear + 14 + Corner : INDEX_NONE;
    }
}

int32 URallyPanelComponent::SlotForKind(const FString& Kind)
{
    const FString K = Kind.ToLower();
    if (K == TEXT("strut")) return GearSlot(EGear::Strut, 0);
    if (K == TEXT("rearstrut")) return GearSlot(EGear::Strut, 2);
    if (K == TEXT("disc")) return GearSlot(EGear::Disc, 0);
    if (K == TEXT("reardisc")) return GearSlot(EGear::Disc, 2);
    if (K == TEXT("turbo")) return ModuleSlot(EModule::Turbo);
    if (K == TEXT("ecu")) return ModuleSlot(EModule::Ecu);
    if (K == TEXT("intercooler")) return ModuleSlot(EModule::Intercooler);
    if (K == TEXT("slivovica")) return DrinkSlot;
    if (K == TEXT("cigarettes")) return SmokeSlot;
    if (K == TEXT("firstaid")) return FirstAidSlot;
    return INDEX_NONE;
}

ERallyPart URallyPanelComponent::PartOfModule(int32 Slot)
{
    if (!IsModule(Slot)) return ERallyPart::Count;
    switch (ModuleKind(Slot))
    {
    case EModule::AirFilter: return ERallyPart::AirFilter;
    case EModule::Intake: return ERallyPart::Intake;
    case EModule::ExhaustManifold: return ERallyPart::ExhaustManifold;
    case EModule::Turbo: return ERallyPart::Turbo;
    case EModule::Intercooler: return ERallyPart::Intercooler;
    case EModule::Alternator: return ERallyPart::Alternator;
    case EModule::Starter: return ERallyPart::Starter;
    case EModule::Ecu: return ERallyPart::Ecu;
    default: return ERallyPart::Count;
    }
}

ERallyPart URallyPanelComponent::PartOfGear(int32 Slot) const
{
    const int32 C = GearCorner(Slot);
    const auto From = [C](ERallyPart First) { return static_cast<ERallyPart>(static_cast<int32>(First) + C); };
    switch (GearKind(Slot))
    {
    case EGear::Strut: return From(ERallyPart::StrutFrontLeft);
    case EGear::Arm: return From(ERallyPart::SuspensionFrontLeft);
    case EGear::Disc: return From(ERallyPart::BrakeFrontLeft);
    case EGear::TieRod: return From(ERallyPart::TieRodFrontLeft);
    default: return From(ERallyPart::DriveshaftFrontLeft);
    }
}

URallyPanelComponent::URallyPanelComponent()
{
    SetIsReplicatedByDefault(true);
    Conditions.SetNum(NumSlots);
    PrimaryComponentTick.bCanEverTick = false;
}

void URallyPanelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(URallyPanelComponent, Conditions);
    DOREPLIFETIME(URallyPanelComponent, JackCorner);
}

void URallyPanelComponent::Initialize(const TArray<TObjectPtr<UStaticMeshComponent>>& InMeshes,
    const TArray<TObjectPtr<UStaticMeshComponent>>& InWheels, const TArray<TObjectPtr<UStaticMeshComponent>>& InGear,
    UStaticMeshComponent* InExhaust, const TArray<TObjectPtr<UStaticMeshComponent>>& InModules, URallyBodyDeformComponent* InDeform)
{
    Meshes = InMeshes;
    Wheels = InWheels;
    Gear = InGear;
    Exhaust = InExhaust;
    Modules = InModules;
    Deform = InDeform;
    ModuleRest.Reset();
    for (UStaticMeshComponent* Mesh : Modules) ModuleRest.Add(Mesh->GetRelativeTransform());
    ModuleTilt.Init(0.0, Modules.Num());
    ModuleShake.Init(0.0, Modules.Num());
    Rest.Reset();
    for (UStaticMeshComponent* Mesh : Meshes) Rest.Add(Mesh->GetRelativeTransform());
    WearRemainder.Init(0, Meshes.Num());
    Swing.Init({}, Meshes.Num());
    Droop.Init({}, Meshes.Num());
    LastClear.Init(FVector2D::ZeroVector, Meshes.Num());
    HandTarget.Init(0.0, Meshes.Num());
    GuideTime.Init(0.0, Meshes.Num());
    WasLeftOpen.Init(false, Meshes.Num());
    // Points a panel can touch things with: the corners of its collision hull (set up for loose parts),
    // or of its bounding box if it has none.
    Probes.SetNum(Meshes.Num());
    for (int32 I = 0; I < Meshes.Num(); ++I)
    {
        const UStaticMesh* Mesh = Meshes[I]->GetStaticMesh();
        const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
        if (Body)
            for (const FKConvexElem& Hull : Body->AggGeom.ConvexElems)
                for (const FVector& Point : Hull.VertexData) Probes[I].Add(Hull.GetTransform().TransformPosition(Point));
        if (Probes[I].IsEmpty() && Mesh)
        {
            const FBox Box = Mesh->GetBoundingBox();
            for (int32 Corner = 0; Corner < 8; ++Corner)
                Probes[I].Add(FVector((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y, (Corner & 4) ? Box.Max.Z : Box.Min.Z));
        }
    }
    EdgeLever.Init(0.0, Meshes.Num());
    LastSwingHit.Init(-10.0, Meshes.Num());
    for (int32 I = 0; I < Meshes.Num(); ++I)
    {
        if (!ValidPanel(I) || Mounts[I].Axis.IsZero()) continue;
        const FVector Pivot = (Mounts[I].A + Mounts[I].B) * 0.5 + AssemblyOffset;
        for (const FVector& Point : Probes[I])
        {
            const FVector R = Rest[I].TransformPosition(Point) - Pivot;
            EdgeLever[I] = FMath::Max(EdgeLever[I], (R - Mounts[I].Axis * FVector::DotProduct(R, Mounts[I].Axis)).Size() * 0.01);
        }
    }
    BuildFixings();
    // What the boot kit weighs (SpawnSpares), known on every machine: the car's own mass includes it.
    BootKitKg = WheelMassKg + JackMassKg + 9 + 8 + 4 + 5 + 6 + 7 + 6 + WaterCanMassKg + OilBottleMassKg + 1.0;
    OnRep_Conditions();
    if (GetOwner()->HasAuthority()) SpawnSpares();
}

namespace
{
    using Rally::Fixing::EKind;
    using Rally::Fixing::ETool;
    // Body panel fixings, assembly frame (cm): two for each hinge or end, in the order of the condition's Out
    // bits (first hinge or end, then second), and the way each faces.
    struct FPanelFixing { EKind Kind; ETool Tool; FVector At; FVector Facing; };
    TArray<FPanelFixing> PanelFixings(int32 I)
    {
        const double S = I % 2 == 0 ? -1.0 : 1.0;
        switch (I)
        {
        case 0: case 1: case 2: case 3:
        {
            // Door hinge bolts on the door's front edge, reached with the door open.
            const double X = I < 2 ? 82.0 : -25.0, Y = (I < 2 ? 80.6 : 78.6) * S;
            return { { EKind::Bolt, ETool::Spanner13, {X, Y, 57.5}, {0, S, 0} }, { EKind::Bolt, ETool::Spanner13, {X, Y, 52.5}, {0, S, 0} },
                { EKind::Bolt, ETool::Spanner13, {X, Y, 112.5}, {0, S, 0} }, { EKind::Bolt, ETool::Spanner13, {X, Y, 107.5}, {0, S, 0} } };
        }
        case 4:
            // Bonnet hinge bolts, on its underside at the back edge: seen from in front with it open.
            return { { EKind::Bolt, ETool::Spanner10, {106, -58, 93.5}, {0, 0, -1} }, { EKind::Bolt, ETool::Spanner10, {100, -58, 93.5}, {0, 0, -1} },
                { EKind::Bolt, ETool::Spanner10, {106, 58, 93.5}, {0, 0, -1} }, { EKind::Bolt, ETool::Spanner10, {100, 58, 93.5}, {0, 0, -1} } };
        case 5:
            // Boot lid hinge bolts, on its underside at the top of the opening.
            return { { EKind::Bolt, ETool::Spanner10, {-119, -50, 132.4}, {0, 0, -1} }, { EKind::Bolt, ETool::Spanner10, {-125, -50, 132.4}, {0, 0, -1} },
                { EKind::Bolt, ETool::Spanner10, {-119, 50, 132.4}, {0, 0, -1} }, { EKind::Bolt, ETool::Spanner10, {-125, 50, 132.4}, {0, 0, -1} } };
        case 7: case 8:
        {
            // Bumpers: a bolt high on the face and a screw at the bottom corner, at each end.
            const double F = I == 7 ? 1.0 : -1.0, X = I == 7 ? 203.0 : -211.0, Z = I == 7 ? 56.0 : 53.0;
            return { { EKind::Bolt, ETool::Spanner10, {X, -66, Z}, {F, 0, 0} }, { EKind::Screw, ETool::Screwdriver, {X, -58, Z - 14}, {F, 0, 0} },
                { EKind::Bolt, ETool::Spanner10, {X, 66, Z}, {F, 0, 0} }, { EKind::Screw, ETool::Screwdriver, {X, 58, Z - 14}, {F, 0, 0} } };
        }
        case 9: case 10:
        {
            // Front wings: bolts along the top edge, under the bonnet; front pair, then rear pair.
            const double L = I == 9 ? -1.0 : 1.0;
            return { { EKind::Bolt, ETool::Spanner10, {186, 70 * L, 88.6}, {0, 0, 1} }, { EKind::Bolt, ETool::Spanner10, {174, 71 * L, 88.6}, {0, 0, 1} },
                { EKind::Bolt, ETool::Spanner10, {110, 77 * L, 90.6}, {0, 0, 1} }, { EKind::Bolt, ETool::Spanner10, {100, 78 * L, 90.6}, {0, 0, 1} } };
        }
        case 11: case 12:
        {
            // Headlamps: bolts on top, under the bonnet; inner pair, then outer pair.
            const double L = I == 11 ? -1.0 : 1.0;
            return { { EKind::Bolt, ETool::Spanner10, {196, 36 * L, 70}, {0, 0, 1} }, { EKind::Bolt, ETool::Spanner10, {196, 43 * L, 70}, {0, 0, 1} },
                { EKind::Bolt, ETool::Spanner10, {194, 74 * L, 70}, {0, 0, 1} }, { EKind::Bolt, ETool::Spanner10, {194, 67 * L, 70}, {0, 0, 1} } };
        }
        case 13: case 14:
        {
            // Tail lamps: nuts inside the boot; upper inner pair, then lower outer pair.
            const double L = I == 13 ? -1.0 : 1.0;
            return { { EKind::Nut, ETool::Spanner10, {-213, 52 * L, 94}, {1, 0, 0} }, { EKind::Nut, ETool::Spanner10, {-213, 52 * L, 90}, {1, 0, 0} },
                { EKind::Nut, ETool::Spanner10, {-213, 68 * L, 80}, {1, 0, 0} }, { EKind::Nut, ETool::Spanner10, {-213, 68 * L, 76}, {1, 0, 0} } };
        }
        default:
            return {};
        }
    }

    // Distance along a ray to a mesh's simple collision hulls, or -1 (below).
    double RayToHulls(const UStaticMeshComponent* Mesh, const FVector& Start, const FVector& Direction, double Length);

    // Head size of a fixing (cm across) by the tool that turns it.
    double HeadCm(ETool Tool)
    {
        switch (Tool)
        {
        case ETool::Spanner10: return 1.5;
        case ETool::Spanner13: return 1.9;
        case ETool::Spanner17: return 2.4;
        case ETool::Spanner19: return 2.7;
        default: return 1.3;
        }
    }
}

void URallyPanelComponent::BuildFixings()
{
    Spots.Reset();
    SpotStart.Init(0, NumSlots + 1);
    LooseCarry.Init(0.0, NumSlots);
    OfferAimDeg.Init(0.0, NumSlots);
    FitFirstTurn.Init(-10.0, NumSlots);
    FitLastTurn.Init(-10.0, NumSlots);
    FitTurns.Init(0, NumSlots);
    const auto Add = [&](EKind Kind, ETool Tool, const FVector& Local, const FVector& Facing) {
        Spots.Add({ Kind, Tool, Local, Facing.GetSafeNormal() });
    };
    for (int32 Slot = 0; Slot < NumSlots; ++Slot)
    {
        SpotStart[Slot] = Spots.Num();
        if (ValidPanel(Slot) && Rest.IsValidIndex(Slot))
        {
            const FTransform Car = GetOwner()->GetActorTransform();
            for (const FPanelFixing& F : PanelFixings(Slot))
            {
                FVector At = AssemblyOffset + F.At;
                // Bumpers, wings and lamps: sat on the part's surface where it faces (their shapes curve
                // away from the nominal points).
                if (Clipped(Slot))
                {
                    const FVector Start = Car.TransformPosition(At + F.Facing * 25.0);
                    const FVector Dir = Car.TransformVectorNoScale(-F.Facing);
                    const double Hit = RayToHulls(Meshes[Slot], Start, Dir, 50.0);
                    if (Hit >= 0.0) At = Car.InverseTransformPosition(Start + Dir * Hit) + F.Facing * 0.3;
                }
                Add(F.Kind, F.Tool, Rest[Slot].InverseTransformPosition(At), Rest[Slot].InverseTransformVectorNoScale(F.Facing));
            }
            continue;
        }
        const UStaticMeshComponent* Mesh = FixingParent(Slot);
        const UStaticMesh* StaticMesh = Mesh ? Mesh->GetStaticMesh() : nullptr;
        if (StaticMesh && IsModule(Slot))
        {
            // Engine modules are worked on from above with the bonnet open: their fixings sit on top,
            // spread along the longer side, each on the part's own surface.
            struct FModuleFixing { EKind Kind; ETool Tool; };
            TArray<FModuleFixing> List;
            switch (ModuleKind(Slot))
            {
            case EModule::Cover: List = { { EKind::Screw, ETool::Screwdriver }, { EKind::Screw, ETool::Screwdriver } }; break;
            case EModule::AirFilter: List = { { EKind::Clip, ETool::Hands }, { EKind::Clip, ETool::Hands } }; break;
            case EModule::Intake: List = { { EKind::Bolt, ETool::Spanner10 }, { EKind::Bolt, ETool::Spanner10 }, { EKind::Bolt, ETool::Spanner10 }, { EKind::Bolt, ETool::Spanner10 } }; break;
            case EModule::ExhaustManifold: List = { { EKind::Nut, ETool::Spanner13 }, { EKind::Nut, ETool::Spanner13 }, { EKind::Nut, ETool::Spanner13 }, { EKind::Nut, ETool::Spanner13 } }; break;
            case EModule::Turbo: List = { { EKind::Nut, ETool::Spanner13 }, { EKind::Nut, ETool::Spanner13 }, { EKind::Nut, ETool::Spanner13 } }; break;
            case EModule::Intercooler: List = { { EKind::Screw, ETool::Screwdriver }, { EKind::Bolt, ETool::Spanner10 }, { EKind::Bolt, ETool::Spanner10 }, { EKind::Screw, ETool::Screwdriver } }; break;
            case EModule::Alternator: List = { { EKind::Bolt, ETool::Spanner13 }, { EKind::Nut, ETool::Spanner10 }, { EKind::Bolt, ETool::Spanner13 } }; break;
            case EModule::Starter: List = { { EKind::Bolt, ETool::Spanner13 }, { EKind::Nut, ETool::Spanner10 }, { EKind::Bolt, ETool::Spanner13 } }; break;
            default: List = { { EKind::Bolt, ETool::Spanner10 }, { EKind::Clip, ETool::Hands }, { EKind::Bolt, ETool::Spanner10 } }; break;
            }
            const FBox Box = StaticMesh->GetBoundingBox();
            const FVector C = Box.GetCenter(), E = Box.GetExtent();
            const bool bAlongX = E.X >= E.Y;
            const FTransform World = Mesh->GetComponentTransform();
            for (int32 F = 0; F < List.Num(); ++F)
            {
                const double T = List.Num() > 1 ? -0.7 + 1.4 * F / (List.Num() - 1) : 0.0;
                FVector Top(C.X + (bAlongX ? T * E.X : 0.0), C.Y + (bAlongX ? 0.0 : T * E.Y), Box.Max.Z);
                const FVector Start = World.TransformPosition(Top + FVector(0, 0, 20));
                const FVector Down = World.TransformVectorNoScale(FVector(0, 0, -1));
                const double Hit = RayToHulls(Mesh, Start, Down, 60.0);
                if (Hit >= 0.0) Top = World.InverseTransformPosition(Start + Down * Hit) + FVector(0, 0, 0.3);
                Add(List[F].Kind, List[F].Tool, Top, FVector(0, 0, 1));
            }
            continue;
        }
        if (!StaticMesh || (!IsWheel(Slot) && !IsGear(Slot))) continue;
        // From the part's own box, in its mesh frame (car axes at rest): the outer side faces out of the car.
        const FBox Box = StaticMesh->GetBoundingBox();
        const FVector C = Box.GetCenter(), E = Box.GetExtent();
        const int32 Corner = IsWheel(Slot) ? Slot - FirstWheel : GearCorner(Slot);
        const double S = Corner % 2 == 0 ? -1.0 : 1.0;
        const FVector Side(0, S, 0);
        if (IsWheel(Slot))
        {
            // Four 17 mm wheel bolts on a 10 cm circle, on the rim's face: 1.3 cm in from the tyre's wall
            // (measured off the wheel mesh). They used to sit 3 cm behind it, out of sight.
            const double Face = C.Y + S * (E.Y - 1.2);
            for (int32 B = 0; B < 4; ++B)
            {
                const double A = FMath::DegreesToRadians(45.0 + 90.0 * B);
                Add(EKind::Bolt, ETool::Spanner17, FVector(C.X + 5.0 * FMath::Cos(A), Face, C.Z + 5.0 * FMath::Sin(A)), Side);
            }
            continue;
        }
        const double Outer = C.Y + S * E.Y;
        switch (GearKind(Slot))
        {
        case EGear::Strut:
            // Two 19 mm bolts into the hub carrier at the foot, a 13 mm brake hose bracket halfway up.
            Add(EKind::Bolt, ETool::Spanner19, FVector(C.X - 2, Outer, Box.Min.Z + 7), Side);
            Add(EKind::Bolt, ETool::Spanner19, FVector(C.X + 2, Outer, Box.Min.Z + 14), Side);
            Add(EKind::Bolt, ETool::Spanner13, FVector(C.X, Outer, C.Z + 4), Side);
            break;
        case EGear::Arm:
            if (Corner < 2)
            {
                // Front lower arm: the ball joint nut at the outer end, the pivot bolt through the inner bush,
                // its head forward where it can be seen from in front of the wheel arch.
                Add(EKind::Nut, ETool::Spanner17, FVector(C.X, C.Y + S * (E.Y - 3), C.Z + E.Z), FVector(0, 0, 1));
                Add(EKind::Bolt, ETool::Spanner17, FVector(Box.Max.X, C.Y - S * (E.Y - 3), C.Z), FVector(1, 0, 0));
            }
            else
            {
                // Rear trailing arm: a pivot bolt at the front, the hub bolt at the back.
                Add(EKind::Bolt, ETool::Spanner17, FVector(Box.Max.X - 3, Outer, C.Z), Side);
                Add(EKind::Bolt, ETool::Spanner17, FVector(Box.Min.X + 4, Outer, C.Z), Side);
            }
            break;
        case EGear::Disc:
        {
            // Two 13 mm caliper carrier bolts at the top, a retaining screw in the hat.
            const double R = E.Z;
            Add(EKind::Bolt, ETool::Spanner13, FVector(C.X - 0.35 * R, Outer, C.Z + 0.6 * R), Side);
            Add(EKind::Bolt, ETool::Spanner13, FVector(C.X + 0.35 * R, Outer, C.Z + 0.6 * R), Side);
            Add(EKind::Screw, ETool::Screwdriver, FVector(C.X + 0.15 * R, Outer, C.Z - 0.15 * R), Side);
            break;
        }
        case EGear::TieRod:
            // Part of the front arm now (IsRetired): no fixings of its own.
            break;
        default:
        {
            // Driveshaft: the 19 mm hub nut in the middle of the hub's face, just proud of the brake disc
            // (it used to sit inside the hub, out of sight), two 13 mm joint bolts at the gearbox end.
            FVector HubNut(C.X, Outer, C.Z);
            const int32 DiscIndex = GearSlot(EGear::Disc, Corner) - FirstGear;
            const UStaticMeshComponent* DiscMesh = Gear.IsValidIndex(DiscIndex) ? Gear[DiscIndex].Get() : nullptr;
            if (DiscMesh && DiscMesh->GetStaticMesh())
            {
                const FBox DiscBox = DiscMesh->GetStaticMesh()->GetBoundingBox();
                const FVector DiscFace(DiscBox.GetCenter().X, S > 0.0 ? DiscBox.Max.Y : DiscBox.Min.Y, DiscBox.GetCenter().Z);
                HubNut = Mesh->GetRelativeTransform().InverseTransformPosition(DiscMesh->GetRelativeTransform().TransformPosition(DiscFace)) + Side * 0.5;
            }
            Add(EKind::Nut, ETool::Spanner19, HubNut, Side);
            Add(EKind::Bolt, ETool::Spanner13, FVector(C.X - 3, C.Y - S * (E.Y - 2), C.Z - E.Z), FVector(0, 0, -1));
            Add(EKind::Bolt, ETool::Spanner13, FVector(C.X + 3, C.Y - S * (E.Y - 2), C.Z - E.Z), FVector(0, 0, -1));
            break;
        }
        }
    }
    SpotStart[NumSlots] = Spots.Num();
    // A small steel head for each, riding on its part.
    for (UStaticMeshComponent* Old : FixingMeshes) if (Old) Old->DestroyComponent();
    FixingMeshes.Reset();
    UStaticMesh* Head = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UMaterialInterface* Steel = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/M_Prototype_Mechanical.M_Prototype_Mechanical"));
    FixingSteel = Steel;
    // What the player is aiming at glows (SetAimHighlight).
    if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/M_Prototype_Glow.M_Prototype_Glow")))
    {
        GlowAimed = UMaterialInstanceDynamic::Create(Glow, this);
        GlowAimed->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.8f, 0.1f));
        GlowAimed->SetScalarParameterValue(TEXT("Strength"), 3.0f);
        GlowPart = UMaterialInstanceDynamic::Create(Glow, this);
        GlowPart->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.4f, 0.05f));
        GlowPart->SetScalarParameterValue(TEXT("Strength"), 1.0f);
    }
    LitSlot = LitFixing = INDEX_NONE;
    for (int32 Slot = 0; Slot < NumSlots; ++Slot)
    {
        UStaticMeshComponent* Parent = FixingParent(Slot);
        for (int32 F = 0; F < NumFixings(Slot); ++F)
        {
            UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(GetOwner());
            Mesh->SetStaticMesh(Head);
            if (Steel) Mesh->SetMaterial(0, Steel);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetGenerateOverlapEvents(false);
            Mesh->SetCastShadow(false);
            Mesh->SetupAttachment(Parent);
            Mesh->RegisterComponent();
            FixingMeshes.Add(Mesh);
        }
    }
    UpdateFixingVisuals();
}

UStaticMeshComponent* URallyPanelComponent::FixingParent(int32 Slot) const
{
    if (ValidPanel(Slot)) return Meshes.IsValidIndex(Slot) ? Meshes[Slot].Get() : nullptr;
    if (IsWheel(Slot)) return Wheels.IsValidIndex(Slot - FirstWheel) ? Wheels[Slot - FirstWheel].Get() : nullptr;
    if (IsGear(Slot)) return Gear.IsValidIndex(Slot - FirstGear) ? Gear[Slot - FirstGear].Get() : nullptr;
    if (IsModule(Slot)) return Modules.IsValidIndex(Slot - FirstModule) ? Modules[Slot - FirstModule].Get() : nullptr;
    return nullptr;
}

bool URallyPanelComponent::FixingsPresent(int32 Slot) const
{
    if (ValidPanel(Slot)) return !IsDetached(Slot) && Conditions.IsValidIndex(Slot) && Conditions[Slot].Strap == 0;
    if (IsWheel(Slot)) return WheelOn(Slot);
    if (IsGear(Slot) || IsModule(Slot)) return !IsRemoved(Slot);
    return false;
}

void URallyPanelComponent::UpdateFixingVisuals()
{
    if (SpotStart.Num() != NumSlots + 1 || FixingMeshes.Num() != Spots.Num()) return;
    for (int32 Slot = 0; Slot < NumSlots; ++Slot)
    {
        const bool bThere = FixingsPresent(Slot);
        for (int32 F = 0; F < NumFixings(Slot); ++F)
        {
            const FFixingSpot& Spot = Spots[SpotStart[Slot] + F];
            UStaticMeshComponent* Mesh = FixingMeshes[SpotStart[Slot] + F];
            Mesh->SetVisibility(bThere);
            if (!bThere) continue;
            // Home it sits flush; out it stands proud on its thread, waiting for a spanner.
            const bool bOut = IsFixingOut(Slot, F);
            const double Head = HeadCm(Spot.Tool);
            const double Length = bOut ? 2.4 : 0.8;
            Mesh->SetRelativeLocationAndRotation(Spot.Local + Spot.Facing * (bOut ? Rally::Fixing::OutStandCm : 0.0) + Spot.Facing * (Length * 0.5 - 0.3),
                FRotationMatrix::MakeFromZ(Spot.Facing).Rotator());
            // The part in view shows where its fixings are: lit and drawn bigger than life (a head is barely a
            // centimetre), the one in the crosshair brightest and biggest.
            const bool bAimed = Slot == LitSlot && F == LitFixing;
            const double Grow = bAimed ? 2.2 : Slot == LitSlot ? 1.8 : 1.0;
            Mesh->SetRelativeScale3D(FVector(Head / 100.0 * Grow, Head / 100.0 * Grow, Length / 100.0 * Grow));
            UMaterialInterface* Look = bAimed && GlowAimed ? GlowAimed.Get() : Slot == LitSlot && GlowPart ? GlowPart.Get() : FixingSteel.Get();
            if (Look && Mesh->GetMaterial(0) != Look) Mesh->SetMaterial(0, Look);
        }
    }
}

void URallyPanelComponent::SetAimHighlight(int32 Slot, int32 Fixing)
{
    if (Slot != INDEX_NONE && (NumFixings(Slot) == 0 || !FixingsPresent(Slot))) Slot = Fixing = INDEX_NONE;
    if (Slot == LitSlot && Fixing == LitFixing) return;
    LitSlot = Slot;
    LitFixing = Slot == INDEX_NONE ? INDEX_NONE : Fixing;
    UpdateFixingVisuals();
}

FString URallyPanelComponent::SlotLabel(int32 Part) const
{
    const char* Grade = Rally::Grade::GradeName(GetGrade(Part));
    return *Grade ? FString::Printf(TEXT("%s %s"), UTF8_TO_TCHAR(Grade), *PanelName(Part)) : PanelName(Part);
}

FString URallyPanelComponent::GradedName(int32 Part, bool bSpare, uint8 Grade)
{
    const char* Name = Rally::Grade::GradeName(static_cast<Rally::Grade::EGrade>(FMath::Min<uint8>(Grade, 3)));
    FString Base = CarriedName(Part, bSpare);
    if (!*Name) return Base;
    // "spare front strut" reads "spare rally front strut".
    if (Base.StartsWith(TEXT("spare "))) return FString::Printf(TEXT("spare %s %s"), UTF8_TO_TCHAR(Name), *Base.RightChop(6));
    return FString::Printf(TEXT("%s %s"), UTF8_TO_TCHAR(Name), *Base);
}

FString URallyPanelComponent::AdjustPrompt(const ARallyCharacter* Character, int32 Part) const
{
    using Rally::Fixing::ETool;
    if (!Character || !FixingsPresent(Part) || !CanReach(Character, Part) || !FixingAccess(Part).IsEmpty()) return FString();
    const ETool Tool = Character->GetHeldTool();
    if (IsGear(Part) && GearKind(Part) == EGear::Strut && GetGrade(Part) == Rally::Grade::EGrade::Rally)
    {
        if (Tool == ETool::Screwdriver)
            return FString::Printf(TEXT("T / Shift+T: damper %d of %d"), GetClicks(Part), Rally::Grade::MaxClicks);
        if (Tool == ETool::Spanner19)
            return FString::Printf(TEXT("T / Shift+T: height collar %+d cm"), GetCollarCm(Part));
        return TEXT("adjustable: damper (screwdriver), height collar (19 mm)");
    }
    if (IsModule(Part) && ModuleKind(Part) == EModule::Turbo)
    {
        // More than the engine computer's fuel map allows and it pinks under load, wearing the engine.
        const int32 EcuSlot = ModuleSlot(EModule::Ecu);
        const double Safe = Rally::Grade::SafeBoost(GetGrade(EcuSlot)) + Rally::Grade::EngineMap(GetEngineMap(EcuSlot)).SafeBoost;
        const FString Limit = GetBoostTenths(Part) * 0.1 > Safe + 0.001
            ? FString::Printf(TEXT(" - over the %.1f bar the engine computer allows: it will pink"), Safe) : FString();
        if (Tool == ETool::Spanner10)
            return FString::Printf(TEXT("T / Shift+T: wastegate boost %.1f bar%s"), GetBoostTenths(Part) * 0.1, *Limit);
        return FString::Printf(TEXT("boost set at the wastegate (10 mm)%s"), *Limit);
    }
    if (IsModule(Part) && ModuleKind(Part) == EModule::Ecu)
    {
        if (Tool == ETool::Screwdriver)
            return FString::Printf(TEXT("T / Shift+T: map %s"), UTF8_TO_TCHAR(Rally::Grade::EngineMap(GetEngineMap(Part)).Name));
        return TEXT("map chosen with a screwdriver");
    }
    return FString();
}

bool URallyPanelComponent::Adjust(ARallyCharacter* Character, int32 I, int32 Step)
{
    using Rally::Fixing::ETool;
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    if (!GetOwner()->HasAuthority() || !Car || !Character || Character->GetCarriedPart() || (Step != 1 && Step != -1)
        || !FixingsPresent(I) || !CanReach(Character, I) || !FixingAccess(I).IsEmpty() || GetOwner()->GetVelocity().Size() > 100.0f) return false;
    const ETool Tool = Character->GetHeldTool();
    FRallyPanelCondition& State = Conditions[I];
    if (IsGear(I) && GearKind(I) == EGear::Strut && GetGrade(I) == Rally::Grade::EGrade::Rally && Tool == ETool::Screwdriver)
    {
        const int32 Clicks = FMath::Clamp(GetClicks(I) + Step, 1, Rally::Grade::MaxClicks);
        if (Clicks == GetClicks(I) || !Character->AdvanceFixingTurn()) return false;
        State.SetA = static_cast<uint8>(Clicks);
        UE_LOG(LogTemp, Display, TEXT("RALLY_TUNE %s damper %d of %d"), *PanelName(I), Clicks, Rally::Grade::MaxClicks);
    }
    else if (IsGear(I) && GearKind(I) == EGear::Strut && GetGrade(I) == Rally::Grade::EGrade::Rally && Tool == ETool::Spanner19)
    {
        const int32 Collar = FMath::Clamp(GetCollarCm(I) + Step, -Rally::Grade::MaxCollarCm, Rally::Grade::MaxCollarCm);
        if (Collar == GetCollarCm(I) || !Character->AdvanceFixingTurn()) return false;
        State.SetB = static_cast<int8>(Collar);
        UE_LOG(LogTemp, Display, TEXT("RALLY_TUNE %s height collar %+d cm"), *PanelName(I), Collar);
    }
    else if (IsModule(I) && ModuleKind(I) == EModule::Ecu && Tool == ETool::Screwdriver)
    {
        const int32 Map = FMath::Clamp(GetEngineMap(I) + Step, 1, 3);
        if (Map == GetEngineMap(I) || !Character->AdvanceFixingTurn()) return false;
        State.SetA = static_cast<uint8>(Map);
        UE_LOG(LogTemp, Display, TEXT("RALLY_TUNE engine map %s"), UTF8_TO_TCHAR(Rally::Grade::EngineMap(Map).Name));
    }
    else if (IsModule(I) && ModuleKind(I) == EModule::Turbo && Tool == ETool::Spanner10)
    {
        const int32 Boost = FMath::Clamp(GetBoostTenths(I) + Step, Rally::Grade::MinBoostTenths, Rally::Grade::MaxBoostTenths);
        if (Boost == GetBoostTenths(I) || !Character->AdvanceFixingTurn()) return false;
        State.SetA = static_cast<uint8>(Boost);
        UE_LOG(LogTemp, Display, TEXT("RALLY_TUNE turbo boost %.1f bar"), Boost * 0.1);
    }
    else return false;
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
    return true;
}

TArray<FRallyPanelCondition> URallyPanelComponent::SaveConditions() const
{
    TArray<FRallyPanelCondition> Saved = Conditions;
    for (FRallyPanelCondition& State : Saved) State.Debris = nullptr;
    return Saved;
}

TArray<FRallySavedPart> URallyPanelComponent::SaveParts() const
{
    TArray<FRallySavedPart> Saved;
    for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
    {
        if (It->GetSourceCar() != GetOwner() || It->IsActorBeingDestroyed()) continue;
        FRallySavedPart Part = It->SaveState();
        Part.bLinked = Conditions.IsValidIndex(Part.Slot) && Conditions[Part.Slot].Debris == *It;
        Saved.Add(Part);
    }
    return Saved;
}

void URallyPanelComponent::Restore(const TArray<FRallyPanelCondition>& Saved, int8 InJackCorner, const TArray<FRallySavedPart>& Parts)
{
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    if (!GetOwner()->HasAuthority() || !Car) return;
    for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
        if (It->GetSourceCar() == GetOwner()) It->Destroy();
    for (int32 I = 0; I < Conditions.Num(); ++I)
    {
        // A tie rod taken off in an older save is back on its arm.
        if (Saved.IsValidIndex(I) && !IsRetired(I)) Conditions[I] = Saved[I];
        Conditions[I].Debris = nullptr;
        if (WearRemainder.IsValidIndex(I)) WearRemainder[I] = 0;
        if (Swing.IsValidIndex(I)) Swing[I] = Droop[I] = {};
        if (LastClear.IsValidIndex(I)) LastClear[I] = FVector2D::ZeroVector;
        if (HandTarget.IsValidIndex(I)) HandTarget[I] = GuideTime[I] = 0.0;
    }
    JackCorner = InJackCorner >= -1 && InJackCorner <= 3 ? InJackCorner : -1;
    UStaticMesh* JackMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Jack.SM_Octavia_Jack"));
    UStaticMesh* CanMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_WaterCan.SM_Octavia_WaterCan"));
    UStaticMesh* BottleMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_OilBottle.SM_Octavia_OilBottle"));
    UStaticMesh* AidMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_FirstAid.SM_Octavia_FirstAid"));
    for (const FRallySavedPart& Saved : Parts)
    {
        if (IsRetired(Saved.Slot)) continue;
        UStaticMesh* Mesh = nullptr;
        if (Saved.Slot == JackSlot) Mesh = JackMesh;
        else if (Saved.Slot == FirstAidSlot) Mesh = AidMesh;
        else if (Saved.Slot == DrinkSlot || Saved.Slot == SmokeSlot) Mesh = MeshForSlot(Saved.Slot);
        else if (Saved.Slot == WaterSlot) Mesh = CanMesh;
        else if (Saved.Slot == OilBottleSlot) Mesh = BottleMesh;
        else if (Saved.Slot == ExhaustSlot) Mesh = Exhaust ? Exhaust->GetStaticMesh().Get() : nullptr;
        else if (const UStaticMeshComponent* Parent = FixingParent(Saved.Slot)) Mesh = Parent->GetStaticMesh().Get();
        if (!Mesh) continue;
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ARallyDebrisActor* Part = GetWorld()->SpawnActor<ARallyDebrisActor>(ARallyDebrisActor::StaticClass(), Saved.Transform, Params);
        if (!Part) continue;
        FRallyDebrisSource Source;
        Source.Car = Car; Source.Mesh = Mesh; Source.Panel = Saved.Slot; Source.MeshToCar = Saved.MeshToCar;
        Source.MassKg = Saved.MassKg; Source.Health = Saved.Health; Source.bSpare = Saved.bSpare; Source.Grade = Saved.Grade;
        Part->Launch(Source, Saved.Dents, FVector::ZeroVector, FVector::ZeroVector);
        if (Saved.Mount != static_cast<uint8>(ERallyPartMount::Loose))
            Part->Mount(static_cast<ERallyPartMount>(Saved.Mount), Saved.MountLocation, Saved.MountRotation);
        if (Saved.bLinked && Conditions.IsValidIndex(Saved.Slot)) Conditions[Saved.Slot].Debris = Part;
        if (Saved.Slot == JackSlot && Saved.Mount == static_cast<uint8>(ERallyPartMount::UnderCar)) Conditions[JackSlot].Debris = Part;
    }
    // The boot packed afresh: saves from before 2026-10-19 laid the kit by each part's pivot, under the floor.
    // Everything comes out first, then a spare wheel goes in its well and the rest on the floor.
    TArray<ARallyDebrisActor*> Stowed;
    for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
        if (It->GetSourceCar() == GetOwner() && It->GetMount() == ERallyPartMount::Stowed && *It != Conditions[JackSlot].Debris) Stowed.Add(*It);
    TArray<FTransform> Was;
    for (ARallyDebrisActor* Part : Stowed)
    {
        Was.Add(FTransform(Part->GetMountRotation(), Part->GetMountLocation()));
        Part->Mount(ERallyPartMount::Stowed, FVector(0.0, 0.0, -10000.0), FRotator::ZeroRotator);
    }
    bool bWellTaken = false;
    for (int32 I = 0; I < Stowed.Num(); ++I)
    {
        ARallyDebrisActor* Part = Stowed[I];
        FVector At = AssemblyOffset + FVector(-187.0, 0.0, 47.0);
        FRotator Turn(0.0, 0.0, 90.0);
        if (IsWheel(Part->GetPanel()) && !bWellTaken) bWellTaken = true;
        // No room (an overfull boot): it stays where it was.
        else if (!BootPlace(Part->GetSourceMesh(), Part, At, Turn)) { At = Was[I].GetLocation(); Turn = Was[I].Rotator(); }
        Part->Mount(ERallyPartMount::Stowed, At, Turn);
    }
    for (int32 I = 0; I < Meshes.Num(); ++I)
        if (Rest.IsValidIndex(I) && !IsDetached(I)) Meshes[I]->SetRelativeTransform(PanelPose(I, 0.0, 0.0));
    OnRep_Jack();
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
}

void URallyPanelComponent::DebugSetOpen(int32 Panel, bool bOpen)
{
    if (!GetOwner()->HasAuthority() || !Conditions.IsValidIndex(Panel)) return;
    Conditions[Panel].bOpen = bOpen;
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
}

void URallyPanelComponent::DebugSetGrade(int32 Part, uint8 Grade, uint8 SetA, int8 SetB)
{
    if (!GetOwner()->HasAuthority() || !Conditions.IsValidIndex(Part)) return;
    Conditions[Part].Grade = FMath::Min<uint8>(Grade, 3);
    Conditions[Part].SetA = SetA;
    Conditions[Part].SetB = SetB;
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
}

double URallyPanelComponent::GetBootLoadKg() const
{
    double Kg = 0.0;
    for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
        if (It->GetSourceCar() == GetOwner() && It->GetMount() == ERallyPartMount::Stowed && !It->IsActorBeingDestroyed()) Kg += It->GetMassKg();
    return Kg;
}

bool URallyPanelComponent::CanStow(const ARallyCharacter* Character, const ARallyDebrisActor* Part) const
{
    if (!Part || Part->GetSourceCar() != GetOwner() || Part->GetPanel() == JackSlot && JackCorner >= 0) return false;
    if (!IsOpen(5) && !IsDetached(5)) return false;
    if (Character && FVector::Dist(Character->GetPawnViewLocation(),
        GetOwner()->GetActorTransform().TransformPosition(AssemblyOffset + FVector(-180, 0, 60))) > ReachCm) return false;
    return GetBootLoadKg() + Part->GetMassKg() <= BootCapacityKg;
}

bool URallyPanelComponent::Stow(ARallyCharacter* Character, ARallyDebrisActor* Part)
{
    if (!GetOwner()->HasAuthority() || !CanStow(Character, Part)) return false;
    // Laid flat in the first free place.
    FVector At;
    FRotator Turn;
    if (!BootPlace(Part->GetSourceMesh(), Part, At, Turn))
    {
        UE_LOG(LogTemp, Display, TEXT("RALLY_BOOT no room for %s"), *GradedName(Part->GetPanel(), Part->IsSpare(), Part->GetGrade()));
        return false;
    }
    if (Character && Part->GetCarrier() == Character) Part->Drop();
    for (FRallyPanelCondition& Slot : Conditions)
        if (Slot.Debris == Part) Slot.Debris = nullptr;
    Part->Mount(ERallyPartMount::Stowed, At, Turn);
    UE_LOG(LogTemp, Display, TEXT("RALLY_BOOT stowed %s (%.0f kg), boot %.0f of %.0f kg"), *GradedName(Part->GetPanel(), Part->IsSpare(), Part->GetGrade()),
        Part->GetMassKg(), GetBootLoadKg(), BootCapacityKg);
    return true;
}

bool URallyPanelComponent::BootPlace(const UStaticMesh* Mesh, const ARallyDebrisActor* Ignore, FVector& OutLocation, FRotator& OutTurn) const
{
    if (!Mesh || !GetWorld()) return false;
    const FBox Local = Mesh->GetBoundingBox();
    OutTurn = LieFlat(Local);
    const FBox Turned = Local.TransformBy(FTransform(OutTurn));
    const FVector Size = Turned.GetSize();
    if (Size.X > BootFrontX - BootBackX || Size.Y > 2.0 * BootHalfWidth) return false;
    // What is in there already, in the assembly frame, a little shrunk so neighbours may touch.
    TArray<FBox> Taken;
    for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
    {
        if (*It == Ignore || It->GetSourceCar() != GetOwner() || It->GetMount() != ERallyPartMount::Stowed || !It->GetSourceMesh()) continue;
        Taken.Add(It->GetSourceMesh()->GetBoundingBox().TransformBy(FTransform(It->GetMountRotation(), It->GetMountLocation() - AssemblyOffset)).ExpandBy(-0.5));
    }
    // Lowest first, then from the back, then from the left, in 2 cm steps.
    for (double Z = BootFloorZ; Z <= BootFloorZ + 40.0; Z += 2.0)
        for (double X = BootBackX; X + Size.X <= BootFrontX; X += 2.0)
            for (double Y = -BootHalfWidth; Y + Size.Y <= BootHalfWidth; Y += 2.0)
            {
                const FBox Candidate(FVector(X, Y, Z), FVector(X, Y, Z) + Size);
                if (Taken.ContainsByPredicate([&Candidate](const FBox& Box) { return Box.Intersect(Candidate); })) continue;
                OutLocation = AssemblyOffset + FVector(X, Y, Z) - Turned.Min;
                return true;
            }
    return false;
}

int32 URallyPanelComponent::WorkshopJob(int32 I) const
{
    using Rally::Economy::EJob;
    const ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    const ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
    if (!Car || !Team || !Team->InServiceYard(Car)) return -1;
    const auto Worn = [Car](ERallyPart Part) {
        FRallyPartCondition Condition;
        Car->GetDamage()->GetPartCondition(Part, Condition);
        return Condition.Health < 1.0f || Condition.bBodged;
    };
    if (I == RadiatorSlot && IsBayOpen() && Worn(ERallyPart::Radiator)) return static_cast<int32>(EJob::Radiator);
    if (I == BatterySlot && Worn(ERallyPart::Battery)) return static_cast<int32>(EJob::Battery);
    if (I == OilFillerSlot && Worn(ERallyPart::Engine)) return static_cast<int32>(EJob::Engine);
    if (I == SumpSlot && Worn(ERallyPart::OilSump)) return static_cast<int32>(EJob::Sump);
    if (I == TankSlot && Worn(ERallyPart::FuelTank)) return static_cast<int32>(EJob::Tank);
    const bool bThirsty = Car->GetEngine() && Car->GetEngine()->GetFuelL() < Rally::Underside::FuelCapacityL - 2.0;
    if (I == TankSlot && bThirsty) return static_cast<int32>(EJob::Fuel);
    if (I == ExhaustSlot && !IsRemoved(I) && Worn(ERallyPart::Exhaust)) return static_cast<int32>(EJob::Exhaust);
    if (I == GearboxSlot && Worn(ERallyPart::Gearbox)) return static_cast<int32>(EJob::Gearbox);
    // The body: dents, bent hinges, a jammed door.
    if (ValidPanel(I) && !IsDetached(I))
    {
        bool bBent = Deform && Deform->GetDents().Num() > 0;
        for (int32 Panel = 0; Panel < NumPanels && !bBent; ++Panel)
            if (ValidPanel(Panel) && !IsDetached(Panel) && (Conditions[Panel].HingeA < 255 || Conditions[Panel].HingeB < 255
                || (!Clipped(Panel) && Conditions[Panel].Latch < 255) || Conditions[Panel].bJammed)) bBent = true;
        if (bBent) return static_cast<int32>(EJob::Body);
        if (bThirsty) return static_cast<int32>(EJob::Fuel);
    }
    return -1;
}

FString URallyPanelComponent::WorkshopPrompt(int32 I) const
{
    const int32 Kind = WorkshopJob(I);
    if (Kind < 0) return FString();
    const Rally::Economy::FJob Job = Rally::Economy::Job(static_cast<Rally::Economy::EJob>(Kind));
    if (static_cast<Rally::Economy::EJob>(Kind) == Rally::Economy::EJob::Fuel)
    {
        const ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
        const double Litres = Rally::Underside::FuelCapacityL - Car->GetEngine()->GetFuelL();
        return FString::Printf(TEXT("P: fill the tank, %.0f L, %s"), Litres,
            UTF8_TO_TCHAR(Rally::Economy::Money(FMath::CeilToInt(Litres) * Rally::Economy::FuelSkPerLitre).c_str()));
    }
    return FString::Printf(TEXT("P: workshop: %s, %s, %.1f h"), UTF8_TO_TCHAR(Job.Name), UTF8_TO_TCHAR(Rally::Economy::Money(Job.PriceSk).c_str()), Job.Hours);
}

bool URallyPanelComponent::WorkshopRepair(ARallyCharacter* Character, int32 I)
{
    using Rally::Economy::EJob;
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
    ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
    const int32 Kind = WorkshopJob(I);
    if (!GetOwner()->HasAuthority() || !Car || !Team || Kind < 0 || (Character && !CanReach(Character, I))) return false;
    Rally::Economy::FJob Job = Rally::Economy::Job(static_cast<EJob>(Kind));
    if (static_cast<EJob>(Kind) == EJob::Fuel)
        Job.PriceSk = FMath::CeilToInt(Rally::Underside::FuelCapacityL - Car->GetEngine()->GetFuelL()) * Rally::Economy::FuelSkPerLitre;
    if (Team->GetKittySk() < Job.PriceSk)
    {
        UE_LOG(LogTemp, Display, TEXT("RALLY_SERVICE cannot afford %s (%d Sk)"), UTF8_TO_TCHAR(Job.Name), Job.PriceSk);
        return false;
    }
    switch (static_cast<EJob>(Kind))
    {
    case EJob::Radiator: Car->GetDamage()->SetFittedPart(ERallyPart::Radiator, 1.0f); break;
    case EJob::Battery: Car->GetDamage()->SetFittedPart(ERallyPart::Battery, 1.0f); break;
    case EJob::Engine: Car->GetDamage()->SetFittedPart(ERallyPart::Engine, 1.0f); break;
    case EJob::Sump: Car->GetDamage()->SetFittedPart(ERallyPart::OilSump, 1.0f); break;
    case EJob::Tank: Car->GetDamage()->SetFittedPart(ERallyPart::FuelTank, 1.0f); break;
    case EJob::Exhaust: Car->GetDamage()->SetFittedPart(ERallyPart::Exhaust, 1.0f); break;
    case EJob::Gearbox: Car->GetDamage()->SetFittedPart(ERallyPart::Gearbox, 1.0f); break;
    case EJob::Fuel: Car->GetEngine()->Refuel(); break;
    default:
        // Straightened, every attached panel's hinges and latch renewed; panels off the car stay off.
        if (Deform) Deform->ClearDents();
        for (int32 Panel = 0; Panel < NumPanels; ++Panel)
        {
            if (!ValidPanel(Panel) || IsDetached(Panel)) continue;
            FRallyPanelCondition& State = Conditions[Panel];
            State.HingeA = State.HingeB = State.Latch = 255;
            State.bJammed = false;
            State.FitOffset = FVector::ZeroVector;
            State.FitRotation = FRotator::ZeroRotator;
        }
        OnRep_Conditions();
        GetOwner()->ForceNetUpdate();
        break;
    }
    Team->AddMoney(-Job.PriceSk, FString::Printf(TEXT("workshop: %s"), UTF8_TO_TCHAR(Job.Name)));
    // The team waits while it is done.
    if (Clock)
    {
        const double Hour = Clock->GetHour() + Job.Hours;
        Clock->SetTime(Clock->GetDay() + (Hour >= 24.0 ? 1 : 0), FMath::Fmod(Hour, 24.0));
    }
    UE_LOG(LogTemp, Display, TEXT("RALLY_SERVICE %s, %.1f h"), UTF8_TO_TCHAR(Job.Name), Job.Hours);
    return true;
}

void URallyPanelComponent::DebugSetFixingOut(int32 Part, int32 Fixing)
{
    if (!GetOwner()->HasAuthority() || Fixing < 0 || Fixing >= NumFixings(Part)) return;
    Conditions[Part].Out |= static_cast<uint8>(1u << Fixing);
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
}

UStaticMesh* URallyPanelComponent::MeshForSlot(int32 Slot) const
{
    const auto Load = [](const TCHAR* Name) {
        return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_%s.SM_Octavia_%s"), Name, Name));
    };
    if (Slot == JackSlot) return Load(TEXT("Jack"));
    if (Slot == WaterSlot) return Load(TEXT("WaterCan"));
    if (Slot == OilBottleSlot) return Load(TEXT("OilBottle"));
    if (Slot == FirstAidSlot) return Load(TEXT("FirstAid"));
    if (Slot == DrinkSlot) return Load(TEXT("Slivovica"));
    if (Slot == SmokeSlot) return Load(TEXT("Cigarettes"));
    if (Slot == ExhaustSlot) return Exhaust ? Exhaust->GetStaticMesh().Get() : nullptr;
    const UStaticMeshComponent* Parent = FixingParent(Slot);
    return Parent ? Parent->GetStaticMesh().Get() : nullptr;
}

ARallyDebrisActor* URallyPanelComponent::DebugSpawnPart(int32 Slot, uint8 Grade, const FVector& Location)
{
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    UStaticMesh* Mesh = MeshForSlot(Slot);
    if (!GetOwner()->HasAuthority() || !Car || !Mesh || (!IsGear(Slot) && !IsModule(Slot) && Slot != DrinkSlot && Slot != SmokeSlot && Slot != FirstAidSlot))
        return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ARallyDebrisActor* Part = GetWorld()->SpawnActor<ARallyDebrisActor>(ARallyDebrisActor::StaticClass(), Location, Car->GetActorRotation(), Params);
    if (!Part) return nullptr;
    FRallyDebrisSource Source;
    Source.Car = Car; Source.Mesh = Mesh; Source.Panel = Slot; Source.MassKg = Slot == DrinkSlot ? 1.2f : Slot == SmokeSlot ? 0.3f : Slot == FirstAidSlot ? 1.0f : 6.0f;
    Source.Health = 1.0f; Source.bSpare = true; Source.Grade = FMath::Min<uint8>(Grade, 3);
    Part->Launch(Source, {}, FVector::ZeroVector, FVector::ZeroVector);
    UE_LOG(LogTemp, Display, TEXT("RALLY_PART spawned %s"), *GradedName(Slot, true, Source.Grade));
    return Part;
}

int32 URallyPanelComponent::CountOut(int32 Part) const
{
    int32 Count = 0;
    for (int32 F = 0; F < NumFixings(Part); ++F) Count += IsFixingOut(Part, F) ? 1 : 0;
    return Count;
}

Rally::Fixing::ETool URallyPanelComponent::FixingTool(int32 Part, int32 Fixing) const
{
    return Fixing >= 0 && Fixing < NumFixings(Part) ? Spots[SpotStart[Part] + Fixing].Tool : ETool::Hands;
}

FString URallyPanelComponent::FixingName(int32 Part, int32 Fixing) const
{
    if (Fixing < 0 || Fixing >= NumFixings(Part)) return FString();
    const FFixingSpot& Spot = Spots[SpotStart[Part] + Fixing];
    return UTF8_TO_TCHAR(Rally::Fixing::FixingName(Spot.Kind, Spot.Tool));
}

FVector URallyPanelComponent::GetFixingLocation(int32 Part, int32 Fixing) const
{
    if (Fixing < 0 || Fixing >= NumFixings(Part) || !FixingMeshes.IsValidIndex(SpotStart[Part] + Fixing)) return GetPartLocation(Part);
    // The head of the fixing, where a spanner goes on.
    const UStaticMeshComponent* Mesh = FixingMeshes[SpotStart[Part] + Fixing];
    const FFixingSpot& Spot = Spots[SpotStart[Part] + Fixing];
    const UStaticMeshComponent* Parent = FixingParent(Part);
    const FVector Local = Spot.Local + Spot.Facing * (IsFixingOut(Part, Fixing) ? Rally::Fixing::OutStandCm : 0.0);
    return Parent ? Parent->GetComponentTransform().TransformPosition(Local) : Mesh->GetComponentLocation();
}

void URallyPanelComponent::GetFixingHold(int32 Part, int32& OutHome, int32& OutTotal) const
{
    OutTotal = NumFixings(Part);
    OutHome = OutTotal - CountOut(Part);
}

FString URallyPanelComponent::FixingAccess(int32 Slot) const
{
    // Order matters: what covers a fixing has to be open or off first.
    if (Slot >= 0 && Slot < 4 && !IsOpen(Slot)) return TEXT("open the door to reach its hinge bolts");
    if ((Slot == 4 || Slot == 5) && !IsOpen(Slot)) return Slot == 4 ? TEXT("open the bonnet to reach its hinge bolts") : TEXT("open the boot to reach its hinge bolts");
    if ((Slot == 9 || Slot == 10 || Slot == 11 || Slot == 12) && !IsBayOpen()) return TEXT("open the bonnet to reach its bolts");
    if ((Slot == 13 || Slot == 14) && !IsOpen(5) && !IsDetached(5)) return TEXT("open the boot to reach its nuts");
    if (IsGear(Slot) && GearKind(Slot) != EGear::TieRod && WheelOn(FirstWheel + GearCorner(Slot))) return TEXT("take the wheel off to reach it");
    if (IsModule(Slot))
    {
        // The bay open; then what sits over or in front of each module comes off first.
        const auto Off = [this](EModule Kind) { return IsRemoved(ModuleSlot(Kind)); };
        if (!IsBayOpen()) return TEXT("open the bonnet to reach it");
        switch (ModuleKind(Slot))
        {
        case EModule::Intake: if (!Off(EModule::Cover)) return TEXT("take the engine cover off first"); break;
        case EModule::Turbo:
            if (!Off(EModule::Cover)) return TEXT("take the engine cover off first");
            if (!Off(EModule::AirFilter)) return TEXT("take the air filter box off first");
            break;
        case EModule::ExhaustManifold:
            if (!Off(EModule::Cover)) return TEXT("take the engine cover off first");
            if (!Off(EModule::Turbo)) return TEXT("take the turbo off first");
            break;
        case EModule::Starter: if (!Off(EModule::AirFilter)) return TEXT("take the air filter box off first"); break;
        case EModule::Intercooler: if (!IsDetached(7)) return TEXT("take the front bumper off first"); break;
        default: break;
        }
    }
    return FString();
}

FString URallyPanelComponent::FixingPrompt(const ARallyCharacter* Character, int32 Part, int32 Fixing) const
{
    if (!Character || Fixing < 0 || Fixing >= NumFixings(Part) || !FixingsPresent(Part)) return FString();
    const FString Name = FString::Printf(TEXT("%s, %s"), *FixingName(Part, Fixing), *PanelName(Part));
    const FString Access = FixingAccess(Part);
    if (!Access.IsEmpty()) return FString::Printf(TEXT("%s: %s"), *Name, *Access);
    const ETool Tool = FixingTool(Part, Fixing);
    const int32 Out = CountOut(Part);
    const FString Count = FString::Printf(TEXT("(%d of %d out)"), Out, NumFixings(Part));
    if (Character->GetHeldTool() != Tool)
        return FString::Printf(TEXT("%s %s: needs the %s, you hold the %s"), *Name, *Count, UTF8_TO_TCHAR(Rally::Fixing::ToolName(Tool)),
            UTF8_TO_TCHAR(Rally::Fixing::ToolName(Character->GetHeldTool())));
    FString Prompt = FString::Printf(TEXT("%s %s   hold %s"), *Name, *Count, IsFixingOut(Part, Fixing) ? TEXT("E: do it up") : TEXT("R: undo it"));
    if (Out == NumFixings(Part) && !(Part >= 0 && Part < 4 && !IsOpen(Part)))
        Prompt += FString::Printf(TEXT("   R on the %s: lift it off"), *PanelName(Part));
    return Prompt;
}

bool URallyPanelComponent::TurnFixing(ARallyCharacter* Character, int32 I, int32 Fixing, bool bHome)
{
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    if (!GetOwner()->HasAuthority() || !Car || !Character || Character->GetCarriedPart() || Fixing < 0 || Fixing >= NumFixings(I)
        || !FixingsPresent(I) || !CanReach(Character, I) || !FixingAccess(I).IsEmpty() || IsFixingOut(I, Fixing) != bHome
        || Character->GetHeldTool() != FixingTool(I, Fixing) || !Character->AdvanceFixingTurn()) return false;
    const auto Before = ValidPanel(I) ? Rally::Panel::Stage(Conditions[I].Fixings(), Clipped(I)) : Rally::Panel::EStage::Intact;
    if (bHome) Conditions[I].Out &= ~static_cast<uint8>(1u << Fixing);
    else Conditions[I].Out |= static_cast<uint8>(1u << Fixing);
    if (bHome)
    {
        const double Now = GetWorld()->GetTimeSeconds();
        if (FitTurns[I] == 0) FitFirstTurn[I] = Now;
        FitLastTurn[I] = Now;
        ++FitTurns[I];
        if (AllHome(I)) FinishFit(I, Character);
    }
    else FitTurns[I] = 0;
    UE_LOG(LogTemp, Display, TEXT("RALLY_FIX %s %s %s (%d of %d out)"), *FixingName(I, Fixing), *PanelName(I), bHome ? TEXT("home") : TEXT("out"),
        CountOut(I), NumFixings(I));
    if (ValidPanel(I)) LogState(I, Before);
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
    return true;
}

void URallyPanelComponent::FinishFit(int32 I, ARallyCharacter* Character)
{
    // Wheels and running gear simply hold once home; a body panel ends up as crooked as it was offered,
    // bent and hurried (Rally::Panel::FitSloppiness), and too crooked its latch or clips only half catch.
    const int32 Turns = FitTurns[I];
    FitTurns[I] = 0;
    if (!ValidPanel(I) || Turns < NumFixings(I)) return;
    const double Interval = Turns > 1 ? (FitLastTurn[I] - FitFirstTurn[I]) / (Turns - 1) : 2.0;
    const double Rush = FMath::Clamp((1.2 - Interval) / 0.7, 0.0, 1.0) * 0.5;
    const double BentCm = Deform->MaxDisplacement(Meshes[I]);
    const double Sloppiness = Rally::Panel::FitSloppiness(Rush, OfferAimDeg[I], BentCm, false);
    const FVector Normal = Mounts[I].Normal;
    FVector Shift = FMath::VRand();
    Shift = (Shift - Normal * FVector::DotProduct(Shift, Normal)) * (Rally::Panel::FitGapCm * Sloppiness)
        + Normal * FMath::FRandRange(0.0, Rally::Panel::FitGapCm * Sloppiness * 0.6);
    Conditions[I].FitOffset = Shift;
    Conditions[I].FitRotation = FQuat(FMath::VRand(), FMath::DegreesToRadians(Rally::Panel::FitSkewDeg * Sloppiness)).Rotator();
    if (Sloppiness >= Rally::Panel::HalfCaughtSloppiness)
    {
        if (Clipped(I)) Conditions[I].HingeA = Conditions[I].HingeB = Rally::Panel::HalfCaughtHealth;
        else Conditions[I].Latch = Rally::Panel::HalfCaughtHealth;
    }
    UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL refit panel=%d bodge=0 sloppiness=%.2f aim=%.0f interval=%.2fs shift=%.1fcm skew=%.1fdeg stage=%s"),
        I, Sloppiness, OfferAimDeg[I], Interval, Shift.Size(), Rally::Panel::FitSkewDeg * Sloppiness,
        StageName(Rally::Panel::Stage(Conditions[I].Fixings(), Clipped(I))));
}

void URallyPanelComponent::FallOff(int32 I)
{
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    if (!Car || !GetOwner()->HasAuthority()) return;
    if (IsWheel(I))
    {
        // Its last bolt gone, the wheel leaves the hub and rolls off; the corner drops.
        UE_LOG(LogTemp, Display, TEXT("RALLY_FIX %s fell off: every bolt worked loose"), *PanelName(I));
        Car->GetDamage()->ApplyPartDamage(static_cast<ERallyPart>(static_cast<int32>(ERallyPart::HubFrontLeft) + I - FirstWheel), 1.0f);
        Conditions[I].Out = 0;
        DetachWheel(I - FirstWheel, true);
        return;
    }
    if (!IsGear(I) || IsRemoved(I)) return;
    UStaticMeshComponent* Mesh = Gear[I - FirstGear];
    FRallyPartCondition Condition;
    Car->GetDamage()->GetPartCondition(PartOfGear(I), Condition);
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ARallyDebrisActor* Part = GetWorld()->SpawnActor<ARallyDebrisActor>(ARallyDebrisActor::StaticClass(), Mesh->GetComponentTransform(), Params);
    if (!Part) return;
    FRallyDebrisSource Source;
    Source.Car = Car; Source.Mesh = Mesh->GetStaticMesh(); Source.Panel = I; Source.MeshToCar = Mesh->GetRelativeTransform();
    Source.MassKg = 5.0f; Source.Health = Condition.Health;
    Source.Grade = Conditions[I].Grade;
    Part->Launch(Source, {}, Car->GetVelocity() * 0.8, FVector::ZeroVector);
    Conditions[I].bRemoved = true;
    Conditions[I].Out = 0;
    Conditions[I].Debris = Part;
    Car->GetDamage()->SetFittedPart(PartOfGear(I), 0.0f);
    UE_LOG(LogTemp, Display, TEXT("RALLY_FIX %s fell off: every fixing worked loose"), *PanelName(I));
}

void URallyPanelComponent::SpawnSpares()
{
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    if (!Car || !GetWorld()) return;
    // The boot kit: a spare wheel lying in its well under the floor, then the jack and one of each
    // suspension part an axle can need (and two discs) packed on the load floor, biggest first (BootPlace).
    struct FSpare { int32 Slot; float Mass; };
    const FSpare Kit[] = {
        { FirstWheel + 2, WheelMassKg },
        { GearSlot(EGear::Strut, 0), 9 },
        { GearSlot(EGear::Strut, 2), 8 },
        { GearSlot(EGear::Driveshaft, 0), 6 },
        { GearSlot(EGear::Arm, 0), 4 },
        { GearSlot(EGear::Arm, 2), 5 },
        { GearSlot(EGear::Disc, 0), 7 },
        { GearSlot(EGear::Disc, 2), 6 },
        { JackSlot, JackMassKg },
        { WaterSlot, WaterCanMassKg },
        { OilBottleSlot, OilBottleMassKg },
        { FirstAidSlot, 1.0f },
    };
    UStaticMesh* JackMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_Jack.SM_Octavia_Jack"));
    UStaticMesh* CanMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_WaterCan.SM_Octavia_WaterCan"));
    UStaticMesh* BottleMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_OilBottle.SM_Octavia_OilBottle"));
    UStaticMesh* AidMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Vehicles/Octavia/Prototype/SM_Octavia_FirstAid.SM_Octavia_FirstAid"));
    for (const FSpare& Spare : Kit)
    {
        UStaticMesh* Mesh = Spare.Slot == WaterSlot ? CanMesh : Spare.Slot == OilBottleSlot ? BottleMesh : Spare.Slot == FirstAidSlot ? AidMesh : JackMesh;
        if (IsWheel(Spare.Slot)) Mesh = Wheels[Spare.Slot - FirstWheel]->GetStaticMesh();
        else if (IsGear(Spare.Slot)) Mesh = Gear.IsValidIndex(Spare.Slot - FirstGear) ? Gear[Spare.Slot - FirstGear]->GetStaticMesh() : nullptr;
        if (!Mesh) continue;
        // The wheel lies flat in the well, its centre 10 cm above the floor pan.
        FVector Location = AssemblyOffset + FVector(-187.0, 0.0, 47.0);
        FRotator Turn(0.0, 0.0, 90.0);
        if (!IsWheel(Spare.Slot) && !BootPlace(Mesh, nullptr, Location, Turn))
        {
            UE_LOG(LogTemp, Warning, TEXT("RALLY_BOOT no room in the kit for %s"), *CarriedName(Spare.Slot, true));
            continue;
        }
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ARallyDebrisActor* Part = GetWorld()->SpawnActor<ARallyDebrisActor>(ARallyDebrisActor::StaticClass(),
            Car->GetActorTransform().TransformPosition(Location), Car->GetActorRotation(), Params);
        if (!Part) continue;
        FRallyDebrisSource Source;
        Source.Car = Car; Source.Mesh = Mesh; Source.Panel = Spare.Slot; Source.MassKg = Spare.Mass;
        Source.Health = 1.0f; Source.bSpare = Spare.Slot != JackSlot;
        Part->Launch(Source, {}, FVector::ZeroVector, FVector::ZeroVector);
        Part->Mount(ERallyPartMount::Stowed, Location, Turn);
    }
}

bool URallyPanelComponent::CornerReady(int32 Corner) const
{
    for (const EGear Kind : { EGear::Strut, EGear::Arm, EGear::Disc })
        if (IsRemoved(GearSlot(Kind, Corner))) return false;
    return true;
}

void URallyPanelComponent::OnRep_Jack()
{
    if (ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner())) Car->SetJackCorner(JackCorner);
}

bool URallyPanelComponent::IsDetached(int32 I) const
{
    return ValidPanel(I) && Conditions.IsValidIndex(I)
        && Rally::Panel::Stage(Conditions[I].Fixings(), Clipped(I)) == Rally::Panel::EStage::Detached;
}

bool URallyPanelComponent::IsDoorJammed(int32 I) const
{
    return I >= 0 && I < 4 && Conditions.IsValidIndex(I) && Conditions[I].bJammed && !IsDetached(I);
}

uint32 URallyPanelComponent::AttachedDentMask() const
{
    // Every deformable mesh (chassis, panels at I + 1, fixed body parts after them) except missing panels.
    uint32 Mask = 0xffffffff;
    for (int32 I = 0; I < NumPanels; ++I)
        if (IsDetached(I)) Mask &= ~(1u << (I + 1));
    return Mask;
}

void URallyPanelComponent::OnRep_Conditions()
{
    UpdateFixingVisuals();
    if (ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner())) Car->RefreshCondition();
    for (int32 I = 0; I < Gear.Num(); ++I)
        Gear[I]->SetVisibility(!IsRemoved(FirstGear + I));
    if (Exhaust) Exhaust->SetVisibility(!IsRemoved(ExhaustSlot));
    for (int32 M = 0; M < Modules.Num(); ++M)
        Modules[M]->SetVisibility(!IsRemoved(FirstModule + M));
    for (int32 I = 0; I < Meshes.Num(); ++I)
    {
        Meshes[I]->SetHiddenInGame(IsDetached(I), true);
        if (!GetOwner()->HasAuthority() && ValidPanel(I) && Conditions.IsValidIndex(I))
            UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL replicated panel=%d stage=%s open=%d"), I,
                StageName(Rally::Panel::Stage(Conditions[I].Fixings(), Clipped(I))), Conditions[I].bOpen ? 1 : 0);
    }
}

void URallyPanelComponent::LogState(int32 I, Rally::Panel::EStage Before) const
{
    const auto After = Rally::Panel::Stage(Conditions[I].Fixings(), Clipped(I));
    if (Before != After)
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL panel=%d %s -> %s fixings=%d,%d,%d strap=%d"),
            I, StageName(Before), StageName(After), Conditions[I].HingeA, Conditions[I].HingeB,
            Conditions[I].Latch, Conditions[I].Strap);
}

void URallyPanelComponent::ApplyImpact(const FBox& Patch, const FVector& Direction, float DeltaV)
{
    if (!GetOwner()->HasAuthority()) return;
    for (int32 I = 0; I < Meshes.Num(); ++I)
    {
        if (!ValidPanel(I) || IsDetached(I)) continue;
        auto& State = Conditions[I];
        const auto Before = Rally::Panel::Stage(State.Fixings(), Clipped(I));
        auto F = State.Fixings();
        const auto Reach = [&](const FVector& Point) {
            return Rally::Impact::ReachFactor(FMath::Sqrt(Patch.ComputeSquaredDistanceToPoint(Point + AssemblyOffset)),
                24.0, Rally::Impact::CrushDistance(Rally::Impact::DentFor(DeltaV).Depth));
        };
        const FMount& M = Mounts[I];
        F.A = Rally::Panel::Damage(F.A, Rally::Panel::ImpactLoss(DeltaV, Clipped(I)) * Reach(M.A));
        F.B = Rally::Panel::Damage(F.B, Rally::Panel::ImpactLoss(DeltaV, Clipped(I)) * Reach(M.B));
        if (!Clipped(I))
            F.Latch = Rally::Panel::Damage(F.Latch, Rally::Panel::ImpactLoss(DeltaV, true) * Reach(M.Latch));
        if (F.Strap > 0)
            F.Strap = Rally::Panel::Damage(F.Strap, Rally::Panel::ImpactLoss(DeltaV, true)
                * FMath::Max3(Reach(M.A), Reach(M.B), Reach(M.Latch)));
        if (I < 4 && FMath::Abs(Direction.Y) > 0.65 && DeltaV > 10 && Reach(M.Latch) > 0.3)
        {
            State.bJammed = true;
            State.bOpen = false;
        }
        State.Set(F);
        // Whatever held it open is not what holds it now.
        if (!Rally::Panel::CanOpenByHand(F, Clipped(I), State.bJammed)) State.bOpen = false;
        LogState(I, Before);
        if (IsDetached(I)) Detach(I);
    }
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
}

void URallyPanelComponent::TickPanels(float Dt, uint8 DoorsOpen)
{
    if (Rest.Num() != Meshes.Num() || Dt <= 0) return;
    const FTransform Car = GetOwner()->GetActorTransform();
    const FVector Velocity = GetOwner()->GetVelocity();
    FVector Accel = FVector::ZeroVector;
    if (bHaveVelocity && Dt < 0.25f && FVector::Dist(Velocity, PreviousVelocity) < 1500.0)
        Accel = Car.InverseTransformVectorNoScale((Velocity - PreviousVelocity) / Dt) / 980.0;
    PreviousVelocity = Velocity;
    bHaveVelocity = true;
    SmoothedAccel += (Accel - SmoothedAccel) * (1.0 - FMath::Exp(-Dt / 0.06));
    const float Kmh = FVector::DotProduct(Velocity, GetOwner()->GetActorForwardVector()) * 0.036f;
    bool Changed = false;
    for (int32 I = 0; I < Meshes.Num(); ++I)
    {
        if (!ValidPanel(I)) continue;
        auto& State = Conditions[I];
        const auto Before = Rally::Panel::Stage(State.Fixings(), Clipped(I));
        if (GetOwner()->HasAuthority() && Before != Rally::Panel::EStage::Detached)
        {
            auto F = State.Fixings();
            Rally::Panel::Advance(F, Clipped(I), WearRemainder[I], Dt, Kmh, Accel.Size());
            if (F.A != State.HingeA || F.B != State.HingeB || F.Latch != State.Latch || F.Strap != State.Strap)
            {
                State.Set(F);
                if (!Rally::Panel::CanOpenByHand(F, Clipped(I), State.bJammed)) State.bOpen = false;
                Changed = true;
                LogState(I, Before);
                if (IsDetached(I)) Detach(I);
            }
        }
        if (!IsDetached(I)) MovePanel(I, Dt, Kmh, Car, Velocity, DoorsOpen);
    }
    // Fixings left out work the others loose on the move; a wheel or running-gear part with none left
    // home drops off (a panel with none left is held by nothing, and its hinges' wear above finishes it).
    if (GetOwner()->HasAuthority() && LooseCarry.Num() == NumSlots)
    {
        for (int32 Slot = 0; Slot < NumSlots; ++Slot)
        {
            const int32 Total = NumFixings(Slot);
            if (Total == 0 || !Conditions.IsValidIndex(Slot) || Conditions[Slot].Out == 0 || !FixingsPresent(Slot)) continue;
            const int32 Home = Total - CountOut(Slot);
            if (Home == 0)
            {
                if ((IsWheel(Slot) || IsGear(Slot)) && FMath::Abs(Kmh) > Rally::Fixing::FallOffKmh) { FallOff(Slot); Changed = true; }
                continue;
            }
            for (int32 More = Rally::Fixing::WorkLoose(Home, Total, LooseCarry[Slot], Dt, Kmh, Accel.Size()); More > 0; --More)
            {
                for (int32 F = 0; F < Total; ++F)
                    if (!IsFixingOut(Slot, F)) { Conditions[Slot].Out |= static_cast<uint8>(1u << F); break; }
                UE_LOG(LogTemp, Display, TEXT("RALLY_FIX a fixing of the %s worked loose (%d of %d out)%s"), *PanelName(Slot), CountOut(Slot), Total,
                    CountOut(Slot) >= Total && IsModule(Slot) ? TEXT(": it hangs on its hoses") : TEXT(""));
                MulticastFixingLoose(static_cast<uint8>(Slot), CountOut(Slot) >= Total);
                Changed = true;
            }
        }
    }
    PoseModules(Dt, Kmh);
    if (Changed) { OnRep_Conditions(); GetOwner()->ForceNetUpdate(); }
    PreviousCar = Car;
    bHavePreviousCar = true;
}

void URallyPanelComponent::MulticastFixingLoose_Implementation(uint8 Slot, bool bNoneLeft)
{
    OnFixingLoose.Broadcast(Slot, bNoneLeft);
}

double URallyPanelComponent::GetLoosestShare() const
{
    double Loosest = 0.0;
    for (int32 Slot = FirstWheel; Slot < NumSlots; ++Slot)
    {
        if (!(IsWheel(Slot) || IsGear(Slot) || IsModule(Slot)) || !FixingsPresent(Slot) || NumFixings(Slot) == 0) continue;
        Loosest = FMath::Max(Loosest, static_cast<double>(CountOut(Slot)) / NumFixings(Slot));
    }
    return Loosest;
}

void URallyPanelComponent::PoseModules(float Dt, float Kmh)
{
    if (ModuleRest.Num() != Modules.Num()) return;
    ShakeTime += Dt;
    const bool bMoving = FMath::Abs(Kmh) > 3.0f;
    for (int32 M = 0; M < Modules.Num(); ++M)
    {
        const int32 Slot = FirstModule + M;
        UStaticMeshComponent* Mesh = Modules[M];
        const int32 Total = NumFixings(Slot);
        const int32 Home = Total - CountOut(Slot);
        // About eight seconds on the move shake a loose part fully out of true.
        if (IsRemoved(Slot) || Home >= Total) ModuleShake[M] = 0.0;
        else if (bMoving) ModuleShake[M] = FMath::Min(ModuleShake[M] + Dt / 8.0, 1.0);
        const double Tilt = ModuleShake[M] * Rally::Fixing::LooseTiltDeg(Home, Total);
        const bool bWasCrooked = ModuleTilt[M] != 0.0;
        if (Tilt <= 0.0)
        {
            ModuleTilt[M] = 0.0;
            if (bWasCrooked) Mesh->SetRelativeTransform(ModuleRest[M]);
            continue;
        }
        // Each part leans its own way about its middle, drops on its loose mounts, and chatters on the move.
        const double Wobble = bMoving ? FMath::Sin(ShakeTime * 2.0 * UE_DOUBLE_PI * 11.0 + M) * (1.0 + 0.1 * Tilt) : 0.0;
        ModuleTilt[M] = Tilt;
        const FVector Axis(FMath::Cos(M * 1.7), FMath::Sin(M * 1.7), 0.0);
        const FVector Centre = Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetBounds().Origin : FVector::ZeroVector;
        const FTransform Lean = FTransform(-Centre) * FTransform(FQuat(Axis, FMath::DegreesToRadians(Tilt + Wobble)))
            * FTransform(Centre - FVector(0.0, 0.0, ModuleShake[M] * Rally::Fixing::LooseDropCm(Home, Total)));
        Mesh->SetRelativeTransform(Lean * ModuleRest[M]);
    }
}

FTransform URallyPanelComponent::PanelPose(int32 I, double SwingDeg, double DroopDeg) const
{
    const FMount& M = Mounts[I];
    const FRallyPanelCondition& State = Conditions[I];
    const FVector SwingPivot = (M.A + M.B) * 0.5 + AssemblyOffset;
    const FVector DroopPivot = (State.HingeA <= 0 ? M.B : M.A) + AssemblyOffset;
    const FQuat SwingTurn(M.Axis.IsZero() ? FVector::UpVector : M.Axis, FMath::DegreesToRadians(SwingDeg));
    const FQuat DroopTurn(M.DroopAxis, FMath::DegreesToRadians(DroopDeg));
    const FVector Centre = Rest[I].GetLocation();
    const FVector Hung = DroopPivot + DroopTurn.RotateVector(SwingPivot + SwingTurn.RotateVector(Centre - SwingPivot) - DroopPivot);
    // A refitted panel sits as it was bolted or strapped on: skewed about its hinge line and shifted.
    const FQuat Fit = State.FitRotation.Quaternion();
    return FTransform(Fit * DroopTurn * SwingTurn * Rest[I].GetRotation(),
        SwingPivot + Fit.RotateVector(Hung - SwingPivot) + FVector(State.FitOffset));
}

bool URallyPanelComponent::PoseBlocked(int32 I, const FTransform& Car, const FTransform& Pose, const FVector& Anchor) const
{
    // Each probe point joined to the hinge the panel hangs from: a line through the ground, a wall, a
    // tree or another car means the panel is in it. The panel's own car is ignored; its shut position
    // and hinge stops stand in for the body.
    FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyPanelSwing), false, GetOwner());
    const FVector Start = Car.TransformPosition(Anchor);
    for (const FVector& Point : Probes[I])
        if (GetWorld()->LineTraceTestByChannel(Start, Car.TransformPosition(Pose.TransformPosition(Point)), ECC_Visibility, Params))
            return true;
    return false;
}

void URallyPanelComponent::MovePanel(int32 I, float Dt, float Kmh, const FTransform& Car, const FVector& CarVelocity, uint8 DoorsOpen)
{
    const FMount& M = Mounts[I];
    const FRallyPanelCondition& State = Conditions[I];
    const bool bStrapped = State.Strap > 0;
    const bool bLatched = !Clipped(I) && State.Latch > 0;
    const bool bOpenable = !State.bJammed && !bStrapped && bLatched;
    // A front door is held open while someone gets in or out.
    const bool bEntry = bOpenable && I < 2 && (DoorsOpen & (1 << I));
    // Opened by hand, a panel is guided out to its open angle, then let go: its door check, prop or
    // struts hold it there, but only so hard. Braking, wind, bumps or a knock swing it, and swung back
    // into its frame hard enough it catches its latch (the server decides); otherwise it rests ajar.
    const bool bLeftOpen = bOpenable && State.bOpen && !bEntry;
    if (bLeftOpen && !WasLeftOpen[I]) GuideTime[I] = GuideSeconds;
    WasLeftOpen[I] = bLeftOpen;
    if (!bLeftOpen || Swing[I].Angle >= M.OpenAngle - 2.0) GuideTime[I] = 0.0;
    GuideTime[I] = FMath::Max(0.0, GuideTime[I] - Dt);
    const bool bHeldOpen = bEntry || (bLeftOpen && GuideTime[I] > 0.0);
    const bool bLetGo = bLeftOpen && !bHeldOpen;
    // Swings freely once nothing holds its free edge: latch gone, not strapped or jammed shut.
    const bool bSwingFree = !Clipped(I) && !bLatched && !bStrapped && !State.bJammed;
    // A lone surviving hinge (or bumper clip) lets it droop; a working latch keeps that to a sag.
    const bool bOneFixing = (State.HingeA <= 0) != (State.HingeB <= 0) && !bStrapped;
    const double MaxDroop = bOneFixing ? (bLatched ? 2.0 : M.MaxDroop) : 0.0;
    const FVector SwingPivot = (M.A + M.B) * 0.5 + AssemblyOffset;
    const FVector DroopPivot = (State.HingeA <= 0 ? M.B : M.A) + AssemblyOffset;
    const FVector Centre = Rest[I].GetLocation();
    const FQuat RestTurn = Rest[I].GetRotation();
    Rally::Panel::FHinge& S = Swing[I];
    Rally::Panel::FHinge& D = Droop[I];
    const FVector2D Previous = LastClear[I];
    double StruckOpenStop = 0.0;
    double StruckShut = 0.0;
    bool bPressedShut = false;
    // Where a hand (or prop, or latch) is taking the panel: towards open when held open, towards shut
    // (or its rattle in a loose latch) otherwise, at hand pace. A free panel goes where physics takes it.
    double Wanted = bHeldOpen ? M.OpenAngle : 0.0;
    if (!bHeldOpen && !State.bJammed && !Clipped(I) && Rally::Panel::Stage(State.Fixings(), false) == Rally::Panel::EStage::Loose)
        Wanted = bStrapped ? 0.3 : 0.8 + FMath::Clamp(SmoothedAccel.Z * 1.5, -0.7, 1.3);
    HandTarget[I] = bSwingFree || bLetGo ? S.Angle : FMath::FInterpConstantTo(HandTarget[I], Wanted, static_cast<double>(Dt), HandSwingSpeed);

    if (bSwingFree || bHeldOpen || bLetGo || MaxDroop > 0.0)
    {
        // A rigid plate on its hinge: gravity and the car's own acceleration (felt as the reverse
        // force), wind pressing on the plate, and a lift where wind gets under a free leading edge.
        const FVector Felt = Car.InverseTransformVectorNoScale(FVector(0.0, 0.0, -9.81)) - SmoothedAccel * 9.81;
        const FVector Wind = -Car.InverseTransformVectorNoScale(CarVelocity) * 0.01;
        const double Pressure = 0.5 * 1.225 * Wind.SizeSquared();
        const double SwingInertia = PlateInertia(M.Mass, Centre, SwingPivot, M.Axis);
        const double DroopInertia = PlateInertia(M.Mass, Centre, DroopPivot, M.DroopAxis);
        double Remaining = FMath::Min(static_cast<double>(Dt), 0.1);
        while (Remaining > 0.0)
        {
            const double Step = FMath::Min(Remaining, HingeStep);
            Remaining -= Step;
            const FTransform Pose = PanelPose(I, S.Angle, D.Angle);
            const FVector Cm = Pose.GetLocation();
            const FVector Normal = (Pose.GetRotation() * RestTurn.Inverse()).RotateVector(M.Normal);
            FVector Force = Felt * M.Mass + Normal * (Pressure * M.Area * 1.2 * FVector::DotProduct(Wind.GetSafeNormal(), Normal));
            if (M.Lift > 0.0f && bSwingFree && S.Angle < 20.0 && Kmh > 0.0f)
                Force += FVector::UpVector * (Pressure * M.Area * M.Lift * (1.0 - S.Angle / 20.0));
            if (bSwingFree || bHeldOpen || bLetGo)
            {
                const FQuat DroopTurn(M.DroopAxis, FMath::DegreesToRadians(D.Angle));
                const FVector Axis = DroopTurn.RotateVector(M.Axis);
                const FVector Pivot = DroopPivot + DroopTurn.RotateVector(SwingPivot - DroopPivot);
                double Torque = FVector::DotProduct(FVector::CrossProduct((Cm - Pivot) * 0.01, Force), Axis);
                double Damping = SwingInertia * HingeFriction;
                if (bHeldOpen)
                {
                    // A hand: stiff but not rigid.
                    Torque += SwingInertia * HoldStiffness * FMath::DegreesToRadians(HandTarget[I] - S.Angle);
                    Damping += SwingInertia * HoldDamping;
                }
                else if (bLetGo && FMath::Abs(M.OpenAngle - S.Angle) < M.CheckRange)
                {
                    // Door check, prop or struts: hold it near its open angle, up to their strength.
                    Torque += FMath::Clamp(SwingInertia * CheckStiffness * FMath::DegreesToRadians(M.OpenAngle - S.Angle),
                        -static_cast<double>(M.CheckTorque), static_cast<double>(M.CheckTorque));
                    Damping += SwingInertia * HoldDamping * 0.25;
                }
                const double Struck = Rally::Panel::StepHinge(S, Torque, SwingInertia, Damping, 0.0, M.MaxAngle, HingeBounce, Step);
                if ((bSwingFree || bLetGo) && Struck > 0.0 && S.Angle >= M.MaxAngle) StruckOpenStop = FMath::Max(StruckOpenStop, Struck);
                if (bLetGo && Struck > 0.0 && S.Angle <= 0.0) StruckShut = FMath::Max(StruckShut, Struck);
                if (bLetGo && S.Angle <= 0.0 && Torque < -Rally::Panel::LatchPressTorque) bPressedShut = true;
            }
            if (MaxDroop > 0.0)
            {
                // While a door, bonnet or boot lid still sits in its opening, the body's edge stops its
                // free side dropping more than a little; swung out, it droops as far as the hinge allows.
                double Low = -MaxDroop, High = MaxDroop;
                if (!Clipped(I) && S.Angle < 15.0)
                {
                    const bool bPositiveLowers = FVector::CrossProduct(M.DroopAxis, Cm - DroopPivot).Z < 0.0;
                    (bPositiveLowers ? High : Low) = bPositiveLowers ? FMath::Min(MaxDroop, 1.5) : -FMath::Min(MaxDroop, 1.5);
                }
                const double Torque = FVector::DotProduct(FVector::CrossProduct((Cm - DroopPivot) * 0.01, Force), M.DroopAxis);
                Rally::Panel::StepHinge(D, Torque, DroopInertia, DroopInertia * HingeFriction, Low, High, HingeBounce, Step);
            }
        }
    }
    if (!bSwingFree && !bHeldOpen && !bLetGo)
    {
        // A loose latch lets the panel rattle in its opening; a strap holds it tighter.
        Spring(S, HandTarget[I], Dt);
        S.Angle = FMath::Clamp(S.Angle, 0.0, static_cast<double>(M.MaxAngle));
    }
    if (MaxDroop <= 0.0) Spring(D, bStrapped && (State.HingeA <= 0) != (State.HingeB <= 0) ? 3.0 : 0.0, Dt);

    // Shut in its opening it cannot touch anything the body does not; out of it, ground, walls, trees
    // and other cars stop it. Back off towards where it was last clear (or the shut pose) and bounce.
    const FVector Anchor = MaxDroop > 0.0 ? DroopPivot : SwingPivot;
    const FVector2D Now(S.Angle, D.Angle);
    FVector2D Clear = LastClear[I];
    FVector2D Blocked = Now;
    bool bHit = false;
    // Nothing new to touch while the car stands still and the panel has not moved.
    const bool bStill = CarVelocity.SizeSquared() < 1.0 && (Now - Clear).GetAbsMax() < 0.01;
    if ((S.Angle >= 1.0 || FMath::Abs(D.Angle) >= 1.0) && !bStill)
    {
        // A fast swing is checked every few degrees on the way, so it cannot skip past a thin post.
        const int32 Steps = FMath::Clamp(FMath::CeilToInt((Now - Clear).GetAbsMax() / 4.0), 1, 12);
        const FVector2D From = Clear;
        for (int32 Step = 1; Step <= Steps && !bHit; ++Step)
        {
            const FVector2D At = FMath::Lerp(From, Now, static_cast<double>(Step) / Steps);
            if (PoseBlocked(I, Car, PanelPose(I, At.X, At.Y), Anchor)) { bHit = true; Blocked = At; }
            else Clear = At;
        }
        // The car may have carried the last clear pose into something: fall back to the shut pose.
        if (bHit && Clear == From && PoseBlocked(I, Car, PanelPose(I, Clear.X, Clear.Y), Anchor)) Clear = FVector2D::ZeroVector;
    }
    // Where it first ran into something this frame, before backing off.
    const FVector2D Struck = Blocked;
    if (bHit)
    {
        for (int32 Halving = 0; Halving < 5; ++Halving)
        {
            const FVector2D Middle = (Clear + Blocked) * 0.5;
            (PoseBlocked(I, Car, PanelPose(I, Middle.X, Middle.Y), Anchor) ? Blocked : Clear) = Middle;
        }
        S.Angle = Clear.X;
        D.Angle = Clear.Y;
        // A hand guiding it stops where it meets something and holds it there; let go, it bounces.
        if (bHeldOpen) { HandTarget[I] = S.Angle; S.Speed = 0.0; }
        else S.Speed = FMath::Abs(S.Speed) < 20.0 ? 0.0 : -S.Speed * HingeBounce;
        D.Speed = FMath::Abs(D.Speed) < 20.0 ? 0.0 : -D.Speed * HingeBounce;
    }
    LastClear[I] = FVector2D(S.Angle, D.Angle);

    const FTransform Pose = PanelPose(I, S.Angle, D.Angle);
    Meshes[I]->SetRelativeLocationAndRotation(Pose.GetLocation(), Pose.GetRotation());

    // Damage and latching are the server's: its own copy of this motion decides them, and the result
    // replicates.
    if (!GetOwner()->HasAuthority()) return;
    const double ShutSpeed = FMath::DegreesToRadians(StruckShut) * EdgeLever[I];
    if ((ShutSpeed >= Rally::Panel::LatchCatchSpeed || bPressedShut) && Conditions[I].bOpen)
    {
        Conditions[I].bOpen = false;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL panel=%d %s, latch caught"), I, ShutSpeed >= Rally::Panel::LatchCatchSpeed
            ? *FString::Printf(TEXT("swung shut at %.1f m/s"), ShutSpeed) : TEXT("pressed into its frame"));
        OnRep_Conditions();
        GetOwner()->ForceNetUpdate();
    }
    if (StruckOpenStop > 0.0)
        StrainHinges(I, Rally::Panel::OverextendLoss(FMath::DegreesToRadians(StruckOpenStop) * EdgeLever[I]), TEXT("flung against its stop"));
    // A hand guiding the panel eases it against whatever it meets.
    if (bHit && !bHeldOpen && bHavePreviousCar && Dt > 0.0f && !IsDetached(I))
    {
        // Each point's own path since last frame: the fastest one running into a surface is the hit, at
        // that speed into it. Sliding along a surface (a bumper dragging on the road) runs into nothing.
        // Which way the blow turns the panel about its hinge line: back towards shut, or open past its stop.
        const FTransform WasPose = PanelPose(I, Previous.X, Previous.Y);
        const FTransform IsPose = PanelPose(I, Struck.X, Struck.Y);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyPanelStrike), false, GetOwner());
        double Into = 0.0;
        FHitResult Strike;
        const auto Follow = [&](const FVector& Was, const FVector& Is)
        {
            // Run a little past where it is now so a point resting on the surface still finds it.
            FHitResult Path;
            if (!GetWorld()->LineTraceSingleByChannel(Path, Was, Is + (Is - Was).GetSafeNormal() * 5.0, ECC_Visibility, Params)) return;
            const double Speed = -FVector::DotProduct((Is - Was) / Dt, Path.ImpactNormal) * 0.01;
            if (Speed > Into) { Into = Speed; Strike = Path; }
        };
        const FVector AnchorWorld = Car.TransformPosition(Anchor);
        for (const FVector& Probe : Probes[I])
        {
            const FVector Is = Car.TransformPosition(IsPose.TransformPosition(Probe));
            Follow(PreviousCar.TransformPosition(WasPose.TransformPosition(Probe)), Is);
            // The bit of panel where its hinge-to-corner line meets the obstacle (between the corners, as a
            // post meets a door), and where that bit was last frame.
            FHitResult Meet;
            if (GetWorld()->LineTraceSingleByChannel(Meet, AnchorWorld, Is, ECC_Visibility, Params))
                Follow(PreviousCar.TransformPosition(WasPose.TransformPosition(IsPose.InverseTransformPosition(Car.InverseTransformPosition(Meet.ImpactPoint)))),
                    Meet.ImpactPoint);
        }
        if (Into > 0.0)
        {
            const FVector PointCar = Car.InverseTransformPosition(Strike.ImpactPoint);
            const FVector PushCar = Car.InverseTransformVectorNoScale(Strike.ImpactNormal);
            const bool bForcedOpen = !M.Axis.IsZero()
                && FVector::DotProduct(FVector::CrossProduct(PointCar - SwingPivot, PushCar), M.Axis) > 0.0;
            SwingHit(I, PointCar, PushCar, Into, bForcedOpen);
        }
    }
}

void URallyPanelComponent::SwingHit(int32 I, const FVector& PointCar, const FVector& PushCar, double Speed, bool bForcedOpen)
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (Speed < Rally::Panel::SwingDentSpeed || Now - LastSwingHit[I] < 0.25) return;
    LastSwingHit[I] = Now;
    // Dented where it hit. Dents are kept as if the panel were shut, so carry the point back there.
    const FTransform Pose = Meshes[I]->GetRelativeTransform();
    const Rally::Impact::FDent Size = Rally::Impact::DentFor(Speed);
    FRallyDent Dent;
    Dent.Point = Rest[I].TransformPosition(Pose.InverseTransformPosition(PointCar));
    Dent.Direction = Rest[I].TransformVectorNoScale(Pose.InverseTransformVectorNoScale(PushCar)).GetSafeNormal();
    Dent.Extent = FVector(4.0);
    Dent.Depth = static_cast<float>(Size.Depth);
    Dent.Radius = static_cast<float>(Size.Radius * 0.6);
    Dent.PartsMask = 1u << (I + 1);
    if (Size.Depth > 0.0) Deform->AddDent(Dent);
    UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL swing_hit panel=%d speed=%.1fm/s %s"), I, Speed,
        bForcedOpen ? TEXT("forced open") : TEXT("pushed shut"));
    // Driven shut hard enough it slams, catching its latch if that still works.
    if (!bForcedOpen && Speed >= Rally::Panel::SlamShutSpeed && Conditions[I].bOpen)
    {
        Conditions[I].bOpen = false;
        UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL panel=%d slammed shut"), I);
    }
    StrainHinges(I, Rally::Panel::SwingHitLoss(Speed, bForcedOpen), bForcedOpen ? TEXT("forced open") : TEXT("slammed"));
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
}

void URallyPanelComponent::StrainHinges(int32 I, double Loss, const TCHAR* Why)
{
    if (Loss <= 0.0 || IsDetached(I)) return;
    FRallyPanelCondition& State = Conditions[I];
    const auto Before = Rally::Panel::Stage(State.Fixings(), Clipped(I));
    auto F = State.Fixings();
    F.A = Rally::Panel::Damage(F.A, Loss);
    F.B = Rally::Panel::Damage(F.B, Loss);
    // An open panel's latch is holding nothing: with both hinges gone it comes away.
    if (F.A <= 0 && F.B <= 0 && (State.bOpen || Swing[I].Angle > 10.0)) F.Latch = 0;
    State.Set(F);
    if (!Rally::Panel::CanOpenByHand(F, Clipped(I), State.bJammed)) State.bOpen = false;
    UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL hinges panel=%d %s loss=%.2f fixings=%d,%d,%d"), I, Why, Loss, F.A, F.B, F.Latch);
    LogState(I, Before);
    if (IsDetached(I)) Detach(I);
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
}

void URallyPanelComponent::Detach(int32 I)
{
    if (!GetOwner()->HasAuthority() || Conditions[I].Debris || !Meshes[I]->GetStaticMesh()) return;
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    if (!Car || !Deform) return;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ARallyDebrisActor* Part = GetWorld()->SpawnActor<ARallyDebrisActor>(ARallyDebrisActor::StaticClass(), Meshes[I]->GetComponentTransform(), Params);
    if (!Part) return;
    FRallyDebrisSource Source;
    Source.Car = Car; Source.Mesh = Meshes[I]->GetStaticMesh(); Source.Panel = I;
    Source.MeshToCar = Rest[I]; Source.MassKg = Mounts[I].Mass;
    const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Car->GetRootComponent());
    const FVector Angular = Root ? Root->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
    const FVector Linear = Car->GetVelocity() + FVector::CrossProduct(Angular, Part->GetActorLocation() - Car->GetActorLocation());
    Part->Launch(Source, Deform->SnapshotForPart(I + 1), Linear, Angular * (180.0 / PI));
    Conditions[I].Debris = Part;
    Conditions[I].bOpen = false;
    Meshes[I]->SetHiddenInGame(true, true);
}

void URallyPanelComponent::DetachWheel(int32 Wheel, bool bThrown)
{
    const int32 I = FirstWheel + Wheel;
    if (!GetOwner()->HasAuthority() || !Wheels.IsValidIndex(Wheel) || !Wheels[Wheel]->GetStaticMesh()
        || !Conditions.IsValidIndex(I) || IsValid(Conditions[I].Debris)) return;
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    if (!Car) return;
    UStaticMeshComponent* Mesh = Wheels[Wheel];
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ARallyDebrisActor* Part = GetWorld()->SpawnActor<ARallyDebrisActor>(ARallyDebrisActor::StaticClass(), Mesh->GetComponentTransform(), Params);
    if (!Part) return;
    FRallyDebrisSource Source;
    Source.Car = Car; Source.Mesh = Mesh->GetStaticMesh(); Source.Panel = I;
    Source.MeshToCar = Mesh->GetRelativeTransform(); Source.MassKg = WheelMassKg;
    FRallyPartCondition Tyre;
    Car->GetDamage()->GetPartCondition(static_cast<ERallyPart>(static_cast<int32>(ERallyPart::TyreFrontLeft) + Wheel), Tyre);
    Source.Health = Tyre.Health;
    // Thrown out sideways from the hub, still turning at road speed about its axle.
    const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Car->GetRootComponent());
    const FVector Angular = Root ? Root->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
    const FVector Outward = Car->GetActorRightVector() * (Wheel % 2 == 0 ? -1.0 : 1.0);
    const FVector Linear = Car->GetVelocity() + FVector::CrossProduct(Angular, Part->GetActorLocation() - Car->GetActorLocation())
        + (bThrown ? Outward * 350.0 + Car->GetActorUpVector() * 150.0 : FVector::ZeroVector);
    const double Spin = FVector::DotProduct(Car->GetVelocity(), Car->GetActorForwardVector()) / 31.0;
    Part->Launch(Source, {}, Linear, (Angular + Car->GetActorRightVector() * (bThrown ? Spin : 0.0)) * (180.0 / PI));
    Conditions[I].Debris = Part;
    GetOwner()->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("RALLY_WHEEL off wheel=%d"), Wheel);
}

FVector URallyPanelComponent::GetRefitLocation(int32 I) const
{
    // Running gear is worked on from beside its wheel.
    if (IsGear(I)) I = FirstWheel + GearCorner(I);
    // The engine bay and the sump are worked on from in front of the car, the tank from its left and the
    // exhaust from behind.
    if (IsServicePoint(I))
    {
        const FVector Stand = I == TankSlot ? FVector(-76, -145, 10) : I == ExhaustSlot ? FVector(-285, -40, 10)
            : FVector(285, I == BatterySlot ? ARallyCarPawn::BatteryPoint().Y : I == SumpSlot ? ARallyCarPawn::SumpPoint().Y
                : I == GearboxSlot ? ARallyCarPawn::GearboxPoint().Y : 0.0, 10);
        return GetOwner()->GetActorTransform().TransformPosition(Stand);
    }
    if (IsModule(I) && Modules.IsValidIndex(I - FirstModule))
        return GetOwner()->GetActorTransform().TransformPosition(FVector(285, Modules[I - FirstModule]->GetRelativeLocation().Y, 10));
    if (IsWheel(I) && Wheels.IsValidIndex(I - FirstWheel))
    {
        FVector P = Wheels[I - FirstWheel]->GetRelativeLocation();
        P.Y = P.Y < 0.0 ? -145 : 145;
        P.Z = 10;
        return GetOwner()->GetActorTransform().TransformPosition(P);
    }
    if (!ValidPanel(I) || !Rest.IsValidIndex(I)) return GetOwner()->GetActorLocation();
    // Beside the car for doors and wings, ahead of or behind it for the rest.
    FVector P = Rest[I].GetLocation();
    if (I < 4 || I == 9 || I == 10) P.Y = P.Y < 0.0 ? -145 : 145;
    else P.X = P.X > 0.0 ? 285 : -285;
    P.Z = 10;
    return GetOwner()->GetActorTransform().TransformPosition(P);
}

bool URallyPanelComponent::CanReach(const ARallyCharacter* Character, int32 I) const
{
    if (!Character || Character->GetSeatedCar() || (!ValidPanel(I) && !IsWheel(I) && !IsGear(I) && !IsServicePoint(I) && !IsModule(I))
        || (ValidPanel(I) && !Rest.IsValidIndex(I)) || (IsBayPoint(I) && !IsBayOpen())
        || (IsModule(I) && !IsBayOpen() && !(ModuleKind(I) == EModule::Intercooler && IsDetached(7)))
        || GetOwner()->GetVelocity().Size() > 100.0f) return false;
    // Measured from the eyes (lower when crouching) to the part; the car's own box does not block.
    const FVector Eye = Character->GetPawnViewLocation();
    const FVector Part = GetPartLocation(I);
    if (FVector::Dist(Eye, Part) > ReachCm + LeanCm) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyPanelReach), false, Character);
    Params.AddIgnoredActor(GetOwner());
    if (Character->GetCarriedPart()) Params.AddIgnoredActor(Character->GetCarriedPart());
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Part, ECC_Visibility, Params);
}

namespace
{
    // Distance along a ray to a mesh's simple collision hulls (set up for loose parts), or -1.
    double RayToHulls(const UStaticMeshComponent* Mesh, const FVector& Start, const FVector& Direction, double Length)
    {
        // Visibility is no guide: dented panels draw an editable copy and hide this one. Callers leave out
        // parts that are off the car.
        const UStaticMesh* StaticMesh = Mesh ? Mesh->GetStaticMesh() : nullptr;
        const UBodySetup* Body = StaticMesh ? StaticMesh->GetBodySetup() : nullptr;
        if (!Body) return -1.0;
        const FTransform Frame = Mesh->GetComponentTransform();
        double Best = -1.0;
        for (const FKConvexElem& Hull : Body->AggGeom.ConvexElems)
        {
            const Chaos::FConvexPtr& Convex = Hull.GetChaosConvexMesh();
            if (!Convex.IsValid()) continue;
            const FTransform HullFrame = Hull.GetTransform() * Frame;
            const FVector LocalStart = HullFrame.InverseTransformPosition(Start);
            const FVector LocalDirection = HullFrame.InverseTransformVectorNoScale(Direction).GetSafeNormal();
            // Clip the ray against every face plane: it is inside the hull between the last plane it
            // enters through and the first it leaves through.
            double Enter = 0.0, Leave = Length;
            bool bMissed = Convex->NumPlanes() == 0;
            for (int32 Plane = 0; Plane < Convex->NumPlanes() && !bMissed; ++Plane)
            {
                const auto Face = Convex->GetPlane(Plane);
                const FVector Normal(Face.Normal());
                const double Height = FVector::DotProduct(LocalStart - FVector(Face.X()), Normal);
                const double Rate = FVector::DotProduct(LocalDirection, Normal);
                if (FMath::Abs(Rate) < 1e-9)
                {
                    bMissed = Height > 0.0;
                    continue;
                }
                const double Cross = -Height / Rate;
                if (Rate < 0.0) Enter = FMath::Max(Enter, Cross);
                else Leave = FMath::Min(Leave, Cross);
                bMissed = Enter > Leave;
            }
            if (!bMissed && (Best < 0.0 || Enter < Best))
                Best = Enter;
        }
        return Best;
    }

    // Distance along a ray to a sphere, or -1.
    double RayToSphere(const FVector& Start, const FVector& Direction, double Length, const FVector& Centre, double Radius)
    {
        const FVector ToCentre = Centre - Start;
        const double Along = FVector::DotProduct(ToCentre, Direction);
        const double MissSquared = ToCentre.SizeSquared() - Along * Along;
        if (MissSquared > Radius * Radius) return -1.0;
        const double Entry = Along - FMath::Sqrt(Radius * Radius - MissSquared);
        const double Hit = Entry >= 0.0 ? Entry : Along;
        return Hit >= 0.0 && Hit <= Length ? Hit : -1.0;
    }
}

double URallyPanelComponent::RayToParts(const FVector& Start, const FVector& Direction, double Length) const
{
    const FVector Dir = Direction.GetSafeNormal();
    double Best = -1.0;
    const auto Consider = [&](double Distance) { if (Distance >= 0.0 && (Best < 0.0 || Distance < Best)) Best = Distance; };
    for (int32 I = 0; I < Meshes.Num(); ++I)
        if (ValidPanel(I) && !IsDetached(I)) Consider(RayToHulls(Meshes[I], Start, Dir, Length));
    for (int32 C = 0; C < Wheels.Num(); ++C)
        if (WheelOn(FirstWheel + C)) Consider(RayToHulls(Wheels[C], Start, Dir, Length));
    for (int32 G = 0; G < Gear.Num(); ++G)
        if (!IsRemoved(FirstGear + G) && !IsRetired(FirstGear + G)) Consider(RayToHulls(Gear[G], Start, Dir, Length));
    return Best;
}

int32 URallyPanelComponent::FindTargetAlongRay(const FVector& Start, const FVector& Direction, double Length, bool bCrouched, double& OutDistance,
    int32* OutFixing) const
{
    if (OutFixing) *OutFixing = INDEX_NONE;
    const FVector Dir = Direction.GetSafeNormal();
    int32 Best = INDEX_NONE, BestUnder = INDEX_NONE;
    double BestDistance = Length, BestUnderDistance = Length;
    const auto Consider = [&](int32 Slot, double Distance)
    {
        if (Distance < 0.0) return;
        if (IsUnderCar(Slot) && bCrouched && Distance <= BestUnderDistance) { BestUnder = Slot; BestUnderDistance = Distance; }
        if (Distance <= BestDistance) { Best = Slot; BestDistance = Distance; }
    };
    for (int32 I = 0; I < Meshes.Num(); ++I)
        if (ValidPanel(I) && !IsDetached(I)) Consider(I, RayToHulls(Meshes[I], Start, Dir, Length));
    for (int32 C = 0; C < Wheels.Num(); ++C)
        if (WheelOn(FirstWheel + C)) Consider(FirstWheel + C, RayToHulls(Wheels[C], Start, Dir, Length));
    for (int32 G = 0; G < Gear.Num(); ++G)
        if (!IsRemoved(FirstGear + G) && !IsRetired(FirstGear + G)) Consider(FirstGear + G, RayToHulls(Gear[G], Start, Dir, Length));
    if (Exhaust && !IsRemoved(ExhaustSlot)) Consider(ExhaustSlot, RayToHulls(Exhaust, Start, Dir, Length));
    for (int32 M = 0; M < Modules.Num(); ++M)
        if (!IsRemoved(FirstModule + M)) Consider(FirstModule + M, RayToHulls(Modules[M], Start, Dir, Length));
    // Places without a mesh of their own: the engine bay's with the bonnet open, the underside's crouched.
    struct FSpot { int32 Slot; double Radius; };
    const FSpot Places[] = { { RadiatorSlot, 22.0 }, { BatterySlot, 16.0 }, { OilFillerSlot, 7.0 },
        { SumpSlot, 25.0 }, { GearboxSlot, 25.0 }, { TankSlot, 35.0 } };
    for (const FSpot& Spot : Places)
    {
        if (IsBayPoint(Spot.Slot) ? !IsBayOpen() : !bCrouched) continue;
        Consider(Spot.Slot, RayToSphere(Start, Dir, Length, GetPartLocation(Spot.Slot), Spot.Radius));
    }
    // Crouched and looking under the car, what is under there wins over the panels in front of it.
    if (BestUnder != INDEX_NONE)
    {
        Best = BestUnder;
        BestDistance = BestUnderDistance;
    }
    // A fixing in front of or just into what the ray meets; behind running gear at the same corner as
    // well (it sits tangled together and a hand reaches round).
    if (OutFixing && Spots.Num() > 0)
    {
        double Nearest = Length;
        int32 FixingSlot = INDEX_NONE;
        for (int32 Slot = 0; Slot < NumSlots; ++Slot)
        {
            if (NumFixings(Slot) == 0 || !FixingsPresent(Slot)) continue;
            // Running gear behind a wheel that is still on cannot be got at.
            if (IsGear(Slot) && GearKind(Slot) != EGear::TieRod && WheelOn(FirstWheel + GearCorner(Slot))) continue;
            const int32 Corner = IsWheel(Slot) ? Slot - FirstWheel : IsGear(Slot) ? GearCorner(Slot) : INDEX_NONE;
            double Limit = Length;
            if (Best != INDEX_NONE)
                Limit = FMath::Min(Length, BestDistance + (Slot != Best && IsGear(Slot) && IsGear(Best) && GearCorner(Slot) == GearCorner(Best) ? 15.0 : 6.0));
            // A front wing's collision hull fills its wheel arch; the arch itself is open.
            if ((Best == 9 && Corner == 0) || (Best == 10 && Corner == 1)) Limit = Length;
            for (int32 F = 0; F < NumFixings(Slot); ++F)
            {
                const double D = RayToSphere(Start, Dir, Limit, GetFixingLocation(Slot, F), Rally::Fixing::AimRadiusCm);
                if (D >= 0.0 && D < Nearest) { Nearest = D; FixingSlot = Slot; *OutFixing = F; }
            }
        }
        if (FixingSlot != INDEX_NONE)
        {
            OutDistance = Nearest;
            return FixingSlot;
        }
        // Aim assist: on a part within arm's length whose fixings can be got at, the nearest of them close to
        // the crosshair, so a bolt does not need a pixel-perfect aim.
        if (Best != INDEX_NONE && BestDistance <= Rally::Fixing::AssistReachCm && NumFixings(Best) > 0 && FixingsPresent(Best)
            && FixingAccess(Best).IsEmpty() && !(IsGear(Best) && WheelOn(FirstWheel + GearCorner(Best))))
        {
            double Closest = Rally::Fixing::AssistRadiusCm;
            for (int32 F = 0; F < NumFixings(Best); ++F)
            {
                const FVector At = GetFixingLocation(Best, F);
                const double Along = FVector::DotProduct(At - Start, Dir);
                if (Along < 0.0 || Along > BestDistance + 10.0) continue;
                const double Off = FVector::Dist(Start + Dir * Along, At);
                if (Off < Closest) { Closest = Off; *OutFixing = F; }
            }
            if (*OutFixing != INDEX_NONE)
            {
                OutDistance = BestDistance;
                return Best;
            }
        }
    }
    OutDistance = BestDistance;
    return Best;
}

FVector URallyPanelComponent::GetAimPoint(int32 Part, const FVector& Eye, bool bCrouched) const
{
    const FVector Centre = GetPartLocation(Part);
    const auto Hits = [&](const FVector& Point)
    {
        // As a player's crosshair finds things: a fixing in the way counts as its own part.
        double Distance = 0.0;
        int32 Fixing = INDEX_NONE;
        return FindTargetAlongRay(Eye, Point - Eye, ReachCm, bCrouched, Distance, &Fixing) == Part;
    };
    if (Hits(Centre)) return Centre;
    // Otherwise a point on its hull that is in view, nearest the middle first.
    const UStaticMeshComponent* Mesh = Part == ExhaustSlot ? Exhaust.Get() : FixingParent(Part);
    const UStaticMesh* StaticMesh = Mesh ? Mesh->GetStaticMesh() : nullptr;
    const UBodySetup* Body = StaticMesh ? StaticMesh->GetBodySetup() : nullptr;
    if (!Body) return Centre;
    TArray<FVector> Points;
    for (const FKConvexElem& Hull : Body->AggGeom.ConvexElems)
    {
        const FTransform HullFrame = Hull.GetTransform() * Mesh->GetComponentTransform();
        FVector Middle = FVector::ZeroVector;
        for (const FVector& V : Hull.VertexData) Middle += V;
        Middle /= FMath::Max(Hull.VertexData.Num(), 1);
        for (const FVector& V : Hull.VertexData) Points.Add(HullFrame.TransformPosition(FMath::Lerp(Middle, V, 0.7)));
    }
    Points.Sort([&](const FVector& A, const FVector& B) { return FVector::DistSquared(A, Centre) < FVector::DistSquared(B, Centre); });
    for (const FVector& Point : Points)
        if (Hits(Point)) return Point;
    return Centre;
}

FString URallyPanelComponent::PanelName(int32 I)
{
    static const TCHAR* Names[] = { TEXT("left front door"),TEXT("right front door"),TEXT("left rear door"),TEXT("right rear door"),
        TEXT("bonnet"),TEXT("boot lid"),TEXT("radiator"),TEXT("front bumper"),TEXT("rear bumper"),
        TEXT("left front wing"),TEXT("right front wing"),TEXT("left headlamp"),TEXT("right headlamp"),
        TEXT("left tail lamp"),TEXT("right tail lamp"),
        TEXT("front left wheel"),TEXT("front right wheel"),TEXT("rear left wheel"),TEXT("rear right wheel") };
    if (IsGear(I))
    {
        static const TCHAR* Kinds[] = { TEXT("strut"), TEXT("arm"), TEXT("brake disc"), TEXT("tie rod"), TEXT("driveshaft") };
        return FString::Printf(TEXT("%s %s"), CornerNames[GearCorner(I)], Kinds[static_cast<int32>(GearKind(I))]);
    }
    if (I == JackSlot) return TEXT("jack");
    if (I == FirstAidSlot) return TEXT("first-aid kit");
    if (I == DrinkSlot) return TEXT("bottle of slivovica");
    if (I == SmokeSlot) return TEXT("carton of cigarettes");
    if (I == WaterSlot) return TEXT("water can");
    if (I == BatterySlot) return TEXT("battery");
    if (I == OilBottleSlot) return TEXT("oil bottle");
    if (I == OilFillerSlot) return TEXT("oil filler");
    if (I == SumpSlot) return TEXT("sump");
    if (I == TankSlot) return TEXT("fuel tank");
    if (I == ExhaustSlot) return TEXT("exhaust");
    if (I == GearboxSlot) return TEXT("gearbox");
    if (IsModule(I))
    {
        static const TCHAR* ModuleNames[] = { TEXT("engine cover"), TEXT("air filter box"), TEXT("intake manifold"), TEXT("exhaust manifold"),
            TEXT("turbo"), TEXT("intercooler"), TEXT("alternator"), TEXT("starter motor"), TEXT("engine computer") };
        return ModuleNames[I - FirstModule];
    }
    return ValidPanel(I) || IsWheel(I) || I == RadiatorSlot ? Names[I] : TEXT("part");
}

FString URallyPanelComponent::PartState(int32 I) const
{
    const ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    if (!Car) return FString();
    const auto Health = [Car](ERallyPart Part) {
        FRallyPartCondition Condition;
        Car->GetDamage()->GetPartCondition(Part, Condition);
        return Condition;
    };
    if (IsWheel(I))
    {
        const int32 Corner = I - FirstWheel;
        const FRallyPartCondition Tyre = Health(static_cast<ERallyPart>(static_cast<int32>(ERallyPart::TyreFrontLeft) + Corner));
        return Tyre.Health <= 0.0f ? FString(TEXT("tyre flat")) : Tyre.Health < 1.0f ? FString::Printf(TEXT("tyre cut %.0f%%"), Tyre.Health * 100.0f) : FString();
    }
    if (!IsGear(I) || IsRetired(I)) return FString();
    const FRallyPartCondition Condition = Health(PartOfGear(I));
    const float H = Condition.Health;
    if (H >= 1.0f) return FString();
    const bool bFront = GearCorner(I) < 2;
    const FString Bodged = Condition.bBodged ? TEXT(" (bodged)") : TEXT("");
    switch (GearKind(I))
    {
    case EGear::Arm:
        return H <= 0.0f ? FString(bFront ? TEXT("snapped, the wheel no longer steers") : TEXT("snapped"))
            : FString::Printf(TEXT("bent %.0f%%, the wheel points out%s"), H * 100.0f, *Bodged);
    case EGear::Strut:
        return H <= 0.0f ? FString(TEXT("spring broken, the corner sags")) : FString::Printf(TEXT("leaking %.0f%%, soft and bouncy%s"), H * 100.0f, *Bodged);
    case EGear::Disc:
        return H <= 0.0f ? FString(TEXT("caliper smashed, hardly brakes")) : FString::Printf(TEXT("scored %.0f%%, brakes weakly%s"), H * 100.0f, *Bodged);
    case EGear::Driveshaft:
        return H <= 0.0f ? FString(TEXT("snapped, no drive")) : FString::Printf(TEXT("joint worn %.0f%%%s"), H * 100.0f, *Bodged);
    default:
        return FString();
    }
}

FString URallyPanelComponent::CarriedName(int32 I, bool bSpare)
{
    if (!bSpare) return PanelName(I);
    if (IsWheel(I)) return TEXT("spare wheel");
    if (I == WaterSlot) return TEXT("water can");
    if (I == OilBottleSlot) return TEXT("oil bottle");
    if (IsGear(I))
    {
        static const TCHAR* Kinds[] = { TEXT("strut"), TEXT("arm"), TEXT("brake disc"), TEXT("tie rod"), TEXT("driveshaft") };
        return FString::Printf(TEXT("spare %s %s"), GearCorner(I) < 2 ? TEXT("front") : TEXT("rear"), Kinds[static_cast<int32>(GearKind(I))]);
    }
    return PanelName(I);
}

int32 URallyPanelComponent::FitTarget(const ARallyCharacter* Character, const ARallyDebrisActor* Part) const
{
    if (!Character || !Part) return INDEX_NONE;
    const int32 Own = Part->GetPanel();
    // Its own place if in reach, else the nearest other place on the car it fits.
    const FVector Eye = Character->GetPawnViewLocation();
    const auto Best = [&](const TArray<int32>& Slots) {
        int32 Found = INDEX_NONE;
        double Nearest = TNumericLimits<double>::Max();
        for (const int32 Slot : Slots)
        {
            if (!CanReach(Character, Slot)) continue;
            if (Slot == Own) return Slot;
            const double Distance = FVector::DistSquared(Eye, GetPartLocation(Slot));
            if (Distance < Nearest) { Nearest = Distance; Found = Slot; }
        }
        return Found;
    };
    TArray<int32> Slots;
    if (IsFluid(Own))
    {
        // Poured in at the radiator's or the engine's filler while there is room and some left.
        const ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
        if (!Car || Part->GetHealth() <= 0.0f) return INDEX_NONE;
        const URallyEngineComponent* Engine = Car->GetEngine();
        const int32 Filler = Own == WaterSlot ? RadiatorSlot : OilFillerSlot;
        const bool bRoom = Own == WaterSlot ? Engine->GetCoolantL() < Rally::Engine::CoolantCapacityL - 0.05
            : Engine->GetOilL() < Rally::Underside::OilCapacityL - 0.05;
        return bRoom && CanReach(Character, Filler) ? Filler : INDEX_NONE;
    }
    if (Own == JackSlot)
    {
        if (JackCorner >= 0) return INDEX_NONE;
        for (int32 C = 0; C < 4; ++C) Slots.Add(FirstWheel + C);
        return Best(Slots);
    }
    if (IsWheel(Own))
    {
        // Any wheel fits any corner whose wheel is off and whose strut, arm and disc are there.
        for (int32 C = 0; C < 4; ++C)
            if (!WheelOn(FirstWheel + C) && CornerReady(C)) Slots.Add(FirstWheel + C);
        return Best(Slots);
    }
    if (IsGear(Own))
    {
        // A part fits its own kind on its own axle, on either side.
        const bool bFront = GearCorner(Own) < 2;
        for (int32 C = bFront ? 0 : 2; C < (bFront ? 2 : 4); ++C)
        {
            const int32 Slot = GearSlot(GearKind(Own), C);
            if (Slot != INDEX_NONE && IsRemoved(Slot)) Slots.Add(Slot);
        }
        return Best(Slots);
    }
    if (IsModule(Own)) return IsRemoved(Own) && CanReach(Character, Own) ? Own : INDEX_NONE;
    return IsDetached(Own) && CanReach(Character, Own) ? Own : INDEX_NONE;
}

void URallyPanelComponent::DebugPlaceJack(int32 Corner)
{
    if (!GetOwner()->HasAuthority()) return;
    ARallyDebrisActor* Jack = Conditions[JackSlot].Debris;
    if (!Jack)
        for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
            if (It->GetSourceCar() == GetOwner() && It->GetPanel() == JackSlot && !It->GetCarrier()) { Jack = *It; break; }
    if (!Jack) return;
    if (Corner < 0 || Corner > 3)
    {
        Jack->Mount(ERallyPartMount::Loose);
        Conditions[JackSlot].Debris = nullptr;
        JackCorner = -1;
    }
    else
    {
        JackCorner = static_cast<int8>(Corner);
        const FVector Foot = AssemblyOffset + ARallyCarPawn::JackPoint(JackCorner) - FVector(0, 0, ARallyCarPawn::JackLiftCm);
        Jack->Mount(ERallyPartMount::UnderCar, Foot, FRotator(0, JackCorner % 2 == 0 ? 90 : -90, 0));
        Conditions[JackSlot].Debris = Jack;
    }
    OnRep_Jack();
    GetOwner()->ForceNetUpdate();
}

bool URallyPanelComponent::LowerJack(ARallyCharacter* Character)
{
    if (!GetOwner()->HasAuthority() || JackCorner < 0 || !Character || Character->GetCarriedPart()
        || !CanReach(Character, FirstWheel + JackCorner)) return false;
    UE_LOG(LogTemp, Display, TEXT("RALLY_GEAR jack lowered %s"), CornerNames[JackCorner]);
    JackCorner = -1;
    if (ARallyDebrisActor* Jack = Conditions[JackSlot].Debris) Jack->Mount(ERallyPartMount::Loose);
    Conditions[JackSlot].Debris = nullptr;
    OnRep_Jack();
    GetOwner()->ForceNetUpdate();
    return true;
}

FString URallyPanelComponent::InteractionPrompt(const ARallyCharacter* Character, int32 I) const
{
    if (!CanReach(Character,I)) return FString();
    const ARallyDebrisActor* Carried = Character->GetCarriedPart();
    if (IsServicePoint(I) && !IsBayPoint(I))
    {
        // Under the car: what can be seen from outside it, and what can be done there.
        const ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
        if (Carried) return FString();
        FRallyPartCondition Condition;
        Car->GetDamage()->GetPartCondition(I == SumpSlot ? ERallyPart::OilSump : I == TankSlot ? ERallyPart::FuelTank
            : I == GearboxSlot ? ERallyPart::Gearbox : ERallyPart::Exhaust, Condition);
        const bool bCanBodge = !Condition.bBodged && Condition.Health < Rally::Damage::BodgeHealth;
        const FString NeedJack = FString::Printf(TEXT("   (jack up the %s to get under it)"), I == SumpSlot || I == GearboxSlot ? TEXT("front") : TEXT("rear"));
        if (I == ExhaustSlot)
        {
            if (IsRemoved(I) || !Rally::Underside::ExhaustHanging(Condition.Health)) return FString();
            return FString::Printf(TEXT("Exhaust hanging   %s   R: pull it off (%d tugs)"),
                IsJackedFor(I) ? *FString::Printf(TEXT("B: wire it up (straps: %d)"), Character->GetStrapCount()) : *NeedJack.TrimStart(),
                Rally::Panel::PullOffActions);
        }
        if (I == GearboxSlot)
        {
            if (Condition.Health >= 1.0f) return FString();
            FString Lost;
            for (int32 Number = 5; Number >= 1; --Number)
                if (!Rally::Underside::HasGear(Number, Condition.Health)) Lost += FString::Printf(TEXT("%s%d"), Lost.IsEmpty() ? TEXT("") : TEXT(","), Number);
            FString Prompt = Condition.Health <= 0.0f ? FString(TEXT("Gearbox broken: a workshop job"))
                : FString::Printf(TEXT("Gearbox damaged%s%s"), Lost.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", no %s"), *Lost),
                    Rally::Underside::PopOutRate(Condition.Health) > 0.0 ? TEXT(", jumps out of gear") : TEXT(""));
            if (bCanBodge && Condition.Health > 0.0f)
                Prompt += IsJackedFor(I) ? FString(TEXT("   B: free the gear linkage, top up the gearbox oil (10 mm spanner)")) : NeedJack;
            return Prompt;
        }
        const bool bLeaking = I == SumpSlot ? Rally::Underside::OilLeakRate(Condition.Health, true) > 0.0 : Rally::Underside::FuelLeakRate(Condition.Health) > 0.0;
        if (Condition.Health >= 1.0f) return FString();
        FString Prompt = I == SumpSlot ? (bLeaking ? TEXT("Sump cracked, oil dripping") : TEXT("Sump dented"))
            : (bLeaking ? TEXT("Fuel tank holed, fuel dripping") : TEXT("Fuel tank dented"));
        if (bCanBodge)
            Prompt += IsJackedFor(I) ? FString::Printf(TEXT("   B: patch it with putty (%d)"), Character->GetPutty()) : NeedJack;
        return Prompt;
    }
    if (IsBayPoint(I))
    {
        const ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
        const URallyEngineComponent* Engine = Car->GetEngine();
        if (Carried)
        {
            if (FitTarget(Character, Carried) != I) return FString();
            return I == OilFillerSlot
                ? FString::Printf(TEXT("E: pour in oil (%.1f L in the bottle, %.1f of %.1f L in)   G: drop"),
                    Carried->GetHealth() * OilBottleLitres, Engine->GetOilL(), Rally::Underside::OilCapacityL)
                : FString::Printf(TEXT("E: top up the coolant (%.1f L in the can, %.1f of %.0f L in)   G: drop"),
                    Carried->GetHealth() * WaterCanLitres, Engine->GetCoolantL(), Rally::Engine::CoolantCapacityL);
        }
        if (I == OilFillerSlot)
            return FString::Printf(TEXT("Oil %.1f of %.1f L on the dipstick"), Engine->GetOilL(), Rally::Underside::OilCapacityL);
        FRallyPartCondition Condition;
        Car->GetDamage()->GetPartCondition(I == RadiatorSlot ? ERallyPart::Radiator : ERallyPart::Battery, Condition);
        const bool bCanBodge = !Condition.bBodged && Condition.Health < Rally::Damage::BodgeHealth;
        if (I == BatterySlot)
            return bCanBodge ? TEXT("B: clean and tighten the battery terminals, tie it down (10 mm spanner)") : FString();
        // What you see under the bonnet: the level in the header tank and whether the core is weeping.
        FString Prompt = FString::Printf(TEXT("Coolant %.1f of %.0f L%s"), Engine->GetCoolantL(), Rally::Engine::CoolantCapacityL,
            Rally::Engine::LeakRate(Condition.Health, Engine->GetTempC(), true) > 0.0 ? TEXT(", radiator leaking") : TEXT(""));
        if (bCanBodge && Condition.Health > 0.0f)
            Prompt += FString::Printf(TEXT("   B: seal the leak (sealant: %d)"), Character->GetSealant());
        return Prompt;
    }
    if (IsWheel(I))
    {
        const int32 Corner = I - FirstWheel;
        if (Carried)
        {
            if (FitTarget(Character, Carried) != I) return FString();
            return Carried->GetPanel() == JackSlot ? FString::Printf(TEXT("E: put the jack under the %s corner   G: drop"), CornerNames[Corner])
                : FString(TEXT("E: offer it up onto the hub   G: drop"));
        }
        FString Prompt = JackCorner == Corner ? TEXT("E: lower the jack") : TEXT("");
        if (WheelOn(I) && !PartState(I).IsEmpty())
            Prompt += FString::Printf(TEXT("%s%s: %s"), Prompt.IsEmpty() ? TEXT("") : TEXT("   "), *PanelName(I), *PartState(I));
        if (WheelOn(I))
            Prompt += CountOut(I) < NumFixings(I)
                ? FString::Printf(TEXT("%s%s: %d of %d bolts out (17 mm spanner)"), Prompt.IsEmpty() ? TEXT("") : TEXT("   "), *PanelName(I), CountOut(I), NumFixings(I))
                : FString::Printf(TEXT("%sR: lift the %s off%s"), Prompt.IsEmpty() ? TEXT("") : TEXT("   "), *PanelName(I),
                    JackCorner == Corner ? TEXT("") : TEXT(" - no jack: it drops onto the hub"));
        return Prompt;
    }
    if (IsModule(I))
    {
        if (Carried)
            return FitTarget(Character, Carried) == I ? FString::Printf(TEXT("E: offer up the %s   G: drop"), *PanelName(I)) : FString();
        if (IsRemoved(I)) return FString();
        FString Prompt = PanelName(I);
        FRallyPartCondition Condition;
        if (PartOfModule(I) != ERallyPart::Count && Cast<ARallyCarPawn>(GetOwner())->GetDamage()->GetPartCondition(PartOfModule(I), Condition)
            && Condition.Health < 1.0f)
            Prompt += FString::Printf(TEXT(" (%.0f%%)"), Condition.Health * 100.0f);
        // What the engine under the cover sounds and smokes like.
        if (ModuleKind(I) == EModule::Cover)
        {
            const URallyEngineComponent* Engine = Cast<ARallyCarPawn>(GetOwner())->GetEngine();
            if (Engine && Engine->GetEngineHealth() < 1.0)
                Prompt += FString::Printf(TEXT(" - engine worn %.0f%%%s"), Engine->GetEngineHealth() * 100.0,
                    Engine->GetEngineHealth() < Rally::Engine::SmokeFromHealth ? TEXT(", burning oil") : TEXT(""));
        }
        const FString Access = FixingAccess(I);
        if (!Access.IsEmpty()) return Prompt + TEXT(": ") + Access;
        return CountOut(I) < NumFixings(I) ? FString::Printf(TEXT("%s: %d of %d fixings out"), *Prompt, CountOut(I), NumFixings(I))
            : FString::Printf(TEXT("%s, hanging loose   R: lift it out"), *Prompt);
    }
    if (IsGear(I))
    {
        if (Carried)
            return FitTarget(Character, Carried) == I ? FString::Printf(TEXT("E: offer up the %s   G: drop"), *PanelName(I)) : FString();
        if (IsRemoved(I)) return FString();
        const EGear Kind = GearKind(I);
        FRallyPartCondition Condition;
        Cast<ARallyCarPawn>(GetOwner())->GetDamage()->GetPartCondition(PartOfGear(I), Condition);
        // What it is and what shape it is in, then what can be done about it.
        const FString State = PartState(I);
        FString Prompt = State.IsEmpty() ? SlotLabel(I) : FString::Printf(TEXT("%s: %s"), *SlotLabel(I), *State);
        if (Kind == EGear::Arm && Condition.Health > 0.0f && Condition.Health < Rally::Damage::BodgeHealth)
            Prompt += TEXT("   B: hammer it roughly straight (hammer)");
        // It all sits behind the wheel.
        const FString Access = FixingAccess(I);
        if (!Access.IsEmpty())
            Prompt += FString::Printf(TEXT("   %s"), *Access);
        else
            Prompt += CountOut(I) < NumFixings(I) ? FString::Printf(TEXT("   %d of %d fixings out"), CountOut(I), NumFixings(I))
                : FString(TEXT("   R: lift it out"));
        return Prompt;
    }
    const auto S = Rally::Panel::Stage(Conditions[I].Fixings(), Clipped(I));
    if (IsDetached(I))
    {
        if (!Carried || Carried != Conditions[I].Debris) return FString();
        return Rally::Panel::CanRefit(Deform->MaxDisplacement(Meshes[I]))
            ? TEXT("E: offer it up   B: strap on   G: drop")
            : TEXT("Too crushed for bolts: B to strap on, or take to service   G: drop");
    }
    FString Prompt;
    if (!Carried && Rally::Panel::CanOpenByHand(Conditions[I].Fixings(), Clipped(I), Conditions[I].bJammed))
        Prompt = FString::Printf(TEXT("F: %s %s"), Conditions[I].bOpen ? TEXT("close") : TEXT("open"), *PanelName(I));
    if ((S == Rally::Panel::EStage::Loose || S == Rally::Panel::EStage::Hanging) && !Conditions[I].Strap)
        Prompt += FString::Printf(TEXT("%s%s: B to strap shut"), Prompt.IsEmpty() ? TEXT("") : TEXT("   "), *PanelName(I));
    FString Off;
    if (!Carried && I != 6)
    {
        if (Conditions[I].Strap) Off = TEXT("R: cut the strap");
        else if (S == Rally::Panel::EStage::Hanging) Off = FString::Printf(TEXT("R: pull the %s off (%d tugs)"), *PanelName(I), Rally::Panel::PullOffActions);
        else if (CountOut(I) == NumFixings(I) && (Clipped(I) || Conditions[I].bOpen)) Off = FString::Printf(TEXT("R: lift the %s off"), *PanelName(I));
        else if (CountOut(I) > 0) Off = FString::Printf(TEXT("%d of %d fixings out"), CountOut(I), NumFixings(I));
    }
    if (!Off.IsEmpty()) Prompt += (Prompt.IsEmpty() ? TEXT("") : TEXT("   ")) + Off;
    return Prompt;
}

bool URallyPanelComponent::WheelOn(int32 I) const
{
    const ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    FRallyPartCondition Hub;
    return IsWheel(I) && Car && Car->GetDamage()->GetPartCondition(static_cast<ERallyPart>(static_cast<int32>(ERallyPart::HubFrontLeft) + I - FirstWheel), Hub)
        && Hub.Health > 0.0f;
}

FVector URallyPanelComponent::GetPartLocation(int32 I) const
{
    if (IsWheel(I) && Wheels.IsValidIndex(I - FirstWheel)) return Wheels[I - FirstWheel]->GetComponentLocation();
    if (IsServicePoint(I))
        return GetOwner()->GetActorTransform().TransformPosition(AssemblyOffset
            + (I == BatterySlot ? ARallyCarPawn::BatteryPoint() + FVector(0, 0, 8) : I == OilFillerSlot ? OilFiller
            : I == SumpSlot ? ARallyCarPawn::SumpPoint() : I == TankSlot ? ARallyCarPawn::FuelTankPoint()
            : I == GearboxSlot ? ARallyCarPawn::GearboxPoint()
            : I == ExhaustSlot ? ARallyCarPawn::ExhaustPoint(2) : RadiatorFiller));
    if (IsGear(I) && Gear.IsValidIndex(I - FirstGear)) return Gear[I - FirstGear]->Bounds.Origin;
    if (IsModule(I) && Modules.IsValidIndex(I - FirstModule)) return Modules[I - FirstModule]->Bounds.Origin;
    if (Meshes.IsValidIndex(I)) return Meshes[I]->Bounds.Origin;
    return GetOwner()->GetActorLocation();
}

bool URallyPanelComponent::Remove(ARallyCharacter* Character, int32 I)
{
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    if (!GetOwner()->HasAuthority() || !Car || !Character || Character->GetCarriedPart() || !CanReach(Character, I)) return false;
    if (I == ExhaustSlot)
    {
        // A hanging exhaust is pulled off its front hanger by hand and left on the ground.
        FRallyPartCondition Condition;
        Car->GetDamage()->GetPartCondition(ERallyPart::Exhaust, Condition);
        if (IsRemoved(I) || !Exhaust || !Rally::Underside::ExhaustHanging(Condition.Health)) return false;
        if (!Character->AdvanceRemoval(Car, I, Rally::Panel::PullOffActions)) return true;
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ARallyDebrisActor* Part = GetWorld()->SpawnActor<ARallyDebrisActor>(ARallyDebrisActor::StaticClass(), Exhaust->GetComponentTransform(), Params);
        if (!Part) return false;
        FRallyDebrisSource Source;
        Source.Car = Car; Source.Mesh = Exhaust->GetStaticMesh(); Source.Panel = I; Source.MeshToCar = Exhaust->GetRelativeTransform();
        Source.MassKg = ExhaustMassKg; Source.Health = Condition.Health;
        Part->Launch(Source, {}, FVector::ZeroVector, FVector::ZeroVector);
        Conditions[I].bRemoved = true;
        Conditions[I].Debris = Part;
        OnRep_Conditions();
        GetOwner()->ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE exhaust pulled off (health %.2f)"), Condition.Health);
        return true;
    }
    if (IsGear(I))
    {
        // Behind the wheel (all but the tie rod), so the wheel comes off first; lifted out once every
        // fixing is undone.
        if (IsRemoved(I) || !FixingAccess(I).IsEmpty() || CountOut(I) < NumFixings(I)) return false;
        UStaticMeshComponent* Mesh = Gear[I - FirstGear];
        FRallyPartCondition Condition;
        Car->GetDamage()->GetPartCondition(PartOfGear(I), Condition);
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ARallyDebrisActor* Part = GetWorld()->SpawnActor<ARallyDebrisActor>(ARallyDebrisActor::StaticClass(), Mesh->GetComponentTransform(), Params);
        if (!Part) return false;
        FRallyDebrisSource Source;
        Source.Car = Car; Source.Mesh = Mesh->GetStaticMesh(); Source.Panel = I; Source.MeshToCar = Mesh->GetRelativeTransform();
        Source.MassKg = GearKind(I) == EGear::Strut ? 9.0f : GearKind(I) == EGear::Disc ? 7.0f : GearKind(I) == EGear::Driveshaft ? 6.0f
            : GearKind(I) == EGear::Arm ? 4.0f : 1.5f;
        Source.Health = Condition.Health;
        Source.Grade = Conditions[I].Grade;
        Part->Launch(Source, {}, FVector::ZeroVector, FVector::ZeroVector);
        Conditions[I].bRemoved = true;
        Conditions[I].Out = 0;
        Conditions[I].Debris = Part;
        // Missing behaves like broken until something is fitted.
        Car->GetDamage()->SetFittedPart(PartOfGear(I), 0.0f);
        OnRep_Conditions();
        GetOwner()->ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("RALLY_GEAR removed %s (health %.2f)"), *PanelName(I), Condition.Health);
    }
    else if (IsModule(I))
    {
        // Lifted out once every fixing is undone and what covers it is off.
        if (IsRemoved(I) || !FixingAccess(I).IsEmpty() || CountOut(I) < NumFixings(I)) return false;
        UStaticMeshComponent* Mesh = Modules[I - FirstModule];
        const ERallyPart Part = PartOfModule(I);
        FRallyPartCondition Condition;
        if (Part != ERallyPart::Count) Car->GetDamage()->GetPartCondition(Part, Condition);
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ARallyDebrisActor* Loose = GetWorld()->SpawnActor<ARallyDebrisActor>(ARallyDebrisActor::StaticClass(), Mesh->GetComponentTransform(), Params);
        if (!Loose) return false;
        static const float Masses[] = { 3.0f, 2.0f, 4.0f, 7.0f, 9.0f, 5.0f, 6.0f, 5.0f, 1.0f };
        FRallyDebrisSource Source;
        Source.Car = Car; Source.Mesh = Mesh->GetStaticMesh(); Source.Panel = I; Source.MeshToCar = Mesh->GetRelativeTransform();
        Source.MassKg = Masses[I - FirstModule]; Source.Health = Condition.Health;
        Source.Grade = Conditions[I].Grade;
        Loose->Launch(Source, {}, FVector::ZeroVector, FVector::ZeroVector);
        Conditions[I].bRemoved = true;
        Conditions[I].Out = 0;
        Conditions[I].Debris = Loose;
        if (Part != ERallyPart::Count) Car->GetDamage()->SetFittedPart(Part, 0.0f);
        OnRep_Conditions();
        GetOwner()->ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("RALLY_MODULE removed %s (health %.2f)"), *PanelName(I), Condition.Health);
    }
    else if (IsWheel(I))
    {
        // With the jack under that corner it stays up; without, it settles onto its hub.
        // Lifted off its hub once all four bolts are out.
        if (!WheelOn(I) || CountOut(I) < NumFixings(I)) return false;
        Conditions[I].Out = 0;
        Car->GetDamage()->ApplyPartDamage(static_cast<ERallyPart>(static_cast<int32>(ERallyPart::HubFrontLeft) + I - FirstWheel), 1.0f);
        DetachWheel(I - FirstWheel, false);
    }
    else
    {
        if (!ValidPanel(I) || I == 6 || IsDetached(I)) return false;
        FRallyPanelCondition& State = Conditions[I];
        const auto Before = Rally::Panel::Stage(State.Fixings(), Clipped(I));
        int32 Needed = 0;
        if (State.Strap) Needed = Rally::Panel::CutStrapActions;
        else if (Before == Rally::Panel::EStage::Hanging) Needed = Rally::Panel::PullOffActions;
        // Bolted on: lifted off once every fixing is undone (a door, bonnet or boot lid open to reach them).
        else if (CountOut(I) < NumFixings(I) || (!Clipped(I) && !State.bOpen)) return false;
        if (Needed > 0 && !Character->AdvanceRemoval(Car, I, Needed)) return true;
        if (State.Strap)
        {
            // Cutting the strap leaves whatever was holding it underneath.
            State.Strap = 0;
            UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL strap cut panel=%d"), I);
        }
        else
        {
            State.Clear();
            UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL removed panel=%d (%s)"), I,
                Before == Rally::Panel::EStage::Hanging ? TEXT("pulled off") : TEXT("unbolted"));
        }
        LogState(I, Before);
        if (IsDetached(I)) Detach(I);
        OnRep_Conditions();
        GetOwner()->ForceNetUpdate();
    }
    // Off it comes, into the hands of whoever took it off.
    if (ARallyDebrisActor* Part = Conditions[I].Debris) Part->PickUp(Character);
    return true;
}

bool URallyPanelComponent::Refit(ARallyCharacter* Character, ARallyDebrisActor* Part, bool bBodge)
{
    const int32 I = Part ? Part->GetPanel() : INDEX_NONE;
    ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
    if (IsFluid(I))
    {
        // Poured in at the filler; the can or bottle stays in hand with whatever is left.
        if (!GetOwner()->HasAuthority() || bBodge || !Car || Part->GetCarrier() != Character || Character->GetCarriedPart() != Part
            || FitTarget(Character, Part) == INDEX_NONE) return false;
        const double Size = I == WaterSlot ? WaterCanLitres : OilBottleLitres;
        const double Poured = I == WaterSlot ? Car->GetEngine()->TopUp(Part->GetHealth() * Size) : Car->GetEngine()->TopUpOil(Part->GetHealth() * Size);
        if (Poured <= 0.0) return false;
        Part->SetHealth(Part->GetHealth() - static_cast<float>(Poured / Size));
        UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE poured %.1f L from the %s (%.1f L left in it)"), Poured, *PanelName(I), Part->GetHealth() * Size);
        return true;
    }
    if (I == JackSlot || IsWheel(I) || IsGear(I) || IsModule(I))
    {
        if (!GetOwner()->HasAuthority() || bBodge || !Car || Part->GetCarrier() != Character || Character->GetCarriedPart() != Part) return false;
        const int32 Target = FitTarget(Character, Part);
        if (Target == INDEX_NONE) return false;
        if (I == JackSlot)
        {
            // Under the sill by that wheel; the car lifts that corner (ARallyCarPawn::SetJackCorner).
            JackCorner = static_cast<int8>(Target - FirstWheel);
            Part->Drop();
            const FVector Foot = AssemblyOffset + ARallyCarPawn::JackPoint(JackCorner) - FVector(0, 0, ARallyCarPawn::JackLiftCm);
            Part->Mount(ERallyPartMount::UnderCar, Foot, FRotator(0, JackCorner % 2 == 0 ? 90 : -90, 0));
            Conditions[JackSlot].Debris = Part;
            OnRep_Jack();
            GetOwner()->ForceNetUpdate();
            UE_LOG(LogTemp, Display, TEXT("RALLY_GEAR jack under %s"), CornerNames[JackCorner]);
            return true;
        }
        // Offered up onto its hub or mounts with its bolts started by hand; it holds once they are done up.
        // A wheel cannot be strapped on.
        if (IsWheel(Target))
        {
            const int32 Corner = Target - FirstWheel;
            Car->GetDamage()->CompleteValidatedRepair(static_cast<ERallyPart>(static_cast<int32>(ERallyPart::HubFrontLeft) + Corner), true);
            Car->GetDamage()->SetFittedPart(static_cast<ERallyPart>(static_cast<int32>(ERallyPart::TyreFrontLeft) + Corner), Part->GetHealth());
        }
        else if (IsModule(Target))
        {
            if (PartOfModule(Target) != ERallyPart::Count) Car->GetDamage()->SetFittedPart(PartOfModule(Target), Part->GetHealth());
            Conditions[Target].bRemoved = false;
        }
        else
        {
            Car->GetDamage()->SetFittedPart(PartOfGear(Target), Part->GetHealth());
            Conditions[Target].bRemoved = false;
        }
        Conditions[Target].Out = AllOutMask(Target);
        if (!IsWheel(Target))
        {
            Conditions[Target].Grade = Part->GetGrade();
            Conditions[Target].SetA = 0;
            Conditions[Target].SetB = 0;
        }
        FitTurns[Target] = 0;
        for (FRallyPanelCondition& Slot : Conditions)
            if (Slot.Debris == Part) Slot.Debris = nullptr;
        UE_LOG(LogTemp, Display, TEXT("RALLY_GEAR fitted %s at %s (health %.2f)"), *CarriedName(I, Part->IsSpare()), *PanelName(Target), Part->GetHealth());
        Part->Drop();
        Part->Destroy();
        OnRep_Conditions();
        GetOwner()->ForceNetUpdate();
        return true;
    }
    if (!GetOwner()->HasAuthority() || !ValidPanel(I) || !IsDetached(I) || !CanReach(Character,I)
        || Part != Conditions[I].Debris || Part->GetCarrier() != Character || Character->GetCarriedPart() != Part) return false;
    const double BentCm = Deform->MaxDisplacement(Meshes[I]);
    if (bBodge)
    {
        if (!Character->ConsumeStrap()) return false;
        Conditions[I].Strap = 180;
    }
    else
    {
        // Offered up in its opening with its fixings started by hand; it holds once they are done up
        // (TurnFixing), and how crooked it ends up is settled then (FinishFit).
        if (!Rally::Panel::CanRefit(BentCm)) return false;
        Conditions[I] = FRallyPanelCondition();
        Conditions[I].Out = AllOutMask(I);
    }
    // It never goes back on as it left the factory. Not facing the part squarely, a bent panel and
    // hurried bolting all leave it more crooked; a strap job hangs crooked whatever you do.
    const FVector MountWorld = GetOwner()->GetActorTransform().TransformPosition(Rest[I].GetLocation());
    const double AimDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(
        Character->GetBaseAimRotation().Vector(), (MountWorld - Character->GetPawnViewLocation()).GetSafeNormal()), -1.0, 1.0)));
    const double Sloppiness = bBodge ? Rally::Panel::FitSloppiness(1.0, AimDeg, BentCm, true) : 0.0;
    const FVector Normal = Mounts[I].Normal;
    FVector Shift = Normal * 0.8 + FVector(0, 0, -0.6);
    if (bBodge)
    {
        // Shifted in its plane and standing proud of the body rather than sunk into it; skewed about a random axis.
        Shift = FMath::VRand();
        Shift = (Shift - Normal * FVector::DotProduct(Shift, Normal)) * (Rally::Panel::FitGapCm * Sloppiness)
            + Normal * FMath::FRandRange(0.0, Rally::Panel::FitGapCm * Sloppiness * 0.6);
        Conditions[I].FitRotation = FQuat(FMath::VRand(), FMath::DegreesToRadians(Rally::Panel::FitSkewDeg * Sloppiness)).Rotator();
    }
    else
    {
        // On loose fixings it sags a little out of its opening until they are done up.
        OfferAimDeg[I] = AimDeg;
        FitTurns[I] = 0;
        Conditions[I].FitRotation = FRotator::ZeroRotator;
    }
    Conditions[I].FitOffset = Shift;
    Part->Drop();
    Part->Destroy();
    Conditions[I].Debris = nullptr;
    Conditions[I].bJammed = false;
    WearRemainder[I] = 0;
    Swing[I] = Droop[I] = {};
    LastClear[I] = FVector2D::ZeroVector;
    HandTarget[I] = GuideTime[I] = 0.0;
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL %s panel=%d sloppiness=%.2f aim=%.0f shift=%.1fcm stage=%s"),
        bBodge ? TEXT("strapped on") : TEXT("offered up"), I, Sloppiness, AimDeg, Shift.Size(),
        StageName(Rally::Panel::Stage(Conditions[I].Fixings(), Clipped(I))));
    return true;
}

bool URallyPanelComponent::StrapPanel(ARallyCharacter* Character, int32 I)
{
    if (IsServicePoint(I) && I != OilFillerSlot)
    {
        // Bodges to the usual level: radiator sealant from the tin (a crushed-flat core is beyond it), the
        // battery's terminals cleaned and tightened and the battery tied down, putty over a cracked sump or
        // holed tank, a strap round a hanging exhaust.
        ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
        if (!GetOwner()->HasAuthority() || !Car || !CanReach(Character, I)) return false;
        const ERallyPart Part = I == RadiatorSlot ? ERallyPart::Radiator : I == BatterySlot ? ERallyPart::Battery
            : I == SumpSlot ? ERallyPart::OilSump : I == TankSlot ? ERallyPart::FuelTank : I == GearboxSlot ? ERallyPart::Gearbox
            : ERallyPart::Exhaust;
        if (IsUnderCar(I) && !IsJackedFor(I)) return false;
        FRallyPartCondition Condition;
        Car->GetDamage()->GetPartCondition(Part, Condition);
        if (Condition.bBodged || Condition.Health >= Rally::Damage::BodgeHealth) return false;
        if (I == ExhaustSlot && (IsRemoved(I) || !Rally::Underside::ExhaustHanging(Condition.Health))) return false;
        if ((I == RadiatorSlot || I == GearboxSlot) && Condition.Health <= 0.0f) return false;
        const bool bSupplies = I == RadiatorSlot ? Character->ConsumeSealant()
            : I == BatterySlot || I == GearboxSlot ? Character->GetHeldTool() == Rally::Fixing::ETool::Spanner10
            : I == ExhaustSlot ? Character->ConsumeStrap() : Character->ConsumePutty();
        if (!bSupplies || !Car->GetDamage()->CompleteValidatedRepair(Part, false)) return false;
        UE_LOG(LogTemp, Display, TEXT("RALLY_ENGINE %s bodged (%.2f -> 0.60)"), *PanelName(I), Condition.Health);
        return true;
    }
    if (IsGear(I))
    {
        // A bent arm hammered roughly straight: a bodge.
        const EGear Kind = GearKind(I);
        ARallyCarPawn* Car = Cast<ARallyCarPawn>(GetOwner());
        if (!GetOwner()->HasAuthority() || !Car || Kind != EGear::Arm || IsRemoved(I)
            || !CanReach(Character, I) || Character->GetHeldTool() != Rally::Fixing::ETool::Hammer) return false;
        FRallyPartCondition Condition;
        Car->GetDamage()->GetPartCondition(PartOfGear(I), Condition);
        if (Condition.Health <= 0.0f || !Car->GetDamage()->CompleteValidatedRepair(PartOfGear(I), false)) return false;
        UE_LOG(LogTemp, Display, TEXT("RALLY_GEAR hammered %s straight (%.2f -> bodge)"), *PanelName(I), Condition.Health);
        return true;
    }
    if (!GetOwner()->HasAuthority() || !ValidPanel(I) || !CanReach(Character,I) || IsDetached(I) || Conditions[I].Strap) return false;
    const auto S = Rally::Panel::Stage(Conditions[I].Fixings(), Clipped(I));
    if (S == Rally::Panel::EStage::Intact || !Character->ConsumeStrap()) return false;
    Conditions[I].Strap = 180;
    Conditions[I].bOpen = false;
    WearRemainder[I] = 0;
    GetOwner()->ForceNetUpdate();
    return true;
}

bool URallyPanelComponent::ToggleOpen(ARallyCharacter* Character, int32 I)
{
    if (!GetOwner()->HasAuthority() || !ValidPanel(I) || IsDetached(I) || !Character || Character->GetCarriedPart()
        || !CanReach(Character, I)) return false;
    FRallyPanelCondition& State = Conditions[I];
    if (!Rally::Panel::CanOpenByHand(State.Fixings(), Clipped(I), State.bJammed)) return false;
    State.bOpen = !State.bOpen;
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL open panel=%d open=%d"), I, State.bOpen ? 1 : 0);
    return true;
}

bool URallyPanelComponent::ToggleOpenFromInside(int32 I)
{
    if (!GetOwner()->HasAuthority() || I < 0 || I > 3 || IsDetached(I)) return false;
    FRallyPanelCondition& State = Conditions[I];
    if (!Rally::Panel::CanOpenByHand(State.Fixings(), Clipped(I), State.bJammed)) return false;
    State.bOpen = !State.bOpen;
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("RALLY_PANEL open panel=%d open=%d (from seat)"), I, State.bOpen ? 1 : 0);
    return true;
}

ARallyDebrisActor* URallyPanelComponent::GetDebris(int32 I) const
{
    return Conditions.IsValidIndex(I) ? Conditions[I].Debris : nullptr;
}

void URallyPanelComponent::DebugDetach(int32 I)
{
    if (!GetOwner()->HasAuthority() || !ValidPanel(I) || IsDetached(I)) return;
    const auto Before = Rally::Panel::Stage(Conditions[I].Fixings(), Clipped(I));
    Conditions[I].Clear();
    LogState(I, Before);
    Detach(I);
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
}

void URallyPanelComponent::DebugBreakLatch(int32 I)
{
    if (!GetOwner()->HasAuthority() || !ValidPanel(I) || Clipped(I) || IsDetached(I)) return;
    const auto Before = Rally::Panel::Stage(Conditions[I].Fixings(), Clipped(I));
    Conditions[I].Latch = 0;
    Conditions[I].bOpen = false;
    LogState(I, Before);
    OnRep_Conditions();
    GetOwner()->ForceNetUpdate();
}

void URallyPanelComponent::RepairAll()
{
    if (!GetOwner()->HasAuthority()) return;
    // Every loose part of this car goes, the jack comes down, and the boot kit is restocked.
    for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
        if (It->GetSourceCar() == GetOwner()) It->Destroy();
    JackCorner = -1;
    OnRep_Jack();
    for (int32 I = 0; I < Conditions.Num(); ++I)
    {
        if (IsValid(Conditions[I].Debris)) Conditions[I].Debris->Destroy();
        const uint8 Grade = Conditions[I].Grade, SetA = Conditions[I].SetA;
        const int8 SetB = Conditions[I].SetB;
        Conditions[I] = FRallyPanelCondition();
        Conditions[I].Grade = Grade;
        Conditions[I].SetA = SetA;
        Conditions[I].SetB = SetB;
        if (WearRemainder.IsValidIndex(I)) WearRemainder[I] = 0;
        if (Swing.IsValidIndex(I)) Swing[I] = Droop[I] = {};
        if (LastClear.IsValidIndex(I)) LastClear[I] = FVector2D::ZeroVector;
        if (HandTarget.IsValidIndex(I)) HandTarget[I] = GuideTime[I] = 0.0;
        if (Rest.IsValidIndex(I)) Meshes[I]->SetRelativeTransform(Rest[I]);
    }
    OnRep_Conditions();
    SpawnSpares();
    GetOwner()->ForceNetUpdate();
}
