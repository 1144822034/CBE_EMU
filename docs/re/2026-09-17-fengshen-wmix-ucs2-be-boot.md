# 2026-09-17 封神演义V2 CBE 启动闪退（wmix / UCS-2 BE / DF_WriteShort）

## 现象

模拟器启动 `CBE/封神演义V2.CBE` 后很快宿主 assert。  
早期日志：`vMAssert(vmdlEnter\vmdlEnterNet.c:154)`，此前仅有  
`http_get_mock ... wmix.kvbar.com ... body=1`。

封神演义是**单机** RPG：场景/角色等资源在 CBE 内嵌 DF 包中，无江湖 OL 联机 WT 服。  
启动时的 `wmix` HTTP 属于 CoolBar 平台引导（下载/写入 `dfwmixV40.dat`），不是游戏服。

## 触发

```text
bin/main.exe --cbe="CBE/封神演义V2.CBE" --autotest --no-launcher
```

## 根因链（首次偏离 → 症状）

1. **HTTP stub 过短**  
   `vm_net_queue_http_get_mock_response` 对非 wpay URL 默认回 `{1}`。  
   `vmdlEnterNet` 把响应当成「2 字节头 + payload」；`len=1` 时后续 `file_open` 路径上  
   解析/打开结果 `r6=-1`，触发 `vmdlEnterNet.c:154`。

2. **UCS-2 端序写坏路径（真正打开失败原因）**  
   引导在通过长度检查后，构造路径 `./\dfwmixV40.dat`（UCS-2）：  
   - 客户机 `strh` 写入的前缀是 **BE** 半字（`00 2e 00 2f 00 5c`）  
   - 宿主 `UCS2` 管理器 idx4（GBK→UCS2）/`vMGB2UCS2` 用 `uc_mem_write` 直接写入宿主  
     **UTF-16LE** 字节，后缀变成 `64 00 66 00...`  
   - `vm_read_path_string` / `file_open` 只按 LE UCS-2 或 C 字符串识别 → 读成空路径 →  
     `file_open` 返回 -1 → 仍 assert 在 `:154`。

3. **空 payload 写文件**  
   最小合法 wmix 体 `{1,0}` 的 payload 长度为 0。  
   `vm_cbfs_vm_file_write` 对 `size<=0` 原先返回 `-1`，会在 `:156` 再 assert。  
   （与 `file_read` 对 `size<=0` 返回 0 不一致。）

4. **DF_WriteShort 游标端序（SCR_Render / DF_Record 越界）**  
   wmix 通过后进入 `DoLoading`，`DF_Record`（`0x01018b46`）经 `bx r3` 调用  
   gameutil `DF_WriteShort`（`0x0c00285c`，idx24）把字段写入记录缓冲。  
   - DF **payload** 本身是 LE（与 `DF_WriteInt` 一致）  
   - **offset 游标**在 guest 栈上，必须按 `isBiggianProgram` 端序读写  
   - 旧实现用 `uc_mem_read/write(..., &offset, 4)` 按宿主 LE 写游标：  
     写入 `4` → 内存 `04 00 00 00` → guest `ldr` 读成 `0x04000000`  
   - 随后 `strb [buf, r2]` 访问 `buf+0x4000000`（日志 `900cdb8`）soft-fail，  
     记录损坏，后续 `SCR_Render` 空指针 `UC_ERR_MAP` assert。

## 修改

| 位置 | 改动 |
|------|------|
| `src/main.c` `vm_net_queue_http_get_mock_response` | `wmix.kvbar.com` / `/wmix/` 回 `{1,0}` |
| `src/main.c` UCS2 管理器 idx2/3/4、`vMGB2UCS2` | 经 `vm_write_ucs2_host_le_to_guest` 按 CBE 端序写半字 |
| `src/vmFunc.c` `vm_readStringUCS2ByRegLimited` | 用 `vm_get_var_short` 读 |
| `src/vmFunc.c` `vm_read_path_string` | 优先按 guest 端序半字走路径 |
| `src/vmFunc.c` `vm_cbfs_vm_file_write` | `size<=0` 返回 0 |
| `src/vmFunc.c` `vm_DF_ReadShort` / `vm_DF_WriteShort` | 游标用 `vm_get_var`/`vm_set_var`；payload 仍按 LE |

## 验证

- **已通过（wmix）**：不再出现 `vmdlEnterNet.c:154`；能进入  
  `initDFDataPackage` / `DoLoading`。
- **已通过（DF_Record）**：无 `地址无法访问:...900cdb8` / `SCR_Render异常`；  
  可见 `df_tcard_block ... name=00_palace01.sce` 以及后续 `03_kunlun01.sce`。
- **仍可见（非本阶段）**：autotest 下出现循环  
  `[info][wpay] null BLX at 0x0100bab6 blocked -> inject cancel key`  
  （计费/wpay 空回调），与 DF 记录端序无关，**unresolved**。

## 复测命令

```text
cd bin
set CBE_LOG_MODE=debug
main.exe --cbe="CBE/封神演义V2.CBE" --autotest --no-launcher
```

期望：无 `vmdlEnterNet.c:154`；无 `SCR_Render异常`；出现 `00_palace01.sce`。
