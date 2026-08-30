# 2026-08-29 武器职业类别限制

## 契约

`equip.dsh` 武器类别与职业一一对应：

| 职业 | job | 武器类别 |
| --- | ---: | ---: |
| 天机 | 1 | 7 |
| 幻剑 | 2 | 8 |
| 鬼道 | 3 | 9 |

防具等非 7/8/9 类别不限制。

## 修改

- 服务端：`vm_net_mock_role_equip_backpack_item` / `vm_net_mock_role_swap_equipped_backpack_item` 在等级校验后拒绝 `wrong-job-weapon`。
- Pixi：替换列表过滤；背包/装备面板装备与替换前本地提示。

## 验证

1. 天机只能穿类别 7 武器；幻剑 8；鬼道 9。
2. 错职业武器：7/8 / 7/9 `result=0`，日志 `reason=wrong-job-weapon`。
3. 防具替换不受影响。
4. `make -j2`
