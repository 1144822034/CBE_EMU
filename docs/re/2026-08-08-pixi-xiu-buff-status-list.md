# Pixi「修」图标：已用增益列表与剩余时长

日期：2026-08-08

## 需求

地图角色旁「修」状态图标点击后，显示当前已生效的增益物品及剩余时长。

## 契约

- 点击路径对齐 CBE `SendSceneAction31` → 请求 **WT `1/7/31`**。
- 响应 **`7/31 { expinfo }`**；无增益时附带 **`7/32 { expcard=0 }`**。
- `expinfo` 为 GBK 文案；状态查询下发**全部**激活时效增益，行间用 `\n` 分隔：
  - 经验卡（809/810/811/845）
  - 战斗心得（828）
  - 大力丸 / 神力丸（829/830）
- 登录 / 使用成功推送的 `7/31` 仍保持单槽 tip（经验卡优先于心得），不因本需求改成多行列表。

## 实现

| 层 | 改动 |
| --- | --- |
| mock `catalog` | `vm_net_mock_active_buff_status_list_info`；`build_exp_card_status_response` 使用列表 |
| mock `role` | `combat_pill_remaining_seconds` / `combat_pill_item_id` |
| Pixi `SceneView` | `xiuBadge` 可点 → `onXiuTap` |
| Pixi `GameSession` | `requestBuffStatus()` → `1/7/31` |
| Pixi `WorldHud` | `showTip` 居中绿字 toast 展示多行 |

## 验证

1. 使用经验卡后点「修」：见「N倍经验卡生效中(剩余…)」。
2. 同时有心得 / 战力丹：列表多行均含剩余时长。
3. 无增益：提示「当前没有生效的增益物品」。
4. `make -j2` 通过。
