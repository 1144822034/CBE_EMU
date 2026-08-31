/**
 * TexturePacker / Cocos2d plist format 2 atlas loader for PixiJS.
 * Used by JHOnlineData/chibang wing sheets (`sfx_*_0.plist` + `.png`).
 */
import { Assets, Rectangle, Texture } from "pixi.js";
import { jhOnlineDataPathUrl } from "./publicUrl";

export type PlistFrame = {
  name: string;
  /** Atlas rect as written in plist (pre-swap). */
  frame: { x: number; y: number; w: number; h: number };
  /** Cocos display offset (Y-up). */
  offset: { x: number; y: number };
  rotated: boolean;
  sourceSize: { w: number; h: number };
  sourceColorRect: { x: number; y: number; w: number; h: number };
};

export type LoadedPlistAtlas = {
  key: string;
  textureFileName: string;
  /** Absolute-or-app URL passed to Assets.load (refcount / unload key). */
  pngUrl: string;
  metaSize: { w: number; h: number };
  frames: PlistFrame[];
  /** Textures sorted by frame name (animation order). */
  textures: Texture[];
  base: Texture;
  /** Shared logical canvas size (all frames trimmed into this). */
  commonSize: { w: number; h: number };
};

/** Bump when atlas post-process changes so stale in-memory caches refresh. */
const ATLAS_CACHE_VER = "centroid-v4-ui-perframe";

const cache = new Map<string, Promise<LoadedPlistAtlas>>();

/**
 * Display retain count per PNG URL (not per plist prefix).
 * Mount dir packs share one PNG across face/prefix atlas entries — refcount
 * must follow the shared Assets texture, never unload while peers still paint.
 */
const pngRetain = new Map<string, number>();
const pngBase = new Map<string, Texture>();
const unloadTimers = new Map<string, ReturnType<typeof setTimeout>>();
/** Delay so brief face/act churn does not thrash GPU re-upload. */
const UNLOAD_DELAY_MS = isNativePlatform() ? 30_000 : 2500;

/** atlas cache key → pngUrl (for sync eviction on idle unload). */
const cacheKeysByPng = new Map<string, Set<string>>();

function isNativePlatform(): boolean {
  try {
    const cap = (
      globalThis as unknown as {
        Capacitor?: { isNativePlatform?: () => boolean };
      }
    ).Capacitor;
    return !!cap?.isNativePlatform?.();
  } catch {
    return false;
  }
}

function rememberCacheKey(pngUrl: string, key: string): void {
  let set = cacheKeysByPng.get(pngUrl);
  if (!set) {
    set = new Set();
    cacheKeysByPng.set(pngUrl, set);
  }
  set.add(key);
}

/** Drop in-memory atlas promises for this PNG so the next load rebuilds live textures. */
function evictAtlasCacheForPng(pngUrl: string): void {
  const keys = cacheKeysByPng.get(pngUrl);
  if (keys) {
    for (const key of keys) cache.delete(key);
    cacheKeysByPng.delete(pngUrl);
  }
  // Also sweep any orphan keys (prefix packs sharing png before remember ran).
  for (const [key, prom] of [...cache.entries()]) {
    void prom.then((atlas) => {
      if (atlas.pngUrl === pngUrl && cache.get(key) === prom) cache.delete(key);
    }).catch(() => {
      if (cache.get(key) === prom) cache.delete(key);
    });
  }
}

function cancelScheduledUnload(pngUrl: string): void {
  const t = unloadTimers.get(pngUrl);
  if (t != null) {
    clearTimeout(t);
    unloadTimers.delete(pngUrl);
  }
}

/**
 * Pixi official: `texture.source.unload()` frees GPU, keeps JS Texture objects.
 * Never `Assets.unload` here — shared multi-face sheets went blank last time.
 *
 * Browser: skip idle unload — 2.5s unload + cache reuse caused RENDER_CRASH and
 * peer cosmetics never staying visible. Native keeps delayed unload + cache evict.
 */
function scheduleGpuUnload(pngUrl: string): void {
  cancelScheduledUnload(pngUrl);
  if (!isNativePlatform()) {
    /* Browser: only allow TextureGC via autoGarbageCollect after release. */
    return;
  }
  unloadTimers.set(
    pngUrl,
    setTimeout(() => {
      unloadTimers.delete(pngUrl);
      if ((pngRetain.get(pngUrl) ?? 0) > 0) return;
      const base = pngBase.get(pngUrl);
      pngBase.delete(pngUrl);
      evictAtlasCacheForPng(pngUrl);
      if (!base || base.destroyed) return;
      try {
        const src = base.source;
        if (src && !src.destroyed) src.unload();
      } catch (e) {
        console.warn("[plist] source.unload", e);
      }
    }, UNLOAD_DELAY_MS),
  );
}

/** Re-touch GPU after idle unload / context restore while overlays still retain. */
function ensurePlistSourceAlive(atlas: LoadedPlistAtlas): void {
  try {
    const src = atlas.base?.source;
    if (!src || src.destroyed) {
      console.warn(
        "[plist] base source dead on retain",
        atlas.pngUrl.replace(/^.*\//, ""),
      );
      evictAtlasCacheForPng(atlas.pngUrl);
      return;
    }
    src.autoGarbageCollect = false;
    src.update?.();
  } catch (e) {
    console.warn("[plist] ensure source", e);
  }
}

/** Retain shared PNG while an overlay displays frames from this atlas. */
export function retainPlistAtlas(atlas: LoadedPlistAtlas | null | undefined): void {
  if (!atlas?.pngUrl) return;
  const url = atlas.pngUrl;
  cancelScheduledUnload(url);
  pngBase.set(url, atlas.base);
  pngRetain.set(url, (pngRetain.get(url) ?? 0) + 1);
  ensurePlistSourceAlive(atlas);
}

/** Drop one display hold; GPU unload only when nobody retains the PNG. */
export function releasePlistAtlas(atlas: LoadedPlistAtlas | null | undefined): void {
  if (!atlas?.pngUrl) return;
  const url = atlas.pngUrl;
  const n = (pngRetain.get(url) ?? 0) - 1;
  if (n <= 0) {
    pngRetain.delete(url);
    try {
      atlas.base.source.autoGarbageCollect = true;
    } catch {
      /* ignore */
    }
    scheduleGpuUnload(url);
  } else {
    pngRetain.set(url, n);
  }
}

/** Swap overlay atlas: retain next then release prev (same PNG is a no-op net). */
export function rebindPlistAtlas(
  prev: LoadedPlistAtlas | null | undefined,
  next: LoadedPlistAtlas | null | undefined,
): LoadedPlistAtlas | null {
  if (prev === next) return next ?? null;
  if (next) retainPlistAtlas(next);
  if (prev) releasePlistAtlas(prev);
  return next ?? null;
}

/**
 * After `await loadPlistAtlas`: rebind only if still current.
 * If a concurrent `clear()` bumped loadGen mid-flight, release the just-bound
 * atlas so we never leave a Sprite on-stage with retain=0 (idle unload →
 * RENDER_CRASH / alphaMode).
 */
export function bindPlistAtlasIfCurrent(
  prev: LoadedPlistAtlas | null | undefined,
  next: LoadedPlistAtlas,
  stillCurrent: () => boolean,
): LoadedPlistAtlas | null {
  if (!stillCurrent()) return prev ?? null;
  const bound = rebindPlistAtlas(prev, next);
  if (!stillCurrent()) return rebindPlistAtlas(bound, null);
  return bound;
}

/** After webglcontextrestored: re-touch GPU for atlases still in Promise cache. */
export function nudgePlistAtlasesAfterGpuResume(): void {
  for (const p of cache.values()) {
    void p.then((atlas) => {
      try {
        atlas.base.source.update?.();
      } catch {
        /* ignore */
      }
    }).catch(() => {
      /* ignore */
    });
  }
}

/** Forensic / DevTools. */
export function plistAtlasGpuStats(): {
  cachedSheets: number;
  retainedPngs: number;
  pendingUnload: number;
} {
  return {
    cachedSheets: cache.size,
    retainedPngs: pngRetain.size,
    pendingUnload: unloadTimers.size,
  };
}
function parseBracePair(s: string): { a: number; b: number } | null {
  const m = String(s || "")
    .trim()
    .match(/^\{([-\d.]+),\s*([-\d.]+)\}$/);
  if (!m) return null;
  return { a: Number(m[1]), b: Number(m[2]) };
}

function parseFrameRect(
  s: string,
): { x: number; y: number; w: number; h: number } | null {
  const m = String(s || "")
    .trim()
    .match(/^\{\{([-\d.]+),\s*([-\d.]+)\},\s*\{([-\d.]+),\s*([-\d.]+)\}\}$/);
  if (!m) return null;
  return {
    x: Number(m[1]),
    y: Number(m[2]),
    w: Number(m[3]),
    h: Number(m[4]),
  };
}

/**
 * Format-2 plist text parse (no DOMParser — works in browser and Node smoke).
 * Expects TexturePacker XML with `frames` / `metadata` dicts.
 */
export function parsePlistAtlasXml(
  xmlText: string,
  opts?: { uiAtlas?: boolean },
): {
  textureFileName: string;
  metaSize: { w: number; h: number };
  frames: PlistFrame[];
} {
  const text = String(xmlText || "");
  const frames: PlistFrame[] = [];
  const frameBlockRe =
    /<key>([^<]+\.png)<\/key>\s*<dict>([\s\S]*?)<\/dict>/g;
  let m: RegExpExecArray | null;
  while ((m = frameBlockRe.exec(text))) {
    const name = m[1]!.trim();
    const body = m[2]!;
    /* Wing/mount/chenghao sheets use _0000.png … _0015.png; UI atlases do not. */
    if (!opts?.uiAtlas && !/_\d{4}\.png$/i.test(name)) continue;
    const frameStr = body.match(
      /<key>frame<\/key>\s*<string>([^<]*)<\/string>/,
    )?.[1];
    const offsetStr = body.match(
      /<key>offset<\/key>\s*<string>([^<]*)<\/string>/,
    )?.[1];
    const srcSizeStr = body.match(
      /<key>sourceSize<\/key>\s*<string>([^<]*)<\/string>/,
    )?.[1];
    const srcRectStr = body.match(
      /<key>sourceColorRect<\/key>\s*<string>([^<]*)<\/string>/,
    )?.[1];
    const rotated = /<key>rotated<\/key>\s*<true\s*\/>/i.test(body);
    const frame = parseFrameRect(frameStr || "");
    const offset = parseBracePair(offsetStr || "");
    const srcSize = parseBracePair(srcSizeStr || "");
    const srcRect = parseFrameRect(srcRectStr || "");
    if (!frame || !offset || !srcSize) continue;
    frames.push({
      name,
      frame,
      offset: { x: offset.a, y: offset.b },
      rotated,
      sourceSize: { w: srcSize.a, h: srcSize.b },
      sourceColorRect: srcRect
        ? { x: srcRect.x, y: srcRect.y, w: srcRect.w, h: srcRect.h }
        : { x: 0, y: 0, w: frame.w, h: frame.h },
    });
  }
  frames.sort((a, b) =>
    a.name.localeCompare(b.name, undefined, { numeric: true }),
  );

  const metaMatch = text.match(
    /<key>metadata<\/key>\s*<dict>([\s\S]*?)<\/dict>/,
  );
  const metaBody = metaMatch?.[1] || "";
  const texName =
    metaBody.match(
      /<key>textureFileName<\/key>\s*<string>([^<]*)<\/string>/,
    )?.[1]?.trim() ||
    metaBody.match(
      /<key>realTextureFileName<\/key>\s*<string>([^<]*)<\/string>/,
    )?.[1]?.trim() ||
    "";
  const sizeStr = metaBody.match(
    /<key>size<\/key>\s*<string>([^<]*)<\/string>/,
  )?.[1];
  const sizePair = parseBracePair(sizeStr || "");
  const format = Number(
    metaBody.match(/<key>format<\/key>\s*<integer>(\d+)<\/integer>/)?.[1] ||
      "0",
  );
  if (format && format !== 2) {
    console.warn(`[plist] unexpected format=${format} (expected 2)`);
  }
  if (!texName) throw new Error("plist missing textureFileName");
  if (!frames.length) throw new Error("plist has no frames");
  return {
    textureFileName: texName,
    metaSize: sizePair ? { w: sizePair.a, h: sizePair.b } : { w: 0, h: 0 },
    frames,
  };
}

type AtlasPixels = { data: Uint8ClampedArray; w: number; h: number };

async function loadAtlasPixels(pngUrl: string): Promise<AtlasPixels | null> {
  try {
    if (typeof createImageBitmap !== "function" || typeof document === "undefined") {
      return null;
    }
    const blob = await (await fetch(pngUrl, { cache: "force-cache" })).blob();
    const bmp = await createImageBitmap(blob);
    const canvas = document.createElement("canvas");
    canvas.width = bmp.width;
    canvas.height = bmp.height;
    const ctx = canvas.getContext("2d", { willReadFrequently: true });
    if (!ctx) {
      bmp.close();
      return null;
    }
    ctx.drawImage(bmp, 0, 0);
    const img = ctx.getImageData(0, 0, bmp.width, bmp.height);
    bmp.close();
    return { data: img.data, w: img.width, h: img.height };
  } catch (e) {
    console.warn("[plist] pixel sample failed; falling back to box-center", e);
    return null;
  }
}

type OpaqueMetrics = {
  cx: number;
  cy: number;
  w: number;
  h: number;
  /** Opaque sample count in logical frame. */
  n: number;
  /** Opaque samples with lx < w/2 (balance / thrash detect). */
  leftN: number;
  /** Opaque bounding-box width in logical space (0 if empty). */
  bboxW: number;
  bboxH: number;
};

/**
 * Opaque-pixel centroid in logical (unrotated) frame space.
 * TexturePacker `rotated` = 90° CW in atlas; inverse maps atlas→logical.
 */
function opaqueCentroid(img: AtlasPixels, fr: PlistFrame): OpaqueMetrics {
  const { x, y, w, h } = fr.frame;
  const aw = fr.rotated ? h : w;
  const ah = fr.rotated ? w : h;
  let sx = 0;
  let sy = 0;
  let n = 0;
  let leftN = 0;
  let minX = w;
  let maxX = -1;
  let minY = h;
  let maxY = -1;
  for (let j = 0; j < ah; j++) {
    for (let i = 0; i < aw; i++) {
      const px = x + i;
      const py = y + j;
      if (px < 0 || py < 0 || px >= img.w || py >= img.h) continue;
      if (img.data[(py * img.w + px) * 4 + 3]! < 16) continue;
      let lx: number;
      let ly: number;
      if (fr.rotated) {
        // Match Pixi `rotate: 2` undo of TexturePacker 90° CW pack.
        lx = j;
        ly = aw - 1 - i;
      } else {
        lx = i;
        ly = j;
      }
      sx += lx;
      sy += ly;
      n++;
      if (lx < w * 0.5) leftN++;
      if (lx < minX) minX = lx;
      if (lx > maxX) maxX = lx;
      if (ly < minY) minY = ly;
      if (ly > maxY) maxY = ly;
    }
  }
  return {
    w,
    h,
    n,
    leftN,
    bboxW: n ? maxX - minX + 1 : 0,
    bboxH: n ? maxY - minY + 1 : 0,
    cx: n ? sx / n : w * 0.5,
    cy: n ? sy / n : h * 0.5,
  };
}

function frameToTexture(
  base: Texture,
  fr: PlistFrame,
  commonOrig: { w: number; h: number },
  trimX: number,
  trimY: number,
): Texture {
  const { x, y, w, h } = fr.frame;
  const frame = fr.rotated
    ? new Rectangle(x, y, h, w)
    : new Rectangle(x, y, w, h);
  return new Texture({
    source: base.source,
    frame,
    orig: new Rectangle(0, 0, commonOrig.w, commonOrig.h),
    trim: new Rectangle(trimX, trimY, w, h),
    rotate: fr.rotated ? 2 : 0,
  });
}

export type LoadPlistAtlasOpts = {
  /** Keep only frames whose name starts with this prefix. */
  frameNamePrefix?: string;
  /**
   * Cocos/TexturePacker UI sheet (e.g. fruit machine `002.plist`): frame names
   * are not `_0000.png` animation cells — parse every `.png` entry.
   */
  uiAtlas?: boolean;
  /**
   * For chenghao: drop near-empty cells; if the sheet thrashes L/R (Y-spin /
   * half-frame tumble like 86043), keep only balanced full-face frames.
   */
  stableFace?: boolean;
};

/**
 * Load a JHOnlineData-relative atlas (`chibang/sfx_88010_0` without extension,
 * or with `.plist`).
 * @param opts.frameNamePrefix Keep only frames whose name starts with this
 *   (e.g. `monster_11370_0_0_3_` to pick one facing out of a multi-dir pack).
 */
export function loadPlistAtlas(
  jhRelativePath: string,
  opts?: LoadPlistAtlasOpts,
): Promise<LoadedPlistAtlas> {
  const raw = String(jhRelativePath || "").replace(/^[/\\]+/, "");
  const prefix = opts?.frameNamePrefix ? String(opts.frameNamePrefix) : "";
  const stableFace = !!opts?.stableFace;
  const uiAtlas = !!opts?.uiAtlas;
  const key = `${ATLAS_CACHE_VER}:${raw.replace(/\.plist$/i, "")}:p=${prefix}:sf=${stableFace ? 1 : 0}:ui=${uiAtlas ? 1 : 0}`;
  const hit = cache.get(key);
  if (hit) return hit;

  const promise = (async () => {
    const sheetKey = raw.replace(/\.plist$/i, "");
    const plistUrl = jhOnlineDataPathUrl(`${sheetKey}.plist`);
    const res = await fetch(plistUrl, { cache: "force-cache" });
    if (!res.ok) throw new Error(`plist HTTP ${res.status}: ${sheetKey}`);
    const xmlText = await res.text();
    const parsed = parsePlistAtlasXml(xmlText, { uiAtlas });
    const dir = sheetKey.includes("/")
      ? sheetKey.slice(0, sheetKey.lastIndexOf("/") + 1)
      : "";
    const pngPath = `${dir}${parsed.textureFileName}`;
    const pngUrl = jhOnlineDataPathUrl(pngPath);
    let base: Texture;
    try {
      base = (await Assets.load(pngUrl)) as Texture;
    } catch (e) {
      // Load never succeeded — safe to drop Assets cache entry.
      try {
        await Assets.unload(pngUrl);
      } catch {
        /* ignore */
      }
      throw e;
    }
    base.source.scaleMode = "nearest";
    // Idle sheets may GC; retainPlistAtlas sets false while displayed.
    base.source.autoGarbageCollect = true;
    if (isNativePlatform()) {
      base.source.autoGenerateMipmaps = false;
    }
    let frames = parsed.frames.filter(
      (fr) =>
        fr.frame.w > 2 &&
        fr.frame.h > 2 &&
        fr.sourceSize.w > 2 &&
        fr.sourceSize.h > 2,
    );
    if (prefix) {
      frames = frames.filter((fr) => fr.name.startsWith(prefix));
    }
    if (!frames.length) {
      // Do NOT Assets.unload(pngUrl): other prefixes/faces may share this PNG.
      throw new Error(
        prefix
          ? `plist has no frames for prefix ${prefix}: ${sheetKey}`
          : `plist has only empty/sentinel frames: ${sheetKey}`,
      );
    }
    if (!prefix && frames.length !== parsed.frames.length) {
      console.warn(
        `[plist] drop ${parsed.frames.length - frames.length} sentinel frame(s) in ${sheetKey}`,
      );
    }

    // Pixel sample is expensive (full PNG getImageData). Skip for wing/mount on
    // all platforms — browser used to sample every peer sheet and stalled so
    // only self cosmetics appeared. Chenghao stableFace still samples.
    const skipPixelSample = !stableFace;
    const pixels = skipPixelSample ? null : await loadAtlasPixels(pngUrl);
    let cents: OpaqueMetrics[] = pixels
      ? frames.map((fr) => opaqueCentroid(pixels, fr))
      : frames.map((fr) => ({
          w: fr.frame.w,
          h: fr.frame.h,
          n: 0,
          leftN: 0,
          bboxW: fr.frame.w,
          bboxH: fr.frame.h,
          cx: fr.frame.w * 0.5,
          cy: fr.frame.h * 0.5,
        }));

    if (pixels) {
      const before = frames.length;
      const maxN = Math.max(0, ...cents.map((c) => c.n));
      // Always drop near-empty atlas cells (86043 frame 0008 = 3px).
      const minKeepN = Math.max(64, Math.floor(maxN * 0.05));
      let keep = frames.map((_, i) => cents[i]!.n >= minKeepN);

      if (stableFace && maxN > 0) {
        const activeIdx: number[] = [];
        for (let i = 0; i < frames.length; i++) {
          if (cents[i]!.n >= minKeepN) activeIdx.push(i);
        }
        let cxMin = 1;
        let cxMax = 0;
        let lrMin = 1;
        let lrMax = 0;
        for (const i of activeIdx) {
          const c = cents[i]!;
          const cx = c.cx / Math.max(1, c.w);
          const lr = c.n ? c.leftN / c.n : 0.5;
          if (cx < cxMin) cxMin = cx;
          if (cx > cxMax) cxMax = cx;
          if (lr < lrMin) lrMin = lr;
          if (lr > lrMax) lrMax = lr;
        }
        // Half-frame / Y-spin sheets swing mass hard L↔R (86043).
        const thrash = cxMax - cxMin > 0.2 || lrMax - lrMin > 0.3;
        if (thrash) {
          const faceMinN = Math.max(minKeepN, Math.floor(maxN * 0.75));
          const faceKeep = frames.map((fr, i) => {
            const c = cents[i]!;
            if (c.n < faceMinN) return false;
            if (c.bboxW < Math.floor(fr.frame.w * 0.95)) return false;
            const cx = c.cx / Math.max(1, c.w);
            const lr = c.leftN / c.n;
            return Math.abs(cx - 0.5) <= 0.08 && Math.abs(lr - 0.5) <= 0.12;
          });
          if (faceKeep.filter(Boolean).length >= 2) {
            keep = faceKeep;
          }
        }
      }

      const nextFrames: PlistFrame[] = [];
      const nextCents: OpaqueMetrics[] = [];
      for (let i = 0; i < frames.length; i++) {
        if (!keep[i]) continue;
        nextFrames.push(frames[i]!);
        nextCents.push(cents[i]!);
      }
      if (nextFrames.length) {
        frames = nextFrames;
        cents = nextCents;
      }
      if (frames.length !== before) {
        console.warn(
          `[plist] drop ${before - frames.length} sparse/edge frame(s) in ${sheetKey}` +
            (stableFace ? " (stableFace)" : ""),
        );
      }
    }

    let maxW = 0;
    let maxH = 0;
    for (const f of frames) {
      if (f.frame.w > maxW) maxW = f.frame.w;
      if (f.frame.h > maxH) maxH = f.frame.h;
      if (f.sourceSize.w > maxW) maxW = f.sourceSize.w;
      if (f.sourceSize.h > maxH) maxH = f.sourceSize.h;
    }

    let textures: Texture[];
    let commonOrig: { w: number; h: number };

    if (uiAtlas) {
      // UI 图集帧尺寸悬殊（水果机含 544px 侧灯）。不可共用 commonOrig，
      // 否则小图锚在巨大透明画布上，亮灯相对暗灯会明显错位。
      // 按每帧 sourceSize + Cocos offset（Y-up → Pixi Y-down）摆 trim。
      textures = frames.map((fr) => {
        const ow = Math.max(1, fr.sourceSize.w | 0);
        const oh = Math.max(1, fr.sourceSize.h | 0);
        const trimX = (ow - fr.frame.w) * 0.5 + fr.offset.x;
        const trimY = (oh - fr.frame.h) * 0.5 - fr.offset.y;
        return frameToTexture(base, fr, { w: ow, h: oh }, trimX, trimY);
      });
      commonOrig = { w: maxW, h: maxH };
    } else {
      // Grow canvas so every content centroid can sit on the shared center.
      let commonW = maxW;
      let commonH = maxH;
      for (const c of cents) {
        commonW = Math.max(commonW, Math.ceil(2 * Math.max(c.cx, c.w - c.cx)));
        commonH = Math.max(commonH, Math.ceil(2 * Math.max(c.cy, c.h - c.cy)));
      }
      commonOrig = { w: commonW, h: commonH };
      const targetX = commonW * 0.5;
      const targetY = commonH * 0.5;
      textures = frames.map((fr, i) => {
        const c = cents[i]!;
        return frameToTexture(
          base,
          fr,
          commonOrig,
          targetX - c.cx,
          targetY - c.cy,
        );
      });
    }

    rememberCacheKey(pngUrl, key);
    return {
      key: prefix ? `${sheetKey}#${prefix}` : sheetKey,
      textureFileName: parsed.textureFileName,
      pngUrl,
      metaSize: parsed.metaSize,
      frames,
      textures,
      base,
      commonSize: commonOrig,
    };
  })();

  cache.set(key, promise);
  promise.catch(() => {
    if (cache.get(key) === promise) cache.delete(key);
  });
  return promise;
}
