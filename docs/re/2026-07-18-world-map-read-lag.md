# 世界地图打开卡顿：数据表小读取开销

## 结论

卡顿不在地图选择协议。运行日志中的 `16/4` 地图确认由
`builtin-teleport-stone-map-confirm` 在约 1–2 ms 的正常请求处理区间内完成；
真正的同步重负载发生在世界地图界面初始化时的客户端本地数据表读取。

## 客户端与资源证据

- `JianghuOL.CBE:LoadItemDataSheets(0x01035C48)` 在地图缓存未就绪时依次调用
  `LoadMapDataSheet(0x0103581E, mode=0/1/4)`。
- `LoadMapDataSheet` 分别加载 `wMap.dsh`、`wMapLine.dsh` 和 `sMap.dsh`；子地图线
  页面还会加载 `sMapLine.dsh`。
- `LoadDataSheetFile(0x01045D28)` 读取 16 字节头和字段头；
  `SeekDataSheetRecord(0x01045C60)` 对每行先读取 4 字节行长，再读取最多
  `0x200` 字节行数据。
- 当前资源行数为：`wMap=27`、`sMap=135`、`wMapLine=26`、`sMapLine=102`。
  单次基础世界地图初始化至少产生约 376 次小文件读取，不含字段头和额外查找。

## 主机侧负开销

旧 `vm_cbfs_vm_file_read` 对每次读取都会：

1. 读取 PC、LR、R0、R1 四个 Unicorn 寄存器，但这些值从未使用；
2. 按本次大小执行 `SDL_malloc`；
3. `fread -> uc_mem_write` 后执行 `SDL_free`。

数百次 4–512 字节读取把寄存器桥接和堆分配固定成本集中到界面打开的一帧，
在子场景较多的世界区域尤其明显。

## 修正

- 删除文件读取热路径中的四次无用寄存器访问和空的 CBM 分支。
- `size <= 0x200` 使用 512 字节有界栈缓冲，覆盖客户端数据表的完整行上限。
- 大于 `0x200` 的资源读取仍使用原有 `SDL_malloc/SDL_free`，不改变大文件语义。
- 文件定位、读指针、返回字节数和 `uc_mem_write` 行为保持不变。

## 验证

- `make -j2` 通过。
- `git diff --check` 通过，仅输出仓库已有的 LF/CRLF 提示。
- mock service 重新监听 `127.0.0.1:19090`，启动 stderr 为 0。
- 该阶段没有新增或修改网络响应；客户端仍通过原生地图数据和既有 `16/4`
  确认链路工作。

