# 后台场景传送点编辑（2026-07-27）

## 能力

游戏内容管理页增加「传送点编辑」：

- 调整边缘传送触发矩形（left/top/right/bottom）与接近点（spawn）
- 修改目标场景、入口 ID（entryId）、exit ID（targetEntryId）
- 调整落脚点（写入**目标场景**中相同 `entryId` 的 spawn）
- 新增 / 移除边缘传送点

META / 命名付费传送点仍只读展示，不在本版编辑。

## 权威数据

传送点几何在客户端本地 SCE 中触发，因此保存路径是：

1. 解码 `JHOnlineData/*.sce`（LZSS 资源包装 → `SCE2`）
2. 定位 / 替换 / 插入 / 删除 `edge_portal` 记录
3. 以字面量 LZSS 重新封装并写回权威目录
4. `vm_net_mock_update_admin_publish_named_files` 发布，供 WT 18/7 下载

无 MySQL 覆盖表；SCE 字节即为权威。

## 落脚点规则

与运行时 `vm_net_mock_resolve_sce_edge_portal_target` 一致：

- 源传送点给出 `targetScene` + `entryId`
- 落点取目标 SCE 中 `entryId` 匹配的边缘传送点 `spawnX/Y`

保存源传送点时若填写落脚点，会同步改写目标场景对应入口；目标缺少该
`entryId` 时保存失败并提示。

## 验证

- `make -j2 SERVER_TARGET=bin/jh-online-server-portal-edit.exe`
- 打开内容页任选场景，编辑既有边缘传送点坐标并保存，预览框位置更新
- 客户端清缓存或走资源更新后，踩门落到新落脚点
- 新增后列表出现新项；移除后触发区消失
