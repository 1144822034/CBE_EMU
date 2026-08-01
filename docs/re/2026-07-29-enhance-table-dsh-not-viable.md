# 能否把强化表做成 JHOnlineData 新 .dsh？（2026-07-29）

## 结论

**不能。** 仅向 `JHOnlineData` 增加一个新的 `.dsh`（或经 `18/7` 下发同名文件），
**不会**让客户端填上 `global+0x580+4` 的强化缩放表。

原因分三层：

1. **客户端只按硬编码文件名打开已知表**，不会扫描目录里所有 `.dsh`。
2. **整棵 CBE + 全部 mm*.cbm 里都没有**强化表相关的 `.dsh` 名。
3. **`0x01028BCE` 要的是裸 `flat/pct` 数组指针**，与 DataSheet 行列格式不是同一套解析器。

## 证据

### 1. 磁盘上已有的 `.dsh`

`web/fs/JHOnlineData` / `bin/JHOnlineData` / 官包 `JHOnlineData` 均为这 10 个：

| 文件 | 谁用 |
| --- | --- |
| `equip.dsh` `item.dsh` `skill.dsh` `eidolon.dsh` | 客户端 CBE/CBM |
| `wMap.dsh` `wMapLine.dsh` `sMap.dsh` `sMapLine.dsh` | 客户端（地图） |
| `automonster.dsh` `task.dsh` | **仅服务端** mock 使用；CBE/CBM **无**字符串引用 |

### 2. 客户端二进制里的 `.dsh` 名（穷尽）

`江湖OL.CBE` 可打印串：

```text
equip.dsh  item.dsh  skill.dsh  eidolon.dsh
wMap.dsh  wMapLine.dsh  sMap.dsh  sMapLine.dsh
JHOnlineData\%s
```

各 `mmTitle/mmGame/mmBattle/mmShop*.cbm` 额外出现的仍只有上述集合
（无 `enhance*.dsh` / `qh*.dsh` / `streng*.dsh`）。

路径格式 `JHOnlineData\%s` 只是「在目录下打开**调用方传入的名字**」；名字来自
代码里的字面量或已知资源类型（sce/map/actor/gif…），不是「加载目录下全部 dsh」。

### 3. 更新通道也不能「塞进未知表就自动用」

`docs/re/2026-07-19-game-content-update-workflow.md`：

- 命名资源走 `18/7`，**按需**：发布了客户端也不会在启动时主动要；
- 要启动即装，需打进对应 **CBM** 模块。

因此即便把 `enhance_scale.dsh` 放进 catalog 并能下到 `JHOnlineData/`，
只要没有「打开该名 → 解析 → `str` 到 `+0x580+4`」的代码，表指针仍是 null。

### 4. 格式也不匹配 DataSheet

强化缩放消费端（`0x01028BCE`）：

```text
table = *(obj + 0x580 + 4)   // 指针
entry[i] = { u8 flat, pad, i16 pct }  // 定长 4 字节 × L
```

而 `LoadDataSheetFile(0x01045D28)` 读的是 16 字节头 + 字段头 + 变长行记录
（见 `2026-07-18-world-map-read-lag.md`）。没有现成桥把「新 dsh 行」转成上述数组。

与本服 `M(L)` 对齐的裸表定义见 `2026-07-29-enhance-scale-table-ml.md` /
`docs/re/assets/enhance_scale_table_ml.bin`。

## 对「加资源」方案的判定

| 做法 | 结果 |
| --- | --- |
| 只在 `JHOnlineData` 丢一个新 `.dsh` | 客户端不会打开 → 无效 |
| 经 `18/7` 下发该文件 | 文件可落地，仍无人打开/无人写 `+0x580+4` → 无效 |
| 改现有 `equip.dsh` 加列 | 装备加载路径不写强化表；主行仍用空表 → 无效（且会破坏既有列契约） |
| 改 CBE/CBM 加加载逻辑 | 禁止（规范：不改客户端逻辑/全局） |

## 仍可做的方向（合法）

1. **本构建已无静态写入点**（见 `2026-07-29-enhance-scale-table-no-writer.md`）；
   再取证只为对照其他版本包或运行时确认全程为 0，而不是期待现有 CBE 突然装表。
2. **保持服务端权威**：穿戴 `actorinfo` / 战斗 / 商店查看继续用 `M(L)`。
3. 不要先造孤立 dsh；其它版本若真有 writer/资源名，再按契约接。

## 验证（本轮）

- [x] 枚举三份 `JHOnlineData` 的全部 `.dsh`
- [x] 扫描 `江湖OL.CBE` + 全部 `mm*.cbm` 的 `.dsh` 字符串
- [x] 对照更新通道「命名资源按需、不自动消费」契约
- [x] 对照 `0x01028BCE` 表项布局 ≠ DataSheet 行格式
