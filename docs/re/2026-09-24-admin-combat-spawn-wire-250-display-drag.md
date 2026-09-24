# 后台刷怪点保存 218 刷新显示 250 / 不能拖动

日期：2026-09-24

## 症状

1. 「场景资源与服务端动态 NPC」在 `36节日庆典_01.sce` 增加怪物 ID `218` 刷怪点，保存成功。
2. MySQL `server_scene_combat_spawns.real_actor_id` 仍为 `218`（未变）。
3. 刷新后台地图后，刷怪点显示为 `#250`。
4. 进图踩怪/开战正常（仍按 real `218`）。
5. 后台预览拖动刷怪点失败（松手回弹）。

## 根因

权威链路：

1. 保存时 SCE field `0x0E` 常写 ParseMinfo 安全线号 keep-id `#250`；真实目录 ID 进绑定表。
2. 库存 ID 若 `resource_labels[].actorResource` 为空、但 `fill_from_catalog` 已从 override 补上 `.actor`，`pick_map_wire_id` 仍会落到 `#250`。
3. 后台 `apply_bindings` 以前**只按 (scene,x,y)** 把 wire→real。坐标对不上时仍显示 SCE 线号 `#250`；开战 remap 有 wire-unique 回退，故踩怪正常。
4. 拖动走 `save-combat-spawn-position` → `locate_all` → `admin_save_all`。若仍拿到 `#250` 且目录无该 ID，`fill_from_catalog` 失败 → 坐标回滚，表现为「不能拖动」。

首次偏离：读 SCE 后后台未按与开战相同的绑定解析显示 real ID。

## 修复

1. `apply_bindings` 改为调用 `label_enemy_for_wire`（坐标绑定 → wire-unique → 名/Actor 唯一 → 保留 wire）。
2. `pick_map_wire_id`：户外非自定义、`≤300` 且已有 primary `.actor` 时保留自身线号，避免无谓 `#250`。
3. 拖动保存：若任一刷怪点仍解析不到目录内真实 ID，返回明确错误，避免静默回弹。

## 验证

1. `make -j2`
2. 后台打开节日庆典图：已有 `218` 点应显示 ID `218`（不是 `250`）。
3. 拖动该红标松手：提示坐标已保存；刷新后坐标与 ID 仍为 `218`。
4. 进图踩怪仍为目录 `218` 属性。
