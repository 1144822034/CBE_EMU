import { decodeGbk, parseDshInt, readU32LE } from "./binary";
import { fetchResourceBytes } from "./loader";
import {
  loadCustomCatalog,
  mergeCustomIntoSkillCatalog,
} from "./customCatalog";

/** skill.dsh 目标指向 (col 10): 0己 / 1单友 / 2友群 / 3单敌 / 4敌群 */
export type SkillTargetKind = 0 | 1 | 2 | 3 | 4;

/** One row from skill.dsh (job-filtered catalog). */
export type SkillCatalogItem = {
  id: number;
  name: string;
  /** skill.dsh 列 3「说明」——技能介绍文案. */
  desc: string;
  /** eidolon.dsh 序列号 (技能图片). */
  effectIndex: number;
  level: number;
  /** raw job 0=天机 1=幻剑 2=鬼道 */
  rawJob: number;
  /** skill.dsh 列 7「价值」——导师学习铜钱. */
  learnPrice: number;
  mpCost: number;
  targetKind: SkillTargetKind;
  /** skill.dsh 使用限制：1=地图可施放（清风拂体）. */
  useLimit: number;
  /** skill.dsh 生命变化（正数为治疗）. */
  hpChange: number;
};

export type BattleSkillRow = {
  id: number;
  name: string;
  /** skill.dsh「说明」；目录未命中时可缺省. */
  desc?: string;
  effectIndex?: number;
  mpCost?: number;
  /** Default 3 (单敌) when catalog miss. */
  targetKind: SkillTargetKind;
  useLimit?: number;
  hpChange?: number;
};

let dshBase: SkillCatalogItem[] | null = null;
let cached: SkillCatalogItem[] | null = null;

function clampTargetKind(v: number): SkillTargetKind {
  const n = v | 0;
  if (n >= 0 && n <= 4) return n as SkillTargetKind;
  return 3;
}

/** Parse plain skill.dsh bytes (also used by node smoke without fetch). */
export function parseSkillCatalogBytes(raw: Uint8Array): SkillCatalogItem[] {
  const out: SkillCatalogItem[] = [];
  if (raw.length < 16) return out;
  const cols = readU32LE(raw, 4);
  const rows = readU32LE(raw, 8);
  if (cols < 7 || cols > 64 || rows > 512) return out;
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
    const id = parseDshInt(cells[0] ?? "");
    const name = cells[1] ?? "";
    const desc = (cells[3] ?? "").replace(/^-+\s*$/, "").trim();
    const effectIndex = parseDshInt(cells[2] ?? "");
    const level = parseDshInt(cells[4] ?? "");
    const rawJob = parseDshInt(cells[6] ?? "");
    const learnPrice = parseDshInt(cells[7] ?? "");
    const targetKind = clampTargetKind(parseDshInt(cells[10] ?? "3", 3));
    const mpCost = parseDshInt(cells[12] ?? "");
    const useLimit = parseDshInt(cells[8] ?? "");
    const hpChange = parseDshInt(cells[14] ?? "");
    if (id > 0 && name) {
      out.push({
        id,
        name,
        desc,
        effectIndex,
        level,
        rawJob,
        learnPrice,
        mpCost,
        targetKind,
        useLimit,
        hpChange,
      });
    }
  }
  return out;
}

/** 鬼道单体回血（清风拂体）：使用限制=1 且目标指向=1. */
export function isMapHealSkill(sk: {
  id: number;
  name?: string;
  targetKind?: SkillTargetKind;
  useLimit?: number;
  hpChange?: number;
}): boolean {
  if ((sk.useLimit ?? 0) === 1 && (sk.targetKind ?? 3) === 1 && (sk.hpChange ?? 0) > 0) {
    return true;
  }
  return /^清风拂体/.test(sk.name ?? "") || (sk.id >= 211 && sk.id <= 215);
}

export async function loadSkillCatalog(): Promise<SkillCatalogItem[]> {
  if (!dshBase) {
    const raw = await fetchResourceBytes("skill.dsh");
    dshBase = parseSkillCatalogBytes(raw);
  }
  try {
    const custom = await loadCustomCatalog();
    cached = mergeCustomIntoSkillCatalog(dshBase, custom);
  } catch {
    cached = dshBase;
  }
  return cached;
}

/** Sync lookup after warmItemCatalogs / loadSkillCatalog. */
export function lookupSkillSync(skillId: number): SkillCatalogItem | null {
  const id = skillId >>> 0;
  if (!id) return null;
  const cat = cached ?? dshBase;
  if (!cat) return null;
  return cat.find((row) => row.id === id) ?? null;
}

/** actorinfo jobIndex is already raw 0..2. */
export function clampRawJob(jobIndex: number): number {
  return Math.max(0, Math.min(2, jobIndex | 0));
}

export function skillNeedsTargetPick(kind: SkillTargetKind): boolean {
  return kind === 1 || kind === 3;
}

/**
 * Resolve server learned IDs against skill.dsh for this role's job.
 * Unknown IDs stay as `技能#id` so Operate still uses the server id.
 */
export function resolveLearnedSkillsFromCatalog(
  catalog: SkillCatalogItem[],
  jobIndex: number,
  learnedIds: number[],
): BattleSkillRow[] {
  const rawJob = clampRawJob(jobIndex);
  const byId = new Map<number, SkillCatalogItem>();
  for (const row of catalog) {
    if (row.rawJob === rawJob) byId.set(row.id, row);
  }
  const out: BattleSkillRow[] = [];
  for (const id of learnedIds) {
    if (!id) continue;
    const hit = byId.get(id);
    if (hit) {
      out.push({
        id: hit.id,
        name: hit.name,
        desc: hit.desc,
        effectIndex: hit.effectIndex,
        mpCost: hit.mpCost,
        targetKind: hit.targetKind,
        useLimit: hit.useLimit,
        hpChange: hit.hpChange,
      });
    } else {
      out.push({ id, name: `技能#${id}`, targetKind: 3 });
    }
  }
  return out;
}

export async function resolveLearnedSkills(
  jobIndex: number,
  learnedIds: number[],
): Promise<BattleSkillRow[]> {
  return resolveLearnedSkillsFromCatalog(
    await loadSkillCatalog(),
    jobIndex,
    learnedIds,
  );
}

/** Starter skill id for demo / fallback when server sends none. */
export function starterSkillIdForJob(jobIndex: number): number {
  const raw = clampRawJob(jobIndex);
  if (raw === 1) return 101;
  if (raw === 2) return 201;
  return 1;
}
