# 未强化到 +4 也显示灰色强化附加（2026-09-10）

## 需求

未强化或强化未到 +4 的装备，详情仍展示四档强化附加，且全部灰色
（`unlock > enhance` → 灰；达到对应档才亮/红字洗练）。

## 根因

Pixi `buildEquipDetailLines` 仅在 `enhance >= 4` 时追加 `enhanceLines`；
服务端 common-extra 也仅在 `L>=4` 才下发四档里程碑。低于 +4 时整块不显示。

战斗加成本就只在 `add_unlocked_milestones` 对 `unlock <= L` 生效，与展示无关。

## 修改

1. `PixiJS_web/src/ui/equipDetail.ts`：装备详情始终 `enhanceLines`；既有
   `locked = unlock > enhance → GRAY` 覆盖 +0..+3 全灰。
2. `mock_server_catalog.c` `seq_put_item_common_extra`：装备一律 wire 四档
   里程碑（含首获 seed / 洗练覆盖），便于灰显真实数值。

## 验证

- [ ] +0 装备详情：强化附加四行全灰；数值与首获 seed / 默认档一致
- [ ] +3：四行仍全灰；+4：第一行亮（或洗练红），后三灰
- [ ] 战斗属性在 +4 前不含里程碑加成（与改前一致）
