# 世界 BOSS 线号借用污染库存怪显示名

## 症状

后台怪物目录 `#18`（库存鬼火）显示名变成「世界BOSS」，与自定义世界 BOSS `#213` 同名、同 `e_ghostfiresR.actor`。

## 根因

1. 户外注入自定义怪时，`pick_map_wire_id` 为通过 ParseMinfo，会**借用**同 `.actor` 的库存线号（`#213` → wire `#18`）。
2. SCE 落盘后 nameplate 写在 wire 位上：`actorId=18` + 显示名「世界BOSS」。
3. 进程重启后 `resource_labels_load` 按 SCE 的 wire `actorId` 回填目录显示名 → **#18 被污染**。

绑定表虽记 `wire→real`，战斗 remap 正确，但标签扫描此前未按 real 归属。

## 修复

| 点 | 行为 |
|----|------|
| `label_enemy_for_wire` | 扫描 SCE 名牌时，有 binding 则归属 **real** id，不写到 wire 库存位 |
| `pick_map_wire_id` | custom / `>300` 一律用 FB keep-id `#250`（不用 `#200`=青龙王），不再借同 actor 库存 id |
| bindings 保存成功 | `monster_resource_labels_invalidate()`，下次重扫 |

## 已污染环境运维

代码上线后仍可能残留错误 SCE 字节，需任选：

1. 后台打开「活死人墓」等已注入世界 BOSS 的图，删掉错误刷怪点后**按新逻辑重存**（应落 `#250` + binding）；或  
2. 清掉该图 `server_scene_combat_spawns` 绑定并恢复干净 SCE，再注入世界 BOSS。

然后重启服务端，确认 `#18` 恢复为库存名、`#213` 仍为「世界BOSS」。

## 验证

- 新注入 custom 户外怪：wire=`250`，binding `250→real`，库存同 actor 怪名不变。  
- FB keep-id 为 `#250`（`VM_NET_MOCK_SCE_COMBAT_MAP_WIRE_FB`）；历史文档里的 `#200` 为旧约定。  
- 标签缓存失效后重扫：有 binding 的 nameplate 记到 real，不记到 wire。  
- `make -j2`。
