"""configs.json: which parts from which kit make each car. Built from the kits' manifests (plain python, no bpy)."""
import json, os
ROOT = os.path.dirname(os.path.abspath(__file__))
eng = json.load(open("/home/claude/octavia18t/export/parts_manifest.json"))["parts"]
sus = json.load(open("/home/claude/octavia_susp/export/parts_manifest.json"))["parts"]
drv = json.load(open(os.path.join(ROOT, "export", "parts_manifest.json")))["parts"]
ENGINE_REPLACED = ["CatalyticConverter", "CentreSilencer", "RearSilencer", "FuelTank"]
REAR_4X4 = ("HubCarrier_", "TrailingArm_", "LowerLink_", "UpperLink_", "Driveshaft_R", "CVJoint_Inner_R", "CVJoint_Outer_R",
            "CVBoot_Inner_R", "CVBoot_Outer_R", "AntiRollBarLink_R")
REAR_4X4_SHARED = ("RearAntiRollBar", "RearSubframe", "RearFinalDrive")
sus_names = [p["name"] for p in sus]
s4 = [n for n in sus_names if n.startswith(REAR_4X4) or n in REAR_4X4_SHARED]
s_common = [n for n in sus_names if n not in s4]
d = {p["name"]: p for p in drv}
common = dict(
    body="Octavia1U_Body_v2 (BODY_KIT, all parts)", interior="Octavia1U_Interior_v2 (INTERIOR_KIT, all parts)",
    engine=[p["name"] for p in eng if p["name"] not in ENGINE_REPLACED],
    suspension=s_common,
    drivetrain=["Exhaust_Cat", "Exhaust_CentreSilencer", "Exhaust_RearSilencer", "FuelFillerNeck"])
cfg = {
    "note": "Each car = common + its own list. Part names are object names in the kits' blend/FBX files; fasteners, bolt points and "
            "UCX_ hulls come with their parent part. The engine set's CatalyticConverter, CentreSilencer, RearSilencer and FuelTank are "
            "not used: they were laid out for a different floor; the drivetrain kit's exhaust and tanks replace them in both cars.",
    "common": common,
    "Octavia_FWD": dict(drive="front", gearbox="Gearbox_02J (engine set) without the angle drive",
                        suspension=[], drivetrain=["FuelTank_FWD", "TwistBeam", "PivotBracket_RL", "PivotBracket_RR", "RearHub_RL", "RearHub_RR"]),
    "Octavia_4x4": dict(drive="all wheels (Haldex)", gearbox="Gearbox_02J (engine set) + AngleDrive",
                        suspension=s4, drivetrain=["AngleDrive", "Propshaft_Front", "PropshaftCentreBearing", "Propshaft_Rear", "FuelTank_4x4"]),
}
for k in ("Octavia_FWD", "Octavia_4x4"):
    for n in cfg[k]["drivetrain"]: assert n in d, n
cfg["triangles"] = {
    "Octavia_FWD_kit_parts": sum(d[n]["triangles"] for n in common["drivetrain"] + cfg["Octavia_FWD"]["drivetrain"]),
    "Octavia_4x4_kit_parts": sum(d[n]["triangles"] for n in common["drivetrain"] + cfg["Octavia_4x4"]["drivetrain"])}
json.dump(cfg, open(os.path.join(ROOT, "export", "configs.json"), "w"), indent=1)
print(len(common["engine"]), "engine parts,", len(s_common), "common suspension,", len(s4), "4x4-only suspension")
print(cfg["triangles"])
