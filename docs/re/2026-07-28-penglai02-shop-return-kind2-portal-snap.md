# 蓬莱2 出商城后延迟再加载并甩到出口

Date: 2026-07-28

Status: implemented (server)

```text
trigger: portal → 00蓬莱仙岛_02 → shop exit (NPC/catalog OK) → wait
symptom: delayed scene reload; player snapped to exit portal ~(389,473)
root: post-catalog kind-2 30/1 → client 2/3 exit=0 → full-bootstrap
      resolved SCE entry-0 spawn instead of live grid
fix: recent current-scene-reload → inherit live pos; skip full-bootstrap
```

## 证据（用户日志）

1. `shop_return_npc_catalog_deliver` + post-catalog clear → NPC 正常、可走 `(172,85)`。
2. `shop_return_kind2_reenter_arm via=post-catalog-clear-done` → `shop_return_scene_enter ... pos=(172,85)`。
3. 紧接着 `mock_scene_npc_rearm ... scene-change-full-bootstrap` + `scene_ready ... pos=(389,473)` + `builtin-scene-change`。
4. `(389,473)` 是 `00蓬莱仙岛_02` SCE `exit=0` 入口/出口落点（见历史 transition baseline），不是当时走路坐标。

## 根因

- NPC 出店仍需要 kind-2 `30/1` 重建门节点（见 portal-kind2）。
- kind-2 已 `mark_current_scene_reload` 并保存 `(172,85)`。
- 客户端 follow-up `WT2/3 exit=0` 未命中 penglai02-repeat / current-scene-ack（短包、且 `_02` 不在 current-completion 名单）。
- `should_use_full_scene_bootstrap` 对蓬莱_02 恒 true → 用 SCE entry-0 坐标做 position-bearing `30/2` → 二次进图并位移。

## 修改

1. `get_scene_change_target`：`exit=0` 且近期 `current_scene_reload` → 继承 role/live 坐标（`inherit_reload`）。
2. `should_use_full_scene_bootstrap`：同场景近期 reload → **false**（走无 posinfo 的 ack 路径）。
3. kind-2 投递时 `mark_completed_scene_change_target` 写入 live 坐标，避免复用过期门落点。

## 验证

```text
# 蓬莱1 踩门进蓬莱2 → 进出商城
shop_return_npc_catalog_deliver ...
shop_return_kind2_reenter_arm / shop_return_scene_enter ... pos=<walk pos>
# 若有 follow-up 2/3：
mock_scene_target_inherit_reload ... 或 inherit_completed 为同一 walk pos
# 无 scene-change-full-bootstrap 甩到 (389,473)
# 可走；可再踩门
```

## 边界

- 不取消 NPC 图 post-catalog 后的 kind-2（门节点仍依赖）。
- 空图 kind-2、地图石进图、真实跨图 portal full-bootstrap 不变。
