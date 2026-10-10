# CBE 背包单行堆叠允许 >99（不改 item.dsh）

Date: 2026-10-10

Status: implemented

## 目标

与 Pixi 一致：背包可堆叠道具单行数量可超过 99，不因 `item.dsh` col10「堆叠数」=99 被 `ProcessItem` 拆成多格。

## 根因

- Pixi：`bagVisibleStackCount` / UI 直接显示服务端 `count`，不以 DSH 堆叠数为单行上限。
- CBE：`BuildTimerItemData`（`0x01033044`）把 DSH 堆叠数写入 `item+244`（stackMax）；`TimerControl_ProcessItem`（`0x01032EB8`）合并/插入时若 `count > stackMax` 则拆行递归。
- 主机 `30/21` 改写已允许 wire count 至 255（`vm_client_cbe_grid_wire_count`）；瓶颈在客户端 stackMax=99。

## 修改（主机 hook，不改 DSH / 不改 .cbe）

`src/main.c`（JNI 同步）：

| PC | 时机 | 动作 |
|----|------|------|
| `0x010330CC` | BuildTimerItemData 写完 stackMax 且 0→1 之后 | `r6=item`：若 stackMax∈(1,255) → 写 255 |
| `0x01032EB8` | ProcessItem 入口 | `r1=incoming`：同上 |
| `0x01032F18` | ProcessItem 合并候选 | `lr=existing`：同上 |

保留 stackMax ≤1（壶/配方等可见堆叠 1；808 的 0→1 仍由 ROM 处理且网格已过滤）。

## 验证

1. `make -j2`，重启 CBE，重新选角进图。
2. 角色持有某 stack=99 类道具且服务端 count>99（如 800/840）：背包应单行显示真实数量，不拆成多行 99+余数。
3. 802/803 仍可见堆叠 1；808 仍不进通用网格。
4. Autotest `backpack_full … stack_max=` 对可堆叠行应为 255。
