#pragma once

// How each part of the car gives in a crash: the same impact bends a plastic bumper, a thin bonnet,
// a door with side-impact beams and a windscreen differently. Visual tuning only; part damage and
// fixings use ImpactRules.h and PanelRules.h. No engine dependencies.
namespace Rally::Crush
{
    // Chunky crumples: a dent's shape is sampled on a grid this fine (cm, in the car's frame) and blended linearly
    // between, so it breaks into flat facets with straight creases rather than a smooth wobbly bowl.
    constexpr double FacetCm = 12.0;

    struct FProfile
    {
        // How far it gives compared with the body shell (1).
        double Give = 1.0;
        // Squeezed lengthwise, sheet folds into creases about this far apart (cm), this pronounced.
        double FoldWavelength = 28.0;
        double Folding = 1.0;
        // Share of the dent that springs back out afterwards (plastic recovers, steel does not).
        double Springback = 0.0;
        // Total crush (cm) after which a point gives no further.
        double MaxCrushCm = 40.0;
        // How readily its finish marks: paint scuffs, glass crazes, a lamp lens cracks.
        double Scuff = 1.0;
    };

    // What a deformable mesh is.
    enum class EKind : int
    {
        Shell, Door, Bonnet, BootLid, Radiator, Bumper, Fender, Lamp, Roof, Glass, BumperBar, Interior, Engine
    };

    inline FProfile ForKind(EKind Kind)
    {
        switch (Kind)
        {
        case EKind::Door:
            // Side-impact beams: shallower dents, tighter creases.
            return { 0.75, 24.0, 0.9, 0.05, 30.0, 1.0 };
        case EKind::Bonnet:
            // Thin and wide: folds into a few big creases and tents up.
            return { 1.05, 34.0, 1.5, 0.0, 45.0, 1.0 };
        case EKind::BootLid:
            // Boot lid round the rear window: a little stiffer than the bonnet.
            return { 0.9, 30.0, 1.2, 0.0, 40.0, 1.0 };
        case EKind::Radiator:
            // Radiator core: soft, crushes in fine folds.
            return { 1.25, 10.0, 0.7, 0.0, 30.0, 0.6 };
        case EKind::Bumper:
            // Plastic bumper cover: bends in broad warps rather than creasing, springs about halfway
            // back, and scuffs easily.
            return { 1.0, 50.0, 0.35, 0.45, 25.0, 1.6 };
        case EKind::Fender:
            // Thin bolt-on wing: crumples readily into tight creases.
            return { 1.1, 24.0, 1.3, 0.0, 40.0, 1.0 };
        case EKind::Lamp:
            // Plastic housing and lens: barely bends before it cracks.
            return { 0.7, 20.0, 0.2, 0.0, 15.0, 2.5 };
        case EKind::Roof:
            // Held up by the pillars: gives a little less than the shell.
            return { 0.85, 30.0, 1.1, 0.0, 35.0, 1.0 };
        case EKind::Glass:
            // Bonded windscreen: sags a little with its frame, crazes long before it bends.
            return { 0.5, 60.0, 0.1, 0.0, 12.0, 2.5 };
        case EKind::BumperBar:
            // Steel beam behind the bumper cover: stiff, bends in one broad curve.
            return { 0.55, 45.0, 0.5, 0.0, 25.0, 0.5 };
        case EKind::Interior:
            // Seats, dashboard and trim: pushed in a little, never creased.
            return { 0.35, 40.0, 0.4, 0.0, 20.0, 0.3 };
        case EKind::Engine:
            // Engine and gearbox castings: hardly move; hoses and covers ride along.
            return { 0.15, 40.0, 0.2, 0.0, 10.0, 0.3 };
        default:
            return {};
        }
    }

    // The car's panel layout (URallyPanelComponent): 0-3 doors (front left, front right, rear left,
    // rear right), 4 bonnet, 5 boot lid, 6 radiator, 7-8 bumpers, 9-10 front wings, 11-12 headlamps,
    // 13-14 tail lamps.
    inline EKind KindOfPanel(int Panel)
    {
        if (Panel >= 0 && Panel <= 3) return EKind::Door;
        switch (Panel)
        {
        case 4: return EKind::Bonnet;
        case 5: return EKind::BootLid;
        case 6: return EKind::Radiator;
        case 7: case 8: return EKind::Bumper;
        case 9: case 10: return EKind::Fender;
        case 11: case 12: case 13: case 14: return EKind::Lamp;
        default: return EKind::Shell;
        }
    }
}
