# 已缓存游戏数据的原生更新（WT 18/9 → 18/8 → 18/7）

## 问题与范围

场景战斗怪部署会改写服务端权威的 SCE 文件。例如
00蓬莱仙岛_02.sce 增加小猴子战斗节点后，旧客户端缓存中仍可能保留未包含
该节点的同名 SCE。此前具名资源发布只在客户端因缺失资源发起 WT 18/7 时
下发字节，无法覆盖已经存在的缓存文件。

本记录适用于所有非 CBM 的已缓存游戏数据；SCE 只是场景战斗怪部署的首个使用场景。
不清理客户端目录、不修改客户端内存，也
不以场景重入伪造更新完成。

## 已确认的客户端链路

1. 江湖OL.CBE 的 send_version_update_request（0x0103B2D6）发出 WT 18/9。
2. startup_handle_update_target_metadata（0x0103B59A）读取 type、id、code。
   type=1 会进入 startup_screen_select_phase_and_continue（0x0103B4AA），继而
   由 startup_screen_request_next_update_chunk（0x0103B45A）请求 WT 18/8。
3. startup_handle_update_data_chunk（0x0103B860）读取 totalsize、totalnum、
   version、crc、data，并对 data 作有符号字节累加校验。
4. startup_screen_extract_one_game_data_entry_to_temp（0x0103A660）将 data
   解为重复的 u8 文件名长度 + 文件名。
5. startup_screen_commit_temp_data_file_into_game_data（0x0103A55C）对每个
   名称删除 JHOnlineData 中对应的缓存文件。此阶段不是 SCE 字节传输。
6. 后续正常资源加载发现文件缺失，沿既有 WT 18/7 路径请求同名资源并安装。

客户端把已完成版本作为 WT 18/9 请求的 `version/codeVersion` 上报。收到 `type=1`
就会走上述更新/续传分支，因此服务端必须在上报 pair 与当前 release 的 `id/code` 完全
一致时返回 `type=0`。需要更新时，WT 18/9 的 id 必须等于 WT 18/8 的 version，code
必须等于所有 18/8 data 分片的最终有符号字节累加和。

## 服务端契约

游戏数据内容发布状态由 MySQL 管理：

- `server_content_update_releases` 的 `config_id=1` 保存 enabled、
  `release_id` 与有符号字节校验和；
- `server_content_update_files` 以 `sort_order` 保存构成 WT 18/8 data 的
  有序非 CBM 资源名称（`VARBINARY`，保留 GBK 协议字节）。

游戏数据通过场景战斗怪部署或更新管理页加入内容版本时：

1. 服务端资源根目录保留同名的权威非 CBM 资源，客户端缺失时会以正常 WT 18/7 请求它。
2. 将该资源纳入累积的内容失效清单；仅当资源首次纳入或发布字节发生变化时，递增
   release_id，并重新计算校验和。
3. 客户端下次完整启动时，18/9 返回 type=1 和该 release 的 id/code。
4. 客户端以 18/8 接收文件名清单，删除缓存，随后通过 18/7 重取资源。

清单是累积的，因为客户端只持久保存一个 id/code 对，可能跳过中间多个发布；同一
id/code 对由服务端严格识别为已安装，并以 `type=0` 结束启动更新协商，不请求 18/8
也不删除资源。重复提交同一文件的相同字节不会产生新版本；缺失文件只会在下一次加载时
下载一次。

首次升级时，服务端仅在 `server_data_migrations` 中不存在
`content-update-mysql-v1` 标记时读取一次旧的 `server_content_update.tsv`，并在
MySQL 事务提交后写入标记。之后不会再读取或写入该 CSV；旧内容校验失败时明确拒绝
迁移，而不会凭默认场景重建发布状态。

## 代码位置

- src/server/mock_server_core.c
  - 内容清单的 MySQL 加载、校验、事务持久化与旧 TSV 的一次性迁移。
  - WT 18/9 的 type/id/code 元数据和 WT 18/8 分片构造。
- src/server/mock_server_dispatch.c
  - WT 18/8 检测器放在通用 18/6、18/7 更新处理之前。
- src/web_admin_server.c
  - 可多选服务器非 CBM 游戏数据加入、移除、重新发布或清空内容版本；不再提供具名资源发布。

## 回归夹具

scripts/content-update-manifest-regression.c 不连接数据库、不启动服务端，也不
修改资源目录。它以 release id 77 和文件 00fixture.sce 构造请求，断言：

1. 新客户端的 18/9 响应为 type=1、id=77、code 为清单的有符号字节校验和；携带
   相同 version/codeVersion 的客户端则收到 type=0。
2. 18/8 响应的 totalsize、totalnum=1、version=77、crc 和 data 全部匹配。
3. data 的字节格式严格为 u8 长度再接文件名。

运行命令：

    gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/content-update-manifest-regression.c src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o obj/server/content-update-manifest-regression.exe -Wl,--gc-sections -lpthread -liconv -lm -lkernel32 -lws2_32
    .\obj\server\content-update-manifest-regression.exe

已确认夹具输出 content update manifest regression passed。

## 人工客户端验收边界

重新构建并重启服务端后，完整退出并重新启动客户端，再进入包含已发布场景
SCE 的地图。服务端应依次记录：

    mock_update_version ... content=<id>/<code>/<count>
    mock_content_update_chunk ...
    mock_update_chunk ... subtype=7 file=00蓬莱仙岛_02.sce ...

最后一条发生在客户端删除缓存、实际重新加载该地图之后。此时场景战斗怪节点和
小猴子 Actor 应来自新版 SCE 的正常解析结果。
