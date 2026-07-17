# 大端序 CBE 运行契约（武林外传新品）

## 目标与样例

本轮以 `bin/CBE_BE/武林外传(新品).CBE` 为样例，目标是把 native 大端 ABI 适配在调用边界，文件、内存、图片解码、LCD 绘制、屏幕调度继续复用已有小端实现。

样例头部解析结果：

- 代码：文件偏移 `0x9b`，长度 `0x154fc`，装载基址 `0x043e3000`。
- BSS：文件偏移 `0x1559f`，长度 `0x1150`。
- RW 数据：文件偏移 `0x166f7`，长度 `0x2280`，运行时数据基址 `0x043f84fc`。
- 内嵌 DataPackage：文件偏移 `0x1898b`，长度 `0x3dc7b`，共 153 个资源。

## IDA 证据与错误根因

参考固件为 IDA 中的移动乐园 600H 固件，客户端为当前打开的 `武林外传(新品).CBE`。

### 生命周期不是逐帧 logic

- 客户端 `sub_0` 通过 service `0x79e` 注册 `{main, exit, dispatcher}`，运行时分别是 `0x043e56f3`、`0x043e5679`。
- 固件 `vmEnterWin` 在进入窗口时同步调用 main，窗口销毁时才调用 exit。main 是两阶段初始化入口，不是逐帧 logic。
- 模拟器现在连续执行两次 main 完成 manager bootstrap 和真正的游戏入口，然后落回已有小端屏幕调度器；不再把 main 每帧重复调用，也不在启动时误调 exit。

### “系统更新”不是 IMEI 返回值造成的

- `CBE_BE/imeiInfo.dat` 能被正常读取，末尾状态字节为 `0x03`，客户端实际进入了游戏主初始化 `sub_1D38`。
- `upinfo3.dat` 是运行时可写状态，不是只读分发资源。`CBE_BE/upinfo.dat` 的末字节为 `0x01`，客户端完成一次检查后写回的 `CoolBar_H_QVGA/upinfo3.dat` 末字节为 `0x00`。把缺失的运行时文件回退到分发 `upinfo.dat`，会在每次干净启动时重新导入更新标志；这才是“系统更新”首屏的直接分支条件，并非 IMEI 或 service `0x406` 的返回值。
- 真正的问题是固件同步调用 main，而模拟器延后到模块入口返回后才调用。旧兼容代码把 GameManagerOld 表地址写入了客户端的 SystemInfo 全局，随后客户端在 SystemInfo `+4/+8` 安装绘图适配器时覆盖了 GameManagerOld 的图片槽，初始化分支和调用链随之失真。
- service `0x52` 现在先创建独立 SystemInfo 对象并写入客户端全局；service `0x7d1` 再补齐方法。SystemInfo `+0x28` 经客户端首屏 destroy（IDA `0xAED8`）确认是图片释放入口，复用小端 `IMG_Destory`。

### manager 表的真实长度

- 客户端传给 SystemInfo `+0xf0` 的 `0x043f855c` 只包含 7 个生命周期回调，后面紧接资源 ID 全局，并不是 52 项 manager 表。
- 旧实现展开 52 项会把资源 ID `0x54`、`0x55` 等覆盖为宿主函数桩，导致后续图片资源号变成 `0x0c0000xx`。
- native 路径现在只验证并保留前 7 个客户端回调；传统小端路径仍按原逻辑填充 52 项。

### Thumb 与回调栈

- 600H 的 GameOld 表把所有函数指针写成 `function + 1`。native manager、SystemInfo 和对象方法因此统一保留 Thumb 低位。
- Unicorn 在 SVC 模式改写 CPSR 时可能恢复旧的 banked R13。`vm_bx` 和模拟入口在切换 Thumb 状态后恢复当前 SP；屏幕、定时器和网络等宿主发起的顶层回调使用新的任务栈顶。

## 已实现的宿主契约

### `CBE_BE` 配套文件

- 所选 CBE 的同级目录作为大端资源根目录，独立保存在宿主状态中。
- `CoolBar_H_QVGA/downinfo3.dat`、`helpinfo3.dat`、`imeiInfo3.dat` 等读取请求会在 `CBE_BE` 中尝试同名文件，再去掉文件名末尾数字，解析到 `downinfo.dat`、`helpinfo.dat`、`imeiInfo.dat`。
- `upinfoN.dat` 明确排除在同级只读回退之外：缺失时由客户端根据 `downinfo.dat` 重建，后续读写都落到运行时 `CoolBar_H_QVGA`。这样既不会覆盖 `CBE_BE` 中的分发种子，也不会反复导入旧更新标志。路径输入同时支持 UCS-2LE/UCS-2BE，底层仍复用小端 CBFS 接口。

### native 服务与内存

- service `0xb7`/`0xb8`/`0xb9`/`0xbe` 分别复用已有 DreamFactory MemoryBlock 初始化、重置、分配和释放语义。样例先申请 5120 字节，随后重建为 `0x57800` 字节。
- service `0x41a`、`0x41b`、`0x427`、`0x41c`、`0x42a` 继续复用小端文件打开、写入、读取、关闭和取长度实现，结果通过现有 `0x7d1` 两阶段接口回传。
- 内嵌 DataPackage 沿用现有解析器，识别 153 个条目；GameOld native 槽 79 触发同一数据页初始化。

### GameManagerOld 与对象布局

- service `0x52` 用已有 GameManagerOld 列表初始化 159 项，再为 native ABI 加 Thumb 位，并只覆盖经 IDA/运行确认的槽位。
- 图片创建、透明/非透明裁剪绘制、图片宽高、RGB565、LCD flush、屏幕切换都转到已有小端实现。
- native 槽 76 复用图片库初始化。Wulin 的槽 108/容量 5 是紧凑对象，方法只到 `+0x30`，其后立刻是首屏回调表；槽 73/容量 20 是派生图片库，方法延伸到 `+0x50`。按容量区分可避免覆盖首屏 `init/destroy`，同时保留退出时需要的释放方法。
- native 槽 78 初始化紧凑场景对象，并复用 600H `sub_1F688A` 的 region/capacity 布局。Wulin 客户端事件方法位于 `+0x30`，SDK 方法从 `+0x34` 开始。
- Wulin 使用的文字对象构造槽在所检查的 600H SDK 版本中为空。模拟器按客户端包装器 `sub_3AC` 的 ABI 初始化基础文字对象，客户端仍负责安装自己的 `+0x24` 方法。

### native 图片 ABI

Wulin 把图片头读取为：

```text
+0x00  pixels : u32
+0x04  width  : u32
+0x08  height : u32
```

小端图片解码器使用 `{pixels, u16 width, u16 height, ownership}`。直接返回紧凑头会让 Wulin 把高度当成宽度、把 ownership 后的 0 当成高度，首屏裁剪高度因此为 0，只剩黑屏。

native 返回边界现在重排为 3 个 32 位成员，把 ownership 暂存在 height 的高 16 位；调用小端裁剪绘制前，在 guest 栈上构造临时紧凑头。这样 GIF/PNG/原始 RGB565 解码和 LCD 栅格化无需维护第二份实现，图片释放时也能恢复 ownership。

## 运行验证

大端序样例：

```powershell
.\main.exe '--cbe=CBE_BE/武林外传(新品).CBE' --autotest --shot-ms=1000 --max-ms=8000
```

结果：在删除运行时 `CoolBar_H_QVGA/upinfo3.dat` 的冷启动条件下退出码仍为 0。`downinfo.dat`、`helpinfo.dat`、`imeiInfo.dat` 均从 `CBE_BE` 成功解析，`upinfo3.dat` 由客户端重建；DataPackage、两套图片库、场景和文字对象完成初始化；首屏显示游戏自己的“请稍候...”启动画面，不再进入“系统更新”画面；8 秒运行及 native exit 清理无空地址取指、assert 或内存池耗尽。

小端序回归：

```powershell
.\main.exe '--cbe=CBE/江湖OL.CBE' --autotest --shot-ms=1000 --max-ms=4000
```

结果：退出码 0，首屏、后续屏幕和宿主退出清理均正常。

## 当前边界

- 600H 与 Wulin 来自不同 SDK 小版本，少数高位 GameOld 槽位只能结合客户端包装器确认；新增映射必须保持小端索引不变。
- 紧凑场景和图片库中尚未逆出的非关键方法当前为可调用软返回。已确认的矩形填充、图片绘制和释放路径正常；场景提交等未知方法只做一次告警，避免逐帧刷屏。
- service `0x406` 目前只确认两阶段返回约定，仍按成功且返回 0 处理，没有伪造客户端状态。
