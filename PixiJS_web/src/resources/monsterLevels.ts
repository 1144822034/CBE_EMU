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

/** Runtime rows from server_monsters (via gateway → admin). Wins over builtin. */
const MONSTER_LEVEL_OVERRIDES: Record<number, number> = {};

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

export function monsterLevelForEnemyId(enemyId: number): number | undefined {
  const id = enemyId >>> 0;
  if (!id) return undefined;
  const over = MONSTER_LEVEL_OVERRIDES[id];
  if (over && over > 0) return over;
  const level = MONSTER_LEVEL_BY_ID[id];
  return level && level > 0 ? level : undefined;
}

/** Map nameplate like 6级野猪. Skips if name already has a level prefix. */
export function formatMonsterMapName(name: string, enemyId?: number): string {
  const base = (name || "怪").trim() || "怪";
  if (/^\d+级/.test(base) || /^Lv\.?\d+/i.test(base)) return base;
  const level = enemyId != null ? monsterLevelForEnemyId(enemyId) : undefined;
  if (!level) return base;
  return `${level}级${base}`;
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
    const ctrl = typeof AbortController !== "undefined" ? new AbortController() : null;
    const timer =
      ctrl != null
        ? window.setTimeout(() => ctrl.abort(), 4000)
        : 0;
    const res = await fetch(target, {
      method: "GET",
      signal: ctrl?.signal,
      cache: "no-store",
    });
    if (timer) window.clearTimeout(timer);
    if (!res.ok) return 0;
    const data = (await res.json()) as { overrides?: Record<string, number> };
    if (!data?.overrides || typeof data.overrides !== "object") return 0;
    const n = applyMonsterLevelOverrides(data.overrides);
    if (n > 0) {
      console.info(`[monsterLevels] overrides=${n} from ${target}`);
    }
    return n;
  } catch (e) {
    console.warn(`[monsterLevels] load failed ${target}`, e);
    return 0;
  }
}
