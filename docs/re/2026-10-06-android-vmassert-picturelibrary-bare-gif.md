# Android 闪退：`vMAssert(DF_PictureLibrary.c:95)` / 裸名 GIF

日期: 2026-10-06

## 现象

`com.xiaoxiao.mt6252simulator` 启动后约 1s 进程退出（SIGABRT）。

## 证据

```
[call]vMAssert(..\..\code\src\DF_PictureLibrary.c:95, lr:1000647, last:1000644)
r6:ffffffff
Abort: hook_vm_sys_manager_func idx=16 assert(0)
```

第二屏 `SCR_FUNC init=0103b0b1` 后立刻断言。`/sdcard/JHOnline/JHOnlineData/loading.gif` 磁盘上存在；`MANAGE_EXTERNAL_STORAGE` appops=allow。

## 根因

1. CBE 包内标题 GIF 不在目录（DoLoading 后包内项很少）。
2. `vm_read_path_string` 先按半字 UCS-2 读路径，把 ASCII `"loading.gif"` 配对成 `lo`/`ad`/`in`…（U+6F6C…），名字变成乱码，磁盘查找失败。
3. 宿主原先只把裸 `.dsh` 映射到 `JHOnlineData/`。即使名字正确，`GetFileID` 包 miss 也不走磁盘。

第一次偏离：资源名被读错 + 包 miss 无磁盘回退 → `r6=-1` → guest `vMAssert` → 宿主 `assert(0)`。

## 修改

- `vm_read_path_string`：先识别 ASCII C 字符串再 UCS-2。
- `GetFileID`/`GetFileByID` 包 miss 后走 `JHOnlineData/<leaf>`（`.gif/.map/.sce/.actor/.dsh/.cbm`）。

## 验证

标题 `loading.gif`/`UI8.gif` 等 `cacheId=0x7000+`，`ScreenInit Ok`，不再 `DF_PictureLibrary.c:95`。

## 后续（unresolved）

标题之后 guest 再要 `empty.sce`（包/磁盘都没有）→ `SCR_Render` Unicorn 失败 → `RunArmProgram` `assert(0)`。PC `bin/JHOnlineData` 也无该文件。未确认 empty.sce 是包内名、占位还是 WT18/7 资源。
