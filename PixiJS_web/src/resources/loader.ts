import { unwrapResource } from "./lzss";
import { decodeLkv2Gif, type GifRgba } from "./lkv2Gif";
import {
  parseSce2,
  parseSceCombatSpawns,
  parseSceDecorations,
  parseSceInteractives,
  parseScePortals,
  parseSceTeleportStones,
  isTeleportStoneActor,
  siblingMapName,
  type SceCombatSpawn,
  type SceDecoration,
  type SceHeader,
  type SceInteractive,
  type ScePortal,
} from "./sce";
import { composeMapRgba, parseMapPayload, type MapPreview } from "./map";
import { sceneAssetCandidates } from "../net/protocol";

type CacheEntry = {
  buf: ArrayBuffer;
  /** Server `X-JH-Resource-Stamp` / ETag when known. */
  stamp: string | null;
};

const cache = new Map<string, CacheEntry>();

export type FetchResourceOpts = {
  /**
   * Compare against server stamp before reusing memory cache.
   * Default: auto for scene maps / e_* combat assets (CBE should_version_check).
   */
  revalidate?: boolean;
};

/**
 * Match CBE `should_version_check_existing`: .sce/.map, bare scene keys,
 * and e_*.actor / e_*.gif (map combat actors). UI skins skip probe.
 */
export function shouldVersionCheckResource(name: string): boolean {
  if (isPackagedNativeApp()) return false;
  const leaf = name.split(/[/\\]/).pop() || name;
  const lower = leaf.toLowerCase();
  if (lower.endsWith(".sce") || lower.endsWith(".map")) return true;
  if (/^e_.+\.(actor|gif)$/i.test(leaf)) return true;
  if (!leaf.includes(".")) return true;
  return false;
}

/** Capacitor / Cordova WebView — assets are baked in; skip HEAD revalidate. */
function isPackagedNativeApp(): boolean {
  try {
    const cap = (window as unknown as { Capacitor?: { isNativePlatform?: () => boolean } })
      .Capacitor;
    if (cap?.isNativePlatform?.()) return true;
  } catch {
    /* ignore */
  }
  return false;
}

function resourceUrl(name: string): string {
  return `/JHOnlineData/${encodeURIComponent(name)}`;
}

function stampFromHeaders(headers: Headers): string | null {
  return (
    headers.get("X-JH-Resource-Stamp") ||
    headers.get("ETag")?.replace(/^W\//, "").replace(/"/g, "") ||
    null
  );
}

/** HEAD probe — returns null if server has no stamp / HEAD unsupported. */
export async function probeResourceStamp(name: string): Promise<string | null> {
  try {
    const res = await fetch(resourceUrl(name), {
      method: "HEAD",
      cache: "no-store",
    });
    if (!res.ok) return null;
    return stampFromHeaders(res.headers);
  } catch {
    return null;
  }
}

export function getCachedResourceStamp(name: string): string | null {
  return cache.get(name)?.stamp ?? null;
}

export function invalidateResourceCache(name?: string): void {
  if (name) cache.delete(name);
  else cache.clear();
}

export async function fetchResourceBytes(
  name: string,
  opts?: FetchResourceOpts,
): Promise<Uint8Array> {
  const key = name;
  const revalidate =
    opts?.revalidate ?? shouldVersionCheckResource(name);
  const hit = cache.get(key);

  if (hit && revalidate) {
    const remote = await probeResourceStamp(name);
    if (remote && hit.stamp && remote === hit.stamp) {
      return new Uint8Array(hit.buf);
    }
    if (remote && hit.stamp && remote !== hit.stamp) {
      console.info(
        `[resource] version-stale ${name} local=${hit.stamp} remote=${remote}`,
      );
      cache.delete(key);
    } else if (!remote && hit.stamp) {
      // Stamp endpoint missing — keep cache (static host without plugin).
      return new Uint8Array(hit.buf);
    } else if (remote && !hit.stamp) {
      // Had bytes without stamp; refresh once to bind stamp.
      cache.delete(key);
    } else {
      return new Uint8Array(hit.buf);
    }
  } else if (hit && !revalidate) {
    return new Uint8Array(hit.buf);
  }

  const res = await fetch(resourceUrl(name), { cache: "no-store" });
  if (!res.ok) throw new Error(`fetch ${name}: HTTP ${res.status}`);
  const buf = await res.arrayBuffer();
  const stamp = stampFromHeaders(res.headers);
  cache.set(key, { buf, stamp });
  return new Uint8Array(buf);
}

export async function resourceExists(name: string): Promise<boolean> {
  try {
    const res = await fetch(resourceUrl(name), {
      method: "HEAD",
      cache: "no-store",
    });
    if (res.ok) return true;
    // Some static servers reject HEAD — fall back to GET probe via cache.
    if (res.status === 405 || res.status === 501) {
      await fetchResourceBytes(name, { revalidate: false });
      return true;
    }
    return false;
  } catch {
    try {
      await fetchResourceBytes(name, { revalidate: false });
      return true;
    } catch {
      return false;
    }
  }
}

export async function loadDecodedPayload(
  name: string,
  opts?: FetchResourceOpts,
): Promise<Uint8Array> {
  return unwrapResource(await fetchResourceBytes(name, opts));
}

export async function loadGif(
  name: string,
  opts?: FetchResourceOpts,
): Promise<GifRgba> {
  const payload = await loadDecodedPayload(name, opts);
  return decodeLkv2Gif(payload);
}

export type LoadedScene = {
  sceneFile: string;
  sce: SceHeader;
  map: MapPreview;
  mapFile: string;
  width: number;
  height: number;
  rgba: Uint8ClampedArray;
  /** Wild monsters from SCE kind=3 (not 27/11 NPC catalog). */
  combatSpawns: SceCombatSpawn[];
  /** Edge / meta / named portals. */
  portals: ScePortal[];
  /** SCE decoration props (b_flowers01 / FB_tree …). */
  decorations: SceDecoration[];
  /** SCE interactive props (teleport stones etc.). */
  interactives: SceInteractive[];
  /** Teleport stone subset of interactives. */
  teleportStones: SceInteractive[];
};

export async function loadSceneMap(sceneFile: string): Promise<LoadedScene> {
  // Always version-check scene shell + map tiles when entering a map.
  const revalidate = true as const;
  const candidates = sceneAssetCandidates(sceneFile);
  let resolved = candidates[0]!;
  let scePayload: Uint8Array | null = null;
  let lastErr: unknown = null;
  for (const name of candidates) {
    try {
      scePayload = await loadDecodedPayload(name, { revalidate });
      resolved = name;
      break;
    } catch (e) {
      lastErr = e;
    }
  }
  if (!scePayload) throw lastErr ?? new Error(`scene not found: ${sceneFile}`);

  let sce: SceHeader;
  let mapFile: string;
  try {
    sce = parseSce2(scePayload);
    mapFile = sce.mapName;
  } catch {
    mapFile = siblingMapName(resolved);
    sce = {
      width: 0,
      height: 0,
      mapName: mapFile,
      payload: scePayload,
      base: 0,
    };
  }

  let mapPayload: Uint8Array;
  try {
    mapPayload = await loadDecodedPayload(mapFile, { revalidate });
  } catch (err) {
    const alt = siblingMapName(resolved);
    if (alt === mapFile) throw err;
    mapFile = alt;
    mapPayload = await loadDecodedPayload(mapFile, { revalidate });
  }

  const map = parseMapPayload(mapPayload, mapFile);
  const sheets = await Promise.all(
    map.imageNames.map(async (gifName) => {
      try {
        const g = await loadGif(gifName, { revalidate });
        return {
          rgb565: g.rgb565,
          pitch: g.pitch,
          width: g.width,
          height: g.height,
        };
      } catch (e) {
        console.warn("[scene] gif failed", gifName, e);
        return null;
      }
    }),
  );

  const composed = composeMapRgba(map, sheets);
  const combatSpawns = parseSceCombatSpawns(sce.payload);
  const portals = parseScePortals(sce.payload);
  const decorations = parseSceDecorations(sce.payload);
  const interactives = parseSceInteractives(sce.payload);
  const fromInteractive = interactives.filter((n) =>
    isTeleportStoneActor(n.actorResource),
  );
  const fromScan = parseSceTeleportStones(sce.payload);
  const stoneKeys = new Set(fromInteractive.map((s) => `${s.actorResource}@${s.x},${s.y}`));
  const teleportStones = [
    ...fromInteractive,
    ...fromScan.filter((s) => !stoneKeys.has(`${s.actorResource}@${s.x},${s.y}`)),
  ];
  return {
    sceneFile: resolved,
    sce,
    map,
    mapFile,
    width: composed.width,
    height: composed.height,
    rgba: composed.rgba,
    combatSpawns,
    portals,
    decorations,
    interactives,
    teleportStones,
  };
}

export type SceneIndex = {
  assetRootHint: string;
  defaultScene: string;
  scenes: string[];
};

export async function loadSceneIndex(): Promise<SceneIndex> {
  const res = await fetch("/scene-index.json");
  if (res.ok) return (await res.json()) as SceneIndex;
  // Fallback curated list (UTF-8 names).
  return {
    assetRootHint: "web/fs/JHOnlineData",
    defaultScene: "01桃花岛_01.sce",
    scenes: [
      "c00蓬莱仙岛_01.sce",
      "01桃花岛_01.sce",
      "01桃花岛_02.sce",
      "11终南山_01.sce",
    ],
  };
}
