# 游戏内容更新管理页导致服务端退出

## 触发条件

后台已登录状态访问 `GET /?tab=updates`（点击“游戏内容更新管理”）。服务端在渲染
`vm_mock_admin_render_update_page` 时退出；启动日志通常只会留下监听成功记录，因为故障
发生在处理该 HTTP 请求的工作线程中。

## 调查链路

`GET /?tab=updates` → `vm_mock_admin_render_page` →
`vm_mock_admin_render_update_page` → `vm_mock_admin_text_appendf` → `vsnprintf`。

独立回归程序 `scripts/update-page-render-regression.c` 在不启动监听器、不连接 MySQL、
不写入资源状态的条件下重现了同一错误。MinGW GDB 的第一处可归因帧是
`vm_mock_admin_text_appendf` 的 `vsnprintf`，格式字符串从“下发记录”开始。

## 根因

该次 `vm_mock_admin_text_appendf` 的 HTML 同时含有：

1. “当前记录 `%u` 个客户端标识”；
2. 场景内容发布摘要的 `%s`、`%s`、`%u`、`%u`、`%u`。

共六个格式化参数，但调用只传入了后五个场景摘要参数，遗漏了下发记录数量。
`vsnprintf` 因而把错误类型的栈值当作后续参数，最终读取一个无效 `%s` 指针并访问冲突。
这不是页面过大、MySQL 或网络监听器问题。

同一段渲染中，“服务器资源（%u）”只有一个格式化占位符却传入了两个参数；虽不会直接
造成这次崩溃，但会显示错误的数字，也一并改为正确的资源文件数量。

## 修复

在更新页摘要渲染调用中按占位符顺序补入
`g_vm_net_mock_update_delivery_count`，并移除资源数量标签的多余参数、传入 `fileCount`。
修复位于 `src/web_admin_server.c`，即格式串的唯一所有者；没有修改请求路由、吞掉错误或
改变客户端更新协议。

## 当前页面导航契约

更新管理页采用左侧选项列表：

1. 游戏数据内容更新；
2. 启动模块更新；
3. 启动模块配置。

三项均使用同一 `tab=updates` 路由和 `section=content|modules|configuration` 参数。
页面为每项输出 `data-admin-list` 与 `data-admin-detail`；`admin.js` 只请求并替换右侧
`data-admin-detail`，同时更新 History 状态，避免切换选项时整个后台页面重载。服务端仍
生成完整、已鉴权的 HTML 文档，以保持直接访问、刷新及浏览器后退的正常行为。

## 回归验证

执行：

```powershell
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/update-page-render-regression.c src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o obj/server/update-page-render-regression.exe '-Wl,--gc-sections' -lpthread -liconv -lm -lkernel32 -lws2_32
.\obj\server\update-page-render-regression.exe
```

该测试直接渲染默认游戏数据、启动模块和启动模块配置三个真实页面状态，并确认每个状态
都有 `data-admin-list`、`data-admin-detail` 与对应功能区，且响应未截断。它覆盖了此前发生
访问冲突的同一格式化调用，而不依赖浏览器或用户服务进程。
