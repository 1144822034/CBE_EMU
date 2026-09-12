# 强化 +1 后默认档「变成随机」（2026-09-12）

## 触发与现象

首获 20% 洗练 seed 服务端正常；未强化（+0）背包详情看像默认黄字档。
对该装备强化成功到 +1 后，强化附加四档突然变成洗练池随机（红字 / 类型数值变了）。

## 业务链路

1. 入包：`enhance_wash_seed_initial` 按 `first_seed_percent` 写入实例
   `enhance_wash`（或保持空 → 配置默认档）。
2. common-extra / 17/1 / 7/7：装备一律下发四档里程碑（含 bit7=washed）。
3. Pixi `openBag` / 7/7 合并 → `GameSession.applyBagEquipDurability`。
4. `equipDetail.enhanceLines`：有 `enhanceAttrs` 用 wire；否则回退本地
   `MILESTONES` 黄字公式。

## 首次偏离

服务端 29/3 只 `enhanceLevel++`，不调用 `seed_initial` / `roll_attr`。

偏离在客户端：`applyBagEquipDurability` 在 `wireEnhance === 0` 时**删除**
wire `enhanceAttrs`（重铸防复活旧洗练时写得过宽）。+0 详情只能画本地默认档；
强化到 +1 后 attrs 被保留，首获 seed 才露出，看起来像「默认档被随机了」。

## 根因

`enhance===0` 时丢弃 wire 里程碑，与
`2026-09-10-enhance-attr-gray-before-plus4.md`（+0 须灰显真实 seed/默认）冲突。
重铸正确契约是：`enhance===0` 时**不要**用 `preferEnhanceAttrs(prev)` 复活旧词条，
而不是丢掉当前 wire。

## 修改

`PixiJS_web/src/game/GameSession.ts` `applyBagEquipDurability`：

- `enhance===0`：保留 wire `enhanceAttrs`（默认档或首获 seed）。
- pending 填补仅在 wire 无 attrs 时使用；仍不 `preferEnhanceAttrs` 到 +0。

## 验证

- [ ] +0 已 seed：强化附加四行灰字，数值/类型已是洗练覆盖（可红字判定）
- [ ] +0 未 seed：四行灰字为配置默认档；强化到 +1 后类型/数值不变
- [ ] 重铸源 `enhance→0`：不复活转移前洗练；目标带走 wash
- [ ] `npx tsc --noEmit`（PixiJS_web）
