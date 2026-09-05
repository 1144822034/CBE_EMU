import { Container, Graphics } from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  WORLD_BOTTOM_H,
  WORLD_TOP_H,
  paintCbePanel,
  makePixelText,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

/**
 * Compact non-modal "和XXX对话?" prompt over the map.
 * Stays on world phase so WASD still moves; walk-away closes it.
 */
export class ApproachPrompt extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  actorId = 0;
  private npcName = "";

  onConfirm: ((actorId: number) => void) | null = null;
  onCancel: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.addChild(this.box);
  }

  get open(): boolean {
    return this.visible;
  }

  show(actorId: number, name: string): void {
    this.actorId = actorId;
    this.npcName = name || "对方";
    this.visible = true;
    this.redraw();
  }

  hide(): void {
    this.visible = false;
    this.actorId = 0;
    this.npcName = "";
    this.clearLabels();
    clearPointerHits(this.rowHits);
    this.box.clear();
  }

  confirm(): void {
    if (!this.visible || !this.actorId) return;
    const id = this.actorId;
    this.hide();
    this.onConfirm?.(id);
  }

  cancel(): void {
    if (!this.visible) return;
    this.hide();
    this.onCancel?.();
  }

  private clearLabels(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
  }


  /** webglcontextrestored: remount paintCbePanel Graphics then redraw. */
  nudgeAfterGpuResume(): void {
    this.box = replaceGraphics(this.box);
    if (this.visible) this.redraw();
  }

  private redraw(): void {
    this.clearLabels();
    clearPointerHits(this.rowHits);
    this.box.clear();

    const body = `和${this.npcName}对话?`;
    // Compact CBE-style plate; no full-screen dim so the map stays playable.
    const w = Math.min(200, LCD_WIDTH - 28);
    const h = 52;
    const x = Math.floor((LCD_WIDTH - w) / 2);
    const mapMid =
      WORLD_TOP_H + Math.floor((LCD_HEIGHT - WORLD_TOP_H - WORLD_BOTTOM_H) / 2);
    const y = Math.max(WORLD_TOP_H + 8, mapMid - Math.floor(h / 2) - 24);

    paintCbePanel(this, this.box, x, y, w, h, CBE.bgDeep);

    const text = makePixelText(body, 12, CBE.ink);
    text.anchor.set(0.5, 0.5);
    text.position.set(LCD_WIDTH / 2, y + Math.floor(h / 2));
    text.eventMode = "none";
    this.addChild(text);
    this.labels.push(text);
    this.rowHits.push(
      attachRowPointerHit(this, x, y, w, h, () => this.confirm()),
    );
  }
}
