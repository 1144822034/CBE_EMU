# 副本向导进入时的资源更新循环

## 状态

`fixed`，2026-08-07。

## 触发与证据

用户从副本向导进入副本后，客户端停留在资源更新界面。`bin/server_out.txt` 的同一次
链路为：

```text
mock_npc_instance_enter actor=30406 scene=b_29梦境空间 pos=(50,50) response=30/1
mock_update_chunk_missing subtype=7 file=b_29梦境空间 version=1 len=86
```

服务端资源根 `web/fs/JHOnlineData/` 中实际存在：

```text
b_29梦境空间.sce  167 bytes
b_29梦境空间.map  434 bytes
```

并且该 SCE 的第一个资源字符串就是 `b_29梦境空间.map`。因此地图与场景资源均不缺失；
首次偏离是 `30/1` 下发了没有 `.sce` 的目标名，客户端随即按同一裸名发出真实 `WT18/7`。
资源服务严格按请求名查找，正确拒绝了不存在的 `b_29梦境空间`，客户端无法完成安装而循环。

## 根因

后台动态 NPC 的 `vm_mock_admin_scene_file_to_runtime_key()` 曾把所有选择的 SCE 文件转换
为所谓的“运行时键”。对 `b_*` 副本场景，该转换去掉 `.sce` 并把裸名持久化到
`server_dynamic_npc_instances.target_scene`。服务器内部 SCE 检查允许可选后缀，故该错误
配置能够通过保存与载入；但客户端的命名资源下载协议没有这个后缀猜测契约。

## 修复

1. 后台场景选择器将所选 `.sce` 文件名原样保存，不再添加/删除前缀或扩展名；
2. 副本向导配置显式要求目标有 `.sce` 后缀且能在服务端资源根以精确名称打开；
3. 服务启动加载动态 NPC 时，对历史的无扩展目标仅在 `<旧键>.sce` 确实存在时执行一次
   MySQL 持久化迁移；找不到精确 SCE 的行记录 `unresolved` 并不下发；
4. `WT18/7` 仍不会为裸名补后缀或发送伪造成功包，避免把错误的目标状态隐藏为“资源已更新”。

## 回归

`scripts/instance-scene-resource-key-regression.c` 仅使用资源根目录，验证：

- 后台转换后 `b_29梦境空间.sce` 保持逐字节相同；
- 历史裸键只在同名 `.sce` 存在时可迁移；
- WT18/7 能读取完整 `.sce`，但继续拒绝裸键。

仍需客户端复测“副本向导 → 进入副本”：日志应出现
`mock_npc_instance_enter ... scene=b_29梦境空间.sce`，若客户端缓存缺失，应出现
`mock_update_chunk ... file=b_29梦境空间.sce`，随后资源完成回调与场景进入继续推进。
