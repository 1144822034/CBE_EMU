# player-1 副本场景战斗怪未刷新：内容版本与缓存脱节

Date: 2026-08-30

Status: root cause identified; no code change in this investigation

## 1. 当前卡点

- 可见现象：后台在 `29梦境空间_03.sce` 保存两条场景战斗怪草稿并发布后，player-1
  经 NPC `30406` 进入副本，画面中没有新增怪物。
- 本轮最小目标：确定问题是在草稿/部署、内容更新、SCE 安装还是客户端场景节点创建。

## 2. 运行时证据

1. 发布并未失败。`bin/server_out.txt` 记录当前部署为
   `drafts=2 enabled=6 nodes=13->19 raw=787 payload=767 entity_count=1->7`；两条草稿的
   数量合计为 6 个启用 kind-3 节点。
2. player-1 的 `JHOnlineData` 是到 `bin/JHOnlineData` 的 junction；其中实际
   `29梦境空间_03.sce` 仍为 278 字节、解压 payload 320 字节，只含 13 个场景摆放物和
   portal，`combat_spawns=[]`。它不是刚发布的 787 字节资源。
3. 服务端已保存的 release 资源是
   `web/fs/JHOnlineData/.cbe-overlays/jh_online_release_20260822/29梦境空间_03.sce`
   （787 字节、payload 767）。部署日志的实体计数说明其中包含 6 条新增战斗怪记录。
4. 本次 player-1 session（client `9215a111`）上报 `WT18/9` 为 `release=15/1`，服务端
   因而记录 `action=current`；同次启动没有任何 `WT18/8` 清单或 `WT18/7`
   `29梦境空间_03.sce` 下载。后续 `WT18/5` 虽记录 `action=update`，但也没有推进到清单
   请求。
5. 随后 NPC 副本入口正常完成 `30/1 -> WT2/3 -> WT6/1 + 30/2(no-posinfo)`；没有 SCE
   下载或安装回调。`mock_npc_instance_entry_target ... resource-proven-before-scene-enter`
   只证明服务端权威 SCE 满足配置，不能证明客户端已安装同一文件。

## 3. 客户端契约证据

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `send_version_update_request` / `0x0103B2D6` | WT18/9 上报已完成的 release/code。 |
| `江湖OL.CBE` | `startup_handle_update_target_metadata` / `0x0103B59A` | 收到 `type=1` 后才会请求 WT18/8 清单。 |
| `江湖OL.CBE` | `startup_handle_update_data_chunk` / `0x0103B860` | 读取 WT18/8 文件名清单并驱动缓存失效。 |
| `江湖OL.CBE` | `LoadSceneDataFromStream` / `0x01006204` | 从本地 SCE 创建场景实体；服务器不能以传送包替代 kind-3 节点。 |

本环境当前没有可调用的 IDA MCP；上述地址来自既有内容更新与场景战斗怪调查，且与本次
WT18 请求序列、实际缓存文件和部署产物交叉一致。

## 4. 首个偏离与根因

客户端内容版本台账宣称 `15/1` 已安装，而 player-1 实际仍持有发布前的 278 字节
`29梦境空间_03.sce`。因此服务端在 WT18/9 返回“当前”，客户端不请求 WT18/8，也不会
删除旧缓存；进副本时读取的必然是无 kind-3 的基础场景。

之后再次点击“发布”得到 `content_changed=0`，说明文件字节和 release 15 相同，故不会
产生新的 release 来纠正这个已脱节的客户端台账。这不是副本传送、NPC 包或战斗下行包的
问题，也不能用动态怪物包、强制重入或客户端状态写入绕过。

## 5. 后续验证 / 修复边界

- 需要先通过正常内容管理流程产生一个新的、实际变更的内容 release，使 player-1 的
  WT18/9 收到 `type=1`，再观察 `WT18/8` 清单和场景首次加载时的
  `WT18/7 29梦境空间_03.sce`。
- 完整退出并重启 player-1 后，只有其本地该文件不再是 278 字节、且客户端自然加载新的
  SCE 时，才应验证 type-2 场景节点与物理碰撞战斗。
- 重新发布同一字节只会保留 `content_changed=0`，不能修复台账/缓存脱节；若要实现“强制
  重新发布”能力，需单独以 WT18/9/18/8 客户端证据设计，不能在副本入口添加兜底。
