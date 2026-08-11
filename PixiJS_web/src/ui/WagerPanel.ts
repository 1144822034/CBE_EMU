import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  drawCbePanel,
  drawCbeTitlePill,
  makePixelText,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import type { WagerListRow, WagerMine } from "../net/protocolArena";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import type { SoftkeyLabels } from "./SoftkeyBar";
import { COPPER_PER_GOLD } from "../net/protocolAuction";

export type WagerPanelAction =
  | "create"
  | "challenge"
  | "confirm"
  | "reject"
  | "cancel"
  | "refresh"
  | "close"
  | "noop";

type WagerRowAction = {
  label: string;
  action: WagerPanelAction;
  challengeId?: number;
  softLabel?: string;
};

/** 赏金押金最低 100 金（1金=10000铜）. */
export const WAGER_MIN_STAKE_GOLD = 100;
export const WAGER_MIN_STAKE_COPPER = WAGER_MIN_STAKE_GOLD * COPPER_PER_GOLD;

function formatStakeJin(copper: number): string {
  const jin = Math.max(0, Math.floor((copper >>> 0) / COPPER_PER_GOLD));
  return `${jin}金`;
}

/**
 * 赏金擂台面板：摆擂 / 列表挑战 / 擂主确认。
 * 软键契约：left/center/right（勿用 mid）。押金仅左右方向键调节（单位：金）。
 */
export class WagerPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private rows: WagerListRow[] = [];
  private mine: WagerMine | null = null;
  private selfRoleId = 0;
  private listCursor = 0;
  private stakeInput = WAGER_MIN_STAKE_COPPER;
  private toast: PanelTipState = createPanelTipState();
  onAction: ((action: WagerPanelAction, challengeId?: number) => void) | null =
    null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(this.box);
  }

  open(
    rows: WagerListRow[],
    mine: WagerMine | null,
    selfRoleId = 0,
  ): void {
    this.rows = rows;
    this.mine = mine;
    this.selfRoleId = selfRoleId >>> 0;
    this.listCursor = 0;
    this.visible = true;
    this.redraw();
  }

  setData(
    rows: WagerListRow[],
    mine: WagerMine | null,
    selfRoleId?: number,
  ): void {
    this.rows = rows;
    this.mine = mine;
    if (selfRoleId != null) this.selfRoleId = selfRoleId >>> 0;
    this.redraw();
  }

  close(): void {
    clearPanelTip(this.toast);
    this.visible = false;
  }

  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (this.visible) this.redraw();
    }, ms);
  }

  /** delta 为铜币；左右键通常一次 ±1 金. */
  adjustStake(delta: number): void {
    if (!this.visible) return;
    const next = Math.max(WAGER_MIN_STAKE_COPPER, this.stakeInput + delta);
    /* 上限 10000 金（u32 铜币安全范围） */
    this.stakeInput = Math.min(next, 10_000 * COPPER_PER_GOLD);
    this.redraw();
  }

  get stake(): number {
    return this.stakeInput >>> 0;
  }

  move(dir: -1 | 1): void {
    if (!this.visible) return;
    const n = this.actions().length;
    this.listCursor = (this.listCursor + dir + n) % n;
    this.redraw();
  }

  confirm(): void {
    if (!this.visible) return;
    const actions = this.actions();
    const a = actions[this.listCursor];
    if (!a || a.action === "noop") return;
    this.onAction?.(a.action, a.challengeId);
  }

  softkeys(): SoftkeyLabels {
    const a = this.actions()[this.listCursor];
    const label = a?.softLabel || (a?.action === "create" ? "摆擂" : "确认");
    return { left: label, center: label, right: "关闭" };
  }

  private isOwnHostRow(r: WagerListRow): boolean {
    if (this.selfRoleId && r.hostLeaderRoleId === this.selfRoleId) return true;
    if (
      this.mine?.challengeId &&
      this.mine.challengeId === r.challengeId &&
      (this.mine.status === 0 || this.mine.status === 1)
    ) {
      return true;
    }
    return false;
  }

  private actions(): WagerRowAction[] {
    const out: WagerRowAction[] = [];
    out.push({
      label: `摆擂 押金${formatStakeJin(this.stakeInput)}`,
      action: "create",
      softLabel: "摆擂",
    });
    for (const r of this.rows) {
      const own = this.isOwnHostRow(r);
      const stakeLabel = formatStakeJin(r.stake);
      if (r.status === 1) {
        if (own) {
          out.push({
            label: `${r.hostName} ${r.hostCount}人 押${stakeLabel} [待我确认]`,
            action: "noop",
            challengeId: r.challengeId,
            softLabel: "确认",
          });
        } else {
          out.push({
            label: `${r.hostName} ${r.hostCount}人 押${stakeLabel} [待确认]`,
            action: "noop",
            challengeId: r.challengeId,
            softLabel: "确认",
          });
        }
        continue;
      }
      if (own) {
        out.push({
          label: `${r.hostName} ${r.hostCount}人 押${stakeLabel} [我的擂台]`,
          action: "noop",
          challengeId: r.challengeId,
          softLabel: "确认",
        });
        continue;
      }
      out.push({
        label: `${r.hostName} ${r.hostCount}人 押${stakeLabel} [可挑战]`,
        action: "challenge",
        challengeId: r.challengeId,
        softLabel: "应战",
      });
    }
    if (this.mine?.needConfirm) {
      out.push({
        label: `确认开战 #${this.mine.challengeId}`,
        action: "confirm",
        challengeId: this.mine.challengeId,
        softLabel: "开战",
      });
      out.push({
        label: `拒绝挑战 #${this.mine.challengeId}`,
        action: "reject",
        challengeId: this.mine.challengeId,
        softLabel: "拒绝",
      });
    }
    if (this.mine && this.mine.status === 0 && this.mine.challengeId) {
      out.push({
        label: `撤擂 #${this.mine.challengeId}`,
        action: "cancel",
        challengeId: this.mine.challengeId,
        softLabel: "撤擂",
      });
    }
    out.push({ label: "刷新", action: "refresh", softLabel: "刷新" });
    out.push({ label: "关闭", action: "close", softLabel: "关闭" });
    return out;
  }

  private clearLabels(): void {
    for (const l of this.labels) l.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
  }

  private addLabel(text: string, x: number, y: number, color: number): void {
    const l = makePixelText(text, 12, color);
    l.x = x;
    l.y = y;
    this.addChild(l);
    this.labels.push(l);
  }

  private redraw(): void {
    this.clearLabels();
    this.box.clear();
    drawCbePanel(this.box, 0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
    drawCbeTitlePill(this.box, 8, 6, LCD_WIDTH - 16, 22);
    this.addLabel("赏金擂台", 16, 8, CBE.cream);
    const mineLine = this.mine?.challengeId
      ? `我的:#${this.mine.challengeId} 状态${this.mine.status}${
          this.mine.needConfirm ? " 待确认" : ""
        }`
      : "我的:无";
    this.addLabel(mineLine, 12, 32, CBE.inkMint);
    this.addLabel("左右调押金(金) 上下选择", 12, 48, CBE.inkLabel);

    const actions = this.actions();
    if (this.listCursor >= actions.length) this.listCursor = 0;
    let y = 68;
    const maxY = LCD_HEIGHT - SOFTKEY_H - 8;
    let start = 0;
    const rowH = 14;
    const maxShow = Math.max(1, Math.floor((maxY - y) / rowH));
    if (this.listCursor >= maxShow) start = this.listCursor - maxShow + 1;
    for (let i = 0; i < maxShow && start + i < actions.length; i++) {
      const idx = start + i;
      const a = actions[idx]!;
      const hi = idx === this.listCursor;
      if (hi) {
        this.box.rect(8, y - 1, LCD_WIDTH - 16, 14).fill({ color: CBE.select });
      }
      this.addLabel(a.label, 12, y, hi ? CBE.menuHi : CBE.ink);
      this.rowHits.push(
        attachRowPointerHit(this, 8, y - 1, LCD_WIDTH - 16, 14, () => {
          this.listCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
      y += rowH;
    }
    drawPanelTipOverlay(this, this.toast);
  }
}
