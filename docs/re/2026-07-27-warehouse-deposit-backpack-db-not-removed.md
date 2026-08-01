# 仓库存入后背包表未删除（双写分裂）

日期：2026-07-27

## 现象

存入仓库后 `account_role_warehouse` 有货，但 `account_role_backpack`
仍保留同一件；重登背包又出现，或库内两边各有一份。

## 根因

`vm_net_mock_warehouse_deposit_backpack_seq` 先前顺序为：

1. 改内存（背包清零、仓库追加）
2. `warehouse_persist`（独立 `DELETE/INSERT`，自动提交）
3. `role_db_save`（另开事务写背包）

若第 3 步失败，内存会回滚，但第 2 步已提交 → 仓库表有货、背包表未删。
成功路径下若背包保存与仓库不在同一事务，中途崩溃也会留下同类分裂。

首次偏离：持久化事务边界，不是 26/1 菜单编码。

## 修改

1. `role_db_save_relational` 在同一事务 `COMMIT` 前写入
   `account_role_warehouse`（与背包快照一起提交/回滚）。
2. 存入只调用一次 `role_db_save("warehouse-deposit")`，不再先单独
   `warehouse_persist`。
3. 取回同样只走 `role_db_save`（含仓库）。
4. 背包 INSERT 跳过条件改为 `itemId==0 || count==0`，避免零数量残行写回。

## 验证

1. `make -j2`，重启服务。
2. 存入一件：日志 `mock_warehouse_deposit ... bag_rows=N-1`；
   MySQL 背包无该 `item_seq`，仓库有对应行。
3. 故意制造背包保存失败时（断库），两边都不应只成功一半。
4. 取回后两边仍互斥、无双份。
