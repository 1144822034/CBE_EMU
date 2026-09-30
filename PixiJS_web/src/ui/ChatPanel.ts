/**
 * Channel chat panel — tabs match CBE overview (世界/派/本/伍/统).
 * ←→ switch channel; ↑↓ scroll; compose bar + ChatIme (no window.prompt).
 * 「本」shows local(4)+private(7); with pmTarget, sends 1/3/2 whisper.
 */
import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  paintCbePanel,
  cbePopupBounds,
  blockPanelPointerPassthrough,
} from "./constants";
import { BitmapLabel, UC3_CELL, measureUiTextWidth, wrapUiText } from "./BitmapLabel";
import { ChatIme } from "./ChatIme";
import { replaceGraphics } from "../render/gpuResume";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import { ARENA_TEXT_TITLE_COLORS } from "./designation";

/** Wire 1/3/3 type → display. */
export type ChatEntry = {
  type: number;
  text: string;
  /** Sender role / nearby actor id for starting private chat; 0 = none. */
  senderRoleId?: number;
  senderName?: string;
  /** Optional whole-line override (rare). */
  color?: number;
  /** Arena kill-streak: color only this title substring (survives wrap). */
  streakTitle?: string;
  streakColor?: number;
};

export type ChatPmTarget = { id: number; name: string };

type ChannelDef = {
  /** Short tab glyph (screenshot). */
  tab: string;
  /** Send 1/3/1 type; -1 = read-only. */
  sendType: number;
  /** Downlink types shown in this tab. */
  types: number[];
  color: number;
};

/** Screenshot order: 世界 派 本 伍 统 */
const CHANNELS: ChannelDef[] = [
  { tab: "世界", sendType: 0, types: [0], color: 0xffde00 },
  { tab: "派", sendType: 3, types: [3], color: 0xffa040 },
  { tab: "本", sendType: 4, types: [4, 7], color: 0xd0e8d5 },
  { tab: "伍", sendType: 2, types: [2], color: 0x40e0ff },
  { tab: "统", sendType: -1, types: [5, 6], color: 0x29ff2e },
];

const LOCAL_CHANNEL_IDX = 2;
/** 「统」系统频道 — 打开聊天默认落此标签. */
const SYSTEM_CHANNEL_IDX = 4;
const MSG_PAGE = 12;
/** Same display size as 高清/宋体（{@link UC3_CELL}）；tabs / soft hints already use it. */
const CHAT_MSG_FONT_SIZE = UC3_CELL;
const CHAT_MSG_LINE_H = UC3_CELL;
/** Wrap budget ≈ panel inner width (LCD − side padding). */
const CHAT_MSG_WRAP_PX = LCD_WIDTH - 20;
const COMPOSE_ROWS = 2;
const COMPOSE_PAD_Y = 4;
const COMPOSE_BAR_H = COMPOSE_ROWS * CHAT_MSG_FONT_SIZE + COMPOSE_PAD_Y * 2;
/** Gap under popup bottom edge above compose bar (hint sits in this band). */
const COMPOSE_BOTTOM_GAP = 6;
const COMPOSE_HINT_LIFT = 8;

export function chatPrefixForType(type: number): string {
  switch (type) {
    case 0:
      return "世";
    case 2:
      return "队";
    case 3:
      return "帮";
    case 4:
      return "本";
    case 5:
    case 6:
      return "系";
    case 7:
      return "私";
    default:
      return "?";
  }
}

export function inferChatType(text: string): number {
  if (text.startsWith("[世]")) return 0;
  if (text.startsWith("[队]")) return 2;
  if (text.startsWith("[帮]")) return 3;
  if (text.startsWith("[本]") || text.startsWith("[旁]")) return 4;
  if (text.startsWith("[系]") || text.startsWith("[挂]")) return 5;
  if (text.startsWith("[私]")) return 7;
  return 5;
}

/** Private (type 7) — purple, distinct from local mint on the same tab. */
const CHAT_PRIVATE_COLOR = 0xc080ff;

export function colorForChatType(type: number): number {
  if (type === 7) return CHAT_PRIVATE_COLOR;
  for (const ch of CHANNELS) {
    if (ch.types.includes(type)) return ch.color;
  }
  return CBE.inkMint;
}

/**
 * 比武连杀：仅「称号」着色；与头顶文字称号同色表（designation.ts）。
 * Wire: `{玩家} 比武 {阶段} 连胜N人,已经{称号} !`
 * 服务端：本场 LIVE 每档仅全场首个达成者播报并解锁。
 */
const ARENA_KILL_STREAK_COLORS = ARENA_TEXT_TITLE_COLORS;

export function findArenaKillStreakTitle(
  text: string,
): { title: string; color: number; index: number } | null {
  const raw = text || "";
  if (!raw.includes("比武") || !raw.includes("连胜") || !raw.includes("已经"))
    return null;
  for (let i = ARENA_KILL_STREAK_COLORS.length - 1; i >= 0; i--) {
    const row = ARENA_KILL_STREAK_COLORS[i]!;
    const index = raw.indexOf(row.title);
    if (index >= 0) return { title: row.title, color: row.color, index };
  }
  return null;
}

/** True when line is a kill-streak announce (for tip / detection). */
export function arenaKillStreakColor(text: string): number | null {
  return findArenaKillStreakTitle(text)?.color ?? null;
}

/** Split one display line so only the streak title uses its color. */
export function chatLineColorRuns(
  text: string,
  baseColor: number,
  entry?: ChatEntry,
): { text: string; color: number }[] {
  let title = entry?.streakTitle ?? "";
  let color = entry?.streakColor;
  let index = title ? text.indexOf(title) : -1;
  if (index < 0 || !title || color == null) {
    const hit = findArenaKillStreakTitle(text);
    if (!hit) return [{ text, color: baseColor }];
    title = hit.title;
    color = hit.color;
    index = hit.index;
  }
  if (index < 0) return [{ text, color: baseColor }];
  const out: { text: string; color: number }[] = [];
  if (index > 0) out.push({ text: text.slice(0, index), color: baseColor });
  out.push({ text: title, color });
  const after = text.slice(index + title.length);
  if (after) out.push({ text: after, color: baseColor });
  return out;
}

export function colorForChatEntry(entry: ChatEntry): number {
  if (entry.color != null) return entry.color;
  return colorForChatType(entry.type);
}

export class ChatPanel extends Container {
  private shade = new Graphics();
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private entries: ChatEntry[] = [];
  private channelIdx = SYSTEM_CHANNEL_IDX;
  private scroll = 0;
  private draft = "";
  private ime = new ChatIme();
  private inputHit = new Graphics();
  private toast: PanelTipState = createPanelTipState();
  /** Bumped on open/hide so deferred focus cannot remount IME after close. */
  private openGen = 0;
  /** Ignore re-entry while onSend is in flight (Enter IME + window handleOk). */
  private sendBusy = false;
  private focusTimer: ReturnType<typeof setTimeout> | null = null;
  private pmTarget: ChatPmTarget | null = null;
  /** Skip starting PM to self when tapping own lines. */
  selfRoleId = 0;

  /**
   * Return false to keep draft (e.g. missing 小喇叭 tip).
   * type 7 + opts.sendTo = private whisper (1/3/2).
   */
  onSend:
    | ((
        text: string,
        type: number,
        opts?: { sendTo?: number },
      ) => boolean | void | Promise<boolean | void>)
    | null = null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    blockPanelPointerPassthrough(this);
    this.paintShade();
    this.addChild(this.shade);
    this.addChild(this.box);
    this.wireInputHit();
    this.addChild(this.inputHit);

    this.ime.onChange = (v) => {
      this.draft = v;
      this.redraw();
    };
    this.ime.onSubmit = () => this.confirm();
  }

  /**
   * webglcontextrestored: UC3 labels return via refreshAllUc3Labels, but
   * panel Graphics (shade / chrome / input hit) stay blank — same contract as
   * WorldHud / BattleScreen. Softkeys can still say 发送/返回 while messages
   * float over the map with no panel fill.
   * Remount even while hidden so the next open() is not blank.
   */
  nudgeAfterGpuResume(): void {
    this.shade = replaceGraphics(this.shade);
    this.paintShade();
    this.box = replaceGraphics(this.box);
    this.inputHit = replaceGraphics(this.inputHit);
    this.wireInputHit();
    if (this.toast.gfx.parent) {
      this.toast.gfx = replaceGraphics(this.toast.gfx);
    }
    if (!this.visible) return;
    this.redraw();
    this.ime.show(this.inputRect());
    this.ime.blur();
  }

  private paintShade(): void {
    this.shade
      .clear()
      .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
      .fill({ color: 0x000000, alpha: 0.4 });
  }

  private wireInputHit(): void {
    this.inputHit.eventMode = "static";
    this.inputHit.cursor = "text";
    this.inputHit.removeAllListeners();
    this.inputHit.on("pointertap", () => this.focusInput());
  }

  open(
    entries: ChatEntry[],
    opts?: { pm?: ChatPmTarget | null; keepChannel?: boolean },
  ): void {
    this.openGen += 1;
    this.sendBusy = false;
    this.clearFocusTimer();
    this.entries = entries.slice();
    if (!opts?.keepChannel) this.channelIdx = SYSTEM_CHANNEL_IDX;
    this.scroll = 0;
    this.draft = "";
    if (opts && "pm" in opts) {
      this.pmTarget =
        opts.pm && opts.pm.id > 0
          ? { id: opts.pm.id >>> 0, name: opts.pm.name || "玩家" }
          : null;
      if (this.pmTarget) this.channelIdx = LOCAL_CHANNEL_IDX;
    } else {
      this.pmTarget = null;
    }
    this.visible = true;
    // Open-after-recover: remount shade/box before paint (hidden panels skip nudge redraw).
    this.shade = replaceGraphics(this.shade);
    this.paintShade();
    this.box = replaceGraphics(this.box);
    this.inputHit = replaceGraphics(this.inputHit);
    this.wireInputHit();
    this.redraw();
    this.ime.clear();
    // Mount inert overlay for layout only — keyboard opens when user taps input.
    this.ime.show(this.inputRect());
    this.ime.blur();
  }

  /** Enter private compose against a player (friend / world line / nearby). */
  beginPrivateChat(id: number, name: string): void {
    const sendTo = id >>> 0;
    if (!sendTo) return;
    if (this.selfRoleId > 0 && sendTo === (this.selfRoleId >>> 0)) return;
    this.pmTarget = { id: sendTo, name: (name || "玩家").trim() || "玩家" };
    this.channelIdx = LOCAL_CHANNEL_IDX;
    this.scroll = 0;
    if (this.visible) {
      this.redraw();
      this.focusInput();
    }
  }

  clearPrivateTarget(): void {
    this.pmTarget = null;
    if (this.visible) this.redraw();
  }

  getPrivateTarget(): ChatPmTarget | null {
    return this.pmTarget;
  }

  setEntries(entries: ChatEntry[]): void {
    this.entries = entries.slice();
    this.clampScroll();
    if (this.visible) this.redraw();
  }

  hide(): void {
    this.openGen += 1;
    this.sendBusy = false;
    this.clearFocusTimer();
    clearPanelTip(this.toast);
    this.visible = false;
    this.ime.hide();
    for (const t of this.labels) t.destroy();
    this.labels = [];
  }

  /** Same contract as TitleScreen.releaseIme — drop HTML overlay when leaving chat. */
  releaseIme(): void {
    this.hide();
  }

  destroy(options?: boolean | { children?: boolean; texture?: boolean }): void {
    this.openGen += 1;
    this.clearFocusTimer();
    clearPanelTip(this.toast);
    this.ime.destroy();
    super.destroy(options);
  }

  private clearFocusTimer(): void {
    if (this.focusTimer) {
      clearTimeout(this.focusTimer);
      this.focusTimer = null;
    }
  }

  currentSendType(): number {
    if (this.pmTarget && this.channelIdx === LOCAL_CHANNEL_IDX) return 7;
    return CHANNELS[this.channelIdx]!.sendType;
  }

  cycleChannel(delta: number): void {
    if (!this.visible) return;
    const n = CHANNELS.length;
    this.channelIdx = (this.channelIdx + delta + n) % n;
    this.scroll = 0;
    this.redraw();
    if (this.visible) this.ime.show(this.inputRect());
    else this.ime.hide();
  }

  move(delta: number): void {
    if (!this.visible) return;
    this.scroll = Math.max(0, this.scroll + delta);
    this.clampScroll();
    this.redraw();
  }

  /** Green center toast — same style as SystemPanel mall/bag tips. */
  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (this.visible) this.redraw();
    }, ms);
  }

  /** True while the HTML IME owns focus (Enter already submits via ChatIme). */
  isImeFocused(): boolean {
    return this.visible && this.ime.isFocused();
  }

  confirm(): void {
    if (!this.visible || this.sendBusy) return;
    const ch = CHANNELS[this.channelIdx]!;
    if (ch.sendType < 0) {
      this.focusInput();
      return;
    }
    const text = this.draft.trim() || this.ime.value.trim();
    if (!text) {
      this.focusInput();
      return;
    }
    const whisper =
      this.pmTarget && this.channelIdx === LOCAL_CHANNEL_IDX
        ? this.pmTarget
        : null;
    const sendType = whisper ? 7 : ch.sendType;
    const gen = this.openGen;
    this.sendBusy = true;
    void Promise.resolve(
      this.onSend?.(
        text,
        sendType,
        whisper ? { sendTo: whisper.id } : undefined,
      ),
    )
      .then((ok) => {
        if (!this.visible || this.openGen !== gen) return;
        if (ok === false) {
          this.focusInput();
          return;
        }
        this.draft = "";
        this.ime.clear();
        this.redraw();
        this.focusInput();
      })
      .finally(() => {
        this.sendBusy = false;
      });
  }

  cancel(): void {
    if (!this.visible) return;
    if (tryDismissPanelTipOnSoftRight(this.toast)) return;
    this.hide();
    this.onClose?.();
  }

  focusInput(): void {
    if (!this.visible) return;
    if (CHANNELS[this.channelIdx]!.sendType < 0) {
      this.ime.blur();
      return;
    }
    this.ime.show(this.inputRect());
    this.ime.focus(this.draft);
  }

  private filtered(): ChatEntry[] {
    const types = new Set(CHANNELS[this.channelIdx]!.types);
    return this.entries.filter((e) => types.has(e.type));
  }

  private clampScroll(): void {
    const n = this.buildDisplayLines().length;
    const maxScroll = Math.max(0, n - MSG_PAGE);
    this.scroll = Math.min(Math.max(0, this.scroll), maxScroll);
  }

  private inputRect(): { x: number; y: number; w: number; h: number } {
    const { x, y, w, h } = cbePopupBounds();
    const barY = y + h - (COMPOSE_BAR_H + COMPOSE_BOTTOM_GAP);
    return {
      x: x + 40,
      y: barY + COMPOSE_PAD_Y,
      w: w - 50,
      h: COMPOSE_ROWS * CHAT_MSG_FONT_SIZE,
    };
  }

  private canWhisperEntry(entry: ChatEntry): boolean {
    const id = (entry.senderRoleId ?? 0) >>> 0;
    if (!id) return false;
    if (this.selfRoleId > 0 && id === (this.selfRoleId >>> 0)) return false;
    // System / hang tips are not player lines even if id sneaks in.
    if (entry.type === 5 || entry.type === 6) return false;
    return true;
  }

  private redraw(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.box.clear();

    const { x, y, w, h } = cbePopupBounds();
    paintCbePanel(this, this.box, x, y, w, h, 0x0a1812);

    // Tab strip
    const tabY = y + 8;
    const tabH = 18;
    let tabX = x + 8;
    this.addLabel("<", tabX, tabY + 2, 0x7aef9a);
    this.rowHits.push(
      attachRowPointerHit(this, tabX - 2, tabY, 16, tabH, () => this.cycleChannel(-1)),
    );
    tabX += 14;
    CHANNELS.forEach((ch, i) => {
      const lab = ch.tab;
      const tw = lab.length > 1 ? 36 : 22;
      const hi = i === this.channelIdx;
      if (hi) {
        this.box
          .rect(tabX - 2, tabY, tw + 4, tabH)
          .fill({ color: 0x3a4a42 })
          .stroke({ width: 1, color: CBE.borderDim });
      }
      this.addLabel(lab, tabX + 2, tabY + 2, hi ? CBE.ink : CBE.inkLabel);
      const idx = i;
      const hitX = tabX - 2;
      this.rowHits.push(
        attachRowPointerHit(this, hitX, tabY, tw + 4, tabH, () => {
          if (idx === this.channelIdx) return;
          this.channelIdx = idx;
          this.scroll = 0;
          this.redraw();
          if (this.visible) this.ime.show(this.inputRect());
        }),
      );
      tabX += tw + 4;
    });
    this.addLabel(">", tabX + 2, tabY + 2, 0x7aef9a);
    this.rowHits.push(
      attachRowPointerHit(this, tabX, tabY, 16, tabH, () => this.cycleChannel(1)),
    );

    this.box
      .moveTo(x + 6, tabY + tabH + 4)
      .lineTo(x + w - 6, tabY + tabH + 4)
      .stroke({ width: 1, color: 0x1a3028 });

    // Messages → explicit wrap lines (avoid Text wordWrap stacking in 16px rows)
    const msgTop = tabY + tabH + 10;
    const displayLines = this.buildDisplayLines();
    const maxScroll = Math.max(0, displayLines.length - MSG_PAGE);
    this.scroll = Math.min(this.scroll, maxScroll);
    const end = displayLines.length - this.scroll;
    const start = Math.max(0, end - MSG_PAGE);
    const shown = displayLines.slice(start, end);
    shown.forEach((row, i) => {
      const ly = msgTop + i * CHAT_MSG_LINE_H;
      let lx = x + 10;
      const runs = chatLineColorRuns(row.text, row.color, row.entry);
      for (const run of runs) {
        if (!run.text) continue;
        this.addLabel(run.text, lx, ly, run.color, {
          fontSize: CHAT_MSG_FONT_SIZE,
        });
        lx += measureUiTextWidth(run.text, CHAT_MSG_FONT_SIZE);
      }
      if (this.canWhisperEntry(row.entry)) {
        const entry = row.entry;
        this.rowHits.push(
          attachRowPointerHit(this, x + 8, ly, w - 16, CHAT_MSG_LINE_H, () => {
            const sid = (entry.senderRoleId ?? 0) >>> 0;
            const sname =
              (entry.senderName || "").trim() ||
              entry.text.replace(/^\[[^\]]+\]/, "").split(":")[0]?.trim() ||
              "玩家";
            this.beginPrivateChat(sid, sname);
            this.showTip(`私聊 ${sname}`);
          }),
        );
      }
    });
    if (!shown.length) {
      this.addLabel("(暂无消息)", x + 10, msgTop + 8, CBE.inkLabel, {
        fontSize: CHAT_MSG_FONT_SIZE,
      });
    }

    // Compose bar (2 rows × UC3_CELL)
    const ch = CHANNELS[this.channelIdx]!;
    const whisper =
      this.pmTarget && this.channelIdx === LOCAL_CHANNEL_IDX
        ? this.pmTarget
        : null;
    const barY = y + h - (COMPOSE_BAR_H + COMPOSE_BOTTOM_GAP);
    this.box
      .roundRect(x + 6, barY, w - 12, COMPOSE_BAR_H, 2)
      .fill({ color: 0x12241c })
      .stroke({ width: 1, color: CBE.borderDim });
    const tag =
      ch.sendType < 0
        ? "[统]"
        : whisper
          ? "[私]"
          : `[${chatPrefixForType(ch.sendType)}]`;
    this.addLabel(tag, x + 12, barY + COMPOSE_PAD_Y, whisper ? CHAT_PRIVATE_COLOR : ch.color, {
      fontSize: CHAT_MSG_FONT_SIZE,
    });
    let placeholder: string;
    if (ch.sendType < 0) placeholder = "系统频道不可发送";
    else if (whisper) {
      const nick = whisper.name.slice(0, 6);
      placeholder = this.draft || `对${nick}…`;
    } else {
      placeholder = this.draft || "点击输入…";
    }
    const draftMaxW = w - 56;
    const draftLines = wrapUiText(
      placeholder,
      draftMaxW,
      CHAT_MSG_FONT_SIZE,
    ).slice(0, COMPOSE_ROWS);
    draftLines.forEach((line, i) => {
      this.addLabel(
        line,
        x + 44,
        barY + COMPOSE_PAD_Y + i * CHAT_MSG_FONT_SIZE,
        this.draft ? CBE.ink : CBE.inkLabel,
        { fontSize: CHAT_MSG_FONT_SIZE },
      );
    });

    this.inputHit.clear();
    if (ch.sendType >= 0) {
      this.inputHit
        .rect(x + 40, barY, w - 50, COMPOSE_BAR_H)
        .fill({ color: 0xffffff, alpha: 0.001 });
    }

    const hint =
      ch.sendType < 0
        ? "←→频道  ↑↓翻阅  返回关闭"
        : whisper
          ? `私聊:${whisper.name.slice(0, 8)}  点消息可选人`
          : "←→频道  ↑↓翻阅  点消息可私聊";
    this.addLabelCentered(
      hint,
      x + w / 2,
      barY - COMPOSE_HINT_LIFT,
      CBE.inkLabel,
    );

    drawPanelTipOverlay(this, this.toast);
  }

  /** One painted row per wrapped line (preserves \n and width wrap). */
  private buildDisplayLines(): {
    text: string;
    color: number;
    entry: ChatEntry;
  }[] {
    const out: { text: string; color: number; entry: ChatEntry }[] = [];
    for (const entry of this.filtered()) {
      const color = colorForChatEntry(entry);
      const paragraphs = entry.text.split(/\n/);
      for (const para of paragraphs) {
        const wrapped = wrapUiText(
          para.length ? para : " ",
          CHAT_MSG_WRAP_PX,
          CHAT_MSG_FONT_SIZE,
        );
        for (const line of wrapped) {
          out.push({ text: line, color, entry });
        }
      }
    }
    return out;
  }

  private addLabel(
    text: string,
    x: number,
    y: number,
    color: number,
    opts?: { maxWidth?: number; fontSize?: number },
  ): void {
    const lab = new BitmapLabel();
    lab.position.set(x, y);
    void lab.setText(text, color, {
      smooth: false,
      outline: false,
      fontSize: opts?.fontSize ?? UC3_CELL,
      maxWidth: opts?.maxWidth,
      // Compose bar may clip; list rows are pre-wrapped — never stack in-label.
      wordWrap: false,
    });
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
    void lab.setText(text, color, {
      smooth: false,
      outline: false,
      fontSize: UC3_CELL,
      wordWrap: false,
    });
    this.addChild(lab);
    this.labels.push(lab);
  }
}
