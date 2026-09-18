# UC3 点阵字体下状态栏 / 名牌右下错位（2026-09-17）

## 现象

玩家在菜单手动切到天空点阵 / 全字库 / 经典子集后：

- 地图角状态条「修/拍/擂/…」字落在色板右下，几乎贴边
- 头顶名牌（称号 + 角色名、怪名）整体偏右下，名字左缘约在角色中轴

地点条「太乙峰」等用 `anchor.set(0.5, …)` 走 sprite pivot 的仍大致居中。

## 根因

`BitmapLabel` 宋体 DOM 路径用 `boundsArea` 提供布局宽高。切到 UC3 时 `rerender` 会先 `hideDomLabel()`，原先写成：

```ts
this.boundsArea = new Rectangle(0, 0, 0, 0);
```

Pixi v8 `getLocalBounds` 对任意 **truthy** `boundsArea` 直接采信、不再量子节点。空矩形使 `label.width` / `label.height` 恒为 **0**。

UC3 `rerender` **每次**都会调用 `hideDomLabel()`，因此冷启默认点阵也会中招，不单是从宋体切过去。

居中公式随之崩坏：

| 位置 | 计算 | 实际 |
|------|------|------|
| 状态条 17×17 槽 | `floor((17 - width) / 2)` | `floor(17/2)=8` → 14px 字从 (8,8) 起画，右下溢出 |
| 名牌 | `-floor(width / 2)` | `0` → 左缘钉在角色中轴，整串偏右 |

地点条用 `spr.width` 设 pivot，不读 `Container.width`，所以看起来正常。

## 修改

1. `hideDomLabel` / `showSprite`：清除 `boundsArea`（`undefined`），恢复按 sprite 量宽高
2. 状态条文字改为 `anchor(0.5,0.5)` + 槽心坐标，rebake 后仍居中
3. 字体切换后 `SceneView.remountAfterFontChange` 重挂状态字并 `relayoutAllNameplates`

## 验证

1. 系统设置切到「天空点阵」：状态字在色板正中；自己/怪名牌相对精灵居中
2. 再切回「系统宋体」再切点阵：无右下回退
3. 地点条、面板正文无回归错位
