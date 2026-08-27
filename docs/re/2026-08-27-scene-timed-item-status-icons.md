# 场景左上角时效道具标识

## 现象与首次偏离

场景左上角此前无条件显示一个“修”标识。根因是所有角色进入场景和后续
`1/7/20` 状态查询均固定下发 `pcimg=0`。`江湖OL.CBE` 将该零值解释为显示
`ep_icon.gif`，它不是“没有时效道具”的默认值；因此没有效果时也留下了错误的固定图标。

同一时间，战斗心得（`828`）和大力丸／神力丸（`829/830`）的权威时效记录已经写入
`account_role_item_effects` 并参与经验／战斗计算，但场景状态包没有把这些记录投影到
客户端已有的图标状态字段。这是显示与权威状态第一次分叉的位置。

## 客户端证据

`江湖OL.CBE` 的完整角色状态解析（`1/1/6` 的 `0x01013398` 路径）读取如下字段；它们随后由 `scene_rebuild_status_meter_node` 的状态构建路径
（`0x0100E3E2`）决定左上角标识：

| 字段 | 值 | 原生图像 | 对应状态 |
| --- | --- | --- | --- |
| `pcimg` | `0` | `ep_icon.gif`（修） | 客户端把零值当作显示条件；无此状态时必须为 `1`，以隐藏固定图标。 |
| `ruffianflag` | `1` | `ruffian.gif`（力） | `1/22/3` 的 `net_handle_ruffianflag_info(0x01010F6C)` 已读取这个字段；用于有效的时效攻防道具。 |
| `expcard` | `1` | `training.gif`（练） | 已有经验卡状态。 |
| `expbook` | `1` | `muse.gif`（悟） | 战斗心得的原生心得标识。 |

资源编号也可由 CBE 的资源表及 `0x0100E3E2` 核对：`training.gif=0x35`、
`ep_icon.gif=0x36`、`ruffian.gif=0x37`、`muse.gif=0x5e`。这不是宿主绘制或客户端内存写入；
图标仍由固件根据正常响应自行创建。

`1/25/6` 的战斗心得使用回调（`0x01026574`）只读取 `result`、`maxnum` 与 `iteminfo`；
`1/25/7` 的确认回调（`0x0102DCDA`）只读取 `result` 与 `useinfo`。两者都没有读取
`expbook` 字段，因此不能承载“悟”的即时刷新。

更重要的是，先前对 `1/1/14` 的字段归属判断错误。`0x010132F8` 的 kind-1 循环会接受
subtype `2/3/6/14`，但 `expbook` 的唯一访问点是 subtype `6` 分支的
`0x010133CC`（字段字符串 `0x0101353C`）。`1/1/14` 会触发一次面板重建，却不会改写
缓存的 `expbook`，所以只能重建旧状态；这与“重登后才显示悟”完全一致。

曾经尝试将完整状态对象附在同一 `25/6` 回复的第二个对象中：

```text
1/25/6 {result=1,maxnum,iteminfo}
1/1/6 {result=0,revivetype,ruffianflag,type,practiseflag,pcimg,expcard,expbook,practiseinfo,lastexp,curexp,persentexp}
```

这里的 `result=0` 只用于避开 `result==1` 的角色选择／场景创建分支，且不携带
`actorinfo`。但玩家最新实测已否定“同一回复内多对象会按对象类型自动分发”的假设：服务端日志
已确认 `25/6` 成功回复了两对象（`response=265 status6=1`），随后 `25/7` 也完成，画面仍无
“悟”。因此 `1/1/6` 不是由该请求的专用回调交给通用状态解析器，不能作为即时刷新修复，且该
附加对象已从生产回包移除。

当前 `bin/main.exe` 增加了只读取证窗口。它会在每个 `1/25/6` 回包（同时记录是否确有附带
`1/1/6`）中，记录该回调内是否命中：`0x01026574`（25/6）、`0x010132F8`（通用 kind-1）、`0x01013398`
（subtype-6）、`0x010133CC`（`expbook`）和 `0x01013594`（状态栏重建）。日志写入
`logs/battle-insight-status-trace.log`；它不改写包、CBE 内存、寄存器、回调或事件顺序。已据此
移除无效附加对象；后续只能查找客户端实际登记的状态刷新请求／事件，不能伪造推送或替换回调。

大力丸及其他 `1/22/3` 时效攻防道具则会由该响应内已被客户端解析的
`ruffianflag` 立即更新“力”标识。

`25/7` 保持单一的数量窗口完成确认，不再附加无效的 `1/1/14`。对战斗心得而言，`25/6` 也仅
打开数量窗口；扣道具和启动效果属于成功的 `25/7`。

## 修改契约

1. 以 `account_role_item_effects` 的未过期记录作为唯一来源：经验卡、战斗心得和时效攻防效果
   分别投影到 `expcard`、`expbook` 和 `ruffianflag`。
2. 无论角色是否有时效道具，`pcimg` 一律发 `1`，使客户端隐藏旧的固定“修”图标。
3. `1/22/3` 每次响应都反映当前仍有效的时效攻防状态；一次被拒绝的重复使用不能清掉先前
   已生效的大力丸标识。
4. 商城等角色状态响应同样反映 `ruffianflag`，不能在打开／关闭界面时错误清掉场景中的“力”。
5. 不修改 CBE／CBM、客户端内存、寄存器、回调或宿主绘制；响应字节仍通过固件登记的普通网络
   回调交付。

## 回归

`timed-item-status-icon-regression` 覆盖无效果、经验卡、战斗心得、时效攻防效果及三者并存时
的字段投影，并验证完整 `1/1/6` 状态对象在无效果时使用 `pcimg=1`，避免固定“修”回归。
`timed-item-status-icon-regression` 额外验证完整角色状态的字段投影；它不声称这份状态可由
`25/6` 或 `25/7` 专用回调即时消费。`battle-insight-status-regression` 覆盖点击“悟”后的
`7/36.bookinfo` 说明响应。`battle-insight-followup-regression` 锁定 `25/7` 是单一
`result/useinfo` 完成对象，且仅接受数量一致的重传，避免重复消费或不相关的角色状态重放。

## 实时徽标协议收束（2026-08-27）

实际登录抓包表明，`1/7/7 {type=2}` 的正常回包是
`1/7/20 {result=1,pcimg}`；`江湖OL.CBE:0x01011C88` 的 subtype-20 解析器直接把
`pcimg` 写入 `global+31273`，而 `UpdateSceneMenuState(0x0100E3E2)` 会在该值为 `0` 时显示
`ep_icon.gif`（修）。原 `vm_net_mock_append_game_type_response_object()` 在这条真实请求上
硬编码 `pcimg=0`，与“无时效固定图标必须隐藏”的字段契约相反。现已改为回传共同状态投影的
`pcimg=1`；这仍是原请求对应的一次普通 event-7 回包，未涉及客户端内存、回调或事件顺序。

同时删除了 `mock_server_catalog.c` 中未被生产 dispatcher 调用的静态 `1/1/6` 伪状态构造器。
它曾被旧单测直接调用，但不能穿过 `25/6`／`25/7` 专用 callback 到达 fresh-enter parser，保留它会
误导后续实现以为可以通过拼包即时刷新“悟”。回归现改为构造真实的 `1/7/7 {type=2}` 与
`{type=3}` 请求，并断言相应回包严格为 `1/7/20 {result=1,pcimg=1}` 和
`1/7/32 {result=1,expcard=0}`。这既覆盖修／练的独立实时协议，也不把 `expbook` 塞入任何不读取
它的对象。

已执行 `make -j2`、`timed-item-status-icon-regression`、
`battle-insight-followup-regression` 与 `battle-insight-status-regression`，均通过。后两项继续
证明 828 的 `25/7` 只有 `result/useinfo` 完成契约、点击“悟”说明走独立的 `7/36.bookinfo`；
它们不构成即时 `expbook` 刷新的依据。
