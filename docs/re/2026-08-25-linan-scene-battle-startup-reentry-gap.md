# Linan scene-battle fireball: Actor resource-closure investigation

Date: 2026-08-25

Status: the earlier startup/control-state explanation below is retained as
chronology, but is superseded by the later direct Linan-versus-Danxia resource
comparison. Publishing a complete Actor/GIF dependency closure is necessary
for release integrity, but the latest ordinary client reproduction proves that
the update manifest does not proactively install every listed leaf. The
automatic-action dispatch boundary remains unresolved.

## Symptom

After deploying a kind-3 scene-battle monster into `c04临安府_01.sce`, the
client draws its fireball. Moving the player through it does not enter battle.

## What the latest reproduction proves

The deployed SCE is installed, parsed, rendered and collision-positioned:

```text
scene_runtime_init_and_sync(0x01012FE6)
actor=1 ... resource=e_huayao.actor
player=(108,104), nearest actor=1 at (108,108), distance2=16
runtime_ready=1, assets_ready=1
```

Thus a bad SCE, missing effect actor, unready resource, bad player movement or
collision distance is not the first deviation.

The user input reaches the normal main dispatcher at
`JianghuOL.CBE:0x010163A4`. The inputs used in the reproduction are normal
directional values (`0x40000`, `0x20000`, `0x10000`, `0x8000`), but every
dispatch and render trace has:

```text
input_callback=05017ab9
control_state=0
```

Static code at `0x010163A4` switches on `Global_R9 + 23682`:

- state `0` takes the base-input path and does **not** invoke the active scene
  screen callback;
- states `1`, `2`, `4` and `5` dispatch into that callback / active screen.

The registered mmGame action callback at `module+0x8A8` consequently has no
execution trace in this session. No `TriggerAutoBattle(0x010183A0)`, node
scan, collision callback or client-originated `WT 4/1` follows. The server
only sees ordinary movement traffic.

This is the exact reason that touching the visible fireball does not start a
battle: the scene remains visually live but never enters the client control
mode that drives automatic battle scanning.

## First protocol/lifecycle deviation

After the final SCE `WT18/7` installation callback, the server logs:

```text
mock_startup_sce_install_scene_enter_suppressed
  action=control-ack-only
  reason=16-2-direct-enter-retains-live-background-array
```

It then receives the short `WT 25/5` control request and returns the generic
`builtin-scene-default-event` response. The client does run mmGame `sub_11CE`
(the response dispatcher), but never reaches its `sub_BCC` scene-enter-object
branch.

The missing branch is the known native contract:

```text
16/2 result=1 + scene + position
  -> mmGame sub_11CE
  -> sub_BCC scene-enter object
  -> main API +116 / EnterSceneByMapName
  -> scene-control state becomes 1
  -> action callback -> TriggerAutoBattle -> WT4/1
```

The current generic `WT25/5` acknowledgement intentionally contains none of
that scene-position entry contract. The resulting state `0` is therefore a
protocol/lifecycle consequence, not a collision-handler failure.

## Why the dormant 16/2 builder is not enabled blindly

`vm_net_mock_build_startup_sce_install_scene_enter_response()` can build the
`16/2 result=1` object. Earlier runtime evidence showed that issuing this
same-scene direct entry while the role-select-created shell still owns its
background Actor array repeats allocation and can crash.

The existing screen-manager support has a one-shot
`vm_net_mock_consume_update_completed_scene_reenter()` path for a completed
resource update. A repair must prove that the initial SCE path enters that
safe, client-driven lifecycle exactly once before enabling the `16/2` response.
It must not write client state, bypass `sub_11CE`, or simply force state `1`.

## Implemented test gate

The server now keeps the established suppression by default. Only a server
process launched with this exact environment variable can arm the dormant
response:

```text
CBE_TEST_STARTUP_SCE_DIRECT_ENTER=1
```

After a final install of the role's current SCE, one matching short `WT25/5`
may consume that arm and return one `16/2 result=1` object with the role's
current scene and coordinates. The arm expires after 90 scheduler ticks and
is invalidated if the role, installed resource generation, or completed scene
target no longer matches. A second `WT25/5` is not consumed and returns to the
ordinary control-acknowledgement path.

The gate is server-side. Setting this variable in the player launcher alone
does not enable it. For the direct player-3 reproduction,
`bin\\multiplayer\\start-server.bat` now defaults it to `0`. It may only be
enabled explicitly for packet forensics, never as the normal startup repair.

`startup-sce-direct-enter-test-gate-regression` constructs the exact
short-`WT25/5` bytes and validates both sides of this contract without opening
a listener or connecting to MySQL:

```text
make startup-sce-direct-enter-test-gate-regression
.\obj\server\startup-sce-direct-enter-test-gate-regression.exe
```

The test proves packet ownership and one-shot behaviour only. It does not
prove the CBE screen lifetime, control-state change, collision scan, or battle
parser.

## Direct player-3 reproduction result (2026-08-25)

The gated response was delivered exactly once in the direct player-3 run:

```text
startup target: c04临安府_01.sce (80,124)
request:         short WT25/5
response:        WT16/2 {result=1, scene, posinfo, exitid=0}
```

This is not sufficient. Its immediate client follow-up was handled as
`builtin-scene-change`, which resolved a new `WT2/3` target at the SCE safe
entry `(201,140)` and returned a position-bearing `WT30/2`. The generic
position branch therefore performed a second scene entry rather than merely
acknowledging the scene shell created by the `16/2` callback. The client did
not subsequently expose the known standalone/coalesced runtime `16/3` direct
enter acknowledgement in this run.

The negative evidence is decisive: `16/2 result=1` is the documented
`sub_11CE -> sub_BCC` fall-through scene-entry shortcut. The immediately
following `2/3` causes a second position-bearing `30/2` scene entry at
`(201,140)`. This contradicts the already-proven first-scene lifecycle rule:
the role-select-created shell must complete its `12/1` follow-up with a
no-`posinfo` acknowledgement, not a second same-scene entry. The test gate is
therefore a disproven hypothesis, not a repair, and is disabled by default.

The client did receive and dispatch the `16/2`, but its active scene remains
`01053450/01053438` with `control_state=0`; a nearby deployed actor at
distance squared `16` still never reaches `TriggerAutoBattle` or emits `WT4/1`.

`CBE_TRACE_STARTUP_SCE_FOLLOWUP=1` remains available for the disproven packet
probe. The active next probe is
`CBE_TRACE_SCENE_BATTLE_CONTROL_STATE=1`: it records up to 64 guest writes
overlapping `Global_R9+23682`, including the native PC, LR, registers and prior
two-byte state. It performs no guest-memory, register, PC/LR, response or event
write. This identifies the actual branch that does—or does not—install the
scene control state after the normal one-scene startup lifecycle.

## Required repair validation

For the direct player-3 reproduction, close any existing local mock service,
then start `bin\\multiplayer\\start-server.bat` and
`bin\\multiplayer\\start-player-3.bat`. The server keeps the normal one-scene
startup path; the player launcher enables actor/collision plus control-state
writer tracing. First verify all of the following in one run:

1. no startup `16/2 result=1` or second position-bearing same-scene entry;
2. the first native guest write (or absence of a write) over `R9+23682` is
   recorded with its PC/LR and occurs after the relevant scene callback;
3. only after its writer is identified, `control_state` changes from `0` to
   `1` through normal client code;
4. the deployed actor reaches `TriggerAutoBattle -> collision -> WT4/1`, and
   the battle parser opens the expected battle screen;
5. repeat login/re-entry and a scene without a deployed battle actor do not
   duplicate events or leak scene state.

For the reproducible isolated fixture, double-click
`bin\\multiplayer\\start-linan-scene-battle-direct-enter-test.bat` instead.
It uses ports `19450/19451`, copies the currently deployed `jh_online` Linan
overlay into an artifact-owned resource root, seeds `guest00001` in a newly
generated `jh_online_autotest_<id>` database at `(108,104)`, and starts a new
client profile. The user performs the one normal movement input; no desktop
automation is used. Closing that client writes `result.json` beside the
service/client logs and removes only the generated test database.

Until that lifecycle test passes, the default `WT25/5` generic acknowledgement
remains safer than enabling the known crashing response, but it necessarily
leaves startup-deployed scene fireballs non-interactive.

## Superseding automatic-collision evidence (2026-08-25)

The preceding mouse-click acceptance theory is withdrawn.  丹霞山、终南山等
野外场景已经证明，角色移动到火团碰撞范围内时必须直接进入战斗；触摸只能是另一条
客户端 action 路径，绝不能作为场景战斗怪的前置条件或验收条件。

`Global_R9+23682` remains relevant, but only as the client's raw-direction
control-state.  During the failed 临安府 startup run it stayed `0`, so the
direction path `mmGame:sub_566 -> main API +88` did not dispatch through the
registered `R9+0x5D24` control delegate that the native scene lifecycle
normally installs.  This state is client-owned: a server response must never
write or force it.  The missing behavior is therefore a legal scene lifecycle
transition, not a collision flag and not an input substitution.

The deployed SCE record is already structurally equivalent to a wilderness
kind-3 monster record: it has the monster id, body Actor (`e_huayao.actor`),
fireball effect (`e_ghostfireR.actor`), native scene node, battle vtable slot,
and close geometry.  The first divergence is that the dynamic SCE resource is
installed after the initial scene shell is created.  Normal `WT6/1` content
installation reaches `mmGame:sub_11CE`, but does not take its `sub_BCC` scene
rebuild branch.  Consequently the new fireball renders, while the movement
collision lifecycle remains that of the already-created empty shell.

The earlier `WT16/2 { result=1 }` experiment was the wrong protocol family.
It is a direct-enter/settings-recovery response and led to a same-scene
`WT2/3` reinitialization; it does not establish the startup collision route.
The parser-proven scene-rebuild contract is instead:

```text
WT 1/16/3 { result: typed-u8(2), scene, posinfo, exitid }
mmGame:sub_11CE -> sub_BCC -> main API +116 -> scene_runtime_init_and_sync
```

The current server test gate sends exactly that object once, only after a
fresh SCE installation and its first independent `WT25/5`; it sends no
synthetic `WT4/1` and does not touch client state.  Acceptance is solely:
restart with the standard server/player-3 scripts, enter `c04临安府_01`, and
move into the fireball without clicking.  A pass must show the normal
`TriggerAutoBattle`/collision path and a genuine client `WT4/1` followed by
the existing battle exchange.  If it still fails, the first missing lifecycle
callback in that run is the next evidence target.

## WT16/3 direct-enter probe: negative result (2026-08-25)

The direct player-3 reproduction did still fail after the gated short
`WT25/5 -> WT16/3 { result=2, scene, posinfo, exitid }` response.  The client
logged `scene_battle_lifecycle phase=scene-enter-object`, which proves that
`mmGame:sub_BCC` parsed the object.  It then initialized the scene input
callback but cleared the normal control and touch delegates.  Subsequent real
direction inputs ran `SceneTickUpdatePositions`, while no collision scan,
`TriggerAutoBattle`, or client `WT4/1` occurred.

This rules out the standalone `WT16/3` reply to `WT25/5` as a repair.  It is
the wrong request context for reconstructing the first-scene lifecycle and is
disabled by default in `start-server.bat`.  It also does not change the
requirement: collision must remain an ordinary movement result, never a mouse
click or a synthetic battle request.

The deployed record's field 14 is confirmed to be the client actor id and
does create a native battle-capable node.  There is currently no evidence that
the value `1` itself is rejected, so changing it to a wilderness-looking id
would be an unsupported data mutation rather than a fix.

The remaining supported root-cause statement is earlier in the startup
sequence: the role-selection actor-info response creates the initial scene
shell, while the current SCE and its actor/effect files are installed later
through `WT18/7`.  A safe solution must make the complete SCE resource set
available before that first shell is built, or enter a genuinely different
scene through its normal lifecycle.  It must not re-enter the same scene with
`16/2`, `16/3`, or a forced control-state write; prior runtime evidence shows
that same-scene re-entry can duplicate background actor state and later crash.

## Deferred automatic-action slot: next root-cause boundary (2026-08-25)

The latest direct reproduction was run against an already-running server
process whose startup log explicitly says
`mock_startup_sce_install_scene_enter_test_armed`.  That process inherited the
old `CBE_TEST_STARTUP_SCE_DIRECT_ENTER=1` setting, so its `WT16/3` experiment
is not admissible as evidence for the normal startup path.  The normal
`bin\\multiplayer\\start-server.bat` now unconditionally sets that variable
to `0`, rather than preserving an inherited value.  The isolated
`startup-sce-direct-enter-test-gate-regression` passed: at `0`, short
`WT25/5` is left to the ordinary acknowledgement path; at explicit `1`, the
one-shot forensic response remains available only to the regression.

Static mmGame evidence narrows the missing automatic path without asserting a
data cause.  Its normal scene logic (`sub_604`) invokes `sub_183A` every tick.
`sub_183A` calls its deferred action callback only when the module-owned slot
at `r4+0x38` is non-zero.  The action callback is the normal route that later
reaches `TriggerAutoBattle`; it is not a mouse-click replacement.  In the
failed Linan run the character reached squared distance `25` from the native
kind-3 node, but no action callback, collision scan, or `WT4/1` followed.

`src/main.c` now resolves that slot only after fingerprinting the live mmGame
image and records its value at the `sub_183A` check.
`src/hookRam.c` records the bounded native writes to that exact slot when
`CBE_TRACE_SCENE_BATTLE_COLLISION=1`.  Both instruments are read-only: they do
not set the slot, inject an action, modify the response, or change guest
registers, PC, LR, timing, or memory.  The first non-zero write (or confirmed
absence of one) is the next required comparison against a natural wilderness
fireball collision.

The `automonster.dsh`/`tempData.bin` table remains a candidate only: it lists
normal wilderness automatic-monster maps but currently has no Linan entry.
There is not yet client parser/caller evidence that this table owns the
deferred-action-slot write.  It must not be edited or published as a fix until
that writer is identified.

### Standard-path reproduction and trace correction

The subsequent direct player-3 run used the corrected standard launcher. Its
server log records `mock_startup_sce_install_scene_enter_suppressed` and the
ordinary `WT25/5` control acknowledgement; no `WT16/3` object was sent. The
client rendered actor id `1` as a native kind-3 node and moved within squared
distance `80` of it, still with no `TriggerAutoBattle` or `WT4/1`. This
confirms the failure also exists without the disproven direct-enter probe.

The first version of the new observation point did not fire because CBM
static offsets are six bytes ahead of their loaded instruction address: the
existing `sub_604` fingerprint at loaded `base+0x604` corresponds to static
`0x60A`. The `sub_183A` post-`ADD r4,sb` check is therefore loaded
`base+0x183A`, not `base+0x1840`. The trace was corrected and rebuilt before
the next reproduction. This changes observation only, not client behavior.

The corrected-PC run still did not hit the instruction anchor, although the
standard path reached overlap distance `0` without a collision request. The
investigation therefore now samples the same slot through `sub_183A`'s
relocated literal at runtime (`static 0x1BF4`, loaded six bytes earlier) at
the already-observed scene-logic entry, and arms the write watch only after
that exact address is readable. This is still purely observational.

Resource correlation alone does not justify changing `automonster.dsh`:
project evidence classifies it as a server-side hangup encounter table, and
`tempData.bin` embeds the same data. Although all named wilderness examples
also have rows in that table while Linan does not, no client parser/caller
links it to movement collision. It remains excluded from the repair until the
native deferred-action-slot writer is observed.

### Correction: the deferred-action-slot probe was not a valid boundary

The follow-up player-3 trace recorded the raw word at the proposed module
literal address as `0x94001A08`. A direct read of the installed CBM explains
this: local CBM addresses have a file-code offset (`0x9A`), so the previously
used raw `0x1BF4` location is instruction data, not the proposed relocation
literal. The associated `sub_183A` hypothesis was also inconsistent with the
existing mmGame disassembly: the `sub_1834/sub_18F2/sub_21DC` group belongs to
deferred release work in `sub_604`, not the proven
`sub_8A8 -> sub_68E -> TriggerAutoBattle` route.

Accordingly, the temporary `scene_battle_auto_action_slot` sampler and RAM
watch were removed before any business change. They did not write guest state
or affect a reproduction, but their negative result is not admissible as a
collision conclusion.

The retained, directly observed boundary is `Global_R9+23682`
(`control_state`). In the latest normal `start-server.bat` / `start-player-3`
run, the client registered the ordinary mmGame input callback and reached a
deployed actor at squared distance `0`, yet every direction dispatch saw
`control_state=0`. The enabled native write watch recorded only writes of
zero, including `0x01018158` (the main state setter) with `R0=0`; it recorded
no transition to state `1`. At state zero the main input dispatcher takes the
base-input branch and does not call the registered scene action callback, so
no collision scan or client-originated `WT4/1` can occur.

This restores the actionable root-cause statement: the SCE update has been
installed and rendered after the initial role-selection scene shell exists,
but the normal startup exchange never reaches the client lifecycle that sets
the scene control state to an action-enabled value. A safe repair must supply
the deployed SCE before that shell is constructed, or reproduce an already
proven non-overlapping scene-entry contract. It must not force the state,
invoke `TriggerAutoBattle`, or reuse the already-disproved same-scene
`16/2`/`16/3` re-entry probes.

### WT18/9 protocol-code/manifest-CRC separation (2026-08-25)

The player-3 cache supplied the decisive precondition for the first option.
Its `mmorpg_updateversioncbm` records contain the active release id `19`, the
wire `codeVersion` `1`, and the manifest signed-byte checksum `6360` in a
separate word.  The cached `c04临安府_01.sce` hash also matches the current
server overlay.  The old server compared and returned `6360` as WT18/9
`code`, so the client truthfully reported `19/1` on every restart and was
commanded to delete c04 again.  That made the normal WT18/7 install occur only
after role selection had already created the first scene shell.

The server now treats the fields according to their separate contracts:
WT18/9 compares and returns `release_id` plus protocol code `1`; WT18/8 keeps
using the published signed-byte checksum in `crc`.  A client that has already
processed the current manifest therefore receives `type=0` on its next launch,
keeps the verified c04 cache, and constructs the first scene from that resource
instead of replacing it during the startup shell lifecycle.  This does not
copy files, change CBE/CBM bytes, force control state, or synthesize a battle
request.  A newly changed deployment still has a new release id and therefore
performs the normal one-time invalidation/install before a subsequent restart.

Validation: `make -j2` rebuilt both `bin/main.exe` and
`bin/jh-online-server.exe`; `make content-update-manifest-regression` and
`obj/server/content-update-manifest-regression.exe` passed.  The regression
asserts `WT18/9 {id=77, code=1}` for an outdated client, `type=0` for a
client reporting `77/1`, and the independent `WT18/8.crc=2616` checksum.

### Resource-before-shell reproduction: negative result (2026-08-25)

The first direct standard-launcher reproduction after the WT18/9 correction
reported `mock_update_version ... client_content=19/1 action=current` and
`pending=0`.  No `WT18/7` SCE install was sent in that run, so the deployed
`c04临安府_01.sce` was present before role selection constructed its first
scene shell.  The fireball still rendered and overlap distance reached zero,
but every observed direction dispatch retained `control_state=0`; neither the
native collision route nor a client-originated `WT4/1` followed.

That result disproves resource availability alone as the root cause.  The
manifest-code correction remains required to prevent invalidating a verified
cache on every restart, but it is not a battle-start repair.  The next
evidence target is the first lifecycle caller that gives a proven wilderness
scene an action-enabled control state.  The control-state write watch and the
main setter-entry trace now record a bounded, read-only stack snapshot under
the existing `CBE_TRACE_SCENE_BATTLE_CONTROL_STATE=1` flag.  They do not
alter guest state, PC/LR, requests, responses, or input timing.

## Superseding resource-closure finding (2026-08-25)

The latest direct player-3 reproduction invalidates the claim that Linan was
permanently stuck in `control_state=0`. Its ordinary direction input first
writes `0 -> 1` at `JianghuOL.CBE:0x01016B78`, and later input dispatches
observe `control_state=1`. It still emits no `TriggerAutoBattle` or client
`WT4/1` for the deployed Linan node. Therefore forcing or rebuilding that
state would address a symptom, not the first remaining difference.

In the same run, touching the native Danxia monster produces the complete
automatic path without a click:

```text
scene_battle_trigger  JianghuOL.CBE:0x010183A0
WT4/1 { id=76, index=2, pos=(403,261) }
mock_challenge_battle_start target_source=request-live-node
```

The two SCE records use the same proven kind-3 grammar. `field16` is not a
valid differentiator: the generated Linan row has `5`, while shipped Zhongnan
and Taohua rows also use native values `5` and `6` respectively. The
`automonster.dsh` hangup table is likewise not a supported cause; there is no
client parser/caller evidence tying it to movement collision.

The first concrete asset difference is the generated Linan body's transitive
dependency set. Its `field17` is `e_huayao.actor`, which names
`e_huayao.gif` and `yingzi.gif`; the field18 fireball is
`e_ghostfireR.actor`, which names `e_ghostfiresR.gif`. At reproduction time
the player-3 cache lacked all three body leaves:

```text
e_huayao.actor  absent
e_huayao.gif    absent
yingzi.gif      absent
```

The already-shipped fireball Actor/GIF were present. The actor-resource trace
has no Linan `e_huayao.actor` named-download attempt, whereas Danxia
opens/downloads `e_tiger.actor` before it emits `WT4/1`. This explains why a
standalone fireball can remain visible while the body-backed battle node is
not made usable.

The active MySQL content release (`19`) contained the Linan SCE and
`e_huayao.actor`, but not the Actor's GIF leaves. Because the client already
reported that release as current, it had no new native invalidation cycle in
which to repair that incomplete cache. The old deployment publisher only
listed `scene + field17 Actor + field18 Actor`; validation did inspect GIFs,
but publication discarded their names.

### Repair

`vm_net_mock_scene_battle_monster_collect_publish_names()` now validates each
field17/field18 Actor through the existing authoritative Actor inspector and
adds its complete, deduplicated GIF closure directly after that Actor. Names
are copied into deployment-owned storage before calling the content publisher,
so every pointer remains valid through the native `WT18/9 -> WT18/8 -> WT18/7`
publication. This does not copy resources into a player's cache, change CBE
state, mutate a packet after construction, or manufacture `WT4/1`.

For the current Linan row, a new deploy publishes in this order:

```text
c04临安府_01.sce
e_huayao.actor
e_huayao.gif
yingzi.gif
e_ghostfireR.actor
e_ghostfiresR.gif
```

The new GIF names force a new release once the operator deploys the existing
row through the normal admin action. On the next ordinary client startup the
native updater installs the complete list before the scene is created; no
manual cache copy or launcher isolation is required.

### Verification

- `make -j2` passed after the implementation.
- `content-update-manifest-regression` passed without opening a listener,
  connecting to MySQL, or modifying a cache. It opens the authoritative
  `e_huayao.actor`/`e_ghostfireR.actor` resources and asserts the exact
  six-name deployment closure and deduplication.
- The pre-existing
  `obj/server/scene-battle-monster-field18-regression.exe` still passes its
  shipped-SCE/kind-3 parse checks. The Makefile has no named target for that
  historical executable, so it is supporting structural evidence rather than
  a fresh build target.

Manual acceptance boundary after deploying the new release and restarting
with the normal `start-server.bat` and `start-player-3.bat`:
`e_huayao.actor`, `e_huayao.gif`, and `yingzi.gif` must exist under the shared
player cache; walking into the Linan fireball must then originate the usual
client `WT4/1` without a mouse click. If the complete cache is present and the
request is still absent, the next investigation point is the body Actor's
runtime node creation—not the previously disproven control-state or
direct-entry probes.

### Release 20 normal restart: manifest closure is advisory, not a prefetch

The subsequent direct player-3 reproduction completed a second ordinary
restart after release 20 had been installed.  The server recorded the expected
current-version decision:

```text
mock_content_client_state ... release=20/1 pending=0
mock_update_version ... content=id:20 protocol_code:1 files:9
client_content=20/1 action=current
```

It nevertheless recorded no `WT18/7` request for `e_huayao.actor`,
`e_huayao.gif`, or `yingzi.gif`; those three files remain absent from the
player-3 `JHOnlineData` cache.  The native updater had requested the SCE and
the already-visible fireball resources on the preceding update run, but a
manifest entry only makes a resource eligible for refresh.  It does not cause
the CBE to download an unused leaf on its own.

At the same time the installed `bin/JHOnlineData/c04临安府_01.sce` is the
376-byte deployed resource and its parsed runtime node is still
`actor=1, resource=e_huayao.actor`; normal movement reached the exact same
coordinates as that node.  There was no `TriggerAutoBattle(0x010183A0)`, no
client-originated `WT4/1`, and no server battle-start line.  This disproves the
claim above that a second restart alone completes the deployment precondition.

### Same-client Linan/Danxia control: the server accepts the native request

The requested same-client comparison then produced a native Danxia battle
after the Linan non-trigger.  The client entered `TriggerAutoBattle` at
`0x010183A0` with return address `0x051B5B4B`, emitted its ordinary `WT4/1`,
and the mock server recorded `mock_challenge_battle_start id=76` at the
request's live node position.  The preceding Linan overlap had no trigger
entry and no `WT4/1`.  This makes a server challenge handler or click-input
requirement an excluded cause for the Linan failure.

The first active-tick base probe did not log because it used the fingerprint
address (`module+0x604`) as the tick address.  The two observed live ticks
instead establish the correct relationship:

```text
Linan:  module 05017210 + 0x566 = active tick 05017776
Danxia: module 051B59A4 + 0x566 = active tick 051B5F0A
Danxia: module 051B59A4 + 0x1A2 = BL before battle entry
```

`module+0x604` and `module+0x8A8` remain only byte fingerprints that validate
the inferred code image.  The trace now derives the base from the live
`+0x566` tick and, at `module+0x1A2`, records registers immediately before the
known Thumb `BL` that produces the `TriggerAutoBattle` return address
`module+0x1A6|1`.  It is read-only: it neither prefetches assets nor changes
the cache, guest state, inputs, packets, or battle flow.  The next identical
manual Linan-then-Danxia run will determine whether Linan fails to reach that
callsite or reaches it with a different call context.

### Correction: Danxia return address is the `sub_8A8 -> sub_68E` tail-call chain

The preceding `+0x566/+0x1A2` interpretation was not valid for Danxia.  It
assumed that its active screen logic was the same `sub_566` used by the Linan
screen, without validating the two mmGame fingerprints.  The established
mmGame disassembly gives the correct interpretation of the observed
`TriggerAutoBattle` LR instead:

```text
mmGame sub_8A8 action 2/3/4
  -> BL sub_68E
  -> main API +68(1)
  -> tail path through *(R9+0x2850)+20
  -> TriggerAutoBattle
```

The private callback can preserve the upstream LR, so the successful native
return address is the return from `sub_8A8`'s `BL sub_68E`, not a direct
four-byte call to the CBE battle function.  This exact rule was already
observed for the historical native path: LR-even `0x0502F98A` minus the module
base `0x0502EEC2` equals `0xAC8`.  Applying that offset to the latest Danxia
LR yields a *candidate* base `0x051B5082`; it must still pass the established
`sub_604` and `sub_8A8` byte fingerprints before it is used.

The separately sampled `activeLogic` pointer cannot be subtracted from that
candidate base: its resulting local `0xE88` lies inside the known `sub_D04`
network-item parser, so it is not admissible as a scene-logic identity.  The
earlier statement that Danxia selected `sub_E88` is withdrawn.  What is proven
is narrower: Danxia reached the action-2/3/4 callback route and Linan did not;
the valid kind-3 record and missing `e_huayao` cache leaves remain observations,
not proof of why the action differs.

The next probe derives the module base only from the verified `LR - 0xAC8`
rule at `TriggerAutoBattle`, then records the real `sub_8A8`, `sub_8A8+0x1C`
(the call to `sub_68E`), and `sub_68E` entries.  Their callers and action
arguments will identify the first producer of the native automatic action.
It is read-only; no node callback, client state, or battle request is forced.

The corrected probe was built with `make -j2`.  Because the base becomes known
only when the first native battle enters `TriggerAutoBattle`, the next manual
comparison must touch a native Danxia fireball once to establish that base,
then touch one again after returning from battle to capture the upstream action
entries.  Linan remains the negative control in the same ordinary player-3
session.

### Correction: current Danxia return address is not `mmGame+0xAC8`

The next direct player-3 run supplied a stronger, same-image base fact than
the earlier return-address subtraction.  The scene init registered its normal
main API `+52` callback as `0x0518F269`; the already-established dual-byte
fingerprint accepts the even address `0x0518F268` as
`0x0518E9C0 + 0x8A8`.  That is the live `mmGame` input action callback, not a
guessed screen address.

In that same code image, the native Danxia `TriggerAutoBattle` entry preserved
LR `0x0518F3EB`.  Its even return address is therefore
`0x0518F3EA - 0x0518E9C0 = 0xA2A`, not `0xAC8`.  The earlier `LR - 0xAC8`
rule was an over-generalization from an older allocation and is withdrawn.
It was removed from the read-only trace; it had not written client state or
changed any gameplay path.

This run also completed a normal portal transfer back into Linan before its
last negative collision.  The re-entered Linan node still parsed as
`actor=1, resource=e_huayao.actor`, movement overlapped the fireball, and
there was still no `TriggerAutoBattle` or `WT4/1`.  Thus ordinary cross-map
entry does not repair the difference, and the next comparison is limited to
capturing the real registered callback at `+0x8A8` on a second native Danxia
collision.  No deployment, callback, packet, cache, or input is synthesized.

### Read-only trace correction: preserve verified action callbacks across battle screens

The two subsequent Danxia collisions both reached `TriggerAutoBattle` and the
normal server battle start, but no `scene_battle_action` entry was retained.
The loss is in the host trace, not the client route: the battle screen replaces
`vmAddedScreen` before the registered input callback's tail path reaches the
existing CBE trigger hook, and the previous probe cleared its screen-local
callback address at that boundary.

`src/main.c` now keeps only the most recent four callback addresses that were
accepted by the existing `sub_604` and `sub_8A8` dual-byte fingerprint.  On a
later instruction hook matching one of those already registered addresses, it
emits `scene_battle_action phase=input-dispatch-retained` with registers and
the verified module base.  It records no unverified pointer, and it neither
writes guest memory nor changes callback dispatch, input, packets, resources,
or battle state.  `make -j2` succeeds.  One restarted native Danxia collision
is the next evidence step; a second collision after return remains a useful
same-session confirmation.

### Read-only capture now follows the actual client slot write

The prior callback-base check was intentionally conservative, but it did not
accept the native Danxia callback image: the live slot held `0x0503A9F1`, while
subtracting the Linan-specific `0x8A8` did not pass the mmGame fingerprint.
That proves the two callbacks must not be silently treated as the same
function.  It does not by itself establish why their action outcomes differ.

The host trace now retains the last **nonzero** client write to
`Global_R9+0x5D28` in a host-only variable.  The instruction hook observes a
PC equal to that pointer and logs
`scene_battle_action phase=input-dispatch-slot-write` with its real register
arguments.  Clearing the guest slot never overwrites the host observation, so
the short transition into the battle screen cannot hide the entry.  This adds
no guest write, no callback invocation, no input, and no packet.  `make -j2`
succeeds.  One restarted Danxia walk-in collision is sufficient for the next
capture; Linan remains an unchanged negative control.

### Native input callback slot is the authoritative dynamic action identity

The following Danxia reproduction supplied the missing registration evidence
directly from the existing read-only write watch on `Global_R9+0x5D28`:

```text
input_callback write: 0x0503A9F1
TriggerAutoBattle LR: 0x0503A40B
WT4/1 -> mock_challenge_battle_start id=76
```

The same relationship is present in earlier native allocations
(`0x051B6131 -> 0x051B5B4B` and `0x0518F9D1 -> 0x0518F3EB`).  The slot write
is therefore the live, client-owned input callback that precedes automatic
battle; it is not the older main-API registration address used by the Linan
scene shell.  The relative return site is consistently callback-even minus
`0x5E6`, but that arithmetic alone is not treated as a contract.

The trigger trace now reads the live callback slot at the already-entered
`TriggerAutoBattle` boundary, derives `callback - 0x8A8` only when the same
two executable-byte fingerprints pass, and retains that verified address for
the next action entry.  This is still read-only and does not alter the slot,
callback, controller, input event, resource cache, network packet, or battle
state.  `make -j2` succeeds.  The next manual run needs two ordinary Danxia
walk-in collisions: the first establishes the native callback image, and the
second records its entry parameters.  Linan is left unchanged as the negative
control.

### Correction: the input slot is not the automatic-battle producer

The next direct Danxia run reached `TriggerAutoBattle`, emitted the normal
`WT4/1`, and completed the normal server battle start, but it produced no
`scene_battle_action phase=input-dispatch-slot-write` entry. The host-only
watch had already retained the nonzero slot value, so this is evidence that
the stored callback is a scene-controller/input callback rather than the
instruction stream that directly invokes automatic battle.

Accordingly, the preceding section's description of that slot as the
"authoritative dynamic action identity" is withdrawn. The verified facts are
narrower: Danxia writes the slot and later enters `TriggerAutoBattle`; Linan
does not enter `TriggerAutoBattle`. Their causal relationship remains
unresolved. The next read-only probe is the resource lifecycle around the
Linan body Actor, whose cache leaves are absent despite the visible child
fireball. No callback, client state, packet, resource, or input is changed.

### Root cause: generic Actor acceptance admitted a non-combat field-17 body

The resource-lifecycle probe resolved the remaining upstream difference.  The
deployed Linan scene did create and look up the field-18 child effect:

```text
scene_asset_lifecycle ... asset=e_ghostfireR.actor
scene_asset_lifecycle ... asset=e_ghostfiresR.gif
```

It never performed a lookup for `e_huayao.actor`, `e_huayao.gif`, or
`yingzi.gif`, although the generated kind-3 record contains
`field17=e_huayao.actor`.  In the same ordinary player-3 process, a native
Danxia encounter did look up `e_tiger.actor` before its ordinary walk-in
`TriggerAutoBattle -> WT4/1` sequence.  Thus the first divergent client state
is body-node resource selection, before input handling, battle requests, or
the mock battle handler.

The prior server validation was too broad: every existing `.actor` other than
the four known fireball effects was permitted as a field-17 body.  The actor
descriptor for `e_huayao.actor` is structurally parseable, but that is not the
client contract for a collision-capable kind-3 node.  It occurs in no shipped
kind-3 record; its only kind-3 use was the generated Linan deployment.  This
is direct evidence that a general Actor catalogue is not a safe source for
scene-battle bodies.  It does not assert an unproven one-to-one mapping between
monster ID and Actor resource.

The repair builds an immutable compatibility catalogue by scanning the base
resource root through `vm_net_mock_scene_battle_monster_read_base_raw()`, not
the active database overlay.  The current data set yields 53 selectable body
Actors from 478 native kind-3 records across 196 SCE files.  It also sees 33
native fireball-only markers; those known effects remain deliberately excluded
from field-17 body selection.  This distinction prevents an invalid generated
SCE from becoming its own future compatibility evidence.

Both the admin picker and the save/deploy validation use this catalogue.  An
old invalid draft is preserved rather than silently rewritten; it is displayed
as incompatible and deployment is rejected until an operator explicitly picks
an observed native body.  The smallest direct A/B replacement for the Linan
row is the already observed native pair `monster_id=76` and
`actor_resource=e_tiger.actor`, retaining `e_ghostfireR.actor` as field 18 and
the existing coordinates.  After saving and deploying that explicit choice,
restart the service and normal player-3 client, enter Linan, and walk into the
fireball.  Expected evidence is the client-owned `TriggerAutoBattle`, followed
by `WT4/1`; no mouse click is part of the acceptance criterion.

### Verification

- `make -j2` passed after the compatibility repair.
- A new named `scene-battle-monster-field18-regression` target passed without
  starting a listener, connecting to MySQL, or writing game resources.  It
  asserts that native `e_tiger.actor` is accepted, `e_huayao.actor` is
  rejected, all 196 shipped SCE entity lists parse through the counted-record
  grammar, and generated kind-3 record insertion remains deterministic.

### Correction: a native Actor alone is still insufficient

The next direct player-3 reproduction used an Actor from the new body
catalogue but retained a different monster ID.  The published node was
`actor=1001, resource=e_monkey.actor`; an earlier equivalent experiment was
`actor=1001, resource=e_tiger.actor`.  Both formed visible nodes and fireballs
but never entered `TriggerAutoBattle` or emitted `WT4/1`.

The existing native control is decisive: the shipped node
`actor=1000, resource=e_monkey.actor` reached the collision callback with a
successful return and immediately produced `WT4/1` carrying ID 1000.  This
proves that field 14 and field 17 are one client-facing identity contract, not
independent gameplay-stat and visual choices.

The compatibility catalogue therefore now records the complete pair
`(monster_id, actor_resource)` from immutable base SCE2 resources.  It has 82
distinct deployable pairs after excluding 33 fireball-only markers.  Save and
deployment reject a body that is valid only for another monster; the admin
page also marks existing mixed drafts as invalid.  The direct Linan replacement
for the currently published `1001 + e_monkey.actor` row is the exact native
pair `1000 + e_monkey.actor`, retaining the known field-18 fireball effect and
the existing position.  `76 + e_tiger.actor` is an independent valid native
pair; `1001 + e_tiger.actor` is not.

The pair-only conclusion is based on the two mixed negative runtime controls
and the native positive control.  It does not yet assert whether a field-16
visual hint or field-18 effect must also be coupled to the same source record;
the current selected values remain within the independently verified native
contracts.  The next manual acceptance test must run against a rebuilt server
and use an exact pair before further client-side tracing is justified.

### Correction: configured monster IDs are not native identity evidence

The player clarified that `1000` was a player-configured ID, rather than the
ID of a shipped native control.  Therefore the preceding comparison cannot
establish a required field-14/field-17 pair: it compared generated
configurations, not a native positive control.  The apparent pairing rule and
the 82-pair compatibility catalogue were withdrawn from the server and admin
page.

The deployment contract again treats the configured monster ID independently
from the field-17 body Actor.  The body Actor must still be one observed in a
base SCE2 kind-3 record, excluding known field-18 fireball-only effects; that
remains evidence-based validation for the earlier `e_huayao.actor` failure,
not a claim that a particular ID owns that Actor.

The first unresolved deviation is unchanged: the Linan generated node is
visible and reaches collision handling, but does not enter
`TriggerAutoBattle` or emit `WT4/1`.  The next step is a read-only comparison
of its scene-controller route with an actual shipped wilderness control, with
no further restrictions on the player's monster-ID choices.

### Correction: earlier collision trace targeted actor 1, not the deployment

Inspection of `bin/multiplayer/start-player-3.bat` found that it set
`CBE_TRACE_SCE_NODE_ACTOR_ID=1`.  The collision probe filters all of its
nearest-node, callback, and scheduler observations by that value.  The
configured little-monkey deployment uses the player's own ID `1000`, so those
filtered observations were for an unrelated live node.  In particular, they
cannot be used as evidence for an ID/Actor pairing rule or for a Linan-specific
failure before the correct node has been observed.

The launcher now defaults the probe to `1000` and still permits an explicit
environment override for a differently configured test node.  A fresh
reproduction must be captured before naming the first client-side deviation;
the present status is `unresolved`, not a confirmed `TriggerAutoBattle`
absence for monster `1000`.

### Root cause: generated field 16 selected the generic node class

The corrected player-3 trace captured the configured Linan node itself:
`actor=1000`, `resource=e_tiger.actor`, `kind=3`, and collision function
`0x01004ce9`.  Its runtime API table was the generic main table
`0x0500b210`, where `TriggerAutoBattle` is present at slot 108 but is never
called by the scene scheduler.  The player reached a nearest-node distance of
15 pixels; there was no entry at `JianghuOL.CBE:0x010183a0`, no collision
callback, and no new `WT4/1` at the service.

The first generated-record difference is field 16.  The deployed Linan
record encoded `field16=5`, because the admin schema had limited the control
to the invented choices 5 and 6.  Scanning the immutable base SCE2 resources
with the same parser that writes deployments shows that the shipped
`e_tiger.actor` kind-3 record carries `field16=17`; `e_monkey.actor` carries
`field16=5`.  Field 16 is therefore a node-class selector tied to the body
Actor profile, not an ordinary/strong difficulty choice and not an actor-ID
pairing rule.

The generator now resolves field 16 from the selected body Actor's native
profile immediately before writing the kind-3 record.  A configured monster
ID remains independent.  Actor resources whose shipped records contain more
than one field-16 value are excluded from automatic deployment rather than
guessing.  The regression checks `e_monkey.actor -> 5`,
`e_tiger.actor -> 17`, and verifies that a stale stored value of 5 is emitted
as 17 for an e_tiger deployment.

Verification: `make scene-battle-monster-field18-regression` passed after the
change; its immutable-base scan found 53 body Actors, 6 with ambiguous field
16 profiles, and 33 fireball-only markers.  Final manual acceptance requires
re-deploying the existing Linan draft with the rebuilt service, then observing
the normal `WT4/1` followed by the existing `WT4/5` battle response.

### Deployment-verification correction: latest reproduction still used the old SCE

The subsequent player report of “still no battle” cannot yet confirm or reject
the field-16 repair.  Read-only inspection of the active overlay
`c04临安府_01.sce` found its last-write time was `22:04:21`, while the tested
server/client session began at `22:27`.  Its generated kind-3 record for
configured actor ID 1000 still contains the scalar sequence
`01 00 10 00 05 00` (field 16 = 5), followed by `e_tiger.actor`.  A deployment
produced by the repaired writer must instead contain
`01 00 10 00 11 00` (field 16 = 17).

The matching server interval contains no scene-battle deployment transaction,
content publication, or new content release.  The latest client trace still
shows the original no-callback/no-`WT4/1` behavior, but it is therefore a
reproduction of the pre-repair resource.  Status of the field-16 hypothesis is
`pending actual redeployment`, not confirmed root cause.  The required next
evidence is a successful deployment from the rebuilt service, followed by a
new content release and an overlay whose field 16 bytes are `11 00` before
interpreting the next touch result.

### Release-24 timing correction: the repaired SCE was installed after this scene shell

The next reproduction did publish and download the repaired file, so the
previous deployment-gap status is closed.  The active overlay was rewritten at
`22:30:53`; its generated record contains
`01 00 10 00 11 00` (field 16 = 17) for `actor=1000` and `e_tiger.actor`.
However, the same server trace establishes that this was the *first* client
session for release 24, not a startup from the repaired cache:

```text
mock_title_role_select                          -> WT 1/6
mock_scene_compact_skill_default                -> initial scene shell
WT 18/7 c04临安府_01.sce final install            -> install_generation=1
mock_startup_sce_install_scene_enter_suppressed -> control acknowledgement only
```

Thus the role-select response constructed the live Linan shell before the new
SCE arrived.  The server correctly does not hot-replace that shell with the
already-disproved same-scene `16/2` or `16/3` re-entry.  Its later movement
trace consequently remains on the pre-install scene route: `control_state=0`,
no entry at `TriggerAutoBattle`, and no `WT4/1`.

This result is not a valid acceptance or rejection of the field-16 record,
because the changed bytes were present on disk but not in the running scene.
The required next control is one additional clean player restart against the
unchanged release 24, with no new deployment: WT18/9 must report the release
as current and no `WT18/7(c04临安府_01.sce)` may occur before role selection.
Only that second launch constructs the first Linan shell from the repaired
file.  The field-16 hypothesis remains pending that control; the service must
not force a battle, mutate client state, or perform a same-scene re-entry.

### Release-24 cached-shell control: field 16 and resource timing are insufficient (2026-08-25)

The required second launch was completed against the unchanged release 24.  It
reported the content as current (`24/1`, `pending=0`), requested no
`WT18/7(c04临安府_01.sce)`, and built the first live node from the repaired
overlay.  The node is `actor=1000`, `e_tiger.actor`, at `(120,120)`, with the
same runtime tail (`1,2,0,0,0,1`) and collision routine (`0x01004CE9`) as a
native combat node.  No client-originated `WT4/1` followed the reported touch.

The generated kind-3 bytes also now match the native Danxia `e_tiger` grammar
exactly, apart from the intentional position, display name, and actor id:

```text
03 x y 05 00 01 00 0e <actor-id> 03 0f <name>
01 10 11 00 03 11 e_tiger.actor 03 03 e_ghostfireR.actor
```

Both SCE headers carry `scene_flag=1`; adding another monster field or copying
that header flag would therefore be an unsupported mutation.  The earlier
field-16 repair remains necessary to emit the native `e_tiger` profile, but it
is not by itself a battle-start repair.  Resource-before-shell timing is also
excluded for this cached control.

The `R9+23852` observation must likewise not be promoted to a root cause:
static `TriggerAutoBattle(0x010183A0)` does not read it.  Its proven inputs are
the occupied live kind-2 node, the node collision callback, `R9+23768`, and
the normal UI/busy gates.  The missing first event is instead the *caller*
that invokes `TriggerAutoBattle`; it is absent in the Linan role-select shell.

The current server log establishes the remaining lifecycle difference.  The
Linan login path is `WT1/6` actor-info followed by compact/task follow-ups and
explicitly ends with `action=no-second-scene-enter`; it emits no `WT30/1` or
position-bearing `WT30/2`.  A normal portal/cross-map transition owns a new
destination shell through `WT30/1`, followed by its single `WT30/2`
completion.  The next safe discriminating control is therefore to leave
Linan-01 through an existing portal and return normally to the unchanged
Linan-01 resource, then touch actor 1000.  This must be observed before
changing login routing: same-scene `16/2`/`16/3`, client-state writes, or a
synthetic `WT4/1` remain prohibited because they bypass or corrupt the missing
lifecycle boundary.

### Root cause: `c` scene keys select the non-wilderness screen (2026-08-25)

The normal portal control was completed and it falsifies the preceding
lifecycle hypothesis.  The player moved from `c04临安府_01.sce` to
`c04临安府_05.sce` through the source portal and then returned through the
ordinary destination flow.  The observed response for each destination was
the normal transport `WT2/3` containing its position-bearing `30/2` object;
there is no evidence that this route first requires a `30/1`, so the preceding
`30/1 -> 30/2` wording is withdrawn.  After the return, the repaired native
`e_tiger` node still existed and the client received normal movement input,
but there was no `TriggerAutoBattle` entry or client `WT4/1`.

Static CBE evidence identifies the first durable difference instead.  At
`JianghuOL.CBE:0x0100EEBC`, the client classifies the requested scene key: a
successful special-name comparison, or simply a first byte of `0x63` (`'c'`),
returns class `1`; other ordinary scene keys return class `0`.  Its caller
`EnterSceneByMapName(0x0101809C)` invokes that classifier at `0x010180BC` and
uses class `1` to request scene mode `3`, versus mode `4` for class `0`, at
`0x0101813C..0x01018142`.

Thus `c04临安府_01.sce` is unavoidably constructed as the town/special screen
solely because of its `c` prefix.  A native wilderness key such as
`03丹霞山_01.sce` takes the other screen mode.  Runtime evidence agrees: the
Linan return uses screen `0x01053450` and its normal input logic
`0x05017776`, while the Danxia control uses screen `0x01053F78` and reaches
`TriggerAutoBattle(0x010183A0)`.  The generated kind-3 node can therefore be
valid, visible, and collision-capable without the town screen ever scheduling
the wilderness automatic-battle route.  Changing field 16, resource timing,
portal re-entry, a delegate, or a battle request cannot change this filename
classification.

The bounded confirmation uses
`bin/multiplayer/start-server-linan-battle-mirror-test.bat`.  It prepares the
explicit test key `04linan_battle_01.sce` from the currently deployed
`c04临安府_01.sce`, starts the ordinary service port/database with
`CBE_SCENE_KEY=04linan_battle_01.sce`, and leaves the original SCE and player
data untouched.  The batch resolves its source with the ASCII-only pattern
`c04*_01.sce`, avoiding cmd.exe code-page decoding of the Chinese filename.
`start-player-3.bat` remains the client launcher.  This changes the client
scene key, not its primary map payload: the test SCE still refers to the same
`04临安府_01.map` and contains the deployed fireball.

Acceptance is a client-owned `TriggerAutoBattle -> WT4/1` on walking into the
same fireball.  A pass proves that c-prefixed town-key classification is the
missing contract; the permanent deployment design must then generate and route
an explicit non-`c` combat mirror for a city scene, rather than trying to make
the city screen behave like a wilderness screen through packet or memory
changes.  A failure leaves the mirror experiment as negative evidence and
does not justify altering the client's screen mode directly.

### Mirror v1 crash: a wilderness screen also requires a background SCE (2026-08-25)

The first mirror control reached the intended missing boundary: collision callback
returned `1`, the client emitted the normal `WT4/1{id=1000,index=4,pos=(120,120)}`
request, and the service returned its ordinary 185-byte battle response. The packet
is therefore not the first violation.

The earliest mirror-specific resource evidence instead shows no nested background
scene: `actor-resource-cache.log` opens `04linan_battle_01.sce` but no `b_*.sce`, and
`actor-scene-node-capacity.log` shows `battle-background ... base=00000000`. A native
Danxia control opens `b_03丹霞山.sce`; its child descriptors use the dedicated table-B
allocator, leaving a nonzero two-row battle-background table. Static tracing defines
this distinction: descriptors whose resource name starts with `b` are allocated from
`R9+0x5CB4`, not the ordinary 25-slot scene array.

The city source's only named portal has an empty inline field-18 value, whereas
`03丹霞山_01.sce` has the native field-18 background reference
`b_03丹霞山.sce`. Consequently the mirror activates the wilderness collision route
without the background actor table it needs. The first observed null access is in the
mmGame scene code at `0x05017784` (address `0x8`); the later
`SetMapCtrlViewport(0x01046C48)` read at `0x40` is a consequence and must not be
treated as the root cause.

`tools/prepare_linan_battle_mirror.py` now makes the disposable mirror by decoding the
source SCE, replacing only that empty field-18 value with the existing, native-parseable
`b_03丹霞山.sce` reference, validating the decoded result, then writing a valid literal
type-2 resource. The original city SCE is read-only. The next acceptance criterion is
that client startup opens the background SCE and allocates its two background rows before
the same client-owned `TriggerAutoBattle -> WT4/1`; only then may battle-start behaviour
be evaluated. This is still a bounded compatibility probe, not a permanent claim that a
Danxia backdrop is the correct final presentation for a Linan combat mirror.
