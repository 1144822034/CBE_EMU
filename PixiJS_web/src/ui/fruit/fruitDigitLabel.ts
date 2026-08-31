/** luckyFruit LabelAtlas `ziti_jp`（0–9，每格 23×33）。 */
import { Container, Rectangle, Sprite, Texture } from "pixi.js";

export const FRUIT_DIGIT_W = 23;
export const FRUIT_DIGIT_H = 33;

export type FruitDigitFont = {
  digits: Texture[];
};

export function sliceFruitDigitFont(sheet: Texture): FruitDigitFont {
  const base = sheet.source;
  const digits: Texture[] = [];
  for (let i = 0; i < 10; i++) {
    const tex = new Texture({
      source: base,
      frame: new Rectangle(1 + i * FRUIT_DIGIT_W, 0, FRUIT_DIGIT_W, FRUIT_DIGIT_H),
    });
    digits.push(tex);
  }
  return { digits };
}

export type FruitDigitShowOpts = {
  /** 最少位数（左侧补 0），默认 1。 */
  minDigits?: number;
  /** 最多位数，默认 6。 */
  maxDigits?: number;
};

/** 居中显示正整数（原版 credit / bonus / point / 押分 同款数码管）。 */
export class FruitDigitLabel extends Container {
  private font: FruitDigitFont | null = null;
  private sprites: Sprite[] = [];
  private gap = 0;

  setFont(font: FruitDigitFont): void {
    this.font = font;
  }

  show(value: number | string, opts?: FruitDigitShowOpts): void {
    const font = this.font;
    if (!font) return;
    const minDigits = Math.max(1, opts?.minDigits ?? 1);
    const maxDigits = Math.max(minDigits, opts?.maxDigits ?? 6);
    let raw = String(value).replace(/\D/g, "");
    if (!raw) raw = "0";
    if (raw.length > maxDigits) raw = raw.slice(-maxDigits);
    while (raw.length < minDigits) raw = `0${raw}`;

    while (this.sprites.length < raw.length) {
      const sp = new Sprite();
      sp.anchor.set(0.5);
      sp.eventMode = "none";
      this.addChild(sp);
      this.sprites.push(sp);
    }
    for (let i = 0; i < this.sprites.length; i++) {
      const sp = this.sprites[i]!;
      if (i >= raw.length) {
        sp.visible = false;
        continue;
      }
      const d = raw.charCodeAt(i) - 48;
      sp.texture = font.digits[d] ?? font.digits[0]!;
      sp.visible = true;
    }
    const n = raw.length;
    const totalW = n * FRUIT_DIGIT_W + Math.max(0, n - 1) * this.gap;
    let x = -totalW / 2 + FRUIT_DIGIT_W / 2;
    for (let i = 0; i < n; i++) {
      const sp = this.sprites[i]!;
      sp.x = x;
      sp.y = 0;
      x += FRUIT_DIGIT_W + this.gap;
    }
  }
}
