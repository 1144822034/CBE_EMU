/**
 * NPC display-name → scene from server_dynamic_npcs (gateway → admin).
 * Used by the left quest tracker for「提交NPC所在场景」.
 */

/** UTF-8 display_name → runtime scene key. */
const NPC_SCENE_BY_NAME: Map<string, string> = new Map();

let lastLoadAtMs = 0;
const RELOAD_MIN_MS = 10_000;

export function lookupNpcSceneFromDb(npcName: string): string | null {
  const key = (npcName || "").replace(/\0+$/g, "").trim();
  if (!key) return null;
  return NPC_SCENE_BY_NAME.get(key) || null;
}

export function applyNpcSceneIndex(scenes: Record<string, string>): number {
  NPC_SCENE_BY_NAME.clear();
  let n = 0;
  for (const [name, scene] of Object.entries(scenes)) {
    const key = (name || "").trim();
    const sc = (scene || "").trim();
    if (!key || !sc) continue;
    NPC_SCENE_BY_NAME.set(key, sc);
    n++;
  }
  return n;
}

/**
 * Derive HTTP npc-scenes URL from live gateway WS URL.
 * ws://host:19092 → http://host:19092/api/npc-scenes
 */
export function defaultNpcScenesUrl(gatewayWsUrl?: string): string {
  if (typeof window !== "undefined") {
    const params = new URLSearchParams(window.location.search);
    const fromQuery = params.get("admin") || params.get("npcScenes");
    if (fromQuery) return normalizeNpcScenesUrl(fromQuery);
    try {
      const stored = window.localStorage.getItem("pix_admin");
      if (stored) return normalizeNpcScenesUrl(stored);
    } catch {
      /* private mode */
    }
  }
  const ws = gatewayWsUrl || "";
  try {
    const u = new URL(ws);
    const httpProto = u.protocol === "wss:" ? "https:" : "http:";
    return `${httpProto}//${u.host}/api/npc-scenes`;
  } catch {
    return "http://127.0.0.1:19092/api/npc-scenes";
  }
}

function normalizeNpcScenesUrl(raw: string): string {
  const s = raw.trim();
  if (!s) return s;
  if (s.includes("/api/npc-scenes")) return s;
  if (s.includes("/api/monster-levels")) {
    return s.replace("/api/monster-levels", "/api/npc-scenes");
  }
  return s.replace(/\/+$/, "") + "/api/npc-scenes";
}

/** Fetch name→scene map; failures leave prior cache (or empty). */
export async function loadNpcSceneIndex(url?: string): Promise<number> {
  const now = Date.now();
  if (lastLoadAtMs && now - lastLoadAtMs < RELOAD_MIN_MS && NPC_SCENE_BY_NAME.size) {
    return NPC_SCENE_BY_NAME.size;
  }
  const target = url || defaultNpcScenesUrl();
  try {
    const ctrl = typeof AbortController !== "undefined" ? new AbortController() : null;
    const timer =
      ctrl != null ? window.setTimeout(() => ctrl.abort(), 4000) : 0;
    const res = await fetch(target, {
      method: "GET",
      signal: ctrl?.signal,
      cache: "no-store",
    });
    if (timer) window.clearTimeout(timer);
    if (!res.ok) return NPC_SCENE_BY_NAME.size;
    const data = (await res.json()) as {
      ok?: boolean;
      scenes?: Record<string, string>;
    };
    if (!data?.scenes || typeof data.scenes !== "object") {
      return NPC_SCENE_BY_NAME.size;
    }
    const n = applyNpcSceneIndex(data.scenes);
    lastLoadAtMs = Date.now();
    if (n > 0) {
      console.info(`[npcScenes] entries=${n} from ${target}`);
    }
    return n;
  } catch (e) {
    console.warn(`[npcScenes] load failed ${target}`, e);
    return NPC_SCENE_BY_NAME.size;
  }
}
