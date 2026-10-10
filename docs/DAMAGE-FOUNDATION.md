# Damage foundation

## Implemented source

URallyDamageComponent tracks four tyres, four suspension corners and one radiator.
All begin at health 1; damage clamps at 0. Invalid amounts are rejected.
A proper fix restores 1. A bodge restores 0.6 and marks the part as bodged.
0.6 is provisional tuning. Repeated bodges cannot replenish a bodged part until a proper fix.
Bodges cannot reduce a healthier part's condition.

The component checks owner authority before mutations. The owning vehicle must replicate.
State is replicated using a RepNotify array; server-side listeners are explicitly notified too.
There are no client-callable server RPCs here.
Gameplay must use an owned player interaction path with server-side validation.

## Integration after engine installation

1. Compile the Unreal project. Add RallyDamageComponent to the replicated vehicle Blueprint.
2. Bind OnDamageChanged to a temporary debug display. Read initial state when binding too:
   unchanged default state need not generate a replication notification.
3. From a server-only debug action, apply 1.0 damage to TyreFrontLeft.
4. Verify both players display 0 for that tyre.
5. Wire tyre condition to a supported Chaos simulation change, then verify a driving symptom.
   No physics parameters are changed by the component currently.
6. Add a timed repair interaction; validate stopped vehicle, proximity, seat state, tools,
   available spare and exclusive repair ownership on the server before completing it.
7. Commit the repair and inventory consumption together in the server interaction handler.
   A rejected/no-op repair must not consume a spare.
8. Test simultaneous repairs, late joins, host/client driving and disconnects.

## Not implemented

Collision-to-damage mapping, driving symptoms, overheating, bodge failure risk,
repair time, inventory, tools, UI and persistence between sessions.
A bodge currently differs only by condition and its flag.
The component is groundwork, not a playable repair system.

## Validation

Run scripts\Test-Rules.cmd to compile and execute the engine-independent rules with MSVC.
These tests do not compile Unreal reflection code or test replication.
Unreal build and two-player integration tests remain required.
