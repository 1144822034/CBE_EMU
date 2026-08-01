# 19091 管理端 worker 栈溢出崩溃

日期：2026-07-26

## 症状

管理端（默认 `:19091`）打开「游戏内容管理」「任务管理」等重页面时，进程偶发直接退出；浏览器表现为连接被重置或空白页。游戏端口 `:19090` 未必先有报错。

## 触发条件

1. mock-service worker 线程处理管理端 HTTP 请求。
2. 渲染页面临时把大目录数组放在栈上（场景/角色/XSE 列表、动态 NPC 行、完整任务目录、怪物目录，以及 64KiB 请求缓冲）。
3. Windows 上 winpthread 默认栈约 1MB，上述局部合计可达数百 KB，再叠加嵌套调用/游戏 handler 局部，即可越过栈顶。

## 契约与第一次偏离

- **契约**：worker 线程应能完整处理管理端 GET/POST，渲染并回写 HTML，不得因局部数组尺寸导致栈破坏。
- **第一次偏离**：进入 `vm_mock_admin_render_content_page` / `vm_mock_admin_render_task_page`（及同类路径）时，在栈上分配超大 `files[]` / `tasks[]` / `request[]`，而不是像更新页那样用堆缓冲。崩溃 PC 往往落在后续无关指令，是栈破坏后的症状。

## 根因

管理端页面 catalog 与 HTTP 请求缓冲使用栈数组，与 worker 默认栈尺寸不匹配。

## 修改点

1. `src/web_admin_server.c`
   - `vm_mock_admin_handle_client`：请求缓冲 `calloc`，各错误路径 `free`。
   - `vm_mock_admin_render_content_page` / `render_task_page` / `render_npc_task_select`：目录与预览行堆分配。
   - `vm_mock_admin_scene_from_form` / `optional_scene_from_form`：场景文件列表堆分配。
   - `toggle-npc` action：动态 NPC 行列表堆分配。
2. `src/web_admin_monsters.inc.c`：怪物目录堆分配。
3. `src/server/mock_server_transport.c`：worker `pthread_attr_setstacksize` 设为 8MB，启动日志带 `worker_stack_mb=8`。

## 验证

- `make -j2` 通过。
- 复测：登录管理端后连续打开账号 / 游戏内容 / 任务 / 怪物 / 商品 / 更新页；保存 NPC、任务、怪物表单；确认进程不退出且页面完整返回。
- 启动日志应出现 `worker_stack_mb=8`。

## 仍未知 / 风险

- 若某管理动作在持锁期间走极深的游戏逻辑且再叠大栈局部，仍可能吃紧；后续新页面应默认大缓冲走堆。
- 未在本文档覆盖「请求不完整」超时问题；见 `2026-07-26-admin-incomplete-request-timeout.md`。
