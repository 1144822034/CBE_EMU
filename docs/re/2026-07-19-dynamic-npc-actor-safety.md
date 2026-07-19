# Web 动态 NPC Actor 资源安全边界

## 现象

在 Web 后台向 `c00蓬莱仙岛_01` 增加“新手礼包”，选择
`n_man1.actor` 和 `00开门红包.xse` 后，客户端进入场景收到 `resp=721`，随后：

```text
地址无法访问:14a type:0 size:20 value:1
r0:14a r1:1e
lr:1044e4d pc:104d2e4
```

服务端运行证据显示新增行已进入启动场景的 `27/11`：`npcnum=2`、
`npcinfo_len=177`、`source=sce+service-dynamic`。

## IDA 证据

- 按 `binary_name=江湖OL.CBE` 选择 IDA 实例。
- `0x0104D2E4` 是 `mem_zero_small` 的首字节写入。
- 调用者 `scene_actor_asset_slot_table_load_entry/0x01044DA0` 在
  `0x01044E48` 清空一条 30 字节资源名记录：

```c
mem_zero_small(net_manager->assetNameRows + 30 * table->count, 30);
```

- `n_man1.actor` 只存在于服务端下载源 `web/fs/JHOnlineData`，不在客户端基础包
  `bin/JHOnlineData`。`LoadAssetFromFile` 因此进入缺资源分支。
- 场景启动的 `27/11` 解析时，客户端资源请求表尚未分配，基址为 0；当
  `table->count == 11` 时目标地址成为 `30 * 11 == 0x14A`，与崩溃完全一致。

## 最终约束

XSE 由 mock 服务解释，因此脚本只要求存在于服务端资源源。Actor 模型由 CBE
场景创建器立即载入，但“权威资源存在”和“桌面客户端已缓存”是两种不同状态，
不能再把后者当成 Actor 是否存在的判断条件。

- Web Actor 下拉框列出服务端权威资源目录中的 `.actor`，不再要求它预先存在于
  `bin/JHOnlineData`。
- 保存或恢复 NPC 时，后台解压并解析 Actor，校验资源类型、引用的 GIF 名称，
  并逐一解码所有引用图片；缺失、损坏或依赖不完整的资源仍会被拒绝。
- 校验通过后，后台把 Actor 和引用 GIF 作为同一批具名资源发布到更新目录，再
  启用 NPC；后台不接触客户端缓存，也不复制客户端文件。
- 服务启动加载 MySQL 动态 NPC 时会为旧记录补齐同样的依赖发布；同一 Actor 的
  内容哈希版本已匹配时不会重复改写目录，损坏记录则只在运行时隔离。
- 初始 `27/11` 直接发送完整 NPC 目录。模拟器文件层在 CBE 收到“文件不存在”
  之前拦截安全的 `.actor/.gif` 缺失项，以当前客户端身份发送 WT `18/7`；服务端
  只响应已发布名称，按 `0x1000` 分块返回，客户端逐块校验累计有符号字节和，写入
  同目录临时文件，完成后原子改名并重试原始打开。
- 不再延迟重放一份 `27/11`。`InitUpdateCheckCtx` 只在启动更新生命周期分配
  `assetNameRows`；场景就绪或等待若干 tick 都不会补做该分配，延迟包仍会写空指针。
- 数据库中已有但不兼容的记录会保留供管理员修复，但运行时强制停用，不进入
  `27/11`，`dynamic_npc_quarantine` 仍作为启动期的最后一道防线。
- 启动 NPC 资源自检允许管理员追加合法动态 NPC，不再要求生产场景的 NPC 数量
  必须恰好等于内置数量。

现有“新手礼包”记录通过真实 Web 接口改为该场景已验证的
`n_warriormaster.actor`，保留 `00开门红包.xse`。

## 初始安全修复验证

1. 保留旧 `n_man1.actor` 数据启动服务：记录被隔离，场景响应不含该字符串；
2. Web 下拉框不再包含可选择的 `n_man1.actor`，伪造提交返回资源未安装错误；
3. 通过 Web 保存安全模型后，场景响应为两行 NPC，`npcinfo_len=195`，且不含
   `n_man1.actor`；
4. 真实 CBE 自动登录收到场景响应后持续运行 45 秒，无地址异常、算术异常或断言；
5. 客户端画面正常显示“新手礼包”，并弹出“和新手礼包对话？”确认框。

客户端验证截图：
`bin/multiplayer-data/player-1/autotest/screens/000009_00045045.bmp`。

最终重启日志：`tmp/mock-service-19090.20260719-113739.stdout.log`；动态 NPC
加载结果为 `rows=2 skipped=0 quarantined=0`，stderr 长度为 0。

## Actor 误判修复验证

1. `web/fs/JHOnlineData` 中共有 147 个 Actor，旧校验因只检查
   `bin/JHOnlineData` 而仅允许其中 50 个；
2. 独立 Actor 解析器确认其中 145 个真实资源结构完整；两个
   `mock_missing_motion.actor` 测试伪资源仍被保存接口拒绝；
3. 新后台页面显示完整 147 个候选项，原先被排除的 `n_man1.actor` 已能选择，
   `/actor-preview.svg?actor=n_man1.actor` 返回可解析 SVG；
4. 通过真实 Web 保存接口使用 `n_man1.actor` 创建临时 NPC，后台发布
   `n_man1.actor` 与依赖 `n_man1.gif`，但客户端缓存保持不变；
5. 另用带有客户端可接受哨兵矩形的 `e_deity.actor` 验证不会把合法特殊模型
   误判为损坏；
6. `make -j2` 通过，新服务监听 `127.0.0.1:19090/19091`，stderr 为空。

## WT 18/7 缺失下载验证

1. 在客户端缓存没有 `e_deity.actor/.gif` 时，通过真实后台保存临时动态 NPC；
   `server_update_catalog.tsv` 同时出现 Actor 与 GIF 的内容哈希版本。
2. 客户端收到初始 `27/11` 后，文件层依次记录
   `resource_cache_miss_download_begin`、`resource_cache_download_chunk` 和
   `resource_cache_download_complete`。
3. 服务端收到两条真实 WT `18/7`：Actor 为 456 字节、版本 44250，GIF 为
   3696 字节、版本 44925。下载缓存和权威源的 SHA-256 分别完全一致。
4. 客户端在场景中持续到 50 秒后正常退出，stderr 为空，未再出现 `0x14A`、
   `0x168` 地址异常或断言；截图中能看到临时 NPC。
5. 保留缓存再次登录 45 秒，客户端没有新的资源下载日志，服务端也没有重复的
   `e_deity` WT `18/7` 请求，证明缓存命中不会重复传输。

本次网络下载日志为
`tmp/client-actor-network.20260719-150655.stdout.log` 和
`tmp/mock-service-actor-network.20260719-150622.stdout.log`；验证截图为
`bin/autotest/screens/000009_00045087.bmp`。
