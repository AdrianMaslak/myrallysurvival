"""Bring the painted livery into Unreal (docs/PAINTING.md).
Run via scripts/Invoke-EditorPython.ps1 -Script ./scripts/import_livery.py -PassMarker LIVERY_IMPORT_PASS
(editor closed), after saving Art/Vehicles/Octavia/Prototype/Textures/T_Octavia_Livery.png.

Imports the PNG over /Game/Vehicles/Octavia/Prototype/T_Octavia_Livery and, the first time, makes
M_Prototype_Paint read it: the undamaged colour going into the damage Lerp (scripts/setup_damage_assets.py,
which scrapes to bare steel on vertex colour R) becomes the texture sampled on UV channel 1, the Livery map
the export writes. Safe to run again: later runs only reimport the image.
"""
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Art/Vehicles/Octavia/Prototype/Textures/T_Octavia_Livery.png"
DEST = "/Game/Vehicles/Octavia/Prototype"
NAME = "T_Octavia_Livery"
lib = unreal.MaterialEditingLibrary

if not SOURCE.exists():
    raise RuntimeError(f"Missing {SOURCE}; run scripts/add_livery_uvs.py in Blender first.")
task = unreal.AssetImportTask()
task.filename = str(SOURCE)
task.destination_path = DEST
task.destination_name = NAME
task.automated = True
task.replace_existing = True
task.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture = unreal.load_asset(f"{DEST}/{NAME}")
if not isinstance(texture, unreal.Texture2D):
    raise RuntimeError("Livery texture import failed")
texture.set_editor_property("srgb", True)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_VEHICLE)
unreal.EditorAssetLibrary.save_loaded_asset(texture, False)

material = unreal.load_asset(f"{DEST}/M_Prototype_Paint")
lerp = lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
if not isinstance(lerp, unreal.MaterialExpressionLinearInterpolate):
    raise RuntimeError("M_Prototype_Paint has no damage Lerp; run scripts/setup_damage_assets.py first.")
inputs = lib.get_inputs_for_material_expression(material, lerp)
current = inputs[1]
if isinstance(current, unreal.MaterialExpressionTextureSample):
    note = "reimported the image"
else:
    previous = current.get_editor_property("constant") if isinstance(current, unreal.MaterialExpressionConstant3Vector) else None
    coords = lib.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1100, -150)
    coords.set_editor_property("coordinate_index", 1)
    sample = lib.create_material_expression(material, unreal.MaterialExpressionTextureSample, -800, -150)
    sample.set_editor_property("texture", texture)
    lib.connect_material_expressions(coords, "", sample, "UVs")
    # The texture becomes the Lerp's undamaged input; the old colour node is left unconnected.
    if not lib.connect_material_expressions(sample, "", lerp, "B"):
        raise RuntimeError("Could not connect the livery to the paint")
    note = f"paint now reads the livery on UV 1 (previous colour {previous})"
lib.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
    raise RuntimeError("Could not save M_Prototype_Paint")
unreal.log(f"LIVERY_IMPORT_PASS: {note}")
