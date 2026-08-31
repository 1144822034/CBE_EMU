import { readI32LE, decodeGbk } from "./binary";
import type { GifRgba } from "./lkv2Gif";
import {
  getCachedResourceStamp,
  loadDecodedPayload,
  loadGif,
  shouldVersionCheckResource,
} from "./loader";
import { isPeerCosmeticsVisible } from "./peerCosmeticsPref";

export type ActorRect = {
  left: number;
  top: number;
  right: number;
  bottom: number;
  imageIndex: number;
};

export type ActorFrame = {
  rectIndex: number;
  offsetX: number;
  offsetY: number;
};

export type ActorPart = {
  partId: number;
  frames: ActorFrame[];
};

export type ActorAnimation = {
  parts: ActorPart[];
};

export type ParsedActor = {
  name: string;
  imageNames: string[];
  rects: ActorRect[];
  animations: ActorAnimation[];
};

export type LoadedActor = ParsedActor & {
  images: (GifRgba | null)[];
  /**
   * PixiJS hi-res / redesign sheets: draw with
   * `ACTOR_DISPLAY_SCALE / artScale` so on-screen size stays native.
   * Native LKV2 actors keep artScale=1 and original rect stacking.
   */
  artScale?: number;
  /**
   * `rects` = stock CBE crop/stack (default).
   * `poses` = redesign PNG sheet whose layout ≠ .actor rects — use whole poses.
   */
  sheetMode?: "rects" | "poses";
  /**
   * Hi-res e_deity redesign: body/attack are full poses baked into rect2/rect0.
   * Do NOT stack stock sword/FX fragment rects (they are LKV2 part layout).
   */
  deityHiRes?: boolean;
  /**
   * When sheet is v34-style pose layout (not stock UV×N packed), crop bodies and
   * overlays from these absolute boxes instead of scaled .actor rects.
   */
  deityPoseLayout?: DeityPoseLayout;
  /** Detected full-body poses on a redesign sheet (imageIndex 0). */
  poseAtlas?: ActorPoseAtlas;
};

/** Absolute sheet crops for e_deity pose-layout atlas (idle/attack/sword/FX). */
export type DeityPoseLayout = {
  idle: ActorPoseBox;
  action: ActorPoseBox;
  sword: ActorPoseBox;
  /** Two attack FX frames (alternate; stock rect3/rect4 intent). */
  fxFrames: [ActorPoseBox, ActorPoseBox];
};

export type ActorPoseBox = {
  left: number;
  top: number;
  right: number;
  bottom: number;
};

export type ActorPoseAtlas = {
  idle: ActorPoseBox;
  /** Idle breath / bob cycle (sorted left→right). Falls back to [idle]. */
  idleFrames: ActorPoseBox[];
  action: ActorPoseBox;
};

export type ComposedFrame = {
  width: number;
  height: number;
  rgba: Uint8ClampedArray;
  /** Draw origin relative to actor feet-ish point (min offset of frame). */
  originX: number;
  originY: number;
  /** Horizontal mirror for map left/right when actor only stores one side. */
  flipX?: boolean;
  /** See LoadedActor.artScale — propagated for sprite scale compensation. */
  artScale?: number;
};

/**
 * Horizontal sprite anchor so battle feet stay pinned at root.
 * Cast/attack strips (鬼道 staff) widen the canvas; center-anchor (0.5) then
 * looks like the whole body steps back/out. Lower-band opaque mass ≈ feet.
 */
export function battleFootAnchorX(frame: ComposedFrame): number {
  const w = frame.width | 0;
  const h = frame.height | 0;
  if (w <= 0 || h <= 0) return 0.5;
  const rgba = frame.rgba;
  const y0 = Math.floor(h * 0.62);
  let sx = 0;
  let n = 0;
  for (let y = y0; y < h; y++) {
    const row = y * w;
    for (let x = 0; x < w; x++) {
      if (rgba[(row + x) * 4 + 3]! < 40) continue;
      sx += x;
      n++;
    }
  }
  if (!n) {
    for (let i = 3; i < rgba.length; i += 4) {
      if (rgba[i]! < 40) continue;
      sx += (i / 4) % w;
      n++;
    }
  }
  if (!n) return 0.5;
  const ax = sx / n / w;
  return Math.min(0.88, Math.max(0.12, ax));
}

/**
 * Parse decoded .actor payload (type-2 LZSS unwrapped).
 * Layout matches `vm_mock_admin_actor_resource_inspect` in web_admin_server.c.
 */
export function parseActorPayload(data: Uint8Array, name = ""): ParsedActor {
  let pos = 0;
  const need = (n: number) => {
    if (pos + n > data.length) throw new Error(`actor truncated @${pos}+${n}`);
  };
  const i32 = () => {
    need(4);
    const v = readI32LE(data, pos);
    pos += 4;
    return v;
  };
  const str = () => {
    need(1);
    const len = data[pos++]!;
    if (len === 0 || len >= 64) throw new Error(`bad actor string len ${len}`);
    need(len);
    const bytes = data.subarray(pos, pos + len);
    pos += len;
    return decodeGbk(bytes).replace(/\0+$/, "");
  };

  const imageCount = i32();
  if (imageCount <= 0 || imageCount > 64) throw new Error(`bad imageCount ${imageCount}`);
  const imageNames: string[] = [];
  for (let i = 0; i < imageCount; i++) {
    const s = str();
    if (!/\.gif$/i.test(s)) throw new Error(`actor image not gif: ${s}`);
    imageNames.push(s);
  }

  const rectCount = i32();
  if (rectCount <= 0 || rectCount > 4096) throw new Error(`bad rectCount ${rectCount}`);
  const rects: ActorRect[] = [];
  for (let i = 0; i < rectCount; i++) {
    rects.push({
      left: i32(),
      top: i32(),
      right: i32(),
      bottom: i32(),
      imageIndex: i32(),
    });
  }

  const animationCount = i32();
  if (animationCount <= 0 || animationCount > 4096) {
    throw new Error(`bad animationCount ${animationCount}`);
  }
  const animations: ActorAnimation[] = [];
  let totalFrames = 0;
  for (let a = 0; a < animationCount; a++) {
    const partCount = i32();
    if (partCount <= 0 || partCount > 4096) throw new Error(`bad partCount ${partCount}`);
    const parts: ActorPart[] = [];
    for (let p = 0; p < partCount; p++) {
      const partId = i32();
      const frameCount = i32();
      if (frameCount < 0 || frameCount > 65535) throw new Error(`bad frameCount ${frameCount}`);
      const frames: ActorFrame[] = [];
      for (let f = 0; f < frameCount; f++) {
        const rectIndex = i32();
        const offsetX = i32();
        const offsetY = i32();
        i32(); // ignored
        i32(); // ignored
        if (rectIndex < 0 || rectIndex >= rectCount) {
          throw new Error(`bad rectIndex ${rectIndex}`);
        }
        frames.push({ rectIndex, offsetX, offsetY });
        totalFrames++;
      }
      parts.push({ partId, frames });
    }
    animations.push({ parts });
  }

  if (pos !== data.length) {
    throw new Error(`actor tail leftover name=${name} pos=${pos} len=${data.length}`);
  }
  if (totalFrames === 0) throw new Error(`actor has no frames: ${name}`);
  return { name, imageNames, rects, animations };
}

type ActorCacheEntry = {
  stamp: string | null;
  promise: Promise<LoadedActor>;
};

const actorCache = new Map<string, ActorCacheEntry>();

/** Stock LKV2 sheet vs hi-res redesign PNG — follows client「特效」pref. */
export function deitySheetVariant(): "png" | "gif" {
  return isPeerCosmeticsVisible() ? "png" : "gif";
}

function isDeityActorName(name: string): boolean {
  return /e_deity/i.test(name);
}

function actorCacheKey(name: string): string {
  const base = name.toLowerCase();
  return isDeityActorName(base) ? `${base}#${deitySheetVariant()}` : base;
}

/**
 * e_deity.actor still lists e_deity.gif; when特效 on, load e_deity.png instead.
 * Falls back to gif if png is missing.
 */
function resolveActorImageName(imageName: string): string {
  if (!/^e_deity\.gif$/i.test(imageName)) return imageName;
  return deitySheetVariant() === "png" ? "e_deity.png" : "e_deity.gif";
}

export function invalidateActorCache(name?: string): void {
  if (!name) {
    actorCache.clear();
    return;
  }
  const lower = name.toLowerCase();
  if (isDeityActorName(lower)) {
    for (const k of [...actorCache.keys()]) {
      if (k === lower || k.startsWith(`${lower}#`)) actorCache.delete(k);
    }
    return;
  }
  actorCache.delete(lower);
}

export async function loadActor(name: string): Promise<LoadedActor> {
  const key = actorCacheKey(name);
  const revalidate = shouldVersionCheckResource(name);
  // Probe+fetch first so stamp is current before deciding cache reuse.
  const payload = await loadDecodedPayload(name, { revalidate });
  const stamp = getCachedResourceStamp(name);
  const prev = actorCache.get(key);
  if (prev && (!revalidate || prev.stamp === stamp)) {
    return prev.promise;
  }
  if (prev && revalidate && prev.stamp !== stamp) {
    console.info(
      `[actor] version-stale ${name} local=${prev.stamp} remote=${stamp}`,
    );
  }
  const promise = (async () => {
    const parsed = parseActorPayload(payload, name);
    const images = await Promise.all(
      parsed.imageNames.map(async (gif) => {
        const resolved = resolveActorImageName(gif);
        try {
          return await loadGif(resolved, {
            revalidate: shouldVersionCheckResource(resolved),
          });
        } catch (e) {
          if (resolved !== gif) {
            console.warn(
              "[actor] deity png failed, falling back to gif",
              name,
              resolved,
              e,
            );
            try {
              return await loadGif(gif, {
                revalidate: shouldVersionCheckResource(gif),
              });
            } catch (e2) {
              console.warn("[actor] gif failed", name, gif, e2);
              return null;
            }
          }
          console.warn("[actor] gif failed", name, gif, e);
          return null;
        }
      }),
    );
    const loaded: LoadedActor = { ...parsed, images };
    finalizeActorSheet(loaded);
    return loaded;
  })();
  actorCache.set(key, { stamp, promise });
  return promise;
}

/**
 * Detect integer hi-res multiplier vs stock actor UV (e_deity = 89×114).
 */
function detectHiResSheetScale(
  img: GifRgba,
  maxR: number,
  maxB: number,
): number {
  const candidates: Array<[number, number]> = [
    [89, 114],
    [maxR, maxB],
  ];
  for (const [nw, nh] of candidates) {
    if (nw <= 0 || nh <= 0) continue;
    const sx = img.width / nw;
    const sy = img.height / nh;
    if (sx < 1.35 || sy < 1.35) continue;
    const s = Math.round((sx + sy) / 2);
    if (s < 2) continue;
    if (Math.abs(sx - s) > s * 0.2 || Math.abs(sy - s) > s * 0.2) continue;
    return s;
  }
  const sx = img.width / maxR;
  const sy = img.height / maxB;
  if (sx >= 1.35 && sy >= 1.35) {
    const s = Math.round((sx + sy) / 2);
    if (s >= 2) return s;
  }
  return 1;
}

/**
 * e_deity sheet: stock 89×114 UV at 10× on a larger canvas (e.g. 1024×1536).
 * Generic detectHiResSheetScale averages width/height ratios and can pick 12×,
 * pushing rect10 past the image edge — sword/FX compose returns null.
 */
function detectDeityHiResScale(img: GifRgba, maxR: number, maxB: number): number {
  const stockScale = 10;
  if (maxR * stockScale <= img.width && maxB * stockScale <= img.height) {
    return stockScale;
  }
  return detectHiResSheetScale(img, maxR, maxB);
}

/**
 * True when sheet is stock-UV packed (rect10 zone holds the glowing sword).
 * False for v34 pose layout (idle|attack top, sword mid-right, FX bottom) —
 * stock UV×10 would crop dress scraps there.
 */
function isDeityStockUvPackedSheet(img: GifRgba): boolean {
  // Exact packed canvas from pack_deity_stock_layout.py
  if (img.width === 890 && img.height === 1140) return true;
  // Stock UV×10 rect10 = (630,460)-(860,710)
  const x0 = 630;
  const y0 = 460;
  const x1 = Math.min(img.width, 860);
  const y1 = Math.min(img.height, 710);
  if (x1 - x0 < 40 || y1 - y0 < 40) return false;
  let blue = 0;
  let opaque = 0;
  for (let y = y0; y < y1; y++) {
    for (let x = x0; x < x1; x++) {
      const i = (y * img.width + x) * 4;
      const a = img.rgba[i + 3]!;
      if (a < 40) continue;
      opaque++;
      const r = img.rgba[i]!;
      const g = img.rgba[i + 1]!;
      const b = img.rgba[i + 2]!;
      if (b > r + 30 && b > g + 15) blue++;
    }
  }
  // Packed sword slot is mostly transparent with cyan blade; dress scrap is white.
  return opaque > 500 && blue > opaque * 0.12;
}

/**
 * Detect v34-style e_deity pose sheet: idle TL, attack TR, sword mid-right, FX bottom.
 * Does not modify the atlas — only records crop boxes.
 */
function detectDeityPoseLayout(img: GifRgba): DeityPoseLayout | null {
  const midX = Math.floor(img.width / 2);
  const midY = Math.floor(img.height * 0.52);
  const idle = largestOpaqueComponent(img, 0, 0, midX, midY);
  const action = largestOpaqueComponent(img, midX, 0, img.width, midY);
  if (!idle || !action) return null;
  trimPoseBox(img, idle);
  trimPoseBox(img, action);

  // Sword sits mid-right below attack torso band.
  const sword = largestOpaqueComponent(
    img,
    Math.floor(img.width * 0.45),
    Math.floor(img.height * 0.42),
    img.width,
    Math.floor(img.height * 0.72),
  );
  // FX band along bottom — split into two frames (stock rect3/rect4).
  const fxBand = largestOpaqueComponent(
    img,
    0,
    Math.floor(img.height * 0.66),
    img.width,
    img.height,
  );
  if (!sword || !fxBand) return null;
  trimPoseBox(img, sword);
  trimPoseBox(img, fxBand);

  const fxMid = Math.floor((fxBand.left + fxBand.right) / 2);
  const fxA =
    largestOpaqueComponent(img, fxBand.left, fxBand.top, fxMid, fxBand.bottom) ??
    ({
      left: fxBand.left,
      top: fxBand.top,
      right: fxMid,
      bottom: fxBand.bottom,
    } as ActorPoseBox);
  const fxB =
    largestOpaqueComponent(img, fxMid, fxBand.top, fxBand.right, fxBand.bottom) ??
    ({
      left: fxMid,
      top: fxBand.top,
      right: fxBand.right,
      bottom: fxBand.bottom,
    } as ActorPoseBox);
  trimPoseBox(img, fxA);
  trimPoseBox(img, fxB);

  const idleH = idle.bottom - idle.top;
  const swordH = sword.bottom - sword.top;
  const fxAH = fxA.bottom - fxA.top;
  const fxBH = fxB.bottom - fxB.top;
  if (idleH < img.height * 0.25) return null;
  if (swordH > idleH * 0.85 || swordH < 40) return null;
  if (fxAH < 40 || fxBH < 40) return null;

  return { idle, action, sword, fxFrames: [fxA, fxB] };
}

/** Scale .actor rects + part offsets to match a hi-res sheet (keeps CBE stack order). */
function scaleActorRectsToHiRes(actor: LoadedActor, scale: number): void {
  if (scale < 2) return;
  for (const r of actor.rects) {
    r.left = Math.round(r.left * scale);
    r.top = Math.round(r.top * scale);
    r.right = Math.round(r.right * scale);
    r.bottom = Math.round(r.bottom * scale);
  }
  for (const anim of actor.animations) {
    for (const part of anim.parts) {
      for (const f of part.frames) {
        f.offsetX = Math.round(f.offsetX * scale);
        f.offsetY = Math.round(f.offsetY * scale);
      }
    }
  }
}

function isDeityActor(actor: LoadedActor): boolean {
  return (
    /e_deity/i.test(actor.name) ||
    actor.imageNames.some((n) => /e_deity\.gif/i.test(n))
  );
}

/**
 * Grow hi-res e_deity body/attack crops to cover feet + sword tip that extend
 * past stock 89×114 UV (AI layout keeps the same packing fractions but paints
 * full limbs past the original clip).
 */
function expandDeityHiResRects(actor: LoadedActor, img: GifRgba): void {
  // Stock: rect2 idle body (0,0)-(41,61), rect0 attack (41,0)-(85,46).
  const body = actor.rects[2];
  const atk = actor.rects[0];
  if (!body || !atk) return;

  // Packed hi-res sheet matches stock rect2/rect0/rect3/rect4 UV — keep scaled
  // .actor rects (growing/shrinking picked up debris or clipped limbs).
  if (atk.left < body.right) atk.left = body.right;
}

/** Tighten composed RGBA to opaque bounds (feet flush to texture bottom). */
function tightenComposedOpaque(frame: ComposedFrame): ComposedFrame {
  const { width: w, height: h, rgba } = frame;
  let minX = w;
  let minY = h;
  let maxX = 0;
  let maxY = 0;
  let found = false;
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      if (rgba[(y * w + x) * 4 + 3]! < 40) continue;
      found = true;
      if (x < minX) minX = x;
      if (y < minY) minY = y;
      if (x + 1 > maxX) maxX = x + 1;
      if (y + 1 > maxY) maxY = y + 1;
    }
  }
  if (!found) return frame;
  const tw = maxX - minX;
  const th = maxY - minY;
  if (tw <= 0 || th <= 0 || (tw === w && th === h)) return frame;
  const out = new Uint8ClampedArray(tw * th * 4);
  for (let y = 0; y < th; y++) {
    for (let x = 0; x < tw; x++) {
      const si = ((minY + y) * w + (minX + x)) * 4;
      const di = (y * tw + x) * 4;
      out[di] = rgba[si]!;
      out[di + 1] = rgba[si + 1]!;
      out[di + 2] = rgba[si + 2]!;
      out[di + 3] = rgba[si + 3]!;
    }
  }
  return {
    width: tw,
    height: th,
    rgba: out,
    originX: frame.originX + minX,
    originY: frame.originY + minY,
    artScale: frame.artScale,
    flipX: frame.flipX,
  };
}

/**
 * Decide sheet compose mode.
 * - Stock LKV2 / native-sized art → `rects` (unchanged .actor crops).
 * - Hi-res raster matching stock UV grid → scale rects, keep `rects` stack.
 * - e_deity hi-res redesign → scaled rects + deityHiRes (body/attack only).
 * - Redesign raster with alien layout → `poses` (whole-body crops).
 */
export function finalizeActorSheet(actor: LoadedActor): void {
  actor.sheetMode = "rects";
  actor.artScale = 1;
  actor.poseAtlas = undefined;
  actor.deityHiRes = false;
  actor.deityPoseLayout = undefined;

  let maxR = 0;
  let maxB = 0;
  for (const r of actor.rects) {
    if (r.imageIndex !== 0) continue;
    maxR = Math.max(maxR, r.right);
    maxB = Math.max(maxB, r.bottom);
  }
  const img = actor.images[0];
  if (!img || maxR <= 0 || maxB <= 0) return;

  const deity = isDeityActor(actor);

  if (img.sourceKind === "raster") {
    // v34 / alien pose layout: never scale stock UV — that crops dress as "sword".
    if (deity && !isDeityStockUvPackedSheet(img)) {
      const layout = detectDeityPoseLayout(img);
      if (layout) {
        actor.deityHiRes = true;
        actor.deityPoseLayout = layout;
        actor.poseAtlas = {
          idle: layout.idle,
          idleFrames: [layout.idle],
          action: layout.action,
        };
        const idleH = layout.idle.bottom - layout.idle.top;
        actor.artScale = Math.max(1, idleH / 60);
        applyDeityDisplayScale(actor);
        console.info(
          `[actor] sheetMode=poseLayout deityHiRes ${actor.name} artScale=${actor.artScale.toFixed(2)} idle=${layout.idle.right - layout.idle.left}x${idleH} sword=${layout.sword.right - layout.sword.left}x${layout.sword.bottom - layout.sword.top}`,
        );
        return;
      }
    }

    const scale = deity
      ? detectDeityHiResScale(img, maxR, maxB)
      : detectHiResSheetScale(img, maxR, maxB);
    if (scale >= 2) {
      scaleActorRectsToHiRes(actor, scale);
      if (deity) {
        expandDeityHiResRects(actor, img);
        actor.deityHiRes = true;
        // 10× sheet: /2 ≈ x10 on-screen.
        actor.artScale = Math.max(1, scale / 2);
        applyDeityDisplayScale(actor);
        console.info(
          `[actor] sheetMode=rects deityHiRes ${actor.name} scale=${scale} artScale=${actor.artScale.toFixed(2)} body=${actor.rects[2]?.right}x${actor.rects[2]?.bottom} atk=${actor.rects[0]?.right}x${actor.rects[0]?.bottom}`,
        );
        return;
      }
      actor.artScale = scale;
      console.info(
        `[actor] sheetMode=rects hi-res ${actor.name} scale=${scale} artScale=${actor.artScale.toFixed(2)}`,
      );
      return;
    }
    if (deity) {
      console.warn(
        "[actor] e_deity hi-res scale detect failed — keeping stock rects",
        img.width,
        img.height,
      );
      return;
    }
  }

  const oversized = img.width > maxR * 1.35 || img.height > maxB * 1.35;
  if (!oversized) return;

  const atlas = detectPoseAtlas(img);
  if (!atlas) {
    console.warn("[actor] pose atlas detect failed", actor.name, img.width, img.height);
    return;
  }
  actor.sheetMode = "poses";
  actor.poseAtlas = atlas;
  const idleH = atlas.idle.bottom - atlas.idle.top;
  const baseArt = Math.max(1, idleH / 60);
  actor.artScale = baseArt;
  console.info(
    `[actor] sheetMode=poses ${actor.name} artScale=${actor.artScale.toFixed(2)} idle=${atlas.idle.right - atlas.idle.left}x${idleH} action=${atlas.action.right - atlas.action.left}x${atlas.action.bottom - atlas.action.top}`,
  );
}

/** Opaque bounding box inside [x0,x1)×[y0,y1). */
function opaqueBounds(
  img: GifRgba,
  x0: number,
  y0: number,
  x1: number,
  y1: number,
  minAlpha = 40,
): ActorPoseBox | null {
  let left = x1;
  let top = y1;
  let right = x0;
  let bottom = y0;
  let found = false;
  for (let y = y0; y < y1; y++) {
    for (let x = x0; x < x1; x++) {
      if (img.rgba[(y * img.width + x) * 4 + 3]! < minAlpha) continue;
      found = true;
      if (x < left) left = x;
      if (y < top) top = y;
      if (x + 1 > right) right = x + 1;
      if (y + 1 > bottom) bottom = y + 1;
    }
  }
  if (!found) return null;
  const w = right - left;
  const h = bottom - top;
  if (w < 8 || h < 16) return null;
  return { left, top, right, bottom };
}

/**
 * Redesign sheets — prefer classic e_deity packed layout (idle TL / attack TR),
 * matching stock .actor UV regions on the 89×114 sheet. Fall back to 3-col idle.
 */
export function detectPoseAtlas(img: GifRgba): ActorPoseAtlas | null {
  const classic = detectClassicDeityAtlas(img);
  if (classic) return classic;

  const midY = Math.floor(img.height * 0.52);
  const idleCols = 3;
  const pad = Math.max(2, Math.floor(img.width * 0.006));

  const bandContent: Array<{ band: number; box: ActorPoseBox }> = [];
  for (let i = 0; i < idleCols; i++) {
    const x0 = Math.floor((img.width * i) / idleCols);
    const x1 = Math.floor((img.width * (i + 1)) / idleCols);
    const box = largestOpaqueComponent(
      img,
      x0 + (i === 0 ? 0 : pad),
      0,
      x1 - (i === idleCols - 1 ? 0 : pad),
      midY,
    );
    if (!box) continue;
    box.left = Math.max(box.left, x0);
    box.right = Math.min(box.right, x1);
    if (box.right - box.left < 16 || box.bottom - box.top < 32) continue;
    trimPoseBox(img, box);
    bandContent.push({ band: i, box });
  }

  if (bandContent.length < 1) {
    const midX = Math.floor(img.width / 2);
    const idle = largestOpaqueComponent(img, 0, 0, midX, midY);
    const action = largestOpaqueComponent(img, midX, 0, img.width, midY);
    if (!idle || !action) return null;
    trimPoseBox(img, idle);
    trimPoseBox(img, action);
    return { idle, idleFrames: [idle], action };
  }

  let top = bandContent[0]!.box.top;
  let bottom = bandContent[0]!.box.bottom;
  for (const { box } of bandContent) {
    top = Math.min(top, box.top);
    bottom = Math.max(bottom, box.bottom);
  }

  // Fixed equal cells — same W/H and same foot line for every breath frame
  const idleFrames: ActorPoseBox[] = bandContent.map(({ band }) => {
    const x0 = Math.floor((img.width * band) / idleCols);
    const x1 = Math.floor((img.width * (band + 1)) / idleCols);
    return { left: x0, top, right: x1, bottom };
  });

  // Bottom: flood from left-half seeds into full bottom so sword can extend right,
  // without swallowing the disconnected spare idle on the right.
  let action = connectedFromSeedBand(
    img,
    0,
    midY,
    Math.floor(img.width * 0.48),
    img.height,
    0,
    midY,
    img.width,
    img.height,
  );
  if (action) {
    // Do not aggressive-trim — sword tip columns are sparse.
  } else {
    action = idleFrames[idleFrames.length - 1]!;
  }

  return { idle: idleFrames[0]!, idleFrames, action };
}

/**
 * Stock e_deity.gif layout (89×114): idle body ≈(0,0)-(41,61), attack ≈(41,0)-(85,46),
 * FX below y≈61. Hi-res redesigns keep the same relative packing.
 */
function detectClassicDeityAtlas(img: GifRgba): ActorPoseAtlas | null {
  // Match original UV fractions on the sheet.
  const midX = Math.floor((img.width * 41) / 89);
  const idleBottom = Math.floor((img.height * 61) / 114);
  const atkBottom = Math.floor((img.height * 50) / 114);

  const idle = largestOpaqueComponent(img, 0, 0, midX, idleBottom);
  const action = largestOpaqueComponent(img, midX, 0, img.width, atkBottom);
  if (!idle || !action) return null;

  const idleH = idle.bottom - idle.top;
  const atkH = action.bottom - action.top;
  // Require both to be substantial full-body-ish crops (not tiny FX).
  if (idleH < img.height * 0.22 || atkH < img.height * 0.18) return null;
  // Heights should be in the same ballpark (original idle taller than attack crop).
  if (atkH > idleH * 1.35 || idleH > atkH * 2.2) return null;

  trimPoseBox(img, idle);
  // Attack may include sword extending right — grow from seed without hard mid clamp.
  let actionFull = connectedFromSeedBand(
    img,
    midX,
    0,
    img.width,
    atkBottom,
    midX,
    0,
    img.width,
    Math.floor(img.height * 0.55),
  );
  if (!actionFull) actionFull = action;
  trimPoseBox(img, actionFull);

  // Single idle frame — playback breath uses applyChestBob (matches stock flap,
  // avoids multi-frame sword pop from inconsistent AI idle copies).
  return { idle, idleFrames: [idle], action: actionFull };
}

/**
 * Grow a connected opaque component starting only from seeds inside the seed
 * window, but allowing expansion into the expand window (e.g. sword past mid).
 */
function connectedFromSeedBand(
  img: GifRgba,
  seedX0: number,
  seedY0: number,
  seedX1: number,
  seedY1: number,
  expX0: number,
  expY0: number,
  expX1: number,
  expY1: number,
  minAlpha = 40,
): ActorPoseBox | null {
  const w = expX1 - expX0;
  const h = expY1 - expY0;
  if (w <= 0 || h <= 0) return null;
  const seen = new Uint8Array(w * h);
  const qx = new Int32Array(w * h);
  const qy = new Int32Array(w * h);
  const idx = (x: number, y: number) => (y - expY0) * w + (x - expX0);
  const opaque = (x: number, y: number) =>
    img.rgba[(y * img.width + x) * 4 + 3]! >= minAlpha;

  let head = 0;
  let tail = 0;
  for (let y = seedY0; y < seedY1; y++) {
    for (let x = seedX0; x < seedX1; x++) {
      if (x < expX0 || y < expY0 || x >= expX1 || y >= expY1) continue;
      if (!opaque(x, y)) continue;
      const si = idx(x, y);
      if (seen[si]) continue;
      seen[si] = 1;
      qx[tail] = x;
      qy[tail] = y;
      tail++;
    }
  }
  if (tail === 0) return null;

  let left = expX1;
  let top = expY1;
  let right = expX0;
  let bottom = expY0;
  let area = 0;
  while (head < tail) {
    const cx = qx[head]!;
    const cy = qy[head]!;
    head++;
    area++;
    if (cx < left) left = cx;
    if (cy < top) top = cy;
    if (cx + 1 > right) right = cx + 1;
    if (cy + 1 > bottom) bottom = cy + 1;
    for (const [nx, ny] of [
      [cx + 1, cy],
      [cx - 1, cy],
      [cx, cy + 1],
      [cx, cy - 1],
    ] as Array<[number, number]>) {
      if (nx < expX0 || ny < expY0 || nx >= expX1 || ny >= expY1) continue;
      const ni = idx(nx, ny);
      if (seen[ni] || !opaque(nx, ny)) continue;
      seen[ni] = 1;
      qx[tail] = nx;
      qy[tail] = ny;
      tail++;
    }
  }
  if (area < 400 || right - left < 16 || bottom - top < 32) return null;
  return { left, top, right, bottom };
}

/** All significant 4-connected opaque blobs in window, largest-first then keep. */
function allOpaqueComponents(
  img: GifRgba,
  x0: number,
  y0: number,
  x1: number,
  y1: number,
  minAlpha = 40,
): ActorPoseBox[] {
  const w = x1 - x0;
  const h = y1 - y0;
  if (w <= 0 || h <= 0) return [];
  const seen = new Uint8Array(w * h);
  const qx = new Int32Array(w * h);
  const qy = new Int32Array(w * h);
  const out: Array<ActorPoseBox & { area: number }> = [];
  const idx = (x: number, y: number) => (y - y0) * w + (x - x0);
  const opaque = (x: number, y: number) =>
    img.rgba[(y * img.width + x) * 4 + 3]! >= minAlpha;

  for (let y = y0; y < y1; y++) {
    for (let x = x0; x < x1; x++) {
      const si = idx(x, y);
      if (seen[si] || !opaque(x, y)) continue;
      let head = 0;
      let tail = 0;
      qx[tail] = x;
      qy[tail] = y;
      tail++;
      seen[si] = 1;
      let left = x;
      let top = y;
      let right = x + 1;
      let bottom = y + 1;
      let area = 0;
      while (head < tail) {
        const cx = qx[head]!;
        const cy = qy[head]!;
        head++;
        area++;
        if (cx < left) left = cx;
        if (cy < top) top = cy;
        if (cx + 1 > right) right = cx + 1;
        if (cy + 1 > bottom) bottom = cy + 1;
        for (const [nx, ny] of [
          [cx + 1, cy],
          [cx - 1, cy],
          [cx, cy + 1],
          [cx, cy - 1],
        ] as Array<[number, number]>) {
          if (nx < x0 || ny < y0 || nx >= x1 || ny >= y1) continue;
          const ni = idx(nx, ny);
          if (seen[ni] || !opaque(nx, ny)) continue;
          seen[ni] = 1;
          qx[tail] = nx;
          qy[tail] = ny;
          tail++;
        }
      }
      if (area >= 400) out.push({ left, top, right, bottom, area });
    }
  }
  out.sort((a, b) => b.area - a.area);
  // Keep top blobs that don't heavily overlap (separate frames)
  const kept: ActorPoseBox[] = [];
  for (const b of out) {
    const overlaps = kept.some((k) => {
      const ix = Math.max(0, Math.min(b.right, k.right) - Math.max(b.left, k.left));
      const iy = Math.max(0, Math.min(b.bottom, k.bottom) - Math.max(b.top, k.top));
      const inter = ix * iy;
      const smaller = Math.min(
        (b.right - b.left) * (b.bottom - b.top),
        (k.right - k.left) * (k.bottom - k.top),
      );
      return inter > smaller * 0.35;
    });
    if (!overlaps) kept.push({ left: b.left, top: b.top, right: b.right, bottom: b.bottom });
    if (kept.length >= 6) break;
  }
  return kept;
}

/** Largest 4-connected opaque blob inside the search window. */
function largestOpaqueComponent(
  img: GifRgba,
  x0: number,
  y0: number,
  x1: number,
  y1: number,
  minAlpha = 40,
): ActorPoseBox | null {
  const w = x1 - x0;
  const h = y1 - y0;
  if (w <= 0 || h <= 0) return null;
  const seen = new Uint8Array(w * h);
  const qx = new Int32Array(w * h);
  const qy = new Int32Array(w * h);

  let best: ActorPoseBox | null = null;
  let bestArea = 0;

  const idx = (x: number, y: number) => (y - y0) * w + (x - x0);
  const opaque = (x: number, y: number) =>
    img.rgba[(y * img.width + x) * 4 + 3]! >= minAlpha;

  for (let y = y0; y < y1; y++) {
    for (let x = x0; x < x1; x++) {
      const si = idx(x, y);
      if (seen[si] || !opaque(x, y)) continue;
      let head = 0;
      let tail = 0;
      qx[tail] = x;
      qy[tail] = y;
      tail++;
      seen[si] = 1;
      let left = x;
      let top = y;
      let right = x + 1;
      let bottom = y + 1;
      let area = 0;
      while (head < tail) {
        const cx = qx[head]!;
        const cy = qy[head]!;
        head++;
        area++;
        if (cx < left) left = cx;
        if (cy < top) top = cy;
        if (cx + 1 > right) right = cx + 1;
        if (cy + 1 > bottom) bottom = cy + 1;
        const nbs: Array<[number, number]> = [
          [cx + 1, cy],
          [cx - 1, cy],
          [cx, cy + 1],
          [cx, cy - 1],
        ];
        for (const [nx, ny] of nbs) {
          if (nx < x0 || ny < y0 || nx >= x1 || ny >= y1) continue;
          const ni = idx(nx, ny);
          if (seen[ni] || !opaque(nx, ny)) continue;
          seen[ni] = 1;
          qx[tail] = nx;
          qy[tail] = ny;
          tail++;
        }
      }
      if (area > bestArea && right - left >= 8 && bottom - top >= 16) {
        bestArea = area;
        best = { left, top, right, bottom };
      }
    }
  }
  return best;
}

/** Shrink box while outer columns/rows are nearly empty (kills seam slivers). */
function trimPoseBox(img: GifRgba, box: ActorPoseBox, minAlpha = 40): void {
  const colHits = (x: number) => {
    let n = 0;
    for (let y = box.top; y < box.bottom; y++) {
      if (img.rgba[(y * img.width + x) * 4 + 3]! >= minAlpha) n++;
    }
    return n;
  };
  const rowHits = (y: number) => {
    let n = 0;
    for (let x = box.left; x < box.right; x++) {
      if (img.rgba[(y * img.width + x) * 4 + 3]! >= minAlpha) n++;
    }
    return n;
  };
  const minCol = Math.max(2, Math.floor((box.bottom - box.top) * 0.02));
  const minRow = Math.max(2, Math.floor((box.right - box.left) * 0.02));
  while (box.right - box.left > 16 && colHits(box.right - 1) <= minCol) box.right--;
  while (box.right - box.left > 16 && colHits(box.left) <= minCol) box.left++;
  while (box.bottom - box.top > 16 && rowHits(box.bottom - 1) <= minRow) box.bottom--;
  while (box.bottom - box.top > 16 && rowHits(box.top) <= minRow) box.top++;
}

/**
 * Like opaqueBounds, but after finding dense body rows, stop when content
 * thins out (ignore sparse lower FX / spare sword pixels).
 */
function opaqueBoundsGapAware(
  img: GifRgba,
  x0: number,
  y0: number,
  x1: number,
  y1: number,
  minAlpha = 40,
  /** Rows with fewer opaque pixels than this are "thin" (FX fringe). */
  thinHits = 70,
  gapRows = 10,
): ActorPoseBox | null {
  let left = x1;
  let top = y1;
  let right = x0;
  let bottom = y0;
  let seenDense = false;
  let thinRun = 0;
  for (let y = y0; y < y1; y++) {
    let rowHits = 0;
    let rowLeft = x1;
    let rowRight = x0;
    for (let x = x0; x < x1; x++) {
      if (img.rgba[(y * img.width + x) * 4 + 3]! < minAlpha) continue;
      rowHits++;
      if (x < rowLeft) rowLeft = x;
      if (x + 1 > rowRight) rowRight = x + 1;
    }
    if (rowHits === 0) {
      if (seenDense) {
        thinRun++;
        if (thinRun >= gapRows) break;
      }
      continue;
    }
    const dense = rowHits >= thinHits;
    if (!dense) {
      if (seenDense) {
        thinRun++;
        if (thinRun >= gapRows) break;
        // still include a little fringe while counting thin run
        bottom = y + 1;
        if (rowLeft < left) left = rowLeft;
        if (rowRight > right) right = rowRight;
      }
      continue;
    }
    thinRun = 0;
    seenDense = true;
    if (y < top) top = y;
    bottom = y + 1;
    if (rowLeft < left) left = rowLeft;
    if (rowRight > right) right = rowRight;
  }
  if (!seenDense) return null;
  const w = right - left;
  const h = bottom - top;
  if (w < 8 || h < 16) return null;
  return { left, top, right, bottom };
}

function cropPoseBox(
  actor: LoadedActor,
  box: ActorPoseBox,
  flipX = false,
  /** Align content into fixed cell — idle uses torso-X to avoid sword sway. */
  stabilize = false,
): ComposedFrame | null {
  const img = actor.images[0];
  if (!img) return null;
  const w = box.right - box.left;
  const h = box.bottom - box.top;
  if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return null;

  let srcLeft = box.left;
  let srcTop = box.top;
  let srcRight = box.right;
  let srcBottom = box.bottom;
  // Horizontal anchor: prefer torso mass center (not feet/sword tip).
  let anchorX = (box.left + box.right) / 2;

  if (stabilize) {
    let cL = srcRight;
    let cT = srcBottom;
    let cR = srcLeft;
    let cB = srcTop;
    let found = false;
    for (let y = box.top; y < box.bottom; y++) {
      for (let x = box.left; x < box.right; x++) {
        if (img.rgba[(y * img.width + x) * 4 + 3]! < 40) continue;
        found = true;
        if (x < cL) cL = x;
        if (y < cT) cT = y;
        if (x + 1 > cR) cR = x + 1;
        if (y + 1 > cB) cB = y + 1;
      }
    }
    if (found) {
      srcLeft = cL;
      srcTop = cT;
      srcRight = cR;
      srcBottom = cB;
      const ch0 = cB - cT;
      const cw0 = cR - cL;
      // Mid-torso band: densest horizontal mass = body, not sword/ribbons.
      const y0 = cT + Math.floor(ch0 * 0.28);
      const y1 = cT + Math.floor(ch0 * 0.62);
      const colHits = new Int32Array(cw0);
      for (let y = y0; y < y1; y++) {
        for (let x = cL; x < cR; x++) {
          if (img.rgba[(y * img.width + x) * 4 + 3]! < 40) continue;
          colHits[x - cL]!++;
        }
      }
      let peak = 0;
      for (let i = 0; i < cw0; i++) {
        if (colHits[i]! > peak) peak = colHits[i]!;
      }
      const minHits = Math.max(2, Math.floor(peak * 0.35));
      let sumX = 0;
      let n = 0;
      for (let i = 0; i < cw0; i++) {
        if (colHits[i]! < minHits) continue;
        sumX += (cL + i + 0.5) * colHits[i]!;
        n += colHits[i]!;
      }
      if (n > 0) {
        anchorX = sumX / n;
      } else {
        anchorX = (cL + cR) / 2;
      }
    }
  }

  const cw = srcRight - srcLeft;
  const ch = srcBottom - srcTop;
  let destX = stabilize ? Math.round(w / 2 - (anchorX - srcLeft)) : 0;
  let destY = stabilize ? h - ch : 0;
  // Stabilize can push limbs/sword past box edges — grow canvas instead of clip.
  let outW = w;
  let outH = h;
  if (stabilize) {
    const padL = Math.max(0, -destX);
    const padR = Math.max(0, destX + cw - w) + 2;
    const padT = Math.max(0, -destY);
    const padB = Math.max(0, destY + ch - h);
    if (padL || padR || padT || padB) {
      destX += padL;
      destY += padT;
      outW = w + padL + padR;
      outH = h + padT + padB;
    }
  }

  const rgba = new Uint8ClampedArray(outW * outH * 4);
  // Place torso center on canvas center-X; feet on canvas bottom

  for (let y = 0; y < ch; y++) {
    for (let x = 0; x < cw; x++) {
      const sx = srcLeft + x;
      const si = ((srcTop + y) * img.width + sx) * 4;
      const dx = destX + x;
      const dy = destY + y;
      if (dx < 0 || dy < 0 || dx >= outW || dy >= outH) continue;
      const di = (dy * outW + dx) * 4;
      rgba[di] = img.rgba[si]!;
      rgba[di + 1] = img.rgba[si + 1]!;
      rgba[di + 2] = img.rgba[si + 2]!;
      rgba[di + 3] = img.rgba[si + 3]!;
    }
  }

  if (flipX) {
    for (let y = 0; y < outH; y++) {
      let lo = y * outW;
      let hi = lo + outW - 1;
      while (lo < hi) {
        const i0 = lo * 4;
        const i1 = hi * 4;
        for (let k = 0; k < 4; k++) {
          const t = rgba[i0 + k]!;
          rgba[i0 + k] = rgba[i1 + k]!;
          rgba[i1 + k] = t;
        }
        lo++;
        hi--;
      }
    }
  }

  return {
    width: outW,
    height: outH,
    rgba,
    originX: -Math.floor(outW / 2),
    originY: -outH,
    artScale: actor.artScale ?? 1,
  };
}

/** Map/battle poses for redesign sheets that do not share .actor UV layout. */
export function composePoseSheetFrame(
  actor: LoadedActor,
  kind: "idle" | "action",
  flipX = false,
  phase = 0,
): ComposedFrame | null {
  if (kind === "action") {
    const box = actor.poseAtlas?.action;
    if (!box) return null;
    const frame = cropPoseBox(actor, box, flipX, true);
    if (!frame) return null;
    // Match on-screen body size to idle (attack art is often a smaller crop).
    const idleBox = actor.poseAtlas?.idle;
    if (idleBox) {
      const idleH = idleBox.bottom - idleBox.top;
      if (idleH > 0 && frame.height > 0 && Math.abs(frame.height - idleH) > 2) {
        return scaleComposedNearest(frame, idleH / frame.height);
      }
    }
    return frame;
  }

  // Idle: cycle pre-drawn breath frames when present; else single body + chest bob
  // (classic e_deity layout — matches stock .actor flap without multi-frame sword pop).
  const frames =
    actor.poseAtlas?.idleFrames?.length
      ? actor.poseAtlas.idleFrames
      : actor.poseAtlas?.idle
        ? [actor.poseAtlas.idle]
        : [];
  if (!frames.length) return null;
  const n = frames.length;
  let fi: number;
  if (n >= 3) {
    const seq = [0, 1, 2, 1];
    fi = seq[((phase % seq.length) + seq.length) % seq.length]!;
    if (fi >= n) fi = n - 1;
  } else {
    fi = ((phase % n) + n) % n;
  }
  const base = cropPoseBox(actor, frames[fi]!, false, true);
  if (!base) return null;
  const frame = n < 3 ? applyChestBob(base, phase) : base;
  if (flipX) mirrorComposedX(frame);
  return frame;
}

/**
 * Hi-res e_deity redesign: stock .actor stacks LKV2 sword/FX fragments that do
 * not align with the baked full-body idle (rect2) / attack (rect0) art.
 * Idle = rect2 body + stock sword rects + chest bob; attack = rect0 + FX 3/4.
 */
/** Hand sword layer only — rect6/7 are stock foot/sleeve scraps, not sword on baked body. */
const DEITY_IDLE_SWORD_RECTS = new Set([10]);

/** Hand anchor on baked rect2 body (pixels, body-local). */
const DEITY_IDLE_SWORD_HAND_X = 100;
const DEITY_IDLE_SWORD_HAND_Y = 300;
/** Hilt pivot inside rect10 crop (pixels, layer-local). */
const DEITY_IDLE_SWORD_HILT_X = 185;
const DEITY_IDLE_SWORD_HILT_Y = 120;
/** Fine-tune sword vs hand after anchor (does not move body). */
const DEITY_IDLE_SWORD_NUDGE_X = -98;
const DEITY_IDLE_SWORD_NUDGE_Y = 10;
/** Shift body + sword right together (less left overflow past map edge). */
const DEITY_IDLE_SHIFT_X = 120;
/** Base side pad; left grows / right shrinks by SHIFT_X. */
const DEITY_IDLE_PAD_BASE = 240;
/** Idle sword scale (body unchanged). */
const DEITY_SWORD_SCALE = 0.46;
/** Attack FX scale (rect3/rect4 only; body rect0 unchanged). */
const DEITY_FX_SCALE = 0.46;
/** Attack FX horizontal nudge (pose-layout compose; + = right). */
const DEITY_FX_NUDGE_X = 48;
/** On-screen size multiplier for e_deity (sprite scale ∝ 1/artScale). */
const DEITY_DISPLAY_SCALE: number = 1.8;
/** Extra right margin on attack — lunge + sword tip after torso stabilize. */
const DEITY_ATTACK_PAD_RIGHT = 0;
/** Attack body scale (pose-layout compose only; 1 = native crop size). */
const DEITY_ATTACK_SCALE: number = 0.8;
/** Attack crop box left offset (+ = right). */
const DEITY_ATTACK_CROP_X: number = -60;
/** Attack crop box width delta (+ = wider, − = narrower). */
const DEITY_ATTACK_CROP_W: number = 60;

function adjustDeityAttackCropBox(
  box: ActorPoseBox,
  imgWidth: number,
): ActorPoseBox {
  const baseW = box.right - box.left;
  const left = Math.max(0, box.left + DEITY_ATTACK_CROP_X);
  const width = Math.max(16, baseW + DEITY_ATTACK_CROP_W);
  const right = Math.min(imgWidth, left + width);
  return {
    left,
    top: box.top,
    right: Math.max(left + 16, right),
    bottom: box.bottom,
  };
}

function applyDeityDisplayScale(actor: LoadedActor): void {
  if (DEITY_DISPLAY_SCALE === 1) return;
  actor.artScale = Math.max(1, (actor.artScale ?? 1) / DEITY_DISPLAY_SCALE);
}

/** Add transparent margin; keeps feet-bottom + horizontal-center anchor. */
function padComposedFrame(
  frame: ComposedFrame,
  padLeft: number,
  padRight: number,
  padTop: number,
  padBottom: number,
): ComposedFrame {
  if (!padLeft && !padRight && !padTop && !padBottom) return frame;
  const width = frame.width + padLeft + padRight;
  const height = frame.height + padTop + padBottom;
  const rgba = new Uint8ClampedArray(width * height * 4);
  blitLayerOnto(rgba, width, height, frame, padLeft, padTop);
  return {
    width,
    height,
    rgba,
    originX: -Math.floor(width / 2),
    originY: -height,
    artScale: frame.artScale,
    flipX: frame.flipX,
  };
}

function blitLayerOnto(
  rgba: Uint8ClampedArray,
  width: number,
  height: number,
  layer: ComposedFrame,
  destX: number,
  destY: number,
): void {
  const baseX = Math.round(destX);
  const baseY = Math.round(destY);
  for (let y = 0; y < layer.height; y++) {
    for (let x = 0; x < layer.width; x++) {
      const si = (y * layer.width + x) * 4;
      const a = layer.rgba[si + 3]!;
      if (a === 0) continue;
      const sx = baseX + x;
      const sy = baseY + y;
      if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
      const di = (sy * width + sx) * 4;
      rgba[di] = layer.rgba[si]!;
      rgba[di + 1] = layer.rgba[si + 1]!;
      rgba[di + 2] = layer.rgba[si + 2]!;
      rgba[di + 3] = a;
    }
  }
}

/** Shrink/grow layer RGBA (nearest, top-left anchor). */
function scaleLayerNearest(layer: ComposedFrame, scale: number): ComposedFrame {
  if (scale >= 0.999) return layer;
  const sw = layer.width;
  const sh = layer.height;
  const dw = Math.max(1, Math.round(sw * scale));
  const dh = Math.max(1, Math.round(sh * scale));
  const rgba = new Uint8ClampedArray(dw * dh * 4);
  for (let y = 0; y < dh; y++) {
    const sy = Math.min(sh - 1, Math.floor((y + 0.5) / scale));
    for (let x = 0; x < dw; x++) {
      const sx = Math.min(sw - 1, Math.floor((x + 0.5) / scale));
      const si = (sy * sw + sx) * 4;
      const di = (y * dw + x) * 4;
      rgba[di] = layer.rgba[si]!;
      rgba[di + 1] = layer.rgba[si + 1]!;
      rgba[di + 2] = layer.rgba[si + 2]!;
      rgba[di + 3] = layer.rgba[si + 3]!;
    }
  }
  return {
    width: dw,
    height: dh,
    rgba,
    originX: layer.originX,
    originY: layer.originY,
    artScale: layer.artScale,
    flipX: layer.flipX,
  };
}

/** Blit layer scaled around a pivot; pivot stays at (destX+pivotX, destY+pivotY). */
function blitLayerScaledOnto(
  rgba: Uint8ClampedArray,
  width: number,
  height: number,
  layer: ComposedFrame,
  destX: number,
  destY: number,
  scale: number,
  pivotX: number,
  pivotY: number,
): void {
  if (scale >= 0.999) {
    blitLayerOnto(rgba, width, height, layer, destX, destY);
    return;
  }
  const scaled = scaleLayerNearest(layer, scale);
  blitLayerOnto(
    rgba,
    width,
    height,
    scaled,
    Math.round(destX + pivotX * (1 - scale)),
    Math.round(destY + pivotY * (1 - scale)),
  );
}

/**
 * Idle body (rect2) + sword on a fixed canvas. SHIFT_X moves body and sword
 * together; sprite anchor stays 0.5,1 so the actor draws slightly right on map.
 */
function composeDeityIdleStacked(
  actor: LoadedActor,
  frames: ActorFrame[],
  phase = 0,
): ComposedFrame | null {
  const bodyFrame = frames.find((f) => f.rectIndex === 2);
  if (!bodyFrame) return null;
  const body = composeActorFrame(actor, bodyFrame);
  if (!body) return null;

  const originX = bodyFrame.offsetX;
  const originY = bodyFrame.offsetY;
  const padLeft = DEITY_IDLE_PAD_BASE + DEITY_IDLE_SHIFT_X;
  const padRight = DEITY_IDLE_PAD_BASE - DEITY_IDLE_SHIFT_X;
  const width = body.width + padLeft + padRight;
  const height = body.height;
  const bodyX = padLeft;
  const bodyY = 0;

  const rgba = new Uint8ClampedArray(width * height * 4);
  // Chest bob on body only — doing it after sword stacks would erase the sword.
  const bodyBob = applyChestBob(
    {
      width: body.width,
      height: body.height,
      rgba: new Uint8ClampedArray(body.rgba),
      originX: body.originX,
      originY: body.originY,
      artScale: body.artScale,
    },
    phase,
  );
  blitLayerOnto(rgba, width, height, bodyBob, bodyX, bodyY);

  for (const frame of frames) {
    if (frame.rectIndex === 2 || !DEITY_IDLE_SWORD_RECTS.has(frame.rectIndex)) {
      continue;
    }
    const layer = composeActorFrame(actor, frame);
    if (!layer) continue;
    const dx =
      bodyX +
      DEITY_IDLE_SWORD_HAND_X -
      DEITY_IDLE_SWORD_HILT_X +
      DEITY_IDLE_SWORD_NUDGE_X;
    const dy =
      bodyY +
      DEITY_IDLE_SWORD_HAND_Y -
      DEITY_IDLE_SWORD_HILT_Y +
      DEITY_IDLE_SWORD_NUDGE_Y;
    blitLayerScaledOnto(
      rgba,
      width,
      height,
      layer,
      dx,
      dy,
      DEITY_SWORD_SCALE,
      DEITY_IDLE_SWORD_HILT_X,
      DEITY_IDLE_SWORD_HILT_Y,
    );
  }

  return {
    width,
    height,
    rgba,
    originX,
    originY,
    artScale: actor.artScale ?? 1,
  };
}

/** Attack body (rect0) + scaled FX (rect3/rect4). Canvas bbox uses full stock layout. */
function composeDeityAttackStacked(
  actor: LoadedActor,
  frames: ActorFrame[],
): ComposedFrame | null {
  const fxRects = new Set([3, 4]);
  type Layer = { frame: ActorFrame; layer: ComposedFrame; rect: ActorRect };
  const layers: Layer[] = [];
  for (const frame of frames) {
    const rect = actor.rects[frame.rectIndex];
    if (!rect) continue;
    const layer = composeActorFrame(actor, frame);
    if (!layer) continue;
    const w = rect.right - rect.left;
    const h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) continue;
    layers.push({ frame, layer, rect });
  }
  if (!layers.length) return null;

  // Full unscaled bounds — shrinking FX must not shrink the output texture.
  let minX = Infinity;
  let minY = Infinity;
  let maxX = -Infinity;
  let maxY = -Infinity;
  for (const { frame, rect } of layers) {
    const w = rect.right - rect.left;
    const h = rect.bottom - rect.top;
    minX = Math.min(minX, frame.offsetX);
    minY = Math.min(minY, frame.offsetY);
    maxX = Math.max(maxX, frame.offsetX + w);
    maxY = Math.max(maxY, frame.offsetY + h);
  }
  if (!(maxX > minX && maxY > minY)) return null;
  const width = maxX - minX;
  const height = maxY - minY;
  if (width > 2048 || height > 2048) return null;

  const rgba = new Uint8ClampedArray(width * height * 4);
  // Body first, FX on top (stock part order).
  const sorted = [...layers].sort((a, b) => {
    const ao = fxRects.has(a.frame.rectIndex) ? 1 : 0;
    const bo = fxRects.has(b.frame.rectIndex) ? 1 : 0;
    return ao - bo;
  });
  for (const { frame, layer } of sorted) {
    const scale = fxRects.has(frame.rectIndex) ? DEITY_FX_SCALE : 1;
    const destX = frame.offsetX - minX;
    const destY = frame.offsetY - minY;
    // Center pivot — bottom pivot pulled scaled glow off visible area.
    const pivotX = layer.width / 2;
    const pivotY = layer.height / 2;
    blitLayerScaledOnto(
      rgba,
      width,
      height,
      layer,
      destX,
      destY,
      scale,
      pivotX,
      pivotY,
    );
  }
  return {
    width,
    height,
    rgba,
    originX: minX,
    originY: minY,
    artScale: actor.artScale ?? 1,
  };
}

function composeDeityHiResFrame(
  actor: LoadedActor,
  kind: "idle" | "action",
  flipX = false,
  phase = 0,
): ComposedFrame | null {
  if (actor.deityPoseLayout) {
    const frame = composeDeityPoseLayoutFrame(actor, kind, phase);
    if (!frame) return null;
    if (flipX) {
      mirrorComposedX(frame);
      frame.flipX = true;
    }
    return frame;
  }

  if (kind === "idle") {
    const standAnim = actor.animations[0];
    const part = standAnim?.parts[0];
    // Body + hand sword only — skip head/dot/foot/sleeve scraps (rect1/6/7/8/9/11/12).
    const keep = new Set([2, 10]);
    if (part?.frames.length) {
      const filtered = part.frames.filter((f) => keep.has(f.rectIndex));
      const stacked = composeDeityIdleStacked(actor, filtered, phase);
      if (stacked) {
        if (flipX) {
          mirrorComposedX(stacked);
          stacked.flipX = true;
        }
        return stacked;
      }
    }
    const bodyFrame =
      standAnim?.parts[0]?.frames.find((f) => f.rectIndex === 2) ??
      ({
        rectIndex: 2,
        offsetX: 0,
        offsetY: -(actor.rects[2]?.bottom ?? 60),
      } as ActorFrame);
    const base = composeActorFrame(actor, bodyFrame);
    if (!base) return null;
    const frame = tightenComposedOpaque(applyChestBob(base, phase));
    if (flipX) {
      mirrorComposedX(frame);
      frame.flipX = true;
    }
    return frame;
  }

  // Attack body only — no FX overlay (rect3/rect4 skipped).
  const atkAnim = actor.animations[1];
  const atkFrame =
    atkAnim?.parts[2]?.frames.find((f) => f.rectIndex === 0) ??
    atkAnim?.parts[0]?.frames.find((f) => f.rectIndex === 0) ??
    ({
      rectIndex: 0,
      offsetX: 0,
      offsetY: -(actor.rects[0]?.bottom ?? 46),
    } as ActorFrame);
  const base = composeActorFrame(actor, atkFrame);
  if (!base) return null;
  const frame = tightenComposedOpaque(base);
  if (flipX) {
    mirrorComposedX(frame);
    frame.flipX = true;
  }
  return frame;
}

/**
 * Compose from v34 pose-layout boxes (idle/attack/sword/FX). Atlas untouched.
 */
function composeDeityPoseLayoutFrame(
  actor: LoadedActor,
  kind: "idle" | "action",
  phase = 0,
): ComposedFrame | null {
  const layout = actor.deityPoseLayout;
  if (!layout) return null;

  if (kind === "action") {
    // Attack body only — no FX overlay.
    const img = actor.images[0];
    if (!img) return null;
    const attackBox = adjustDeityAttackCropBox(layout.action, img.width);
    let frame = cropPoseBox(actor, attackBox, false, true);
    if (!frame) return null;
    if (Math.abs(DEITY_ATTACK_SCALE - 1) > 0.001) {
      frame = scaleComposedNearest(frame, DEITY_ATTACK_SCALE);
    }
    return padComposedFrame(frame, 0, DEITY_ATTACK_PAD_RIGHT, 0, 0);
  }

  const body = cropPoseBox(actor, layout.idle, false, true);
  if (!body) return null;
  const bodyBob = applyChestBob(
    {
      width: body.width,
      height: body.height,
      rgba: new Uint8ClampedArray(body.rgba),
      originX: body.originX,
      originY: body.originY,
      artScale: body.artScale,
    },
    phase,
  );
  const sword = cropPoseBox(actor, layout.sword, false, false);
  if (!sword) {
    return {
      ...bodyBob,
      originX: -Math.floor(bodyBob.width / 2),
      originY: -bodyBob.height,
      artScale: actor.artScale ?? 1,
    };
  }

  const padLeft = DEITY_IDLE_PAD_BASE + DEITY_IDLE_SHIFT_X;
  const padRight = Math.max(40, DEITY_IDLE_PAD_BASE - DEITY_IDLE_SHIFT_X);
  const width = bodyBob.width + padLeft + padRight;
  const height = bodyBob.height;
  const rgba = new Uint8ClampedArray(width * height * 4);
  blitLayerOnto(rgba, width, height, bodyBob, padLeft, 0);

  // Hand on idle body (fractions of stabilized crop); hilt near sword pommel.
  const handX = bodyBob.width * 0.28;
  const handY = bodyBob.height * 0.48;
  const hiltX = sword.width * 0.72;
  const hiltY = sword.height * 0.28;
  const dx = padLeft + handX - hiltX + DEITY_IDLE_SWORD_NUDGE_X;
  const dy = handY - hiltY + DEITY_IDLE_SWORD_NUDGE_Y;
  blitLayerScaledOnto(
    rgba,
    width,
    height,
    sword,
    dx,
    dy,
    DEITY_SWORD_SCALE,
    hiltX,
    hiltY,
  );

  return {
    width,
    height,
    rgba,
    originX: -Math.floor(width / 2),
    originY: -height,
    artScale: actor.artScale ?? 1,
  };
}

/** Larger chest bob on a single idle body — phase ping-pong. */
function applyChestBob(frame: ComposedFrame, phase: number): ComposedFrame {
  const w = frame.width;
  const h = frame.height;
  const src = frame.rgba;
  const out = new Uint8ClampedArray(src);

  // Locate bust from opaque pixels in upper-torso band (not fixed 30–70% —
  // off-center art previously only moved one breast).
  const y0 = Math.floor(h * 0.24);
  const y1 = Math.floor(h * 0.52);
  if (y1 - y0 < 8) return frame;

  let sumX = 0;
  let n = 0;
  let minX = w;
  let maxX = 0;
  for (let y = y0; y < y1; y++) {
    for (let x = 0; x < w; x++) {
      if (src[(y * w + x) * 4 + 3]! < 40) continue;
      sumX += x;
      n++;
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
    }
  }
  if (n < 20) return frame;

  const cx = sumX / n;
  // Cover full bust: prefer detected torso span, clamped away from sleeve tips.
  const span = Math.max(maxX - minX + 1, Math.floor(w * 0.35));
  let x0 = Math.floor(cx - span * 0.42);
  let x1 = Math.ceil(cx + span * 0.42);
  x0 = Math.max(0, Math.min(x0, w - 8));
  x1 = Math.max(x0 + 8, Math.min(w, x1));

  const amp = Math.max(1, Math.round(h * 0.0035));
  const offsets = [-amp, 0, amp, 0];
  const shift = offsets[((phase % offsets.length) + offsets.length) % offsets.length]!;
  if (shift === 0) return { ...frame, rgba: out };

  // Soft horizontal falloff at edges so sleeves don't shear.
  const edge = Math.max(3, Math.floor((x1 - x0) * 0.12));
  for (let y = y0; y < y1; y++) {
    const sy = Math.min(y1 - 1, Math.max(y0, y - shift));
    for (let x = x0; x < x1; x++) {
      const dl = x - x0;
      const dr = x1 - 1 - x;
      const soft = Math.min(dl, dr, edge);
      // Fully remap interior; near edges blend with original.
      const si = (sy * w + x) * 4;
      const oi = (y * w + x) * 4;
      const di = oi;
      if (soft >= edge) {
        out[di] = src[si]!;
        out[di + 1] = src[si + 1]!;
        out[di + 2] = src[si + 2]!;
        out[di + 3] = src[si + 3]!;
      } else {
        const t = soft / edge;
        out[di] = Math.round(src[oi]! * (1 - t) + src[si]! * t);
        out[di + 1] = Math.round(src[oi + 1]! * (1 - t) + src[si + 1]! * t);
        out[di + 2] = Math.round(src[oi + 2]! * (1 - t) + src[si + 2]! * t);
        out[di + 3] = Math.round(src[oi + 3]! * (1 - t) + src[si + 3]! * t);
      }
    }
  }
  return { ...frame, rgba: out };
}

function mirrorComposedX(frame: ComposedFrame): void {
  const { width: w, height: h, rgba } = frame;
  for (let y = 0; y < h; y++) {
    let lo = y * w;
    let hi = lo + w - 1;
    while (lo < hi) {
      const i0 = lo * 4;
      const i1 = hi * 4;
      for (let k = 0; k < 4; k++) {
        const t = rgba[i0 + k]!;
        rgba[i0 + k] = rgba[i1 + k]!;
        rgba[i1 + k] = t;
      }
      lo++;
      hi--;
    }
  }
}

function scaleComposedNearest(frame: ComposedFrame, scale: number): ComposedFrame {
  const sw = frame.width;
  const sh = frame.height;
  const dw = Math.max(1, Math.round(sw * scale));
  const dh = Math.max(1, Math.round(sh * scale));
  if (dw > 2048 || dh > 2048) return frame;
  const rgba = new Uint8ClampedArray(dw * dh * 4);
  for (let y = 0; y < dh; y++) {
    const sy = Math.min(sh - 1, Math.floor((y + 0.5) / scale));
    for (let x = 0; x < dw; x++) {
      const sx = Math.min(sw - 1, Math.floor((x + 0.5) / scale));
      const si = (sy * sw + sx) * 4;
      const di = (y * dw + x) * 4;
      rgba[di] = frame.rgba[si]!;
      rgba[di + 1] = frame.rgba[si + 1]!;
      rgba[di + 2] = frame.rgba[si + 2]!;
      rgba[di + 3] = frame.rgba[si + 3]!;
    }
  }
  return {
    width: dw,
    height: dh,
    rgba,
    originX: -Math.floor(dw / 2),
    originY: -dh,
    artScale: frame.artScale,
    flipX: frame.flipX,
  };
}

/**
 * @deprecated Alias — rect scaling lives in finalizeActorSheet.
 */
export function applyActorArtScale(actor: LoadedActor): void {
  finalizeActorSheet(actor);
}

/** First animation with any frames. */
export function pickStandAnimation(actor: ParsedActor): ActorAnimation | null {
  for (const anim of actor.animations) {
    if (anim.parts.some((p) => p.frames.length > 0)) return anim;
  }
  return null;
}

/** Largest valid (w×h>0) frame rect area inside a part — skips zero-size rows. */
export function pickBestFrame(actor: ParsedActor, part: ActorPart): ActorFrame | null {
  let best: ActorFrame | null = null;
  let bestArea = -1;
  for (const f of part.frames) {
    const r = actor.rects[f.rectIndex];
    if (!r) continue;
    const w = r.right - r.left;
    const h = r.bottom - r.top;
    if (w <= 0 || h <= 0) continue;
    const area = w * h;
    if (area > bestArea) {
      bestArea = area;
      best = f;
    }
  }
  return best;
}

/** @deprecated prefer pickStandAnimation — kept for callers that want a single part. */
export function pickStandPart(actor: ParsedActor): ActorPart | null {
  const anim = pickStandAnimation(actor);
  if (!anim) return null;
  // Prefer the part whose best valid frame is largest (skip zero-size / tiny clips).
  // ui_hw_war.actor frame0 is a zero-height sentinel; face is a later frame.
  let best: ActorPart | null = null;
  let bestArea = -1;
  for (const part of anim.parts) {
    if (!part.frames.length) continue;
    const f = pickBestFrame(actor, part);
    if (!f) continue;
    const r = actor.rects[f.rectIndex]!;
    const area = Math.max(0, r.right - r.left) * Math.max(0, r.bottom - r.top);
    if (area > bestArea) {
      bestArea = area;
      best = part;
    }
  }
  return best;
}

/** Compose one animation frame (single part) to RGBA. */
export function composeActorFrame(
  actor: LoadedActor,
  frame: ActorFrame,
): ComposedFrame | null {
  const rect = actor.rects[frame.rectIndex];
  if (!rect) return null;
  const image = actor.images[rect.imageIndex];
  if (!image) return null;
  const w = rect.right - rect.left;
  const h = rect.bottom - rect.top;
  if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return null;
  if (rect.left < 0 || rect.top < 0 || rect.right > image.width || rect.bottom > image.height) {
    return null;
  }

  const rgba = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const si = ((rect.top + y) * image.width + (rect.left + x)) * 4;
      const di = (y * w + x) * 4;
      rgba[di] = image.rgba[si]!;
      rgba[di + 1] = image.rgba[si + 1]!;
      rgba[di + 2] = image.rgba[si + 2]!;
      rgba[di + 3] = image.rgba[si + 3]!;
    }
  }
  return {
    width: w,
    height: h,
    rgba,
    originX: frame.offsetX,
    originY: frame.offsetY,
    artScale: actor.artScale ?? 1,
  };
}

/**
 * Compose all parts of an animation at `frameIndex` onto one canvas
 * (matches how CBE stacks part layers; dedupes identical rect+offset).
 */
export function composeAnimationFrame(
  actor: LoadedActor,
  animIndex = 0,
  frameIndex = 0,
): ComposedFrame | null {
  const anim = actor.animations[animIndex] ?? pickStandAnimation(actor);
  if (!anim) return null;

  type Layer = { frame: ActorFrame; rect: ActorRect };
  const layers: Layer[] = [];
  const seen = new Set<string>();
  for (const part of anim.parts) {
    if (!part.frames.length) continue;
    const frame = part.frames[frameIndex % part.frames.length]!;
    const rect = actor.rects[frame.rectIndex];
    if (!rect) continue;
    const key = `${frame.rectIndex}:${frame.offsetX}:${frame.offsetY}`;
    if (seen.has(key)) continue;
    seen.add(key);
    layers.push({ frame, rect });
  }
  if (!layers.length) return null;

  let minX = Infinity;
  let minY = Infinity;
  let maxX = -Infinity;
  let maxY = -Infinity;
  for (const { frame, rect } of layers) {
    const w = rect.right - rect.left;
    const h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) continue;
    minX = Math.min(minX, frame.offsetX);
    minY = Math.min(minY, frame.offsetY);
    maxX = Math.max(maxX, frame.offsetX + w);
    maxY = Math.max(maxY, frame.offsetY + h);
  }
  if (!(maxX > minX && maxY > minY)) return null;
  const width = maxX - minX;
  const height = maxY - minY;
  if (width > 2048 || height > 2048) return null;

  const rgba = new Uint8ClampedArray(width * height * 4);
  for (const { frame, rect } of layers) {
    const image = actor.images[rect.imageIndex];
    if (!image) continue;
    const w = rect.right - rect.left;
    const h = rect.bottom - rect.top;
    if (
      w <= 0 ||
      h <= 0 ||
      rect.left < 0 ||
      rect.top < 0 ||
      rect.right > image.width ||
      rect.bottom > image.height
    ) {
      continue;
    }
    const destX = frame.offsetX - minX;
    const destY = frame.offsetY - minY;
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const si = ((rect.top + y) * image.width + (rect.left + x)) * 4;
        const a = image.rgba[si + 3]!;
        if (a === 0) continue;
        const di = ((destY + y) * width + (destX + x)) * 4;
        rgba[di] = image.rgba[si]!;
        rgba[di + 1] = image.rgba[si + 1]!;
        rgba[di + 2] = image.rgba[si + 2]!;
        rgba[di + 3] = a;
      }
    }
  }
  return { width, height, rgba, originX: minX, originY: minY, artScale: actor.artScale ?? 1 };
}

export function composeActorFrameIndex(
  actor: LoadedActor,
  frameIndex = 0,
): ComposedFrame | null {
  // Prefer first animation multi-part compose; fall back to largest valid crop.
  const multi = composeAnimationFrame(actor, 0, frameIndex);
  if (multi && multi.width * multi.height >= 64) return multi;
  const part = pickStandPart(actor);
  if (!part || !part.frames.length) return multi;
  // When caller asks for frame 0 (portraits / stand), prefer the largest valid
  // frame in the part — some UI actors store the face after empty sentinel rows.
  if (frameIndex === 0) {
    const best = pickBestFrame(actor, part);
    if (best) return composeActorFrame(actor, best);
  }
  const indexed = part.frames[frameIndex % part.frames.length]!;
  const rect = actor.rects[indexed.rectIndex];
  if (rect && rect.right > rect.left && rect.bottom > rect.top) {
    return composeActorFrame(actor, indexed);
  }
  const fallback = pickBestFrame(actor, part);
  return fallback ? composeActorFrame(actor, fallback) : multi;
}

/**
 * Title/splash keyframes (`ui_title.actor`): each animation *part* is one
 * keyframe whose `frames[]` are stacked layers (bg + logo halves + sword…).
 * Canvas size defaults to first layer (usually 240×400 Background).
 */
export function composeActorPartKeyframe(
  actor: LoadedActor,
  partIndex: number,
  animIndex = 0,
): ComposedFrame | null {
  const anim = actor.animations[animIndex] ?? pickStandAnimation(actor);
  if (!anim) return null;
  const part = anim.parts[partIndex];
  if (!part || !part.frames.length) return null;

  const base = composeActorFrame(actor, part.frames[0]!);
  if (!base) return null;
  // Prefer full-screen bg when present.
  let width = base.width;
  let height = base.height;
  let originX = 0;
  let originY = 0;
  for (const f of part.frames) {
    const rect = actor.rects[f.rectIndex];
    if (!rect) continue;
    const w = rect.right - rect.left;
    const h = rect.bottom - rect.top;
    if (w >= 240 && h >= 400) {
      width = w;
      height = h;
      originX = f.offsetX;
      originY = f.offsetY;
      break;
    }
  }

  const rgba = new Uint8ClampedArray(width * height * 4);
  for (const frame of part.frames) {
    const layer = composeActorFrame(actor, frame);
    if (!layer) continue;
    const dx = frame.offsetX - originX;
    const dy = frame.offsetY - originY;
    for (let y = 0; y < layer.height; y++) {
      for (let x = 0; x < layer.width; x++) {
        const sx = dx + x;
        const sy = dy + y;
        if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
        const si = (y * layer.width + x) * 4;
        const a = layer.rgba[si + 3]!;
        if (a === 0) continue;
        const di = (sy * width + sx) * 4;
        rgba[di] = layer.rgba[si]!;
        rgba[di + 1] = layer.rgba[si + 1]!;
        rgba[di + 2] = layer.rgba[si + 2]!;
        rgba[di + 3] = 255;
      }
    }
  }
  return { width, height, rgba, originX, originY, artScale: actor.artScale ?? 1 };
}

export function actorPartKeyframeCount(actor: ParsedActor, animIndex = 0): number {
  return actor.animations[animIndex]?.parts.length ?? 0;
}

/** Frame count for stand/walk animation cycling (legacy: frame-index within parts). */
export function standFrameCount(actor: ParsedActor): number {
  const anim = pickStandAnimation(actor);
  if (!anim) return 0;
  return Math.max(0, ...anim.parts.map((p) => p.frames.length));
}

/**
 * Map / scene actors store each *part* as one pose: `frames[]` are layers to
 * stack (shadow + body + weapon…). Walk/stand are separate animations; facing
 * picks a part. Evidence: hW_warrior anim0/1 part stacks ≈20×39 / 22×37;
 * e_ghostfire 4 parts ≈13×28; FB_NPC 1 part multi-layer.
 *
 * `skipImage` drops FX layers (e.g. /jian|guang/ for side map poses that reuse
 * attack anims, or never use death.gif for locomotion).
 */
export function composePartStacked(
  actor: LoadedActor,
  animIndex: number,
  partIndex: number,
  skipImage?: RegExp,
): ComposedFrame | null {
  const anim = actor.animations[animIndex];
  if (!anim?.parts.length) return null;
  const part = anim.parts[((partIndex % anim.parts.length) + anim.parts.length) % anim.parts.length];
  if (!part?.frames.length) return null;

  type Layer = { frame: ActorFrame; rect: ActorRect };
  const layers: Layer[] = [];
  for (const frame of part.frames) {
    const rect = actor.rects[frame.rectIndex];
    if (!rect) continue;
    if (skipImage && skipImage.test(actor.imageNames[rect.imageIndex] || "")) {
      continue;
    }
    const w = rect.right - rect.left;
    const h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) continue;
    layers.push({ frame, rect });
  }
  if (!layers.length) return null;

  let minX = Infinity;
  let minY = Infinity;
  let maxX = -Infinity;
  let maxY = -Infinity;
  for (const { frame, rect } of layers) {
    const w = rect.right - rect.left;
    const h = rect.bottom - rect.top;
    minX = Math.min(minX, frame.offsetX);
    minY = Math.min(minY, frame.offsetY);
    maxX = Math.max(maxX, frame.offsetX + w);
    maxY = Math.max(maxY, frame.offsetY + h);
  }
  const width = maxX - minX;
  const height = maxY - minY;
  if (!(width > 0 && height > 0) || width > 2048 || height > 2048) return null;

  const rgba = new Uint8ClampedArray(width * height * 4);
  for (const { frame, rect } of layers) {
    const image = actor.images[rect.imageIndex];
    if (!image) continue;
    const w = rect.right - rect.left;
    const h = rect.bottom - rect.top;
    if (
      rect.left < 0 ||
      rect.top < 0 ||
      rect.right > image.width ||
      rect.bottom > image.height
    ) {
      continue;
    }
    const destX = frame.offsetX - minX;
    const destY = frame.offsetY - minY;
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const si = ((rect.top + y) * image.width + (rect.left + x)) * 4;
        const a = image.rgba[si + 3]!;
        if (a === 0) continue;
        const di = ((destY + y) * width + (destX + x)) * 4;
        rgba[di] = image.rgba[si]!;
        rgba[di + 1] = image.rgba[si + 1]!;
        rgba[di + 2] = image.rgba[si + 2]!;
        rgba[di + 3] = a;
      }
    }
  }
  return { width, height, rgba, originX: minX, originY: minY, artScale: actor.artScale ?? 1 };
}

/** Average layer count — walk poses have more pieces than stand. */
function avgLayers(anim: ActorAnimation | undefined): number {
  if (!anim?.parts.length) return 0;
  let n = 0;
  for (const p of anim.parts) n += p.frames.length;
  return n / anim.parts.length;
}

export type MapAnimPlan = {
  /**
   * `player4`: CBE job actors (hW_warrior evidence)
   *   down: stand/walk anim1 (front; parts cycle while moving)
   *   up: stand/walk anim3 (back)
   *   right: stand anim5 body + walk anim0
   *   left: stand anim4 body + walk anim2
   *   anim0/2 are L/R walks — not NS; anim8/9 are death.
   * `parts`: monster/NPC — facing and/or flap via parts (see partMode).
   * `static`: single pose.
   */
  mode: "player4" | "parts" | "static";
  /**
   * How `parts` mode picks a part:
   * - `frames`: all parts unique → animation strip (e_ghostfire flicker); ignore dir.
   * - `dirFlap`: 2 unique poses padded to 4/6/8 → dir picks group, phase flaps.
   * - `dir`: part index from CBE facing (4-dir etc.).
   */
  partMode: "frames" | "dirFlap" | "dir";
  standAnim: number;
  /** Walk pose anim indices to alternate (monster flap often [0,1]). */
  walkAnims: number[];
  partCount: number;
};

/** Fingerprint a part stack without decoding pixels. */
function partFingerprint(part: ActorPart): string {
  return part.frames
    .map((f) => `${f.rectIndex}:${f.offsetX}:${f.offsetY}`)
    .join("|");
}

function uniquePartCount(anim: ActorAnimation | undefined): number {
  if (!anim?.parts.length) return 0;
  return new Set(anim.parts.map(partFingerprint)).size;
}

/** Classify monster part layout (ghostfire frames vs padded L/R flap). */
export function classifyPartMode(anim: ActorAnimation | undefined): MapAnimPlan["partMode"] {
  if (!anim?.parts.length) return "dir";
  const n = anim.parts.length;
  const uniq = uniquePartCount(anim);
  if (n >= 2 && uniq === n) return "frames";
  if (uniq === 2 && n >= 4) return "dirFlap";
  // Paired hold frames (n_teleporter pulse: 5 unique × 2) — cycle as animation.
  if (n >= 4 && uniq >= 3 && uniq * 2 === n) {
    let paired = true;
    for (let i = 0; i + 1 < n; i += 2) {
      if (partFingerprint(anim.parts[i]!) !== partFingerprint(anim.parts[i + 1]!)) {
        paired = false;
        break;
      }
    }
    if (paired) return "frames";
  }
  return "dir";
}

/** Mean rect area of an anim — skip alternating walk anims that are a different pose. */
function animMeanArea(actor: ParsedActor, animIndex: number): number {
  const anim = actor.animations[animIndex];
  if (!anim) return 0;
  let area = 0;
  let n = 0;
  for (const part of anim.parts) {
    for (const f of part.frames) {
      const r = actor.rects[f.rectIndex];
      if (!r) continue;
      const w = r.right - r.left;
      const h = r.bottom - r.top;
      if (w > 0 && h > 0) {
        area += w * h;
        n++;
      }
    }
  }
  return n ? area / n : 0;
}

function walkAnimsCompatible(actor: ParsedActor, a: number, b: number): boolean {
  const aa = animMeanArea(actor, a);
  const bb = animMeanArea(actor, b);
  if (!(aa > 0 && bb > 0)) return false;
  const ratio = aa > bb ? aa / bb : bb / aa;
  return ratio < 1.35;
}

/** Pick stand/walk anim indices from actor layout. */
export function planMapAnims(actor: ParsedActor): MapAnimPlan {
  const empty = (): MapAnimPlan => ({
    mode: "static",
    partMode: "dir",
    standAnim: 0,
    walkAnims: [0],
    partCount: 0,
  });
  const n = actor.animations.length;
  if (n <= 0) return empty();
  const partCount = actor.animations[0]?.parts.length ?? 0;
  const partMode = classifyPartMode(actor.animations[0]);

  if (n === 1) {
    return {
      mode: partCount > 1 ? "parts" : "static",
      partMode,
      standAnim: 0,
      walkAnims: [0],
      partCount,
    };
  }

  // Player jobs: ≥10 anims, 8 parts; anim8/9 reference death.gif.
  if (n >= 10 && partCount >= 8 && actor.animations[4] && actor.animations[5]) {
    return {
      mode: "player4",
      partMode: "dir",
      standAnim: 1,
      walkAnims: [0],
      partCount,
    };
  }

  const a0 = avgLayers(actor.animations[0]);
  const a1 = avgLayers(actor.animations[1]);
  if (n >= 3 && a0 > a1 + 0.5 && partCount >= 4) {
    return {
      mode: "parts",
      partMode: classifyPartMode(actor.animations[1]) === "frames"
        ? "frames"
        : classifyPartMode(actor.animations[0]),
      standAnim: 1,
      walkAnims: [0, 2].filter((i) => i < n),
      partCount,
    };
  }
  if (n >= 2) {
    const walks =
      partMode === "frames"
        ? [0]
        : walkAnimsCompatible(actor, 0, 1)
          ? [0, 1]
          : [0];
    return {
      mode: "parts",
      partMode,
      standAnim: 0,
      walkAnims: walks,
      partCount,
    };
  }
  return {
    mode: "parts",
    partMode,
    standAnim: 0,
    walkAnims: [0],
    partCount,
  };
}

/**
 * CBE move dir 1=up 2=right 3=down 4=left → part index inside an anim.
 * Used for monster `dir` / `dirFlap` modes (not player4 / frames).
 */
export function mapDirToPartIndex(
  partCount: number,
  cbeDir: number,
  phase = 0,
): number {
  if (partCount <= 1) return 0;
  const dir = ((cbeDir - 1) % 4 + 4) % 4; // 0..3
  if (partCount >= 8) return (dir % 4) * 2;
  if (partCount === 6) return (dir % 3) + (phase % 2) * 3;
  if (partCount === 4) return dir % 4;
  if (partCount === 3) return dir % 3;
  return phase % partCount;
}

/** Monster part picker — ghostfire frames vs padded flap vs true dirs. */
export function mapMonsterPartIndex(
  partMode: MapAnimPlan["partMode"],
  partCount: number,
  cbeDir: number,
  phase: number,
): number {
  if (partCount <= 1) return 0;
  if (partMode === "frames") {
    return ((phase % partCount) + partCount) % partCount;
  }
  if (partMode === "dirFlap") {
    if (partCount === 6) return mapDirToPartIndex(6, cbeDir, phase);
    if (partCount === 4) {
      // Evidence e_bee*: p0==p1, p2==p3 — two facings × two flap frames.
      const base = cbeDir === 3 || cbeDir === 4 ? 2 : 0;
      return base + (phase % 2);
    }
    if (partCount >= 8) {
      // Padded banks vary: believer/soldier = two offset-twin breath poses;
      // kirin = two rect-remapping stands. Battle idle uses standBreathPartIndex
      // for the offset-twin case; map keeps a stable facing pad here.
      const base = cbeDir === 3 || cbeDir === 4 ? 4 : 0;
      return base + (phase % 4);
    }
  }
  return mapDirToPartIndex(partCount, cbeDir, phase);
}

/**
 * Map side poses: drop attack slash sheets + glow + death (body keeps a held
 * silhouette for some jobs). Battle idle must NOT use this — 天机 weapon is
 * solely `jian.gif` and would vanish (幻剑 `bishou` / 鬼道 `zhang` would stay).
 */
const SIDE_MAP_SKIP = /jian|guang|death/i;
/** Battle idle/hit: keep weapon layers (jian/zhang/bishou); drop glow + death. */
const BATTLE_IDLE_SKIP = /guang|death/i;

/**
 * Player job map facing — matches CBE stacked part playback (same as admin
 * actor preview: one part = one pose, frames[] = layers).
 *
 * Evidence (hW_warrior):
 *   anim1 = front stand (face toward viewer); anim3 = back stand.
 *   anim0 = right walk; anim2 = left walk — NOT front/back walk pairs.
 *   anim4 body = left stand; anim5 body = right stand (skip jian/guang).
 *   anim8/9 include death.gif — never map-locomote.
 * Front/back walk reuse stand anims and cycle parts (no dedicated NS walk).
 */
function player4DirAnims(cbeDir: number): {
  stand: number;
  walk: number;
  flipX: boolean;
  /** Skip FX when composing stand pose (side attack anims). */
  skipFxStand: boolean;
  /** Skip FX when composing walk pose. */
  skipFxWalk: boolean;
} {
  const dir = ((cbeDir - 1) % 4 + 4) % 4;
  switch (dir) {
    case 0: // up — back stand; walk cycles back parts
      return {
        stand: 3,
        walk: 3,
        flipX: false,
        skipFxStand: false,
        skipFxWalk: false,
      };
    case 1: // right — side stand + right walk
      return {
        stand: 5,
        walk: 0,
        flipX: false,
        skipFxStand: true,
        skipFxWalk: false,
      };
    case 2: // down — front stand; walk cycles front parts
      return {
        stand: 1,
        walk: 1,
        flipX: false,
        skipFxStand: false,
        skipFxWalk: false,
      };
    default: // left — side stand + left walk
      return {
        stand: 4,
        walk: 2,
        flipX: false,
        skipFxStand: true,
        skipFxWalk: false,
      };
  }
}

/** Compose a map pose for facing / move state. */
export function composeMapActor(
  actor: LoadedActor,
  cbeDir: number,
  moving: boolean,
  phase: number,
): ComposedFrame | null {
  // Redesign PNG sheets: whole idle/action poses — never stack stock .actor rects.
  if (actor.sheetMode === "poses" && actor.poseAtlas) {
    const dir = ((cbeDir - 1) % 4 + 4) % 4;
    const flipX = dir === 3; // left
    // Map idle uses breath cycle via phase; action is battle-only.
    void moving;
    return (
      composePoseSheetFrame(actor, "idle", flipX, phase) ||
      composePoseSheetFrame(actor, "action", flipX, phase)
    );
  }

  // Hi-res e_deity: baked body/attack poses — skip LKV2 fragment stack.
  if (actor.deityHiRes) {
    const dir = ((cbeDir - 1) % 4 + 4) % 4;
    const flipX = dir === 3;
    void moving;
    return composeDeityHiResFrame(actor, "idle", flipX, phase);
  }

  const plan = planMapAnims(actor);
  if (!plan.partCount && !actor.animations[0]?.parts.length) {
    return composeActorFrameIndex(actor, phase);
  }

  if (plan.mode === "player4") {
    const d = player4DirAnims(cbeDir);
    // Side stand anim4/5 part0 = legs-apart ready (matches attack body).
    // Map idle 收脚 is walk anim part6 (hW_warrior a0p6 / a2p6 evidence).
    const sideIdle = !moving && d.skipFxStand;
    const animIndex = sideIdle ? d.walk : moving ? d.walk : d.stand;
    const n = actor.animations[animIndex]?.parts.length ?? 0;
    let partIndex = 0;
    if (n > 1) {
      if (moving) partIndex = (phase % 4) * 2 % n;
      else if (sideIdle) partIndex = n >= 8 ? 6 : n >= 4 ? Math.floor(n / 2) : 0;
      else partIndex = 0; // front/back stand = part0
    }
    const skip = (moving ? d.skipFxWalk : d.skipFxStand)
      ? SIDE_MAP_SKIP
      : undefined;
    const stacked = composePartStacked(actor, animIndex, partIndex, skip);
    if (stacked && stacked.width * stacked.height >= 32) {
      return { ...stacked, flipX: d.flipX };
    }
    return composeActorFrameIndex(actor, phase);
  }

  const partCount =
    actor.animations[plan.standAnim]?.parts.length ||
    actor.animations[0]?.parts.length ||
    0;
  if (!partCount) return composeActorFrameIndex(actor, phase);

  let animIndex: number;
  let partIndex: number;
  if (plan.partMode === "frames") {
    // e_ghostfire*: parts are flicker frames, not facings.
    animIndex = plan.standAnim;
    partIndex = mapMonsterPartIndex("frames", partCount, cbeDir, phase);
  } else if (partCount === 6) {
    animIndex = moving ? plan.walkAnims[0]! : plan.standAnim;
    partIndex = mapMonsterPartIndex(
      plan.partMode,
      partCount,
      cbeDir,
      phase,
    );
  } else if (moving && plan.walkAnims.length > 1) {
    animIndex = plan.walkAnims[phase % plan.walkAnims.length]!;
    const n = actor.animations[animIndex]?.parts.length ?? partCount;
    partIndex = mapMonsterPartIndex(
      plan.partMode,
      n,
      cbeDir,
      phase,
    );
  } else if (moving) {
    animIndex = plan.walkAnims[0]!;
    const n = actor.animations[animIndex]?.parts.length ?? partCount;
    partIndex = mapMonsterPartIndex(plan.partMode, n, cbeDir, phase);
  } else {
    animIndex = plan.standAnim;
    partIndex = mapMonsterPartIndex(
      plan.partMode,
      actor.animations[animIndex]?.parts.length ?? partCount,
      cbeDir,
      phase,
    );
  }

  const stacked = composePartStacked(actor, animIndex, partIndex);
  if (stacked && stacked.width * stacked.height >= 32) return stacked;
  return composeActorFrameIndex(actor, phase);
}

/**
 * Battle-stage poses — same JHOnlineData `.actor` assets mmBattle plays via
 * unit sprite objects + main-CBE PicLib blit (not a separate pixel codec).
 *
 * Resource evidence (blade/body centroid + PNG probe; see tmp/facing-probe):
 *   hW_warrior stand left=anim4 / right=anim5
 *   attack/cast left=anim6 / right=anim7
 *   CBE evidence (e_ratB 掘地鼠): 待机趴着身子不动，只晃尾巴 — stand anim0
 *   keeps body+head layers fixed and alternates the last (tail) layer.
 *   anim1 (5 parts) changes head+silhouette → attack strip, not idle.
 *   e_bee*: same rect stack at facing part2; only layer offsets change between
 *   anim0/anim1 (翅) — same “body fixed / detail moves” rule, not walk morph.
 *   e_believer / e_solider (dirFlap x8): standAnim has two offset-twin poses
 *   (p0..3 vs p4..7) — 待机上身/下身轻起伏; not anim1 slash morph.
 *   e_skeletonH: anim1 + wuqi1 → attack-only; idle stays on anim0 bob.
 *
 * unresolved (need IDA DrawBattleCharacter): exact frame timer, melee dash
 * interpolation, unit struct anim/part field offsets.
 */
export type BattlePose = "idle" | "attack" | "cast" | "hit" | "death";

function animImageNames(actor: LoadedActor, animIndex: number): Set<string> {
  const names = new Set<string>();
  for (const part of actor.animations[animIndex]?.parts ?? []) {
    for (const f of part.frames) {
      const rect = actor.rects[f.rectIndex];
      if (!rect) continue;
      names.add(actor.imageNames[rect.imageIndex] ?? "");
    }
  }
  return names;
}

/** Armed slash sheets (wuqi1) — attack strip, never 待机. */
function animHasSlashSheet(actor: LoadedActor, animIndex: number): boolean {
  for (const n of animImageNames(actor, animIndex)) {
    if (/wuqi1/i.test(n)) return true;
  }
  return false;
}

/**
 * Attack-only anim: slash sheet, or introduces a body sheet absent from stand
 * (e_devil1.gif). Shadow `ying.gif` alone does not count.
 */
function animIsAttackOnly(
  actor: LoadedActor,
  animIndex: number,
  standAnim: number,
): boolean {
  if (animHasSlashSheet(actor, animIndex)) return true;
  const stand = animImageNames(actor, standAnim);
  for (const n of animImageNames(actor, animIndex)) {
    if (!n || /ying/i.test(n)) continue;
    if (!stand.has(n)) return true;
  }
  return false;
}

function layerKey(f: ActorFrame): string {
  return `${f.rectIndex}:${f.offsetX}:${f.offsetY}`;
}

function sharedLayerPrefix(a: ActorPart, b: ActorPart): number {
  const n = Math.min(a.frames.length, b.frames.length);
  let k = 0;
  for (; k < n; k++) {
    if (layerKey(a.frames[k]!) !== layerKey(b.frames[k]!)) break;
  }
  return k;
}

/**
 * 掘地鼠-style detail flap: body/head stack shared; only a trailing detail
 * layer differs (尾巴 / 石怪贴片 / 多一层装饰). Rejects left↔right facing
 * banks where most layers shift.
 */
function isStandDetailFlapPair(a: ActorPart, b: ActorPart): boolean {
  const na = a.frames.length;
  const nb = b.frames.length;
  if (na < 1 || nb < 1) return false;
  if (na === nb) {
    if (na < 2) return false;
    const prefix = sharedLayerPrefix(a, b);
    // Same stack length: allow last 1–2 layers to differ only.
    return prefix >= 2 && prefix >= na - 2 && prefix < na;
  }
  // Unequal: longer part adds trailing detail on top of the shorter stack.
  const [shorter, longer] = na < nb ? [a, b] : [b, a];
  const prefix = sharedLayerPrefix(shorter, longer);
  return (
    prefix === shorter.frames.length &&
    longer.frames.length === shorter.frames.length + 1
  );
}

/**
 * Stand idle detail flap: facing base part + nearest detail partner.
 * Returns part index in standAnim, or null if no wag pair.
 */
function standTailFlapPartIndex(
  actor: LoadedActor,
  plan: MapAnimPlan,
  cbeDir: number,
  phase: number,
): number | null {
  const anim = actor.animations[plan.standAnim];
  if (!anim || anim.parts.length < 2) return null;
  const base = mapDirToPartIndex(anim.parts.length, cbeDir, 0);
  const basePart = anim.parts[base];
  const baseStacked = composePartStacked(actor, plan.standAnim, base);
  if (!basePart || !baseStacked) return null;
  let partner = -1;
  let best = 1e9;
  for (let i = 0; i < anim.parts.length; i++) {
    if (i === base) continue;
    if (!isStandDetailFlapPair(basePart, anim.parts[i]!)) continue;
    const stacked = composePartStacked(actor, plan.standAnim, i);
    if (!stacked) continue;
    // Skip padded dir duplicates that compose identically.
    if (
      stacked.width === baseStacked.width &&
      stacked.height === baseStacked.height &&
      stacked.rgba.length === baseStacked.rgba.length &&
      stacked.rgba.every((v, idx) => v === baseStacked.rgba[idx])
    ) {
      continue;
    }
    const dist = Math.abs(i - base);
    if (dist < best) {
      best = dist;
      partner = i;
    }
  }
  if (partner < 0) return null;
  return phase % 2 === 0 ? base : partner;
}

/**
 * Attack/action strip: non-stand anim with a different part count (e_ratB
 * anim1=5). Not used for idle — rat anim1 also remaps the head layer.
 */
function findMonsterActionStrip(
  actor: LoadedActor,
  plan: MapAnimPlan,
): number | null {
  const standN = actor.animations[plan.standAnim]?.parts.length ?? 0;
  if (standN <= 0) return null;
  for (let ai = 0; ai < actor.animations.length; ai++) {
    if (ai === plan.standAnim) continue;
    const n = actor.animations[ai]?.parts.length ?? 0;
    if (n <= 1 || n === standN) continue;
    if (animHasSlashSheet(actor, ai)) continue;
    return ai;
  }
  return null;
}

/**
 * Same rect stack, only offsets differ — bee wing flap at a fixed facing part.
 * Rejects anim twins that remap rects (attack morph / 整身切姿).
 */
function isOffsetFlapTwin(a: ActorPart, b: ActorPart): boolean {
  if (a.frames.length !== b.frames.length || a.frames.length < 1) return false;
  let offsetDiff = false;
  for (let i = 0; i < a.frames.length; i++) {
    const x = a.frames[i]!;
    const y = b.frames[i]!;
    if (x.rectIndex !== y.rectIndex) return false;
    if (x.offsetX !== y.offsetX || x.offsetY !== y.offsetY) offsetDiff = true;
  }
  return offsetDiff;
}

/** Drop shadow/ying layers — e_beeG anim1 drops ying but body still offset-flaps. */
function partBodyFrames(actor: LoadedActor, part: ActorPart): ActorPart["frames"] {
  return part.frames.filter((f) => {
    const rect = actor.rects[f.rectIndex];
    if (!rect) return true;
    return !/ying/i.test(actor.imageNames[rect.imageIndex] ?? "");
  });
}

function isOffsetFlapTwinBody(
  actor: LoadedActor,
  a: ActorPart,
  b: ActorPart,
): boolean {
  const fa = partBodyFrames(actor, a);
  const fb = partBodyFrames(actor, b);
  if (fa.length < 1 || fa.length !== fb.length) return false;
  let offsetDiff = false;
  for (let i = 0; i < fa.length; i++) {
    const x = fa[i]!;
    const y = fb[i]!;
    if (x.rectIndex !== y.rectIndex) return false;
    if (x.offsetX !== y.offsetX || x.offsetY !== y.offsetY) offsetDiff = true;
  }
  return offsetDiff;
}

function battleFacingPartIndex(plan: MapAnimPlan, cbeDir: number): number {
  // Bee/monkey art: part2 faces the player; map cbeDir parts face wrong way.
  if (plan.partMode === "dirFlap" && plan.partCount === 4) return 2;
  return mapDirToPartIndex(plan.partCount, cbeDir, 0);
}

/**
 * Idle/attack detail flap across anim twins at one facing part (e_bee*).
 * Limited to dirFlap×4 — wider layouts (猪粽妖等) can share rects with offset
 * drift that is NOT 翅扇，勿误用。
 */
function offsetFlapAnimIndex(
  actor: LoadedActor,
  plan: MapAnimPlan,
  cbeDir: number,
  phase: number,
): { anim: number; part: number } | null {
  if (plan.partMode !== "dirFlap" || plan.partCount !== 4) return null;
  const twin =
    plan.walkAnims.find((ai) => ai !== plan.standAnim) ??
    (actor.animations[1] && 1 !== plan.standAnim ? 1 : undefined);
  if (twin == null || animIsAttackOnly(actor, twin, plan.standAnim)) {
    return null;
  }
  const part = battleFacingPartIndex(plan, cbeDir);
  const a = actor.animations[plan.standAnim]?.parts[part];
  const b = actor.animations[twin]?.parts[part];
  if (
    !a ||
    !b ||
    (!isOffsetFlapTwin(a, b) && !isOffsetFlapTwinBody(actor, a, b))
  ) {
    return null;
  }
  return {
    anim: phase % 2 === 0 ? plan.standAnim : twin,
    part,
  };
}

/**
 * Two stand poses that share a body prefix (same rects) then nudge limbs —
 * e_craw / e_monkey ×4. Rejects bee facing banks (body rect already differs).
 */
function isLooseStandBobPair(a: ActorPart, b: ActorPart): boolean {
  if (isOffsetFlapTwin(a, b) || isStandDetailFlapPair(a, b)) return true;
  const n = Math.min(a.frames.length, b.frames.length);
  if (n < 2) return false;
  let sameRectPrefix = 0;
  for (let i = 0; i < n; i++) {
    if (a.frames[i]!.rectIndex !== b.frames[i]!.rectIndex) break;
    sameRectPrefix++;
  }
  return sameRectPrefix >= 2;
}

/**
 * dirFlap stand bob between the two unique padded poses:
 * - ×8: p0..3 vs p4..7 (教徒呼吸 / 虎·麒麟细节)
 * - ×4: only loose body-prefix pairs (螃蟹/猴); bees use offsetFlapAnimIndex
 */
function standBreathPartIndex(
  actor: LoadedActor,
  plan: MapAnimPlan,
  phase: number,
): number | null {
  if (plan.partMode !== "dirFlap") return null;
  if (!(plan.partCount === 4 || plan.partCount >= 8)) return null;
  const anim = actor.animations[plan.standAnim];
  if (!anim || anim.parts.length < 2) return null;
  const uniqIdx: number[] = [];
  const seen = new Set<string>();
  for (let i = 0; i < anim.parts.length; i++) {
    const fp = partFingerprint(anim.parts[i]!);
    if (seen.has(fp)) continue;
    seen.add(fp);
    uniqIdx.push(i);
    if (uniqIdx.length > 2) return null;
  }
  if (uniqIdx.length !== 2) return null;
  if (
    plan.partCount === 4 &&
    !isLooseStandBobPair(anim.parts[uniqIdx[0]!]!, anim.parts[uniqIdx[1]!]!)
  ) {
    return null;
  }
  return uniqIdx[phase % 2]!;
}

function withBattleFacing(
  frame: ComposedFrame,
  facingRight: boolean,
): ComposedFrame {
  return facingRight ? frame : { ...frame, flipX: true };
}

export function composeBattlePose(
  actor: LoadedActor,
  facingRight: boolean,
  pose: BattlePose,
  phase: number,
): ComposedFrame | null {
  if (actor.sheetMode === "poses" && actor.poseAtlas) {
    const flipX = !facingRight;
    if (pose === "attack" || pose === "cast" || pose === "hit") {
      return (
        composePoseSheetFrame(actor, "action", flipX, phase) ||
        composePoseSheetFrame(actor, "idle", flipX, phase)
      );
    }
    // idle / death: breath cycle on idle frames
    return composePoseSheetFrame(actor, "idle", flipX, phase);
  }

  // Hi-res e_deity: attack/cast use action pose; hit stays on idle (no attack frame).
  if (actor.deityHiRes) {
    const flipX = !facingRight;
    if (pose === "attack" || pose === "cast") {
      return (
        composeDeityHiResFrame(actor, "action", flipX, phase) ||
        composeDeityHiResFrame(actor, "idle", flipX, phase)
      );
    }
    return composeDeityHiResFrame(actor, "idle", flipX, phase);
  }

  const plan = planMapAnims(actor);
  const cbeDir = facingRight ? 2 : 4;

  if (plan.mode === "player4") {
    if (pose === "death") {
      const anim = facingRight ? 9 : 8;
      const n = actor.animations[anim]?.parts.length ?? 1;
      if (!n) return null;
      const part = Math.min(phase, n - 1);
      return composePartStacked(actor, anim, part);
    }
    if (pose === "attack" || pose === "cast") {
      // Resource evidence: anim6 faces left, anim7 faces right.
      // Keep jian/guang slash layers — mmBattle plays the full attack strip
      // (weapon swing). Idle/map still skip FX via SIDE_MAP_SKIP.
      // Play the strip once (clamp), do not loop — long pose holds used to
      // wrap phase%n ≈ twice and looked like double cast/attack.
      const anim = facingRight ? 7 : 6;
      const n = actor.animations[anim]?.parts.length ?? 1;
      if (!n) return composeMapActor(actor, cbeDir, true, phase);
      const part = Math.min(Math.max(0, phase | 0), n - 1);
      return composePartStacked(actor, anim, part);
    }
    if (pose === "hit") {
      const d = player4DirAnims(cbeDir);
      const n = actor.animations[d.stand]?.parts.length ?? 1;
      const part = n > 2 ? 2 : 0;
      return composePartStacked(actor, d.stand, part, BATTLE_IDLE_SKIP);
    }
    // Idle: cycle side-stand parts with weapon layers (jian/zhang/bishou).
    // Map locomotion still uses SIDE_MAP_SKIP via composeMapActor.
    const d = player4DirAnims(cbeDir);
    const n = actor.animations[d.stand]?.parts.length ?? 1;
    const part = n <= 1 ? 0 : ((phase % 4) * 2) % n;
    const stacked = composePartStacked(actor, d.stand, part, BATTLE_IDLE_SKIP);
    if (stacked && stacked.width * stacked.height >= 32) {
      return { ...stacked, flipX: d.flipX };
    }
    // Fallback must still keep weapons — composeMapActor side-idle strips jian.
    const bare = composePartStacked(actor, d.stand, part);
    if (bare && bare.width * bare.height >= 32) {
      return { ...bare, flipX: d.flipX };
    }
    return composeActorFrameIndex(actor, phase);
  }

  // Monsters — idle: stand trailing-detail (鼠) → stand breath bob (人形×8)
  // → offset-twin flap (蜂). Never idle on remapping anim1 walk/attack morphs.
  if (pose === "idle") {
    const wag = standTailFlapPartIndex(actor, plan, cbeDir, phase);
    if (wag != null) {
      const stacked = composePartStacked(actor, plan.standAnim, wag);
      if (stacked) return stacked;
    }
    const breath = standBreathPartIndex(actor, plan, phase);
    if (breath != null) {
      const stacked = composePartStacked(actor, plan.standAnim, breath);
      if (stacked) return withBattleFacing(stacked, facingRight);
    }
    const wing = offsetFlapAnimIndex(actor, plan, cbeDir, phase);
    if (wing) {
      const stacked = composePartStacked(actor, wing.anim, wing.part);
      if (stacked) return withBattleFacing(stacked, facingRight);
    }
  }
  if (pose === "attack" || pose === "cast") {
    const actionStrip = findMonsterActionStrip(actor, plan);
    if (actionStrip != null) {
      const n = actor.animations[actionStrip]!.parts.length;
      const part = Math.min(Math.max(0, phase | 0), n - 1);
      const stacked = composePartStacked(actor, actionStrip, part);
      if (stacked) return withBattleFacing(stacked, facingRight);
    }
    // Bees have no separate attack strip — same offset-twin flap as idle.
    const wing = offsetFlapAnimIndex(actor, plan, cbeDir, phase);
    if (wing) {
      const stacked = composePartStacked(actor, wing.anim, wing.part);
      if (stacked) return withBattleFacing(stacked, facingRight);
    }
  }

  const moving = pose === "attack" || pose === "cast";
  return composeMapActor(actor, cbeDir, moving, phase);
}

/** jobIndex 0..2, sex 0=male 1=female — matches mock_server table. */
export function actorResourceForJobSex(jobIndex: number, sex: number): string {
  const names = [
    "h_warrior.actor",
    "hW_warrior.actor",
    "h_assassin.actor",
    "hW_assassin.actor",
    "h_mage.actor",
    "hW_mage.actor",
  ];
  const job = Math.max(0, Math.min(2, jobIndex | 0));
  const s = sex ? 1 : 0;
  return names[job * 2 + s]!;
}

/** HUD portrait motion actors (ui_h_* / ui_hw_*). */
export function uiActorResourceForJobSex(jobIndex: number, sex: number): string {
  const names = [
    "ui_h_war.actor",
    "ui_hw_war.actor",
    "ui_h_ass.actor",
    "ui_hw_ass.actor",
    "ui_h_mag.actor",
    "ui_hw_mag.actor",
  ];
  const job = Math.max(0, Math.min(2, jobIndex | 0));
  const s = sex ? 1 : 0;
  return names[job * 2 + s]!;
}
