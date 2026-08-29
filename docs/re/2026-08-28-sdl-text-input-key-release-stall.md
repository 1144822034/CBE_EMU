# SDL 文本输入提交后宿主按键闩锁未释放

Date: 2026-08-28

Status: implemented; host-input regression validated

## 当前卡点

- 可见现象：在 SDL 输入框键入文字并按回车提交后，模拟器对后续任何键盘按键没有反应。
- 最小触发：先以键盘按下打开 CBE 文本输入，再在 SDL 文本模式生效后释放该打开按键；
  输入文字并按回车，待输入框关闭后继续按任意键。
- 目标：恢复宿主 SDL 键位闩锁的配对释放，不修改 CBE/CBM、CBE 键状态、screen、回调或
  输入框业务语义。

## 已确认调用链

1. 普通 `SDL_KEYDOWN` 在 `isKeyDown == SDLK_UNKNOWN` 时记录键值，并经
   `keyEvent(MR_KEY_PRESS, key)` 放入原有硬件事件队列。
2. CBE 处理该按下后调用 host input API；`vm_input_open()` 设置 `g_vmInputOpen=1`，
   请求 SDL 文本输入。
3. 旧 `SDL_KEYUP` 分支在 `g_vmInputOpen` 为真时直接 `break`。若这正是打开输入框的
   原按键释放，`isKeyDown` 将永久保持那个旧键值。
4. 回车只排入 `VM_EVENT_INPUT_DONE`；模拟线程的 `vm_input_finish()` 在调用 CBE callback
   **之前**已经把 `g_vmInputOpen=0`。因此 CBE 输入框本身按正确路径收尾。
5. 之后普通 `SDL_KEYDOWN` 检查 `isKeyDown == SDLK_UNKNOWN` 失败，不会调用 `keyEvent()`；
   模拟器表面上表现为所有按键无反应。

## 证据与边界

- `src/main.c` 的 `SDL_KEYDOWN` / `SDL_KEYUP` 对 `g_vmInputOpen` 的不对称处理是确定的
  宿主状态失配：按下前记录键值，文本模式期间却丢弃同一键的释放。
- `vm_input_finish()` 已在 callback 前关闭文本模式；不需要、也不能伪造 CBE callback
  返回、强制切 screen 或写入 CBE 全局状态。
- `vm_clear_key_down_state()` 只清除 CBE 暴露的 `g_curKeyDownState`，不拥有
  SDL 线程的 `isKeyDown`。因此不能拿它作为宿主闩锁复位的替代。
- 这是纯宿主平台输入事件契约；没有新增网络 packet、callback 重排或客户端内存写入。
  已有 `docs/re/2026-06-26-sdl-text-input.md` 记录了 SDL 文本输入的 open/text/done/close
  契约。

## 根因陈述

触发条件是“用于打开文本输入的键在 `g_vmInputOpen` 已变为真后才抵达 SDL 的
`KEYUP`”。被违反的契约是每一个被宿主记录的键按下必须由同键释放清除宿主闩锁；第一处
错误状态是 `isKeyDown` 残留旧键。随后回车完成 CBE 文本输入并不修复该宿主残留，新的
按键因单键防重机制被拒绝。

## 实施结果

- `src/main.c` 新增 `vm_host_handle_key_up()`：匹配当前 `isKeyDown` 的 `SDL_KEYUP`
  无论 SDL 文本模式是否打开都先释放宿主闩锁；只有文本模式未打开时，才按原语义调用
  `keyEvent(MR_KEY_RELEASE, key)`。
- SDL 事件循环统一调用该辅助函数。文本模式仍不会把打开输入框的按键释放、回车释放或
  输入法释放传入 CBE；改动只修复 SDL 线程自身的配对状态。
- 新增 `scripts/sdl-text-input-key-release-regression.c` 及 Makefile 目标，复现“普通按下
  -> 输入框打开 -> 原按键释放 -> 输入完成 -> 后续新键按下/释放”。测试按真实
  `SDL_KEYDOWN` 的空闲闩锁条件决定是否入队，断言后续键仍进入已有 VM 硬件事件队列。

## 验证清单

- [x] 文本模式中的原按键释放清除 `isKeyDown`
- [x] 文本模式不向 CBE 额外投递打开键的释放
- [x] 输入完成后新的按键按下和释放仍进入原有硬件事件队列
- [x] 普通非文本输入的按键释放语义不变
- [x] 没有 CBE/CBM、寄存器、PC/LR 或 screen 状态写入
- [x] `make sdl-text-input-key-release-regression` 编译通过；其可执行文件以退出码 0
  通过。
- [x] `make -j2` 通过，并重新链接 `bin/main.exe`。

该回归不启动 SDL 窗口、服务端或 CBE 虚拟机；它验证的是本故障第一次偏离发生的 SDL
线程输入闩锁与 VM 硬件事件队列边界。实际文本提交和 callback 仍完全由既有 CBE 路径处理。
