/**
 * Surrounding players / team / friends panels (CBE softkey list style).
 */
import { Container, Graphics, Sprite, type Texture } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  drawCbePanel,
  drawCbeTitlePill,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { tryLoadUiTexture, uiSprite } from "./UiAssets";
import type { PeerActor } from "../net/protocol";
import type {
  FriendRow,
  OtherEquipView,
  PlayerInfoView,
  TeamMember,
} from "../net/protocolSocial";
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

const RANK_TAB_BG = 0x375946;
const RANK_DEFAULT_TABS = ["等级榜", "财富榜", "PK榜"];
const FRIEND_HUB_TABS = ["好友", "恩怨"];
const ENMITY_DIR_TABS = ["我击杀", "击杀我"];
/** 1金元宝 = 10000 铜（与背包金/银/铜栏同源）. */
const COPPER_PER_GOLD = 10000;

/** Map tips must show role name, never h_*.actor resource. */
function displayPeerName(name: string | undefined): string {
  const s = (name || "").replace(/\0+$/g, "").trim();
  if (!s || /\.actor$/i.test(s)) return "玩家";
  return s;
}

export type SocialKind =
  | "nearby"
  | "friends"
  | "enmity"
  | "team"
  | "info"
  | "otherequip"
  | "titles"
  | "rank"
  | "chatlog";

export type SocialRow = {
  id: number;
  name: string;
  subtitle?: string;
};

export type SocialAction =
  | "player_info"
  | "view_equip"
  | "invite_team"
  | "add_friend"
  | "trade"
  | "private_chat"
  | "spar"
  | "guild_invite"
  | "leave_team"
  | "refresh"
  | "invite_pick"
  | "equip_title"
  | "send_chat"
  | "rank_tab"
  | "friend_hub_tab"
  | "enmity_dir_tab"
  | "hunt_order"
  | "list_page";

const JOBS = ["天机", "幻剑", "鬼道"];

/** Visible list rows inside the social panel (title + footer reserved). */
const LIST_PAGE_SIZE = 10;
/** Rank list rows under the tab strip. */
const RANK_PAGE_SIZE = 9;
/** 好友 / 恩怨：一屏一条服务端页（与 FRIEND_PAGE_SIZE / ENMITY_PAGE_SIZE 对齐）. */
export const SOCIAL_LIST_PAGE_SIZE = LIST_PAGE_SIZE;

type Mode = "list" | "actions" | "text";

export class SocialPanel extends Container {
  private kind: SocialKind = "nearby";
  private mode: Mode = "list";
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private uiSprites: Sprite[] = [];
  private rowHits: Container[] = [];
  private listCursor = 0;
  private actionCursor = 0;
  private rows: SocialRow[] = [];
  private actionLabels: string[] = [];
  private textLines: string[] = [];
  private textPage = 0;
  private title = "";
  private rankTabs: string[] = RANK_DEFAULT_TABS.slice();
  private rankType = 0;
  private rankMyorder = 0;
  /** Bumped on each rank fetch; late openRank must ignore stale replies. */
  private rankFetchGen = 0;
  /** 0=好友 1=恩怨（好友页顶栏）. */
  private friendHubTab = 0;
  /** 0=我击杀 1=击杀我. */
  private enmityDir = 0;
  /** 追杀令选目标模式：列表确定直接对选中仇人使用. */
  private huntPickSeq = 0;
  /** 好友/恩怨服务端页（1-based）. */
  private listPage = 1;
  private listPages = 1;
  private jinTex: Texture | null = null;
  private jinTexLoading = false;
  private toast: PanelTipState = createPanelTipState();

  onAction: ((kind: SocialKind, action: SocialAction, row?: SocialRow) => void) | null =
    null;
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

  openNearby(peers: PeerActor[]): void {
    this.kind = "nearby";
    this.title = "周围玩家";
    this.rows = peers.map((p) => ({
      id: p.actorId,
      name: displayPeerName(p.name),
      subtitle: p.title
        ? `${p.title} @${p.x},${p.y}`
        : `@${p.x},${p.y}`,
    }));
    this.enterList();
  }

  /**
   * Map face-to-face peer tap → same action menu as 周围玩家 list confirm.
   * Prefers the live nearby list so refresh/cursor stay consistent; synthesizes
   * a single row when the peer is visible on the map but not yet in the page.
   */
  openNearbyPeerActions(
    actorId: number,
    peers: PeerActor[],
    fallback?: { name: string; x: number; y: number; title?: string },
  ): void {
    this.kind = "nearby";
    this.title = "周围玩家";
    let list = peers.slice();
    if (!list.some((p) => p.actorId === actorId) && fallback) {
      list = [
        {
          actorId,
          name: displayPeerName(fallback.name),
          x: fallback.x | 0,
          y: fallback.y | 0,
          visualVariant: 0,
          visualGroup: 0,
          title: fallback.title,
        },
        ...list,
      ];
    }
    this.rows = list.map((p) => ({
      id: p.actorId,
      name: displayPeerName(p.name),
      subtitle: p.title
        ? `${p.title} @${p.x},${p.y}`
        : `@${p.x},${p.y}`,
    }));
    const ix = this.rows.findIndex((r) => r.id === actorId);
    this.listCursor = ix >= 0 ? ix : 0;
    this.visible = true;
    this.openActions();
  }

  openFriends(
    friends: FriendRow[],
    page = 1,
    allpgs = 1,
    opts?: { keepHub?: boolean },
  ): void {
    this.kind = "friends";
    this.title = "好友";
    if (!opts?.keepHub) {
      this.friendHubTab = 0;
      this.huntPickSeq = 0;
    }
    this.listPage = Math.max(1, page | 0);
    this.listPages = Math.max(1, allpgs | 0);
    this.rows = friends.map((f) => ({
      id: f.id,
      name: f.name,
      subtitle: `${f.online ? "在线" : "离线"} 亲密度${f.degree}`,
    }));
    this.enterList();
  }

  /**
   * 恩怨列表（我击杀 / 击杀我）.
   * @param huntPickSeq 非 0 时为追杀令选目标：确定即使用，不进动作菜单.
   */
  openEnmity(
    rows: { id: number; name: string; online: boolean; kills: number }[],
    dir: number,
    huntPickSeq = 0,
    page = 1,
    allpgs = 1,
  ): void {
    this.kind = "enmity";
    this.title = huntPickSeq ? "追杀令·选仇人" : "恩怨";
    this.friendHubTab = 1;
    this.enmityDir = dir === 1 ? 1 : 0;
    this.huntPickSeq = huntPickSeq >>> 0;
    this.listPage = Math.max(1, page | 0);
    this.listPages = Math.max(1, allpgs | 0);
    const verb = this.enmityDir === 1 ? "被杀" : "击杀";
    this.rows = rows.map((r) => ({
      id: r.id,
      name: r.name,
      subtitle: `${r.online ? "在线" : "离线"} ${verb}×${r.kills || 1}`,
    }));
    this.enterList();
  }

  get enmityDirection(): number {
    return this.enmityDir;
  }

  get friendHub(): number {
    return this.friendHubTab;
  }

  get huntOrderPickSeq(): number {
    return this.huntPickSeq;
  }

  get serverListPage(): number {
    return this.listPage;
  }

  get serverListPages(): number {
    return this.listPages;
  }

  clearHuntPick(): void {
    this.huntPickSeq = 0;
  }

  openTeam(members: TeamMember[], leaderId: number): void {
    this.kind = "team";
    this.title = "组队";
    if (!members.length) {
      this.rows = [{ id: 0, name: "(未组队)", subtitle: "从周围玩家邀请" }];
    } else {
      this.rows = members.map((m) => ({
        id: m.id,
        name: `${m.name}${m.id === leaderId ? "★" : ""}`,
        subtitle: `${JOBS[m.jobIndex] ?? "?"} HP${m.hp}/${m.hpMax}${m.online ? "" : " 离"}`,
      }));
    }
    this.enterList();
  }

  openTitles(
    rows: { id: number; name: string; desc: string; equipped: boolean }[],
  ): void {
    this.kind = "titles";
    this.title = "称号";
    this.rows = rows.map((r) => ({
      id: r.id,
      name: `${r.equipped ? "★" : ""}${r.name}`,
      subtitle: r.desc.slice(0, 16),
    }));
    this.enterList();
  }

  /** Call before openRanking(); pass the token into openRank when it returns. */
  beginRankFetch(): number {
    return ++this.rankFetchGen;
  }

  /**
   * Ranking board with mall/bag-style category tabs (←→).
   * Wealth tab (type=1) paints score as gold-ingot only (omit 银/铜).
   */
  openRank(opts: {
    type?: number;
    tabs?: string[];
    rows: { id: number; name: string; score: string }[];
    myorder: number;
    /** From beginRankFetch(); drop if a newer tab switch already started. */
    fetchGen?: number;
  }): void {
    if (
      opts.fetchGen != null &&
      opts.fetchGen !== this.rankFetchGen
    ) {
      return;
    }
    this.kind = "rank";
    this.title = "排行榜";
    this.rankTabs =
      opts.tabs && opts.tabs.length ? opts.tabs.slice() : RANK_DEFAULT_TABS.slice();
    this.rankType = Math.max(
      0,
      Math.min(this.rankTabs.length - 1, opts.type ?? 0),
    );
    this.rankMyorder = opts.myorder >>> 0;
    this.ensureJinIcon();
    this.rows = opts.rows.map((r, i) => ({
      id: r.id,
      name: `${i + 1}.${r.name}`.slice(0, 12),
      subtitle: r.score,
    }));
    this.enterList();
  }

  openChatLog(lines: string[]): void {
    this.kind = "chatlog";
    this.title = "聊天信息";
    this.rows = [
      { id: -1, name: "发送世界消息…", subtitle: "" },
      ...lines.slice(-20).map((line, i) => ({
        id: i + 1,
        name: line.slice(0, 22),
        subtitle: "",
      })),
    ];
    this.enterList();
  }

  showPlayerInfo(info: PlayerInfoView): void {
    this.kind = "info";
    this.title = "玩家信息";
    this.textLines = info.lines.length ? info.lines : ["(无信息)"];
    this.textPage = 0;
    this.mode = "text";
    this.visible = true;
    this.redraw();
  }

  showOtherEquip(eq: OtherEquipView): void {
    this.kind = "otherequip";
    this.title = `装备·${eq.name}`;
    const lines = [
      `Lv${eq.level} ${JOBS[eq.jobIndex] ?? "?"} ${eq.sex ? "女" : "男"}`,
      ...eq.items.map(
        (it) =>
          `${it.name}${it.enhance ? `+${it.enhance}` : ""}`,
      ),
    ];
    this.textLines = lines.length ? lines : ["(无装备)"];
    this.textPage = 0;
    this.mode = "text";
    this.visible = true;
    this.redraw();
  }

  get selectedRow(): SocialRow | undefined {
    return this.rows[this.listCursor];
  }

  /** ←→ on ranking / 好友·恩怨 hub / 恩怨方向. */
  moveCategory(delta: number): void {
    if (!this.visible || this.mode !== "list") return;
    if (this.kind === "rank") {
      const n = this.rankTabs.length;
      if (n <= 1) return;
      const next = (this.rankType + delta + n) % n;
      if (next === this.rankType) return;
      /* 先切标签并清空旧行，避免 PK 榜短暂显示财富/等级分数. */
      this.rankType = next;
      this.rows = [];
      this.listCursor = 0;
      this.rankMyorder = 0;
      this.redraw();
      this.onAction?.(this.kind, "rank_tab", {
        id: next,
        name: this.rankTabs[next] ?? `榜${next}`,
      });
      return;
    }
    if (this.kind === "friends" || this.kind === "enmity") {
      /* Hub 好友|恩怨；恩怨页再 ←→ 切 我击杀|击杀我. */
      if (this.kind === "enmity" && this.friendHubTab === 1 && !this.huntPickSeq) {
        const n = ENMITY_DIR_TABS.length;
        const next = (this.enmityDir + delta + n) % n;
        if (next !== this.enmityDir) {
          this.enmityDir = next;
          this.onAction?.(this.kind, "enmity_dir_tab", {
            id: next,
            name: ENMITY_DIR_TABS[next] ?? "",
          });
          return;
        }
      }
      const n = FRIEND_HUB_TABS.length;
      const next = (this.friendHubTab + delta + n) % n;
      if (next === this.friendHubTab) return;
      this.friendHubTab = next;
      this.onAction?.(this.kind, "friend_hub_tab", {
        id: next,
        name: FRIEND_HUB_TABS[next] ?? "",
      });
    }
  }

  private listPageStart(): number {
    if (!this.rows.length) return 0;
    const pageSize = this.kind === "rank" ? RANK_PAGE_SIZE : LIST_PAGE_SIZE;
    return Math.floor(this.listCursor / pageSize) * pageSize;
  }

  private textPageSize(): number {
    return 12;
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
    if (this.mode === "text") {
      const ps = this.textPageSize();
      const pages = Math.max(1, Math.ceil(this.textLines.length / ps));
      this.textPage = (this.textPage + delta + pages) % pages;
      this.redraw();
      return;
    }
    if (this.mode === "list") {
      const n = Math.max(1, this.rows.length);
      if (
        (this.kind === "friends" || this.kind === "enmity") &&
        this.listPages > 1
      ) {
        if (delta > 0 && this.listCursor >= n - 1 && this.listPage < this.listPages) {
          this.onAction?.(this.kind, "list_page", {
            id: this.listPage + 1,
            name: "next",
          });
          return;
        }
        if (delta < 0 && this.listCursor <= 0 && this.listPage > 1) {
          this.onAction?.(this.kind, "list_page", {
            id: this.listPage - 1,
            name: "prev",
          });
          return;
        }
        const next = this.listCursor + delta;
        if (next < 0 || next >= n) {
          this.redraw();
          return;
        }
        this.listCursor = next;
        this.redraw();
        return;
      }
      this.listCursor = (this.listCursor + delta + n) % n;
      this.redraw();
    }
  }

  confirm(): void {
    if (!this.visible) return;
    if (this.mode === "text") {
      const ps = this.textPageSize();
      const pages = Math.max(1, Math.ceil(this.textLines.length / ps));
      if (pages > 1 && this.textPage < pages - 1) {
        this.textPage += 1;
        this.redraw();
        return;
      }
      this.hide();
      this.onClose?.();
      return;
    }
    // Ranking: tabs switch via ←→; 确定 is a no-op on the list.
    if (this.mode === "list" && this.kind === "rank") {
      return;
    }
    if (this.mode === "list" && this.kind === "enmity" && this.huntPickSeq) {
      const row = this.rows[this.listCursor];
      if (row && row.id) {
        this.onAction?.(this.kind, "hunt_order", row);
      }
      return;
    }
    if (this.mode === "list") {
      this.openActions();
      return;
    }
    const label = this.actionLabels[this.actionCursor];
    if (!label || label === "返回") {
      this.mode = "list";
      this.redraw();
      return;
    }
    const action = labelToAction(label);
    const row = this.rows[this.listCursor];
    this.onAction?.(this.kind, action, row);
    if (action === "leave_team" || action === "refresh") {
      /* keep open; session will refresh */
    } else if (
      action === "player_info" ||
      action === "view_equip" ||
      action === "invite_team" ||
      action === "add_friend" ||
      action === "trade" ||
      action === "spar" ||
      action === "guild_invite" ||
      action === "equip_title" ||
      action === "send_chat"
    ) {
      /* stay; info overlays may replace */
    }
  }

  cancel(): void {
    if (!this.visible) return;
    if (tryDismissPanelTipOnSoftRight(this.toast)) return;
    if (this.mode === "actions") {
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

  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (this.visible) this.redraw();
    }, ms);
  }

  private enterList(): void {
    this.mode = "list";
    this.listCursor = 0;
    this.visible = true;
    this.redraw();
  }

  private openActions(): void {
    this.actionLabels = this.buildActions();
    this.actionCursor = 0;
    this.mode = "actions";
    this.redraw();
  }

  private buildActions(): string[] {
    if (this.kind === "nearby" || this.kind === "friends") {
      const row = this.rows[this.listCursor];
      if (!row || row.id === 0) return ["返回"];
      return [
        "玩家信息",
        "查看装备",
        "组队邀请",
        "加为好友",
        "交易",
        "发起私聊",
        "切磋",
        "入帮",
        "返回",
      ];
    }
    if (this.kind === "enmity") {
      const row = this.rows[this.listCursor];
      if (!row || row.id === 0) return ["返回"];
      return ["玩家信息", "查看装备", "切磋", "返回"];
    }
    if (this.kind === "titles") {
      return ["装备称号", "返回"];
    }
    if (this.kind === "chatlog") {
      return ["发送消息", "返回"];
    }
    if (this.kind === "team") {
      const acts = ["刷新"];
      if (this.rows.some((r) => r.id !== 0)) acts.unshift("离队");
      acts.push("邀请附近…");
      acts.push("返回");
      return acts;
    }
    return ["返回"];
  }

  private redraw(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    for (const s of this.uiSprites) s.destroy();
    this.uiSprites = [];
    clearPointerHits(this.rowHits);
    this.box.clear();
    const x = 8;
    const y = 28;
    const w = LCD_WIDTH - 16;
    const h = LCD_HEIGHT - SOFTKEY_H - 40;
    drawCbePanel(this.box, x, y, w, h, CBE.bgDeep);
    const pillW = Math.min(100, this.title.length * 12 + 24);
    const pillH = 20;
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, y + 6, pillW, pillH);
    this.addLabel(this.title, LCD_WIDTH / 2 - pillW / 2 + 8, y + 9, CBE.ink);

    try {
      if (this.mode === "text") {
        const ps = this.textPageSize();
        const pages = Math.max(1, Math.ceil(this.textLines.length / ps));
        const start = this.textPage * ps;
        const shown = this.textLines.slice(start, start + ps);
        shown.forEach((line, i) => {
          this.addLabel(line.slice(0, 28), x + 10, y + 36 + i * 15, CBE.inkMint);
        });
        const hint =
          pages > 1
            ? `确定=${this.textPage + 1 < pages ? "下页" : "关闭"}`
            : "确定/取消=关闭";
        this.addLabel(hint, x + 10, y + h - 18, CBE.inkLabel);
        if (pages > 1) {
          attachPageNavButtons({
            host: this,
            hits: this.rowHits,
            box: this.box,
            x: x + 8,
            y: y + h - 40,
            w: w - 16,
            page: this.textPage + 1,
            pages,
            addLabel: (t, lx, ly, c) => this.addLabel(t, lx, ly, c),
            onPrev: () => {
              this.textPage = Math.max(0, this.textPage - 1);
              this.redraw();
            },
            onNext: () => {
              this.textPage = Math.min(pages - 1, this.textPage + 1);
              this.redraw();
            },
          });
        }
        this.rowHits.push(
          attachRowPointerHit(this, x + 6, y + 36, w - 12, h - 70, () => this.confirm()),
        );
        return;
      }

      if (this.mode === "actions") {
        this.actionLabels.forEach((lab, i) => {
          const hi = i === this.actionCursor;
          if (hi) {
            this.box
              .rect(x + 6, y + 36 + i * 18, w - 12, 16)
              .fill({ color: CBE.select });
          }
          this.addLabel(lab, x + 12, y + 38 + i * 18, hi ? CBE.ink : CBE.inkMint);
          const idx = i;
          this.rowHits.push(
            attachRowPointerHit(this, x + 6, y + 36 + i * 18, w - 12, 16, () => {
              this.actionCursor = idx;
              this.redraw();
              this.confirm();
            }),
          );
        });
        return;
      }

      if (this.kind === "rank") {
        this.drawRankList(x, y, w, h);
        return;
      }

      if (this.kind === "friends" || this.kind === "enmity") {
        this.drawFriendHubList(x, y, w, h);
        return;
      }

      if (!this.rows.length) {
        this.addLabel("(空)", x + 10, y + 40, CBE.inkMint);
        return;
      }

      const total = this.rows.length;
      const pages = Math.max(1, Math.ceil(total / LIST_PAGE_SIZE));
      const pageStart = this.listPageStart();
      const page = Math.floor(pageStart / LIST_PAGE_SIZE) + 1;
      const localCursor = this.listCursor - pageStart;
      const shown = this.rows.slice(pageStart, pageStart + LIST_PAGE_SIZE);

      shown.forEach((row, i) => {
        const hi = i === localCursor;
        if (hi) {
          this.box
            .rect(x + 6, y + 36 + i * 20, w - 12, 18)
            .fill({ color: CBE.select });
        }
        this.addLabel(
          row.name.slice(0, 14),
          x + 10,
          y + 38 + i * 20,
          hi ? CBE.ink : CBE.inkMint,
        );
        if (row.subtitle) {
          this.addLabel(
            row.subtitle.slice(0, 16),
            x + 100,
            y + 38 + i * 20,
            CBE.inkLabel,
          );
        }
        const abs = pageStart + i;
        this.rowHits.push(
          attachRowPointerHit(this, x + 6, y + 36 + i * 20, w - 12, 18, () => {
            this.listCursor = abs;
            this.redraw();
            this.confirm();
          }),
        );
      });

      if (pages > 1) {
        attachPageNavButtons({
          host: this,
          hits: this.rowHits,
          box: this.box,
          x: x + 8,
          y: y + h - 22,
          w: w - 16,
          page,
          pages,
          addLabel: (t, lx, ly, c) => this.addLabel(t, lx, ly, c),
          onPrev: () => {
            this.listCursor = Math.max(0, pageStart - LIST_PAGE_SIZE);
            this.redraw();
          },
          onNext: () => {
            this.listCursor = Math.min(
              total - 1,
              pageStart + LIST_PAGE_SIZE,
            );
            this.redraw();
          },
        });
      }
    } finally {
      drawPanelTipOverlay(this, this.toast);
    }
  }

  private drawFriendHubList(x: number, y: number, w: number, h: number): void {
    const tabY = y + 30;
    const tabH = 20;
    this.drawGenericTabs(
      x + 6,
      tabY,
      w - 12,
      tabH,
      FRIEND_HUB_TABS,
      this.friendHubTab,
      (idx) => {
        if (idx === this.friendHubTab) return;
        const delta = idx - this.friendHubTab;
        this.friendHubTab = idx;
        this.onAction?.(this.kind, "friend_hub_tab", {
          id: idx,
          name: FRIEND_HUB_TABS[idx] ?? "",
        });
        void delta;
      },
    );

    let listTop = tabY + tabH + 4;
    if (this.kind === "enmity" && !this.huntPickSeq) {
      this.drawGenericTabs(
        x + 6,
        listTop,
        w - 12,
        tabH,
        ENMITY_DIR_TABS,
        this.enmityDir,
        (idx) => {
          if (idx === this.enmityDir) return;
          this.enmityDir = idx;
          this.onAction?.(this.kind, "enmity_dir_tab", {
            id: idx,
            name: ENMITY_DIR_TABS[idx] ?? "",
          });
        },
      );
      listTop += tabH + 4;
    }

    const footY = y + h - 18;
    if (!this.rows.length) {
      this.addLabel(
        this.kind === "enmity" ? "(暂无恩怨)" : "(暂无好友)",
        x + 10,
        listTop + 8,
        CBE.inkMint,
      );
    } else {
      /* 一屏 = 一服务端页，不再二次本地切片. */
      const total = this.rows.length;
      const localCursor = Math.min(this.listCursor, total - 1);
      this.listCursor = localCursor;
      this.rows.forEach((row, i) => {
        const hi = i === localCursor;
        const ry = listTop + i * 20;
        if (hi) {
          this.box.rect(x + 6, ry, w - 12, 18).fill({ color: CBE.select });
        }
        this.addLabel(
          row.name.slice(0, 12),
          x + 10,
          ry + 2,
          hi ? CBE.ink : CBE.inkMint,
        );
        if (row.subtitle) {
          this.addLabel(
            row.subtitle.slice(0, 14),
            x + 100,
            ry + 2,
            CBE.inkLabel,
          );
        }
        this.rowHits.push(
          attachRowPointerHit(this, x + 6, ry, w - 12, 18, () => {
            this.listCursor = i;
            this.redraw();
            this.confirm();
          }),
        );
      });
    }
    const pageHint =
      this.listPages > 1
        ? ""
        : this.rows.length
          ? `↑↓ ${this.listCursor + 1}/${this.rows.length}`
          : "";
    if (this.listPages > 1) {
      attachPageNavButtons({
        host: this,
        hits: this.rowHits,
        box: this.box,
        x: x + 8,
        y: footY - 22,
        w: w - 16,
        page: this.listPage,
        pages: this.listPages,
        addLabel: (t, lx, ly, c) => this.addLabel(t, lx, ly, c),
        onPrev: () => {
          if (this.listPage <= 1) return;
          this.onAction?.(this.kind, "list_page", {
            id: this.listPage - 1,
            name: "prev",
          });
        },
        onNext: () => {
          if (this.listPage >= this.listPages) return;
          this.onAction?.(this.kind, "list_page", {
            id: this.listPage + 1,
            name: "next",
          });
        },
      });
    } else if (pageHint) {
      this.addLabel(pageHint, x + 10, footY - 14, CBE.inkLabel);
    }
    const hint = this.huntPickSeq
      ? "确定=使用追杀令"
      : this.kind === "enmity"
        ? "←→切方向  顶栏切好友"
        : "←→切恩怨";
    this.addLabel(hint, x + 10, footY, CBE.inkLabel);
  }

  private drawGenericTabs(
    x: number,
    y: number,
    w: number,
    h: number,
    tabs: string[],
    active: number,
    onPick: (idx: number) => void,
  ): void {
    const n = tabs.length;
    if (n <= 0) return;
    const gap = 2;
    const tw = Math.floor((w - gap * (n - 1)) / n);
    for (let i = 0; i < n; i++) {
      const label = tabs[i] || `页${i}`;
      const tx = x + i * (tw + gap);
      const isActive = i === active;
      this.box
        .roundRect(tx, y, tw, h, 2)
        .fill({ color: isActive ? CBE.bgPanelDark : RANK_TAB_BG })
        .stroke({
          width: 1,
          color: isActive ? CBE.borderBright : CBE.borderDim,
        });
      const text = isActive ? label.slice(0, 4) : label.slice(0, 1);
      this.addLabelCentered(
        text,
        tx + tw / 2,
        y + h / 2,
        isActive ? CBE.ink : CBE.inkMint,
      );
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, tx, y, tw, h, () => onPick(idx)),
      );
    }
  }

  private drawRankList(x: number, y: number, w: number, h: number): void {
    const tabY = y + 30;
    const tabH = 20;
    this.drawRankTabs(x + 6, tabY, w - 12, tabH);

    const listTop = tabY + tabH + 6;
    const footY = y + h - 34;
    if (!this.rows.length) {
      this.addLabel("(暂无排名)", x + 10, listTop + 8, CBE.inkMint);
    } else {
      const total = this.rows.length;
      const pages = Math.max(1, Math.ceil(total / RANK_PAGE_SIZE));
      const pageStart = this.listPageStart();
      const page = Math.floor(pageStart / RANK_PAGE_SIZE) + 1;
      const localCursor = this.listCursor - pageStart;
      const shown = this.rows.slice(pageStart, pageStart + RANK_PAGE_SIZE);
      const wealth = this.rankType === 1;

      shown.forEach((row, i) => {
        const hi = i === localCursor;
        const ry = listTop + i * 20;
        if (hi) {
          this.box.rect(x + 6, ry, w - 12, 18).fill({ color: CBE.select });
        }
        this.addLabel(
          row.name.slice(0, 12),
          x + 10,
          ry + 2,
          hi ? CBE.ink : CBE.inkMint,
        );
        if (wealth) {
          this.drawWealthScore(row.subtitle || "0", x + w - 10, ry + 2);
        } else if (row.subtitle) {
          this.addLabelRight(
            row.subtitle.slice(0, 10),
            x + w - 10,
            ry + 2,
            CBE.inkLabel,
          );
        }
        const abs = pageStart + i;
        this.rowHits.push(
          attachRowPointerHit(this, x + 6, ry, w - 12, 18, () => {
            this.listCursor = abs;
            this.redraw();
          }),
        );
      });

      if (pages > 1) {
        attachPageNavButtons({
          host: this,
          hits: this.rowHits,
          box: this.box,
          x: x + 8,
          y: footY - 4,
          w: w - 16,
          page,
          pages,
          addLabel: (t, lx, ly, c) => this.addLabel(t, lx, ly, c),
          onPrev: () => {
            this.listCursor = Math.max(0, pageStart - RANK_PAGE_SIZE);
            this.redraw();
          },
          onNext: () => {
            this.listCursor = Math.min(
              total - 1,
              pageStart + RANK_PAGE_SIZE,
            );
            this.redraw();
          },
        });
      }
    }

    const my =
      this.rankType >= 2
        ? this.rankMyorder > 0
          ? `我的PK ${this.rankMyorder}`
          : "我的PK 0"
        : this.rankMyorder > 0
          ? `我的名次 ${this.rankMyorder}`
          : "我的名次 -";
    this.addLabel(my, x + 10, footY, CBE.inkTip);
    this.addLabel("←→切换榜", x + w - 72, footY, CBE.inkLabel);
  }

  private drawRankTabs(x: number, y: number, w: number, h: number): void {
    const tabs = this.rankTabs.length ? this.rankTabs : RANK_DEFAULT_TABS;
    const n = tabs.length;
    const gap = 2;
    const tw = Math.floor((w - gap * (n - 1)) / n);
    for (let i = 0; i < n; i++) {
      const label = tabs[i] || `榜${i}`;
      const tx = x + i * (tw + gap);
      const active = i === this.rankType;
      this.box
        .roundRect(tx, y, tw, h, 2)
        .fill({ color: active ? CBE.bgPanelDark : RANK_TAB_BG })
        .stroke({ width: 1, color: active ? CBE.borderBright : CBE.borderDim });
      // Active: full name; inactive: first char (mall/bag tab clip).
      const text = active ? label.slice(0, 4) : label.slice(0, 1);
      this.addLabelCentered(
        text,
        tx + tw / 2,
        y + h / 2,
        active ? CBE.ink : CBE.inkMint,
      );
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, tx, y, tw, h, () => {
          if (idx === this.rankType) return;
          const delta = idx - this.rankType;
          this.moveCategory(delta);
        }),
      );
    }
  }

  /** Wealth score: copper total → gold count + jin.gif (no 银/铜). */
  private drawWealthScore(score: string, rightX: number, y: number): void {
    const gold = wealthGoldFromScore(score);
    const iw = 11;
    const ih = 8;
    if (this.jinTex) {
      const spr = uiSprite(this.jinTex, iw, ih);
      spr.position.set(rightX - iw, y + 3);
      this.addChild(spr);
      this.uiSprites.push(spr);
      this.addLabelRight(String(gold), rightX - iw - 2, y, CBE.ink);
    } else {
      this.addLabelRight(`${gold}金`, rightX, y, CBE.ink);
    }
  }

  private ensureJinIcon(): void {
    if (this.jinTex || this.jinTexLoading) return;
    this.jinTexLoading = true;
    void tryLoadUiTexture("jin.gif").then((tex) => {
      this.jinTex = tex;
      this.jinTexLoading = false;
      if (this.visible && this.kind === "rank" && this.rankType === 1) {
        this.redraw();
      }
    });
  }

  private addLabel(text: string, lx: number, ly: number, color: number): void {
    const t = new BitmapLabel();
    void t.setText(text, color);
    t.position.set(lx, ly);
    this.addChild(t);
    this.labels.push(t);
  }

  private addLabelCentered(
    text: string,
    cx: number,
    cy: number,
    color: number,
  ): void {
    const t = new BitmapLabel();
    void t.setText(text, color).then(() => {
      t.position.set(cx - t.width / 2, cy - t.height / 2);
    });
    t.position.set(cx, cy);
    this.addChild(t);
    this.labels.push(t);
  }

  private addLabelRight(
    text: string,
    rightX: number,
    ly: number,
    color: number,
  ): void {
    const t = new BitmapLabel();
    void t.setText(text, color).then(() => {
      t.position.set(rightX - t.width, ly);
    });
    t.position.set(rightX, ly);
    this.addChild(t);
    this.labels.push(t);
  }
}

function wealthGoldFromScore(score: string): number {
  const n = Number.parseInt(score, 10);
  if (!Number.isFinite(n) || n < 0) return 0;
  return Math.floor(n / COPPER_PER_GOLD);
}

function labelToAction(label: string): SocialAction {
  switch (label) {
    case "玩家信息":
      return "player_info";
    case "查看装备":
      return "view_equip";
    case "组队邀请":
      return "invite_team";
    case "加为好友":
      return "add_friend";
    case "交易":
      return "trade";
    case "发起私聊":
      return "private_chat";
    case "切磋":
      return "spar";
    case "入帮":
      return "guild_invite";
    case "离队":
      return "leave_team";
    case "刷新":
      return "refresh";
    case "邀请附近…":
      return "invite_pick";
    case "装备称号":
      return "equip_title";
    case "发送消息":
      return "send_chat";
    case "追杀":
      return "hunt_order";
    default:
      return "refresh";
  }
}
