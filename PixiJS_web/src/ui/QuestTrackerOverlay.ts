/**
 * Left-side accepted-quest tracker under the map status strip.
 * Collapse toggle above the task plate — independent from 修/拍/… strip.
 */
import { Container, Graphics } from "pixi.js";
import { CBE, LCD_WIDTH, makePixelText } from "./constants";
import { BitmapLabel, UC3_CELL, measureUiTextWidth } from "./BitmapLabel";
import { sceneDisplayName } from "./WorldHud";
import type { TaskListRow } from "../net/protocolExtras";
import type { RingStatus } from "../net/protocolActivities";
import { NPC_RING_QUEST } from "../net/protocolActivities";
import { lookupNpcSceneFromDb } from "../resources/npcScenes";
import { replaceGraphics } from "../render/gpuResume";
import {
  isQuestTrackerCollapsed,
  toggleQuestTrackerCollapsed,
} from "../resources/questTrackerPref";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

/** Mirror server VM_MOCK_RING_TYPE_* / RingQuestPanel. */
const RING_TYPE_VISIT = 1;
const RING_TYPE_KILL_MOB = 2;
const RING_TYPE_KILL_BOSS = 3;
const RING_TYPE_KILL_PLAYER = 4;
const RING_TYPE_COLLECT = 5;
const RING_TYPE_SUBMIT_MED = 6;
const RING_TYPE_SUBMIT_EQUIP = 7;
const RING_TYPE_WORLD_TRUTH = 8;

/**
 * Map-local coords (parent = SceneView.root, already at WORLD_TOP_H).
 * Clear status strip (修/拍/… @ y=4, 17px) — same band as WorldHud PARTY_Y0.
 * When party strip is visible, shift down by mateCount * PARTY_SLOT_H (not right).
 * Toggle sits above the task plate.
 */
const STRIP_BOTTOM = 4 + 17;
const TRACKER_Y0 = STRIP_BOTTOM + 2;
const TRACKER_X0 = 4;
const TRACKER_MAX_W = 118;
const MAX_TASKS = 4;
const TOGGLE_SIZE = 17;
const TOGGLE_GAP = 3;
/** Panel fill / stroke — keep readable but less occlusive on the map. */
const PANEL_FILL_ALPHA = 0.3;
const PANEL_STROKE_ALPHA = 0.45;
const PANEL_INNER_ALPHA = 0.32;

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

/** Compact right-side progress for the map tracker. */
export function formatRingQuestProgress(st: RingStatus): string {
  const target = st.targetCount || 1;
  switch (st.taskType) {
    case RING_TYPE_VISIT:
      return st.progress >= 1 ? "可提交" : "未对话";
    case RING_TYPE_KILL_MOB:
    case RING_TYPE_KILL_BOSS:
      return st.progress >= target ? "可提交" : `${st.progress}/${target}场`;
    case RING_TYPE_KILL_PLAYER:
      return st.progress >= target ? "可提交" : `${st.progress}/${target}名`;
    case RING_TYPE_COLLECT:
      return `需${target}个`;
    case RING_TYPE_SUBMIT_MED:
      return `需${target}组`;
    case RING_TYPE_SUBMIT_EQUIP:
      return `需${target}件`;
    case RING_TYPE_WORLD_TRUTH:
      return st.progress >= 1 ? "可提交" : "未发送";
    default:
      return st.progress >= target ? "可提交" : `${st.progress}/${target}`;
  }
}

function ringDescParenLabel(desc: string): string {
  const m = /[（(]([^）)]+)[）)]/.exec(desc);
  if (!m) return "";
  const inner = (m[1] || "").trim();
  // Visit desc uses （对话即可完成）— not a map label.
  if (!inner || inner.includes("对话")) return "";
  return inner;
}

function ringShortTitle(st: RingStatus): string {
  const desc = (st.desc || "").trim();
  if (!desc) return "跑环";
  // Drop leading 击杀N场 / 收集N个 / 提交N组 so the name fits the strip.
  const trimmed = desc
    .replace(/^世界真心话[：:]/, "")
    .replace(/^击杀\d+[场名]/, "")
    .replace(/^收集\d+个/, "")
    .replace(/^提交\d+组/, "")
    .replace(/^提交\d+件/, "")
    .replace(/^拜访/, "")
    .replace(/[（(][^）)]*[）)]$/, "")
    .replace(/，.+$/, "")
    .trim();
  if (!trimmed) return "跑环";
  return trimmed.length > 6 ? `跑环·${trimmed.slice(0, 5)}` : `跑环·${trimmed}`;
}

/**
 * One tracker row for the active daily ring quest, or null if idle.
 */
export function buildRingQuestTrackerRow(
  ring: RingStatus | null | undefined,
  opts?: {
    npcSceneByName?: ReadonlyMap<string, string>;
  },
): QuestTrackerRow | null {
  if (!ring?.taskActive) return null;
  const progressLabel = formatRingQuestProgress(ring);
  let receiver = "跑环使";
  let sceneLabel = "";
  if (ring.taskType === RING_TYPE_VISIT) {
    receiver = (ring.targetNpc || "").trim() || "NPC";
  } else if (
    ring.taskType === RING_TYPE_KILL_MOB ||
    ring.taskType === RING_TYPE_KILL_BOSS
  ) {
    const map = ringDescParenLabel(ring.desc || "");
    receiver = map || "击杀";
    sceneLabel = "";
  } else if (ring.taskType === RING_TYPE_KILL_PLAYER) {
    receiver = "切磋";
  }
  if (!sceneLabel) {
    const sceneKey = receiver
      ? opts?.npcSceneByName?.get(receiver) ||
        lookupNpcSceneFromDb(receiver) ||
        ""
      : opts?.npcSceneByName?.get("跑环使") ||
        lookupNpcSceneFromDb("跑环使") ||
        "";
    if (sceneKey) sceneLabel = sceneDisplayName(sceneKey);
  }
  return {
    taskId: NPC_RING_QUEST >>> 0,
    name: ringShortTitle(ring),
    receiver,
    sceneLabel,
    progressLabel,
  };
}

/**
 * Build tracker rows: active ring (if any) + accepted quests.
 * Submit NPC + scene from server_dynamic_npcs.
 */
export function buildQuestTrackerRows(
  accepted: TaskListRow[],
  opts?: {
    /** Optional override map; default uses loadNpcSceneIndex cache. */
    npcSceneByName?: ReadonlyMap<string, string>;
    /** Active daily ring quest (WT47); shown as first row when taskActive. */
    ring?: RingStatus | null;
  },
): QuestTrackerRow[] {
  const out: QuestTrackerRow[] = [];
  const ringRow = buildRingQuestTrackerRow(opts?.ring, opts);
  if (ringRow) out.push(ringRow);
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
  /** Tap tracker body → open full task list. */
  onOpenTasks: (() => void) | null = null;

  private toggle = new Container();
  private content = new Container();
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private rows: QuestTrackerRow[] = [];
  private collapsed = isQuestTrackerCollapsed();
  private worldActive = false;
  /** Extra map-local Y under party mate slots (0 when solo). */
  private partyOffsetY = 0;
  /** Extra map-local Y under 比武计时条 (0 when ArenaHud hidden). */
  private arenaOffsetY = 0;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    // Same band as status strip badges (SceneView.root).
    this.zIndex = 20;
    this.sortableChildren = true;

    this.toggle.zIndex = 2;
    this.toggle.eventMode = "static";
    this.toggle.cursor = "pointer";
    this.toggle.hitArea = {
      contains: (px: number, py: number) =>
        px >= 0 && py >= 0 && px <= TOGGLE_SIZE && py <= TOGGLE_SIZE,
    };
    this.toggle.on("pointertap", (e) => {
      e.stopPropagation();
      this.collapsed = toggleQuestTrackerCollapsed();
      this.mountToggleArt();
      this.applyVisibility();
    });

    this.content.zIndex = 1;
    this.content.eventMode = "passive";
    this.content.addChild(this.box);

    this.addChild(this.content, this.toggle);
    this.position.set(TRACKER_X0, TRACKER_Y0);
    this.mountToggleArt();
  }

  /** Only show on the interactive world map band. */
  setWorldActive(on: boolean): void {
    this.worldActive = !!on;
    this.applyVisibility();
  }

  /**
   * Sit below the world party strip so mate portraits/HP do not cover the tracker.
   * Pass WorldHud.partyOverlayOffsetY() (mateCount * slot height).
   */
  setPartyOffsetY(px: number): void {
    const next = Math.max(0, px | 0);
    if (next === this.partyOffsetY) return;
    this.partyOffsetY = next;
    this.relayoutY();
  }

  /**
   * Sit below ArenaHud 比武计时 when the strip is visible.
   * Pass ArenaHud.overlayOffsetY().
   */
  setArenaOffsetY(px: number): void {
    const next = Math.max(0, px | 0);
    if (next === this.arenaOffsetY) return;
    this.arenaOffsetY = next;
    this.relayoutY();
  }

  private relayoutY(): void {
    this.position.y = TRACKER_Y0 + this.partyOffsetY + this.arenaOffsetY;
  }

  setTasks(rows: QuestTrackerRow[]): void {
    this.rows = rows.slice(0, MAX_TASKS);
    this.redraw();
    this.applyVisibility();
  }

  private applyVisibility(): void {
    const hasTasks = this.rows.length > 0;
    const show = this.worldActive && hasTasks;
    this.visible = show;
    this.toggle.visible = show;
    this.content.visible = show && !this.collapsed;
  }

  private mountToggleArt(): void {
    this.toggle.removeChildren().forEach((c) => {
      c.destroy({ children: true });
    });
    // pointRight = expand affordance when collapsed
    const pointRight = this.collapsed;
    const fill = pointRight ? 0x182818 : 0x281818;
    const border = pointRight ? 0xb8e8b0 : 0xe8c8b0;
    const ink = pointRight ? 0xc8f0c0 : 0xf0d8c0;
    const plate = new Graphics()
      .rect(0, 0, TOGGLE_SIZE, TOGGLE_SIZE)
      .fill({ color: fill })
      .stroke({ width: 1, color: border });
    this.toggle.addChild(plate);

    const g = new Graphics();
    const cx = TOGGLE_SIZE / 2;
    const cy = TOGGLE_SIZE / 2;
    const drawChevron = (ox: number) => {
      if (pointRight) {
        g.moveTo(cx + ox - 2.5, cy - 4.5)
          .lineTo(cx + ox + 1.5, cy)
          .lineTo(cx + ox - 2.5, cy + 4.5);
      } else {
        g.moveTo(cx + ox + 2.5, cy - 4.5)
          .lineTo(cx + ox - 1.5, cy)
          .lineTo(cx + ox + 2.5, cy + 4.5);
      }
    };
    drawChevron(pointRight ? -1.5 : 1.5);
    drawChevron(pointRight ? 1.5 : -1.5);
    g.stroke({ width: 1.5, color: ink, join: "miter", cap: "square" });
    this.toggle.addChild(g);
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
    this.content.addChild(lab);
    this.labels.push(lab);
    return lab;
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
    this.position.x = TRACKER_X0;
    this.position.y = TRACKER_Y0 + this.partyOffsetY;
    this.toggle.position.set(0, 0);
    /* Task plate below the hide/show toggle. */
    this.content.position.set(0, TOGGLE_SIZE + TOGGLE_GAP);

    if (!this.rows.length) return;

    const maxW = Math.min(
      TRACKER_MAX_W,
      LCD_WIDTH - TRACKER_X0 - 48,
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
      .fill({ color: 0x061410, alpha: PANEL_FILL_ALPHA })
      .stroke({ width: 1, color: CBE.borderDim, alpha: PANEL_STROKE_ALPHA });
    // Soft inner top edge.
    this.box
      .roundRect(1, 1, maxW - 2, contentH - 2, 2)
      .stroke({ width: 1, color: 0x1a3028, alpha: PANEL_INNER_ALPHA });

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

      const npcBit = row.receiver
        ? truncateToWidth(row.receiver, textW * 0.45)
        : "—";
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
      attachRowPointerHit(this.content, 0, 0, maxW, contentH, () =>
        this.onOpenTasks?.(),
      ),
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
