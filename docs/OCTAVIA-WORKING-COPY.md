# Organized Octavia working copy

Open Art/Vehicles/Octavia/Working/Octavia_Liftback_Working.blend.
The original GLB and source inspection blend are unchanged.

## Collections in the Outliner

- 01_Body: selected liftback shell, doors, bonnet, trunk, glass and exterior detail.
- 02_Interior: one provisional manual interior with seats and dashboard.
- 03_Engine: selected turbo engine, cooling, exhaust and fuel components.
- 04_Suspension: front components and torsion-beam rear arrangement.
- 90_Alternatives_HIDDEN: estate, WRC and other alternative parts.
- 91_LineHelpers_HIDDEN: non-surface helper geometry.
- 00_Roots and 99_Preview: import transforms and inspection camera.

Expand a collection to browse parts. Mesh names begin with their source part name.
Parts retain their original empty-parent groups: select the group and children when
moving a whole assembly. Mesh pieces were not joined, deleted or re-positioned.
Enable hidden collections with their monitor/camera icons only when comparing alternatives;
hide the existing equivalent before displaying a replacement to avoid overlaps.
Use the eye icon to temporarily hide a visible collection while inspecting the engine or cabin.

## Verification

All 618 imported objects preserved, including the inspection camera.
All 418,881 imported Blender polygons preserved. Blender's imported polygon total
is distinct from the original glTF triangle count.
All source object transforms checked during collection organization.
Visible selection: 162 surface mesh objects, 175,168 triangles.
Front and rear solid-shaded renders inspected; the overlapping estate body and floating
roof alternative are no longer visible.
organization-manifest.json records every object's source name and new collection.

## Still pending

No road wheels, rig, gameplay collision or Unreal vehicle integration yet.
Viewport colours are inspection aids, not final materials or validated glass shading.
The selected facelift/vRS-style configuration is provisional, not an exact stock-car reconstruction.
Source asset provenance still needs verification before distribution; see OCTAVIA-ASSET-REVIEW.md.