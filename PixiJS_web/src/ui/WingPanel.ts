/**
 * Wing picker — only unlocked ids from server 1/31.
 */
import { Container, Graphics } from "pixi.js";
import { CBE, LCD_HEIGHT, LCD_WIDTH, SOFTKEY_H } from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { clearPointerHits } from "./pointerHit";
import {
  listShellBounds,
  paintListShellBody,
  paintListShellChrome,
  type ListShellCtx,
} from "./listPanelShell";
import { wingLabel } from "../resources/wingFx";

const PAGE_SIZE = 10;

export class WingPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private listCursor = 0;
  private selectedId = 0;
  private rows: { id: number; label: string }[] = [{ id: 0, label: "关闭翅膀" }];

  onPick: ((wingId: number) => void) | null = null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "none";
    const dim = new Graphics()
      .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
      .fill({ color: 0x000000, alpha: 0.4 });
    dim.eventMode = "static";
    dim.on("pointertap", (e) => e.stopPropagation());
    this.addChild(dim);
    this.addChild(this.box);
  }

  open(currentWingId = 0, unlockedIds: number[] = []): void {
    const unlocked = [...new Set(unlockedIds.filter((id) => id > 0))].sort(
      (a, b) => a - b,
    );
    this.rows = [
      { id: 0, label: "关闭翅膀" },
      ...unlocked.map((id) => ({ id, label: wingLabel(id) })),
    ];
    this.selectedId = currentWingId | 0;
    const idx = this.rows.findIndex((r) => r.id === this.selectedId);
    this.listCursor = idx >= 0 ? idx : 0;
    this.eventMode = "static";
    this.visible = true;
    this.redraw();
  }

  close(): void {
    clearPointerHits(this.rowHits);
    this.visible = false;
    this.eventMode = "none";
  }

  softkeys(): { left: string; right: string } {
    return { left: "选择", right: "关闭" };
  }

  move(delta: number): void {
    const n = this.rows.length;
    if (n <= 0) return;
    this.listCursor = (this.listCursor + delta + n) % n;
    this.redraw();
  }

  confirm(): void {
    const row = this.rows[this.listCursor];
    if (!row) return;
    this.selectedId = row.id;
    this.onPick?.(row.id);
    this.redraw();
  }

  /** Softkey 关闭: dismiss panel only. Unequip via list「关闭翅膀」. */
  cancel(): void {
    this.close();
    this.onClose?.();
  }

  private addLabel(text: string, x: number, y: number, color: number): void {
    const t = new BitmapLabel();
    t.show(text, color, { outline: false, smooth: false });
    t.position.set(x, y);
    this.box.addChild(t);
    this.labels.push(t);
  }

  private redraw(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.box.clear();
    this.box.removeChildren();

    const bounds = listShellBounds();
    const ctx: ListShellCtx = {
      host: this,
      box: this.box,
      hits: this.rowHits,
      addLabel: (text, x, y, color) => this.addLabel(text, x, y, color),
    };
    paintListShellChrome(ctx, "翅膀", CBE.bgDeep, bounds);
    const listTop = bounds.y + 32;
    const footY = bounds.y + bounds.h - 28;
    paintListShellBody({
      ctx,
      x: bounds.x + 6,
      listTop,
      w: bounds.w - 12,
      footY,
      rows: this.rows.map((r) => ({
        name: r.id === this.selectedId ? `* ${r.label}` : r.label,
        nameColor: r.id === this.selectedId ? CBE.inkTip : CBE.ink,
      })),
      cursor: this.listCursor,
      pageSize: PAGE_SIZE,
      paging: "local",
      emptyText: "尚无已解锁翅膀",
      onRow: (absIndex) => {
        this.listCursor = absIndex;
        this.confirm();
      },
    });
  }
}
