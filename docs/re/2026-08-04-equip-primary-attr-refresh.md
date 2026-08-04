# 2026-08-04 穿脱装备力量/敏捷/智慧/护甲/物攻不刷新

## 现象

穿上或卸下带三围里程碑的装备后，属性页力量/敏捷/智慧/护甲不立刻变化；
穿脱武器时物攻也不刷新。气血/法力可随 wear-apply 变化。

## 根因

1. **Wear-apply jump 与名表错位**（`0x01010030`）  
   | 名表 type | 语义 | jump 写入 |  
   | --- | --- | --- |  
   | 1 | 力量 | `+0x84`（魅） |  
   | 2 | 敏捷 | `+0x122`（力） |  
   | 3 | 智慧 | `+0x124`（敏） |  
   | 4 | 物攻 | `+0x130` |  
   | 5 | 护甲 | `+0x132` |  
   无 type 写 `+0x126` 智慧。力/敏 wire 已 remap 为 2/3 以便 jump 落半字；
   详情名表会跟着偏（力→「敏捷」）。智慧仍只靠 actorinfo。

2. **物攻不在六字里**  
   `parse_actorinfo` 六字写 `+122/+124/+12a/+126/+132/+12c`，**没有** `+0x130`。  
   面板物攻只来自武器 `item+0xFA` 的 wear-apply（`0x01028B34`），类别字节
   `item+0x11A` ∈ {7,8,9}（或 0xF 且子类 7/8/9）。

3. **曾用登录全量 blob 做 `1/1/14`**  
   `parse_actorinfo(a2!=0)` 会跳过若干 **read**（不消费 wire）：  
   `summaryStatus`、`primaryBaseMax`、`secondaryBaseMax`、money/背包两字节、  
   `field11E/120`、preview 串、sceneKey、motion 对。  
   EXTRA132（魅力）与六字**始终读取**。  
   全量登录 blob 会错位，六字（力/敏/智/甲）被污染；延迟 map vitals 若仍发
   全量 blob，会把刚 wear-apply 好的三围/甲冲坏。  
   物攻半字虽不被 actorinfo 写入，但面板字符串依赖 host realign；三围/甲错位
   时用户会一并报「物攻也不动」。

## 修复

- `build_actor_info_ex(..., liveRefresh)`：按 `0x0100FA88` live 子集组包；  
  live 护甲 word 发 **绝对** `bare + equipment.armor`（apply 之后覆盖）；  
  登录仍发裸护甲种。
- `append_shop_actor_state14`（商城/map vitals/社交通知还原）一律 live 子集。
- `7/8` / `7/9` 成功：同包追加 live `1/1/14`，仍不追加登录 `7/7 type=2`；  
  保留延迟 map vitals。
- Host：`0x01010224` wear-apply 尾声 + `InitSceneViewport` 尾声 realign 物攻行。

## 验证

1. 裸装记下力/敏/智/甲/攻 → 穿 +16 品质4 上衣：力量应涨。  
2. 披风/鞋：敏捷；帽子：智慧。  
3. 武器：物攻随 `item+0xFA` 涨/卸回 0。  
4. 卸下后三围/甲回到裸装。  
5. 日志：`mock_item_equip ... resp=7/8+1/1/14`、`mock_actorinfo_attrs ... live=1`。  
6. `make -j2`

## 残留

- Jump 仍会短暂误写魅/力/敏；同包 live `1/1/14` 随后覆盖。  
- 力/敏 wire remap 使详情名称偏一格（未改）。  
- live 绝对护甲之后若客户端再次 wear-apply 且不重置半字，存在叠加风险
  （与官服路径是否每次从裸种重建有关；若复现再取证 `0x0100FED8` 重置点）。
