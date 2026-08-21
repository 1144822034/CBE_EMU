# Role Designation Page

## Runtime Trigger

Clicking the in-scene player-info `称号` entry sent an unhandled request:

```text
WT kind=23 subtype=1 len=20
object: 1/23/1 payload_len=11
```

The mock now handles this as `builtin-role-designation23`.

## Client Evidence

There are two different kind `23` consumers:

- `HandleDesignationInfoResponse(0x0102A93E)` is the title page list/select
  parser. It accepts subtype `23/1` for list data and subtype `23/3` for
  selection confirmation.
- `net_handle_designationinfo_update(0x01010DB6)` is an actor metadata update
  path reached from the main business dispatcher for subtype `23/2`.

The title page list parser reads subtype `23/1`:

```text
result
equiptype
count
designationinfo
```

`designationinfo` is a raw stream blob. For each row the parser reads:

```text
tagged i8 actorTitleFieldA  ; 00 01 vv
tagged i8 actorTitleFieldB  ; 00 01 vv
len16 displayName
len16 descriptionText
len16 overheadResource
```

The parser copies each row into a 134-byte list cache:

```text
+0   i16 actorTitleFieldA
+2   i16 actorTitleFieldB
+4   displayName[10]
+14  descriptionText[100]
+114 overheadResource[20]
```

Runtime evidence: sending `title.gif` in the first string slot rendered the
literal text `title.gif` in the page, so this slot is the short list display
name. The mock sends deterministic local designation rows with a short
`displayName`, a separate lower-panel `descriptionText`, and a real local
`overheadResource`.

Runtime negative: the first two fields are not tagged i16. Sending them with
`00 02 00 00` makes the parser's tagged-i8 reader advance only three bytes per
field, leaving the stream cursor two bytes behind. The next len16 string read
then sees length zero for `displayName`, while `descriptionText` receives the
title text. The visible symptom is an empty highlighted list row while the
title appears in the lower description/control area. Sending string lengths as
one byte is also wrong: the reader is `stream_read_cstr_len16`, and the malformed
length later crashed in `MemCopyAligned(0x0104D43C)`.

When subtype `23/3` selection succeeds, the parser copies row `+4` to actor
`+256` and row `+114` to actor `+266`. Runtime evidence showed that putting
human-readable Chinese into row `+114` made the client request that text as a
resource filename. IDA evidence from `scene_draw_node_overhead_overlay`
(`0x01045578`) also treats actor `+266` as an optional named overhead
badge/resource. The mock therefore puts only real local resource names in row
`+114` (`riches_name0.gif` through `riches_name9.gif`), never human-readable
title text.

The first row field is the stable server-side designation id. The client sends
it back as the `type` field in subtype `23/3`, and the mock stores it on the
active role. The second row field is still held at zero; previous experiments
with nonzero values crashed the scene render path after actor metadata update.

Scene-side update uses subtype `23/2`, handled by
`net_handle_designationinfo_update(0x01010DB6)`. Its `designationinfo` row is:

```text
tagged u32 actorId
tagged i8 actorTitleFieldA
tagged i8 actorTitleFieldB
len16 shortTitle
len16 overheadResource
```

The handler updates matching current/nearby actor nodes at `+286/+288`,
`+256`, and `+266`. The scene-enter `actorinfo` path uses the same selected
title resource in the actor `+266` slot so the title badge survives scene
reload.

## Response Contract

The response is:

```text
WT object 1/23/1
  result = 1
  equiptype = active designation id
  count = unlocked designation row count
  designationinfo = raw stream title rows
```

The mock keeps a server-side wealth-title catalog backed by recovered
`riches_name*.gif` resources, plus a level-title catalog backed by
`level_name*.gif`. These are not granted as fixed defaults: `23/1`
only includes entries whose money or level condition is satisfied by the
active role:

```text
0  一贫如洗    riches_name0.gif  0 金       (minMoney=0)
1  衣食无忧    riches_name1.gif  1 金       (minMoney=1,000)
2  生财有道    riches_name2.gif  10 金      (minMoney=10,000)
3  理财有方    riches_name3.gif  100 金     (minMoney=100,000)
4  财运亨通    riches_name4.gif  1,000 金   (minMoney=1,000,000)
5  腰缠万贯    riches_name5.gif  5,000 金   (minMoney=5,000,000)
6  家财万贯    riches_name6.gif  10,000 金  (minMoney=10,000,000)
7  富商巨贾    riches_name7.gif  30,000 金  (minMoney=30,000,000)
8  富甲一方    riches_name8.gif  60,000 金  (minMoney=60,000,000)
9  富可敌国    riches_name9.gif  100,000 金 (minMoney=100,000,000)
```

2026-08-21 起，金钱称号按当前持有金额判断，换算为 `1 金 = 1,000`
持久 `money` 单位。以上是当前服务端配置；称号资源、稳定 ID、`23/1` 列表与
`23/3` 选择回包格式均未改变。

Level titles:

```text
16  不堪一击  level_name0.gif   minLevel=1
17  初学乍练  level_name1.gif   minLevel=5
18  小试牛刀  level_name2.gif   minLevel=10
19  初露锋芒  level_name3.gif   minLevel=15
20  出人头地  level_name4.gif   minLevel=20
21  名震江湖  level_name5.gif   minLevel=25
22  江湖豪杰  level_name6.gif   minLevel=30
23  了然于胸  level_name7.gif   minLevel=35
24  炉火纯青  level_name8.gif   minLevel=40
25  江湖侠隐  level_name9.gif   minLevel=45
26  登峰造极  level_name10.gif  minLevel=50
27  超越极限  level_name11.gif  minLevel=55
28  开山鼻祖  level_name12.gif  minLevel=60
```

The resource strip level_name.gif and its thirteen individual badge files
establish the exact display-name/resource mapping.  初来乍到 is a task name
in the recovered task data; the level-title badge at this progression point is
初学乍练 (level_name1.gif).

If the active stored designation is no longer unlocked, the page-open path
falls back to the highest currently unlocked title before emitting `equiptype`.

Selecting a row sends subtype `23/3`. The page parser accepts subtype `23/3`
as a confirmation packet:

```text
WT object 1/23/3
  result = 1
WT object 1/23/2
  count = 1
  designationinfo = actorId + selected designation row for scene actor node
```

The mock treats the request's `type` field as the selected designation id,
persists it on the active role, then returns success plus a scene-node update.
Because row `+114` is a real `riches_name*.gif` or `level_name*.gif` resource
name, this updates actor `+256` and the resource-bearing badge slot without
triggering a Chinese filename update request.
If the requested title is locked, the mock returns `23/3 result=0` and does not
emit the scene-node update.

The observed selection request payload is:

```text
04 74 79 70 65 00 03 00 01 00  ; type = 0 with the current WT numeric helper
```

The stream is intentionally written with `vm_net_mock_put_object_entry`, not
`vm_net_mock_put_object_blob`, because the parser passes the field value
directly to `stream_reader_init_from_blob`, the same convention used by
`actorinfo`.

## Negative Evidence

The existing `role_action23` handler is unrelated. It handles object `1/10/23`
from `SendRoleActionEvent(0x0103C830)` and responds as `10/23`; it must not
consume this `1/23/1` designation request.

Sending the human title into the actor resource-bearing slot is wrong:
after `23/3` succeeds, row `+114` is copied into actor `+266`. Runtime saw the
scene resource updater request the selected title as a filename when this slot
was populated incorrectly:

```text
mock_update_chunk_missing subtype=7 file=一贫如洗
```

Sending `title.gif` is also wrong for scene overhead display. Runtime screenshot
showed it renders the large login/logo calligraphy over the actor. The correct
wealth-title badge resources recovered from local assets are `riches_name*.gif`
(`riches_name0.gif` = `一贫如洗`, `riches_name1.gif` = `衣食无忧`, ...).

Returning subtype `23/2` for the page-open request is parser-safe but does not
populate the title page list; it only updates actor metadata. Setting
`actorTitleFieldB=1` in that metadata path caused a scene render crash
immediately after `23/2` parsing:

```text
mock_role_designation23 ... field_a=0 field_b=1 ...
pc=03416300 lr=01014597 evidence=scene_draw_actor_pass@0x01014594
```

So the two i16 fields must remain zero until the render-side semantics are
recovered. The handler logs the `23/1` request's candidate
`index/result/page/id` fields and the short payload hex so future cases can
distinguish page-open from selection or activation requests. Runtime page-open
payload observed:

```text
05 69 6E 64 65 78 00 03 00 01 00  ; index = 0
```
