# 副本目标场景怪物未出现：资源完成后的场景重入

## 触发与证据

副本配置为目标场景 `测试地图.sce`、`spawn_enemy_id=1001`。服务端日志的关键顺序为：

1. `mock_npc_instance_enter ... response=30/1`；
2. `mock_teleport_stone_current_scene_complete ... 30/2-no-posinfo`，随后目标被标记完成；
3. 客户端才发起并完成 `WT18/7`，安装 520 字节的 `测试地图.sce`；
4. 没有出现 `screen_mgr allow-update-reenter`，因此客户端没有重新走场景实体流解析。

离线 SCE 扫描确认安装资源包含 `actor=1001` 的 kind-3 记录，位置为 `(120,160)`，所以问题不在
`spawn_enemy_id`、记录格式或资源下载内容，而在资源完成回调时目标生命周期已被清除。

## 根因

副本进入复用了普通场景的 `30/1 -> 30/2` 完成路径。客户端发现同名 SCE 需要更新时，
`WT18/7` 晚于首次场景完成；服务端只在活动目标仍存在时允许一次资源完成重入，已完成目标没有
被恢复，导致新 SCE 虽已安装但没有新的场景加载/节点创建。

后续“退出再进入仍不出现”的复测又暴露了第二个更早的数据依赖缺口：战斗怪部署过去只把
`测试地图.sce` 加入内容更新 manifest。SCE kind-3 实体同时引用主 Actor 和 field18 特效
Actor；如果客户端缓存中缺少其中任意资源，即使重新进入并重新解析了新 SCE，也不能创建
完整实体。当前夹具中的依赖是 `e_batB.actor` 和 `e_ghostfireR.actor`。

## 修复

`vm_net_mock_consume_update_completed_scene_reenter()` 现在只在以下窄条件下恢复最近完成目标：

- 活动场景目标已清除；
- 最近完成目标仍在既有复用窗口内；
- `WT18/7` 完成文件名与该目标场景名完全一致。

恢复后仍通过客户端原生 screen manager 的重入路径，未修改客户端寄存器、内存或网络响应字节。
普通场景的 Actor/GIF 依赖更新仍沿用原有活动目标逻辑，不会被该兼容分支拦截。

`vm_net_mock_scene_battle_monster_admin_deploy()` 现同时收集并发布：

- 目标 SCE；
- 每条启用记录的主 Actor；
- 每条启用记录的 field18 特效 Actor。

文件名按首次出现顺序去重，仍通过原生 `WT18/9 -> WT18/8 -> WT18/7` 安装流程；没有把
怪物伪装成动态 NPC，也没有直接修改客户端节点表。部署日志新增 `manifest_files`，用于核对
本次发布是否包含完整依赖集。

## 验证

- `make -j2`：通过。
- `git diff --check`：通过（仅有工作区既存 CRLF 警告）。
- `scripts/content-update-manifest-regression.c` 新增场景战斗怪依赖收集断言，覆盖主 Actor、
  特效 Actor 和重复依赖去重；语法检查和链接通过。当前 Windows 沙箱运行该既有全量
  `src/main.c` 夹具时在进入测试主体前无输出返回 1，不能将其作为业务通过证据。
- 端到端副本回归暂未执行：当前环境没有 `CBE_AUTOMATION_MYSQL_PASSWORD`，且规范禁止对用户
  `jh_online` 数据库进行自动化写入。下一次隔离测试应确认日志出现
  `content_update_publish` 且部署日志 `manifest_files>=3`；客户端安装
  `测试地图.sce/e_batB.actor/e_ghostfireR.actor` 后，应出现
  `scene_target_restore_for_update_reenter`、`screen_mgr allow-update-reenter`，随后由客户端
  重新解析目标 SCE 并创建 `actor=1001` 节点。
