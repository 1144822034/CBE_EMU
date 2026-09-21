# 装备栏操作后中心立绘消失

日期：2026-09-19

相关：`2026-08-07-pixijs-system-panels.md`、`2026-09-16-equip-detail-alphamode-bitmaplabel-destroy.md`

## 现象

装备信息面板打开时中心有职业/性别全身立绘；穿戴 / 卸载 / 替换（以及宝石弹层往返）后立绘空白，关闭再打开才恢复。

## 根因

中心立绘是 `SystemPanel.equipActorTex` 一次性缓存，**只**在 `openSystem("equip")` → `setEquipActor` 时加载。

装备操作后的 `refreshEquip` 只拉穿戴 + 背包并 `setWorn` / `setBag` → `redraw`，**从不**重载立绘。面板重绘依赖 `equipActorTex` 仍存活。

首次偏离：装备详情大量重建 `BitmapLabel` 易触发 alphaMode strip → `invalidateUiGpuTextures` → `nudgeAfterGpuResume`，该路径**主动 destroy 并清空** `equipActorTex`，注释写明「等下次 `setEquipActor`」，但装备操作刷新从不调用。随后 `drawWornEquipPanel` 跳过中心 Sprite，面板仍停留在 `kind === "equip"`。

同类缺口：宝石弹层 `showPhase("dialog")` → `system.hide()` 销毁立绘；`returnFromGemPick` / `refreshAfterGem` 只 `system.open(panel)`，同样不调 `setEquipActor`。

重新打开恢复，是因为 `openSystem("equip")` 是唯一会再次加载立绘的正常入口。

## 修改

| 点 | 行为 |
|----|------|
| `SystemPanel` | 记住 `job/sex`；`equipActorTexAlive`；`ensureEquipActorTex` 在 GPU 恢复与 `drawWornEquipPanel` 自愈重载 |
| `setEquipActor` | 缓存命中要求纹理 GPU 仍可用，避免死贴图早退 |
| `main.applyEquipActor` | 打开装备栏、装备操作 `refreshEquip`、宝石返回路径显式重载 |

## 验证

1. 打开装备栏 → 确认中心立绘 → 卸载 / 穿戴 / 替换：立绘应保持或短暂异步重载后恢复，无需关开面板。
2. 穿戴栏镶嵌/拆宝石弹层返回：立绘应在。
3. 故意触发一次 alphaMode strip 后仍停在装备栏：立绘应被 `nudgeAfterGpuResume` / 下次 redraw 拉回。
