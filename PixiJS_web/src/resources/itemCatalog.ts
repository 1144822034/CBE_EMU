import { decodeGbk, readU32LE } from "./binary";
import { fetchResourceBytes } from "./loader";

/** item.dsh / equip.dsh lookup — names & quality come from local DSH (CBE paints the same). */

export type ItemCatalogRow = {
  id: number;
  name: string;
  desc: string;
  category: number;
  level: number;
  quality: number;
  stack: number;
  price: number;
  /** 酷宝 column (item.dsh). */
  coolPrice: number;
  hpChange: number;
  mpChange: number;
  expChange: number;
  consumable: boolean;
  kind: "item" | "equip";
  /** item.dsh 关联任务 — 非 0 视为任务物品（不可丢/不可交易）. */
  questLink?: number;
  /** item.dsh 绑定属性 — 非 0 绑定限制. */
  bindAttr?: number;
  /** equip.dsh 类别 → worn slot when equipment. */
  equipSlot?: number;
  attack?: number;
  armor?: number;
  strength?: number;
  agility?: number;
  wisdom?: number;
  dodge?: number;
  hit?: number;
  crit?: number;
  resist?: number;
  durabilityMax?: number;
};

let itemCache: Map<number, ItemCatalogRow> | null = null;
let equipCache: Map<number, ItemCatalogRow> | null = null;

function parseDshRows(raw: Uint8Array): string[][] {
  const out: string[][] = [];
  if (raw.length < 16) return out;
  const cols = readU32LE(raw, 4);
  const rows = readU32LE(raw, 8);
  if (cols < 2 || cols > 64 || rows > 4096) return out;
  let pos = 16;
  for (let i = 0; i < cols; i++) {
    if (pos >= raw.length) return out;
    const fl = raw[pos++]!;
    pos += fl;
  }
  for (let row = 0; row < rows && pos + 4 <= raw.length; row++) {
    const rowLen = readU32LE(raw, pos);
    let rp = pos + 4;
    const end = rp + rowLen;
    if (end > raw.length) break;
    const cells: string[] = [];
    for (let c = 0; c < cols && rp < end; c++) {
      const vl = raw[rp++]!;
      cells.push(decodeGbk(raw.subarray(rp, rp + vl)).replace(/\0+$/, "").trim());
      rp += vl;
    }
    pos = end;
    out.push(cells);
  }
  return out;
}

/**
 * equip.dsh 类别 → body slot (docs/re/2026-06-28-player-attribute-model.md).
 * weapons 7/8/9 → slot 0; 0 helm … 6 ring → slots 1..7.
 */
export function mapEquipCategoryToSlot(category: number): number {
  if (category >= 7 && category <= 9) return 0;
  if (category === 0) return 1;
  if (category === 1) return 2;
  if (category === 2) return 3;
  if (category === 3) return 4;
  if (category === 4) return 5;
  if (category === 5) return 6;
  if (category === 6) return 7;
  return -1;
}

/**
 * equip.dsh 类别 → equipico.gif tile index (0..9).
 * Sheet is 160×16 = ten 16×16 icons aligned with categories 0–9.
 */
export function equipCategoryIconIndex(category: number): number {
  const c = category | 0;
  if (c >= 0 && c <= 9) return c;
  return 0;
}

/**
 * Empty worn body slot → default equipico tile (weapon empty → 剑=7).
 * Body slots: 0 weapon, 1 helm … 7 ring.
 */
export function defaultCategoryForBodySlot(slot: number): number {
  switch (slot | 0) {
    case 0:
      return 7; // 剑
    case 1:
      return 0; // 帽
    case 2:
      return 1; // 衣
    case 3:
      return 2; // 披
    case 4:
      return 3; // 束
    case 5:
      return 4; // 裤
    case 6:
      return 5; // 鞋
    case 7:
      return 6; // 饰
    default:
      return 0;
  }
}

function parseItemRows(raw: Uint8Array): Map<number, ItemCatalogRow> {
  const map = new Map<number, ItemCatalogRow>();
  for (const cells of parseDshRows(raw)) {
    const id = Number(cells[0] ?? "") | 0;
    const name = cells[1] ?? "";
    if (!(id > 0 && name)) continue;
    map.set(id, {
      id,
      name,
      desc: cells[4] ?? "",
      category: Number(cells[5] ?? "") | 0,
      level: Number(cells[6] ?? "") | 0,
      quality: Number(cells[9] ?? "") | 0,
      stack: Number(cells[10] ?? "") | 0,
      price: Number(cells[8] ?? "") | 0,
      coolPrice: Number(cells[29] ?? "") | 0,
      hpChange: Number(cells[15] ?? "") | 0,
      mpChange: Number(cells[16] ?? "") | 0,
      expChange: Number(cells[17] ?? "") | 0,
      consumable: (Number(cells[12] ?? "") | 0) !== 0,
      kind: "item",
      questLink: Number(cells[2] ?? "") | 0,
      bindAttr: Number(cells[7] ?? "") | 0,
    });
  }
  return map;
}

function parseEquipRows(raw: Uint8Array): Map<number, ItemCatalogRow> {
  const map = new Map<number, ItemCatalogRow>();
  for (const cells of parseDshRows(raw)) {
    const id = Number(cells[0] ?? "") | 0;
    const name = cells[1] ?? "";
    if (!(id > 0 && name)) continue;
    const category = Number(cells[7] ?? "") | 0;
    map.set(id, {
      id,
      name,
      desc: cells[2] ?? "",
      category,
      level: Number(cells[3] ?? "") | 0,
      quality: Number(cells[6] ?? "") | 0,
      stack: 1,
      price: Number(cells[5] ?? "") | 0,
      coolPrice: 0,
      hpChange: Number(cells[10] ?? "") | 0,
      mpChange: Number(cells[11] ?? "") | 0,
      expChange: 0,
      consumable: false,
      kind: "equip",
      equipSlot: mapEquipCategoryToSlot(category),
      armor: Number(cells[8] ?? "") | 0,
      attack: Number(cells[9] ?? "") | 0,
      strength: Number(cells[12] ?? "") | 0,
      agility: Number(cells[13] ?? "") | 0,
      wisdom: Number(cells[14] ?? "") | 0,
      crit: Number(cells[15] ?? "") | 0,
      hit: Number(cells[16] ?? "") | 0,
      dodge: Number(cells[17] ?? "") | 0,
      resist: Number(cells[18] ?? "") | 0,
      durabilityMax: Number(cells[19] ?? "") | 0,
    });
  }
  return map;
}

export async function loadItemCatalog(): Promise<Map<number, ItemCatalogRow>> {
  if (itemCache) return itemCache;
  itemCache = parseItemRows(await fetchResourceBytes("item.dsh"));
  return itemCache;
}

export async function loadEquipCatalog(): Promise<Map<number, ItemCatalogRow>> {
  if (equipCache) return equipCache;
  equipCache = parseEquipRows(await fetchResourceBytes("equip.dsh"));
  return equipCache;
}

export async function lookupItem(itemId: number): Promise<ItemCatalogRow | null> {
  const equip = await loadEquipCatalog();
  if (equip.has(itemId)) return equip.get(itemId)!;
  const items = await loadItemCatalog();
  return items.get(itemId) ?? null;
}

export function lookupItemSync(itemId: number): ItemCatalogRow | null {
  return equipCache?.get(itemId) ?? itemCache?.get(itemId) ?? null;
}

/**
 * Trade pick filter — exclude 特殊/任务/绑定、玄晶、已强化装备.
 * Evidence: item.dsh 关联任务(col2)/绑定属性(col7); 玄晶 901–916 / 类别23;
 * enhance_level 背包 common-extra.
 */
export function isTradeableItem(
  itemId: number,
  opts?: { enhance?: number; kind?: "item" | "equip" },
): boolean {
  // 玄晶 901..916（强化材料）
  if (itemId >= 901 && itemId <= 916) return false;
  // 梦境徽章（BOSS 掉落 / 任务相关，不可交易）
  if (itemId === 840) return false;
  // 已强化装备不可交易
  if ((opts?.enhance ?? 0) > 0) return false;

  const cat = lookupItemSync(itemId);
  if (!cat) return true;
  if (cat.kind === "item" && cat.category === 23) return false; // 玄晶类别
  if (cat.kind === "equip") return (opts?.enhance ?? 0) <= 0;
  if (cat.category === 11) return false;
  if ((cat.questLink ?? 0) !== 0) return false;
  if ((cat.bindAttr ?? 0) !== 0) return false;
  return true;
}

/** Prefetch both catalogs (call at bag/shop open). */
export async function warmItemCatalogs(): Promise<void> {
  await Promise.all([loadItemCatalog(), loadEquipCatalog()]);
}

/**
 * Equip/item list name color by 品质 (CBE MMORPG_Item.c @0x010325B0, flag=1).
 * DrawString takes RGB565; expanded here to 24-bit for UC3 paint.
 * Q4 shares Q3 magenta in the CBE jump table (no separate orange stub).
 */
export function qualityColor(quality: number): number {
  switch (quality | 0) {
    case 0:
      return 0xffffff; // RGB565 0xFFFF
    case 1:
      return 0x00ff00; // RGB565 0x07E0 绿
    case 2:
      return 0x29faff; // RGB565 0x2FDF 青（非蓝）
    case 3:
    case 4:
      return 0xee10f6; // RGB565 0xE89E 品红
    default:
      return 0xee10f6;
  }
}

export const WORN_SLOT_NAMES = [
  "武器",
  "头盔",
  "上衣",
  "披风",
  "腰带",
  "护腿",
  "靴子",
  "戒指",
] as const;
