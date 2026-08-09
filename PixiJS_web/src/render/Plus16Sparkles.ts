import { Container, Graphics } from "pixi.js";

/** One hue per worn +16 slot (ROYGBIV + violet), index 0..7. */
export const PLUS16_RAINBOW: readonly number[] = [
  0xff3b3b, // red
  0xff8c1a, // orange
  0xffe014, // yellow
  0x3dff6a, // green
  0x2ad4ff, // cyan
  0x3b6bff, // blue
  0xa23bff, // violet
  0xff3bd4, // magenta
];

/**
 * Map +16 aura: one concentric foot ring per piece + soft glow.
 * Origin = actor feet (0,0). Shared breath scale — rings never cross.
 */
export class Plus16Sparkles {
  readonly root = new Container();
  private readonly halo = new Container();
  private readonly glow = new Graphics();
  private readonly ringsHost = new Container();
  private count = 0;
  private t = 0;

  constructor() {
    this.root.zIndex = 0;
    this.root.eventMode = "none";
    this.root.visible = false;
    this.halo.position.set(0, 0);
    this.halo.addChild(this.glow, this.ringsHost);
    this.root.addChild(this.halo);
  }

  /** 0..8 worn slots at enhance >= 16 — one ring each. */
  setCount(n: number): void {
    const next = Math.max(0, Math.min(8, n | 0));
    if (next === this.count) return;
    this.count = next;
    this.rebuild();
  }

  tick(dtMs: number): void {
    if (this.count <= 0) return;
    this.t += dtMs;
    const wave = 0.5 + 0.5 * Math.sin(this.t * 0.0032);
    this.halo.scale.set(0.88 + 0.18 * wave);
    this.halo.alpha = 0.5 + 0.45 * wave;
  }

  destroy(): void {
    this.root.destroy({ children: true });
    this.count = 0;
  }

  private ringRadii(i: number): { rx: number; ry: number } {
    return { rx: 6.5 + i * 3.2, ry: 2.3 + i * 1.1 };
  }

  private rebuild(): void {
    this.ringsHost.removeChildren().forEach((c) => c.destroy());
    this.glow.clear();
    if (this.count <= 0) {
      this.root.visible = false;
      return;
    }
    this.root.visible = true;

    const outer = this.ringRadii(this.count - 1);
    for (let k = 0; k < this.count; k++) {
      const color = PLUS16_RAINBOW[k]!;
      const { rx, ry } = this.ringRadii(k);
      this.glow.ellipse(0, 0, rx + 2, ry + 1).fill({ color, alpha: 0.06 });
    }
    this.glow
      .ellipse(0, 0, outer.rx * 0.5, outer.ry * 0.5)
      .fill({ color: 0xffffff, alpha: 0.12 });

    for (let i = 0; i < this.count; i++) {
      const color = PLUS16_RAINBOW[i]!;
      const { rx, ry } = this.ringRadii(i);
      const g = new Graphics();
      g.ellipse(0, 0, rx, ry).stroke({ width: 1.0, color, alpha: 0.9 });
      this.ringsHost.addChild(g);
    }
  }
}

/** Count worn items with enhance >= 16. */
export function countPlus16Worn(
  items: ReadonlyArray<{ enhance?: number; kind?: string }>,
): number {
  let n = 0;
  for (const it of items) {
    if ((it.enhance ?? 0) >= 16) n++;
  }
  return Math.min(8, n);
}
