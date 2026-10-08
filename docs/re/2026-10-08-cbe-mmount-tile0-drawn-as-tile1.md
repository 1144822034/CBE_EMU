# CBE 户外沙地画出 tile1 紫纹（相对 Pixi）

日期：2026-10-08

## 现象

金蛇谷等 `m_mount.gif` 场景：CBE 逻辑 LCD（240×400）地面出现紫/近黑锯齿斜纹；Pixi 同图为干净沙地 + 浅色 “+” 抖动。

## 证据

- 地图 cell：`ti=0`（沙地）占绝大多数；compose 与 sheet tile0 对齐 mse≈0。
- CBE 同视野 16×16 块与 sheet **tile1**（src 16,0）对齐 mse≈0.37；非全局 +1（ti=7 等其它格正确）。
- `gifDecode` / Pixi LKV2 解码后 tile0 均为沙地（首像素 `0xC52F`，4 色）；解码本身无误。
- `DrawMapTileLayer`（`0x01004E9C`）从 tiledef 取 src 矩形再 blit，不是简单的 `ti*16`。

## 根因陈述（可检验）

触发：进入 `m_mount` 户外图且视野内有大量 `ti=0`。  
被违反契约：packed tileIndex 0 应对应 sheet 左上 16×16 沙地。  
首错状态：guest 对该格使用的源矩形落在 tile1（16,0）。  
宿主侧曾另有风险：`DrawImage` 在 `dst_w > LCD_WIDTH` 时把合法离屏目标打回屏幕，且行缓冲仅 480 字节。

## 修改

1. `vmFunc.c`：指纹识别 `m_mount`（80×112 且 `px[0]==0xC52F`、tile0 四色、与 tile1 差异≥150），登记像素基址；`DrawImage*` / `FillRectWithImageTiled` 将采样自 (16..31, 0..15) 的源 X 减 16，落到 tile0。  
2. `DrawImage`：仅在 screen struct / 空目标时回退屏幕；允许离屏宽高；行拷贝分块缓冲。  
3. 实现原 `assert` 的 `vMFillRectWithImage` / `Ex`（铺砖 blit）。

故意 `ti=1` 的格在 CBE 上会变成沙地（金蛇谷_03 仅 16 格）；Pixi 不受影响。

## 验证

- `make -j2`：`bin/main.exe` / server 已通过（2026-10-08）。  
- 离线脚本 `PixiJS_web/scripts/tmp-verify-mmount-remap.mts`：指纹 true；remap 后 tile1 矩形≡tile0；compose 金蛇谷_01..04 近邻格 prefer0≫prefer1（例 _03：425 vs 16）。  
- 重进金蛇谷_03：沙地区域无大面积紫纹，应接近 Pixi / compose tile0。  
- 其它 80×112 图集（`m_city1` 等首像素非 `0xC52F`）不应被 remap。

## 仍未知

guest 为何把 tiledef[0] 指到 (16,0)（加载流 off-by-one vs 固件特判）未在宿主内改 guest 代码；若后续 IDA 钉死加载点，可去掉 remap，只保留 DrawImage/FillRect 硬化。
