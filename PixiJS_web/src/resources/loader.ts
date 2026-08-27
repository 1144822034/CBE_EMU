import { unwrapResource } from "./lzss";
import {
  decodeLkv2Gif,
  decodeRasterImageBytes,
  isRasterImageMagic,
  type GifRgba,
} from "./lkv2Gif";
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
import { jhOnlineDataUrl, publicAssetUrl } from "./publicUrl";
import {
  downloadNamedResourceViaWt,
  isLocalRasterRedesign,
  namedResourceContentVersion,
  resourceWireName,
  wantsLiveResourceSync,
  type ResourceExchange,
} from "./wtUpdate";

type CacheEntry = {
  buf: ArrayBuffer;
  /** Server `X-JH-Resource-Stamp` / ETag when known. */
  stamp: string | null;
  /** WT18/7 content-version when installed from live. */
  wtVersion: number | null;
};

const cache = new Map<string, CacheEntry>();

/** Composed scene RGBA + SCE parse — reuse when re-entering the same map. */
const SCENE_COMPOSE_MAX = 16;
const sceneComposeCache = new Map<string, LoadedScene>();

/** Live CBMS exchange for WT18/7 (set from GameSession after connect). */
let resourceExchange: ResourceExchange | null = null;

export function setResourceExchange(exchange: ResourceExchange | null): void {
  resourceExchange = exchange;
}

export type FetchResourceOpts = {
  /**
   * Compare against server stamp before reusing memory cache.
   * Default: auto for scene maps / e_* combat assets (CBE should_version_check).
   */
  revalidate?: boolean;
  /** Skip WT18/7 even when a live exchange is armed. */
  skipLiveSync?: boolean;
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
  if (/^e_.+\.(actor|gif|png)$/i.test(leaf)) return true;
  if (!leaf.includes(".")) return true;
  return false;
}

/** Capacitor / Cordova / Electron — assets are baked in; skip HEAD revalidate. */
function isPackagedNativeApp(): boolean {
  try {
    const cap = (window as unknown as { Capacitor?: { isNativePlatform?: () => boolean } })
      .Capacitor;
    if (cap?.isNativePlatform?.()) return true;
  } catch {
    /* ignore */
  }
  try {
    if (typeof navigator !== "undefined" && /Electron/i.test(navigator.userAgent)) {
      return true;
    }
    if (typeof location !== "undefined") {
      if (location.protocol === "file:" || location.protocol === "pix:") {
        return true;
      }
    }
  } catch {
    /* ignore */
  }
  return false;
}

function resourceUrl(name: string): string {
  return jhOnlineDataUrl(name);
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
  if (!name) sceneComposeCache.clear();
}

function cachePut(
  name: string,
  bytes: Uint8Array,
  stamp: string | null,
  wtVersion: number | null,
): void {
  const copy = new Uint8Array(bytes.length);
  copy.set(bytes);
  cache.set(name, {
    buf: copy.buffer,
    stamp,
    wtVersion,
  });
}

function looksLikeResourceEnvelope(bytes: Uint8Array): boolean {
  if (bytes.length < 5) return false;
  // LE u32 block_len + type, or bare type 1/2 LZSS — same as unwrapResource.
  const b0 = bytes[0]!;
  if (b0 === 1 || b0 === 2) return true;
  const blockLen =
    bytes[0]! | (bytes[1]! << 8) | (bytes[2]! << 16) | (bytes[3]! << 24);
  if (blockLen === bytes.length - 4 && blockLen >= 1) {
    const type = bytes[4]!;
    return type === 1 || type === 2;
  }
  return bytes.length > 64;
}

async function fetchBundledBytes(
  name: string,
): Promise<{ bytes: Uint8Array; stamp: string | null } | null> {
  try {
    const res = await fetch(resourceUrl(name), { cache: "no-store" });
    if (!res.ok) return null;
    const buf = await res.arrayBuffer();
    return {
      bytes: new Uint8Array(buf),
      stamp: stampFromHeaders(res.headers),
    };
  } catch {
    return null;
  }
}

/**
 * Prefer live WT18/7 for scene shells (APK/Electron have no Vite stamp path).
 * Falls back to bundled/HTTP bytes when offline or server has no leaf.
 */
async function syncLiveResource(
  name: string,
  local: Uint8Array | null,
): Promise<Uint8Array | null> {
  if (!resourceExchange || !wantsLiveResourceSync(name)) return null;
  // Keep PixiJS redesign PNG/JPEG art; do not let stock LKV2 WT18/7 clobber it.
  if (isLocalRasterRedesign(local)) {
    return local;
  }
  const wire = resourceWireName(name);
  const localVer = local
    ? namedResourceContentVersion(wire, local)
    : 1;
  try {
    const result = await downloadNamedResourceViaWt(
      resourceExchange,
      wire,
      localVer,
    );
    if (result.kind === "uptodate") {
      if (local) {
        cachePut(name, local, null, result.version);
        return local;
      }
      return null;
    }
    if (result.kind === "payload") {
      if (!looksLikeResourceEnvelope(result.bytes)) {
        console.warn(
          `[resource] wt18/7 reject ${name} wire=${wire} bytes=${result.bytes.length} reason=bad-envelope`,
        );
        return local;
      }
      try {
        unwrapResource(result.bytes);
      } catch (e) {
        console.warn(
          `[resource] wt18/7 reject ${name} wire=${wire} reason=unwrap-fail`,
          e,
        );
        return local;
      }
      console.info(
        `[resource] wt18/7 install ${name} wire=${wire} bytes=${result.bytes.length} ver=${result.version}`,
      );
      cachePut(name, result.bytes, `wt:${result.version}`, result.version);
      return result.bytes;
    }
  } catch (e) {
    console.warn(`[resource] wt18/7 failed ${name}`, e);
  }
  return null;
}

export async function fetchResourceBytes(
  name: string,
  opts?: FetchResourceOpts,
): Promise<Uint8Array> {
  const key = name;
  const revalidate =
    opts?.revalidate ?? shouldVersionCheckResource(name);
  const hit = cache.get(key);
  const useLive =
    !opts?.skipLiveSync &&
    !!resourceExchange &&
    wantsLiveResourceSync(name);

  if (hit && useLive && (revalidate || isPackagedNativeApp())) {
    const local = new Uint8Array(hit.buf);
    const synced = await syncLiveResource(name, local);
    if (synced) return synced;
    return local;
  }

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

  const bundled = await fetchBundledBytes(name);
  if (useLive) {
    const synced = await syncLiveResource(name, bundled?.bytes ?? null);
    if (synced) return synced;
  }
  if (bundled) {
    cachePut(name, bundled.bytes, bundled.stamp, null);
    return bundled.bytes;
  }
  throw new Error(`fetch ${name}: HTTP miss and no WT18/7 payload`);
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
  const raw = await fetchResourceBytes(name, opts);
  // PixiJS accepts plain PNG/JPEG/WebP dropped in as *.gif (no LKV2).
  if (isRasterImageMagic(raw)) {
    // Only punch true sheet background (not black hair / dark outlines).
    return decodeRasterImageBytes(raw, { keyNearBlack: 18 });
  }
  const payload = unwrapResource(raw);
  if (isRasterImageMagic(payload)) {
    return decodeRasterImageBytes(payload, { keyNearBlack: 18 });
  }
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

function sceneComposeKey(sceneFile: string): string {
  return sceneFile.replace(/\\/g, "/").toLowerCase();
}

export async function loadSceneMap(sceneFile: string): Promise<LoadedScene> {
  for (const alias of [sceneFile, ...sceneAssetCandidates(sceneFile)]) {
    const hit = sceneComposeCache.get(sceneComposeKey(alias));
    if (hit) {
      sceneComposeCache.delete(sceneComposeKey(alias));
      sceneComposeCache.set(sceneComposeKey(alias), hit);
      return hit;
    }
  }

  // First visit: version-check scene shell + map tiles (browser). APK bytes stay cached.
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
  const loaded: LoadedScene = {
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
  sceneComposeCache.set(sceneComposeKey(resolved), loaded);
  sceneComposeCache.set(sceneComposeKey(sceneFile), loaded);
  while (sceneComposeCache.size > SCENE_COMPOSE_MAX) {
    const oldest = sceneComposeCache.keys().next().value;
    if (oldest == null) break;
    sceneComposeCache.delete(oldest);
  }
  return loaded;
}

export type SceneIndex = {
  assetRootHint: string;
  defaultScene: string;
  scenes: string[];
};

export async function loadSceneIndex(): Promise<SceneIndex> {
  const fallback: SceneIndex = {
    assetRootHint: "web/fs/JHOnlineData",
    defaultScene: "01桃花岛_01.sce",
    scenes: [
      "c00蓬莱仙岛_01.sce",
      "01桃花岛_01.sce",
      "01桃花岛_02.sce",
      "11终南山_01.sce",
    ],
  };
  try {
    const res = await fetch(publicAssetUrl("scene-index.json"), {
      cache: "no-store",
    });
    if (res.ok) return (await res.json()) as SceneIndex;
  } catch {
    /* Electron file:// / offline — use curated list */
  }
  return fallback;
}
