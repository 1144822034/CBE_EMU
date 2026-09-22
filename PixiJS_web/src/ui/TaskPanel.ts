/**
 * Task list: accepted (6/1) vs candidates (6/14), detail / abandon / accept.
 */
import { Container, Graphics } from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
} from "./constants";
import { BitmapLabel, UI_WRAP_CHARS, wrapChars } from "./BitmapLabel";
import type { TaskCandidateRow, TaskListRow } from "../net/protocolExtras";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import { attachPageNavButtons } from "./pageNav";
import {
  listShellBounds,
  listShellStepPage,
  paintListShellBody,
  paintListShellChrome,
  type ListShellCtx,
} from "./listPanelShell";

export type TaskTab = "accepted" | "candidates";
export type TaskAction = "detail" | "abandon" | "accept" | "refresh";

const LIST_PAGE_SIZE = 8;
const DETAIL_PAGE_SIZE = 10;

type Mode = "list" | "actions" | "detail";

const STATE_LABEL: Record<number, string> = {
  1: "进行中",
  2: "可提交",
};

export class TaskPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private mode: Mode = "list";
  private tab: TaskTab = "accepted";
  private listCursor = 0;
  private actionCursor = 0;
  private detailPage = 0;
  private accepted: TaskListRow[] = [];
  private candidates: TaskCandidateRow[] = [];
  private actionLabels: string[] = [];
  private detailLines: string[] = [];
  private toast: PanelTipState = createPanelTipState();

  onAction: ((action: TaskAction, taskId: number) => void) | null = null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(
      new Graphics()
        .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
        .fill({ color: 0x000000, alpha: 0.35 }),
    );
    this.addChild(this.box);
  }

  open(
    accepted: TaskListRow[],
    candidates: TaskCandidateRow[],
    tab: TaskTab = "accepted",
  ): void {
    this.accepted = accepted;
    this.candidates = candidates;
    this.tab = tab;
    this.mode = "list";
    this.listCursor = 0;
    this.actionCursor = 0;
    this.detailPage = 0;
    clearPanelTip(this.toast);
    this.visible = true;
    this.clampCursor();
    this.redraw();
  }

  setLists(accepted: TaskListRow[], candidates: TaskCandidateRow[]): void {
    this.accepted = accepted;
    this.candidates = candidates;
    this.clampCursor();
    if (this.mode === "actions") this.actionLabels = this.buildActions();
    else if (this.mode !== "detail") this.mode = "list";
    this.redraw();
  }

  showDetail(text: string): void {
    const body = (text || "(无详情)").replace(/\r\n/g, "\n").replace(/\r/g, "\n");
    this.detailLines = body
      .split("\n")
      .flatMap((p) => wrapChars(p, UI_WRAP_CHARS));
    this.detailPage = 0;
    this.mode = "detail";
    this.redraw();
  }

  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (this.visible) this.redraw();
    }, ms);
  }

  get selectedTaskId(): number {
    const row = this.currentRows()[this.listCursor];
    return row?.taskId ?? 0;
  }

  move(delta: number): void {
    if (!this.visible) return;
    if (this.mode === "actions") {
      const n = this.actionLabels.length;
      if (!n) return;
      this.actionCursor = (this.actionCursor + delta + n) % n;
      this.redraw();
      return;
    }
    if (this.mode === "detail") {
      const pages = Math.max(1, Math.ceil(this.detailLines.length / DETAIL_PAGE_SIZE));
      this.detailPage = (this.detailPage + delta + pages) % pages;
      this.redraw();
      return;
    }
    const n = Math.max(1, this.currentRows().length);
    this.listCursor = (this.listCursor + delta + n) % n;
    this.redraw();
  }

  /** ← → switch 已接 / 可接 */
  moveTab(delta: number): void {
    if (!this.visible || this.mode !== "list") return;
    if (delta < 0) this.tab = "accepted";
    else if (delta > 0) this.tab = "candidates";
    this.listCursor = 0;
    this.redraw();
  }

  confirm(): void {
    if (!this.visible) return;
    if (this.mode === "detail") {
      const pages = Math.max(1, Math.ceil(this.detailLines.length / DETAIL_PAGE_SIZE));
      if (pages > 1 && this.detailPage < pages - 1) {
        this.detailPage += 1;
        this.redraw();
        return;
      }
      this.mode = "list";
      this.redraw();
      return;
    }
    if (this.mode === "list") {
      if (!this.currentRows().length) {
        this.onAction?.("refresh", 0);
        return;
      }
      this.actionLabels = this.buildActions();
      this.actionCursor = 0;
      this.mode = "actions";
      this.redraw();
      return;
    }
    const label = this.actionLabels[this.actionCursor];
    if (!label || label === "返回") {
      this.mode = "list";
      this.redraw();
      return;
    }
    const taskId = this.selectedTaskId;
    if (label === "详情") this.onAction?.("detail", taskId);
    else if (label === "放弃") this.onAction?.("abandon", taskId);
    else if (label === "接取") this.onAction?.("accept", taskId);
    else if (label === "刷新") this.onAction?.("refresh", 0);
  }

  cancel(): void {
    if (!this.visible) return;
    if (tryDismissPanelTipOnSoftRight(this.toast)) return;
    if (this.mode === "actions" || this.mode === "detail") {
      this.mode = "list";
      this.redraw();
      return;
    }
    this.hide();
    this.onClose?.();
  }

  hide(): void {
    clearPanelTip(this.toast);
    this.visible = false;
  }

  private currentRows(): { taskId: number; name: string; subtitle: string }[] {
    if (this.tab === "accepted") {
      return this.accepted.map((t) => ({
        taskId: t.taskId,
        name: t.name || `任务${t.taskId}`,
        subtitle: `Lv${t.level} ${STATE_LABEL[t.state] ?? `状态${t.state}`}${
          t.reqCount1 ? ` ${t.progress1}/${t.reqCount1}` : ""
        }`,
      }));
    }
    return this.candidates.map((t) => ({
      taskId: t.taskId,
      name: t.name || `任务${t.taskId}`,
      subtitle: `Lv${t.level} ${t.giver || ""}`.trim(),
    }));
  }

  private clampCursor(): void {
    const n = this.currentRows().length;
    if (!n) this.listCursor = 0;
    else if (this.listCursor >= n) this.listCursor = n - 1;
  }

  private buildActions(): string[] {
    const row = this.currentRows()[this.listCursor];
    if (!row) return ["刷新", "返回"];
    if (this.tab === "accepted") return ["详情", "放弃", "返回"];
    return ["详情", "接取", "返回"];
  }

  private shellCtx(): ListShellCtx {
    return {
      host: this,
      box: this.box,
      hits: this.rowHits,
      addLabel: (t, x, y, c) => this.addLabel(t, x, y, c),
      addLabelCentered: (t, cx, cy, c) => this.addLabelCentered(t, cx, cy, c),
    };
  }

  private addLabel(text: string, px: number, py: number, color: number): void {
    const lab = new BitmapLabel();
    void lab.setText(text, color, { outline: false });
    lab.position.set(px, py);
    this.addChild(lab);
    this.labels.push(lab);
  }

  private addLabelCentered(
    text: string,
    cx: number,
    cy: number,
    color: number,
  ): void {
    const lab = new BitmapLabel();
    lab.anchor.set(0.5, 0.5);
    lab.position.set(Math.round(cx), Math.round(cy));
    void lab.setText(text, color, { outline: false });
    this.addChild(lab);
    this.labels.push(lab);
  }


  /** webglcontextrestored: remount paintCbePanel Graphics then redraw. */
  nudgeAfterGpuResume(): void {
    this.box = replaceGraphics(this.box);
    if (this.visible) this.redraw();
  }

  private redraw(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.box.clear();
    const bounds = listShellBounds();
    const { x, y, w, h } = bounds;
    const ctx = this.shellCtx();
    paintListShellChrome(ctx, "任务列表", CBE.bgDeep, bounds);

    const tabY = y + 30;
    this.addLabel(
      this.tab === "accepted" ? "[已接]" : " 已接 ",
      x + 10,
      tabY,
      this.tab === "accepted" ? CBE.menuHi : CBE.inkLabel,
    );
    this.addLabel(
      this.tab === "candidates" ? "[可接]" : " 可接 ",
      x + 78,
      tabY,
      this.tab === "candidates" ? CBE.menuHi : CBE.inkLabel,
    );
    this.addLabel("←→", x + w - 36, tabY, CBE.inkLabel);
    this.rowHits.push(
      attachRowPointerHit(this, x + 8, tabY - 2, 60, 18, () => {
        if (this.mode !== "list") return;
        this.moveTab(-1);
      }),
    );
    this.rowHits.push(
      attachRowPointerHit(this, x + 76, tabY - 2, 60, 18, () => {
        if (this.mode !== "list") return;
        this.moveTab(1);
      }),
    );

    try {
      if (this.mode === "detail") {
        const ps = DETAIL_PAGE_SIZE;
        const pages = Math.max(1, Math.ceil(this.detailLines.length / ps));
        const start = this.detailPage * ps;
        let ly = tabY + 18;
        for (const line of this.detailLines.slice(start, start + ps)) {
          this.addLabel(line, x + 10, ly, CBE.inkMint);
          ly += 14;
        }
        this.addLabelCentered(
          `详情 ${this.detailPage + 1}/${pages}`,
          x + w / 2,
          y + h - 12,
          CBE.inkLabel,
        );
        if (pages > 1) {
          attachPageNavButtons({
            host: this,
            hits: this.rowHits,
            box: this.box,
            x: x + 8,
            y: y + h - 40,
            w: w - 16,
            page: this.detailPage + 1,
            pages,
            addLabel: (t, lx, ly, c) => this.addLabel(t, lx, ly, c),
            onPrev: () => {
              this.detailPage = Math.max(0, this.detailPage - 1);
              this.redraw();
            },
            onNext: () => {
              this.detailPage = Math.min(pages - 1, this.detailPage + 1);
              this.redraw();
            },
          });
        }
        this.rowHits.push(
          attachRowPointerHit(this, x + 6, tabY + 16, w - 12, h - 70, () =>
            this.confirm(),
          ),
        );
        return;
      }

      if (this.mode === "actions") {
        let ly = tabY + 18;
        const row = this.currentRows()[this.listCursor];
        this.addLabel(row?.name || "", x + 10, ly, CBE.ink);
        ly += 18;
        this.actionLabels.forEach((lab, i) => {
          const sel = i === this.actionCursor;
          if (sel) {
            this.box.rect(x + 6, ly - 1, w - 12, 16).fill({ color: CBE.select });
          }
          this.addLabel(lab, x + 14, ly, sel ? CBE.ink : CBE.inkMint);
          const idx = i;
          const hitY = ly;
          this.rowHits.push(
            attachRowPointerHit(this, x + 6, hitY - 1, w - 12, 16, () => {
              this.actionCursor = idx;
              this.redraw();
              this.confirm();
            }),
          );
          ly += 16;
        });
        return;
      }

      const rows = this.currentRows();
      if (!rows.length) {
        this.addLabel(
          this.tab === "accepted" ? "(无已接任务)" : "(无可接任务)",
          x + 10,
          tabY + 18,
          CBE.inkLabel,
        );
        this.addLabel("确定·刷新", x + 10, tabY + 36, CBE.inkLabel);
        this.rowHits.push(
          attachRowPointerHit(this, x + 6, tabY + 17, w - 12, 40, () =>
            this.confirm(),
          ),
        );
      } else {
        paintListShellBody({
          ctx,
          x,
          listTop: tabY + 18,
          w,
          footY: y + h - 36,
          rows: rows.map((r) => ({ name: r.name, subtitle: r.subtitle })),
          cursor: this.listCursor,
          pageSize: LIST_PAGE_SIZE,
          dualLine: true,
          paging: "local",
          onRow: (abs) => {
            this.listCursor = abs;
            this.redraw();
            this.confirm();
          },
          onPrevPage: () => {
            this.listCursor = listShellStepPage(
              this.listCursor,
              rows.length,
              LIST_PAGE_SIZE,
              -1,
            );
            this.redraw();
          },
          onNextPage: () => {
            this.listCursor = listShellStepPage(
              this.listCursor,
              rows.length,
              LIST_PAGE_SIZE,
              1,
            );
            this.redraw();
          },
        });
        const infoPage = Math.floor(this.listCursor / LIST_PAGE_SIZE) + 1;
        const infoPages = Math.max(
          1,
          Math.ceil(rows.length / LIST_PAGE_SIZE),
        );
        this.addLabelCentered(
          `${this.tab === "accepted" ? "已接" : "可接"} ${infoPage}/${infoPages}`,
          x + w / 2,
          y + h - 12,
          CBE.inkLabel,
        );
      }
    } finally {
      drawPanelTipOverlay(this, this.toast);
    }
  }
}
