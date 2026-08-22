# 副本进入后 ActorSceneNode 空指针崩溃

## 现象

进入 `测试地图.sce` 后，客户端在收到副本进入响应后崩溃：

```text
地址无法访问:13b type:0 size:20 value:300000001
lr=0x01017801 pc=0x0100DA4E
```

服务端日志显示 `30/1` 已发送，但后续没有正常的场景完成包。

## 根因

IDA 证据表明 `0x0100DA4E` 位于 Actor motion descriptor 解析函数。客户端先调用
`scene_node_find_or_create(0x0100EFC4)`，该函数只遍历 25 个
`ActorSceneNode` 槽位；返回空指针后仍无条件写入 `node + 0x13B`，于是访问地址为
`0x13B`。这是已经确认的首个错误状态：节点分配返回空指针，而客户端 parser 不检查
返回值。

此前仅统计 SCE 静态节点、27/11 NPC 和顶层战斗怪。运行时 motion descriptor 是否还会
创建额外隐藏节点、各 `.actor` 对应多少节点，目前没有可靠的分配跟踪证据，不能从磁盘
manifest 字节布局臆测。

## 修复

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
