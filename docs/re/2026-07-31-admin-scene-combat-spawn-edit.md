# 后台地图刷怪点编辑（2026-07-31）

## 能力

游戏内容管理页增加 **地图刷怪点**：

- 查看当前 SCE 中 kind=3 战斗点（预览红标）
- 编辑坐标 X/Y、**怪物目录 ID**；勾选删除；末行可新增
- 主 Actor / 显示名来自怪物管理（目录指定）
- 保存后重写 SCE、publish 场景 + Actor/GIF（WT18/7）

与上方 **挂机选怪三槽** 分离：三槽只影响挂机选 ID，不造地图精灵。

## 权威数据

1. 解码场景 SCE  
2. FB 图：在命名传送前 splice `08 00` + kind=3（保留装饰落点）  
3. 每条刷怪点：
   - **后台 / 战斗真实 ID** ← 怪物目录（含自定义）  
   - **SCE field `0x0E` 线号** ← ParseMinfo 安全键：FB 固定 `#200`；户外优先户外存量 ID / 同主 Actor 模板，否则 `#200`  
   - **SCE 显示名** ← **线号存量名**（FB `#200`=幽冥鬼火）；自定义名写进 field `0x0F` 会在 mmGame ScreenInit 崩（见下）  
   - **地图精灵**：FB 强制 `e_ghostfireB/G`；户外按目录主 Actor + dual 模板  
4. MySQL `server_scene_combat_spawns`：`(scene,slot) → x,y,wire_actor_id,real_actor_id`  
5. 碰撞开战：请求里的 wire id + 坐标 → remap 到 `real_actor_id` 再取属性  
6. 写回并 publish 场景、内嵌 `.map`、Actor 依赖  

## 已知问题 / 修复

1. 无后缀场景名 publish → 已兼容并镜像 `.sce`  
2. FB 缺 `08 00` → 固定 splice  
3. 只写单 Actor（如裸 `e_fireG`）→ 闪退；必须 dual  
4. **FB 图写 `e_fireG` 仍闪退**：FB 保存强制 ghostfire 地图精灵  
5. **自定义 ID 不能直接写入 field `0x0E`**：户外 census 无 203/301；客户端当表键会崩。  
   **现方案**：SCE 写 `#200` + ghostfire + **存量名牌**；真实 ID 进绑定表；开战 remap。  
6. **重进闪退**：SCE2 内嵌 map / 临安目标场景未 publish → instance enter 时自动 publish。  
7. **`_01.map` not-published**（2026-07-31）：孤儿本地 map 劫持场景键；host 解析已修。  
8. **自定义名牌「梦魇」进图崩**（2026-07-31）：资源均 `uptodate` 后 mmGame ScreenInit 跳转未映射 `pc=0x4ad5542`（`lr=0x010136b3`，栈见 `ui_h_war.actor`）。与把 field `0x0F` 从「幽冥鬼火」改成「梦魇」同时出现；改回存量名牌后应恢复。自定义名仅开战可见。  
9. **遇怪崩溃（多点 `#200` + remap）**：错位 moveinfo / 带 `25/12` 的 4/5·4/10 均在再进后崩于 `0x01046C48`；现改为 lone `4/10`（见 `2026-07-31-dream-remap-encounter-crash.md`）。  
10. **进图二次 ScreenInit 崩**（2026-07-31）：远程客户端未观察 `30/1`，`27/12+posinfo` 再进同场景 → `pc=0x4ad5542`；已在 `network-client.c` 补观察（见 `2026-07-31-dream-enter-remote-reenter-crash.md`）。  

## 用法

1. 「怪物管理」为该 ID 配好显示名与（户外可选）`actor_resource`  
2. 「地图刷怪点」填目录 ID（含自定义）+ 坐标并保存  
3. 进图：地图名牌仍是线号存量名；踩怪开战吃自定义属性  

## 验证

- `make -j2`，重启 mock；后台再保存一次梦境刷怪点以 publish 新 SCE  
- 日志：`scene_combat_spawn_wire … name=幽冥鬼火`；开战 `requested=203`  
- remap 踩怪期望：`mock_challenge_battle_remap_pure_subtype10 wire=200 real=203`，`objects=1`，进战不崩
