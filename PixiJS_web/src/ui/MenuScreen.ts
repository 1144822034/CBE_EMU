/**
 * CBE-style two-level scene menu (UI_menu2 scroll).
 * Fixed scroll frame; cursor moves inside the white box; page buttons when
 * items exceed PAGE_ROWS — the menu never shifts upward.
 */
import { Container, Graphics } from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  paintCbePanel,
  paintPanelWorldDim,
  blockPanelPointerPassthrough,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { isSoundEnabled } from "../audio/GameBgm";
import { isPeerCosmeticsVisible } from "../resources/peerCosmeticsPref";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import { attachPageNavButtons, preloadPageNavTextures } from "./pageNav";
import {
  MENU2_CAP_H,
  MENU2_WHITE_FILL,
  MENU2_WHITE_TARGET_W,
  addMenuScrollBackgroundSliced,
  menu2CenteredLayout,
  menu2ContentRect,
  menu2ScrollFixedHeight,
  preloadMenuScrollTexture,
} from "./menuScrollUi";

export type MenuLeaf =
  | "mall"
  | "status"
  | "bag"
  | "equip"
  | "skill"
  | "title"
  | "quest"
  | "guild"
  | "nearby"
  | "friends"
  | "chat"
  | "rank"
  | "unstuck"
      | "leave_game"
  | "multi_open"
  | "change_password"
  | "leave_team"
  | "quick_repair"
  | "hangup"
  | "font"
  | "sound"
  | "peer_fx"
  | "enemy_hp_bar";

type MenuNode = {
  label: string;
  leaf?: MenuLeaf;
  children?: { label: string; leaf: MenuLeaf }[];
};

/** Native index order (main.c): 0..5. */
const ROOT: MenuNode[] = [
  {
    label: "系统设置",
    children: [
      { label: "离开游戏", leaf: "leave_game" },
      { label: "账号多开", leaf: "multi_open" },
      { label: "脱离卡死", leaf: "unstuck" },
      { label: "修改密码", leaf: "change_password" },
      { label: "离开队伍", leaf: "leave_team" },
      { label: "开关声音", leaf: "sound" },
    ],
  },
  {
    label: "个性功能",
    children: [
      { label: "快速修理", leaf: "quick_repair" },
      { label: "字体切换", leaf: "font" },
      { label: "挂机战斗", leaf: "hangup" },
      { label: "屏蔽特效", leaf: "peer_fx" },
      { label: "敌方血条", leaf: "enemy_hp_bar" },
    ],
  },
  {
    label: "社交聊天",
    children: [
      { label: "周围玩家", leaf: "nearby" },
      { label: "好友列表", leaf: "friends" },
      { label: "聊天信息", leaf: "chat" },
      { label: "排行榜", leaf: "rank" },
    ],
  },
  { label: "帮派家族", leaf: "guild" },
  {
    label: "玩家信息",
    children: [
      { label: "属性", leaf: "status" },
      { label: "背包", leaf: "bag" },
      { label: "任务", leaf: "quest" },
      { label: "装备", leaf: "equip" },
      { label: "法术", leaf: "skill" },
      { label: "称号", leaf: "title" },
    ],
  },
  { label: "江湖商城", leaf: "mall" },
];

/** Top-to-bottom on scroll (native WQVGA). */
const ROOT_DISPLAY_IDX = [5, 4, 3, 2, 1, 0];

const SCROLL_WHITE_W = MENU2_WHITE_TARGET_W;
/** Fixed Y — menu never repositions when paging or entering L2. */
const SCROLL_Y = 100;
const ROW_H = 18;
/** Visible rows inside the white parchment column. */
const PAGE_ROWS = 6;
const SCROLL_H = menu2ScrollFixedHeight(PAGE_ROWS, ROW_H);
const INK = 0x181818;
const INK_HI = 0xffffff;
const SEL_FILL = 0x505850;

export class MenuScreen extends Container {
  private level: 1 | 2 = 1;
  /** Index into ROOT_DISPLAY_IDX (L1) or sub row (L2). */
  private menuCursor = 0;
  private subParent: MenuNode | null = null;
  private scrollHost = new Container();
  private dim = new Graphics();
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private redrawGen = 0;

  onSelect: ((leaf: MenuLeaf, label: string) => void) | null = null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.paintDim();
    this.addChild(this.dim);
    this.scrollHost.eventMode = "none";
    this.addChild(this.scrollHost);
    this.addChild(this.box);
    blockPanelPointerPassthrough(this);
    preloadMenuScrollTexture(() => this.redraw());
    preloadPageNavTextures(() => this.redraw());
    this.reset();
  }

  private paintDim(): void {
    paintPanelWorldDim(this.dim);
  }

  private remountChrome(): void {
    this.dim = replaceGraphics(this.dim);
    this.paintDim();
    this.box = replaceGraphics(this.box);
  }

  reset(): void {
    this.level = 1;
    this.menuCursor = 0;
    this.subParent = null;
    this.redraw();
  }

  move(delta: number): void {
    const total = this.entryCount();
    this.menuCursor = (this.menuCursor + delta + total) % total;
    this.redraw();
  }

  confirm(): void {
    if (this.level === 1) {
      const node = this.rootNode();
      if (node.leaf) {
        this.onSelect?.(node.leaf, node.label);
        return;
      }
      this.subParent = node;
      this.menuCursor = 0;
      this.level = 2;
      this.redraw();
      return;
    }
    const kids = this.subParent?.children ?? [];
    if (this.menuCursor >= kids.length) {
      this.level = 1;
      this.subParent = null;
      this.menuCursor = 0;
      this.redraw();
      return;
    }
    const child = kids[this.menuCursor]!;
    this.onSelect?.(child.leaf, child.label);
  }

  cancel(): void {
    if (this.level === 2) {
      this.level = 1;
      this.subParent = null;
      this.menuCursor = 0;
      this.redraw();
      return;
    }
    this.onClose?.();
  }

  private entryCount(): number {
    if (this.level === 1) return ROOT_DISPLAY_IDX.length;
    return (this.subParent?.children?.length ?? 0) + 1;
  }

  private rootNode(): MenuNode {
    return ROOT[ROOT_DISPLAY_IDX[this.menuCursor]!]!;
  }

  private childLabel(leaf: MenuLeaf, label: string): string {
    if (leaf === "sound") {
      return isSoundEnabled() ? "关闭声音" : "开启声音";
    }
    if (leaf === "peer_fx") {
      return isPeerCosmeticsVisible() ? "屏蔽特效" : "显示特效";
    }
    return label;
  }

  private allLabels(): string[] {
    if (this.level === 1) {
      return ROOT_DISPLAY_IDX.map((i) => ROOT[i]!.label);
    }
    const kids = this.subParent?.children ?? [];
    return [
      ...kids.map((c) => this.childLabel(c.leaf, c.label)),
      "返回",
    ];
  }

  /** Page-aligned window so 上一页 / 下一页 jump whole pages. */
  private pageStart(total: number): number {
    if (total <= PAGE_ROWS) return 0;
    return Math.floor(this.menuCursor / PAGE_ROWS) * PAGE_ROWS;
  }

  private async addCenteredLabel(
    text: string,
    cx: number,
    y: number,
    color: number,
    gen: number,
    maxWidth: number,
  ): Promise<void> {
    const lab = new BitmapLabel();
    lab.eventMode = "none";
    lab.anchor.set(0.5, 0);
    lab.position.set(cx, y);
    await lab.setText(text, color, {
      outline: false,
      maxWidth: maxWidth - 2,
      bold: true,
    });
    if (gen !== this.redrawGen) {
      lab.destroy();
      return;
    }
    this.addChild(lab);
    this.labels.push(lab);
  }

  private addNavLabel(text: string, x: number, y: number, color: number): void {
    const lab = new BitmapLabel();
    lab.eventMode = "none";
    lab.position.set(x, y);
    void lab.setText(text, color, { outline: false });
    this.addChild(lab);
    this.labels.push(lab);
  }


  /** webglcontextrestored: remount paintCbePanel Graphics; redraw if open. */
  nudgeAfterGpuResume(): void {
    this.remountChrome();
    if (this.visible) this.redraw();
  }

  /** Leave menu: drop UC3 labels so they do not pin glyph cache while world runs. */
  hide(): void {
    this.visible = false;
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.scrollHost.removeChildren();
    this.box.clear();
  }

  private redraw(): void {
    const gen = ++this.redrawGen;
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.box.clear();
    this.scrollHost.removeChildren().forEach((c) =>
      c.destroy({ children: true }),
    );

    const layout = menu2CenteredLayout(LCD_WIDTH, SCROLL_WHITE_W);
    const x = layout.scrollX;
    const SCROLL_W = layout.scrollW;
    const content = menu2ContentRect(x, SCROLL_WHITE_W);
    const y = SCROLL_Y;
    const labels = this.allLabels();
    const total = labels.length;
    const start = this.pageStart(total);
    const visible = labels.slice(start, start + PAGE_ROWS);
    const pages = Math.max(1, Math.ceil(total / PAGE_ROWS));
    const page = Math.floor(start / PAGE_ROWS) + 1;

    preloadMenuScrollTexture();
    if (
      !addMenuScrollBackgroundSliced(
        this.scrollHost,
        x,
        y,
        SCROLL_H,
        SCROLL_WHITE_W,
      )
    ) {
      paintCbePanel(this, this.box, x, y, SCROLL_W, SCROLL_H, CBE.bgPanel);
    }

    const bodyTop = y + MENU2_CAP_H;
    const bodyH = SCROLL_H - MENU2_CAP_H * 2;
    // Center the visible row block in the parchment body (not top-aligned).
    const usedH = visible.length * ROW_H;
    const top = bodyTop + Math.max(0, Math.floor((bodyH - usedH) / 2));
    this.box.rect(content.x, bodyTop, content.w, bodyH).fill({ color: MENU2_WHITE_FILL });
    visible.forEach((label, vi) => {
      const abs = start + vi;
      const iy = top + vi * ROW_H;
      const hi = abs === this.menuCursor;
      if (hi) {
        this.box.rect(content.x, iy, content.w, ROW_H - 1).fill({ color: SEL_FILL });
      }
      void this.addCenteredLabel(
        label,
        content.cx,
        iy + 1,
        hi ? INK_HI : INK,
        gen,
        content.w,
      );
      this.rowHits.push(
        attachRowPointerHit(this, content.x, iy, content.w, ROW_H - 1, () => {
          this.menuCursor = abs;
          this.redraw();
          this.confirm();
        }),
      );
    });

    if (pages > 1) {
      const navW = Math.min(LCD_WIDTH - 16, 180);
      const navX = Math.floor((LCD_WIDTH - navW) / 2);
      attachPageNavButtons({
        host: this,
        hits: this.rowHits,
        box: this.box,
        x: navX,
        y: y + SCROLL_H + 6,
        w: navW,
        page,
        pages,
        addLabel: (t, lx, ly, c) => this.addNavLabel(t, lx, ly, c),
        onPrev: () => {
          this.menuCursor = Math.max(0, start - PAGE_ROWS);
          this.redraw();
        },
        onNext: () => {
          this.menuCursor = Math.min(total - 1, start + PAGE_ROWS);
          this.redraw();
        },
      });
    }
  }
}
