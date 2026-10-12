# Painting the car

How the Octavia's paint takes a livery, where to paint it, and how it gets into the game.

## How it works

- Every body panel that uses M_Prototype_Paint (shell, doors, bonnet, boot lid, wings, bumper covers,
  mirrors, roof, sills) reads one image, Art/Vehicles/Octavia/Prototype/Textures/T_Octavia_Livery.png,
  4096 x 4096, through a UV map called Livery.
- The Livery map is a blueprint: each painted face is projected from the side it faces most, and the
  views share one scale (5.9 px per cm; the car is 4.65 x 2.01 x 1.59 m). Top to bottom in the image:
  - left side, seen from outside: front of the car on the left;
  - right side, seen from outside: front on the right;
  - top: front on the right, the car's left side at the top;
  - front (car's left on your right, as standing in front of it) and rear (car's right on your right).
  Both sides are laid out as seen from outside, so text reads the right way on both; nothing is mirrored.
- Faces at about 45 degrees (bonnet edges, bumper corners, the roof's edges) are stretched or split
  between two views. Keep numbers, logos and lettering on flat areas: doors, bonnet, roof, boot lid.
- Faces pointing down share the top view, and inner faces (door shuts, the engine bay, inside the wings)
  share their side's view: paint shows there too, only where nobody looks.
- Damage keeps working: dents carry the paint with them, and scrapes show bare steel over the livery
  (vertex colour R, scripts/setup_damage_assets.py).
- Glass, lamps, tyres, rims, interior and mechanical parts have their own plain materials and are not
  part of the livery.
- The map was made once by scripts/add_livery_uvs.py, which refuses to run again: painted work depends
  on the map staying where it is. Do not edit the Livery UV map by hand.

## Painting in 2D (Krita, Photoshop, GIMP, Affinity)

1. Open T_Octavia_Livery_Template.png (same folder, 4096 x 4096): the car's painted faces and edges in
   each view, transparent around them.
2. Keep it as the top layer (Multiply, or 30-50% opacity) as a guide, and paint on layers below it. Start
   from T_Octavia_Livery.png (plain paint colour) or a fill of your own base colour.
3. Hide the template and export over T_Octavia_Livery.png: PNG, 4096 x 4096, RGB without transparency,
   8 bits, sRGB. Keep the layered file next to it (for example Livery_Source.kra); add its extension to
   .gitattributes as LFS if it should be committed.

## Painting on the 3D car (Blender)

1. Open Art/Vehicles/Octavia/Prototype/Octavia_With_Wheels.blend.
2. Set Render Properties > Color Management > View Transform to Standard, so colours look as they will in
   the game (the default AgX shifts them).
3. Select a body panel (a door, the bonnet, the shell), switch to the Texture Paint workspace. The paint
   material already shows T_Octavia_Livery through the Livery map and has it as its paint target, so the
   brush paints straight onto the car. Blender paints one object at a time: select the next panel to
   carry on. The Image Editor next to it paints the same image flat, with the blueprint layout.
4. For logos and numbers use a brush texture with Stencil mapping, or paint them in 2D.
5. Image > Save (Alt+S) writes the PNG. Saving the .blend is not needed for paint alone.
6. Do not add new materials to the car: the import into Unreal refuses meshes that bring new ones.

## Getting it into the game

- With Unreal closed:
  `powershell -ExecutionPolicy Bypass -File scripts\Invoke-EditorPython.ps1 -Script .\scripts\import_livery.py -PassMarker LIVERY_IMPORT_PASS`
  It imports the PNG as /Game/Vehicles/Octavia/Prototype/T_Octavia_Livery and, the first time, makes
  M_Prototype_Paint read it on UV channel 1. Later runs only reimport the image.
- With Unreal open, after that first run: Content Browser, /Game/Vehicles/Octavia/Prototype,
  T_Octavia_Livery, right-click, Reimport.
- Already done once (2026-10-12): the meshes were reimported with their Livery channel and the paint
  wired to the image. A test pattern (each view its own colour, a 20 cm grid, the template's edges) showed
  the right side, roof and rear landing where they should, with door outlines on the doors; the plain
  image is back in place.

## Later

- Shine: the paint has one roughness and metalness everywhere; a second image (matte stripes, chrome
  lettering) can be added the same way.
- Dirt and mud would sit on top of the livery in the material, driven by the game rather than painted.
- Wheels, rims and the interior could get maps of their own.
