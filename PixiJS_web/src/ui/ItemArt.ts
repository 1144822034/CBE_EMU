import { Container, Sprite, Texture } from "pixi.js";
import { equipCategoryIconIndex } from "../resources/itemCatalog";
import { loadUiCrop, tryLoadUiTexture, uiSprite } from "./UiAssets";

/** handkuang.gif outer size. */
export const SLOT_FRAME_SIZE = 25;
/** equipico.gif cell size. */
export const EQUIP_ICON_SIZE = 16;

const iconCache = new Map<number, Texture | null>();
let frameTex: Texture | null | undefined;
let artWarm: Promise<void> | null = null;

/** Prefetch handkuang + all equipico tiles. */
export async function warmEquipArt(): Promise<void> {
  if (!artWarm) {
    artWarm = (async () => {
      frameTex = await tryLoadUiTexture("handkuang.gif");
      await Promise.all(
        Array.from({ length: 10 }, (_, i) => equipIconTexture(i)),
      );
    })().catch((e) => {
      artWarm = null;
      throw e;
    });
  }
  await artWarm;
}

export async function slotFrameTexture(): Promise<Texture | null> {
  if (frameTex !== undefined) return frameTex;
  frameTex = await tryLoadUiTexture("handkuang.gif");
  return frameTex;
}

export async function equipIconTexture(category: number): Promise<Texture | null> {
  const idx = equipCategoryIconIndex(category);
  if (iconCache.has(idx)) return iconCache.get(idx)!;
  const tex = await loadUiCrop(
    "equipico.gif",
    idx * EQUIP_ICON_SIZE,
    0,
    EQUIP_ICON_SIZE,
    EQUIP_ICON_SIZE,
  );
  iconCache.set(idx, tex);
  return tex;
}

export type MountEquipSlotOpts = {
  category: number;
  /** Empty slot: dim the category icon. */
  empty?: boolean;
  broken?: boolean;
};

/**
 * Place handkuang frame + centered 16×16 equipico tile into `host`.
 * Caller owns `host` lifecycle; textures are shared (do not destroy them).
 */
export function mountEquipSlot(
  host: Container,
  frame: Texture | null,
  icon: Texture | null,
  opts: MountEquipSlotOpts,
): void {
  host.removeChildren().forEach((c) => {
    // Shared textures — destroy sprite only.
    c.destroy();
  });

  if (frame) {
    const fr = uiSprite(frame, SLOT_FRAME_SIZE, SLOT_FRAME_SIZE);
    fr.position.set(0, 0);
    host.addChild(fr);
  }

  if (icon) {
    const ic = uiSprite(icon, EQUIP_ICON_SIZE, EQUIP_ICON_SIZE);
    ic.position.set(
      Math.floor((SLOT_FRAME_SIZE - EQUIP_ICON_SIZE) / 2),
      Math.floor((SLOT_FRAME_SIZE - EQUIP_ICON_SIZE) / 2),
    );
    if (opts.empty) ic.alpha = 0.35;
    else if (opts.broken) ic.alpha = 0.55;
    host.addChild(ic);
  }
}

/** Sync helper after warmEquipArt — returns cached textures. */
export function equipIconTextureSync(category: number): Texture | null {
  return iconCache.get(equipCategoryIconIndex(category)) ?? null;
}

export function slotFrameTextureSync(): Texture | null {
  return frameTex ?? null;
}

/**
 * Drop equip-art Texture handles after {@link invalidateUiGpuTextures}.
 * `iconCache` / `frameTex` otherwise keep destroyed sources for worn/bag UI.
 */
export function invalidateEquipArtGpu(): void {
  iconCache.clear();
  frameTex = undefined;
  artWarm = null;
}

/** Small leading icon for replace-list rows (16×16, no frame). */
export function mountEquipIconOnly(
  host: Container,
  icon: Texture | null,
  x: number,
  y: number,
  alpha = 1,
): Sprite | null {
  if (!icon) return null;
  const spr = uiSprite(icon, EQUIP_ICON_SIZE, EQUIP_ICON_SIZE);
  spr.position.set(x, y);
  spr.alpha = alpha;
  host.addChild(spr);
  return spr;
}
