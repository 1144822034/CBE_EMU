import { Texture } from "pixi.js";
import { rgbaToTexture } from "./rgbaTexture";

/**
 * Shared CPU→GPU pose textures for map / battle actors.
 * Uncached per-frame rgbaToTexture+destroy on Android WebView exhausts the
 * GL context (blank/white LCD) even with the screen on.
 *
 * Eviction must never destroy a texture still bound to a live sprite. SceneView
 * and ActorLayer previously passed only *their* in-use keys, so one side could
 * destroy the other's pose mid-frame — Pixi then crashes in StencilMaskPipe
 * (`textureSource.alphaMode` on null) when the map clip mask pops.
 */
const poseTexCache = new Map<string, Texture>();
const POSE_CACHE_SOFT = 96;
const POSE_CACHE_HARD = 128;

/** Refcount of pose keys currently assigned to map/player sprites. */
const livePoseKeys = new Map<string, number>();

export function poseTextureKey(
  actorId: string,
  dir: number,
  moving: boolean,
  phase: number,
  width: number,
  height: number,
): string {
  return `${actorId}|${dir}|${moving ? 1 : 0}|${phase}|${width}x${height}`;
}

export function retainPoseTextureKey(key: string): void {
  if (!key) return;
  livePoseKeys.set(key, (livePoseKeys.get(key) ?? 0) + 1);
}

export function releasePoseTextureKey(key: string): void {
  if (!key) return;
  const n = livePoseKeys.get(key) ?? 0;
  if (n <= 1) livePoseKeys.delete(key);
  else livePoseKeys.set(key, n - 1);
}

/** Swap the key retained for one sprite slot (no-op when unchanged). */
export function rebindPoseTextureKey(prev: string, next: string): void {
  if (prev === next) return;
  if (next) retainPoseTextureKey(next);
  if (prev) releasePoseTextureKey(prev);
}

export function getOrCreatePoseTexture(
  key: string,
  width: number,
  height: number,
  rgba: Uint8ClampedArray | Uint8Array,
  inUseKeys?: ReadonlySet<string>,
): Texture {
  let tex = poseTexCache.get(key);
  if (tex && !tex.destroyed) return tex;
  tex = rgbaToTexture(width, height, rgba);
  poseTexCache.set(key, tex);
  evictPoseTextures(inUseKeys);
  return tex;
}

export function evictPoseTextures(inUseKeys?: ReadonlySet<string>): void {
  if (poseTexCache.size <= POSE_CACHE_HARD) return;
  for (const [k, tex] of poseTexCache) {
    if (poseTexCache.size <= POSE_CACHE_SOFT) break;
    if (livePoseKeys.has(k) || inUseKeys?.has(k)) continue;
    try {
      tex.destroy(true);
    } catch {
      /* ignore */
    }
    poseTexCache.delete(k);
  }
}

export function clearPoseTextureCache(): void {
  for (const tex of poseTexCache.values()) {
    try {
      tex.destroy(true);
    } catch {
      /* ignore */
    }
  }
  poseTexCache.clear();
  livePoseKeys.clear();
}
