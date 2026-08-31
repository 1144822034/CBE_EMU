import { Graphics, Sprite } from "pixi.js";
import { rgbaToTexture } from "./rgbaTexture";

/**
 * After webglcontextrestored, Pixi 8 often leaves Graphics GPU buffers blank
 * while Texture sprites *may* recover via source.update(). Replacing the
 * DisplayObject forces a fresh geometry upload on the next paint.
 *
 * Caveat: BufferImageSource (battle map / pose rgbaToTexture) frequently stays
 * black after update() — callers must recreate those Textures from CPU RGBA
 * (see BattleScreen.nudgeAfterGpuResume), not only nudgeTextureSource.
 *
 * Also: Container.mask as Graphics dies after restore and clips the whole
 * map/unit band to empty while HUD outside the mask still paints. Field
 * evidence (正存室 / 正椁室): replaceGraphics on the mask is not enough on
 * Android WebView — use a Sprite mask remounted from CPU RGBA instead.
 *
 * Evidence: docs/re/2026-08-18-pixi-apk-black-screen-plan.md (Graphics 失效);
 * docs/re/2026-08-30-pixi-battle-black-recover-incomplete.md (map/units black).
 */
export function replaceGraphics(old: Graphics): Graphics {
  const parent = old.parent;
  const idx = parent ? parent.getChildIndex(old) : -1;
  const next = new Graphics();
  old.destroy();
  if (parent && !parent.destroyed) {
    if (idx >= 0 && idx <= parent.children.length) parent.addChildAt(next, idx);
    else parent.addChild(next);
  }
  return next;
}

const WHITE_RGBA = new Uint8ClampedArray([255, 255, 255, 255]);

/** 1×1 white texture scaled to (width×height) — stencil/color mask that remounts. */
export function createSolidMaskSprite(width: number, height: number): Sprite {
  const tex = rgbaToTexture(1, 1, WHITE_RGBA);
  const spr = new Sprite(tex);
  spr.width = Math.max(1, width | 0);
  spr.height = Math.max(1, height | 0);
  spr.eventMode = "none";
  return spr;
}

/**
 * Destroy the previous mask Sprite (and its BufferImageSource) and mount a
 * fresh one at the same child index. Caller must clear container.mask first.
 */
export function remountSolidMaskSprite(
  old: Sprite | null | undefined,
  width: number,
  height: number,
): Sprite {
  const parent = old?.parent ?? null;
  const idx = old && parent && !parent.destroyed ? parent.getChildIndex(old) : -1;
  if (old) {
    try {
      if (parent && !parent.destroyed) parent.removeChild(old);
      old.destroy({ texture: true, textureSource: true });
    } catch {
      /* ignore */
    }
  }
  const next = createSolidMaskSprite(width, height);
  if (parent && !parent.destroyed) {
    if (idx >= 0 && idx <= parent.children.length) parent.addChildAt(next, idx);
    else parent.addChild(next);
  }
  return next;
}

/** Re-touch a TextureSource GPU upload; no-op if source already gone. */
export function nudgeTextureSource(
  tex:
    | {
        source?: {
          update?: () => void;
          destroyed?: boolean;
          width?: number;
        } | null;
      }
    | null
    | undefined,
): boolean {
  try {
    const src = tex?.source;
    if (!src || src.destroyed) return false;
    src.update?.();
    return !!(src.width && src.width > 0);
  } catch {
    return false;
  }
}
