# main_gitee_r2 图片追踪优选后的构建回归

## 触发条件

`main` 优选 `main_gitee_r2` 的 `5df09034a4feb21bc7e176ef897f7092612b27d6`
（本地优选结果为 `749d543cbb30da54a9bceeddda07e5d809e9910f`）后，执行
`make -j2`。客户端编译在包含 `src/vmFunc.c` 的 `src/main.c` 时失败。

## 首次偏离与根因

优选提交以新的 `vm_scene_number_draw_trace` 诊断实现替换旧的 LCD 追踪实现时，
同时删除了图片数据包生命周期所需的三个静态状态：

- `g_vm_img_app_data_package`
- `g_vm_img_inner_data_package`
- `g_vm_img_current_data_package`

这些状态仍由 `vm_IMG_InitDataPageEx`、`vm_IMG_ReleaseDataPage` 及图片资源创建路径
使用，并且重启清理逻辑也会复位它们，导致首个编译错误为未声明标识符。

同一替换还保留了三个对已删除旧追踪函数的调用：
`vm_lcd_trace_scene_number_draw` 两处和 `vm_lcd_trace_scene_resource_create` 一处。
即使先补齐静态状态，它们仍会在后续链接阶段形成未解析符号。

## 修复与边界

在 `src/vmFunc.c` 恢复三项静态数据包状态定义，并删除三处已经不存在的旧追踪调用。
新的 `vm_scene_number_draw_trace` 调用保持不变；修复不改变 CBE 客户端内存、寄存器、
回调、数据包或调度行为。

## 验证

2026-08-31 在 `main` 执行 `make -j2`：客户端 `bin/main.exe` 与服务端
`bin/jh-online-server.exe` 均完成编译和链接。

未运行客户端自动化：本次仅恢复被优选误删的宿主静态声明并移除已删除函数的遗留调用，
没有改变任何协议、解析器或业务流程。
