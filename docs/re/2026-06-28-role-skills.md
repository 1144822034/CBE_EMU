# 2026-06-28 Role Skills

## Data Source

Local skill table:

```text
bin/JHOnlineData/skill.dsh
```

Header values:

```text
columns=32
rows=74
```

Relevant columns:

```text
ID, 名称, 技能图片, 说明, 等级, 法术标示, 职业
```

For battle playback, `技能图片` is the visual effect sequence. It indexes
`eidolon.dsh` (`序列号 -> 精灵名字`) and is the value sent in subtype `4/6`
actioninfo for action type `1`. `法术标示` is not the battle actor/eidolon
index; using it can produce a valid but wrong visual.

The `职业` column is the real job discriminator:

```text
raw job 0 = 天机
raw job 1 = 幻剑
raw job 2 = 鬼道
```

The local mock role DB stores role jobs as `1..3` for title/role-state
convenience. Convert it to the raw client/DSH value before selecting skills:

```text
role job 1 -> raw job 0 -> 天机
role job 2 -> raw job 1 -> 幻剑
role job 3 -> raw job 2 -> 鬼道
```

The later columns named `天机 / 幻剑 / 鬼道` are coefficient columns in this
table. They are all `100` in the current file and must not be used as class
membership flags.

## Client Parser Evidence

`JianghuOL.CBE:net_handle_business_followup_events(0x01010594)` handles WT
object `1/12/1`:

1. Calls `LoadSkillDataSheet(0x0103550E)` with `actor+321`, the actorinfo job
   byte stored by `parse_actorinfo_response(0x0100FA88)`.
2. Reads `learnednum`.
3. Opens the `learnedskill` blob as a tagged stream.
4. Reads one skill ID per learned row.
5. Calls `MarkListItemActive()` for each ID.

`LoadSkillDataSheet()` filters `skill.dsh` by the same raw job value (`0..2`).
Therefore the server only sends learned skill IDs, but those IDs must come from
the same raw-job group that the actorinfo job byte selected. Names,
descriptions, effects, and icons are supplied by the local `skill.dsh`.

## Learned-Skill Rule

The mock no longer derives learned skills from role level. A role with no
persisted skill rows is initialized with exactly one level-1 skill for its
profession. Every later skill is learned explicitly from a skill trainer and
stored in `account_role_skills`; reaching level 5, 10, 15, and so on does not
add any skill automatically.

The one-time initialization trace is
`mock_role_skill_seed ... policy=starter-only`. Existing persisted skills are
left intact because the table does not record whether an older row came from a
trainer or from the removed level-derived migration.

Response source remains the existing skill-tail object family. `learnednum`
is read through object getter `+0x48`, so it must be encoded as tagged-u16.
`learnedskill` is passed directly to `stream_reader_init_from_blob()` and then
read through stream vtable `+0x20` (`stream_read_i32_be_tagged`), so the field
payload must be a raw tagged-u32 stream, without an extra blob length prefix.

```text
1/12/1 { learnednum:u16, learnedskill:raw tagged-u32 stream }
1/7/42 { booknum, booksinfo }
1/17/1 { iteminfo }
```

## 天机 Skills

| Order | DSH Level | ID | Name |
| --- | ---: | ---: | --- |
| 1 | 1 | 1 | 万剑诛仙1 |
| 2 | 3 | 11 | 灵岩护身1 |
| 3 | 5 | 21 | 雷震八方1 |
| 4 | 7 | 31 | 神臂担山1 |
| 5 | 9 | 41 | 破甲烈刃1 |
| 6 | 14 | 2 | 万剑诛仙2 |
| 7 | 15 | 12 | 灵岩护身2 |
| 8 | 20 | 51 | 神堂静默 |
| 9 | 24 | 32 | 神臂担山2 |
| 10 | 26 | 42 | 破甲烈刃2 |
| 11 | 27 | 3 | 万剑诛仙3 |
| 12 | 27 | 13 | 灵岩护身3 |
| 13 | 30 | 22 | 雷震八方2 |
| 14 | 39 | 14 | 灵岩护身4 |
| 15 | 40 | 4 | 万剑诛仙4 |
| 16 | 41 | 33 | 神臂担山3 |
| 17 | 43 | 43 | 破甲烈刃3 |
| 18 | 51 | 15 | 灵岩护身5 |
| 19 | 53 | 5 | 万剑诛仙5 |
| 20 | 55 | 23 | 雷震八方3 |
| 21 | 58 | 34 | 神臂担山4 |
| 22 | 60 | 44 | 破甲烈刃4 |

## 幻剑 Skills

| Order | DSH Level | ID | Name |
| --- | ---: | ---: | --- |
| 1 | 1 | 101 | 风舞刃行1 |
| 2 | 3 | 111 | 混沌鬼气1 |
| 3 | 5 | 121 | 荒魂劫火1 |
| 4 | 7 | 131 | 形神离散 |
| 5 | 9 | 141 | 毒入膏肓1 |
| 6 | 11 | 151 | 尸鬼召还 |
| 7 | 13 | 161 | 电闪神兵1 |
| 8 | 14 | 102 | 风舞刃行2 |
| 9 | 15 | 112 | 混沌鬼气2 |
| 10 | 26 | 142 | 毒入膏肓2 |
| 11 | 27 | 103 | 风舞刃行3 |
| 12 | 27 | 113 | 混沌鬼气3 |
| 13 | 30 | 122 | 荒魂劫火2 |
| 14 | 35 | 162 | 电闪神兵2 |
| 15 | 39 | 114 | 混沌鬼气4 |
| 16 | 40 | 104 | 风舞刃行4 |
| 17 | 43 | 143 | 毒入膏肓3 |
| 18 | 51 | 115 | 混沌鬼气5 |
| 19 | 53 | 105 | 风舞刃行5 |
| 20 | 55 | 123 | 荒魂劫火3 |
| 21 | 57 | 163 | 电闪神兵3 |
| 22 | 60 | 144 | 毒入膏肓4 |

## 鬼道 Skills

| Order | DSH Level | ID | Name |
| --- | ---: | ---: | --- |
| 1 | 1 | 201 | 绯炎幻法1 |
| 2 | 3 | 211 | 清风拂体1 |
| 3 | 5 | 221 | 道统天地1 |
| 4 | 7 | 231 | 天火熔身1 |
| 5 | 9 | 241 | 妙慧通灵1 |
| 6 | 11 | 251 | 五雷天殛1 |
| 7 | 13 | 261 | 三花聚顶1 |
| 8 | 15 | 202 | 绯炎幻法2 |
| 9 | 15 | 212 | 清风拂体2 |
| 10 | 16 | 222 | 道统天地2 |
| 11 | 23 | 232 | 天火熔身2 |
| 12 | 26 | 242 | 妙慧通灵2 |
| 13 | 27 | 213 | 清风拂体3 |
| 14 | 27 | 223 | 道统天地3 |
| 15 | 27 | 252 | 五雷天殛2 |
| 16 | 29 | 203 | 绯炎幻法3 |
| 17 | 33 | 262 | 三花聚顶2 |
| 18 | 38 | 224 | 道统天地4 |
| 19 | 39 | 214 | 清风拂体4 |
| 20 | 39 | 233 | 天火熔身3 |
| 21 | 43 | 204 | 绯炎幻法4 |
| 22 | 43 | 243 | 妙慧通灵3 |
| 23 | 43 | 253 | 五雷天殛3 |
| 24 | 49 | 225 | 道统天地5 |
| 25 | 51 | 215 | 清风拂体5 |
| 26 | 53 | 263 | 三花聚顶3 |
| 27 | 55 | 234 | 天火熔身4 |
| 28 | 57 | 205 | 绯炎幻法5 |
| 29 | 59 | 254 | 五雷天殛4 |
| 30 | 60 | 244 | 妙慧通灵4 |

## Runtime Trace

The skill-tail response logs:

```text
mock_role_skills role=... role_job=... raw_job=... job_name=... level=... learned=... ids=...
```

## 2026-07-01 Job Mapping Fix

The skill catalog now stores `skill.dsh` column `职业` as `rawJob` without adding
one. `vm_net_mock_build_role_learned_skill_blob()` converts persisted
`role->job` to raw job with:

```text
rawJob = roleJob - 1
```

This matches the actorinfo byte consumed by
`LoadSkillDataSheet(actor+321)`. It avoids the previous implicit `+1/-1`
alignment where role DB job values and client skill-sheet job values were easy
to mix up.

## 2026-07-19 Skill Trainer Conditions

The trainer evaluates each `skill.dsh` row against the active role:

1. raw profession must equal `role_job - 1`;
2. `等级` must be no greater than the current role level;
3. the skill ID must not already exist in `account_role_skills`;
4. the role must have at least the row's `价值` amount in copper.

The client loader at `0x0103550E` independently confirms that the sheet row
contains `职业`, `ID`, `名称`, `说明`, `等级`, `价值`, and the following usage
fields. No separate prerequisite-skill column is consumed by that loader, so no
unproven skill-tree prerequisite is imposed by the mock.

For the observed `guest00023/10027` role, `job=2`, `level=1`, and learned skill
`101` are all consistent. The next Huanjian row is skill `111`, level `3`, value
`160`, which explains why a strict level-1 list has zero selectable rows. The
trainer now reports that exact next condition instead of the ambiguous generic
empty-list message.

## 2026-06-28 Empty Spell List Follow-up

The spell panel is not expected to stay empty after entering the scene. Each
level-1 profession has a displayable skill row in `skill.dsh`:

```text
天机: ID 1   万剑诛仙1   技能图片 14 -> f_sword1.actor
幻剑: ID 101 风舞刃行1   技能图片 1  -> f_blood2.actor
鬼道: ID 201 绯炎幻法1   技能图片 7  -> f_flame1.actor
```

Runtime showed the first scene follow-up request as:

```text
wt=12/1 len=54 source=builtin-scene-task-subset-followup
```

That broader task-subset handler consumed the request before the standalone
`builtin-login-tail-skill` handler could run. It now checks whether the request
contains both `1/12/1` and `1/7/42`; when present, it appends the same
`learnednum + learnedskill` and books response used by the login-tail skill
path.

Verification after rebuild:

```text
mock_role_skills role=10001 role_job=3 raw_job=2 job_name=Guidao level=2 learned=1 ids=201
net_send connect=2 wt=12/1 len=54 source=builtin-scene-task-subset-followup resp=304
skill parser probe: learnednum=1, read_skill=201, MarkListItemActive(skill=201)
autotest_exit elapsed=40003 max_ms=40000
```

Negative runtime before the final wire correction:

```text
learnednum as u32 -> client getter +0x48 read 0, so no skill IDs were consumed.
learnedskill via blob helper -> client read 262144 because the extra length
prefix shifted the tagged-u32 stream by two bytes.
```

## 2026-06-29 Compact Scene Skill/Default Follow-up

New runtime after role-select map entry produced a shorter post-`ScreenInit`
request:

```text
unhandled wt=12/1 len=19 first=1/12/1:0,1/7/42:0,1/25/5:0
```

This is not the full first-scene resource/task family. Returning the older
`scene-resource-followup` response with a trailing `30/1` re-entered the scene
again (`screen_mgr same`) and sent the client into resource/download churn.

The mock now handles the exact three-empty-object signature as
`builtin-scene-compact-skill-default` and returns only:

```text
1/12/1 learned skills
1/7/42 empty books
1/17/1 backpack item refresh
1/25/5 { result = 4 }
```

It intentionally does not append `30/1` or `30/2`; the request arrives after the
scene screen has already initialized.
