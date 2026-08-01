# 2026-07-26 商城「充值」Wpay 空回调崩溃 → account_web

## Symptom

商城点「充值」后进程 abort：`地址无法访问:0`，`pc:0 lastPc:100bab6`
（`UC_MEM_FETCH_UNMAPPED`，对空函数指针 BLX）。

## Contract

- 商城「充值」不走 WT 商城购买（`14/3` / `17/2`），而是加载 **Wpay CBM**
  并进入 `wpay_http_pay`（guest `0x051812DA`）。
- 功能机 Wpay / 浏览器支付在模拟器上无法完成；账号侧充值权威路径是
  `servers.conf` 的 `account_web_url`（用户中心 W 币充值页）。
- Android 已有 `vm_android_request_account_web_open` → Java
  `openAccountWebsite()` 打开该 URL。

## First Deviation

日志顺序（`bin/client_out.txt`）：

1. `[wpay_trace] wpay_http_pay pc=051812da ...`
2. `DF_DataPackage_DoLoading` + Wpay `SCR_FUNC`（`0x0518xxxx`）
3. `stack-push caller=01002e50`
4. `pc:0 lastPc:100bab6 r0:0` — guest `0x0100bab6` 对 **null** BLX

首次偏离在 Wpay HTTP 支付 UI 继续推进时缺少有效平台回调；崩溃 PC 是症状。

## Fix

1. 仅在充值特有空 BLX `0x0100bab6`（`r0==0`）打开 `account_web`；
   不按绝对 VA 截断 `wpay_http_pay`（选角阶段也会走到该 VA）。
2. 打开网页后 **弹出支付子界面 + Wpay 屏**，`ScreenResume` 回到商城，
   避免停在 `init:10031db`（pause/resume=0）导致返回后无法操作。
3. guest `vMAssert` / 未映射访问 soft-fail，禁止宿主 `assert(0)` / `SIGABRT`。
4. Java：确认对话框 + Application 上下文 `NEW_TASK` 外开浏览器；对话框关闭后
   把焦点还给 `screenView`。

## Post-redirect freeze (1.2.6)

- **症状**：弹窗/浏览器可开，切回游戏后卡住不能点。
- **证据**：拦截后仍出现
  `[SCR_FUNC](init:10031db,…,pause:0,remuse:0)` / `ScreenInit Ok`
  （支付 overlay `01056048`，在 null BLX 前 `stack-push`）。
- **根因**：`PROGRAM_EXIT` 只避开空 BLX，宿主仍按已挂起的 `screenStructChange`
  初始化该 overlay；该屏不处理输入。
- **修复**：`vm_host_restore_after_mall_recharge_redirect` 丢弃 overlay + Wpay，
  恢复栈上商城并 `g_screenResumeExisting=1`。

## Startup race abort (1.2.7)

- **症状**：选区启动即崩：`入口初始化失败` / `此处内存不可执行`，
  `pc:1000000 lastPc:1`，`assert` @ `RunArmProgram`。
- **证据**：`launching` → Thread-4 入口失败 → 随后主线程才打印
  `loaded … VM loaded (android deferred start)`；`r9=0 msp=0`。
- **根因**：`g_gameLauncherActive=false` 在 `uc_open` 之前，`cbeRun` 见
  `MTK!=NULL` 即跑 `RunArmProgram`，ROM/SP/R9 尚未写完。
- **修复**：`g_cbeVmReadyForRun` 仅在 load 完成后置位；`cbeRun` 等该标志；
  成功 load 后再清 `g_gameLauncherActive`。

## Post-popup freeze (1.2.8–1.3.0)

- **症状**：一点充值（Toast/浏览器）方向键就死。
- **证据**：改 screen stack / `ScreenInit` 商城后仍无后续逻辑；正常路径不该由
  宿主拆栈。
- **根因**：`PROGRAM_EXIT` + 强制改屏栈破坏了 Wpay/商城契约；模态框只是表象。
- **修复（1.3.1）**：
  1. 空 BLX 用 `vm_bx(next)` **跳过指令**（仅改 r0 无效，仍会 `pc=0`）。
  2. 不改屏栈；`scheduler_tick` 注入右软键关 Wpay。
  3. 浏览器延迟 800ms；`SCR_Event` 在 redirect 后对 unmapped 不再 `assert`。

## Product decision (1.3.2–1.3.4)

- 游戏内「充值」不再打开浏览器 / 完成 Wpay。
- 1.3.2 Toast、1.3.3 仅跳过空 BLX 仍会卡住。
- **根因（日志）**：`stack-push 01056048` 后仍 `ScreenInit` 支付层
  （`init:10031db`，`pause/remuse=0`），输入被该层吃掉。
- **1.3.4**：丢弃支付层 + Wpay 后 `ScreenResume` 商城宿主仍卡（`remuse=0`）。
- **1.3.5**：宿主丢弃到根界面仍卡——未走客端 `screen_mgr remove`。
- **1.3.6**：拒绝 `caller=01002e50` 支付层 add；空 BLX 时注入右软键/取消
  （与界面「取消」相同），由客端自己 remove Wpay→商城。

## Verification

- 重装 APK（versionName ≥ 1.3.6）→ 商城 → 充值
- 日志含 `reject pay overlay add` / `inject cancel key`，以及随后真实的
  `screen_mgr remove`（Wpay、商城）
- 退出商城且可继续操作
