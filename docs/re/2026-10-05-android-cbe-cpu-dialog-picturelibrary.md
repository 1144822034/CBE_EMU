# 安卓「当前Cpu状态异常」：江湖 OL 第一屏 PictureLibrary

日期: 2026-10-05

## 现象

确认「经典怀旧」后很快弹出「当前Cpu状态异常，建议重启」。这是 Java
`startEmu()` 返回后 `m_cpuState` 的提示，不是 SoC 硬件故障。

## 证据（logcat `com.jianghu.pixijs`）

- `CbeNativeLoader: loaded apk jniLibs`（本次 APK 内 `libmtksim` 已与
  `jni-hot` 同 hash，overlay 被跳过）。
- CBE 已加载：`selected=CBE/江湖OL.cbe`，`Unicorn Engine Initialized`，
  第一次 `ScreenInit Ok`。
- `DF_DataPackage_DoLoading` 后切到 `SCR_FUNC init=0103b0b1`。
- `[call]vMAssert(..\..\code\src\DF_PictureLibrary.c:95, lr:1000647, last:1000644)`
  `r2:c000440`（HK 表 vMAssert 槽），随后 `地址无法访问:4`，
  `SCR_Init异常 emu_err=11 init=0103b0b1`，`Invalid memory mapping`。
- Thumb `0x01000636..1000646`：`ldr` sb 相对量后 `blx r2` 直接进 vMAssert。

## 根因陈述

首次偏离在 **第二屏 ScreenInit（0103b0b1）里 DF_PictureLibrary.c:95**：
guest 对图片库对象 `+0x40` 的函数指针做 BLX，指针是宿主 `vMAssert` 槽；
断言返回后继续用空对象，访问地址 4，Unicorn `uc_emu_stop`，Java 弹窗。

尚未确认：该虚表为何在安卓启动路径上未建成（PC 同 CBE/同 CBM 48 858
字节可过标题）。禁止用关掉弹窗或吞掉 `vMAssert` 当完成标准。

## 与 PC 对照（热更同步 JNI 之后）

PC `bin/client_out.txt` 同一屏 `init=0103b0b1`：先多次
`aeabi_div0 divisor0->1 pc=0104d538`，再 `ScreenInit Ok`。
安卓 logcat **没有**这些行，直接 `DF_PictureLibrary.c:95`。

`hookCodeCallBack` 虽对 0..0xFFFFFFFF 调 `vm_host_aeabi_div0_divisor_one`，
aarch64 Unicorn 常常只在 TB 块入口回调，进不了 `0x0104D538`，除数 0 得到
0 宽/0 计数，图片库断言。x86 上宽钩子能打到该 PC。

## 修改

`add_manager_code_hooks`：对 `0x0104D538` / `0x0104D610` / `0x01004CD2`
（含 Thumb LSB）加 begin==end 的 `UC_HOOK_CODE`，走同一套 divisor0→1。

窄 CODE 钩子随 APK 已加载（hash `948f1a6d…`）仍无 `aeabi_div0` 行，
说明 aarch64 Unicorn **连 begin==end 也不进该 PC**。

`0x01005bd0` 是 `DF_PictureLibrary::GetImage`：`[lib+0x38]` 查找失败
（r6<0）或 `this+0x14`/`+0x08` 有符号半字比较失败会走到 `:95`。

ROM 写入后把 guest `__aeabi_idiv`/`uidiv` 首指令改成 ARM UDF
（`0xE7F001F0`），由 `hookInsnInvalid` / `UC_ERR_INSN_INVALID` 在宿主做
AEABI 除法（除数为 0 时仍当 1），再 `vm_bx(lr)`。这是运行时 I/O 替
guest 执行库函数，不改游戏逻辑、不吞 `vMAssert`。

`GetImage` `0x01005BD0`/`0x01005BF0` 打 `[warn]` 取证（名字、got、宽高）。

2026-10-05 22:53 新 so 已加载：`aeabi_idiv host-udf patched`，无
`aeabi_idiv host` 调用。`GetImage('loading.gif') got=ffffffff w=10 h=0`，
随后 `UI8.gif`/`UI7.gif` 同样 -1。第一偏离是 **资源名查找失败**，不是
idiv。`DoLoading` 在 `a3!=0` 时读 `VM_DF_DataPackage_LoadType_ADDRESS`
（PRAM）。主路径只 `memset` 了 ROM，PRAM/栈/堆/HK 表是 `malloc` 脏页；
安卓上 `loadType==1` 会走 TF 卡而非 CBE 内嵌 TResource，包内没有
`loading.gif`。

2026-10-05 23:01 清零 PRAM 后仍是 `path=tcard loadType=1 count=0`：guest
**主动写入** loadType=1（PC 同序也能过标题）。PC 能打开 TF 卡路径；
安卓 `LoadFormTCard` 得到空包。`LoadPackage` 已把同一份数据放到
`source=05010ce4`。

## 修改（续）

映射前将 ROM/STACK/PRAM/RAM/HK 全部清零（与 restart 路径一致）。
`DoLoading` 打 `path=tcard|tresource loadType= count=`。
2026-10-05 23:07：TResource 回退导致黑屏（emu 线程卡在
`LoadFromTResource`，无 fallback 日志）。tcard `head=4342452f`（`CBE/`）、
`fileoff=353281`，路径被当成 UCS2 解成 `????????`。

正确契约：打开已加载的 `CBE/江湖OL.cbe`（GBK 8 位路径）再 seek。
`vm_read_path_string` 对 `CBE/` 前缀走 GBK→UTF-8；去掉错误 TResource 回退。

2026-10-05 23:17：TCard `count=2` 后 `GetImage('loading.gif') got=0x201 w=10 h=0`。
嵌入 CBE 包只有两份目录项，标题 GIF 不在包内。契约是和 PixiJS
共用 `JHOnlineData`：`GetFile`/`GetFileID` 优先
`filesDir/dist-hot/files/JHOnlineData/<name>`（Pixi 热更层），再
`filesDir/JHOnline/JHOnlineData/<name>`（与 WebView 同源解压）。
裸名 `.gif/.dsh/...` 以及已带 `JHOnlineData/` 前缀的 fopen 都走这条解析。

2026-10-05 23:31：对照 PC `bin/JHOnlineData`。磁盘资源与 Pixi 相同：
`LE u32 block_len` + `u8 type` + payload。type=1 GIF 解包后才是 LKV2
（`dict_size` BE）。宿主 `vm_resource_cache_load_file` 原先只剥 4 字节，
把 `type=1` 留给 `gifDecodeExt`，`dict_size` 变成 `0x01xxxxxx` → 地图彩噪/
文字竖条。PC `main.exe` 仍是包优先、磁盘兜底；安卓共享树保留，但
GetFileID/GetFile 不再抢在包前面，且 type=1 剥到 offset 5。

2026-10-05 23:58：勿复用他应用的 `/sdcard/JHOnline`。Java `canRead` 可能为真，
但 Pixi uid 对 media_rw 树 `fopen` 失败 → 大厅/UI GIF 全空。固定
`filesDir/JHOnline`，JHOnlineData 仍从 JianghuOL `cbe_jhonline/` 解压。
