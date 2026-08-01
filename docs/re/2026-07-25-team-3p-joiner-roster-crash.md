# 三人组队开战第三人闪退

日期：2026-07-25

## 现象

三人同场景组队后遇怪开战，常有**最后入队的那一名**客户端直接闪退；另两人可进入战斗。

最终症状与场景开战空视觉一致：

```text
pc:1004ea8 lr:5032435
r4 object +0x0C 为空
```

## 根因

`HandleBattleStartMsg(0x66CC)` 对 subtype-5 每个 right-side id 走 `sub_66A4`，必须在观察者本地队伍表（`BattleScene_CreateCharList` 从主 CBE 导入）中命中。

入队契约此前为兼容二人队写成：

- 登录 `5/10`：本机一行
- 同意 `5/3`：仅追加**队长**一行（避免重复 self）
- 既有队员：`5/5` 增量新人

二人队：`[self] + [leader]` = 2，与 `battleinfo right_count=2` 一致。

三人队第三人同意时仍只下发队长一行 → 本地只有 `[self, leader]`，缺中间队员。开战包 `right_count=3` 含三人 wire id，`sub_66A4` 查不到中间 id → 空单位 → 首绘 `0x01004EA8` 闪退。既有两人已通过 `5/5` 收到第三人，roster 完整，故不崩。

首次偏离：第三人成功 `5/3` 的 `groupinfo` 行数不足（`num=1` 而非 `members-1`）。

## 修改

`vm_net_mock_append_team_joiner_leader_roster_object`：成功 `5/3` 追加队内**除 joiner 外**全部成员（队长仍为第一行，不写 self）。

- 二人加入：仍只 1 行（队长）
- 三人加入：2 行（队长 + 中间队员）

## 验证

1. 重启服务；三人重新组队（已残缺 roster 的客户端需重登或离队再建）。
2. 第三人同意日志：`mock_team_joiner_existing_roster ... rows=2 members=3`。
3. 三人开战：三人皆进战斗 UI，无 `0x01004EA8`。
4. 二人组队回归：同意后仍 `rows=1`，双方 UI 正常。
