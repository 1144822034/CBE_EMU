# 副本外仓库存入玄晶后卡住掉线（2026-08-05）

## 现象

户外使用 834 开仓库，存入二级玄晶 `902` 后界面卡住，约 40s 后
`session_reuse … end=idle-timeout` 掉线。

## 日志证据（`lxh001` / `a925d5e5`）

```text
mock_warehouse_deposit … item=902 … bag_rows=40
mock_npc_service action=warehouse-deposit … options=6 resp=277
mock_backpack_remove item=902 seq=148 morph=802 … flask-delete-via-morph
mock_backpack_list_resync … flask=0 equip=0 phase=1->2
  evidence=7/7-morph802-type2+7/11-then-26/0-after-warehouse-deposit
backpack_resync_busy_ack … reason=poll-phase2
session_reuse … hold_ms=41776 end=idle-timeout
```

服务端已走完 phase1 peel + phase2 `26/0`，但客户端之后无请求直至空闲超时。

## 根因

1. **触发**：仓库存入非壶、非装备堆叠物（玄晶 category=23，`902`）。
2. **违反契约**：存入 peel 把行 `7/7 type=2` **改写成 802 壶**再 `7/11=0`。
   玄晶权威扣减契约是同 `itemId` 的
   `7/7 type=2 remaining=0 + 7/11`（强化 / 任务提交已证实），不是 morph→802。
3. **首次错误状态**：客户端 item-manager 收到伪壶改写后，busy /
   `DF_DataPackage_DoLoading` 在后续 lone `26/0` 后仍无法正常回到可交互态；
   仓库对话框仍开着时表现为卡住，最终 `idle-timeout`。
4. **证据**：运行时 `morph=802` 日志；`item.dsh` / 商城类别 23=玄晶；
   `docs/re/2026-07-29-enhance-crystal-bag-count-stale.md` 同构 peel。

已排除：phase2 未投递（日志有 `busy_ack`）；梦境禁仓（本例为副本外）。

## 修改

`vm_net_mock_append_backpack_item_remove7_object`：

| 物品 | phase 1 |
|---|---|
| 802/803 | `7/11=0`（不变） |
| 装备 | `17/1+7/42`（不变） |
| 普通/玄晶等堆叠 | **`7/7 type=2` 同 id `remaining=0` + `7/11=0`**（不再 morph→802） |

phase 2：仍单独 `26/0`。

期望日志：

```text
mock_backpack_remove item=902 … remaining=0 … evidence=…-not-morph802
mock_backpack_list_resync … evidence=7/7-remaining0-type2+7/11-then-26/0-…
backpack_resync_busy_ack …
```

## 验证

1. 户外 834 → 存入一级/二级玄晶：进度条消失，对话框可继续选或关闭，不掉线。
2. 存入壶 / 装备路径无回归。
3. `make -j2`。
