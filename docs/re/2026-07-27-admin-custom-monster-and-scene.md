# 2026-07-27 后台自定义怪物与场景刷怪 / 克隆场景

## 功能

1. **怪物管理 · 新增自定义怪物**
   - 入口：`/?tab=monsters&new=1`
   - 写入 `server_monster_catalog_extra` + `server_monsters`（及可选掉落行）
   - 列表与编辑页标记「自定义」；自定义怪物可删除，不提供「恢复默认」
2. **游戏内容 · 场景刷怪配置**
   - 覆盖 `automonster.dsh` 三槽，持久化到 `server_scene_monsters`
   - 影响挂机 / 场景战斗选怪
3. **游戏内容 · 新增场景**
   - 从现有 `.sce` 二进制克隆；若存在同名 `.map` 一并复制
   - 经具名资源发布，供客户端 WT 18/7 下载

迁移脚本：`server/mysql/migrate_add_custom_monsters_and_scenes.sql`（表定义亦已并入 `schema.sql`）。

## 客户端 Actor 注意

战斗外观由**怪物 ID 对应的客户端 Actor**决定，不是后台显示名称。

- 若选用客户端已有战斗 Actor ID，开战外观正常。
- 若使用无客户端模板的新 ID，开战可能异常或缺模型。
- 建议自定义怪物优先复用现有战斗 Actor ID，仅覆盖属性 / 掉落 / 刷怪槽。

## 验证要点

- 创建自定义怪 → 目录出现「自定义」→ 保存属性 / 删除
- 场景刷怪保存后挂机选到配置 ID；重置后回到 automonster 默认
- 克隆场景后内容页可选中新 `.sce`，客户端能通过更新通道拉取
