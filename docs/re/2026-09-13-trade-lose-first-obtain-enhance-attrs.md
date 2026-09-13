# 交易后首获随机强化档次属性丢失（2026-09-13）

## 触发与现象

玩家 A 将带有「首次获得」洗练 seed（`enhance_wash` 四档 `washed=1`）的未强化装备
交易给玩家 B 后，B 侧装备详情强化附加变回配置默认黄字（或空覆盖），
首获随机档丢失。重登后必现。

## 业务链路

1. 报价校验 `trade_validate_offer`：从卖方背包拷贝 `enhanceWash`（及 instance）。
2. `trade_commit`：`trade_role_add_item` 把 wash 写入买方内存背包行。
3. `trade_persist_pair`：DELETE + INSERT `account_role_backpack`。
4. `durableInventoryCommitted=true`：清 inventory dirty，不再走完整 `role_db_save`。
5. 重登 / 账号切换：`role_db_load` 从 MySQL 读回背包。

## 首次偏离

第 3 步 INSERT 列仅为
`account_id,role_id,slot_index,item_id,item_seq,item_count,enhance_level`，
**未写 `enhance_wash` / `item_instance_id`**（默认空 wash / 0）。

内存角色在成交当次仍持有 wash，故同会话立刻开背包可能仍正常；
权威库已丢，且 dirty 被清掉，后续不会被完整存档补回。

## 根因

交易专用持久化路径与 `role_db_save` 背包契约不一致：
首获 seed / 洗练覆盖的权威是背包行上的 `enhance_wash`，应随交易落库转移。

**不依赖 `account_role_enhance_wash_record`**：该表主要服务玩家主动洗练后的
穿脱/合并兜底；未洗练的首获 seed 通常没有可用 record，不能当作交易恢复来源。

## 修改

1. `trade_persist_pair`：INSERT 对齐完整背包列（`item_instance_id` + `enhance_wash`）。
2. 交易链路保留 `enhanceInstanceId`，原样拷贝 `enhanceWash`。
3. 成交后双方立即 `role_db_save("trade-commit-immediate")`。
4. 洗练 29/9（次数/扣费）与 29/14 替换确认：立即 `role_db_save`。
5. 首获 seed / 继承非空 wash 入包：立即 `role_db_save`（失败才 deferred dirty）。
6. 登录强制 reload：dirty 堆先 flush（见
   `2026-09-13-login-reload-drop-dirty-enhance-wash.md`）。

## 验证

- [ ] A 首获随机装备（未洗练）交易给 B：B 详情四档与 A 成交前一致
- [ ] B 重登后四档仍在；A 侧该实例已移除
- [ ] 空 wash（默认黄字）装备交易后仍为默认档，不误 seed
- [ ] `make -j2` 通过

## 仍未知 / 风险

- 历史上已被空 wash 落库的装备无法自动恢复（无可靠旁路权威）。
- 跨角色复用同一 `item_instance_id` 数值在极端碰撞下仍依赖 counter refresh。
