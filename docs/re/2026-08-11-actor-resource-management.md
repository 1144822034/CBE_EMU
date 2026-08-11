# Actor 资源管理

## 目标

后台需要能够查看、编辑和新建服务端权威 `.actor` 资源。该功能不能把
`bin/JHOnlineData` 之类的客户端缓存当作写入目标，也不能允许上传任意二进制
绕过客户端资源格式。

## 已确认的资源契约

`vm_mock_admin_load_data_payload()` 与现有 Actor 预览、动态 NPC 校验共同证明：

1. Actor 外层为四字节小端长度，随后是 type-2 LZSS 容器；
2. 解压后的顺序是图片名称表、源矩形表、动画数、每个动画的部件表以及每个部件的帧；
3. 每帧有五个 `s32` 值，依次为矩形索引、X/Y 偏移和两个保留/行为字段；
4. 图片依赖必须是服务端资源根目录中可解码的 type-1 GIF；
5. 客户端已有旧缓存时，正确更新路径是 WT18/9 + WT18/8 失效清单，随后客户端按
   WT18/7 正常资源加载器下载，而不是服务端复制客户端文件。

`vm_net_mock_scene_battle_monster_lzss_literal_encode()` 已用于生成可被客户端解码
的 type-2 容器；它使用可验证的 literal LZSS 编码，适用于 Actor 的同一外层格式。

## 实现

- 新增 `?tab=actors`：左侧列出服务端资源根的 Actor，右侧提供预览、结构统计与四个
  可编辑清单（图片、矩形、动画部件、动画帧）。
- 新建资源从已有 Actor 模板载入，要求新文件名为无路径的 ASCII `.actor` 文件名。
- 保存路径只由 `vm_net_mock_update_resource_path()` 解析，因而始终是服务端配置的资源根
  或经 `task.dsh` 验证的服务端回退根；不会写客户端资源目录。
- Actor 目录遵循后台通用的局部导航约定：切换资源或新建模板只替换右侧编辑器，保留目录
  滚动位置与浏览器历史；详情见
  [后台列表/详情局部导航约定](2026-08-11-admin-partial-navigation-contract.md)。
- 保存前重新编码 type-2 容器；写入使用同目录临时文件和原子替换。写入后复用
  `vm_net_mock_ensure_actor_resource_available()` 回读校验 Actor 与 GIF 依赖。
- 内容更新发布失败或回读校验失败时，已有资源恢复原始字节；新资源删除，以免磁盘内容
  与 MySQL 内容更新清单分叉。

## 验证

`make -B obj/server/main.o` 已通过，证明包含后台、资源编码和服务端聚合单元的 C 源可编译。
本次常规 `make -j2` 的最终链接被正在运行的
`bin/jh-online-server.exe` 占用；没有终止用户服务进程。文件释放后应重跑：

```powershell
make -j2
```

人工回归：编辑一个已有 ASCII Actor 的单个帧偏移并保存；确认后台显示“已发布”，完整重启
客户端后观察 WT18/9、WT18/8、WT18/7 与更新后的预览/场景模型。再尝试引用不存在 GIF，
应被拒绝且原资源字节不变。
