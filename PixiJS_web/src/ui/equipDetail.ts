/**
 * CBE bag equip detail lines — catalog paint (equip.dsh) + enhance milestones.
 * Evidence: JianghuOL.CBE 0x01032118 / 0x010325B0; docs/re enhance milestones;
 * screenshots 玄魔古杖 / 梦境魔杖.
 */

import type { BagEnhanceAttr, BagItem } from "../net/protocolExtras";
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
/** Red for washed (洗练) milestone rows. */
export const EQUIP_DETAIL_WASHED = 0xff2020;
/** Bag vs worn: candidate lower than worn. */
export const EQUIP_COMPARE_LOWER = 0x00ff00;
/** Bag vs worn: candidate higher than worn. */
export const EQUIP_COMPARE_HIGHER = 0xff2020;
/** Bag vs worn: equal. */
export const EQUIP_COMPARE_EQUAL = 0xffffff;

export type EquipPaintLine =
  | { kind: "text"; text: string; color: number }
  | {
      kind: "kv";
      key: string;
      value: string;
      keyColor: number;
      valueColor: number;
    }
  | {
      kind: "milestone";
      unlock: number;
      text: string;
      color: number;
    };

export type EquipCompareMark = {
  text: string;
  color: number;
};

/** Numeric detail keys that support bag↔worn compare. */
const COMPARE_ATTR_KEYS = [
  "等级",
  "强化",
  "力量",
  "敏捷",
  "智慧",
  "物攻",
  "护甲",
  "躲闪",
  "命中",
  "暴击",
  "抗性",
  "气血",
  "法力",
] as const;

type CompareAttrKey = (typeof COMPARE_ATTR_KEYS)[number];

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

/** Quality tables — mirror mock_server_catalog.c k*ByQuality. */
const HP_MP_FLAT_BY_Q = [0, 150, 300, 450, 600] as const;
const ATTACK_BY_Q = [0, 23, 50, 123, 296] as const;
const ARMOR_BY_Q = [0, 87, 175, 262, 350] as const;

type MilestoneDef = { type: number; flag: number };

/**
 * slot → +4/+8/+12/+16 defs (mirror mock_server_catalog.c).
 * Types: 1力 2敏 3智 4物攻 5护甲 6躲闪 7命中 8暴击 9气血 10法力; flag=1 → %.
 */
const MILESTONES: MilestoneDef[][] = [
  // 0 武器 — +16 type remapped by category 7力/8敏/9智
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
    { type: 7, flag: 0 },
    { type: 6, flag: 0 },
    { type: 2, flag: 0 },
  ],
  // 4 腰带
  [
    { type: 5, flag: 0 },
    { type: 10, flag: 0 },
    { type: 9, flag: 1 },
    { type: 1, flag: 0 },
  ],
  // 5 护腿
  [
    { type: 4, flag: 0 },
    { type: 5, flag: 0 },
    { type: 7, flag: 0 },
    { type: 9, flag: 0 },
  ],
  // 6 靴子
  [
    { type: 6, flag: 0 },
    { type: 9, flag: 0 },
    { type: 7, flag: 0 },
    { type: 2, flag: 0 },
  ],
  // 7 戒指
  [
    { type: 4, flag: 0 },
    { type: 9, flag: 0 },
    { type: 5, flag: 0 },
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
    // Weapon specials: +12 气血 (Q+1)*600; +16 三围 Q*422.
    // +8 护甲 uses the same halved ARMOR_BY_Q as other slots (server).
    if (unlock === 12 && type === 9 && flag === 0) return q1 * 600;
    if (unlock === 16 && (type === 1 || type === 2 || type === 3)) return q * 422;
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

/** Effective combat totals (物攻/护甲 include M(L) enhance). */
export function equipEffectiveStats(it: BagItem): Map<CompareAttrKey, number> {
  const out = new Map<CompareAttrKey, number>();
  const cat = lookupItemSync(it.itemId);
  if (!cat || cat.kind !== "equip") return out;
  const enhance = it.enhance | 0;
  const attackBase = cat.attack ?? 0;
  const armorBase = cat.armor ?? 0;
  const attackDelta =
    enhance >= 1 && attackBase > 0 ? attackEnhanceDelta(attackBase, enhance) : 0;
  const armorDelta =
    enhance >= 1 && armorBase > 0 ? armorEnhanceDelta(armorBase, enhance) : 0;
  out.set("等级", cat.level | 0);
  out.set("强化", enhance);
  out.set("力量", cat.strength ?? 0);
  out.set("敏捷", cat.agility ?? 0);
  out.set("智慧", cat.wisdom ?? 0);
  out.set("物攻", attackBase + attackDelta);
  out.set("护甲", armorBase + armorDelta);
  out.set("躲闪", cat.dodge ?? 0);
  out.set("命中", cat.hit ?? 0);
  out.set("暴击", cat.crit ?? 0);
  out.set("抗性", cat.resist ?? 0);
  out.set("气血", cat.hpChange ?? 0);
  out.set("法力", cat.mpChange ?? 0);
  return out;
}

/** Parse `[物攻]:` → `物攻`. */
export function equipKvAttrName(key: string): string | null {
  const m = /^\[(.+)\]:$/.exec(key);
  return m?.[1] ?? null;
}

export function equipCompareMark(candidate: number, worn: number): EquipCompareMark {
  const d = candidate - worn;
  if (d === 0) return { text: "=", color: EQUIP_COMPARE_EQUAL };
  if (d > 0) return { text: `+${d}`, color: EQUIP_COMPARE_HIGHER };
  return { text: `${d}`, color: EQUIP_COMPARE_LOWER };
}

/**
 * Bag equip detail vs currently worn same-slot piece.
 * Keys: 等级/强化/力量/… plus milestone tiers as `ms:4` / `ms:8` / …
 */
export function buildEquipCompareMarks(
  candidate: BagItem,
  worn: BagItem,
): Map<string, EquipCompareMark> {
  const a = equipEffectiveStats(candidate);
  const b = equipEffectiveStats(worn);
  const out = new Map<string, EquipCompareMark>();
  for (const key of COMPARE_ATTR_KEYS) {
    const cv = a.get(key) ?? 0;
    const wv = b.get(key) ?? 0;
    // Meta always comparable; combat rows only when candidate shows them (non-zero).
    if (key === "等级" || key === "强化") {
      out.set(key, equipCompareMark(cv, wv));
      continue;
    }
    if (cv !== 0) out.set(key, equipCompareMark(cv, wv));
  }

  // 强化附加 / 洗练档：按 +4/+8/+12/+16 同档对比
  if ((candidate.enhance | 0) >= 4) {
    const candTiers = resolveMilestoneTiers(candidate);
    const wornTiers = resolveMilestoneTiers(worn);
    const wornEnhance = worn.enhance | 0;
    for (const unlock of [4, 8, 12, 16] as const) {
      const cand = candTiers.get(unlock);
      if (!cand) continue;
      const wornTier = wornTiers.get(unlock);
      const wornActive = !!wornTier && wornEnhance >= unlock;
      out.set(`ms:${unlock}`, milestoneCompareMark(cand, wornActive ? wornTier! : null));
    }
  }
  return out;
}

function milestoneCompareMark(
  cand: ResolvedMilestone,
  worn: ResolvedMilestone | null,
): EquipCompareMark {
  if (!worn) {
    // 对方未解锁该档：整档视为多出
    return equipCompareMark(cand.value, 0);
  }
  const candPct = cand.flag & 1;
  const wornPct = worn.flag & 1;
  if (cand.type !== worn.type || candPct !== wornPct) {
    // 不同类型：右侧展示穿戴档「属性名+value」，便于对照
    const name = ATTR_NAMES[worn.type] ?? "?";
    const pct = wornPct !== 0 ? "%" : "";
    return { text: `${name}+${worn.value}${pct}`, color: EQUIP_DETAIL_YELLOW };
  }
  return equipCompareMark(cand.value, worn.value);
}

type ResolvedMilestone = {
  unlock: number;
  type: number;
  flag: number;
  value: number;
  washed: boolean;
};

/** Resolve +4/+8/+12/+16 rows from wire attrs or catalog defaults. */
function resolveMilestoneTiers(it: BagItem): Map<number, ResolvedMilestone> {
  const out = new Map<number, ResolvedMilestone>();
  const cat = lookupItemSync(it.itemId);
  if (!cat || cat.kind !== "equip") return out;

  const fromWire =
    it.enhanceAttrs?.filter(
      (a) => a.unlock === 4 || a.unlock === 8 || a.unlock === 12 || a.unlock === 16,
    ) ?? [];
  if (fromWire.length > 0) {
    for (const a of fromWire) {
      out.set(a.unlock, {
        unlock: a.unlock,
        type: a.type,
        flag: a.flag & 1,
        value: a.value | 0,
        washed: isWashedMilestone(cat, a),
      });
    }
    return out;
  }

  const slot = cat.equipSlot ?? -1;
  if (slot < 0 || slot >= MILESTONES.length) return out;
  const defs = MILESTONES[slot]!;
  for (let i = 0; i < defs.length; i++) {
    const unlock = (i + 1) * 4;
    const def = defs[i]!;
    const type = milestoneType(cat, i, def.type);
    const flag = def.flag & 1;
    const value = milestoneValue(cat, unlock, type, flag);
    out.set(unlock, { unlock, type, flag, value, washed: false });
  }
  return out;
}

/** Main combat attrs from equip.dsh (only non-zero columns), CBE order.
 * 物攻/护甲 append M(L) enhance delta: `[物攻]:+576(+1725)`.
 */
function mainStatLines(
  cat: ItemCatalogRow,
  qColor: number,
  enhance = 0,
): EquipPaintLine[] {
  const attackBase = cat.attack ?? 0;
  const armorBase = cat.armor ?? 0;
  const attackDelta =
    enhance >= 1 && attackBase > 0 ? attackEnhanceDelta(attackBase, enhance) : 0;
  const armorDelta =
    enhance >= 1 && armorBase > 0 ? armorEnhanceDelta(armorBase, enhance) : 0;
  const rows: { key: string; n: number; delta: number }[] = [
    { key: "力量", n: cat.strength ?? 0, delta: 0 },
    { key: "敏捷", n: cat.agility ?? 0, delta: 0 },
    { key: "智慧", n: cat.wisdom ?? 0, delta: 0 },
    { key: "物攻", n: attackBase, delta: attackDelta },
    { key: "护甲", n: armorBase, delta: armorDelta },
    { key: "躲闪", n: cat.dodge ?? 0, delta: 0 },
    { key: "命中", n: cat.hit ?? 0, delta: 0 },
    { key: "暴击", n: cat.crit ?? 0, delta: 0 },
    { key: "抗性", n: cat.resist ?? 0, delta: 0 },
    { key: "气血", n: cat.hpChange ?? 0, delta: 0 },
    { key: "法力", n: cat.mpChange ?? 0, delta: 0 },
  ];
  return rows
    .filter((r) => r.n !== 0)
    .map((r) => ({
      kind: "kv" as const,
      key: `[${r.key}]:`,
      value: r.delta > 0 ? `+${r.n}(+${r.delta})` : `+${r.n}`,
      keyColor: qColor,
      valueColor: EQUIP_DETAIL_YELLOW,
    }));
}

function enhanceLines(cat: ItemCatalogRow, enhance: number, attrs?: BagEnhanceAttr[]): EquipPaintLine[] {
  const out: EquipPaintLine[] = [
    {
      kind: "text",
      text: `强化附加: (${enhance}/16)`,
      color: EQUIP_DETAIL_YELLOW,
    },
  ];

  const milestones =
    attrs?.filter((a) => a.unlock === 4 || a.unlock === 8 || a.unlock === 12 || a.unlock === 16) ??
    [];
  if (milestones.length > 0) {
    for (const a of milestones) {
      const name = ATTR_NAMES[a.type] ?? "?";
      const pct = (a.flag & 1) !== 0 ? "%" : "";
      const locked = a.unlock > enhance;
      const washed = isWashedMilestone(cat, a);
      out.push({
        kind: "milestone",
        unlock: a.unlock,
        text: `(+${a.unlock}) ${name}+${a.value}${pct}`,
        color: locked
          ? EQUIP_DETAIL_GRAY
          : washed
            ? EQUIP_DETAIL_WASHED
            : EQUIP_DETAIL_YELLOW,
      });
    }
    return out;
  }

  const slot = cat.equipSlot ?? -1;
  if (slot < 0 || slot >= MILESTONES.length) return out;
  const defs = MILESTONES[slot]!;
  for (let i = 0; i < defs.length; i++) {
    const unlock = (i + 1) * 4;
    const def = defs[i]!;
    const type = milestoneType(cat, i, def.type);
    const value = milestoneValue(cat, unlock, type, def.flag);
    const name = ATTR_NAMES[type] ?? "?";
    const pct = def.flag !== 0 ? "%" : "";
    const locked = unlock > enhance;
    out.push({
      kind: "milestone",
      unlock,
      text: `(+${unlock}) ${name}+${value}${pct}`,
      color: locked ? EQUIP_DETAIL_GRAY : EQUIP_DETAIL_YELLOW,
    });
  }
  return out;
}

/** Wire bit7 or diverge from catalog default milestone → washed (red). */
function isWashedMilestone(cat: ItemCatalogRow, a: BagEnhanceAttr): boolean {
  if (a.washed) return true;
  const slot = cat.equipSlot ?? -1;
  if (slot < 0 || slot >= MILESTONES.length) return false;
  const slotIndex = a.unlock / 4 - 1;
  if (slotIndex < 0 || slotIndex > 3 || !Number.isInteger(slotIndex)) return false;
  const def = MILESTONES[slot]![slotIndex];
  if (!def) return false;
  const defType = milestoneType(cat, slotIndex, def.type);
  const defFlag = def.flag & 1;
  const defValue = milestoneValue(cat, a.unlock, defType, defFlag);
  return a.type !== defType || (a.flag & 1) !== defFlag || a.value !== defValue;
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
    ...mainStatLines(cat, qColor, enhance),
  );

  // 四档里程碑始终展示；unlock > enhance 时灰显（含 +0..+3）。
  // 物攻/护甲 M(L) 缩放已并入主属性行。
  lines.push(...enhanceLines(cat, enhance, it.enhanceAttrs));

  return lines;
}
