/** Auto-derived from mock_server_role.c builtin monster levels. */
export const MONSTER_LEVEL_BY_ID: Record<number, number> = {
  1: 6,
  3: 1,
  4: 2,
  6: 7,
  9: 3,
  12: 10,
  13: 12,
  15: 55,
  18: 38,
  19: 28,
  21: 10,
  22: 12,
  23: 40,
  24: 28,
  25: 4,
  26: 12,
  28: 7,
  29: 8,
  30: 8,
  31: 9,
  32: 10,
  34: 11,
  36: 14,
  38: 30,
  40: 20,
  41: 20,
  42: 22,
  44: 40,
  45: 22,
  47: 27,
  48: 28,
  49: 31,
  50: 30,
  51: 31,
  52: 28,
  53: 29,
  54: 29,
  55: 34,
  56: 34,
  57: 32,
  60: 36,
  63: 60,
  64: 37,
  65: 60,
  67: 27,
  69: 24,
  70: 24,
  71: 23,
  72: 8,
  73: 5,
  74: 5,
  75: 6,
  76: 3,
  77: 4,
  78: 9,
  79: 11,
  80: 14,
  81: 13,
  82: 15,
  83: 15,
  84: 17,
  85: 17,
  86: 17,
  87: 17,
  88: 18,
  89: 18,
  90: 20,
  91: 21,
  92: 23,
  94: 26,
  97: 36,
  98: 38,
  99: 39,
  100: 40,
  101: 39,
  102: 40,
  103: 40,
  104: 41,
  105: 1,
  106: 2,
  107: 6,
  110: 46,
  111: 48,
  112: 50,
  120: 50,
  121: 52,
  122: 53,
  200: 43,
  201: 44,
  202: 45,
  300: 55,
};

/** Runtime rows from /api/monster-levels (server catalog). Wins over builtin. */
const MONSTER_LEVEL_OVERRIDES: Record<number, number> = {};
/** Admin/catalog `actor_resource` by enemy id (challenge / hangup PvE visuals). */
const MONSTER_ACTOR_OVERRIDES: Record<number, string> = {};
/** UTF-8 display names by REAL catalog id. */
const MONSTER_NAME_BY_ID: Record<number, string> = {};
/** SCE wire keep-ids that must never own a nameplate by themselves. */
const KEEP_WIRE_IDS = new Set([200, 250]);

export type CombatSpawnRealBinding = {
  scene: string;
  x: number;
  y: number;
  wire: number;
  real: number;
};

const COMBAT_SPAWN_REALS: CombatSpawnRealBinding[] = [];

export function applyMonsterLevelOverrides(
  overrides: Record<string | number, number>,
): number {
  let n = 0;
  for (const [key, raw] of Object.entries(overrides)) {
    const id = Number(key) >>> 0;
    const level = Number(raw) >>> 0;
    if (!id || !level || level > 255) continue;
    MONSTER_LEVEL_OVERRIDES[id] = level;
    n += 1;
  }
  return n;
}

export function applyMonsterActorOverrides(
  actors: Record<string | number, string>,
): number {
  let n = 0;
  for (const [key, raw] of Object.entries(actors)) {
    const id = Number(key) >>> 0;
    const actor = String(raw || "").trim();
    if (!id || !actor) continue;
    MONSTER_ACTOR_OVERRIDES[id] = actor;
    n += 1;
  }
  return n;
}

export function applyMonsterNameOverrides(
  names: Record<string | number, string>,
): number {
  let n = 0;
  for (const [key, raw] of Object.entries(names)) {
    const id = Number(key) >>> 0;
    const name = String(raw || "").trim();
    if (!id || !name) continue;
    MONSTER_NAME_BY_ID[id] = name;
    n += 1;
  }
  return n;
}

export function applyCombatSpawnReals(
  rows: Array<{
    s?: string;
    scene?: string;
    x?: number;
    y?: number;
    w?: number;
    wire?: number;
    r?: number;
    real?: number;
  }>,
): number {
  COMBAT_SPAWN_REALS.length = 0;
  let n = 0;
  for (const row of rows) {
    const scene = String(row.s || row.scene || "").trim();
    const x = Number(row.x) >>> 0;
    const y = Number(row.y) >>> 0;
    const wire = Number(row.w ?? row.wire) >>> 0;
    const real = Number(row.r ?? row.real) >>> 0;
    if (!scene || !x || !y || !wire || !real) continue;
    COMBAT_SPAWN_REALS.push({ scene, x, y, wire, real });
    n += 1;
  }
  return n;
}

/** Battle .actor for catalog/custom enemy id (e.g. dream challenge 204). */
export function monsterActorForEnemyId(enemyId: number): string | undefined {
  const id = enemyId >>> 0;
  if (!id) return undefined;
  const actor = MONSTER_ACTOR_OVERRIDES[id];
  return actor || undefined;
}

export function monsterNameForEnemyId(enemyId: number): string | undefined {
  const id = enemyId >>> 0;
  if (!id) return undefined;
  const name = MONSTER_NAME_BY_ID[id];
  return name || undefined;
}

export function monsterLevelForEnemyId(enemyId: number): number | undefined {
  const id = enemyId >>> 0;
  if (!id) return undefined;
  const over = MONSTER_LEVEL_OVERRIDES[id];
  if (over && over > 0) return over;
  const level = MONSTER_LEVEL_BY_ID[id];
  return level && level > 0 ? level : undefined;
}

function sceneKeyLoose(scene: string): string {
  return scene
    .trim()
    .replace(/\\/g, "/")
    .replace(/^.*\//, "")
    .replace(/\.sce$/i, "")
    .toLowerCase();
}

/**
 * Resolve SCE wire id → catalog REAL id for nameplates / hangup.
 * Prefer binding at (scene,x,y); else unanimous wire on that scene; else
 * keep-id (#200/#250) matched by battle .actor or SCE/catalog name; else wire.
 */
export function resolveCombatEnemyRealId(opts: {
  scene?: string;
  x: number;
  y: number;
  wire: number;
  battleActor?: string;
  sceName?: string;
}): number {
  const wire = opts.wire >>> 0;
  const x = opts.x >>> 0;
  const y = opts.y >>> 0;
  if (!wire) return 0;
  const sceneKey = opts.scene ? sceneKeyLoose(opts.scene) : "";

  let posReal = 0;
  let wireReal = 0;
  let wireHits = 0;
  let wireAmbiguous = false;
  for (const b of COMBAT_SPAWN_REALS) {
    if (sceneKey && sceneKeyLoose(b.scene) !== sceneKey) continue;
    if (x && y && b.x === x && b.y === y && b.real) {
      posReal = b.real;
    }
    if (b.wire === wire && b.real) {
      if (wireHits === 0) wireReal = b.real;
      else if (b.real !== wireReal) wireAmbiguous = true;
      wireHits += 1;
    }
  }
  if (posReal) return posReal;
  if (!wireAmbiguous && wireHits >= 1 && wireReal) return wireReal;

  /*
   * No live binding: prefer SCE displayName (same order as server
   * label_enemy_for_wire). Covers leftover world-boss rows that still carry
   * 「世界BOSS」 on borrowed wire #18 / keep-id #250 after despawn cleared
   * bindings — actor-first wrongly resolved e_ghostfiresR → #18 岩浆石火.
   */
  {
    const wantName = (opts.sceName || "").trim();
    if (wantName) {
      let matchId = 0;
      let matchCount = 0;
      for (const [idStr, name] of Object.entries(MONSTER_NAME_BY_ID)) {
        const id = Number(idStr) >>> 0;
        if (!id || KEEP_WIRE_IDS.has(id)) continue;
        if (name.trim() !== wantName) continue;
        if (matchCount === 0) matchId = id;
        else if (matchId !== id) {
          matchId = 0;
          break;
        }
        matchCount += 1;
      }
      if (matchCount >= 1 && matchId) return matchId;
    }
  }

  if (KEEP_WIRE_IDS.has(wire)) {
    const wantActor = (opts.battleActor || "").trim().toLowerCase();
    if (wantActor) {
      let matchId = 0;
      let matchCount = 0;
      for (const [idStr, actor] of Object.entries(MONSTER_ACTOR_OVERRIDES)) {
        const id = Number(idStr) >>> 0;
        if (!id || KEEP_WIRE_IDS.has(id)) continue;
        if (actor.trim().toLowerCase() !== wantActor) continue;
        if (matchCount === 0) matchId = id;
        else if (matchId !== id) {
          matchId = 0;
          break;
        }
        matchCount += 1;
      }
      if (matchCount >= 1 && matchId) return matchId;
    }
  }
  return wire;
}

/** Map nameplate like 6级野猪. Skips if name already has a level prefix. */
export function formatMonsterMapName(name: string, enemyId?: number): string {
  const id = enemyId != null ? enemyId >>> 0 : 0;
  const catalogName = id ? monsterNameForEnemyId(id) : undefined;
  const base = (catalogName || name || "怪").trim() || "怪";
  if (/^\d+级/.test(base) || /^Lv\.?\d+/i.test(base)) return base;
  const level = id ? monsterLevelForEnemyId(id) : undefined;
  if (!level) return base;
  return `${level}级${base}`;
}

/**
 * Build map nameplate + real id for one SCE combat spawn.
 * Always keys level/name by REAL catalog id (bindings / actor / name fallback).
 */
export function formatCombatSpawnNameplate(opts: {
  scene?: string;
  x: number;
  y: number;
  wire: number;
  sceName: string;
  battleActor?: string;
}): { realId: number; name: string } {
  const realId = resolveCombatEnemyRealId({
    scene: opts.scene,
    x: opts.x,
    y: opts.y,
    wire: opts.wire,
    battleActor: opts.battleActor,
    sceName: opts.sceName,
  });
  return {
    realId: realId || (opts.wire >>> 0),
    name: formatMonsterMapName(opts.sceName, realId || opts.wire),
  };
}

/**
 * Derive HTTP monster-levels URL from live gateway WS URL.
 * ws://host:19092 → http://host:19092/api/monster-levels
 * Optional `?admin=` / localStorage.pix_admin overrides the full URL or base.
 */
export function defaultMonsterLevelsUrl(gatewayWsUrl?: string): string {
  if (typeof window !== "undefined") {
    const params = new URLSearchParams(window.location.search);
    const fromQuery = params.get("admin") || params.get("monsterLevels");
    if (fromQuery) return normalizeMonsterLevelsUrl(fromQuery);
    try {
      const stored = window.localStorage.getItem("pix_admin");
      if (stored) return normalizeMonsterLevelsUrl(stored);
    } catch {
      /* private mode */
    }
  }
  const ws = gatewayWsUrl || "";
  try {
    const u = new URL(ws);
    const httpProto = u.protocol === "wss:" ? "https:" : "http:";
    return `${httpProto}//${u.host}/api/monster-levels`;
  } catch {
    return "http://127.0.0.1:19092/api/monster-levels";
  }
}

function normalizeMonsterLevelsUrl(raw: string): string {
  const s = raw.trim();
  if (!s) return s;
  if (s.includes("/api/monster-levels")) return s;
  return s.replace(/\/+$/, "") + "/api/monster-levels";
}

/** Fetch overrides; failures leave builtin table unchanged. */
export async function loadMonsterLevelOverrides(
  url?: string,
): Promise<number> {
  const target = url || defaultMonsterLevelsUrl();
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
    if (!res.ok) return 0;
    const data = (await res.json()) as {
      overrides?: Record<string, number>;
      actors?: Record<string, string>;
      names?: Record<string, string>;
      reals?: Array<Record<string, string | number>>;
    };
    if (!data || typeof data !== "object") return 0;
    const n =
      data.overrides && typeof data.overrides === "object"
        ? applyMonsterLevelOverrides(data.overrides)
        : 0;
    const actorsN =
      data.actors && typeof data.actors === "object"
        ? applyMonsterActorOverrides(data.actors)
        : 0;
    const namesN =
      data.names && typeof data.names === "object"
        ? applyMonsterNameOverrides(data.names)
        : 0;
    const realsN = Array.isArray(data.reals)
      ? applyCombatSpawnReals(data.reals)
      : 0;
    if (n > 0 || actorsN > 0 || namesN > 0 || realsN > 0) {
      console.info(
        `[monsterLevels] levels=${n} actors=${actorsN} names=${namesN} reals=${realsN} from ${target}`,
      );
    }
    return n + namesN + realsN;
  } catch (e) {
    console.warn(`[monsterLevels] load failed ${target}`, e);
    return 0;
  }
}
