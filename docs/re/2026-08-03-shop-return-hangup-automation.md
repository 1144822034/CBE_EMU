# 商城返回后挂机：隔离自动化场景

日期：2026-08-03  
场景 ID：`shop-return-hangup-v1`  
状态：已实现隔离的端到端自动化取证入口；标题登录与初始场景已自动验证。用户已明确
商城的真实 UI 契约是场景右上角**最右侧**小图标，完整回归使用该单一硬件事件继续验证
商城返回与挂机契约。

## 目标与失败定义

重放并验证以下真实客户端路径：进入场景 → 原生商城入口 → 原生返回 → 场景“挂机”
控件。通过不等于“不闪退”。只有同时观察到以下契约才通过：

1. 商城模块的真实 `14/14` 与 `14/4` 下行对象；
2. 商城返回后的 `30/2` 场景完成对象；
3. 挂机下行 `4/5`；
4. 已装载 `mmBattleMstarWqvga.cbm` 的真实 `HandleBattleStartMsg`
   本地 PC `0x66CC`，并继续进入 BattleScene 的首帧绘制。

缺少任一对象、解析边界或处理器命中都记录为失败；不会以自动重试、伪造完成或直接
调用游戏回调掩盖。

## 输入与触发

最大步骤数为 10；总超时 180 秒；单步超时 15 秒（标题启动最多 60 秒）；每个输入仅触发一次。唯一固定时长是
按下到释放的 80ms，用于模拟已有硬件事件队列所需的按键/触摸节奏，不能用于判断任何
业务完成。

| 步骤 | 只读触发条件 | 正常事件队列输入 | 下一验证条件 |
| --- | --- | --- | --- |
| 标题启动 | 原生版本/资源加载 | 定时一次 `F, F, Q, F, F, F` | 真实登录、角色和场景请求链 |
| 账号身份 | 隔离 `LoginRecord` 已由 `Storage_Date` 格式写入 | 无 | 既有账号登录而非自动新建游客 |
| 初始场景 | 真实 `25/5` 场景子集且渲染两帧 | 无 | 场景稳定、导出 LCD 帧 |
| 商城入口 | 真实 `25/5` 场景子集且渲染两帧 | 右上最右侧小图标中心 `(224,44)` 的一次触摸 | 商城真实 `14/14` 与 `14/4` |
| 商城返回 | 两个商城状态对象均已到达 | `E` | `30/2+posinfo`、原 scene 的新 `ScreenInit`、后续 `25/5` |
| 挂机 | 上述原生返回边界均完成 | 场景坐标 `(50,350)` 的一次触摸 | `4/5` + `mmBattle:0x66CC` + BattleScene 首帧 |

首次输入探针 `(102,44)` 已被否定：它打开的是“装备”界面，服务端没有收到 `14/*`
或传送石请求。该输入已移除。后续测试使用用户指定并可由画面验证的右上最右侧商城
图标 `(224,44)`；不得将它扩展为循环点击、候选坐标扫描或直接构造商城/传送石请求。

定时标题输入只负责驱动原生硬件事件队列，不能证明登录或场景成功；成功仍以真实的
`25/5`、商城对象、`30/2` 和 `4/5/mmBattle` parser 证据为准。`mmBattle` 的本地 PC 由
已安装业务 callback 减去其静态 `0x17AC` 偏移计算；不会硬编码运行时内存池地址，也不会
写 PC、LR、寄存器或客户内存。

## 观察与图像证据

在 `vm_lcd_update_with_input_overlay()` 调用 `UpdateLcd()` 后，自动化仅复制
`Lcd_Cache_Buffer` 的 240×400 RGB565 像素，编码为无损 PNG。每张图旁有 JSON，包含场景、
运行帧、分辨率、像素格式、触发条件、活动 screen、模块基址、本地 PC 和 Unix 时间戳。
它不读取 SDL 窗口、桌面或剪贴板。

目标下行包在 host 传输层复制至 guest 前被只读扫描；扫描只记录 WT 对象种类/子类型，
不改字节、callback、事件顺序或队列。`0x66CC` 的观察位于已存在的动态模块只读探针中。

## 隔离运行器

运行：

```powershell
$env:CBE_AUTOMATION_MYSQL_PASSWORD = '<本机 MySQL 测试口令>'
make -j2
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-shop-return-hangup-automation.ps1
```

运行器固定声明并预检 `127.0.0.1:19190`；已占用即退出。它为每次运行创建唯一的：

- `artifacts/automation/shop-return-hangup-v1-.../`；
- `jh_online_autotest_<随机十六进制>` 数据库；
- 服务端进程、客户端工作目录和 PID 清单。

数据库夹具拒绝任何非 `jh_online_autotest_<hex>` 数据库名，绝不写入 `jh_online`。服务端
使用该库和 `web/fs/JHOnlineData`，客户端在运行目录中使用只读资源 junction；用户现有
服务、端口、日志、账号和角色均不在作用域内。运行器只停止自身记录的两个 PID，并在
默认情况下删除自己创建的测试库（`-KeepDatabase` 仅供调查时保留）。

完整场景使用隔离库中的 `guest00001` / 角色 `810001`，位置为
`01桃花岛_01.sce (146,349)`；这正是允许挂机战斗构建器从 SCE2 找到明确战斗点的普通
场景。夹具另有 `teleport-stone-c00` profile，严格使用资源键
`c00蓬莱仙岛_03.sce (157,47)`（不与 `00蓬莱仙岛_03.sce` 或
`00_蓬莱仙岛03.sce` 混同），供 `scene-teleport-stone-probe-v1` 只读验证该场景的真实
载入与内部 LCD 画面。夹具在隔离服务启动前插入角色，避免复制用户数据库或将用户状态
作为前置。

夹具还会在隔离客户端工作目录写入
`nvram/CBE_______OL.CBE_storage_mmorpg_LoginRecord.bin`。该文件是经一次真实客户端游客
登录取证的 180 字节 `Storage_Date("mmorpg_LoginRecord")` 持久化格式：账号与密码字段分别
为偏移 `+16`、`+48`。它只建立测试的“已有账号”初始条件，使标题按原生协议登录
`guest00001`；不会修改客户内存、CBE/CBM、寄存器、PC/LR 或任何网络请求/响应。
`fixture.log` 记录此操作，`result.json` 分别记录场景内部输入和定时输入计数。

## 与根因调查的关系

该场景专门区分：响应已进入主 CBE parser、商城返回后的 mmBattle 委托已安装、以及
`4/5` 是否真的抵达 `HandleBattleStartMsg`。服务端后台自动回合或奖励日志不构成客户端
进入战斗的证据。若场景在商城入口阶段失败，应先比较该一次点击产生的原始请求、
服务端 detector 与 `14/*` 响应结构；不能将失败归因到挂机 `4/5` builder，或通过补发
响应绕过商城 parser。

## 首次运行

运行 `shop-return-hangup-v1-20260803T023931087Z-39704` 时，定时标题事件实际在
`5007/17015/19004/23013/29006/35007 ms` 投递，且服务器记录了真实的 `1/12` 处理；但隔离
客户端没有已有 `LoginRecord`，因此服务端按正确的游客分支签发了 `guest00002`。该账号不
是夹具中的测试角色，未出现 `25/5`，场景在标题启动超时。该失败定位为测试前置条件缺失，
不是挂机 `4/5` builder 或商城返回逻辑的证据；上述持久化夹具正是对此最早偏离点的修复。

第二次运行 `shop-return-hangup-v1-20260803T024924054Z-13708` 使用了隔离
`LoginRecord`，服务端确认登录的是 `guest00001`，客户端也收到了真实 `25/5` 并进入
`mmGame` 场景。随后原入口探针触摸 `(102,44)`；`frames/002_scene-ready.png` 显示为
“装备”界面，服务端日志中不存在 `mock_shop_open14`、`mock_shop_scene_interaction_combo`
或 `16/1`。这把首次偏离精确定位为**错误的自动化输入契约**，不是商城返回 `30/2` 或
挂机 `4/5`。该运行的 `result.json` 为 `failed/stage-timeout`、`input_count=1`、
`timed_input_count=4`；结果只用于否定该坐标，不作为服务端缺陷结论。

为继续取证，新增 `scene-teleport-stone-probe-v1`。运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-shop-return-hangup-automation.ps1 -Scenario scene-teleport-stone-probe-v1
```

它只用标题阶段的正常硬件事件队列进入精确的 `c00蓬莱仙岛_03.sce`，在真实 `25/5` 和渲染
边界后导出 `teleport-stone-scene-ready` PNG 并结束。通过表示初始场景契约已被验证，**不**
表示已打开传送石、商城或挂机；这三个契约仍由后续单一、已证实的 n_telestone 输入来覆盖。

## 商城返回后的挂机：根因与回归

首次完整商城回归为
`shop-return-hangup-v1-20260803T032346813Z-39812`。它已经由右上最右图标得到真实商城
`14/14 + 14/4`，`E` 后服务端也正确下发了无 `posinfo` 的 `30/2`。但自动化在**传输层**观察到
该 `30/2` 的同一循环就投递 `(50,350)`：日志中该触摸在 frame 256 发出，而下一帧的截图才显示
场景 screen 已从商城 `010536b4` 切回 `01053f78`。服务端没有收到 `4/5`。因此首个错误状态是：
触摸仍归属正在退出的商城 screen，并非挂机 handler、`4/5` builder 或战斗 parser 的故障。

修复在自动化状态机，而不是服务端增加补包或重试：进入商城前记录实际场景 screen 描述符，
按 `E` 前记录商城 screen；接收 `30/2` 后，只有观察到场景 screen 已恢复为原描述符，且完成了
晚于该响应的一帧 LCD 渲染，才投递一次场景“挂机”触摸。这里使用 callback 可见的 screen
生命周期作为门槛，不使用固定等待时间。

修复后的隔离回归
`shop-return-hangup-v1-20260803T033034493Z-29900` 通过。取证为：初始 scene=`01053f78`、
shop=`010536b4`；`30/2` 传输序号 159 于 frame 251 到达，frame 252 已恢复 scene owner 后才发出
挂机触摸；服务端随即记录 `scene_hangup_start` 和四对象 `4/5` 启动响应，客户端最终命中
`mmBattle:0x66CC`。`result.json` 的最终断言为
`passed / hangup-4-5-reached-mmBattle-0x66CC`。这同时排除了商城返回包结构、返回时 NPC reseed
和挂机启动 builder 为该路径的当前根因。

为排除一次性时序巧合，使用新的隔离数据库、客户端目录、端口进程和登录记录再次运行
`shop-return-hangup-v1-20260803T033432303Z-33368`，同样通过相同终态断言；该次在 response
序号 174、frame 255 接收 `30/2`，frame 256 才投递挂机触摸，随后观察到挂机启动包。两次
运行结束后，运行器均已清理自己的 PID、监听端口和 `jh_online_autotest_*` 数据库。

## 2026-08-03：位置型 30/2 场景重入回归

上述早期运行所称的“无 `posinfo` 30/2 返回完成”仅能证明旧 scene 画面恢复，不能证明
商城生命周期已释放。新的 IDA 证据表明
`scene_handle_change_result_scene_pos(0x01039770)` 只有读到 `posinfo` 才调用 scene
controller `+116(scene,x,y,0)`。因此自动化的商城返回完成条件也相应更正为三个客户端
可观察边界，而非固定等待：

1. 收到商城返回的 `1/30/2`；
2. 客户端为原 scene descriptor 执行一次新的 `ScreenInit`；
3. 这个新 shell 发出的后续 `25/5` 已回调，且两者之后各越过两个渲染边界。

这只是确定挂机硬件输入的正确归属，不改变客户端状态、网络响应或调度顺序。

修复后的隔离回归 `shop-return-hangup-v1-20260803T072135595Z-22268`：

- 服务端在商城返回 task-subset 发送 255 字节响应，记录
  `completion=30/2-posinfo-reenter`；
- 自动化记录 `return_seq=172`、native `ScreenInit` 于 frame 267、后续 `25/5` 于
  frame 269，随后 frame 272 才发送唯一一次 `(50,350)`；
- 服务端收到真实挂机请求并发送 248 字节 `2/10 + 2/2 + 4/5 + 4/11`；
- 客户端命中 `mmBattle:0x66CC`，继而命中 BattleScene 首帧绘制；
  `result.json` 记录 `passed / hangup-battle-main-draw-after-4-5 / input_count=3`。

无商城的对照 `direct-hangup-control-v1-20260803T072300434Z-33896` 同样通过相同的
`0x66CC -> DrawMain` 断言（`input_count=1`）。两次运行均使用一次性
`jh_online_autotest_*` 数据库、`127.0.0.1:19190/19191` 及其自有 PID，结束后自动清理。
