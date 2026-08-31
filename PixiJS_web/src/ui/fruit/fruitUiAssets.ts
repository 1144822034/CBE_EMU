import { Assets, Texture } from "pixi.js";
import {
  loadPlistAtlas,
  retainPlistAtlas,
  type LoadedPlistAtlas,
} from "../../resources/plistAtlas";
import { jhOnlineDataPathUrl } from "../../resources/publicUrl";
import {
  sliceFruitDigitFont,
  type FruitDigitFont,
} from "./fruitDigitLabel";
import { FRUIT_BG_SCALE, FRUIT_UI_OX, FRUIT_UI_OY } from "./fruitUiLayout";

export type FruitUiAssets = {
  atlas: LoadedPlistAtlas;
  machineBg: Texture;
  arrowLeft: Texture;
  arrowRight: Texture;
  digitFont: FruitDigitFont;
};

let loadPromise: Promise<FruitUiAssets> | null = null;

export function fruitAtlasTexture(
  atlas: LoadedPlistAtlas,
  frameName: string,
): Texture | null {
  const want = frameName.endsWith(".png") ? frameName : `${frameName}.png`;
  const i = atlas.frames.findIndex((f) => f.name === want);
  return i >= 0 ? (atlas.textures[i] ?? null) : null;
}

async function loadStandalone(rel: string): Promise<Texture> {
  const url = jhOnlineDataPathUrl(rel);
  const tex = (await Assets.load(url)) as Texture;
  tex.source.scaleMode = "nearest";
  return tex;
}

export function loadFruitUiAssets(): Promise<FruitUiAssets> {
  if (loadPromise) return loadPromise;
  loadPromise = (async () => {
    const atlas = await loadPlistAtlas("fruit_ui/game/002", { uiAtlas: true });
    retainPlistAtlas(atlas);
    const [machineBg, arrowLeft, arrowRight, digitSheet] = await Promise.all([
      loadStandalone("fruit_ui/machine1_bg.png"),
      loadStandalone("fruit_ui/jiantou_1.png"),
      loadStandalone("fruit_ui/jiantou_3.png"),
      loadStandalone("fruit_ui/font/ziti_jp.png"),
    ]);
    return {
      atlas,
      machineBg,
      arrowLeft,
      arrowRight,
      digitFont: sliceFruitDigitFont(digitSheet),
    };
  })();
  loadPromise.catch(() => {
    loadPromise = null;
  });
  return loadPromise;
}

/** 背景铺满 LCD 宽度（480→240）。 */
export function layoutMachineBg(_bg: Texture): {
  x: number;
  y: number;
  scale: number;
} {
  return {
    x: FRUIT_UI_OX,
    y: FRUIT_UI_OY,
    scale: FRUIT_BG_SCALE,
  };
}
