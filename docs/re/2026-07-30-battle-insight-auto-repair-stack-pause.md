# 战斗心得：自动修装备 / 可叠加 / 离线不计时

## 资源证据

`item.dsh` 828 说明：

> 使用后获得1小时战斗心得状态，挂机连续战斗场次升至200、自动修装备、背包不足自动出售、获得经验增加20%！

此前仅落地经验 +20%。本轮补「自动修装备」中的耐久侧，并按经验卡同类规则调整时长。

## 规则

| 项 | 行为 |
| --- | --- |
| 自动修装备 | 心得激活期间，战斗结束 `apply_battle_wear` **跳过扣耐久**（仍记本场 wear serial，防重入） |
| 叠加 | 已激活时可再使用 828：在剩余结束时刻上再加 60 分钟；倍率保持 20 |
| 离线 | 与经验卡相同：掉线写入 `paused_remaining_sec`、清 `expires_unix`；选角登录再恢复墙钟结束时刻 |
| 遇怪数量 | 心得激活时场景遇怪固定 3 只（见 `2026-07-31-battle-insight-fixed-enemy-count.md`） |
| 未改 | 挂机场次升至 200、背包满自动出售仍 unresolved |

## 修改点

- `vm_net_mock_role_service_apply_battle_wear`：激活心得则 skip
- `consume_backpack_item_with_timed_effect`：`BATTLE_INSIGHT` 可 stack
- `get_active_timed_item_effect`：心得暂停行合成 `expires=now+paused`
- 通用 `pausable_timed_effect_pause/resume`；logout / 选角同时处理经验卡与心得

## 验证

1. 使用心得后打怪，日志 `mock_equipment_durability_wear_skip ... reason=battle-insight-auto-repair`，耐久不变。
2. 心得生效中再使用一张：`mock_battle_insight_stacked`，时长约 +60 分钟。
3. 掉线若干分钟再上线：剩余接近掉线前；日志 `battle-insight_paused` / `battle-insight_resumed`。
4. 心得过期后恢复正常每场扣 1 耐久。
5. `make -j2 server`。
