# PixiJS：JHOnlineData UI chrome（装备栏图标 + HUD）

日期：2026-08-10  
范围：装备信息槽位图标框、HUD VIP/挂机剑；**不改字体**；背包保持品质色文字列表。

## 资源契约

| 资源 | 尺寸 | 用途 |
|------|------|------|
| `equipico.gif` | 160×16 = 10×16×16 | `equip.dsh` 类别 0–9 → tile |
| `handkuang.gif` | 25×25 | **HUD/战斗头像框** 与装备槽共用（八角双色金边） |
| `vip_effect.gif` | 17×17 | **「斗」状态图标**（挂机/战斗类），**不是** VIP 皇冠；勿贴头像 |
| `training.gif` | 17×17 | **「练」** 状态图标 |
| `sword.gif` | 95×19 | （已不用）原挂机软键左侧图标 |
| `ui_h_*` / `ui_hw_*.actor` | actor | HUD 头像脸图（无金边） |
| `jin` / `yin` / `tong.gif` | — | 背包金钱栏（既有） |
| `UIsh1..4` / `UIsh6` | — | HUD 快捷五键（既有） |

类别 → tile：与 `equip.dsh`「类别」列一致（0 帽 … 6 饰，7/8/9 剑/匕/杖）。  
空槽默认类别：`defaultCategoryForBodySlot`（武器空槽 → 7 剑）。

**不**把 `item.dsh`「形象」当作 `equipico` 下标（多数为 `-1`；背包绘制为 DrawString 列表）。

## 头像框（更正）

先前文档把 `handkuang` 排除为「仅道具格」是误判。CBE 字串池无独立 face-frame 名；对照 `reference-shots/000017_00025672-game.png`：

- `handkuang.gif` @ LCD `(3,37)`：金像素 **97/100** 命中，蓝底对橄榄条泄漏极少
- `ui_selectchar.gif` 金环也像，但外圈蓝光泄漏到橄榄底 → 排除
- 脸图来自 `ui_h_*` actor，框与脸分层绘制

## 地图角状态条（修/拍/斗/练）

顺序（左→右，pitch 20，17×17）：

| 槽 | 资源 | 说明 |
|----|------|------|
| 修 | 深褐底 + 米描边 + UC3「修」 | **不用** `UIRed.gif` 亮红底 |
| 拍 | 深蓝底 + 青描边 + UC3「拍」 | 同上风格 |
| 斗 | `vip_effect.gif` | 挂机时显示 |

组队左上角队友条：`WorldHud.partyHost` 须从 `WORLD_TOP_H + (4+17) + 2` 起画，避开状态条（HUD 叠在 map 之上，贴 `WORLD_TOP_H+1` 会挡住图标）。
| 练 | `training.gif` | 预留，待训练 buff 接线 |

证据：`vip_effect`/`training` 为自带字的完整状态格；亮红 `UIRed` 作底板会盖住字形。

## 修改点

- `PixiJS_web/src/ui/ItemArt.ts` — `warmEquipArt` / `mountEquipSlot` / 裁切 API
- `PixiJS_web/src/resources/itemCatalog.ts` — `equipCategoryIconIndex` / `defaultCategoryForBodySlot`
- `PixiJS_web/src/ui/SystemPanel.ts` — 穿戴栏 25×25 框+图标；替换列表行首 16px 图标
- `PixiJS_web/src/ui/WorldHud.ts` — `handkuang` 头像框；挂机软键仅 `UI7`+「挂机」文案（不再叠 `sword.gif`）；**不**再把 `vip_effect` 当 VIP 冠
- `PixiJS_web/src/ui/BattlePartyHud.ts` — 战斗底栏头像同用 `handkuang`

## unresolved

- **穿戴栏 blit 路径**：资产与类别对齐；完整 IDA 闭环未再展开（槽名表 /「未装备」文案既有文档）。
- `handkuang.gif` @ CBE `0x043ac6` 的 HUD xref 未强制核对（像素证据已够改客户端）。

## 验证

1. 装备信息：8 槽为 `handkuang` + 类别图标；穿戴后随 `equip.dsh` 类别变化；坏耐久红光仍在。
2. 替换列表：行首 16px 图标 + 品质色名。
3. HUD：`handkuang` 八角金框 + 挂机 `UI7`/`sword` + `UIsh` 五键；头像不被「斗」挡住。
4. 背包仍为文字列表 + 金钱三图标。
5. `cd PixiJS_web && npx tsc --noEmit`
