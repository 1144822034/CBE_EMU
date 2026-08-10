# 游戏数据内容更新的 MySQL 权威存储

## 目标

取消具名资源发布。普通资源由客户端实际 WT 18/7 请求驱动；任何需要使既有客户端
缓存失效的**非 CBM 游戏数据**都可进入启动内容更新，包括 SCE、MAP、ACTOR、GIF、
XSE、DSH 及其他安全的资源根目录叶文件。

## 持久化契约

`server_content_update_releases(config_id=1)` 保存 WT 18/9 / 18/8 的
`enabled`、`release_id`、`manifest_code`。`server_content_update_files` 按
`sort_order` 保存资源文件名。文件名为 `VARBINARY`，因为下行包需要原始 GBK 字节。

保存时服务端在同一个 MySQL 事务内更新 release、删除旧文件行、插入新文件行并提交；
提交失败则回滚，内存中的上一版本保持不变。客户端不从数据库读取，而是继续按已确认的
WT 18/9 → WT 18/8 → WT 18/7 解析和安装。

## 管理操作

“游戏内容更新管理”的游戏数据内容卡片可：

- 从服务器资源目录多选非 CBM 游戏数据加入版本；
- 从当前清单移除资源；
- 重新发布当前清单（同名资源字节已替换时使用）；
- 清空内容版本。

每次增删或重新发布都会递增 release id、重算校验和并写入 MySQL。部署场景战斗怪
同样调用这一发布边界，因此自动部署和手工管理不会出现两个相互覆盖的状态来源。

## 版本比较与删除条件

`江湖OL.CBE:startup_handle_update_target_metadata(0x0103B59A)` 读取 WT 18/9 的
`type/id/code` 后调用 `startup_screen_is_update_target_already_current()`；本地保存的
目标对与服务端 `id/code` 相同则直接继续启动，不请求 WT 18/8，也不会删除任何缓存。

只有 `id` 或 `code` 不同时，客户端才下载清单并逐项删除其中资源，随后在正常加载时
通过 WT 18/7 重取需要的文件。服务端不按启动次数递增版本，只有管理操作或场景部署
成功后才产生新版本。

单一版本清单是累积的：客户端可能跳过中间版本，因此最新清单需要保留所有仍应失效的
资源名。这意味着选择大量资源会拉长首次更新的逐项处理时间；后台提供文件名筛选，建议
只选择实际变更的资源。资源名严格限制为 99 个协议字节以内，最多 2048 项；CBM 与服务
端 TSV 状态文件不会进入该清单。

## 迁移

服务端首次看到 `content-update-mysql-v1` 尚未写入 `server_data_migrations` 时，读取
一次旧 `server_content_update.tsv`，验证其实际 payload 校验和后迁入 MySQL 并写标记。
没有文件时创建空的禁用配置；校验不一致时不迁移、不伪造新版本。

手工建表脚本：`server/mysql/migrate_add_content_updates.sql`。新安装也可直接执行
`server/mysql/schema.sql`。
