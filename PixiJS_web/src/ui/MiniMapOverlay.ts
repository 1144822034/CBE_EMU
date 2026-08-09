import { Container, Graphics, Sprite, Texture } from "pixi.js";
import { CBE, LCD_WIDTH, WORLD_TOP_H } from "./constants";
import { mapBlocksAt, type MapPreview } from "../resources/map";

/**
 * Top-right local minimap (CBE: red frame, walkable green / blocked dark).
 * Compact horizontal strip under HUD.
 */
export class MiniMapOverlay extends Container {
  private frame = new Graphics();
  private mapSpr: Sprite | null = null;
  private dots = new Graphics();
  /** Wider than tall — small landscape thumb (was 72×88). */
  private mapW = 88;
  private mapH = 48;
  private tex: Texture | null = null;
  private lastKey = "";

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "none";
    this.addChild(this.frame, this.dots);
    this.layoutChrome();
  }

  private layoutChrome(): void {
    const x = LCD_WIDTH - this.mapW - 6;
    const y = WORLD_TOP_H + 2;
    this.position.set(x, y);
    this.frame.clear();
    this.frame.rect(0, 0, this.mapW, this.mapH).stroke({ width: 2, color: 0xe02020 });
  }

  /**
   * Rebuild collision bitmap when scene changes; update marker dots each tick.
   */
  update(
    map: MapPreview | null,
    mapPixelW: number,
    mapPixelH: number,
    playerX: number,
    playerY: number,
    markers: { x: number; y: number; kind: "npc" | "peer" | "monster" | "teleport" }[],
  ): void {
    if (!this.visible) return;
    const key = map ? `${mapPixelW}x${mapPixelH}:${map.cols}x${map.rows}` : "";
    if (map && key !== this.lastKey) {
      this.lastKey = key;
      this.rebuildBitmap(map, mapPixelW, mapPixelH);
    }
    this.dots.clear();
    if (!mapPixelW || !mapPixelH) return;
    const sx = this.mapW / mapPixelW;
    const sy = this.mapH / mapPixelH;
    for (const m of markers) {
      const color =
        m.kind === "teleport"
          ? 0xf0c040 // gold — 传送石 / 传送点
          : m.kind === "monster"
            ? 0xff4040
            : m.kind === "peer"
              ? 0x40ff80
              : 0x40ff40; // npc — green
      this.dots.circle(m.x * sx, m.y * sy, 1.2).fill({ color });
    }
    // Player: yellow diamond
    const px = playerX * sx;
    const py = playerY * sy;
    this.dots
      .poly([px, py - 2.5, px + 2.5, py, px, py + 2.5, px - 2.5, py])
      .fill({ color: 0xffe000 });
  }

  private rebuildBitmap(map: MapPreview, mw: number, mh: number): void {
    const tw = this.mapW;
    const th = this.mapH;
    const canvas = document.createElement("canvas");
    canvas.width = tw;
    canvas.height = th;
    const ctx = canvas.getContext("2d")!;
    const img = ctx.createImageData(tw, th);
    for (let y = 0; y < th; y++) {
      for (let x = 0; x < tw; x++) {
        const mx = ((x + 0.5) / tw) * mw;
        const my = ((y + 0.5) / th) * mh;
        const blocked = mapBlocksAt(map, mx, my, 0);
        const i = (y * tw + x) * 4;
        if (blocked) {
          img.data[i] = 0x28;
          img.data[i + 1] = 0x38;
          img.data[i + 2] = 0x48;
        } else {
          img.data[i] = 0x40;
          img.data[i + 1] = 0x90;
          img.data[i + 2] = 0x50;
        }
        img.data[i + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    if (this.mapSpr) {
      this.removeChild(this.mapSpr);
      this.mapSpr.destroy();
      this.mapSpr = null;
    }
    if (this.tex) {
      this.tex.destroy(true);
      this.tex = null;
    }
    this.tex = Texture.from(canvas);
    this.tex.source.scaleMode = "nearest";
    this.mapSpr = new Sprite(this.tex);
    this.addChildAt(this.mapSpr, 0);
  }

  show(): void {
    this.visible = true;
    this.lastKey = ""; // force bitmap rebuild on next update
  }

  hide(): void {
    this.visible = false;
  }

  toggle(): boolean {
    this.visible = !this.visible;
    return this.visible;
  }
}
