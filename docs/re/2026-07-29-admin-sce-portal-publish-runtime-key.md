# SCE 传送点保存：具名发布用了无扩展名 runtime key

## 症状

管理后台「游戏内容管理」→ SCE 场景 → 传送点编辑 → 保存，报错类似：

`Actor 或引用图片无法作为具名资源发布`（旧文案；现已拆成更具体的失败原因）。

## 根因

1. 表单经 `vm_mock_admin_scene_file_to_runtime_key` 得到**无 `.sce`** 的 runtime key（如 `c00蓬莱仙岛_02` / `00蓬莱仙岛_02`）。
2. `vm_net_mock_write_scene_resource_bytes` 通过 `open_server_scene_resource` 自动补 `.sce`，写入成功。
3. `vm_net_mock_publish_scene_resource` 却把同一 runtime key 原样交给
   `update_admin_publish_named_files`；`update_resource_path` 按**字面文件名**打开，
   找不到无扩展名文件 → `size <= 0` → 误报成 Actor 发布失败。

落脚点目标场景名带 `.sce`（表单校验），通常能发布；源场景先失败，用户只看到上述错误。

## 修改

- `publish_scene_resource`：先 `open_server_scene_resource` 解析真实路径，取磁盘 leaf（`*.sce`）再发布。
- `update_admin_publish_named_files`：区分文件名不安全 / 不存在 / 过大 / 版本失败，避免笼统 Actor 文案掩盖 SCE 问题。

## 验证

1. `make server -j2`
2. 打开任意有边缘传送点的场景，改坐标并保存 → 应提示「传送点已保存并发布场景资源」
3. `server_update_catalog.tsv` 中出现对应 `named ... xxx.sce` 行（带 `.sce`）
