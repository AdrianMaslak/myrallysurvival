# Network audit - source inspection, 2026-10-21

Read-only network research: no networking implementation or configuration changed. Scope is all project declarations and replication registrations under `Source/RallySurvival`, checked against `docs/MULTIPLAYER.md`. Numbers below are payload estimates, not measured packet captures. UE property handles, array indices, actor channels, NetGUID exports, acknowledgements, packet framing, retransmissions, encryption and Steam transport overhead are additional. C++ sizeof is not wire size. Normal property replication sends changes, not every property at NetUpdateFrequency. Ordinary TArray replication can delta individual members; it must not be described as always resending an entire list.

## Driver simulation and observer replay

`Private/Vehicle/RallyCarPawn.cpp` (`SendState`, `ServerSubmitState_Implementation`, `AddSnapshot`, `TickObserved`) and `Public/Vehicle/RallyCarPawn.h` are authoritative for this description. The driver machine runs physics; the server runs unoccupied cars. Everybody else, including the server for a client-driven car, moves a kinematic body from snapshots. Built-in car movement replication is disabled. Driver input is not sent as an input stream.

StateSendRate defaults to 30 Hz; the actor replication eligibility rate is 60 Hz. SendState is game-frame driven and resets its accumulator to zero after a send: 30 Hz is a target, not a guaranteed cadence. Slow or uneven game frames lower and jitter the send rate. State carries sender physics time, position, quaternion, linear/angular velocity, steering, gear, rpm, power, four suspension offsets, four wheel speeds, reset counter and pedals/handbrake. Client sends an unreliable full struct RPC; server assigns NetState and replicates it with COND_SkipOwner. A property update can coalesce multiple received states; it is not a lossless relay queue.

Observers retain at most 32 snapshots (~1.07 s at 30 Hz), reject non-increasing times in AddSnapshot, and clear on reset counter changes. Playout targets newest time minus 0.100 s. It advances by frame dt plus error * min(1, 2dt), and snaps its clock if error exceeds 0.5 s. Position is cubic Hermite using endpoint velocities; rotation is slerp; wheel travel/spin and steering interpolate. With no future sample it extrapolates velocity/angular velocity for at most 0.25 s. Old history is pruned while keeping two entries. Driver handover seeds velocity from replay; wheel damper state is reset. Two independently driven cars still see one another as kinematic; this is not authoritative shared collision resolution.

Server checks location/linear velocity/quaternion NaNs, speed <=80 m/s and position jump <=30 m unless ResetCount changes. It does not fully validate time/angular velocity/wheel fields or quaternion normalization. Crucially NetState is assigned BEFORE AddSnapshot rejects stale time: an out-of-order upstream RPC can regress the state relayed to clients even though the server's local history rejects it.

The 2026-10-11 measurements in MULTIPLAYER.md were one laptop at ~10 fps, not internet results: host observed delay 215 ms without added lag, 330 ms at 100 ms RTT, 355 ms with 1% loss. Do not promise 100 ms end-to-end delay. Client-to-host transit, host scheduling, host-to-observer transit and interpolation all add up. Four clients require no new snapshot architecture, but `SeatCount=2` and GameMode spawns one team car: four players cannot all sit in that car today.

## Inventory conventions

Paths in the tables map directly to the matching class header in `Public/` and implementation in `Private/` (Vehicle, Player, Damage or Game). `None` means DOREPLIFETIME with no additional lifetime condition: relevance/ownership/channel rules still apply. Inventory totals: **66 properties and 43 RPCs**, matched to source declarations. All listed properties flow server to clients; RPCs are separately enumerated. RepNotify default semantics apply (on received change). References estimate established NetGUIDs; initial asset exports can be much larger. Vector and quaternion serialization/version settings can alter estimates; capture the actual build before treating them as budgets.

## Every project-declared replicated property

| Class | Property | Type | Payload estimate | Condition | RepNotify |
|---|---|---|---|---|---|
| URallyBodyDeformComponent | `Dents` | `TArray<FRallyDent>` | ~30-38 B x N + array metadata | None | OnRep_Dents |
| URallyDamageComponent | `Conditions` | `TArray<FRallyPartCondition>` | ~4.125-5 B x N + array metadata | None | OnRep_Conditions |
| ARallyDebrisActor | `Source` | `FRallyDebrisSource` | ~100-120 B full estimate | None | OnRep_Source |
| ARallyDebrisActor | `Carrier` | `TObjectPtr<ARallyCharacter>` | ~1-5 B NetGUID (exports extra) | None | OnRep_Carrier |
| ARallyDebrisActor | `MountKind` | `ERallyPartMount` | 1 B | None | OnRep_Carrier |
| ARallyDebrisActor | `MountLocation` | `FVector_NetQuantize10` | ~5-9 B | None | OnRep_Carrier |
| ARallyDebrisActor | `MountRotation` | `FRotator` | up to 24 B raw; serializer dependent | None | OnRep_Carrier |
| URallyEngineComponent | `State` | `FRallyEngineState` | ~6 B | None | OnRep_State |
| URallyPanelComponent | `Conditions` | `TArray<FRallyPanelCondition>` | ~45-70 B x N + array metadata | None | OnRep_Conditions |
| URallyPanelComponent | `JackCorner` | `int8` | 1 B | None | OnRep_Jack |
| ARallyDayClock | `StartDay` | `int32` | 4 B | None | none |
| ARallyDayClock | `StartHour` | `float` | 4 B | None | none |
| ARallyDayClock | `StartServerTime` | `float` | 4 B | None | none |
| ARallyDayClock | `RainOverride` | `float` | 4 B | None | none |
| ARallyQuestDirector | `Step` | `int32` | 4 B | None | none |
| ARallyStageDirector | `Run` | `FRallyStageRun` | 19 B | None | none |
| ARallyStageDirector | `Results` | `TArray<FRallyStageResult>` | ~12 B x N + array metadata | None | none |
| ARallyStageDirector | `ControlStage` | `int8` | 1 B | None | none |
| ARallyStageDirector | `Event` | `uint8` | 1 B | None | none |
| ARallyStageDirector | `Cup` | `TArray<FRallyCupEntry>` | (8 + string bytes) x N + array metadata | None | none |
| ARallyStageDirector | `ControlDue` | `float` | 4 B | None | none |
| ARallyTeamState | `KittySk` | `int32` | 4 B | None | none |
| ARallyTeamState | `Orders` | `TArray<FRallyOrder>` | 12 B x N + array metadata | None | none |
| ARallyTeamState | `Note` | `FString` | 4 + L to 2L B (encoding dependent) | None | none |
| ARallyTeamState | `LoanSk` | `int32` | 4 B | None | none |
| ARallyTeamState | `NextPaymentDay` | `int32` | 4 B | None | none |
| ARallyTeamState | `Letter` | `FString` | 4 + L to 2L B (encoding dependent) | None | none |
| ARallyTeamState | `LetterTime` | `float` | 4 B | None | none |
| ARallyTeamState | `NoteTime` | `float` | 4 B | None | none |
| ARallyTeamState | `DeliveryPoint` | `FVector` | 24 B raw | None | none |
| ARallyCharacter | `HeldTool` | `uint8` | 1 B | None | none |
| ARallyCharacter | `PocketSk` | `int32` | 4 B | None | none |
| ARallyCharacter | `bSleeping` | `bool` | 1 bit | None | none |
| ARallyCharacter | `bSleepTillRally` | `bool` | 1 bit | None | none |
| ARallyCharacter | `Injury` | `float` | 4 B | None | OnRep_Injury |
| ARallyCharacter | `Cold` | `float` | 4 B | None | OnRep_Injury |
| ARallyCharacter | `Wet` | `float` | 4 B | None | none |
| ARallyCharacter | `Drunk` | `float` | 4 B | None | OnRep_Injury |
| ARallyCharacter | `Calm` | `float` | 4 B | None | OnRep_Injury |
| ARallyCharacter | `CarriedPart` | `TObjectPtr<ARallyDebrisActor>` | ~1-5 B NetGUID (exports extra) | None | OnRep_CarriedPart |
| ARallyCharacter | `bHasSpanner` | `bool` | 1 bit | None | none |
| ARallyCharacter | `Straps` | `uint8` | 1 B | None | none |
| ARallyCharacter | `Sealant` | `uint8` | 1 B | None | none |
| ARallyCharacter | `Putty` | `uint8` | 1 B | None | none |
| ARallyCharacter | `RefitActions` | `uint8` | 1 B | None | none |
| ARallyCharacter | `RemoveActions` | `uint8` | 1 B | None | none |
| ARallyCharacter | `bProne` | `bool` | 1 bit | COND_SimulatedOnly | OnRep_Prone |
| ARallyCharacter | `bOnBack` | `bool` | 1 bit | COND_SimulatedOnly | none |
| ARallyCharacter | `ToolWorkUntil` | `float` | 4 B | None | none |
| ARallyCharacter | `SeatedCar` | `TObjectPtr<ARallyCarPawn>` | ~1-5 B NetGUID (exports extra) | None | OnRep_Seat |
| ARallyCharacter | `SeatIndex` | `int8` | 1 B | None | OnRep_Seat |
| URallyCabinComponent | `Indicator` | `uint8` | 1 B | None | none |
| URallyCabinComponent | `Wiper` | `uint8` | 1 B | None | none |
| URallyCabinComponent | `bHighBeam` | `bool` | 1 bit | None | none |
| URallyCabinComponent | `bRadioOn` | `bool` | 1 bit | None | none |
| URallyCabinComponent | `bDomeLight` | `bool` | 1 bit | None | none |
| URallyCabinComponent | `bHorn` | `bool` | 1 bit | None | none |
| URallyCabinComponent | `MudLevel` | `uint8` | 1 B | None | none |
| ARallyCarPawn | `bLightsOn` | `bool` | 1 bit | None | OnRep_Lights |
| ARallyCarPawn | `BiasSteps` | `int8` | 1 B | None | OnRep_Bias |
| ARallyCarPawn | `PanelTestPhase` | `uint8` | 1 B | None | none |
| ARallyCarPawn | `SeatMoveKind` | `uint8` | 1 B | None | none |
| ARallyCarPawn | `SeatMoveSeat` | `int8` | 1 B | None | none |
| ARallyCarPawn | `NetState` | `FRallyCarNetState` | ~100-120 B full estimate | COND_SkipOwner | OnRep_NetState |
| ARallyCarPawn | `Occupants` | `TArray<TObjectPtr<ARallyCharacter>>` | N x ~1-5 B + array metadata | None | none |
| ARallyCarPawn | `DoorsOpen` | `uint8` | 1 B | None | none |

## Every project-declared RPC

| Class | RPC and parameters | Delivery | Caller | Frequency |
|---|---|---|---|---|
| URallyEngineComponent | `MulticastStartFailed(uint8 Reason)` | NetMulticast, Unreliable | Server | Per failed ignition attempt |
| URallyPanelComponent | `MulticastFixingLoose(uint8 Slot, bool bNoneLeft)` | NetMulticast, Unreliable | Server Panel Tick | Per fixing worked loose; can emit several per tick across slots, no dedicated network rate cap |
| ARallyCharacter | `ServerInteract(ARallyDebrisActor* TargetPart)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerBodgePanel(ARallyCarPawn* Car, int32 Panel)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerHammer(ARallyCarPawn* Car, FVector_NetQuantize Eye, FVector_NetQuantizeNormal Direction)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per swing, minimum 0.45 s client; 0.35 s server tolerance |
| ARallyCharacter | `ServerStrapBetween(ARallyCarPawn* Car, int32 Panel, FVector_NetQuantize10 PointA, FVector_NetQuantize10 PointB)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerTogglePanel(ARallyCarPawn* Car, int32 Panel)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerRemovePart(ARallyCarPawn* Car, int32 Part)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerDropPart()` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerSelectTool(uint8 Tool)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerTurnFixing(ARallyCarPawn* Car, int32 Part, int32 Fixing, bool bHome)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per completed hold, 0.25-0.5 s x body slowdown; server minimum 0.2-0.4 s |
| ARallyCharacter | `ServerHandRev(ARallyCarPawn* Car)` | Server, Unreliable | Owning character client input/interaction (host calls locally) | At most ~6.67/s while held (>0.15 s) |
| ARallyCharacter | `ServerTurnKey()` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerClimb()` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerMoveMoney(int32 Sk)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerWorkshop(ARallyCarPawn* Car, int32 Part)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerOrder(int32 Item)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerToggleSleep(bool bTillRally)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerAdjust(ARallyCarPawn* Car, int32 Part, int32 Step)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per player action; no periodic send |
| ARallyCharacter | `ServerSpawnPart(int32 Slot, uint8 Grade)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per development command/test input; not periodic gameplay |
| ARallyCharacter | `ServerCoil(int32 Coil, float Health)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per development command/test input; not periodic gameplay |
| ARallyCharacter | `ServerMud(float Amount)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per development command/test input; not periodic gameplay |
| ARallyCharacter | `ServerSaveLoad(bool bSave)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per development command/test input; not periodic gameplay |
| ARallyCharacter | `ServerRain(float Rain)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per development command/test input; not periodic gameplay |
| ARallyCharacter | `ServerLeak(uint8 Which, float Health)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Per development command/test input; not periodic gameplay |
| ARallyCharacter | `ServerSetSprint(bool bSprint)` | Server, Reliable | Owning character client input/interaction (host calls locally) | Sprint input transitions / desired run change |
| ARallyCarPawn | `ServerRallyMud(float Amount)` | Server, Reliable | Owning driver client input (host calls locally) | Per development command/test input; not periodic gameplay |
| ARallyCarPawn | `ServerCabinControl(uint8 Control)` | Server, Reliable | Owning driver client input (host calls locally) | Per player action; no periodic send |
| ARallyCarPawn | `ServerHorn(bool bOn)` | Server, Reliable | Owning driver client input (host calls locally) | Press and release (2 calls per hold) |
| ARallyCarPawn | `ServerToggleLights()` | Server, Reliable | Owning driver client input (host calls locally) | Per player action; no periodic send |
| ARallyCarPawn | `ServerToggleSleep(bool bTillRally)` | Server, Reliable | Owning driver client input (host calls locally) | Per player action; no periodic send |
| ARallyCarPawn | `ServerRecce()` | Server, Reliable | Owning driver client input (host calls locally) | Per player action; no periodic send |
| ARallyCarPawn | `ServerBias(int32 Step)` | Server, Reliable | Owning driver client input (host calls locally) | Per player action; no periodic send |
| ARallyCarPawn | `ServerCallTow()` | Server, Reliable | Owning driver client input (host calls locally) | Per player action; no periodic send |
| ARallyCarPawn | `ServerReportImpact(FVector_NetQuantize10 LocalPoint, FVector_NetQuantize10 LocalExtent, FVector_NetQuantizeNormal LocalDirection, float DeltaV)` | Server, Reliable | Client driver FlushPendingImpact/Underside | Per flushed contact; shared server acceptance cap 8/s (does not cap sent bytes) |
| ARallyCarPawn | `ServerDebugDamage(bool bRepair)` | Server, Reliable | Owning driver client input (host calls locally) | Per development command/test input; not periodic gameplay |
| ARallyCarPawn | `MulticastCrashSound(uint8 Kind, uint8 Strength)` | NetMulticast, Unreliable | Server | Per crash/floor hit/hammer sound |
| ARallyCarPawn | `ServerToggleIgnition()` | Server, Reliable | Owning driver client input (host calls locally) | Per player action; no periodic send |
| ARallyCarPawn | `ServerReportUnderside(FVector_NetQuantize10 LocalPoint, FVector_NetQuantize10 LocalExtent, float DeltaV)` | Server, Reliable | Client driver FlushPendingImpact/Underside | Per flushed contact; shared server acceptance cap 8/s (does not cap sent bytes) |
| ARallyCarPawn | `ServerSubmitState(const FRallyCarNetState& State)` | Server, Unreliable | Client driver SendState | Target 30/s while simulating, frame-limited |
| ARallyCarPawn | `ServerRequestExit()` | Server, Reliable | Owning driver client input (host calls locally) | Per player action; no periodic send |
| ARallyCarPawn | `ServerToggleDriverDoor()` | Server, Reliable | Owning driver client input (host calls locally) | Per player action; no periodic send |
| ARallyCarPawn | `ServerRequestClimb()` | Server, Reliable | Owning driver client input (host calls locally) | Per player action; no periodic send |

## Struct and burst accounting

- `FRallyDent`: two quantized vectors (~5-9 B each), normal (~6 B), mask (4 B), radius/depth (8 B), straighten flag: ~30-38 B per complete entry, excluding replication metadata. `URallyBodyDeformComponent` caps crash entries at **48**, but **MaxEntries=200** includes hammer straightening. Full 48-entry baseline is ~1.4-1.8 KB; 200 entries ~6-7.6 KB per receiving connection. Append-only updates may be small; list rebuilds, initial relevance and late joining are the expensive cases. `OnRep_Dents` mesh reconstruction also costs CPU and can cause frame hitches independent of bandwidth.
- `ARallyDebrisActor::Launch` gives each part its own deform component and copied relevant dents. Its `Source` includes car/mesh references, panel index, transform, mass, health, spare and grade (~100-120 B before exports). The separate Dents property duplicates relevant history; detaching multiple dented panels can burst kilobytes per part, plus actor creation, movement and attachment state. The saved-part Dents member is save data, not an additional replicated property.
- `FRallyPanelCondition`: 4 fixing bytes; two strap vectors; strap quality; jammed/removed/open flags; fit vector/rotation; debris reference; uint16 fixing mask; grade and two setting bytes. Approximately 45-70 B for a full slot. Current constants give **63 slots** (0-62): ~2.8-4.4 KB baseline per car. Routine mutations need not resend all 63; simultaneous damage/repair/refit and reference exports are burst sources. No FastArray serializer exists here.
- Mechanical `Conditions`: **43** FRallyPartCondition entries, each float health + bool bodged: ~178-215 B full baseline. Repeated float health wear can dirty values much more often than the UI needs. Panel conditions and mechanical conditions describe different things; do not remove one as "duplicate" without preserving gameplay.
- Cabin: Indicator/Wiper/MudLevel are bytes; high beam/radio/dome/horn are booleans. They change on input, self-cancel or quantized dirt accumulation, not continuously streamed animation. MudLevel is assigned from rounded Mud *255 in UpdateMud; it can update frequently in dirty/wet conditions but unchanged levels cost no property payload. `ServerRallyMud` / `ServerMud` are development commands, not the accumulation loop. Wheel/pedal motion uses NetState. Wiper animation, radio and normal engine sound are local.
- Engine State is five bytes plus running bool, changed on quantization boundaries; underlying simulation runs each tick. Character body floats (Injury/Cold/Wet/Drunk/Calm) can change on the once-per-second TickBody. Inventory, seats, stage results, orders and letters are event traffic. FString and result/order arrays have variable lengths; no universal fixed packet maximum follows from this audit.
- All actors call Super::GetLifetimeReplicatedProps: engine base replication remains. CharacterMovement contributes its packed move RPCs and corrections; Actor contributes role/owner/attachment/movement state where enabled; Pawn controller/player-state and engine GameState/PlayerState/session actors add baseline traffic. These are inherited UE declarations, not missing project RPCs. `URallyCharacterMovement` extends compressed move flags (prone/back), rather than declaring another project RPC. Built-in debris movement includes physics velocity and sleeping state. Loose debris replicates movement; carried/stowed/under-car debris disables it and uses attachment state.

## Ongoing traffic model (four players = listen host + three remote clients)

Use S=100-120 B as a conservative full snapshot payload estimate. The three quantized vectors vary with magnitude; double quaternion storage, static float arrays and per-field handles make exact size serializer-dependent. At 30 sends/s a client driver uploads ~3.0-3.6 KB/s payload per car. With a remote driver, the server sends that car to the two OTHER remote clients (~6.0-7.2 KB/s aggregate full-state equivalent); the local host renders without an outgoing connection, and the driver is skipped. A host-driven or unoccupied car is sent to three clients (~9.0-10.8 KB/s). Multiply by active cars; don't multiply each client driver's upload by three. Downstream delta replication may be smaller than these full-state estimates. With four remote clients plus a dedicated server the fanout is different.

Even a parked car currently changes its Time field and sends at the target rate while its simulation is active, so upstream RPCs still contain full state. There is no idle send suppression. A changing timestamp alone does not imply the full downstream struct is resent. Approximate extra framing of 40-100 B per separately emitted packet at 30 packets/s would add 1.2-3 KB/s per link; packet bundling and Steam framing must be measured, so this is only a planning range.

CharacterMovement send rate is engine/frame/network-settings dependent, not 30 Hz just because cars are. One second of five changed body floats adds ~20 B per character per receiving connection before field handles. Moving loose debris can dominate: at 30-100 effective updates/s and a rough 30-60 B movement payload, each moving part adds ~0.9-6 KB/s per observer. Sleeping unchanged bodies do not necessarily send at that rate, although they remain eligible for replication checks. Static day/stage/team properties do not emit their entire state at their actor frequency. Steam keepalive/ACKs and engine time sync remain even with no gameplay changes. There is no defensible exact total bytes/s without a four-machine capture.

Validate using Network Insights / net tracing on a packaged Development listen host and three clients: stable drive, parked, crash with several detachments, 200-entry hammer history, late join, and 50/100/150 ms RTT with loss. Record per-class/property bytes, reliable backlog, snapshot age and loss, server frame time, and playout underruns; report median and p95, not only average ping.

## Actor replication settings

Verified engine defaults at `C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/Actor.cpp` constructor: 100 Hz update, 2 Hz minimum adaptive frequency, priority 1, DORM_Awake, NetCullDistanceSquared=225000000 cm^2 (150 m radius). `Pawn.cpp` sets priority 3. `Info.cpp` does not override these rates. These are C++ defaults, not a claim that a placed/Blueprint instance cannot override them. Frequency is an eligibility ceiling, bounded by server tick, relevance and connection budget; dormancy differs from rigid-body sleep.

| Project actor | NetUpdateFrequency | NetPriority | Dormancy | Cull radius / relevance | Movement |
|---|---:|---:|---|---|---|
| ARallyCarPawn | 60 Hz | 3 | Awake | 150 m inherited; ownership relevance applies | off; custom NetState |
| ARallyCharacter | 100 Hz inherited | 3 | Awake | 150 m inherited; ownership relevance applies | CharacterMovement; off seated, on exiting |
| ARallyDebrisActor | 100 Hz inherited | 1 | Awake | 150 m inherited; attachments affect relevance | loose only |
| ARallyDayClock | 2 Hz | 1 | Awake | always relevant (distance bypassed) | off |
| ARallyStageDirector | 10 Hz | 1 | Awake | always relevant | off |
| ARallyQuestDirector | 100 Hz inherited | 1 | Awake | always relevant | off |
| ARallyTeamState | 100 Hz inherited | 1 | Awake | always relevant | off |
| ARallyPuddleField | 100 Hz default, unused | 1 default | Awake default, unused | nonreplicated | local |
| ARallyHUD | inherited settings unused | none | none | owning local HUD, no replication | none |
| ARallyGameMode | inherited settings unused | none | none | server only, no replication | none |

Components do not have independent actor update frequency, priority, dormancy or cull distance: damage/deform/panels/engine/cabin ride their owner. Leak, synth, crew mesh and physical material have no project replicated properties/RPCs. Dynamically created environmental/visual AActors, windscreen pieces, lights and puddle visuals are not separately replicated project actor classes. No project code sets explicit dormancy or cull distance; Damage's FlushNetDormancy is already present for future dormancy use.

## Ten highest-value changes (proposals only, ordered by expected benefit)

1. **Stabilize snapshot cadence and server frame time.** Change `Private/Vehicle/RallyCarPawn.cpp::SendState` to preserve accumulator remainder and send the latest state once per frame; avoid catch-up bursts. Profile heavy mesh work on the host. Expected: fewer irregular gaps and tens of ms of scheduling delay under load; no fixed improvement promised below 30 fps.
2. **Reject stale/invalid submissions before assigning NetState.** Same file, ServerSubmitState; validate all numeric fields, monotonically increasing sequence/time within a driver epoch and resets; rate-limit ingress. Expected: eliminate reordered-state relay regressions and bad Hermite segments, with tiny sequence overhead.
3. **Adapt playout delay to measured arrival jitter.** Same file plus `Public/Vehicle/RallyCarPawn.h`; instrument underruns, choose a bounded ~50-150 ms delay, slow changes to the target. Expected: ~30-50 ms less observer delay on stable links while retaining extra buffering on bad links. Reduce the fixed 100 ms only with evidence; more delay trades responsiveness for smoothness.
4. **Pack snapshots explicitly.** `Public/Vehicle/RallyVehicleSetup.h` plus SendState/replay: bounded wheel offsets/speeds and steering, compressed normalized quaternion, relative timestamp and versioned NetSerialize. Aim ~50-70 B instead of ~100-120 B; roughly 30-50% less car payload, especially upstream full RPCs. Preserve sufficient precision for high-speed motion and large countryside coordinates.
5. **Idle/adaptive send rate.** `Private/Vehicle/RallyCarPawn.cpp`: reduce truly stationary unoccupied cars to ~2 Hz or dormancy, immediately wake on input, collision, repair, seating and ownership change; keep occupied moving cars at 30. Expected: up to ~93% fewer parked-car snapshots. Coordinate component dirt/engine changes before using full dormancy.
6. **Make dent history incremental and budget late-join rebuilds.** `Public/Private/Damage/RallyBodyDeformComponent.*`: stable entry IDs/FastArray or explicit versioned batches, compact scalar fields, bounded baseline chunks, amortized mesh rebuild. Expected: smaller baseline/update bursts and fewer render/game-thread stalls; ordinary array append already deltas, so don't promise 48x steady-state savings.
7. **Reduce debris replication cost and duplication.** `Public/Private/Damage/RallyDebrisActor.*`: immutable initial source/dents when appropriate, shared versioned dent baseline where safe, lower moving-part rate (~15-20 Hz) with smoothing, dormant when settled/stowed, wake on pickup/drop/repair. Expected: ~50-80% fewer moving-debris updates vs 60-100 Hz and near-zero idle polling; independent late-join dent reconstruction must survive car repair/destruction.
8. **Split/quantize panel and mechanical changes.** `Public/Private/Damage/RallyPanelComponent.*`, `RallyDamageComponent.*`: keyed dirty-slot FastArrays, optional sparse strap/fit payloads, quantized health. Expected: smaller 63-slot baselines, fewer float-health updates and bounded damage bursts; assess current member deltas first.
9. **Coalesce reliable impact reports at the sender.** `Private/Vehicle/RallyCarPawn.cpp`: aggregate contacts over a short bounded window, cap sends before RPC dispatch; retain reliable gameplay damage or add explicit event acknowledgement/deduplication before changing reliability. Expected: fewer reliable bunches and shorter loss-induced backlog during scraping; server's 8/s acceptance limit alone saves no incoming bandwidth. Keep horn/control state transitions reliable.
10. **Tune per-connection relevance and low-rate state.** `Private/Game/RallyTeamState.cpp`, `RallyQuestDirector.cpp`, `Private/Player/RallyCharacter.cpp`, `Private/Vehicle/RallyCabinComponent.cpp`: lower static directors to ~1-2 Hz with ForceNetUpdate on mutations, quantize/throttle body and mud updates, use owner-only inventory where other clients do not need it; distance/priority policies for repairable parts. Expected: less replication CPU and background metadata with four players, leaving budget for cars. Avoid hiding shared inventory/prompts or culling the occupied car; do not assume all health/cabin state is owner-only.
