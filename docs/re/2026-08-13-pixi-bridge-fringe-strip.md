# PixiJS：桥面 fringe 条带误挡（2026-08-13）

## 现象

部分野外图桥面可见但不能走过。例：`27深渊沼泽_01.sce` 中部横跨水域的桥。

## 根因

MAP 高 nibble 边缘位契约（CBE Thumb `CheckMapMoveCollision*`）为跨格
**exit|entry OR**：

| 方向 | 当前格 exit | 目标格 entry |
|------|-------------|--------------|
| +X | E=`0x8` | W=`0x2` |
| −X | W=`0x2` | E=`0x8` |
| +Y | S=`0x4` | N=`0x1` |
| −Y | N=`0x1` | S=`0x4` |

`27深渊沼泽_01` 桥面两行格子分别为整排 `0x2`(W) 与 `0x8`(E)，夹在南北 `0xF` 水域之间。  
沿桥东移时目标格 entry W 置位 → 每一缝都被挡；从西岸踏上桥同样被挡。

位含义本身与反汇编一致（不是 N/W 标反）。问题在官方 MAP 把桥/岸画成
**同方向单 bit 条带**，严格 OR 会封死条带内部接缝。  
2026-08-09 补上边缘位后，原先只认 `0xF` 时能过的桥开始不能走。

证据：

- 桥心 `gy=21 gx=11..15` flags=`22222`，`gy=22` flags=`88888`
- `mapMoveBlocked` 在修复前：`10→11 … 14→15` 全 `true`
- CBE `0x010451C8` / `0x0104525C`：`lsrs #28` 后 `lsls`/`bmi` 测 exit|entry

## 修改

`PixiJS_web/src/resources/map.ts` — `mapFringeStripSeam`：在 raw OR 为挡时，若属于条带伪缝则放行：

1. 两侧 flags 相同且为单一方向 bit；或
2. `0 ↔ 单方向 bit`，且沿移动方向再看一格仍是同一 fringe（条带延续）

`0xF` 实心与普通 onesided/twosided 悬崖缝仍挡。

## 验证

1. `27深渊沼泽_01`：可从西岸走上桥并走到东岸；南北仍不能踏入水域。
2. 祥龙峰等穿山用例：非条带边缘缝仍不能直线穿。
3. `npx tsx scripts/probe-swamp-bridge-verify.ts` → PASS
4. `npx tsc --noEmit`（PixiJS_web）
