# 后台角色位置恢复：指定场景

## 需求与第一次偏离

“附近传送石”会隐含服务端对“附近”的拓扑判断：同一世界图可能存在多个连通分量，且相似的场景名并不表示同一张地图。后台恢复操作需要由管理员明确指定要恢复到哪一张场景，而不是根据当前异常状态推断目的地。

## 数据契约

后台场景输入使用服务端资源目录列出的精确 `.sce` 文件键：

- 页面只列出 `web/fs/JHOnlineData`（或配置的服务端资源根）中的 `.sce`；
- 表单接收 UTF-8 显示名后转为游戏使用的 GBK 键，并重新在服务端目录中验证；
- 不做 `c` 前缀、去扩展名或同名场景归并；
- 目标坐标只通过
  `vm_net_mock_get_scene_reasonable_spawn_from_sce()` 和
  `vm_net_mock_adjust_safe_player_pos_for_scene()` 从指定 SCE 得出。

因此管理员指定的是场景，不能指定未验证的任意像素坐标。没有可用 SCE 落点、目标资源不存在或表单名称不精确时，操作失败且不写入角色数据。

## 持久化与在线边界

账号管理中的角色行现在显示当前位置，提供可搜索的场景输入框和“重置到指定场景”按钮。

当前操作会先断开目标角色的在线会话，再重新加载离线后的角色快照。随后以既有
`vm_net_mock_role_db_save_relational(..., full_snapshot=true, ...)` MySQL 事务保存选中角色的精确 `scene/x/y`；完整快照是必要的，因为选中角色不一定是当前角色。成功日志为：

```text
[info][mock-admin] role_selected_scene_reset ... landing_source=SCE action=commit
```

该操作不构造客户端场景切换包、不改变客户端内存，也不回退到出生点；下一次正常进入游戏时读取新的持久化位置。

## 回归

`scripts/admin-role-selected-scene-reset-regression.c` 是资源级隔离测试：不监听端口、不连接 MySQL、不修改账号。它验证：

1. 表单中的 `01桃花岛_01.sce` 被保留为完全相同的运行时 SCE 键，并解析出安全落点；
2. 不在服务端资源目录中的 SCE 键会被拒绝，不能获得默认场景。

```powershell
make -j2
gcc -DNETWORK_SUPPORT -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/admin-role-selected-scene-reset-regression.c obj/client/gifDecode.o obj/client/cbeParser.o obj/client/mystd.o obj/client/fontEngine.o obj/client/vmMalloc.o obj/client/fileIoEngine.o obj/client/lcd.o obj/client/automation_png.o obj/client/md5.o obj/server/mysql-client.o '-Wl,--gc-sections' -o tmp/admin-role-selected-scene-reset-regression.exe -lpthread -liconv -lm -lmingw32 -lkernel32 -lws2_32 Lib/unicorn-2.1.4/unicorn-import.lib '-LLib/sdl2-2.0.10/lib' -lSDL2main -lSDL2
$env:PATH = "$PWD\bin;$env:PATH"
.\tmp\admin-role-selected-scene-reset-regression.exe
```
