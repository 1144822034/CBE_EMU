import { Container, Sprite, Texture } from "pixi.js";
import { equipCategoryIconIndex } from "../resources/itemCatalog";
import { loadUiCrop, tryLoadUiTexture, uiSprite } from "./UiAssets";

/** handkuang.gif outer size. */
export const SLOT_FRAME_SIZE = 25;
/** equipico.gif cell size. */
export const EQUIP_ICON_SIZE = 16;
/**
 * Worn-panel part labels `0.gif`..`7.gif` (帽衣披束裤鞋饰品武器).
 * Index matches EQUIP_SLOT_UI visual order, not body slot id.
 */
export const EQUIP_PART_LABEL_COUNT = 8;
/** Widest part label (饰品/武器 are 25×13). */
export const EQUIP_PART_LABEL_MAX_W = 25;

const iconCache = new Map<number, Texture | null>();
const partLabelCache: (Texture | null)[] = [];
let frameTex: Texture | null | undefined;
let artWarm: Promise<void> | null = null;

/** Prefetch handkuang + equipico tiles + worn part labels 0.gif..7.gif. */
export async function warmEquipArt(): Promise<void> {
  if (!artWarm) {
    artWarm = (async () => {
      frameTex = await tryLoadUiTexture("handkuang.gif");
      await Promise.all([
        ...Array.from({ length: 10 }, (_, i) => equipIconTexture(i)),
        ...Array.from({ length: EQUIP_PART_LABEL_COUNT }, (_, i) =>
          equipPartLabelTexture(i),
        ),
      ]);
    })().catch((e) => {
      artWarm = null;
      throw e;
    });
  }
  await artWarm;
}

/** `0.gif`..`7.gif` — cyan part name beside each worn slot. */
export async function equipPartLabelTexture(
  uiIndex: number,
): Promise<Texture | null> {
  const idx = uiIndex | 0;
  if (idx < 0 || idx >= EQUIP_PART_LABEL_COUNT) return null;
  if (partLabelCache[idx] !== undefined) return partLabelCache[idx]!;
  const tex = await tryLoadUiTexture(`${idx}.gif`);
  partLabelCache[idx] = tex;
  return tex;
}

export function equipPartLabelTextureSync(uiIndex: number): Texture | null {
  const idx = uiIndex | 0;
  if (idx < 0 || idx >= EQUIP_PART_LABEL_COUNT) return null;
  return partLabelCache[idx] ?? null;
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
  partLabelCache.length = 0;
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
