/**
 * Left-side accepted-quest tracker under the map status strip.
 * Visibility follows strip expand/collapse (same pref as 修/拍/…).
 */
import { Container, Graphics } from "pixi.js";
import { CBE, LCD_WIDTH, WORLD_TOP_H, makePixelText } from "./constants";
import { BitmapLabel, UC3_CELL, measureUiTextWidth } from "./BitmapLabel";
import { sceneDisplayName } from "./WorldHud";
import type { TaskListRow } from "../net/protocolExtras";
import { lookupNpcSceneFromDb } from "../resources/npcScenes";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

/** Map-local status strip clearance (matches WorldHud PARTY_Y0). */
const STRIP_BOTTOM = 4 + 17;
const TRACKER_Y0 = WORLD_TOP_H + STRIP_BOTTOM + 2;
const TRACKER_X0 = 4;
const TRACKER_MAX_W = 118;
const MAX_TASKS = 4;

/** Smaller than map HUD UC3 (14→10). */
const TEXT_SCALE = 10 / UC3_CELL;
const LINE_H = 10;
const PAD_X = 5;
const PAD_Y = 4;
const ACCENT_W = 2;
const GAP_AFTER_ACCENT = 3;
const TASK_GAP = 4;
const DIVIDER_H = 1;

export type QuestTrackerRow = {
  taskId: number;
  name: string;
  receiver: string;
  sceneLabel: string;
  progressLabel: string;
};

export function formatTaskProgress(t: TaskListRow): string {
  if ((t.state >>> 0) === 2) return "可提交";
  const parts: string[] = [];
  if (t.reqCount1) parts.push(`${t.progress1}/${t.reqCount1}`);
  if (t.reqCount2) parts.push(`${t.progress2}/${t.reqCount2}`);
  return parts.length ? parts.join(" ") : "进行中";
}

/**
 * Build tracker rows: submit NPC + scene from server_dynamic_npcs.
 */
export function buildQuestTrackerRows(
  accepted: TaskListRow[],
  opts?: {
    /** Optional override map; default uses loadNpcSceneIndex cache. */
    npcSceneByName?: ReadonlyMap<string, string>;
  },
): QuestTrackerRow[] {
  const out: QuestTrackerRow[] = [];
  for (const t of accepted) {
    if (out.length >= MAX_TASKS) break;
    const receiver = (t.receiver || "").replace(/\0+$/g, "").trim();
    const sceneKey = receiver
      ? opts?.npcSceneByName?.get(receiver) ||
        lookupNpcSceneFromDb(receiver) ||
        ""
      : "";
    const sceneLabel = sceneKey
      ? sceneDisplayName(sceneKey)
      : receiver
        ? "未知"
        : "";
    out.push({
      taskId: t.taskId >>> 0,
      name: (t.name || `任务${t.taskId}`).replace(/\0+$/g, "").trim(),
      receiver,
      sceneLabel,
      progressLabel: formatTaskProgress(t),
    });
  }
  return out;
}

export class QuestTrackerOverlay extends Container {
  /** Tap tracker → open full task list. */
  onOpenTasks: (() => void) | null = null;

  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private rows: QuestTrackerRow[] = [];
  private stripExpanded = true;
  private worldActive = false;
  private partyInset = 0;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.zIndex = 12;
    this.addChild(this.box);
    this.position.set(TRACKER_X0, TRACKER_Y0);
  }

  /** Mirror map-corner status strip expand (false when collapsed). */
  setStripExpanded(on: boolean): void {
    this.stripExpanded = !!on;
    this.applyVisibility();
  }

  /** Only show on the interactive world map band. */
  setWorldActive(on: boolean): void {
    this.worldActive = !!on;
    this.applyVisibility();
  }

  /** Shift right when party portraits occupy the left column. */
  setPartyInset(px: number): void {
    const next = Math.max(0, px | 0);
    if (next === this.partyInset) return;
    this.partyInset = next;
    this.position.x = TRACKER_X0 + this.partyInset;
    this.redraw();
  }

  setTasks(rows: QuestTrackerRow[]): void {
    this.rows = rows.slice(0, MAX_TASKS);
    this.redraw();
    this.applyVisibility();
  }

  private applyVisibility(): void {
    this.visible =
      this.worldActive && this.stripExpanded && this.rows.length > 0;
  }

  private clearLabels(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
  }

  private addLine(
    text: string,
    x: number,
    y: number,
    color: number,
  ): BitmapLabel {
    const lab = makePixelText(text, 10, color);
    lab.scale.set(TEXT_SCALE);
    lab.position.set(x, y);
    lab.eventMode = "none";
    this.addChild(lab);
    this.labels.push(lab);
    return lab;
  }

  private redraw(): void {
    this.clearLabels();
    clearPointerHits(this.rowHits);
    this.box.clear();
    this.position.x = TRACKER_X0 + this.partyInset;

    if (!this.rows.length) return;

    const maxW = Math.min(
      TRACKER_MAX_W,
      LCD_WIDTH - TRACKER_X0 - this.partyInset - 48,
    );
    const textX = PAD_X + ACCENT_W + GAP_AFTER_ACCENT;
    const textW = maxW - textX - PAD_X;
    const blockH = LINE_H * 2;
    const contentH =
      PAD_Y * 2 +
      this.rows.length * blockH +
      Math.max(0, this.rows.length - 1) * (TASK_GAP + DIVIDER_H);

    this.box
      .roundRect(0, 0, maxW, contentH, 3)
      .fill({ color: 0x061410, alpha: 0.78 })
      .stroke({ width: 1, color: CBE.borderDim, alpha: 0.7 });
    // Soft inner top edge.
    this.box
      .roundRect(1, 1, maxW - 2, contentH - 2, 2)
      .stroke({ width: 1, color: 0x1a3028, alpha: 0.55 });

    let y = PAD_Y;
    for (let i = 0; i < this.rows.length; i++) {
      const row = this.rows[i]!;
      const accentColor =
        row.progressLabel === "可提交" ? CBE.inkTipGreen : CBE.gold;

      this.box
        .rect(PAD_X, y + 1, ACCENT_W, blockH - 2)
        .fill({ color: accentColor, alpha: 0.9 });

      const progColor =
        row.progressLabel === "可提交" ? CBE.inkTipGreen : CBE.inkLabel;
      const progW =
        measureUiTextWidth(row.progressLabel, UC3_CELL) * TEXT_SCALE;
      const nameBudget = Math.max(12, textW - progW - 4);
      const nameText = truncateToWidth(row.name, nameBudget);
      this.addLine(nameText, textX, y, CBE.cream);
      this.addLine(
        row.progressLabel,
        textX + textW - progW,
        y,
        progColor,
      );

      const npcBit = row.receiver ? truncateToWidth(row.receiver, textW * 0.45) : "—";
      const sepW = measureUiTextWidth(" ", UC3_CELL) * TEXT_SCALE;
      const sceneBudget =
        textW - measureUiTextWidth(npcBit, UC3_CELL) * TEXT_SCALE - sepW;
      const sceneBit = row.sceneLabel
        ? truncateToWidth(row.sceneLabel, Math.max(10, sceneBudget))
        : "";
      const sub = sceneBit ? `${npcBit} ${sceneBit}` : npcBit;
      this.addLine(truncateToWidth(sub, textW), textX, y + LINE_H, CBE.inkMint);

      y += blockH;
      if (i < this.rows.length - 1) {
        y += Math.floor(TASK_GAP / 2);
        this.box
          .rect(textX, y, textW, DIVIDER_H)
          .fill({ color: CBE.borderDim, alpha: 0.35 });
        y += DIVIDER_H + Math.ceil(TASK_GAP / 2);
      }
    }

    this.rowHits.push(
      attachRowPointerHit(this, 0, 0, maxW, contentH, () => this.onOpenTasks?.()),
    );
  }
}

function truncateToWidth(s: string, maxPx: number): string {
  if (measureUiTextWidth(s, UC3_CELL) * TEXT_SCALE <= maxPx) return s;
  const chars = [...s];
  let out = "";
  for (let i = 0; i < chars.length; i++) {
    const trial = out + chars[i]! + "…";
    if (measureUiTextWidth(trial, UC3_CELL) * TEXT_SCALE > maxPx) break;
    out += chars[i]!;
  }
  return out ? `${out}…` : "…";
}
