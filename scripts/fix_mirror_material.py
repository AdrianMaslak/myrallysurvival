"""One-off repair of M_Body_Mirror made before import_body_v2.py was fixed: its picture sampler pointed at
/Engine/EngineResources/GrayTexture, which does not exist in 5.8, so the material failed to compile and the mirrors drew
the default checker. Points the sampler at Black and recompiles. Safe to run again.
Run via scripts/Invoke-EditorPython.ps1 -Script scripts/fix_mirror_material.py -PassMarker MIRROR_FIX_PASS."""
import unreal

lib = unreal.MaterialEditingLibrary
material = unreal.load_asset("/Game/Vehicles/Octavia/Body_v2/M_Body_Mirror")
black = unreal.load_asset("/Engine/EngineResources/Black")
assert material and black, "missing material or Black texture"
fixed = 0
for node in lib.get_material_expressions(material) if hasattr(lib, "get_material_expressions") else []:
    if isinstance(node, unreal.MaterialExpressionTextureSampleParameter2D) and str(node.get_editor_property("parameter_name")) == "Mirror":
        node.set_editor_property("texture", black)
        fixed += 1
if not fixed:
    # Older API: walk the material's expression collection.
    for node in material.get_editor_property("expressions"):
        if isinstance(node, unreal.MaterialExpressionTextureSampleParameter2D) and str(node.get_editor_property("parameter_name")) == "Mirror":
            node.set_editor_property("texture", black)
            fixed += 1
assert fixed == 1, f"Mirror sampler not found ({fixed})"
lib.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material, False)
unreal.log("MIRROR_FIX_PASS")
