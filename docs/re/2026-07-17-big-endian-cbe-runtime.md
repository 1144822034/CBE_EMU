# 大端序 CBE 运行契约（武林外传新品）

## 目标与样例

本轮以 `bin/CBE_BE/武林外传(新品).CBE` 为样例，目标是让大端序 CBE 继续沿用模拟器已有的小端序管理器和文件接口，而不是为同一套宿主能力维护第二份实现。

样例头部解析结果：

- 代码：文件偏移 `0x9b`，长度 `0x154fc`，装载基址 `0x043e3000`。
- BSS：文件偏移 `0x1559f`，长度 `0x1150`。
- RW 数据：文件偏移 `0x166f7`，长度 `0x2280`，运行时数据基址 `0x043f84fc`。
- 数据包：文件偏移 `0x1898b`，长度 `0x3dc7b`。

## 固件与客户端证据

参考 binary 为 IDA 中的 `6彩移动乐园600H_TOSHIBA TV00560002EDGB.bin`（ARMB-32）：

- `sub_F9594` 检查连续 8 个 `0xfe` 字节，用它识别大端序 CBE 标记。
- `sub_F873E` 逐项读取 12 字节头部字段，并以大端序组合长度/偏移。
- `sub_F94DA` 与 `sub_F9462` 负责 CBE 校验、装载和后续合法性检查。
- 固件中的源文件字符串指向 `vmInit.c`，与客户端初始化兼容层的行为一致。

客户端侧验证结果：

- `0x043e56f3` 是注册给宿主的 logic 回调，`0x043e5679` 是一次性 init 回调；注册顺序为 `{logic, init, dispatcher}`。
- `0x043e803e` 从 SystemInfo 取得 `+0xf0` 函数，并用 `0x043f855c` 的 52 项表调用它；附近字符串为 `vmInitManagers:%x`。
- 该 52 项表前 7 项已经是客户端 ROM 内适配函数，其余项目是管理器编号。因此初始化时必须保留 ROM 覆盖项，只把编号转换为模拟器现有管理器入口。
- `SystemInfo + 0x20c` 是连续 22 项的 stdio 表。只提供少数槽位会在客户端读取缺失入口后跳到空地址。
- native service `0x52` 的参数不是 SystemInfo，而是客户端自有的 `0x27c` 字节 GameManagerOld 平表。样例把该对象地址保存在 `0x043f964c` 指向的位置，随后只覆盖少数适配槽。
- 客户端包装器 `0x043e41f2` 读取 GameManagerOld `+0x1b0`（槽 108），以 `(object, capacity=5)` 调用；启动尾部的另一包装器以槽 73 和容量 20 初始化相同字段布局。
- 600H 固件 `sub_1F4552` 是对应的图片库初始化器：为 `object+0x00` 分配 480 字节扫描线，为 `+0x0c` 分配 `capacity*2` 的资源 ID 数组，为 `+0x10` 分配 `capacity*4` 的图片指针数组，并在 `+0x18..+0x50` 安装 15 个方法。客户端在初始化后调用的 `+0x20`、`+0x30` 以及自行覆盖的 `+0x34..+0x40` 都与这一对象布局吻合。

## 已实现的宿主契约

1. 大端序 native app 先运行一次 logic 完成管理器 bootstrap，再运行一次 init，之后由宿主逐帧调用 logic。
2. `SystemInfo + 0xf0` 指向已有小端序管理器总表的第 34 项；该入口合并 52 项表并保留客户端 ROM 适配函数。
3. `SystemInfo + 0x20c` 通过 `vm_configManagerTableCount` 填满现有 stdio 管理器的 22 个入口。
4. native service `0x41a`、`0x41b`、`0x427`、`0x41c` 分别复用 `vm_cbfs_vm_file_open`、`vm_cbfs_vm_file_write`、`vm_cbfs_vm_file_read`、`vm_cbfs_vm_file_close`；单参数 service `0x42a` 复用 `vm_cbfs_vm_file_getfilesize`。结果仍通过 `0x7d1` 两阶段接口回传。
5. 文件路径读取同时识别 UCS-2LE 和 UCS-2BE。UCS-2BE 先规范化为 UCS-2LE，再走原有 GBK/宿主路径转换逻辑。
6. 大端序 CBE 的只读配套资源从当前所选 CBE 的同级目录解析。客户端请求 `CoolBar_H_QVGA/downinfo3.dat` 时，会依次尝试原相对路径、同级同名文件，以及去掉文件名末尾数字后的同级 `downinfo.dat`；写入路径不重定向到分发资源，避免改写原文件。
   所选目录保存在独立宿主状态中，不能复用会被绘制、格式化等接口覆盖的 `cbeTextString` 临时缓冲。
7. guest 栈地址允许作为 native 日志/调试参数，避免合法的栈内字符串被误判。
8. 宿主请求退出时调用 `uc_emu_stop` 唤醒仍在 native callback 中运行的 Unicorn，再执行已有清理流程。
9. native service `0xb9` 的参数是 `{pointer-output, size, success-byte}`。客户端先用它申请 12 字节指针表，再为三个 9 字节字符串申请内存；实现复用 `vm_malloc`，等价于小端序内存管理器的 `DF_Malloc_IN`。
10. service `0x52` 先用已有小端 GameManagerOld 函数列表填充 159 项，再仅对大端原生 ABI 覆盖槽 73、108，使其复用同一个图片库初始化入口。槽 79 保存当前图片库对象；小端表的原始索引和行为不变。
11. 图片库重复初始化时先释放旧扫描线、资源 ID 数组和图片指针数组，再重新分配。这与固件 `DF_Malloc_IN(pointer-output, size)` 的替换语义一致，避免更新轮询期间耗尽模拟器内存池。
12. service `0x3d` 接收 `{name, context, stack_size, priority:u8, auto_start:u8}`；样例名称为 `coolbarFighter`。宿主返回稳定非零句柄，最终成功字节仍由已有 `0x7d1` 两阶段回传路径写回。

service `0x406` 目前只确认了两阶段返回约定，尚未锁定它注册的回调语义；现阶段按成功且返回 0 处理，没有伪造客户端状态。

## 运行验证

大端序样例：

```powershell
$env:SDL_VIDEODRIVER='dummy'
.\main.exe '--cbe=CBE_BE/武林外传(新品).CBE' --autotest --shot-ms=1000 --max-ms=6000
```

结果：退出码 0；完成 CBE 装载、native 回调注册、52 项管理器合并、159 项 GameManagerOld 初始化、UCS-2BE 路径解析以及文件打开/读取/关闭。图片库对象分别以容量 5 和 20 初始化，界面自然推进到可见的“系统更新中…”画面，没有空地址取指、assert、内存池耗尽或无法退出的问题。验证截图为 `bin/autotest/screens/000006_00006038.bmp`。

小端序回归：

```powershell
$env:SDL_VIDEODRIVER='dummy'
.\main.exe '--cbe=CBE/江湖OL.cbe' --autotest --shot-ms=1000 --max-ms=4000
```

结果：退出码 0；正常装载、建立首屏并完成宿主退出清理。

## 当前边界

`武林外传(新品).CBE` 会依次访问 `CoolBar_H_QVGA\\upinfo3.dat`、`downinfo3.dat`、`imeiInfo3.dat`，并尝试可选的 `helpinfo3.dat`。前三个分发文件位于 `CBE_BE/upinfo.dat`、`downinfo.dat`、`imeiInfo.dat`，由大端序同级资源解析规则提供给客户端；写回的 `upinfo3.dat` 仍位于运行时存储目录，不会覆盖分发种子。

当前仍停留在系统更新轮询：`helpinfo3.dat` 缺失会按文件不存在返回，service `0x406` 也仍只实现已确认的成功/零结果两阶段契约。图片库方法 `+0x20` 与 `+0x30` 目前保持可调用的软返回，尚未复原 600H 中 `sub_1F43AE`、`sub_1F4282` 的完整资源装载语义；这是继续推进更新流程时应优先验证的边界。
