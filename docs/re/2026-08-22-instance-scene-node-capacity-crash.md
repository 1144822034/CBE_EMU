# 副本进入后 ActorSceneNode 空指针崩溃

## 现象

进入 `测试地图.sce` 后，客户端在收到副本进入响应后崩溃：

```text
地址无法访问:13b type:0 size:20 value:300000001
lr=0x01017801 pc=0x0100DA4E
```

服务端日志显示 `30/1` 已发送，但后续没有正常的场景完成包。

## 最终症状

IDA 证据表明 `0x0100DA4E` 位于 Actor motion descriptor 解析函数。该位置会对两条
分配路径的返回值无条件写入 `node + 0x13B`；两条路径都只遍历 25 个
`ActorSceneNode` 槽位。节点分配返回空指针是已经确认的崩溃前错误状态，但尚不能视为
完整业务链路中的第一次偏离。

崩溃的 `lr=0x01017801` 位于 `AllocActorSceneNode` 调用
`scene_node_claim_slot` 的返回点，证明本次走的是 `0x0100DA14` 分支，而非此前推测的
`scene_node_find_or_create(0x0100DA42)` 分支。这个分支只用于资源名首字节为 `b` 的
descriptor，目标并非第二张固定 25 槽场景表，而是 `R9+0x5CB4` 指向的战斗/背景 Actor
数组。

## 首次偏离与根因

本次 `player-3` 运行时日志给出了完整的首次偏离：

```text
首次 b_01桃花岛.sce: count=2
array base=0505a490
child 0 -> 0505a490
child 1 -> 0505a5e4

再次 b_01桃花岛.sce: count=2
child 0 -> 0505a9e0 = base + 4 * 340
child 1 -> 0505ac88 = base + 6 * 340
```

`AllocBattleActorArray(0x01017E14)` 只在 `R9+0x5CB4 == 0` 时分配
`descriptor_count * 340` 字节并清零；本次真实容量因此只有 2。再次进入场景时该指针仍
非零，函数直接返回而不重新分配。`AllocActorSceneNode(0x010177DA)` 却固定扫描 25 个
候选，于是把数组后的堆内存当作节点读取，并在第 4、6 个伪空闲候选上写入。这里已经早于
最终空指针崩溃，是本次可证明的第一个错误客户状态。

`FreeBattleActorArray(0x01017E3C)` 会释放并清空该指针，而且在
`scene_object_vtable_init:0x01019630` 被注册为场景清理回调。它没有在第二次场景解析前
运行的原因位于宿主 screen 管理器：同一个活动场景 screen 再次 `AddScreen` 时，
`vm_screen_stack_push_with_data_package()` 先从栈中静默移除旧条目再重新压入；调用方随后
仅按重压后的栈深度选择 `pause`。当栈下还有登录/对话 screen 时，活动场景被 pause 而不是
destroy，紧接着同一场景对象再次 init。旧场景的释放回调因此被跳过，新场景解析复用了旧
数组。

根因不是 Actor 判断过严、SCE kind-3 怪物记录、静态场景槽数，也不是在崩溃地址缺少空
指针保护；被违反的是宿主对“活动 screen 被同 screen 替换”所拥有的 destroy-before-init
生命周期契约。

此前仅统计 SCE 静态节点、27/11 NPC 和顶层战斗怪。运行时 motion descriptor 是否还会
创建额外隐藏节点、各 `.actor` 对应多少节点，目前没有可靠的分配跟踪证据，不能从磁盘
manifest 字节布局臆测。

## 只读取证探针

`src/main.c` 新增环境变量控制的只读探针：

```text
CBE_TRACE_ACTOR_SCENE_CAPACITY=1
CBE_TRACE_ACTOR_SCENE_CAPACITY_MAX=128
```

输出到 `logs/actor-scene-node-capacity.log`，覆盖
`0x0100D6E2/0x0100D872/0x0100D942/0x0100D97E/0x0100DA14/0x0100DA42/0x0100DA4E`。
每条记录包含当前场景、第五参数资源、descriptor 地址、运行时 `+0x10` 计数、子项索引、
分配分支和结果；在分配边界额外导出固定 25 槽场景表，并按实际观测到的分配容量导出
战斗/背景 Actor 数组。探针还记录 `AllocBattleActorArray`、`FreeBattleActorArray` 和场景
init 清零计数的 PC。槽位的 `+319 occupied` 和 `+315 kind==2` 正是
`scene_node_claim_slot(0x0100EF7E)` 拒绝 claim 的条件。

该探针不写客户内存、寄存器、PC/LR、返回值或响应字节，并有默认 128 条、最高 1024
条的普通记录硬上限。`0x0100DA4E` 上的空分配结果会无条件越过普通配额写出一次，避免
登录和来源场景提前耗尽配额后丢失崩溃前快照。其用途是固定第一次容量耗尽前的资源和
节点归属，不作为业务成功证据。
多人客户端公共启动器默认启用该探针，因此 `player-1` 到 `player-4` 的下一次复现会在各自
隔离的 `multiplayer-data/<player>/logs/actor-scene-node-capacity.log` 写入证据；调用者仍可
用 `CBE_TRACE_ACTOR_SCENE_CAPACITY=0` 显式关闭。

## 容量计数缺口

对 518 字节基础 SCE 和 533 字节发布 SCE 的逐条解析表明，两者在新增战斗怪前都已有
4 个 Actor 实体：两个 `n_shop.actor`、一个 `n_man2.actor` 和一个
`n_oldman.actor`。当前 `vm_net_mock_scene_battle_monster_payload_collect_node_count()` 仅统计
prop placement 和 kind-3 combat spawn，对这些原有 Actor 实体计数为零。因此服务端当前
输出的 `static` 容量值不是客户端真实节点占用，部署和进入检查都可能错误放行。

IDA 还确认 `scene_rebuild_runtime_nodes(0x0100F7A6)` 只遍历重置
`R9+0x5CB0` 指向的固定 25 槽场景表。此前把 `R9+0x5CB4` 后方也打印为连续 25 槽是错误
解释；其真实容量由第一次 `b_*.sce` 的 descriptor count 决定。本次只有 2 槽，后方数据
是相邻堆内存，不能用于静态容量统计。

## 本轮验证

- `make -j2`：通过。
- `screen-active-reentry-lifecycle-regression`：通过；覆盖同活动 screen 在深栈中重入必须
  destroy、不同 screen 在深栈中仍 pause、根 screen 使用 destroy。
- `actor-scene-node-reserve-regression`：通过。
- `scene-battle-monster-field18-regression`：通过。
- `scene-transition-entry-contract-regression`：通过。
- `instance-guide-direct-entry-regression`：通过。
- 根因修复后的隔离客户端复现未执行：当前进程没有 `CBE_AUTOMATION_MYSQL_PASSWORD`，自动化运行器
  按约束拒绝创建隔离 schema；没有改连或写入 `jh_online` 代替。

## 修复

- 宿主 screen 管理器在处理 AddScreen 前比较请求 screen 与当前活动 screen。两者指向同一
  栈条目时，无论底层栈深度多少都选择 destroy，不再 pause；旧场景 destroy 完成后才 init
  新场景。普通弹窗压入不同 screen 时仍保持 pause 语义。
- `screen_lifecycle.h` 固化上述纯决策，
  `screen-active-reentry-lifecycle-regression.c` 覆盖同 screen 深栈重入、不同 screen 弹窗
  压栈和根 screen 三类边界。
- 只读容量探针不再把战斗/背景数组臆测为 25 槽，而是从真实 alloc/free PC 维护观测容量。
- 先撤销了错误的 `.actor` 文件 motion descriptor 解析：后台和原生资源使用的是压缩
  image/animation manifest，不能按运行时对象布局读取。当前 Actor 子节点预留保持为
  0，不会误拒绝任何可正常预览的 Actor。
- 保留部署和传送前的顶层节点容量检查；运行时子节点映射仍标记为 unresolved，待取得
  客户端分配跟踪后再增加准确计数。
- 非法路径和非 `.actor` 后缀仍会被拒绝；合法 Actor 仍必须通过既有 manifest 结构和
  GIF 依赖完整性检查。
- 未修改客户端内存、寄存器、PC/LR 或伪造响应。

## 验证

- `make -j2`：通过。
- `scripts/actor-scene-node-reserve-regression.c`：通过。测试实际加载
  `e_ghostfireR.actor`、`e_monkey.actor`、`n_woman1.actor`，确认三者的 manifest/GIF
  依赖检查和容量入口均通过；同时确认路径穿越、错误后缀和带子路径的名称被拒绝。
# Follow-up evidence (2026-08-22)

The generated `测试地图.sce` payload ended at the effect Actor string, while
the shipped native scene entity stream ends with a zero `u16` terminator (or a
kind-8 control record that includes that terminator). The server scanner
previously treated EOF as a valid boundary and appended a battle record after
it. That was weaker than the client SCE install contract and left the final
Actor node lifecycle unterminated before the motion-descriptor path at
`0x0100DA4E`.

The deployment path now recognizes a trailing zero-word boundary, inserts
battle records before it, and emits the native zero-word terminator when an
enabled battle record is appended to a legacy scene that omitted it. The
field-18 regression also covers the zero-word insertion boundary. No client
memory, register, parser branch, or response bytes are modified to suppress
the crash.
# 2026-08-22 update: database-scoped scene publication

## First divergence

The crash at `JianghuOL.CBE:0x0100DA4E` is the final null-node symptom, not
the first bad state. Runtime and database evidence now show that the first
divergence happens before the client parses the scene:

- `jh_online` and `jh_online_release` both publish generated SCE bytes into
  the same writable `web/fs/JHOnlineData` directory.
- The two databases have different battle-monster drafts, sources and
  deployment fingerprints, but WT 18/7 downloads one shared file.
- `jh_online_release` captured a 518-byte already-generated `测试地图.sce` as
  its base even though that database has no enabled monster row for the scene.
- The current shared file therefore cannot be proven to belong to the active
  database. A matching SQL fingerprint alone does not establish that the
  bytes served to the client are that database's publication.

This violates the resource ownership contract: one authoritative database
must own both the publication ledger and the mutable bytes represented by that
ledger. Once the wrong SCE is installed, its Actor motion descriptors can
exhaust the fixed 25-entry `ActorSceneNode` table; `scene_node_find_or_create`
then returns zero and the unchecked caller stores through address `0x13b`.

## Implementable fix

Treat the configured `JHOnlineData` directory as an immutable base resource
tree and place generated scene publications in a database-scoped overlay:

```text
<resource-root>/.cbe-overlays/<CBE_MYSQL_DATABASE>/<scene>.sce
```

The server must:

1. capture a missing `server_scene_battle_monster_sources.base_resource` only
   from the immutable configured resource tree, never from an overlay or a
   previously generated shared file;
2. write scene battle-monster deployments only to the active database's
   overlay;
3. prefer that overlay for scene reads, checksums and WT 18/7 downloads, while
   falling back to the immutable base for resources which have no override;
4. require an overlay file for `deployed_source_matches` and entry capacity
   checks, so a legacy shared publication cannot be treated as current merely
   because its bytes happen to contain the configured monster row.

Existing deployments created before this fix intentionally become stale until
they are explicitly redeployed. This is a data migration boundary, not a
runtime fallback: silently accepting the old shared file would preserve the
cross-database ambiguity that caused the crash.

## Evidence record

```text
phase: instance target scene resource installation
status: implemented

request:
  wt_kind: 18
  wt_subtype: 6/7 resource chunk flow
  key_fields: requested SCE resource name

response:
  wt_kind: 18
  wt_subtype: 7
  blobs: scene resource bytes selected from active database overlay

ida_evidence:
  binary: 江湖OL.CBE
  function: parse_actor_motion_descriptor at 0x0100D6E2
  failure_branch: scene_node_find_or_create returns 0; 0x0100DA4E stores via
    the null-derived 0x13b address

runtime_evidence:
  trace_lines: mock_update_chunk for 测试地图.sce; mock_npc_instance_enter
  client_effect: wrong shared SCE reaches the parser before the node-table crash

negative_evidence:
  missing_or_bad_field: not a WT field omission; publication byte ownership is
    not scoped to CBE_MYSQL_DATABASE
  observed_failure: two databases with different drafts mutate one SCE path
```

## Validation

- `make -j2`: passed.
- `database-resource-overlay-regression`: passed; two databases downloaded
  different bytes for the same SCE key and an overlay miss used the immutable
  base.
- `scene-battle-monster-field18-regression`: passed, including the zero-word
  entity terminator boundary.
- `actor-scene-node-reserve-regression`: passed.
- `git diff --check`: passed.

The current shared `web/fs/JHOnlineData/测试地图.sce` predates the overlay
boundary and remains legacy data. It is no longer accepted as proof of a
deployment. The intended database must explicitly redeploy the scene with the
new server build before the instance spawn target becomes ready.

## 2026-08-22 update: asset-name table lifetime crash

### New symptom

After the battle/background Actor array issue was crossed, entering the
instance reached scene completion and then crashed at a different ROM site:

```text
screen_mgr remove ... new_top=0105a814 dp=01056198
address unavailable:0 type:0 size:19 value:4
r1=0 lr=01000627 pc=0100D2AA
```

`JianghuOL.CBE:0x0100D2AA` is the final load in `FindOrAddAssetName`:

```text
LDR R0, [R6,#0x14] ; count
LDR R1, [R6,#0x20] ; R9+0x5AE4 asset-name pointer table
LSLS R0, R0, #2
LDR R0, [R1,R0]    ; crashes because R1 == 0
```

IDA establishes the ownership contract but does not yet establish which side
first violated it:

- `SetupSceneVTable(0x0100DEB4)` allocates the table through `MemVTable8`.
- `FreeBattleTeamData(0x0100D1D0)` frees the table and clears related scene
  state.
- `ClearAssetNameList(0x0100D22C)` frees entries and resets count, but retains
  the table allocation.
- `FindOrAddAssetName(0x0100D262)` assumes the table is valid after its grow
  helper returns and does not accept a null table.

The observed order is WT event-7 callback, remote scene completion, active
scene `RemoveScreen`, immediate host promotion of the previous screen/data
package, followed by continued guest callback work and the null-table load.
The first divergence remains `unresolved`: either guest cleanup freed the
table before stale callback work, or the host switched the active data-package
context inside the manager stub while that callback was still executing.

### Read-only evidence probe

`CBE_TRACE_SCENE_ASSET_LIFECYCLE=1` records the following ROM boundaries to
`logs/scene-asset-lifecycle.log`:

```text
0x0100D1D0 FreeBattleTeamData entry
0x0100D22C ClearAssetNameList entry
0x0100D262 FindOrAddAssetName entry
0x0100D2A4 FindOrAddAssetName after table grow
0x0100DEB4 SetupSceneVTable entry
0x0100DEF8 SetupSceneVTable after table allocation
```

Each bounded record includes the original caller LR (including the saved LR at
`SP+20` after `FindOrAddAssetName` pushes registers), the table count/capacity/
pointer/next slot, asset name, network callback depth and slot, active and
removed screen identities, host and guest data packages, and screen stack
depth. The default limit is 256 records; a null table at a find boundary is
always retained even after the ordinary budget is exhausted. The ScreenManager
remove line now records the same callback and data-package context.

This probe only reads guest registers and memory. It does not change CBE/CBM
instructions, guest memory, registers, PC/LR, return values, network bytes, or
screen/callback ordering. No behavioral fix is admitted until the next runtime
trace identifies the earliest violating caller.

### Probe-build validation

- `make -j2`: passed; rebuilt `bin/main.exe` with the read-only probe.
- `screen-active-reentry-lifecycle-regression`: passed.
- `scene-transition-entry-contract-regression`: passed.
- `instance-guide-direct-entry-regression`: passed.
- `git diff --check`: passed.
- An isolated full client reproduction was not run because automation database
  credentials are unavailable. The user's database was not used as a test
  target. The next manual reproduction must preserve
  `multiplayer-data/<player>/logs/scene-asset-lifecycle.log`.

### First manual probe result

The first probe run did not execute the original Lin'an-to-instance transition.
The server restored the role directly into `测试地图.sce` during startup and
recorded only scene target serial 1; there was no `mock_npc_instance_enter` and
no serial-2/serial-3 remote target. The client completed `scene_ready`, consumed
the startup follow-up, and uploaded five movement packets. During that path the
old scene table was freed only at network depth 0, then the new scene allocated
a valid 100-slot table before its first asset lookup. No null-table find record
was emitted.

Because startup asset loading consumed the original 256-record budget before a
manual transition could occur, the multiplayer launcher now defaults the
lifecycle budget to 2048. Memory faults are also appended to
`logs/guest-memory-fault.log` with PC/LR, R0-R9, SP/CPSR and bounded stack words.
Fault capture runs only after Unicorn reports an invalid guest access and does
not alter guest memory, registers, code, return values, or event ordering.

## 2026-08-22 update: unanchored historical combat-spawn suffix

### Reproduction boundary

The next manual Lin'an-to-`测试地图.sce` reproduction did not reproduce either
previous memory fault:

- `actor-scene-node-capacity.log` reached the target scene with free scene
  slots; the battle/background table was allocated for its authored two-row
  descriptor.
- `scene-asset-lifecycle.log` kept a valid 100-entry asset-name table.
- no `guest-memory-fault.log` was created.

The first observable stall was instead server-side:

```text
mock_scene_enter_defer phase=scene-task-subset-followup
  scene=测试地图.sce missing=e_ghostfireR.actor keep_pending=1
mock_scene_task_subset_deferred
  scene=测试地图.sce objects=5 resp=310 completion=none
```

The client had therefore crossed the scene response parser without the old
`0x13b` or null-table crash. Scene completion remained pending on the service's
interpretation of the published SCE dependencies.

### First data divergence

Read-only inspection of the active `jh_online` publication found:

```text
immutable base: web/fs/JHOnlineData/测试地图.sce
  raw=518, SCE2 payload=501
active overlay: web/fs/JHOnlineData/.cbe-overlays/jh_online/测试地图.sce
  SCE2 payload=594
```

The immutable base already ended with one complete historical kind-3 combat
record, but had no native combat-spawn marker anchoring that record. The active
overlay contained both that unanchored record before the marker and the current
configured record after the marker. The database source row also stored the
same 518-byte base, so every later deployment rebuilt from the polluted bytes.

The client contract is narrower than the old service scanner: combat spawns
begin only after the native `kind-8 marker -> 00 00 -> kind-3` sequence and are
then parsed as consecutive kind-3 records. The old service scanned every byte
after the prop section for any syntactically valid kind-3 record. It therefore:

1. counted a client-invisible historical row toward scene capacity;
2. accepted that row when checking whether the configured deployment existed;
3. included its Actor dependencies in scene-entry readiness;
4. could not remove it when the row was disabled or redeployed.

This is the earliest violated contract in this reproduction. The later
resource wait was a consequence of the service and client using different
visibility boundaries for the same SCE bytes.

### Fix

`mock_server_scene_task.c` now locates the exact native marker and uses only the
consecutive marker-anchored region for node counting, configured-row matching,
and Actor dependency readiness.

During an explicit deployment, a base with no marker is normalized only when
its entire trailing suffix can be parsed as one or more complete kind-3 combat
records. That proven historical suffix is removed before the current rows are
rebuilt behind a native marker. A non-combat terminal is retained. Ambiguous
bytes are not deleted and still cause deployment validation to fail. Deployment
logs expose the migration as:

```text
stripped_legacy_spawns=<count>
```

No client state, response byte, register, CBE code, or parser branch is forced.
The fix makes publication, deployment verification, and dependency scheduling
follow the same SCE structure the client consumes.

### Regression validation

- `make -j2`: passed.
- `scene-battle-monster-field18-regression`: passed; the added polluted-base
  fixture is invisible before normalization, removes exactly one complete
  unanchored row, and exposes exactly one configured row after rebuild.
- `scene-transition-entry-contract-regression`: rebuilt from current source and
  passed, including deferred SCE/Actor downloads followed by one
  `30/2-no-posinfo` completion.
- `instance-guide-direct-entry-regression`: rebuilt from current source and
  passed; NPC instance entry remains a single normal `WT 1/30/1` response.

The active overlay has not been mutated by the investigation. The server must
be restarted and `测试地图.sce` explicitly redeployed before the corrected
normalization can replace the existing 594-byte publication. The expected
deployment evidence is `stripped_legacy_spawns=1` and `nodes=0->1`; the next
manual entry must then prove the current Actor dependency is requested and the
client reaches the normal scene completion and visible monster state.

## 2026-08-22 update: normalized publication installed, Actor not requested

The next reproduction proved that the normalization and publication steps did
run:

```text
scene_battle_monster_deploy ... nodes=0->1 raw=539 payload=521
  spawn_prefix=same-map-native stripped_legacy_spawns=1 manifest_files=2
mock_content_client_state ... release=15/1783 pending=2 source=WT18/9
mock_update_chunk ... file=测试地图.sce chunk=539 total=539 crc=7146
mock_content_client_resource_ready ... release=15 file_index=0
```

After returning to Lin'an and entering the instance again, the server reached:

```text
mock_npc_instance_enter ... scene=测试地图.sce spawn_enemy=1001 response=30/1
mock_scene_enter_defer ... missing=e_ghostfireR.actor keep_pending=1
mock_scene_task_subset_deferred ... resp=310 completion=none
```

There is no intervening client `WT 18/7` request for
`e_ghostfireR.actor`. This disproves the earlier assumption that redeploying
the normalized SCE alone would necessarily drive the dependency download.

The server-side `pending=2` line is not enough to identify the root cause. The
WT 18/9 tracker marks every manifest name pending when a connection reports an
older release, while the client owns the actual delete/load/request sequence.
The absent Actor request can therefore mean either:

1. the installed SCE never reaches the client combat-entity callback or does
   not create actor ID 1001; or
2. the node is created, but the content-update/cache lifecycle legitimately
   skips or loses the Actor request while the connection-scoped server tracker
   continues to call it pending.

Changing scene completion or clearing the pending bit before distinguishing
these cases would hide the first incorrect state. No such behavioral fallback
has been added.

### Read-only discriminator

The multiplayer launcher now enables the existing
`CBE_TRACE_SCE_ENTITY_CALLBACK` probe with
`CBE_TRACE_SCE_NODE_ACTOR_ID=1001`. It records:

- `LoadSceneDataFromStream` at `JianghuOL.CBE:0x010064B2`, including callback,
  stream offset, map name and the next eight bytes;
- `scene_node_find_or_create` at `JianghuOL.CBE:0x0100EFC4`, only when the
  requested actor ID is 1001.

High-volume layer/loader entry logging is now separately gated by
`CBE_TRACE_SCE_ENTITY_LOADER` and remains off by default. Existing probe format
strings that emitted literal `\n` text were corrected to real line endings.
The probe reads registers, stack arguments and guest memory only; it does not
change guest memory, registers, PC/LR, network bytes, callbacks or screen state.

## 2026-08-22 update: same-screen destroy selection did not free the Actor array

The next manual reproduction used the `23:07:26` client build and still faulted
at `JianghuOL.CBE:0x0100DA4E` with `lr=0x01017801`.  The final capacity records
show a two-row table allocated at `0x05059DA0`; four later scene init passes
retained that pointer, and the target `b_01桃花岛.sce` allocation found both rows
occupied.  There is no intervening `FreeBattleActorArray` entry.

This disproves the earlier completion claim for the same-screen exit-mode
change.  Selecting host `VM_SCREEN_EXIT_DESTROY` is not evidence that teardown
ran before the re-entrant parser.  The current runtime screen callback tuple
normalizes with code base `0x05016BD0` to these
`mmGameMstarWqvga.cbm` functions:

```text
init     0x05018015 -> sub_1444
destroy  0x050171D5 -> sub_604
logic    0x05017137 -> sub_566
render   0x050170CF -> sub_4FE
pause    0x05017091 -> sub_4C0
resume   0x05017025 -> sub_454
```

IDA shows `sub_604` performing several indirect manager/object cleanups, but it
does not directly call the main-CBE `FreeBattleActorArray`.  Runtime absence of
the existing `0x01017E3C` probe therefore leaves two distinguishable cases:

1. the host has not called `sub_604` before the next `sub_1444`; or
2. `sub_604` ran, but its current object/manager path did not invoke the scene
   vtable slot `+0x3C` registered at `scene_object_vtable_init:0x01019630`.

No business fallback has been added.  `CBE_TRACE_SCREEN_LIFECYCLE_ORDER=1`
now writes at most 256 read-only records to
`logs/screen-lifecycle-order.log`.  It records manager AddScreen selection,
actual before/after init, actual before pause/destroy, callback addresses, stack
depth, network callback context and the live `R9+0x5CB4` pointer.  The multiplayer
launcher enables it by default.  The next original Lin'an-to-instance run must
use this build; the first `before-init` that retains the old pointer can then be
compared directly with the preceding `before-destroy`/`after-destroy` pair.

Probe-build validation:

- `make -j2`: passed and rebuilt `bin/main.exe`.
- `screen-active-reentry-lifecycle-regression`: rebuilt and passed.
- No isolated end-to-end client run was started because the automation database
  credentials remain unavailable; the user database was not used as a test
  target.

`make -j2` rebuilt `bin/main.exe` successfully. The next reproduction must use
that binary and preserve the newly appended lines in
`multiplayer-data/<player>/logs/sce-entity-callback.log`. A callback record with
no subsequent `scene_node_create actor=1001` places the first divergence in the
SCE combat-record grammar/callback. A matching node-create record moves the
investigation to the WT18/9 cache and Actor load/request lifecycle.

## 2026-08-22 update: screen-manager decision probe

The latest reproduction still shows the fixed-capacity battle/background Actor
array retained across repeated `mmGame` destroy/init cycles:

```text
before-destroy actor_array=05059da0 actor_count=2
after-destroy  actor_array=05059da0 actor_count=2
before-init    actor_array=05059da0 actor_count=2
after-init     actor_array=05059da0 actor_count=0
```

IDA corrects the earlier lifecycle claim. `FreeBattleActorArray(0x01017E3C)`
is installed on the scene-system interface at scene object `+0x380+0x3C` and
is called by `scene_system_shutdown(0x01003C06)` or the guarded battle-exit
path. It is not an `mmGame` screen-destroy callback. `mmGame:sub_604` performs
module-local teardown but does not own this array, so selecting host
`VM_SCREEN_EXIT_DESTROY` cannot by itself satisfy the scene-array lifetime
contract.

The remaining narrow hypothesis is the obsolete update-completion same-screen
authorization. IDA shows that the final `WT18/7` installer does not call
`EnterSceneByMapName(0x0101809C)`; the server-side dependency completion order
already owns the resource-ready contract. The host still permits one normally
suppressed same-active-screen call from `0x01018150` when
`vm_net_mock_consume_update_completed_scene_reenter()` succeeds.

`CBE_TRACE_SCREEN_LIFECYCLE_ORDER=1` now appends bounded
`screen_manager_decision` records for manager indices 2/3. Each record includes
the guest caller/LR, requested and active screen, same-active classification,
whether the update authorization was consumed, whether the duplicate guard
matched, the final accept decision, scene target serial/name and network
callback context. This probe reads existing host state and guest LR only. It
does not change screen decisions, guest memory/registers, packet bytes, event
ordering or resource state.

No business fix is admitted until a reproduction proves whether the repeated
init cycle immediately follows a decision with
`same_active=1 update_reenter_consumed=1 accept=1`.

Probe-build validation:

- `make -j2`: passed and rebuilt `bin/main.exe`.
- `screen-active-reentry-lifecycle-regression`: rebuilt and passed.
- `scene-transition-entry-contract-regression`: rebuilt from current source
  and passed, including deferred SCE/Actor dependencies followed by one
  `30/2-no-posinfo` completion.
- `instance-guide-direct-entry-regression`: rebuilt from current source and
  passed.
- No end-to-end automation run was started because an isolated database target
  was not available; the user's `jh_online` database was not used.
