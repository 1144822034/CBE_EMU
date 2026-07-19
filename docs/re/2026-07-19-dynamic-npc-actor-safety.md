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

## 修正后的约束

XSE 由 mock 服务解释，因此脚本只要求存在于服务端资源源。Actor 模型由 CBE
场景创建器立即载入，因此动态 NPC 只能选择已经安装于客户端基础包的 `.actor`。

- Web Actor 下拉框只列出客户端基础包中可用的模型。
- Web 保存接口再次执行同一校验，不能用伪造表单绕过。
- 数据库中已有但不兼容的记录会保留供管理员修复，但运行时强制停用，不进入
  `27/11`，日志源为 `dynamic_npc_quarantine`。
- 启动 NPC 资源自检允许管理员追加合法动态 NPC，不再要求生产场景的 NPC 数量
  必须恰好等于内置数量。

现有“新手礼包”记录通过真实 Web 接口改为该场景已验证的
`n_warriormaster.actor`，保留 `00开门红包.xse`。

## 验证

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
