# Android 选角进图慢：根因（2026-10-10）

## 现象

冷启动后**第一次**选角确认（`1/1/6`）进图，Android 比 PC 多卡约数秒。同进程第二次选角 ~1ms。

## 取证结论（02:58 热更）

| 项 | 值 |
|----|-----|
| `teardown_free_watch` | arm → done `ok=58 bad=65526 elapsed_ms=4518` |
| 首条 INVALID | `addr=0x77` `pc=0c005404` `lr=05025c7b` `r0=010534ec` |

`0c005404` = `VM_DF_DATAPACKAGE_FUNC_LIST` + 4 → hook `idx+=1` 后为 **`DF_DataPackage_ReleasePackage`**（不是 MF_Free）。

`bad≈65526` ≈ `u16` 满幅：host 在 `ReleasePackage` 里按 `resCount=*(u16*)(pkg+8)` 遍历 `stringTable=*(pkg+12)`，对脏堆字段走出 ~6 万次 `vm_DF_Free`。

## 根因陈述

- **触发**：冷启动后首次标题拆屏，guest 调 `ReleasePackage`（模块 `lr=05025c7b`）。
- **被违反的契约**：
  1. `LoadPackage` 新建子包后，仅应存在 name（+4）与未加载标志（byte0=0）；`+8` resCount / `+12` stringTable 必须为 0，直到 `LoadFormTCard` / `LoadFromTResource` 成功并 `byte0=1`。
  2. `vm_malloc` **不清零**；旧 `LoadPackage` 只写了 +10/+0/+1，**未**像 `vm_initDFDataPackage` 那样清结构。
- **首个错误状态**：标题阶段大量「只 LoadPackage、未完成 load」的子包带着堆垃圾 `resCount≈0xFFFF` + 非空 `stringTable`；拆屏 `ReleasePackage(name=0)` 当已加载资源表遍历。
- **为何仅第一次 / 仅 Android 体感**：第一次拆屏踩中脏块；PC 同路径若不出现等价脏 `+8/+12` 则无风暴。耗时在 host C 循环，手机 Unicorn 上约 4–5s。
- **仍 unknown**：标题为何留下大量未 load 子包（资源/empty.sce 路径）；不影响本次契约修复。

## 修复（`vmFunc.c`，`src/` 与 `JianghuOL/.../cbeEmu/` 对齐）

1. **`LoadPackage`**：`DF_Malloc_IN` 子包后 `emptyBuff` 清零整结构（对齐 `vm_initDFDataPackage`）。
2. **`ReleasePackage`**：`byte0==0` 时只释放 name（+4）与 slot，**不**遍历 +8/+12；并打 `skip-unloaded` / `huge resCount` 取证行。

## 复测期望

冷启动首次 `1/1/6`：

- `teardown_free_watch … bad` 应接近 0（或远小于 6 万）
- `elapsed_ms` 应接近 PC（毫秒～百毫秒级，不再 ~4.5s）
- 可能出现 `[forensic][df] ReleasePackage skip-unloaded …`（说明仍有未 load 子包，但不再风暴）
- **不应**再出现连续 `INVALID` 步进 +4 ×~60k

## 已排除（不是根因）

| 假设 | 证据 |
|------|------|
| logcat / hook 吞吐 | 限流与 `exclude_vm_pool` 后第一次仍 ~4.8s |
| `uc_emu_stop` 掐断 | 卡死 DoLoading |
| 仅 MF_Free / 内存管理 stub | `pc` 落在 DF_DATAPACKAGE Release |

## 曾做的缓解（非根因）

INVALID 限流、池外快拒、取消全池 code hook — 保留无害，但不替代上述契约修复。
