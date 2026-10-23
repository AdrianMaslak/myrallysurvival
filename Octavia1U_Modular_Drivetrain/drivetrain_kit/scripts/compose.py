"""config scenes from assembly + drivetrain kit. usage: -- fwd|4x4"""
import bpy, sys
cfg = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.open_mainfile(filepath="/home/claude/octavia_drive/work/assembly.blend")
O = bpy.data.objects
ENGINE_DROP = {"CatalyticConverter", "CentreSilencer", "RearSilencer", "FuelTank"}   # replaced by the kit (did not fit the car)
REAR_4X4 = {"HubCarrier", "TrailingArm", "LowerLink", "UpperLink", "Driveshaft", "CVJoint", "CVBoot", "RearAntiRollBar", "AntiRollBarLink",
            "RearSubframe", "RearFinalDrive"}
for o in list(O):
    if o.name in ENGINE_DROP: O.remove(o, do_unlink=True); continue
    if cfg == "fwd" and o.name in [c.name for c in bpy.data.collections["SUSP"].objects]:
        base = o.name.rsplit("_", 1)[0] if o.name[-3:-2] == "_" else o.name
        rear = o.name.endswith(("_RL", "_RR")) or o.name in ("RearAntiRollBar", "RearSubframe", "RearFinalDrive")
        if rear and any(o.name.startswith(k) for k in REAR_4X4): O.remove(o, do_unlink=True)
want = {"4x4": ("Drivetrain4x4", "Exhaust", "FuelSystem"), "fwd": ("Exhaust", "FuelSystem", "RearAxleFWD")}[cfg]
skipn = ("FuelTank_FWD",) if cfg == "4x4" else ("FuelTank_4x4",)
path = "/home/claude/octavia_drive/export/Octavia1U_Drivetrain.blend"
with bpy.data.libraries.load(path, link=False) as (src, dst):
    dst.collections = [c for c in src.collections if c in want]
for c in dst.collections:
    bpy.context.scene.collection.children.link(c)
    for o in list(c.objects):
        if o.name.startswith(skipn) or "UCX_" in o.name or "BoltPoint" in o.name: O.remove(o, do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=f"/home/claude/octavia_drive/work/config_{cfg}.blend")
print("saved", cfg, len(bpy.data.objects))
