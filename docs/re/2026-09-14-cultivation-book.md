# 修炼天书 920/921

## 需求

1. **记录（空白 920 → 实例 921）**：等级 **>49**（即 ≥50）的玩家使用空白天书，把「当前升级所需经验」写入书本容量。
2. **单本限制**：同一角色同时只能有一本**未注满**的记录本；再使用 920 会被拒绝。
3. **注入**：记录玩家打怪结算经验会**复制注入**到那本未注满书（角色仍照常获得经验）；注满后停止注入。
   - **享受加成**：注入量 = 战斗结算后的 `rewardExp`（已含经验卡、战斗心得、姻缘世界 buff；打金心得清零后则为 0）。
4. **交易**：仅**注满**的 921 可交易；未注满拒绝上架。
5. **使用**：使用者等级须在 **[天书等级−10, 天书等级−1]**。例如 50 级天书→40–49 级；51 级天书→41–50 级。成功后获得书内经验并消耗 921。
   - **不享受加成**：`role_add_exp(filled_exp)` 裸加，不经经验卡/战斗心得/姻缘等倍率。

## 协议（已证实）

| 道具 | 请求 | 响应 |
| --- | --- | --- |
| 920 | `1/7/35 {seq}` | `1/7/35 {result,bookdes}`；**result=0** 客户端删空白，同包可带 `7/7 type=1` 加入 921 |
| 921 读 | `1/7/38 {seq}` | `7/38 {bookdes}` |
| 921 用 | `1/7/40 {seq}` | `7/40 {result,bookinfo[,level,lastexp,curexp,persentexp]}`；**result=0** 成功并删书 |

证据：`docs/re/2026-07-23-special-item-use-contract.md`。

## 权威状态

表 `account_cultivation_books`：

| 字段 | 含义 |
| --- | --- |
| `book_id` | 主键；写入背包 `enhance_instance_id`，交易随道具迁移 |
| `book_level` | 记录时角色等级 |
| `capacity_exp` | `level_up_cost(book_level)` |
| `filled_exp` | 已注入经验 |

## 实现入口

- `vm_net_mock_build_cultivation_book_response`（7/35·38·40）
- `vm_net_mock_role_inject_cultivation_book_exp`（战斗奖励结算后）
- `vm_net_mock_cultivation_book_is_tradeable_instance`（交易校验）
- 登录 `expbook`：有未注满记录本时为 1
- Pixi：`useCultivationBookBlank` / `useCultivationBook` / 详情拉 `readCultivationBookDes`（7/38）
- 列表名：背包刷新后异步 `7/38` → `修炼天书 LvN·满/未满`；交易锁定 `21/6` 对 `itemId<1000` 可选 string 带同款短名（对端可见）
- migrate：`server/mysql/migrate_add_cultivation_book.sql`

## 验证

- [x] `make -j2`
- [ ] 打开 921 详情：显示等级 / 经验进度 / 状态 / 使用说明（7/38 bookdes）
- [ ] 背包列表 / 交易锁定栏：921 显示 `Lv等级·满|未满`
- [ ] 已有未注满 921 时再用 920：失败且不扣
- [ ] 打怪：书 filled 增加；角色经验仍增加
- [ ] 注满前交易 921：失败；注满后可交易
- [ ] 50 级天书：40–49 级可用，39 与 50 级不可用；使用后经验增加且不吃经验卡加成
- [ ] 51 级天书：41–50 级可用
