# 标题页露出「修/拍/擂/娱/翅/骑/称」状态条（2026-08-30）

## 现象

未登录的标题菜单上出现地图角状态图标条（« 修 拍 擂 娱 翅 骑 称）。白点方向键开关是正常的，应保留。

## 根因

`SceneView.nudgeAfterGpuResume()`（黑屏/GL 恢复）无条件执行 `this.root.visible = true`。  
标题阶段 `showPhase("splash")` 本把 `view.root` 藏起；GPU resume 又把它打开，状态条叠在 `Background.gif` 上。

与系统状态栏 / HTML 白点无关。

## 修改

`SceneView.nudgeAfterGpuResume`：只恢复 `world` / 贴图，**不再**强行 `root.visible = true`；显隐仍由 `main.showPhase` 拥有。

## 验证

1. 标题菜单：无 修/拍/… 条；右上白点仍在。
2. 进世界：状态条正常；收起/展开仍可用。
3. 世界里触发黑屏恢复后：状态条仍在；若在标题时恢复：仍无状态条。
