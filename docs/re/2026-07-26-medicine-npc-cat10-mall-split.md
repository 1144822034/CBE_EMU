# 药品商人与商城密宝的类别 10 上下架分流（2026-07-26）

## 需求

药品商人 NPC 售卖的 `item.dsh` 类别 10：

- **只售卖已下架**（`server_shop_items.enabled=0`，无覆盖时默认上架故不在药商）的条目；
- **已上架**的类别 10 **追加**显示到商城「秘宝道具」（`1/14/5`）；
- 其他 NPC、其他道具/装备类型不变。

## 契约与首次偏离

| 通道 | 修复前 | 修复后 |
| --- | --- | --- |
| 药品商人 selector `0xfe` | `enabled && category==10` | `!enabled && category==10` |
| 商城 `14/5` 秘宝 | `enabled && category==14` | `enabled && (category==14 \|\| category==10)` |
| 装备 NPC / 其他商城页 | 不变 | 不变 |

`enabled` 仍是商城上下架开关：类别 10 上架 → 只出现在密宝（W 币购买路径）；下架 → 只出现在药品商人（铜币 NPC 购买路径）。二者互斥，避免同物双通道。

后台「商品管理 · 秘宝道具」分区同步把类别 10 算进秘宝，便于对药品做上架/下架与改价。

秘宝目录上限由 `8` 提到 `80`（与神兵单页上限同级），否则类别 14（约 10）+ 上架类别 10（最多约 30）无法完整「追加」；客户端仍按 `SHOP_PAGE_SIZE=10` 分页。

## 修改点

- `src/server/mock_server_scene_sync.c`：`vm_net_mock_npc_shop_item_matches_selector`
- `src/server/mock_server_catalog.c`：`vm_net_mock_shop_page_item_matches_subtype` case 5
- `src/server/mock_server_core.c`：`VM_NET_MOCK_SHOP_SECRET_MAX_ITEMS`
- `src/web_admin_server.c`：`vm_mock_admin_shop_is_secret_treasure`

## 验证

1. `make -j2` 通过；`bin/jh-online-server.exe` 已重启；`http://127.0.0.1:19091/healthz` 返回 200。
2. 请在游戏内复测：
   - 类别 10 后台下架 → 药品商人可见，商城秘宝不可见；
   - 同一商品上架 → 药品商人消失，商城秘宝出现并可 W 币购买；
   - 武器/防具 NPC 与商城神兵利器页无变化。

## 后续修正（2026-07-26）：密宝页黑色字 / 不能买

### 症状

上架的类别 10 出现在商城「秘宝道具」后，名称呈黑色且购买异常；类别 14 正常。

### 根因

1. **色号字节误用「形象」**：`14/5` iteminfo 中名称后的 `u8` 经 `mmShop:sub_7BC` 写入
   row+14，列表绘制按装备「品质」小色板解释（0 黑/白、1 绿…）。类别 14 的
   `形象` 多为 `1`（碰巧是绿色）；类别 10 药品/壶常用 `形象=13`，超出色板 → 黑字
   且购买行状态异常。
2. **酷宝价未覆盖类别 10**：目录加载只对 `类别==14` 改用「酷宝」列。类别 10 的
   802/809/828 等仍带着铜币「价值」（0 或上百万），W 币购买路径会错价或买不起。

### 修改

- `vm_net_mock_load_shop_catalog_dsh`：凡非装备且 `酷宝!=0`，目录价改用酷宝。
- `vm_net_mock_build_shop_iteminfo_page_blob`：仅 `subtype==5` 时，将 wire visual
  钳制到 `1..4`，越界则发 `1`（绿）。神兵等装备页不改。

### 复测

- 上架的类别 10（含壶/经验卡/回春散）在密宝页名称应为绿色并可 W 币购买。
- 类别 14 与神兵页无回归；下架类别 10 仍只在药商。
