/**
 * CBE-style two-level scene menu (scroll list).
 * L1: 江湖商城 / 玩家信息 / 帮派家族 / 社交聊天 / 系统设置
 */
import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  drawCbePanel,
  makePixelText,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { isSoundEnabled } from "../audio/GameBgm";
import { isPeerCosmeticsVisible } from "../resources/peerCosmeticsPref";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

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
  | "leave_team"
  | "quick_repair"
  | "hangup"
  | "font"
  | "sound"
  | "peer_fx";

type MenuNode = {
  label: string;
  /** Direct leaf — confirm opens immediately. */
  leaf?: MenuLeaf;
  children?: { label: string; leaf: MenuLeaf }[];
};

const ROOT: MenuNode[] = [
  { label: "江湖商城", leaf: "mall" },
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
  { label: "帮派家族", leaf: "guild" },
  {
    label: "社交聊天",
    children: [
      { label: "周围玩家", leaf: "nearby" },
      { label: "好友列表", leaf: "friends" },
      { label: "聊天信息", leaf: "chat" },
      { label: "排行榜", leaf: "rank" },
    ],
  },
  {
    label: "系统设置",
    children: [
      { label: "离开游戏", leaf: "leave_game" },     
      { label: "脱离卡死", leaf: "unstuck" },
      { label: "离开队伍", leaf: "leave_team" },
      { label: "快速修理", leaf: "quick_repair" },
      { label: "开关声音", leaf: "sound" },
      { label: "屏蔽特效", leaf: "peer_fx" },
      { label: "字体切换", leaf: "font" },
      { label: "挂机战斗", leaf: "hangup" },
    ],
  },
];

export class MenuScreen extends Container {
  private level: 1 | 2 = 1;
  private rootCursor = 0;
  private subCursor = 0;
  private subParent: MenuNode | null = null;
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];

  onSelect: ((leaf: MenuLeaf, label: string) => void) | null = null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    const dim = new Graphics()
      .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
      .fill({ color: 0x000000, alpha: 0.35 });
    this.addChild(dim);
    this.addChild(this.box);
    this.eventMode = "static";
    this.reset();
  }

  /** Open at root level (call when entering menu phase). */
  reset(): void {
    this.level = 1;
    this.rootCursor = 0;
    this.subCursor = 0;
    this.subParent = null;
    this.redraw();
  }

  move(delta: number): void {
    if (this.level === 1) {
      const n = ROOT.length;
      this.rootCursor = (this.rootCursor + delta + n) % n;
    } else {
      const kids = this.subParent?.children ?? [];
      const n = kids.length + 1; /* +返回 */
      this.subCursor = (this.subCursor + delta + n) % n;
    }
    this.redraw();
  }

  confirm(): void {
    if (this.level === 1) {
      const node = ROOT[this.rootCursor]!;
      if (node.leaf) {
        this.onSelect?.(node.leaf, node.label);
        return;
      }
      this.subParent = node;
      this.subCursor = 0;
      this.level = 2;
      this.redraw();
      return;
    }
    const kids = this.subParent?.children ?? [];
    if (this.subCursor >= kids.length) {
      this.level = 1;
      this.subParent = null;
      this.redraw();
      return;
    }
    const child = kids[this.subCursor]!;
    this.onSelect?.(child.leaf, child.label);
  }

  cancel(): void {
    if (this.level === 2) {
      this.level = 1;
      this.subParent = null;
      this.redraw();
      return;
    }
    this.onClose?.();
  }

  private currentLabels(): string[] {
    if (this.level === 1) return ROOT.map((n) => n.label);
    const kids = this.subParent?.children ?? [];
    return [
      ...kids.map((c) =>
        c.leaf === "sound"
          ? isSoundEnabled()
            ? "关闭声音"
            : "开启声音"
          : c.leaf === "peer_fx"
            ? isPeerCosmeticsVisible()
              ? "屏蔽特效"
              : "显示特效"
            : c.label,
      ),
      "返回",
    ];
  }

  private redraw(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.box.clear();

    const labels = this.currentLabels();
    const cursor = this.level === 1 ? this.rootCursor : this.subCursor;
    const rowH = 20;
    const w = 132;
    const h = labels.length * rowH + 28;
    const x = Math.floor((LCD_WIDTH - w) / 2);
    const y = Math.floor((LCD_HEIGHT - SOFTKEY_H - h) / 2);
    drawCbePanel(this.box, x, y, w, h, CBE.bgPanel);

    /* Scroll header strip like CBE */
    this.box.rect(x + 4, y + 4, w - 8, 14).fill({ color: CBE.bgPanelDark });
    const head =
      this.level === 1
        ? "菜单"
        : (this.subParent?.label ?? "子菜单").slice(0, 8);
    const ht = makePixelText(head, 11, CBE.inkLabel);
    ht.position.set(x + 10, y + 5);
    this.addChild(ht);
    this.labels.push(ht);

    labels.forEach((label, i) => {
      const iy = y + 22 + i * rowH;
      if (i === cursor) {
        this.box.rect(x + 5, iy, w - 10, rowH - 2).fill({ color: CBE.select });
      }
      const t = makePixelText(
        label,
        12,
        i === cursor ? CBE.ink : CBE.inkMint,
      );
      t.position.set(x + 14, iy + 2);
      t.eventMode = "none";
      this.addChild(t);
      this.labels.push(t);
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, x + 5, iy, w - 10, rowH - 2, () => {
          if (this.level === 1) this.rootCursor = idx;
          else this.subCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
    });
  }
}
