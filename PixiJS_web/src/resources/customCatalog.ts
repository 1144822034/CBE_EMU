import type { ItemCatalogRow } from "./itemCatalog";
import type { SkillCatalogItem, SkillTargetKind } from "./skill";
import { defaultLiveGatewayUrl } from "../net/Transport";
import { defaultMonsterLevelsUrl } from "./monsterLevels";

/** Admin-created catalog rows from GET /api/custom-catalog (Pixi-only). */

export type CustomCatalogItem = {
  id: number;
  name: string;
  desc: string;
  category: number;
  level: number;
  stack: number;
  consumeMode: number;
  durationMinutes: number;
  hp: number;
  mp: number;
  exp: number;
  questLink: number;
  bindAttr: number;
  visual: number;
  price: number;
  coolPrice: number;
  shopEnabled: number;
};

export type CustomCatalogEquip = {
  id: number;
  name: string;
  desc: string;
  category: number;
  level: number;
  quality: number;
  durabilityMax: number;
  attack: number;
  armor: number;
  hp: number;
  mp: number;
  strength: number;
  agility: number;
  wisdom: number;
  dodge: number;
  hit: number;
  crit: number;
  resist: number;
  price: number;
  shopEnabled: number;
};

export type CustomCatalogSkill = {
  id: number;
  name: string;
  desc: string;
  effectIndex: number;
  level: number;
  rawJob: number;
  learnPrice: number;
  useLimit: number;
  targetKind: number;
  mpCost: number;
  hpChange: number;
};

export type CustomCatalogPayload = {
  /** Server bump counter; 0 when old server / fetch failed. */
  rev: number;
  /** True only after a successful HTTP+JSON parse. */
  ok: boolean;
  /** Content fingerprint for change detection when rev is absent. */
  sig: string;
  items: CustomCatalogItem[];
  equips: CustomCatalogEquip[];
  skills: CustomCatalogSkill[];
};

let cached: CustomCatalogPayload | null = null;
/** Last successful payload — never discarded on fetch failure. */
let lastGood: CustomCatalogPayload | null = null;
let inflight: Promise<CustomCatalogPayload> | null = null;
/** Last signature successfully merged into item/equip/skill maps. */
let appliedSig = "";

export function defaultCustomCatalogUrl(gatewayWsUrl?: string): string {
  const levels = defaultMonsterLevelsUrl(gatewayWsUrl || defaultLiveGatewayUrl());
  if (levels.includes("/api/monster-levels")) {
    return levels.replace("/api/monster-levels", "/api/custom-catalog");
  }
  return levels.replace(/\/+$/, "") + "/api/custom-catalog";
}

/**
 * Drop soft cache so the next load refetches.
 * Does NOT clear lastGood — failed refetch must not wipe names.
 */
export function invalidateCustomCatalog(): void {
  cached = null;
  inflight = null;
}

export function getCustomCatalogRev(): number {
  return (cached ?? lastGood)?.rev ?? 0;
}

export function getAppliedCustomCatalogSig(): string {
  return appliedSig;
}

export function markCustomCatalogApplied(payload: CustomCatalogPayload): void {
  if (payload.ok) appliedSig = payload.sig;
}

export function catalogPayloadSig(payload: CustomCatalogPayload): string {
  return payload.sig;
}

function asNum(v: unknown, fallback = 0): number {
  const n = Number(v);
  return Number.isFinite(n) ? n : fallback;
}

function asStr(v: unknown): string {
  return typeof v === "string" ? v : v == null ? "" : String(v);
}

function clampTargetKind(v: number): SkillTargetKind {
  const n = v | 0;
  if (n >= 0 && n <= 4) return n as SkillTargetKind;
  return 3;
}

function buildSig(
  rev: number,
  items: CustomCatalogItem[],
  equips: CustomCatalogEquip[],
  skills: CustomCatalogSkill[],
): string {
  if (rev > 0) return `rev:${rev}`;
  const eq = equips
    .map(
      (e) =>
        `${e.id}:${e.name}:${e.category}:${e.quality}:${e.attack}:${e.armor}:${e.hp}:${e.mp}:${e.strength}:${e.agility}:${e.wisdom}:${e.price}`,
    )
    .join("|");
  const it = items
    .map((i) => `${i.id}:${i.name}:${i.category}:${i.hp}:${i.mp}:${i.price}`)
    .join("|");
  const sk = skills
    .map((s) => `${s.id}:${s.name}:${s.effectIndex}:${s.mpCost}:${s.hpChange}`)
    .join("|");
  return `body:${it}#${eq}#${sk}`;
}

/**
 * equip.dsh 类别 → body slot (duplicated from itemCatalog to avoid import cycle).
 * weapons 7/8/9 → slot 0; 0 helm … 6 ring → slots 1..7.
 */
function mapEquipCategoryToSlot(category: number): number {
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

function emptyPayload(): CustomCatalogPayload {
  return {
    rev: 0,
    ok: false,
    sig: "",
    items: [],
    equips: [],
    skills: [],
  };
}

/** Fetch custom catalog. Pass force to bypass soft cache (admin live edit). */
export async function loadCustomCatalog(
  url?: string,
  opts?: { force?: boolean },
): Promise<CustomCatalogPayload> {
  if (opts?.force) {
    cached = null;
    /* Do not null inflight mid-flight — await the same promise instead. */
  }
  if (cached) return cached;
  if (inflight) return inflight;
  const target = url || defaultCustomCatalogUrl();
  inflight = (async () => {
    try {
      const ctrl =
        typeof AbortController !== "undefined" ? new AbortController() : null;
      const timer =
        ctrl != null ? window.setTimeout(() => ctrl.abort(), 4000) : 0;
      const res = await fetch(target, {
        method: "GET",
        signal: ctrl?.signal,
        cache: "no-store",
      });
      if (timer) window.clearTimeout(timer);
      if (!res.ok) {
        console.warn(`[customCatalog] ${res.status} from ${target}`);
        if (lastGood) cached = lastGood;
        return lastGood ?? emptyPayload();
      }
      const raw = (await res.json()) as Partial<CustomCatalogPayload> & {
        rev?: unknown;
      };
      const rev = asNum(raw.rev);
      const items: CustomCatalogItem[] = [];
      const equips: CustomCatalogEquip[] = [];
      const skills: CustomCatalogSkill[] = [];
      if (Array.isArray(raw.items)) {
        for (const row of raw.items) {
          const id = asNum((row as CustomCatalogItem).id);
          if (!(id > 0)) continue;
          items.push({
            id,
            name: asStr((row as CustomCatalogItem).name),
            desc: asStr((row as CustomCatalogItem).desc),
            category: asNum((row as CustomCatalogItem).category, 10),
            level: asNum((row as CustomCatalogItem).level, 1),
            stack: asNum((row as CustomCatalogItem).stack, 99),
            consumeMode: asNum((row as CustomCatalogItem).consumeMode, 1),
            durationMinutes: asNum((row as CustomCatalogItem).durationMinutes),
            hp: asNum((row as CustomCatalogItem).hp),
            mp: asNum((row as CustomCatalogItem).mp),
            exp: asNum((row as CustomCatalogItem).exp),
            questLink: asNum((row as CustomCatalogItem).questLink),
            bindAttr: asNum((row as CustomCatalogItem).bindAttr),
            visual: asNum((row as CustomCatalogItem).visual, 1),
            price: asNum((row as CustomCatalogItem).price),
            coolPrice: asNum((row as CustomCatalogItem).coolPrice),
            shopEnabled: asNum((row as CustomCatalogItem).shopEnabled),
          });
        }
      }
      if (Array.isArray(raw.equips)) {
        for (const row of raw.equips) {
          const id = asNum((row as CustomCatalogEquip).id);
          if (!(id > 0)) continue;
          equips.push({
            id,
            name: asStr((row as CustomCatalogEquip).name),
            desc: asStr((row as CustomCatalogEquip).desc),
            category: asNum((row as CustomCatalogEquip).category),
            level: asNum((row as CustomCatalogEquip).level, 1),
            quality: asNum((row as CustomCatalogEquip).quality),
            durabilityMax: asNum((row as CustomCatalogEquip).durabilityMax, 50),
            attack: asNum((row as CustomCatalogEquip).attack),
            armor: asNum((row as CustomCatalogEquip).armor),
            hp: asNum((row as CustomCatalogEquip).hp),
            mp: asNum((row as CustomCatalogEquip).mp),
            strength: asNum((row as CustomCatalogEquip).strength),
            agility: asNum((row as CustomCatalogEquip).agility),
            wisdom: asNum((row as CustomCatalogEquip).wisdom),
            dodge: asNum((row as CustomCatalogEquip).dodge),
            hit: asNum((row as CustomCatalogEquip).hit),
            crit: asNum((row as CustomCatalogEquip).crit),
            resist: asNum((row as CustomCatalogEquip).resist),
            price: asNum((row as CustomCatalogEquip).price),
            shopEnabled: asNum((row as CustomCatalogEquip).shopEnabled),
          });
        }
      }
      if (Array.isArray(raw.skills)) {
        for (const row of raw.skills) {
          const id = asNum((row as CustomCatalogSkill).id);
          if (!(id > 0)) continue;
          skills.push({
            id,
            name: asStr((row as CustomCatalogSkill).name),
            desc: asStr((row as CustomCatalogSkill).desc),
            effectIndex: asNum((row as CustomCatalogSkill).effectIndex),
            level: asNum((row as CustomCatalogSkill).level, 1),
            rawJob: asNum((row as CustomCatalogSkill).rawJob),
            learnPrice: asNum((row as CustomCatalogSkill).learnPrice),
            useLimit: asNum((row as CustomCatalogSkill).useLimit),
            targetKind: asNum((row as CustomCatalogSkill).targetKind, 3),
            mpCost: asNum((row as CustomCatalogSkill).mpCost),
            hpChange: asNum((row as CustomCatalogSkill).hpChange),
          });
        }
      }
      const sig = buildSig(rev, items, equips, skills);
      const payload: CustomCatalogPayload = {
        rev,
        ok: true,
        sig,
        items,
        equips,
        skills,
      };
      cached = payload;
      lastGood = payload;
      if (items.length || equips.length || skills.length || rev > 0) {
        console.info(
          `[customCatalog] rev=${rev} items=${items.length} equips=${equips.length} skills=${skills.length} from ${target}`,
        );
      }
      return payload;
    } catch (e) {
      console.warn(`[customCatalog] fetch failed ${target}`, e);
      if (lastGood) cached = lastGood;
      return lastGood ?? emptyPayload();
    } finally {
      inflight = null;
    }
  })();
  return inflight;
}

/** Merge custom item rows into the item map (custom wins on same id). */
export function mergeCustomItemsIntoMap(
  items: Map<number, ItemCatalogRow>,
  payload: CustomCatalogPayload,
  prevCustomIds?: number[],
): number[] {
  /* Failed fetch → keep prior merge untouched. */
  if (!payload.ok) return prevCustomIds ? prevCustomIds.slice() : [];
  const nextIds = payload.items.map((row) => row.id);
  if (prevCustomIds) {
    for (const id of prevCustomIds) {
      if (!nextIds.includes(id)) items.delete(id);
    }
  }
  for (const row of payload.items) {
    items.set(row.id, {
      id: row.id,
      name: row.name || `物品#${row.id}`,
      desc: row.desc || "",
      category: row.category,
      level: row.level || 1,
      quality: 0,
      stack: row.stack || 1,
      price: row.price,
      coolPrice: row.coolPrice,
      hpChange: row.hp,
      mpChange: row.mp,
      expChange: row.exp,
      consumable: row.consumeMode !== 0,
      kind: "item",
      questLink: row.questLink || undefined,
      bindAttr: row.bindAttr || undefined,
      durabilityMax: 1,
    });
  }
  return nextIds;
}

/** Merge custom equip rows into the equip map (custom wins on same id). */
export function mergeCustomEquipsIntoMap(
  equips: Map<number, ItemCatalogRow>,
  payload: CustomCatalogPayload,
  prevCustomIds?: number[],
): number[] {
  if (!payload.ok) return prevCustomIds ? prevCustomIds.slice() : [];
  const nextIds = payload.equips.map((row) => row.id);
  if (prevCustomIds) {
    for (const id of prevCustomIds) {
      if (!nextIds.includes(id)) equips.delete(id);
    }
  }
  for (const row of payload.equips) {
    equips.set(row.id, {
      id: row.id,
      name: row.name || `装备#${row.id}`,
      desc: row.desc || "",
      category: row.category,
      level: row.level || 1,
      quality: row.quality,
      stack: 1,
      price: row.price,
      coolPrice: 0,
      hpChange: row.hp,
      mpChange: row.mp,
      expChange: 0,
      consumable: false,
      kind: "equip",
      equipSlot: mapEquipCategoryToSlot(row.category),
      attack: row.attack,
      armor: row.armor,
      strength: row.strength,
      agility: row.agility,
      wisdom: row.wisdom,
      dodge: row.dodge,
      hit: row.hit,
      crit: row.crit,
      resist: row.resist,
      durabilityMax: row.durabilityMax || 50,
    });
  }
  return nextIds;
}

/** @deprecated prefer mergeCustomItemsIntoMap / mergeCustomEquipsIntoMap */
export function mergeCustomIntoItemMaps(
  items: Map<number, ItemCatalogRow>,
  equips: Map<number, ItemCatalogRow>,
  payload: CustomCatalogPayload,
): void {
  mergeCustomItemsIntoMap(items, payload);
  mergeCustomEquipsIntoMap(equips, payload);
}

/** Merge custom skills into catalog array (replace same id, else append). */
export function mergeCustomIntoSkillCatalog(
  catalog: SkillCatalogItem[],
  payload: CustomCatalogPayload,
): SkillCatalogItem[] {
  if (!payload.ok) return catalog;
  const byId = new Map<number, SkillCatalogItem>();
  for (const row of catalog) byId.set(row.id, row);
  for (const row of payload.skills) {
    byId.set(row.id, {
      id: row.id,
      name: row.name || `技能#${row.id}`,
      desc: row.desc || "",
      effectIndex: row.effectIndex,
      level: row.level || 1,
      rawJob: row.rawJob,
      learnPrice: row.learnPrice,
      mpCost: row.mpCost,
      targetKind: clampTargetKind(row.targetKind),
      useLimit: row.useLimit,
      hpChange: row.hpChange,
    });
  }
  return Array.from(byId.values());
}
