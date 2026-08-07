# 普通跑图场景加载停滞与重入

日期：2026-08-07  
状态：已修复服务端最早违反的切图契约；待真实客户端回归

## 触发与原始证据

用户在普通边缘跑图时遇到两项现象：

1. 目标场景长期停在加载界面；
2. 加载完成后又出现一次同目标场景重入。

本轮读取 `bin/server_out.txt` 的目标链为：

```text
c04临安_05.sce -> c04临安_06.sce
scene_pending ... c04临安_06 pos=(61,189) reason=scene-target-remember
mock_scene_target_remember serial=3 ...
mmgame-transfer-followup target scene=c04临安_06 needsDownload=0 ...
mock_mmgame_scene_transfer_followup ... resources + 30/2(no-posinfo)
```

同一份日志还显示普通 `WT 2/3` 首次请求在旧实现中只得到无坐标
`WT 30/2`。随后只要请求中含有 `25/5`，即使它同时包含 `6/1`、
`6/13`、`6/14`、`2/10 Type=101`，都会先被
`vm_net_mock_is_mmgame_scene_transfer_followup_request()` 接管。

## 客户端事实

IDA 通过 `binary_name == 江湖OL.CBE` 选择当前实例后复核：

| 客户端位置 | 已确认行为 |
| --- | --- |
| `scene_handle_change_result_scene_pos` `0x01039770` | 仅当 `WT 30/2` 含 `result=1`、`scene` 和 `posinfo` 时才调用场景对象的 enter/update 方法；无 `posinfo` 时只执行 `ResetDownloadState`。 |
| `scene_handle_enter_with_scene_pos` `0x010396D6` | `WT 30/1` 的 `scene + posinfo` 同样进入场景生命周期。 |
| `EnterSceneByMapName` `0x01018150` | 写入目标 scene/坐标并启动新的场景 screen 生命周期。 |
| `0x01018166` | 实际为 `FindEmptyActorSlot`，只遍历 actor slot；它**不是**边缘传送门的本地场景进入函数。 |

因此，“边缘传送已经在本地创建好目标场景，首个 `2/3` 不需要
坐标”的旧假设不成立。它使首个有效服务器响应从未调用客户端场景
进入路径；而之后以不精确 detector 处理复合 `25/5` 请求，又丢失了
该请求明确要求的任务/actor 子响应。

## 根因

最早错误状态是：普通边缘切图目标已被服务器记为 pending，但并没有
向客户端下发该目标唯一的 `scene + posinfo` 进入结果。

旧链路是：

```text
WT 2/3 (首次普通边缘切图)
  -> 30/2 {result, type, scene}                 # 无 posinfo，不进入场景
  -> remember target pending
WT 25/5 + 6/* + 2/10 (或其它含 25/5 的复合请求)
  -> 被 mmGame standalone handler 截走
  -> 资源 + 30/2 {result, type, scene}          # 仍无 posinfo
```

这种状态既没有合法的首次场景进入，也没有保持复合请求的协议归属。
它会表现为加载等待；如果之后另一条重复 `2/3` 误带坐标，则会发生
同目标重入。

## 修复

服务端现在显式记录每个 pending target 的
`sceneEnterPosinfoSent` 生命周期位：

```text
首次普通 WT 2/3
  -> 唯一一次 30/2 {result=1, type=2, scene, posinfo}
  -> target.sceneEnterPosinfoSent = true, keep pending

后续 standalone WT 25/5
  -> 资源/NPC + 30/2 {result=1, type=2, scene}  # 无 posinfo，仅收尾

后续复合 WT 25/5 + 6/* + 2/10
  -> 保持在 scene-task/resource handler
  -> 对 pending target 构建目标场景的 task/resource 数据
  -> 不再追加 scene-channel object；避免超过客户端十个 business object 的上限
```

具体修改：

- `src/server/mock_server_social.c`
  - 首个普通 `WT2/3` 发出唯一的坐标型 `30/2`；
  - 同一 pending target 的重复 `2/3` 保留既有阶段，只答无坐标确认；
  - mmGame transfer detector 收窄为**独立** `WT25/5`，不再按“包含
    `25/5`”抢走复合请求。
- `src/server/mock_server_interaction_login.c`
  - `WT6/1` 和 `WT25/5 + 6/*` 的 pending 完成阶段使用 target scene，
    而不是尚未落库的 source scene；
  - 已发过坐标型进入结果的 target 只会在 standalone resource callback
    得到无坐标 `30/2` 收尾；复合 task callback 保持请求所需对象，避免
    超过十对象限制。
- `src/server/mock_server_equipment_npc.c`
  - 场景目标结构体新增上述显式阶段位，避免按场景名或时间窗口猜测
    是否已经进入。

## 回归验证

新增无监听、无账号写入的服务端协议回归：

```text
scripts/scene-transition-entry-contract-regression.c
```

运行命令：

```powershell
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/scene-transition-entry-contract-regression.c src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c '-Wl,--gc-sections' -o obj/server/scene-transition-entry-contract-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32
obj/server/scene-transition-entry-contract-regression.exe
```

结果：通过，断言：

1. 首个 `WT2/3` 恰有一个带 `posinfo` 的 `WT30/2`；
2. 同一 pending target 的重复 `WT2/3` 只得到无坐标确认，target serial
   不变；
3. `WT25/5 + 6/* + 2/10` 不再由 standalone mmGame handler 接管；
4. 该复合完成响应不含任何 `WT30/2`，且 pending target 被完成；因此
   不会增加第二次场景进入，也不会越过客户端十对象分发上限。

该回归只验证协议和服务端阶段归属；真实客户端仍需覆盖：连续边缘跑图、
同场景往返、切图后立即打开/返回商城，以及资源下载场景。

## 已排除项

- 不是动态 NPC 列表本身造成的首次加载停滞；停滞发生在其 parser
  之前的 scene-entry 契约。
- 不是可通过“吞掉重复请求”解决的问题。重复 `WT2/3` 仍收到正确的
  无坐标确认，只是不再改变已经记录的场景进入阶段。
- 不是 host update-complete reentry；当前 remote client 的
  `vm_net_mock_consume_update_completed_scene_reenter()` 是只读 false stub。
