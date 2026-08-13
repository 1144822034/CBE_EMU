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
  drawCbePanel,
} from "./constants";
import { BitmapLabel, UC3_CELL, wrapUiText } from "./BitmapLabel";
import { ChatIme } from "./ChatIme";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

/** Wire 1/3/3 type → display. */
export type ChatEntry = {
  type: number;
  text: string;
  /** Sender role / nearby actor id for starting private chat; 0 = none. */
  senderRoleId?: number;
  senderName?: string;
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
const MSG_PAGE = 12;

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

export class ChatPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private entries: ChatEntry[] = [];
  private channelIdx = 0;
  private scroll = 0;
  private draft = "";
  private ime = new ChatIme();
  private inputHit = new Graphics();
  private toast: PanelTipState = createPanelTipState();
  /** Bumped on open/hide so deferred focus cannot remount IME after close. */
  private openGen = 0;
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
    this.addChild(
      new Graphics()
        .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
        .fill({ color: 0x000000, alpha: 0.4 }),
    );
    this.addChild(this.box);
    this.inputHit.eventMode = "static";
    this.inputHit.cursor = "text";
    this.inputHit.on("pointertap", () => this.focusInput());
    this.addChild(this.inputHit);

    this.ime.onChange = (v) => {
      this.draft = v;
      this.redraw();
    };
    this.ime.onSubmit = () => this.confirm();
  }

  open(
    entries: ChatEntry[],
    opts?: { pm?: ChatPmTarget | null; keepChannel?: boolean },
  ): void {
    this.openGen += 1;
    this.clearFocusTimer();
    this.entries = entries.slice();
    if (!opts?.keepChannel) this.channelIdx = 0;
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
    this.clearFocusTimer();
    clearPanelTip(this.toast);
    this.visible = false;
    this.ime.hide();
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

  confirm(): void {
    if (!this.visible) return;
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
    void Promise.resolve(
      this.onSend?.(
        text,
        sendType,
        whisper ? { sendTo: whisper.id } : undefined,
      ),
    ).then((ok) => {
      if (!this.visible || this.openGen !== gen) return;
      if (ok === false) {
        this.focusInput();
        return;
      }
      this.draft = "";
      this.ime.clear();
      this.redraw();
      this.focusInput();
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
    const maxW = LCD_WIDTH - 16 - 20;
    const n = this.buildDisplayLines(maxW).length;
    const maxScroll = Math.max(0, n - MSG_PAGE);
    this.scroll = Math.min(Math.max(0, this.scroll), maxScroll);
  }

  private inputRect(): { x: number; y: number; w: number; h: number } {
    const x = 12;
    const y = LCD_HEIGHT - SOFTKEY_H - 36;
    return { x: x + 36, y: y + 4, w: LCD_WIDTH - 28 - 36, h: 22 };
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

    const x = 8;
    const y = 24;
    const w = LCD_WIDTH - 16;
    const h = LCD_HEIGHT - SOFTKEY_H - 32;
    drawCbePanel(this.box, x, y, w, h, 0x0a1812);

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
    const msgMaxW = w - 20;
    const displayLines = this.buildDisplayLines(msgMaxW);
    const maxScroll = Math.max(0, displayLines.length - MSG_PAGE);
    this.scroll = Math.min(this.scroll, maxScroll);
    const end = displayLines.length - this.scroll;
    const start = Math.max(0, end - MSG_PAGE);
    const shown = displayLines.slice(start, end);
    shown.forEach((row, i) => {
      const ly = msgTop + i * 16;
      this.addLabel(row.text, x + 10, ly, row.color);
      if (this.canWhisperEntry(row.entry)) {
        const entry = row.entry;
        this.rowHits.push(
          attachRowPointerHit(this, x + 8, ly, w - 16, 16, () => {
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
      this.addLabel("(暂无消息)", x + 10, msgTop + 8, CBE.inkLabel);
    }

    // Compose bar
    const ch = CHANNELS[this.channelIdx]!;
    const whisper =
      this.pmTarget && this.channelIdx === LOCAL_CHANNEL_IDX
        ? this.pmTarget
        : null;
    const barY = y + h - 34;
    this.box
      .roundRect(x + 6, barY, w - 12, 28, 2)
      .fill({ color: 0x12241c })
      .stroke({ width: 1, color: CBE.borderDim });
    const tag =
      ch.sendType < 0
        ? "[统]"
        : whisper
          ? "[私]"
          : `[${chatPrefixForType(ch.sendType)}]`;
    this.addLabel(tag, x + 12, barY + 8, whisper ? CHAT_PRIVATE_COLOR : ch.color);
    let placeholder: string;
    if (ch.sendType < 0) placeholder = "系统频道不可发送";
    else if (whisper) {
      const nick = whisper.name.slice(0, 6);
      placeholder = this.draft || `对${nick}…`;
    } else {
      placeholder = this.draft || "点击输入…";
    }
    this.addLabel(
      placeholder.slice(0, 16),
      x + 44,
      barY + 8,
      this.draft ? CBE.ink : CBE.inkLabel,
      { maxWidth: w - 56 },
    );

    this.inputHit.clear();
    if (ch.sendType >= 0) {
      this.inputHit
        .rect(x + 40, barY, w - 50, 28)
        .fill({ color: 0xffffff, alpha: 0.001 });
    }

    const hint =
      ch.sendType < 0
        ? "←→频道  ↑↓翻阅  返回关闭"
        : whisper
          ? `私聊:${whisper.name.slice(0, 8)}  点消息可选人`
          : "←→频道  ↑↓翻阅  点消息可私聊";
    this.addLabel(hint, x + 10, y + h - 48, CBE.inkLabel);

    drawPanelTipOverlay(this, this.toast);
  }

  /** One painted row per wrapped line (preserves \n and width wrap). */
  private buildDisplayLines(
    maxWidth: number,
  ): { text: string; color: number; entry: ChatEntry }[] {
    const out: { text: string; color: number; entry: ChatEntry }[] = [];
    for (const entry of this.filtered()) {
      const color = colorForChatType(entry.type);
      const paragraphs = entry.text.split(/\n/);
      for (const para of paragraphs) {
        const wrapped = wrapUiText(para.length ? para : " ", maxWidth, UC3_CELL);
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
    opts?: { maxWidth?: number },
  ): void {
    const lab = new BitmapLabel();
    lab.position.set(x, y);
    void lab.setText(text, color, {
      uc3: true,
      smooth: false,
      outline: false,
      maxWidth: opts?.maxWidth,
      // Compose bar may clip; list rows are pre-wrapped — never stack in-label.
      wordWrap: false,
    });
    this.addChild(lab);
    this.labels.push(lab);
  }
}
