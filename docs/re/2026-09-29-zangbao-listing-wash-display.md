# 藏宝阁浏览详情显示未洗练

日期：2026-09-29

## 现象

上架已洗练装备后，浏览/我的详情走 `buildEquipDetailLines`，强化附加档显示为默认（黄），无 `[洗]`、无红色洗练行。

## 根因

- 上架已把背包 `enhance_wash` 写入 `server_zangbao_listings`（成交/领取也正确跟随）。
- `GET /api/zangbao/browse|mine` 只回 `enhanceLevel`，未回 `enhance_wash` / 宝石孔。
- `listingAsBagItem` 无 `enhanceAttrs` → 详情回退本地默认档。

与物品竞拍对比：拍卖列表在 `enhanceLevel` 后带 bag common-extra（含 wash bit7）。

## 修复

1. browse/mine SELECT 增加 `enhance_wash,gem_reflect,gem_combo,gem_revive`，JSON 字段 `enhanceWash` / `gemReflect`…
2. Pixi `enhanceAttrsFromWashText` 解析四档；`listingAsBagItem` 注入详情；列表加 `[洗]`。

## 验证

上架一件已洗装备 → 浏览进入详情 → 洗练档应为红色且数值与背包一致；列表名带 `[洗]`。
