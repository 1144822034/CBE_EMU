# PixiJS 男角色显示成女角色

## 触发

选角 / 进图 / HUD 头像 / 装备立绘 / 战斗己方：数据库与 CBE 均为男角，Pixi 却加载 `hW_*` / `ui_hw_*` 女体。

## 根因

标题角色列表 compact `actorinfo`（`mmTitle:0x3544` / mock `role->sex + 1`）线上为 **sexGroup 1=男 2=女**。

`parseTitleRoleList` 原样写入 `RoleRow.sex`，而 `actorResourceForJobSex` / `uiActorResourceForJobSex` 约定 **0=男 1=女**，并用 `sex ? 1 : 0` 取资源：

| DB | 线上 sexGroup | 旧 RoleRow.sex | 资源索引 | 显示 |
|----|---------------|----------------|----------|------|
| 0 男 | 1 | 1 | 女 | **错** |
| 1 女 | 2 | 2 | 女 | 碰巧对 |

附近玩家 / 队伍 / 他人装备路径已做 `sexGroup - 1`，只有标题选角得到的 self `RoleRow` 未归一化。

首个偏离：`parseTitleRoleList` 未把 sexGroup 收成 0/1。

## 修复

- `parseTitleRoleList`：`sex = sexGroup >= 1 ? (sexGroup - 1) & 1 : 0`
- `demoBuildServerSelect`：按内部 0/1 再编码为线上 sexGroup
- smoke：男 Demo 用 `sex: 0`，并断言解析结果

## 验证

- 男角选角预览、进图主角、人物头像、装备面板立绘为男体
- 女角仍为女体
- 同图 peer 外观与本端一致（既有 `visualGroup - 1` 路径）
