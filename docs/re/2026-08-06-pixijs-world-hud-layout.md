# PixiJS 主界面 HUD 排布（对照原版 CBE 截图）

日期：2026-08-06  
证据：`PixiJS_web/reference-shots/000017_00025672-game.png`、`000018_00027182-game.png`（由 `bin/autotest/screens` BMP 裁掉 32px 工具条，LCD **240×400**）  
测量脚本：`PixiJS_web/scripts/measure-hud-shot*.ps1`、`probe-hud-boxes.ts`、`probe-sizes.ts`

## 垂直分带

| 带 | y（含） | 说明 |
|----|---------|------|
| 地点 | 0–27 | **全宽**暗条 + UC3 居中地名 |
| 状态橄榄条 | 28–63 | 头像框 + `tiao.gif` + `UIsh`×5 |
| EXP | 63–71 | `uiexp.gif` + `numberexp` + `exp0`/`exp1` |
| 地图 | 72–341 | `SceneView`；左上角「修」叠在地图上（非顶栏） |
| 软键 | 342–363 | `UIxia1` / `UI7`+挂机 / `UIxia3`（**无**背包计数） |
| 聊天 | 364–399 | 两行黄字频道前缀 |

常量：`WORLD_TOP_H=72`，`WORLD_BOTTOM_H=58`，`MAP_VIEW_H=270`（`src/ui/constants.ts`）。

## 状态行水平槽位（截图像素）

| 元素 | 坐标 / 尺寸 | 资源 |
|------|-------------|------|
| 头像框 | x≈3，y≈37，**25×25**；八角双色金边 | **`handkuang.gif`**（与装备槽同资源；内脸 19×19 pad=3） |
| HP/SP | x=34，y=38；`tiao` **66×19**；黑槽 fill x18 w36，HP y4 / SP y12 h4 | `tiao.gif` + Graphics 填色 |
| 快捷×5 | x0=**91**，pitch=**31**，y=**39**，**21×21** | `UIsh1..4` + `UIsh6`（元宝） |
| EXP 徽章 | y=63；**39×13** | `uiexp.gif` + `numberexp.gif`（等级两位数） |
| EXP 槽 | x≈40，相对徽章 y+5；track **202×4** / fill **200×2** | `exp0.gif` / `exp1.gif` |

金框底边簇（y=58）：`91–109, 122–140, 153–171, 184–202, 215–233` → pitch 31。

## 底栏

- `UIxia1.gif` 25×16 左；`UI7.gif` 49×22 偏左中 +「挂机」；`UIxia3.gif` 25×16 右。
- 参考图软键行**没有**「背包: n / m」；背包经菜单进入。`setBagSlots` 保留为空操作以免断 session 钩子。
- 「修」：`UIRed`/`training` 画在 `SceneView` 地图角，不进顶栏。

## 已排除误用

- `ui_selectchar.gif`：选角框；金环相似但外圈蓝光会盖住橄榄状态条  
- `UI2.gif` 88×33：会盖住状态行  
- 顶栏把地图整段不透明垫高到 62 却把 EXP 挤进地图：已改为 72 并单独画橄榄条/EXP 底  

## 名牌与聊天字体

**约定（`PixiJS_web/AGENTS.md`）**：项目内全部 UI 文字走 `font_gb.uc3`，禁止浏览器字体。

| 用途 | 资源 |
|------|------|
| 地图名条 | 全宽 `drawLocBanner` + UC3 白字 |
| 玩家称号（头顶） | `riches_name0..9.gif` / `level_name0..12.gif`（actorinfo `previewImage`） |
| 玩家名 / NPC / 怪物 / 世界聊天 / 软键 / 菜单 | `font_gb.uc3` |

颜色对照截图：玩家名黄 `0xf8dc00`，NPC 青 `0x80e8ff`，怪物红 `0xff6060`，聊天黄 `0xf8dc00`。

## 验证

1. 进图对照 `000017`/`000018`：头像 `handkuang`–tiao–五图标间距、EXP「NN EXP」+ 紫条、底栏三键、聊天黄字。
2. 头顶称号应为 GIF 条（非红色系统字体）；NPC「黄蓉」为字库青字。
3. `cd PixiJS_web && npx tsc --noEmit`
4. VIP 冠：勿用 `vip_effect.gif`（实为「斗」状态格）；队长仍用简易 Graphics。
