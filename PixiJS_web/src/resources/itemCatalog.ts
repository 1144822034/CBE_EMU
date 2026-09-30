import { decodeGbk, parseDshInt, readU32LE } from "./binary";
import { fetchResourceBytes } from "./loader";
import {
  getAppliedCustomCatalogSig,
  invalidateCustomCatalog,
  loadCustomCatalog,
  markCustomCatalogApplied,
  mergeCustomEquipsIntoMap,
  mergeCustomItemsIntoMap,
} from "./customCatalog";
import { loadSkillCatalog } from "./skill";

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
let mergedCustomItemIds: number[] = [];
let mergedCustomEquipIds: number[] = [];

/** Mall / bag item 806 — server hard cap (Pixi; CBE 64 no longer binding). */
export const BACKPACK_EXPAND_ITEM_ID = 806;
export const BACKPACK_EXPAND_MAX_CAPACITY = 256;
/** Per purchase / use: +4 slots (item.dsh + server VM_NET_MOCK_BACKPACK_EXPAND_STEP). */
export const BACKPACK_EXPAND_STEP = 4;

/**
 * How many 806 units fit before hitting {@link BACKPACK_EXPAND_MAX_CAPACITY}.
 * Matches server `vm_net_mock_role_backpack_expand_usable_count`.
 */
export function maxBackpackExpandBuyUnits(currentCapacity: number): number {
  const cur = Math.max(0, currentCapacity | 0);
  if (cur >= BACKPACK_EXPAND_MAX_CAPACITY) return 0;
  const room = BACKPACK_EXPAND_MAX_CAPACITY - cur;
  let usable = Math.floor(room / BACKPACK_EXPAND_STEP);
  if (room % BACKPACK_EXPAND_STEP) usable += 1;
  return usable;
}

/**
 * Append expand-cap note on 806 detail text (bag / mall「查看」).
 * item.dsh base remains「增加您背包的存储空间4格」.
 */
export function itemDisplayDesc(itemId: number, desc: string): string {
  if ((itemId >>> 0) !== BACKPACK_EXPAND_ITEM_ID) return desc;
  const base = (desc && desc !== "-" ? desc : "增加您背包的存储空间4格").trim();
  if (base.includes(String(BACKPACK_EXPAND_MAX_CAPACITY))) return base;
  const trimmed = base.replace(/[。．.]+$/u, "");
  return `${trimmed}，最大可扩容到${BACKPACK_EXPAND_MAX_CAPACITY}`;
}

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
 * Weapon categories are job-locked: 7 天机 / 8 幻剑 / 9 鬼道.
 * Non-weapon categories always allowed. `job` is 1..3 (panel stats);
 * unknown job defers to the server.
 */
export function jobAllowsWeaponCategory(job: number, category: number): boolean {
  if (category < 7 || category > 9) return true;
  if (job < 1 || job > 3) return true;
  if (job === 1) return category === 7;
  if (job === 2) return category === 8;
  if (job === 3) return category === 9;
  return false;
}

/** Tip when equipping a weapon the current job cannot use. */
export function weaponJobRestrictTip(category: number): string {
  if (category === 7) return "该武器仅限天机使用";
  if (category === 8) return "该武器仅限幻剑使用";
  if (category === 9) return "该武器仅限鬼道使用";
  return "职业不符，无法装备该武器";
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
    const id = parseDshInt(cells[0] ?? "");
    const name = cells[1] ?? "";
    if (!(id > 0 && name)) continue;
    map.set(id, {
      id,
      name,
      desc: itemDisplayDesc(id, cells[4] ?? ""),
      category: parseDshInt(cells[5] ?? ""),
      level: parseDshInt(cells[6] ?? ""),
      quality: parseDshInt(cells[9] ?? ""),
      stack: parseDshInt(cells[10] ?? ""),
      price: parseDshInt(cells[8] ?? ""),
      coolPrice: parseDshInt(cells[29] ?? ""),
      hpChange: parseDshInt(cells[15] ?? ""),
      mpChange: parseDshInt(cells[16] ?? ""),
      expChange: parseDshInt(cells[17] ?? ""),
      consumable: parseDshInt(cells[12] ?? "") !== 0,
      kind: "item",
      questLink: parseDshInt(cells[2] ?? ""),
      bindAttr: parseDshInt(cells[7] ?? ""),
    });
  }
  return map;
}

function parseEquipRows(raw: Uint8Array): Map<number, ItemCatalogRow> {
  const map = new Map<number, ItemCatalogRow>();
  for (const cells of parseDshRows(raw)) {
    const id = parseDshInt(cells[0] ?? "");
    const name = cells[1] ?? "";
    if (!(id > 0 && name)) continue;
    const category = parseDshInt(cells[7] ?? "");
    map.set(id, {
      id,
      name,
      desc: cells[2] ?? "",
      category,
      level: parseDshInt(cells[3] ?? ""),
      quality: parseDshInt(cells[6] ?? ""),
      stack: 1,
      price: parseDshInt(cells[5] ?? ""),
      coolPrice: 0,
      hpChange: parseDshInt(cells[10] ?? ""),
      mpChange: parseDshInt(cells[11] ?? ""),
      expChange: 0,
      consumable: false,
      kind: "equip",
      equipSlot: mapEquipCategoryToSlot(category),
      armor: parseDshInt(cells[8] ?? ""),
      attack: parseDshInt(cells[9] ?? ""),
      strength: parseDshInt(cells[12] ?? ""),
      agility: parseDshInt(cells[13] ?? ""),
      wisdom: parseDshInt(cells[14] ?? ""),
      crit: parseDshInt(cells[15] ?? ""),
      hit: parseDshInt(cells[16] ?? ""),
      dodge: parseDshInt(cells[17] ?? ""),
      resist: parseDshInt(cells[18] ?? ""),
      durabilityMax: parseDshInt(cells[19] ?? ""),
    });
  }
  return map;
}

export async function loadItemCatalog(): Promise<Map<number, ItemCatalogRow>> {
  if (!itemCache) {
    itemCache = parseItemRows(await fetchResourceBytes("item.dsh"));
    /* Pixi 自定义商城道具（不在 item.dsh）. */
    if (!itemCache.has(847)) {
    itemCache.set(847, {
      id: 847,
      name: "江湖追杀令",
      desc: "对仇人使用可显示其所在地图。耐久1。",
      category: 10,
      level: 1,
      quality: 0,
      stack: 1,
      price: 0,
      coolPrice: 50,
      hpChange: 0,
      mpChange: 0,
      expChange: 0,
      consumable: true,
      kind: "item",
      durabilityMax: 1,
    });
  }
  if (!itemCache.has(848)) {
    itemCache.set(848, {
      id: 848,
      name: "强制PK证明",
      desc: "发起强制PK时消耗1个。耐久1。",
      category: 14,
      level: 1,
      quality: 0,
      stack: 99,
      price: 0,
      coolPrice: 30,
      hpChange: 0,
      mpChange: 0,
      expChange: 0,
      consumable: true,
      kind: "item",
      durabilityMax: 1,
    });
  }
  if (!itemCache.has(849)) {
    itemCache.set(849, {
      id: 849,
      name: "星空石",
      desc: "来自宇宙星空的神石,自带时空回溯的逆天效果!",
      category: 14,
      level: 1,
      quality: 0,
      stack: 99,
      price: 0,
      coolPrice: 100,
      hpChange: 0,
      mpChange: 0,
      expChange: 0,
      consumable: true,
      kind: "item",
      durabilityMax: 1,
    });
  }
  if (!itemCache.has(852)) {
    itemCache.set(852, {
      id: 852,
      name: "角色改名卡",
      desc: "使用后可修改一次角色名。",
      category: 14,
      level: 1,
      quality: 0,
      stack: 99,
      price: 0,
      coolPrice: 100,
      hpChange: 0,
      mpChange: 0,
      expChange: 0,
      consumable: true,
      kind: "item",
      durabilityMax: 1,
    });
  }
  if (!itemCache.has(853)) {
    itemCache.set(853, {
      id: 853,
      name: "打金心得",
      desc: "使用后获得1小时打金心得状态，获得钱币增加20%，获得经验降为0（战斗只获得钱币）。耐久1。",
      category: 10,
      level: 1,
      quality: 0,
      stack: 99,
      price: 0,
      coolPrice: 50,
      hpChange: 0,
      mpChange: 0,
      expChange: 0,
      consumable: true,
      kind: "item",
      durabilityMax: 1,
    });
  }
  if (!itemCache.has(856)) {
    itemCache.set(856, {
      id: 856,
      name: "金币堆",
      desc: "使用后获得1万金币。可在仓库用金币兑换。",
      category: 10,
      level: 1,
      quality: 0,
      stack: 99,
      price: 0,
      coolPrice: 0,
      hpChange: 0,
      mpChange: 0,
      expChange: 0,
      consumable: true,
      kind: "item",
      durabilityMax: 1,
    });
  }
  if (!itemCache.has(857)) {
    itemCache.set(857, {
      id: 857,
      name: "角色封印卷",
      desc: "空白封印卷，使用后可封印同账号其它角色。已封印卷可上架藏宝阁。",
      category: 14,
      level: 1,
      quality: 0,
      stack: 99,
      price: 0,
      coolPrice: 0,
      hpChange: 0,
      mpChange: 0,
      expChange: 0,
      consumable: true,
      kind: "item",
      durabilityMax: 1,
    });
  }

  // Pixi gem sockets / fragment (922+/942+/962+/978).
  const gemDefs: { id: number; name: string; desc: string }[] = [
    { id: 978, name: "宝石碎片", desc: "3个可合成随机1级宝石；≥6可批量。" },
  ];
  for (let lv = 1; lv <= 16; lv++) {
    gemDefs.push({
      id: 921 + lv,
      name: `反伤宝石${lv}级`,
      desc: `镶嵌后获得${lv}%反伤。3个合成更高1级。`,
    });
    gemDefs.push({
      id: 941 + lv,
      name: `连击宝石${lv}级`,
      desc: `镶嵌后获得${lv}%连击。3个合成更高1级。`,
    });
    gemDefs.push({
      id: 961 + lv,
      name: `复活宝石${lv}级`,
      desc: `镶嵌后获得${lv}%复活。3个合成更高1级。`,
    });
  }
  for (const g of gemDefs) {
    if (!itemCache.has(g.id)) {
      itemCache.set(g.id, {
        id: g.id,
        name: g.name,
        desc: g.desc,
        category: 14,
        level: 1,
        quality: 0,
        stack: 99,
        price: 0,
        coolPrice: 0,
        hpChange: 0,
        mpChange: 0,
        expChange: 0,
        consumable: true,
        kind: "item",
        durabilityMax: 1,
      });
    }
  }

  if (!itemCache.has(854)) {
    itemCache.set(854, {
      id: 854,
      name: "玄晶提炼石",
      desc: "消耗1个，可将已强化装备投入的玄晶提炼回背包，装备销毁。",
      category: 14,
      level: 1,
      quality: 0,
      stack: 99,
      price: 0,
      coolPrice: 100,
      hpChange: 0,
      mpChange: 0,
      expChange: 0,
      consumable: true,
      kind: "item",
      durabilityMax: 1,
    });
  }
  if (!itemCache.has(855)) {
    itemCache.set(855, {
      id: 855,
      name: "跨品重铸石",
      desc: "强化转移时可跨装备品质。跨品质确认消耗1个，同品质不消耗；仍需同类部位；+N重铸另耗1个(N-1)级玄晶。",
      category: 14,
      level: 1,
      quality: 0,
      stack: 99,
      price: 0,
      coolPrice: 100,
      hpChange: 0,
      mpChange: 0,
      expChange: 0,
      consumable: true,
      kind: "item",
      durabilityMax: 1,
    });
  }
  }
  try {
    const custom = await loadCustomCatalog();
    mergedCustomItemIds = mergeCustomItemsIntoMap(
      itemCache!,
      custom,
      mergedCustomItemIds,
    );
  } catch {
    /* custom catalog optional */
  }
  return itemCache!;
}

export async function loadEquipCatalog(): Promise<Map<number, ItemCatalogRow>> {
  if (!equipCache) {
    equipCache = parseEquipRows(await fetchResourceBytes("equip.dsh"));
  }
  try {
    const custom = await loadCustomCatalog();
    mergedCustomEquipIds = mergeCustomEquipsIntoMap(
      equipCache,
      custom,
      mergedCustomEquipIds,
    );
  } catch {
    /* custom catalog optional */
  }
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

/** Exact DSH name match (equip first). Used by warehouse retrieve label coloring. */
export function lookupItemByNameSync(name: string): ItemCatalogRow | null {
  const key = name.trim();
  if (!key) return null;
  if (equipCache) {
    for (const row of equipCache.values()) {
      if (row.name === key) return row;
    }
  }
  if (itemCache) {
    for (const row of itemCache.values()) {
      if (row.name === key) return row;
    }
  }
  return null;
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
  // 宝石 / 碎片不可交易
  if (itemId === 978) return false;
  if (
    (itemId >= 922 && itemId <= 937) ||
    (itemId >= 942 && itemId <= 957) ||
    (itemId >= 962 && itemId <= 977)
  )
    return false;
  // 未注满的修炼天书实例由服务端按 book 状态拒绝；客户端放行 921 交给校验
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

/** Prefetch catalogs; refetch admin extras so live edits apply. */
export async function warmItemCatalogs(): Promise<void> {
  /* Single-flight fetch first, then merge into maps (avoid empty-prune races). */
  invalidateCustomCatalog();
  const custom = await loadCustomCatalog(undefined, { force: true });
  await Promise.all([
    loadItemCatalog(),
    loadEquipCatalog(),
    loadSkillCatalog(),
  ]);
  if (custom.ok) markCustomCatalogApplied(custom);
}

/**
 * Refetch admin extras if server content changed.
 * @returns true when maps were rematched to a new signature.
 */
export async function refreshCustomCatalogIfChanged(): Promise<boolean> {
  const before = getAppliedCustomCatalogSig();
  invalidateCustomCatalog();
  const latest = await loadCustomCatalog(undefined, { force: true });
  if (!latest.ok) return false;
  if (latest.sig && latest.sig === before) return false;
  await Promise.all([
    loadItemCatalog(),
    loadEquipCatalog(),
    loadSkillCatalog(),
  ]);
  markCustomCatalogApplied(latest);
  return true;
}

/**
 * Equip/item list name color by 品质 (CBE MMORPG_Item.c @0x010325B0, flag=1).
 * DrawString takes RGB565; expanded here to 24-bit for UC3 paint.
 * Q4 单独用粉红（与 Q3 品红区分）。
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
      return 0xee10f6; // RGB565 0xE89E 品红
    case 4:
      return 0xff69b4; // 粉红
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
