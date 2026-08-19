# 宝箱开启奖励提示

Date: 2026-08-13

Status: native `1/7/15` reward/prompt response implemented; runtime acceptance
pending

> 2026-08-18 correction: sections 1-14 preserve the investigation history, but
> their `7/37` "timed/display-only" conclusion is superseded.  Runtime and IDA
> evidence proved it calls the same blocking message-box method before reading
> `result`.  Current consumption uses two sequence-keyed `7/11` objects and the
> reward text is queued as system chat after commit.  See
> [黄金宝箱开启进度状态](2026-08-18-chest-open-progress-state.md).

> 2026-08-19 correction: the former conclusion that `7/15` only exists as a
> request inspected `HandleItemOperationResponse(0x01033544)` but missed the
> backpack business handler `HandleShopBuyItem(0x01025AE6)`.  That handler has
> the firmware's native `1/7/15` chest-result branch and owns both reward
> insertion and the visible `获得%d个%s` prompt.  Section 16 is authoritative.

## 1. 当前目标

青铜、白银和黄金宝箱会正确扣除宝箱/钥匙并把中奖物品加入背包，但客户端没有
显示本次实际获得的物品。本轮只在开箱成功后补充面向开箱者的文本提示；不改变
奖池、扣除、背包增量或世界播报的职责。

## 2. 已有运行时与协议证据

`docs/re/2026-08-06-chest-reward-contract.md` 已固定原生请求为
`WT 7/15 { box, key }`，以及成功响应的物品对象顺序：

```text
1/7/1 acknowledgement
1/7/7 type=2 + 1/7/11  (宝箱)
1/7/7 type=2 + 1/7/11  (钥匙)
1/7/7 type=1           (奖励增量)
```

这里的 `1/7/7 type=1` 已由 `mmGame` 用于本地背包增量。`1/7/37 result=0` 虽带
物品获得文本，却也会走 `HandleItemAcquire` 插入路径；将它与上述增量行叠加已有重复
入包的反证，故不能使用 `result=0`。第 14 节记录了不入包的 `result=1` 分支。

## 3. IDA 证据

| binary | function/address | findings |
| --- | --- | --- |
| `mmGameMstarWqvga.cbm` | `sub_11CE` / `0x000011CE` | 当外层事件为 `7` 时按对象顺序遍历；`1/7/7` 转入 `sub_D04` 处理背包行。对 `1/16/2`，先清理等待状态，`result=4` 时读取 `hint` 字符串并调用既有文本提示 UI。 |

因此 `1/16/2 { result: 4, hint: GBK }` 是客户端已有、不会修改背包的文本提示
契约。它可作为上述物品对象之后的最后一个复合对象，由客户端在背包状态已更新后
显示“开启…，获得…”提示。

## 4. 根因与边界

当前 `vm_net_mock_build_chest_open_response()` 只输出物品状态对象。它已经持有经过
验证的宝箱 GBK 名称和奖励目录中的 GBK 物品名称，却没有把本次抽取结果送到任一
个人提示通道。

本轮不把提示塞进世界频道：世界频道是独立的持久化/轮询渠道，且仅适用于管理员
勾选的公告。也不改变成功物品对象的相对顺序。

## 5. 最小实现计划

1. 在 `mock_server_catalog.c` 格式化 GBK 文本：`开启<宝箱>，获得<物品>`；数量大于
   一时追加 `×<数量>`。
2. 在已完成的 `1/7/1`、两组扣除对象和奖励增量对象之后，追加一个
   `1/16/2 { result=4, hint }`，然后再封包。
3. 该对象构造失败时，在修改角色/持久化之前走现有 `1/16/2` 失败提示；保存失败仍
   覆盖为原有失败响应，绝不发送成功提示。
4. 以无副作用回归覆盖三种宝箱名、单件与多件数量的 GBK 文案。

## 6. 验证清单

- [x] 成功包保持原有六个物品对象及顺序，并在最后新增一个 `1/16/2` 文本对象。
- [x] 失败路径仍仅返回现有失败提示，不扣除物品。
- [x] 不含 `1/7/37`，不增加第二次物品入包。
- [ ] `make -j2` 完成链接。
- [x] 文案回归通过。

## 7. 实现与当前验证结果

- 曾尝试在原有六个物品对象之后附加 `1/16/2 { result=4, hint }`。运行时证明该对象
  并不由本次 `7/15` 回调中的 mmGame 提示分支消费，已在第 10 节撤回。
- 文案格式回归仅覆盖 GBK 字符串，不能证明 callback 所属关系，保留为辅助检查而非
  开箱成功证据。
- 当前 `make -j2` 已成功链接 `bin/jh-online-server.exe`。

## 8. 2026-08-17 运行时回归调查

黄金宝箱 `524` 的一次开箱在 `stream_read_i16_be_tagged(0x01033A42)`
中以空 blob 崩溃，回包末尾确有 `1/16/2` 对象。该地址同时被场景 `posinfo`
和物品扩展读取器使用，因而它只能证明 `16/2` 回调期间有缺失 blob，不能单独证明
`hint` 的嵌套字符串为空，也不能证明删除提示是修复。

此前该提示已经由真实客户端显示过；提示实现及文案回归因此保持恢复状态。传输层现在对
每个 `1/16/2` 回包只读记录 `result` 的三字节编码、`hint` 的外层/内层长度和字段顺序；
崩溃 hook 同时记录 LR 周围指令。下一次隔离复现必须以这些原始证据区分：

1. `result=4` 未命中并回退到缺失 `posinfo` 的场景分支；
2. `hint` 字段没有被对象解析器识别；
3. 前序 `7/7` 物品回包破坏了随后回调的对象/流状态。

在首次偏离被这组证据确认前，不再通过移除提示对象改变业务行为。

## 9. 动态模块取证修正（2026-08-17）

最新崩溃现场的调用方为 `0x0502F417`，对象也位于 `0x050020E8`；这说明本次
`mmGameMstarWqvga.cbm` 位于动态内存池，而不是此前观察器写死的
`0x0518xxxx` 装载地址。因此此前缺少 `mmgame_transfer_result` / `sub_bcc`
记录不能作为“客户端没有经过 `16/2`”的证据。

观察器现按 `mmGame:sub_11CE` 的本地偏移 `0x11CE` 和入口字节
`F0 B5 FF B0 FF B0 A9 B0` 验证候选代码基址，再观察本地 PC
`0x1250`（`16/3 result`）、`0x138E`（`16/2 result`）、`0x13FE`
（调用 `sub_BCC`）和 `0x0BCC`（进入 `sub_BCC`）。它仅读取寄存器和
客户内存，并在日志中输出动态 `base` 与 `local`；不会改动 CBE/CBM、响应字节、
事件、输入或客户端状态。`make -j2` 已于本轮通过。

下一次同一黄金宝箱复现的最小证据集必须包含：

1. `mock_wt_16_2_audit`，证明服务端发送的 `result=4` 与非空 `hint`；
2. `mmgame_transfer_result ... local=138e`，证明客户端 getter 实际返回值；
3. 有无 `mmgame_transfer_sub_bcc ... local=13fe/0bcc`；
4. 若仍崩溃，匹配同次 `stream_read_i16_null_blob` 的 `lr`、`reader_head`。

若第 2 项返回非 `4`，继续追溯对象字段/生命周期；若为 `4` 而仍进入第 3 项，
再检查对象顺序或前序回调状态。两种情况都不得以伪造 `posinfo` 或删除提示对象处理。

最新一次崩溃仍只产生 `stream_read_i16_null_blob`，没有已验证的 `sub_11CE` 分支记录；
因此不能把调用方预设为 mmGame。首次空 blob 现在额外执行一次只读内存池扫描，要求同一
候选同时匹配 `sub_11CE`、`16/2 result`、`sub_BCC` 调用和 `sub_BCC` 入口四个机器码
指纹，并输出 `mmgame_transfer_image crash_scan`。该扫描不写客户内存，目的仅是获取本次
实际装载的四个动态地址，排除调用模块误归属后再继续追踪字段读取。

## 10. 根因与修复（2026-08-17）

本次原始包审计证明开箱成功包中的 `1/16/2` 字节本身完整：`result` 为
`00-01-04`，`hint` 外层/内层长度均非零。但崩溃现场的 `r5` 已是这个
`16/2` 对象，而 `r9=01050BD0` 为主 CBE data base；同时没有命中经过四重
指纹验证的 mmGame `sub_11CE`。对象因此由 `7/15` 开箱回调的主 CBE 分支处理，
不是由 `result=4` 文本提示分支处理。该分支把非本契约对象回退为场景结果并读取
缺失的 `posinfo`，最终在 `stream_read_i16_be_tagged(0x01033A42)` 传入空 blob。

修复删除 `vm_net_mock_build_chest_open_response()` 中对复合成功包追加
`1/16/2` 的行为，保留两组 `7/7 type=2 + 7/11` 和一组 `7/7 type=1`。随后才单独
调查可承载奖励文案的通道。这修复的是开箱回包所属回调的协议契约，而不是给崩溃地址加兜底；
该个人提示通道仅保留给已验证的独立物品使用失败/提示响应，不能再复用于 `7/15`。

仍待验证：黄金宝箱复测必须收到不含 `16/2` 的成功包，客户端完成三项背包更新且不再
出现 `read_i16_null_blob`。奖励公告如需恢复，必须先获得该开箱 callback 可安全消费的
独立事件契约，不能把 `16/2` 再拼回当前回包。

## 11. 20/1 假设已撤回（2026-08-17）

此前将 `1/20/1` 识别为开箱提示通道的结论不完整，已由背包实测撤回。它确实由主 CBE
分发器消费，但没有适用于开箱后的通用、安全关闭生命周期；完整结论见第 12 节。

## 12. 已否定的 `1/18/1` 中间方案（2026-08-17）

黄金宝箱实测先后否定了 `1/20/1` 的两个分支：`result=0` 创建没有输入回调的文本框，
不会自动消失；改成 `result=1` 虽会安装回调，但
`net_handle_simple_result_info(0x01011434)` 固定传入
`HandleSceneBackKey` 和 `HandleSceneTouchRegion`。在背包上下文点击确定会进入
`HandleSceneBackKey(0x0101140C)` 的场景收尾路径，实际表现为客户端退出。

因此 `20/1` 不是开箱成功后可复用的个人提示契约，不能仅通过切换 `result` 修复。服务端
移除了开箱成功包中的 `20/1` 对象，并将会创建成功模态框的 `7/1` 一并省略；随后曾将
奖励文案尝试改走固件的 `1/18/1` 定时消息队列：

```text
1/7/7 type=2 + 1/7/11   (宝箱)
1/7/7 type=2 + 1/7/11   (钥匙)
1/7/7 type=1            (奖励)
1/18/1 msg=<GBK 开箱奖励文案>
```

此前回包中的 `1/7/1` 曾被当作开箱请求自己的原生成功确认，但
`HandleItemOperationResponse(0x01033544)` 会在 `result=1` 上无条件调用
`ui_show_message_box("使用成功", 0, 0, 10)`；该消息框没有定时关闭。当前开箱回包省略
`1/7/1`，由后续每组 `7/11` 数量流清理 pending 操作，从而保留背包状态更新而不创建
隐藏模态框。`JianghuOL.CBE:net_business_response_dispatch`
(`0x01012F8A`) 将 `1/18/1` 路由到 `net_handle_msg_popup(0x01010D54)`；该函数只读取
长度定界的 `msg`，提交到已有的 30 帧定时消息队列，并清除网络回调状态，不创建
`ui_show_message_box` 或绑定 `HandleSceneBackKey`。奖励实体仍由 `7/7 type=1` 写入背包。

该对象不含 `25/11`、`20/1` 或 `7/1`，并且不会造成 `read_i16_null_blob`、模态确认框
或点击提示后退出；但第 14 节的背包实测已证明它不可见，不能作为最终方案。

### 构造回归

当时的 `scripts/chest-open-reward-notice-regression.c` 不启动监听器、不连接 MySQL，也不读取
或修改任何角色状态。它当时通过 `1/18/1` 构造器验证 GBK 文本；该断言已由第 14 节的
`1/7/37 result=1` 构造回归取代：

- `msg` 是长度定界、无尾零的 GBK “开启黄金宝箱，获得修炼天书”；
- `result` 为 `00-01-01`，使 `HandleItemAcquire` 不进入插入分支。

2026-08-17 已按脚本注释中的 MinGW 命令重新编译，并从 `bin/` 工作目录运行（提供 SDL/Unicorn
运行时 DLL）。输出为 `chest-open reward-notice regression passed: 1/7/37 display-only acquire notice` 与
`chest-open response regression passed: no 7/1 or 25/11 modal/state; 7/7+7/11 cleanup`。

## 13. 开箱提示通道的首次偏离（2026-08-17）

用户复测提示不可见且背包仍需额外点击一次才能操作。首个偏离不是背包扣除对象，而是
旧成功回包开头的 `1/7/1 result=1`：

- `HandleItemOperationResponse(0x01033544)` 在读取 `result/type/id` 后先清除 pending
  标记，再调用 `ui_show_message_box(0x010338A8, 0, 0, 10)`；`0x010338A8` 是固件
  的“使用成功”文本。
- `ui_show_message_box(0x010103F4)` 把消息框活动标志置为 1，并保存第四参数 10；
  没有倒计时清除分支。旧 `25/11` 只更新场景中央信息横幅；背包界面没有对应绘制路径，
  却仍置为活动状态，所以它表现为不可见的输入拦截层。
- 同一 `HandleItemOperationResponse` 的 `7/11` 分支在读取 `info` 数量流后把待处理
  指针清零。固件已有的小喇叭无成功框路径也省略 `7/1`，只发送 `7/7 + 7/11`。

因此当时宝箱成功响应曾改为：

```text
1/7/7 type=2 + 1/7/11   (宝箱)
1/7/7 type=2 + 1/7/11   (钥匙)
1/7/7 type=1            (奖励)
1/18/1 msg=<GBK 开箱奖励文案>
```

服务端仍在同一 projected role 事务中扣除两件物品并加入奖励；只移除会产生副作用的
成功框确认，不丢弃 `7/11`，也不自动注入输入事件。该中间回归曾断言对象顺序为
`7/7,7/11,7/7,7/11,7/7,18/1`；该最后对象已由第 14 节替换。

## 14. `1/18/1` 不可见后的候选（已否定，2026-08-17）

> 本节记录当时的候选推断。2026-08-18 的反编译与复测证明 `7/37` 会调用
> `ui_show_message_box`，并非定时、非模态提示；最终结论见第 15 节。

用户在背包内复测证明 `1/18/1 { msg }` 没有可见文本，同时不再有输入拦截层。它的
`net_handle_msg_popup(0x01010D54)` 虽然是非模态、30 帧的长度定界文本通道，但在背包
screen 没有可见绘制证据，故不能继续作为开箱提示方案。

主 CBE 中已经存在物品获得专用的 `1/7/37` 通道：

```text
JianghuOL.CBE:net_handle_misc_player_fields(0x01011D16)
  -> HandleItemAcquire(0x0101191A)
  -> 先显示 msg（20 帧），后读取 result
  -> 仅 result=0 时读取 itemid/seq/itemname 并插入物品
```

开箱奖励已由 `1/7/7 type=1` 完成一次本地背包增量，因此末尾对象改为：

```text
1/7/37 { msg: <长度定界 GBK 开箱奖励文案>, result: 1 }
```

`result=1` 使 `HandleItemAcquire` 在显示 `msg` 后立即返回，不读取也不需要
`itemid`、`seq`、`itemname`，不会第二次插入奖励；它不调用 `ui_show_message_box`，也
不绑定 `HandleSceneBackKey`。完整成功对象顺序为：

```text
1/7/7 type=2 + 1/7/11  (宝箱)
1/7/7 type=2 + 1/7/11  (钥匙)
1/7/7 type=1           (奖励)
1/7/37 result=1         (仅显示本次奖励)
```

本次仍需背包实测确认文本可见并自然消失。构造回归锁定 `msg` 的 GBK 内层长度和
`result=00-01-01`，并断言没有 `1/7/1`、`1/20/1`、`1/25/11` 或 `1/18/1`。若该通道
在背包同样不可见，必须记录实际 render 生命周期；不得把 `result` 改为 0 或叠加
`7/37`，否则会违背“单次奖励增量”的背包契约。

## 15. `7/4` 复测与 `7/37` 最终根因（2026-08-18）

后续复现明确收到 `7/4 + 7/11 + 7/11 + 7/7 + 7/37`，界面仍被阻塞。这证明
`7/4` 已经完成物品操作等待，但末尾对象又建立了新的 UI 状态。

`HandleItemAcquire(0x0101191A)` 在检查 `result` 之前无条件调用 manager 方法
`+140`；该方法就是 `ui_show_message_box(0x010103F4)`，第四参数 `20` 是模式而非
倒计时。`result=1` 只跳过奖励插入，无法避免阻塞框。因此 `7/37` 已从开箱成功包移除。

最终成功包只有 `7/4, 7/11, 7/11, 7/7`。奖励 GBK 文案在角色保存成功后进入当前
session 的系统聊天队列，通过正常场景同步轮询下发为 `1/3/3 type=system`；客户端
`net_handle_type_payload_detail(0x010126C6)` 将其加入聊天列表，不创建消息框。完整证据、
失败边界和回归结果见
[黄金宝箱开启进度状态](2026-08-18-chest-open-progress-state.md)。

## 16. 原生 `1/7/15` 奖励提示契约（2026-08-19）

用户复测证明系统聊天对象虽已入队，却不会在当前背包界面即时显示奖励文字。第一次
偏离是服务端将开箱奖励拆成 mmGame `7/7` 增量和延后的聊天消息，绕开了固件已经存在
的宝箱专用成功响应。

`JianghuOL.CBE:HandleShopBuyItem(0x01025AE6)` 在 kind `7`、subtype `15` 时读取：

```text
result:u8
total:u8
iteminfo:blob
```

`result=1` 时，`0x010261F4-0x010262C4` 按 `total` 解析每行：

```text
itemId:u32, seq:i16, count:u32, common item/equipment extra
```

该 blob 不含 mmGame `7/7` 使用的前置行数。每行由
`MoveBattleActorStep(0x0101918E)` 送入 `TimerControl_ProcessItem(0x01032EB8)`；
最后一行成功后，`0x010262DA-0x010262EC` 使用固件内置模板
`0x01026620 "获得%d个%s"` 调用正常消息框。这里的 `count` 是本次获得的增量，既用于
背包合并，也用于提示数量。

修复后的成功对象顺序是：

```text
1/7/4   result=1              结束物品操作等待
1/7/11  chest seq/count       原地同步或删除宝箱行
1/7/11  key seq/count         原地同步或删除钥匙行
1/7/15  result=1,total=1      原生奖励增量并显示“获得N个物品”
```

`7/15` 替换 `7/7 type=1 + 7/7 type=3`，而不是与它们叠加，因此奖励只进入客户端
一次。mmGame 的 `type=1` 已移除，也就不会重新置位其等待状态，不再需要 `type=3`
终结对象。提交后的系统聊天奖励消息同步移除；世界公告仍是独立、仅按后台配置启用的
通道。宝箱和钥匙继续只用 `7/11`，不会重新引入 category 15 物理槽泄漏。

定向回归 `scripts/chest-open-reward-notice-regression.c` 已验证四对象顺序、字段类型、
`total=1`，以及无前置行数的 `itemId -> seq -> count -> common extra` 字节顺序。
`make -j2` 和回归均通过。真实客户端仍需确认提示可见、确认键只关闭游戏提示、奖励与
消耗各发生一次，以及连续开箱不再出现等待层。
