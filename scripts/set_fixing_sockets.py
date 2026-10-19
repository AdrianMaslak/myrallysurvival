"""Fixings placed by hand (docs/DAMAGE.md, Placing them by hand): sockets named Fixing0, Fixing1 ... on a
part's static mesh put that fixing there, its head facing along the socket's X axis. The rest of the parts
keep the places worked out from their bounds.

SM_Octavia_Alternator: the Engine body mesh covers the alternator, so fixings on its own top were out of
sight from every side. Each sits instead on the engine's skin straight above where it was (found by casting
rays up through the bay's meshes), facing up: seen from above and from in front with the bonnet open.
SM_Octavia_ECU: wedged between the bulkhead and the engine, its fixings spread across the car were covered
by one or the other; along the car down its middle they show from above and in front.

Sockets of the same names are replaced, so running it again is harmless.
Run via scripts/Invoke-EditorPython.ps1 -Script scripts/set_fixing_sockets.py -PassMarker FIXING_SOCKETS_PASS.
"""
import unreal

DEST = "/Game/Vehicles/Octavia/Prototype"
UP = unreal.Rotator(roll=0.0, pitch=90.0, yaw=0.0)

# Mesh: [(location in the mesh's own space, cm; rotation whose X axis is the way the head faces)], in the
# part's fixing order.
SOCKETS = {
    "SM_Octavia_Alternator": [
        (unreal.Vector(-8.5, 0.0, 32.5), UP),
        (unreal.Vector(0.0, 0.0, 32.1), UP),
        (unreal.Vector(8.5, 0.0, 15.2), UP),
    ],
    "SM_Octavia_ECU": [
        (unreal.Vector(-2.5, 0.0, 2.3), UP),
        (unreal.Vector(1.0, 0.0, 2.3), UP),
        (unreal.Vector(4.5, 0.0, 2.3), UP),
    ],
}

for name, fixings in SOCKETS.items():
    mesh = unreal.load_asset(f"{DEST}/{name}")
    if not mesh:
        raise RuntimeError(f"{name} is missing")
    for index, (location, rotation) in enumerate(fixings):
        socket_name = f"Fixing{index}"
        old = mesh.find_socket(socket_name)
        if old:
            mesh.remove_socket(old)
        socket = unreal.StaticMeshSocket(mesh)
        socket.set_editor_property("socket_name", socket_name)
        socket.set_editor_property("relative_location", location)
        socket.set_editor_property("relative_rotation", rotation)
        mesh.add_socket(socket)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    placed = [mesh.find_socket(f"Fixing{i}") for i in range(len(fixings))]
    unreal.log("FIXING_SOCKETS %s: %s" % (name, ", ".join(
        "%s at %s" % (s.socket_name, s.relative_location) for s in placed if s)))
    if len([s for s in placed if s]) != len(fixings):
        raise RuntimeError(f"{name}: sockets not all found after adding")
unreal.log("FIXING_SOCKETS_PASS")
