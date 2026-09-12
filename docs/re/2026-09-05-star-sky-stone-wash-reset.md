# 星空石：重置装备洗练档位次数（2026-09-05）

## 范围

PixiJS 自定义商城道具 **849 星空石**：对指定背包装备的已解锁洗练档（+4/+8/+12/+16）使用后，将该档 `wash_count` 置 0。不改洗练属性本身。CBE 不接此 UI。

## 道具

| 字段 | 值 |
| --- | --- |
| itemId | 849 |
| 名称 | 星空石 |
| 说明 | 来自宇宙星空的神石,自带时空回溯的逆天效果! |
| 类别 | 14（秘宝） |
| 酷宝价 | 100（可后台改价/下架） |
| 耐久 | 1；背包 `count` 为剩余次数（可叠加） |

禁止通用 `7/1` 使用（与 847/848 同）：必须走 29/13。

## 协议（PixiJS，family 29）

| WT | 作用 |
| --- | --- |
| 29/13 | `{ stoneseq, equipseq, unlock }` → 扣 1 星空石，对应档 `washCount=0` |

### result

| 值 | 含义 |
| --- | --- |
| 1 | 成功 |
| 2 | 星空石不足 / 不是星空石 |
| 3 | 装备不存在 |
| 4 | 档位未解锁（enhance &lt; unlock） |
| 5 | unlock 非法 |
| 6 | 扣除失败 |
| 7 | 该档次数已为 0（不消耗） |

成功响应可带 `tiers`（与 29/7 同 12 字节/档快照）与 `washcount=0`。

## 客户端流程

背包「使用」星空石 → 对话框选强化≥+4 的背包装备 → 选已解锁档（显示当前次数）→ `29/13`。

## 代码落点

- `mock_server_core.c`：`STAR_SKY_STONE_*`
- `mock_server_catalog.c`：商城注入 + special-use 门闩
- `mock_server_role.c`：购买/发放耐久叠堆
- `mock_server_equipment_npc.c` / `dispatch.c`：29/13
- `PixiJS_web`：`itemCatalog` / `protocolExtras` / `GameSession.resetEnhanceWashCount` / `main` 选装备·档位

## 验证

- [ ] 商城秘宝可见「星空石」，购买后背包数量 +1
- [ ] 使用 → 选装 → 选档 → 次数归 0，金币阶梯回到 5 金起；属性不变
- [ ] 次数已为 0 → result=7，不扣石
- [ ] 未解锁档 / 非装备 → 对应失败 tip
- [ ] `make -j2` 通过
