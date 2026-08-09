/**
 * CBE bag equip detail lines — catalog paint (equip.dsh) + enhance milestones.
 * Evidence: JianghuOL.CBE 0x01032118 / 0x010325B0; docs/re enhance milestones;
 * screenshots 玄魔古杖 / 梦境魔杖.
 */

import type { BagItem } from "../net/protocolExtras";
import {
  lookupItemSync,
  qualityColor,
  WORN_SLOT_NAMES,
  type ItemCatalogRow,
} from "../resources/itemCatalog";

/** Yellow for +attr values / unlocked milestones (CBE color-4 family). */
export const EQUIP_DETAIL_YELLOW = 0xffff00;
/** Gray for locked enhance rows (CBE color-5). */
export const EQUIP_DETAIL_GRAY = 0x808080;

export type EquipPaintLine =
  | { kind: "text"; text: string; color: number }
  | {
      kind: "kv";
      key: string;
      value: string;
      keyColor: number;
      valueColor: number;
    };

const ATTR_NAMES: Record<number, string> = {
  1: "力量",
  2: "敏捷",
  3: "智慧",
  4: "物攻",
  5: "护甲",
  6: "躲闪",
  7: "命中",
  8: "暴击",
  9: "气血",
  10: "法力",
};

/** Weapon +8 护甲表（CBE/截图 Q4=700；非 mock collect 减半表）. */
const WEAPON_ARMOR_BY_Q = [0, 175, 350, 525, 700] as const;
const HP_MP_FLAT_BY_Q = [0, 150, 300, 450, 600] as const;
const ATTACK_BY_Q = [0, 23, 50, 123, 296] as const;
const ARMOR_BY_Q = [0, 87, 175, 262, 350] as const;

type MilestoneDef = { type: number; flag: number };

/** slot → +4/+8/+12/+16 defs (mirror mock_server_catalog.c). */
const MILESTONES: MilestoneDef[][] = [
  // 0 武器
  [
    { type: 8, flag: 0 },
    { type: 5, flag: 0 },
    { type: 9, flag: 0 },
    { type: 1, flag: 0 },
  ],
  // 1 帽子
  [
    { type: 5, flag: 0 },
    { type: 9, flag: 0 },
    { type: 10, flag: 1 },
    { type: 3, flag: 0 },
  ],
  // 2 上衣
  [
    { type: 8, flag: 0 },
    { type: 9, flag: 1 },
    { type: 10, flag: 1 },
    { type: 1, flag: 0 },
  ],
  // 3 披风
  [
    { type: 8, flag: 0 },
    { type: 6, flag: 0 },
    { type: 9, flag: 0 },
    { type: 2, flag: 0 },
  ],
  // 4 腰带
  [
    { type: 7, flag: 0 },
    { type: 5, flag: 0 },
    { type: 9, flag: 1 },
    { type: 1, flag: 0 },
  ],
  // 5 护腿
  [
    { type: 6, flag: 0 },
    { type: 5, flag: 0 },
    { type: 4, flag: 0 },
    { type: 2, flag: 0 },
  ],
  // 6 靴子
  [
    { type: 6, flag: 0 },
    { type: 5, flag: 0 },
    { type: 9, flag: 0 },
    { type: 2, flag: 0 },
  ],
  // 7 戒指
  [
    { type: 8, flag: 0 },
    { type: 4, flag: 0 },
    { type: 7, flag: 0 },
    { type: 3, flag: 0 },
  ],
];

function qualityTier(q: number): number {
  if (q < 1) return 1;
  if (q > 4) return 4;
  return q;
}

function weaponPrimaryType(category: number): number {
  if (category === 8) return 2; // 敏
  if (category === 9) return 3; // 智
  return 1; // 力
}

function jobNameForEquip(cat: ItemCatalogRow): string {
  if (cat.equipSlot === 0) {
    if (cat.category === 8) return "幻剑";
    if (cat.category === 9) return "鬼道";
    return "天机";
  }
  return "无";
}

function typeNameForEquip(cat: ItemCatalogRow): string {
  const slot = cat.equipSlot ?? -1;
  if (slot === 0) return "武器";
  if (slot >= 1 && slot <= 7) return WORN_SLOT_NAMES[slot] ?? "装备";
  return "装备";
}

function milestoneType(
  cat: ItemCatalogRow,
  slotIndex: number,
  defType: number,
): number {
  if ((cat.equipSlot ?? -1) === 0 && slotIndex === 3) {
    return weaponPrimaryType(cat.category);
  }
  return defType;
}

function milestoneValue(
  cat: ItemCatalogRow,
  unlock: number,
  type: number,
  flag: number,
): number {
  const q = cat.quality | 0;
  const tier = qualityTier(q);
  const q1 = q + 1;
  const slot = cat.equipSlot ?? -1;

  if (slot === 0) {
    if (unlock === 12 && type === 9 && flag === 0) return q1 * 600;
    if (unlock === 16 && (type === 1 || type === 2 || type === 3)) return q * 422;
    if (unlock === 8 && type === 5) return WEAPON_ARMOR_BY_Q[tier]!;
  }

  switch (type) {
    case 8:
    case 6:
    case 7:
      return q1;
    case 1:
    case 2:
    case 3:
      return q1 * 120;
    case 9:
    case 10:
      if (flag !== 0) return q1 * 5;
      return HP_MP_FLAT_BY_Q[tier]!;
    case 4:
      return ATTACK_BY_Q[tier]!;
    case 5:
      return ARMOR_BY_Q[tier]!;
    default:
      return 1;
  }
}

/** Main combat attrs from equip.dsh (only non-zero columns), CBE order. */
function mainStatLines(
  cat: ItemCatalogRow,
  qColor: number,
): EquipPaintLine[] {
  const rows: { key: string; n: number }[] = [
    { key: "力量", n: cat.strength ?? 0 },
    { key: "敏捷", n: cat.agility ?? 0 },
    { key: "智慧", n: cat.wisdom ?? 0 },
    { key: "物攻", n: cat.attack ?? 0 },
    { key: "护甲", n: cat.armor ?? 0 },
    { key: "躲闪", n: cat.dodge ?? 0 },
    { key: "命中", n: cat.hit ?? 0 },
    { key: "暴击", n: cat.crit ?? 0 },
    { key: "抗性", n: cat.resist ?? 0 },
    { key: "气血", n: cat.hpChange ?? 0 },
    { key: "法力", n: cat.mpChange ?? 0 },
  ];
  return rows
    .filter((r) => r.n !== 0)
    .map((r) => ({
      kind: "kv" as const,
      key: `[${r.key}]:`,
      value: `+${r.n}`,
      keyColor: qColor,
      valueColor: EQUIP_DETAIL_YELLOW,
    }));
}

function enhanceLines(cat: ItemCatalogRow, enhance: number): EquipPaintLine[] {
  const slot = cat.equipSlot ?? -1;
  if (slot < 0 || slot >= MILESTONES.length) return [];
  const defs = MILESTONES[slot]!;
  const out: EquipPaintLine[] = [
    {
      kind: "text",
      text: `强化附加: (${enhance}/16)`,
      color: EQUIP_DETAIL_YELLOW,
    },
  ];
  for (let i = 0; i < defs.length; i++) {
    const unlock = (i + 1) * 4;
    const def = defs[i]!;
    const type = milestoneType(cat, i, def.type);
    const value = milestoneValue(cat, unlock, type, def.flag);
    const name = ATTR_NAMES[type] ?? "?";
    const pct = def.flag !== 0 ? "%" : "";
    const locked = unlock > enhance;
    out.push({
      kind: "text",
      text: `(+${unlock}) ${name}+${value}${pct}`,
      color: locked ? EQUIP_DETAIL_GRAY : EQUIP_DETAIL_YELLOW,
    });
  }
  return out;
}

/** M(L) = base * (1 + (10L + 35*⌊L/4⌋)/100) */
function bonusScale(base: number, enhanceLevel: number): number {
  if (base <= 0 || enhanceLevel <= 0) return base;
  const level = Math.min(enhanceLevel, 16);
  const tiers = Math.floor(level / 4);
  const bonusPct = 10 * level + 35 * tiers;
  return base + Math.floor((base * bonusPct) / 100);
}

export function attackEnhanceDelta(base: number, enhanceLevel: number): number {
  const scaled = bonusScale(base, enhanceLevel);
  return scaled > base ? scaled - base : 0;
}

/** 护甲强化增量 = (M(L)-base)/2 */
export function armorEnhanceDelta(base: number, enhanceLevel: number): number {
  const full = bonusScale(base, enhanceLevel);
  if (full <= base) return 0;
  return Math.floor((full - base) / 2);
}

/**
 * Enhance UI attr rows: `[护甲]: +cur +next` (current enhance vs target L+1).
 * Matches CBE enhance panel (screenshot 传奇腰带).
 */
export function buildEnhanceCompareLines(
  it: BagItem,
  curLevel: number,
  nextLevel: number,
): EquipPaintLine[] {
  const cat = lookupItemSync(it.itemId);
  if (!cat || cat.kind !== "equip") return [];
  const qColor = qualityColor(it.quality);
  const rows: { key: string; base: number; kind: "flat" | "armor" | "attack" }[] =
    [
      { key: "力量", base: cat.strength ?? 0, kind: "flat" },
      { key: "敏捷", base: cat.agility ?? 0, kind: "flat" },
      { key: "智慧", base: cat.wisdom ?? 0, kind: "flat" },
      { key: "物攻", base: cat.attack ?? 0, kind: "attack" },
      { key: "护甲", base: cat.armor ?? 0, kind: "armor" },
      { key: "躲闪", base: cat.dodge ?? 0, kind: "flat" },
      { key: "命中", base: cat.hit ?? 0, kind: "flat" },
      { key: "暴击", base: cat.crit ?? 0, kind: "flat" },
      { key: "抗性", base: cat.resist ?? 0, kind: "flat" },
      { key: "气血", base: cat.hpChange ?? 0, kind: "flat" },
      { key: "法力", base: cat.mpChange ?? 0, kind: "flat" },
    ];
  const out: EquipPaintLine[] = [];
  for (const r of rows) {
    if (r.base === 0) continue;
    let cur = r.base;
    let next = r.base;
    if (r.kind === "armor") {
      cur = r.base + armorEnhanceDelta(r.base, curLevel);
      next = r.base + armorEnhanceDelta(r.base, nextLevel);
    } else if (r.kind === "attack") {
      cur = r.base + attackEnhanceDelta(r.base, curLevel);
      next = r.base + attackEnhanceDelta(r.base, nextLevel);
    }
    out.push({
      kind: "kv",
      key: `[${r.key}]:`,
      value: `+${cur} +${next}`,
      keyColor: qColor,
      valueColor: EQUIP_DETAIL_YELLOW,
    });
  }
  return out;
}

/**
 * Wire after milestones: M(L)-base for 护甲/物攻 (unlock=1).
 * Client cap 6 → after 4 milestones at most 2 extras.
 * Display: `(+1) 物攻+Δ` (CBE 0x01032118).
 */
function scaleExtraLines(cat: ItemCatalogRow, enhance: number): EquipPaintLine[] {
  if (enhance < 1) return [];
  const extras: { name: string; base: number; delta: number }[] = [
    {
      name: "护甲",
      base: cat.armor ?? 0,
      delta: armorEnhanceDelta(cat.armor ?? 0, enhance),
    },
    {
      name: "物攻",
      base: cat.attack ?? 0,
      delta: attackEnhanceDelta(cat.attack ?? 0, enhance),
    },
  ];
  const out: EquipPaintLine[] = [];
  for (const e of extras) {
    if (e.base <= 0 || e.delta <= 0) continue;
    out.push({
      kind: "text",
      text: `(+1) ${e.name}+${e.delta}`,
      color: EQUIP_DETAIL_YELLOW,
    });
  }
  return out;
}

export type EquipDetailOpts = {
  /** Worn panel screenshot omits 类型/职业. */
  omitTypeJob?: boolean;
};

export function buildEquipDetailLines(
  it: BagItem,
  opts: EquipDetailOpts = {},
): EquipPaintLine[] {
  const cat = lookupItemSync(it.itemId);
  const qColor = qualityColor(it.quality);
  if (!cat || cat.kind !== "equip") {
    return [
      { kind: "text", text: it.name, color: qColor },
      { kind: "text", text: `数量 ${it.count}`, color: qColor },
    ];
  }

  const durMax = cat.durabilityMax || 0;
  // Worn: 7/7 type=2 wire count. Bag: wire/preserved current (login grant = 1).
  const durCur =
    it.durability != null
      ? Math.max(0, durMax > 0 ? Math.min(durMax, it.durability) : it.durability)
      : it.kind === "equip"
        ? 1
        : durMax;
  const enhance = it.enhance | 0;
  const lines: EquipPaintLine[] = [
    { kind: "kv", key: "[等级]:", value: String(cat.level), keyColor: qColor, valueColor: qColor },
    {
      kind: "kv",
      key: "[强化]:",
      value: String(enhance),
      keyColor: qColor,
      valueColor: qColor,
    },
  ];
  if (!opts.omitTypeJob) {
    lines.push(
      {
        kind: "kv",
        key: "[类型]:",
        value: typeNameForEquip(cat),
        keyColor: qColor,
        valueColor: qColor,
      },
      {
        kind: "kv",
        key: "[职业]:",
        value: jobNameForEquip(cat),
        keyColor: qColor,
        valueColor: qColor,
      },
    );
  }
  lines.push(
    {
      kind: "kv",
      key: "[耐久]:",
      value: `${durCur}/${durMax || durCur || 0}`,
      keyColor: qColor,
      valueColor: qColor,
    },
    ...mainStatLines(cat, qColor),
  );

  // L>=4: 四档里程碑；其后追加 M(L)-base 护甲/物攻（原分页第 2 页那几行）
  // 1<=L<4: 仅强化基础缩放行
  if (enhance >= 4) {
    lines.push(...enhanceLines(cat, enhance));
    lines.push(...scaleExtraLines(cat, enhance));
  } else if (enhance >= 1) {
    const extras = scaleExtraLines(cat, enhance);
    if (extras.length) {
      lines.push({
        kind: "text",
        text: `强化附加: (${enhance}/16)`,
        color: EQUIP_DETAIL_YELLOW,
      });
      lines.push(...extras);
    }
  }

  return lines;
}
