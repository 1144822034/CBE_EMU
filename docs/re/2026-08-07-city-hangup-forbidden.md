# 2026-08-07：城市（临安-庆阳宫等）不允许挂机

phase: city-hangup-forbid
status: fixed

## 症状

在临安府（如庆阳宫 `c04临安府_06`）等城镇点击挂机，会开战或进入挂机环；产品预期城市不能挂机。

## 契约

- 挂机选怪权威：`automonster.dsh`（仅野外 `01*`…`28*`，**无** `c04临安府_*` 等城市场景）。
- 副本内挂机：仅当当前场景等于向导的 `instanceScene`（`29*`）时，可用 `challenge_enemy_id`。
- 城市拒绝：提示「当前场景不能挂机。」（`25/12` + `1/3/3`，勿用无请求的 `25/11` 以免粘住斗图标）。

## 第一次偏离

`vm_net_mock_select_instance_challenge_enemy_for_scene` 第三段把「当前场景 = 向导站立户外 `row->scene`」当成可挂机，户外梦境使者等带 `instanceScene` 的向导因此在临安命中梦境 boss，挂机开战。

（第三段已改为要求 `instanceScene` 为空，仅原地挑战向导。）

## 根因

挂机回退把**城市里的副本传送向导**当成了挂机怪来源，违反「城市无 automonster → 不可挂机」契约。

## 修改

| 层 | 改动 |
|----|------|
| `mock_server_scene_task.c` | 本地 challenge 向导须 `instanceScene[0]==0`（户外传送向导不匹配） |
| `mock_server_role.c` | `scene_is_city_hangup_forbidden`：`c`+数字 城镇键，以及去掉 `c` 的单数字+GBK 别名 |
| `mock_server_battle.c` | 武装挂机环前拒绝城市/无怪；统一 `25/12+1/3/3` 中文 tip |
| Pixi | `不能挂机` 清 `hangupActive`；聊天栏显示拒绝提示 |

`CBE_HANGUP_BATTLE_ENEMY_ID` 调试覆盖仍可绕过城市门闩。

## 验证

1. 重启 `jh-online-server`。
2. 临安任意城区（含庆阳宫）点挂机 → tip「当前场景不能挂机」，**不**进战、不武装挂机环。
3. 野外（如桃花岛）挂机仍正常。
4. 梦境 `29*` 内挂机（若已绑 challenge）仍可用；户外使者旁不可挂机。

## Negative

- 不要用英文 `25/11 No hangup monster`（粘斗、客户端难读）。
- 不要为城市硬编码假 automonster 行来「能挂」。
- 不要在户外按 `row->scene` 匹配带 `instanceScene` 的向导。
