# Runtime Scene Transfer Single-Load Contract

Date: 2026-07-16

Status: validated on `c00蓬莱仙岛_01 <-> 00蓬莱仙岛_02`

## Symptoms

- A runtime edge-portal transfer initialized the destination scene locally, then
  restarted the same loading screen after the `2/3` response.
- Entering `00蓬莱仙岛_02` also opened the resource-update flow for a file named
  `小猴子`.

## Resource Evidence

The destination resources are present in both the client cache and the service
resource tree. The corresponding `.sce`, `.map`, and directly referenced
`.actor` files are readable. The update request was not for one of those files:

```text
mock_update_chunk_missing subtype=7 file=小猴子 version=1
```

This rules out a missing scene bundle as the cause of the update UI.

## Client Evidence

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `scene_parse_npcinfo_and_spawn_npcs` / `0x01037998` | Each `27/11 npcinfo` row reads three integers, four strings, then the final actor id. The fourth string is registered into the node visual slot. |
| `江湖OL.CBE` | `RegisterDisplayName` / `0x0100EEE0` | The registered string is copied into one of four scene visual/name slots used by the created node. |
| `江湖OL.CBE` | `parse_scene_response_entry` / `0x010396D6` | `30/1 scene+posinfo` invokes the scene-enter vtable. |
| `江湖OL.CBE` | `parse_scene_posinfo_field` / `0x01039770` | `30/2 result=1 scene+posinfo` invokes the same scene-enter vtable; `result=2` without `posinfo` is acknowledgement-only. |
| `江湖OL.CBE` | `ui_apply_named_posinfo_target` / `0x0100E9B8` | `27/12 name+posinfo` also reaches the scene-enter vtable and is not a no-reentry coordinate update. |

## Root Causes

### Duplicate loading

The local SCE edge-portal path had already created and initialized the target
scene screen before the response was dispatched. The old full-bootstrap response
then appended both:

1. `30/2 result=1 + scene + posinfo`
2. `30/1 scene + posinfo`

Both call `EnterSceneByMapName(0x01018150)`, so one response restarted the same
scene twice. Deferred post-enter/task-subset completion paths also appended a
second `30/2` after `27/12`.

The two portal directions use different completion shapes:

- `c00蓬莱仙岛_01 -> 00蓬莱仙岛_02`: the initial `WT 2/3 len=74`
  still requires one position-bearing `30/2` to leave loading. The redundant
  `30/1` is omitted.
- `00蓬莱仙岛_02 -> c00蓬莱仙岛_01`: the initial `2/3` records the target and the
  later mmGame `25/5` follow-up runs after the destination shell is active. Its
  scene object is now `30/2 result=2` without `posinfo`, so it does not re-enter.
- Deferred completion retains one position-bearing object only; `27/12` is no
  longer followed by another `30/2`.

### False resource update

The `27/11 npcinfo` builder wrote `displayName` into both the visible-name field
and the fourth string. For the monkey row this made the scene node resolve
`小猴子` as its visual resource key. The fourth string now carries
`e_monkey.actor` (and the smith row carries `n_blacksmith.actor`).

## Validation

Build:

```text
make -j2
```

Forward portal (`c00蓬莱仙岛_01 -> 00蓬莱仙岛_02`):

```text
WT 2/3 len=74 -> builtin-scene-change resp=392
caller=01018150 count=1
target init count=2 (local shell + one required completion)
ready=1 assets=1 parserState=7 pos=(128,57)
npc 20020 pos=(338,125)
npc 20021 pos=(376,125)
```

Reverse portal (`00蓬莱仙岛_02 -> c00蓬莱仙岛_01`):

```text
WT 2/3 len=75 -> builtin-scene-change resp=96
WT 25/5 len=44 -> builtin-mmgame-scene-transfer-followup resp=371
caller=01018150 count=0
destination screen init count=1
```

Both runs completed without an assertion or inaccessible-address fault. No
`18/7 file=小猴子` request appeared after the corrected NPC row was delivered.

## Remaining Scope

Other scene families should retain the same invariant: one position-bearing
completion at most per transition serial. A route that already completes its
local destination shell should use acknowledgement-only output; a route that
remains on loading must receive exactly one evidence-backed scene-position
completion.
