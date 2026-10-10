# CBE/Pixi：仓库/出售凭证耐久走 7/11（可见堆叠 1）

Date: 2026-10-10

Status: implemented

## 契约

| 道具 | 持久化 `count` | 30/21 wire | 7/11 |
|------|----------------|------------|------|
| 802/803 壶 | HP/MP 储量 | 可见堆叠 **1** | 储量权威 |
| **834/839 凭证** | **剩余打开次数（耐久）** | 可见堆叠 **1** | **耐久权威** |

满耐久常量：`VM_NET_MOCK_WAREHOUSE_PASS_DURABILITY = 50`（839 同）。

此前 834 把耐久 50 直接放进 30/21 count：CBE `ProcessItem` 会按堆叠合并/拆分；抬 stackMax 后更易把两张凭证并成一行。

## 修改

1. **服务端** `mock_server_catalog.c`：`uses_count711` = 壶 ∪ 834/839；`grid_wire_count` / common-extra stack / login·开包 `7/11` 一并列发。
2. **CBE 主机** `network-client.c`：834/839 wire count→1，compact stack hint→1；`main.c` 强制 `stackMax=1`（不抬到 255）。
3. **Pixi** `protocolExtras.ts`：`applyFlaskReservoirsToBag` / preserve / merge 对凭证与壶同路径（不按堆叠相加）。

## 验证

1. `make -j2`，重启服与 CBE/Pixi，重新选角。
2. 背包一张 834：单行；数量/详情为耐久 50（非 50 张）。
3. 再买一张：两行各耐久 50，不合并。
4. 使用一次：耐久 49；日志仍有 `mock_warehouse_pass_use` + `7/1+7/7+7/11`。
5. 登录包含 `7/11` 且含 834 的 seq。
