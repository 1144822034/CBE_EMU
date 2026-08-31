import { Graphics } from "pixi.js";

/**
 * After webglcontextrestored, Pixi 8 often leaves Graphics GPU buffers blank
 * while Texture sprites *may* recover via source.update(). Replacing the
 * DisplayObject forces a fresh geometry upload on the next paint.
 *
 * Caveat: BufferImageSource (battle map / pose rgbaToTexture) frequently stays
 * black after update() — callers must recreate those Textures from CPU RGBA
 * (see BattleScreen.nudgeAfterGpuResume), not only nudgeTextureSource.
 *
 * Also: Container.mask Graphics (SceneView.clipMask / BattleScreen.stageHost mask)
 * dying after restore clips the whole map/unit band to empty while HUD outside
 * the mask still paints — replace those masks first on resume.
 * Prefer one mask owner per band; sharing one Graphics across hosts breaks when
 * StencilMask.reset toggles includeInBuild during replaceGraphics.
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
