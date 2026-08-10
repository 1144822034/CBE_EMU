import { readI32LE, decodeGbk } from "./binary";
import type { GifRgba } from "./lkv2Gif";
import {
  getCachedResourceStamp,
  loadDecodedPayload,
  loadGif,
  shouldVersionCheckResource,
} from "./loader";

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
};

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

export function invalidateActorCache(name?: string): void {
  if (name) actorCache.delete(name.toLowerCase());
  else actorCache.clear();
}

export async function loadActor(name: string): Promise<LoadedActor> {
  const key = name.toLowerCase();
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
        try {
          return await loadGif(gif, {
            revalidate: shouldVersionCheckResource(gif),
          });
        } catch (e) {
          console.warn("[actor] gif failed", name, gif, e);
          return null;
        }
      }),
    );
    return { ...parsed, images };
  })();
  actorCache.set(key, { stamp, promise });
  return promise;
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
  if (w <= 0 || h <= 0 || w > 512 || h > 512) return null;
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
  if (width > 512 || height > 512) return null;

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
  return { width, height, rgba, originX: minX, originY: minY };
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
  return { width, height, rgba, originX, originY };
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
  if (!(width > 0 && height > 0) || width > 512 || height > 512) return null;

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
  return { width, height, rgba, originX: minX, originY: minY };
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
