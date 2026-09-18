# 2026-09-18 原罪天使剧情后异常退出（RWPI softfloat slot0）

## 现象

剧情推进加载 `02fallstar_09` / `01heaven_03.sce` 后进程退出。用户日志可见
`DoLoading` 后 `quit requested: window_close`；完整 `client_out.txt` 在
`SCR_Render` 前先出现：

```text
地址无法访问:0 type:0 size:19 value:4
pc:10274bc lr:1027464 r9:1029e6c
r6 → "01heaven_03.sce"
SCR_Render异常:Invalid memory mapping (UC_ERR_MAP)
```

软故障 `uc_emu_stop` 后界面停死，关闭窗口即表现为「异常退出」。

## 触发

```text
cd bin
main.exe
# 启动器进入原罪天使，推进到会加载 heaven_03 / fallstar 的剧情
```

## 根因链（首次偏离 → 症状）

1. **BE RWPI softfloat 取 FP status 地址**（`0x010274b8`）：
   ```text
   ip = *[r9]
   ip = *[ip]
   r0 = ip + 0xe980   ; 调用方再 +4 → data_base + 0xe984
   ```
   同族调用在 `0x01027774` 等，属浮点异常状态字路径。

2. **BSS 首字（slot0）为 0**：镜像为 `0,1,2,3,...`。`*[r9]==0` 时第二下
   `ldr ip,[ip]` 访存地址 0。

3. **不能写死自指**：`sb+0` 还被模块对象路径使用（`ldr =0; add sb` 后把
   `&slot0` 交给库例程；`ldr [sb+lit]` 再取对象）。把 slot0 写成
   `Program_Data_Address` 会在启动期触发 `DF_PictureLibrary.c:95`。

4. **首次偏离**：`heaven_03.sce` 触发浮点 → NULL 解引用 → soft-fail →
   `SCR_Render` 再崩 → 用户关窗。

## 修改

| 位置 | 改动 |
|------|------|
| `src/main.c` `vm_cbe_scan_rwpi_fp_status_helper` | 扫描 BE softfloat helper 机器码与 literal |
| `src/main.c` `hook_rwpi_fp_status_helper` | 仅当 `*[r9]==0` 时返回 `data_base+literal` 并直接返回；已重定位则放行 |
| 三处 BSS 加载之后 | scan + install 窄范围 `UC_HOOK_CODE` |

## 验证

- 启动日志：`[info][cbe] rwpi fp-status helper=010274b8 literal=0000e980`
- 无 `DF_PictureLibrary.c:95`（对比已证伪的自指方案）
- 剧情过 `01heaven_03.sce` 后无 `地址无法访问:0` / `SCR_Render异常`

## 复测命令

```text
cd bin
set CBE_LOG_MODE=debug
main.exe --cbe="CBE/原罪天使(大型RPG)V100.CBE" --no-launcher
# 推进到原崩溃剧情点
```

## unresolved

- 真机 CoolBar 何时/如何填充 slot0 模块指针仍未知；当前仅在未重定位时兜底 FP status 地址。
