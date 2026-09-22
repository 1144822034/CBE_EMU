/**
 * Entertainment entry list (棋牌/音乐/视频) — Yinhe 时辰.lua URLs.
 */
import { Container, Graphics } from "pixi.js";
import { CBE, LCD_HEIGHT, LCD_WIDTH, SOFTKEY_H } from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import { replaceGraphics } from "../render/gpuResume";
import {
  listShellBounds,
  paintListShellBody,
  paintListShellChrome,
  type ListShellCtx,
} from "./listPanelShell";

export type EntertainmentEntry = {
  id: string;
  label: string;
  url: string;
  /**
   * When set, open a native video player against this redirect API
   * instead of iframe-embedding `url` (sites like nrzj block / don't fit iframe).
   */
  videoApi?: string;
  /**
   * Resolve playable mp4 via JSON (`url` / `msg` fields) instead of treating
   * videoApi as a media redirect. Needed when APK WebView hangs on 302 chains.
   */
  videoApiJson?: boolean;
  /** Wider inset for music players (prev/next / lyrics hit targets). */
  panel?: "mall" | "wide";
  /** Open in-game panel instead of web overlay. */
  nativePanel?: "guess" | "fruit";
};

/** Same catalog as 银河西游 时辰.lua qipai / music / video entries. */
export const ENTERTAINMENT_ENTRIES: EntertainmentEntry[] = [
  {
    id: "guess",
    label: "猜猜看",
    url: "",
    nativePanel: "guess",
  },
  {
    id: "fruit",
    label: "水果机",
    url: "",
    nativePanel: "fruit",
  },
  {
    id: "qipai",
    label: "棋牌选择",
    url: "https://game.hullqin.cn/ddz",
  },
  {
    id: "music",
    label: "在线音乐",
    url: "https://www.qqmp3.vip/",
    panel: "wide",
  },
  {
    id: "beauty",
    label: "美女视频",
    url: "https://v.nrzj.vip/",
    videoApi: "https://v.nrzj.vip/video.php",
  },
  {
    id: "tiktok",
    label: "TikTok",
    url: "https://v.nrzj.vip/tiktok/",
    videoApi: "https://v.nrzj.vip/tiktok/video.php",
  },
  {
    id: "douyin",
    label: "抖音",
    url: "https://api.qemao.com/api/douyin/",
    videoApi: "https://api.qemao.com/api/douyin/?type=json",
    videoApiJson: true,
  },
  {
    id: "kuaishou",
    label: "快手",
    // 外开仍指向原站；播放改走 nrzj 快手池（ksxjj.txqq.pro/video.php 实测只轮换 2 条）。
    url: "http://ksxjj.txqq.pro/",
    videoApi: "https://v.nrzj.vip/video.php?type=ks",
  },
  {
    id: "rewu",
    label: "在线热舞",
    url: "https://smoba.xin/",
    videoApi: "https://smoba.xin/api.php",
  },
];

export class EntertainmentPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private listCursor = 0;
  private entries: EntertainmentEntry[] = ENTERTAINMENT_ENTRIES.slice();

  onPick: ((entry: EntertainmentEntry) => void) | null = null;
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

  open(entries?: EntertainmentEntry[]): void {
    this.entries =
      entries && entries.length > 0
        ? entries.slice()
        : ENTERTAINMENT_ENTRIES.slice();
    this.listCursor = 0;
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
    return { left: "打开", right: "关闭" };
  }

  move(delta: number): void {
    const n = this.entries.length;
    if (n <= 0) return;
    this.listCursor = (this.listCursor + delta + n) % n;
    this.redraw();
  }

  confirm(): void {
    const entry = this.entries[this.listCursor];
    if (entry) this.onPick?.(entry);
  }

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

  private addLabelCentered(
    text: string,
    cx: number,
    cy: number,
    color: number,
  ): void {
    const t = new BitmapLabel();
    t.anchor.set(0.5, 0.5);
    t.position.set(Math.round(cx), Math.round(cy));
    t.show(text, color, { outline: false, smooth: false });
    this.box.addChild(t);
    this.labels.push(t);
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
    this.box.removeChildren();

    const bounds = listShellBounds();
    const ctx: ListShellCtx = {
      host: this,
      box: this.box,
      hits: this.rowHits,
      addLabel: (text, x, y, color) => this.addLabel(text, x, y, color),
      addLabelCentered: (text, cx, cy, color) =>
        this.addLabelCentered(text, cx, cy, color),
    };
    paintListShellChrome(ctx, "娱乐", CBE.bgDeep, bounds);
    const listTop = bounds.y + 32;
    const footY = bounds.y + bounds.h - 28;
    paintListShellBody({
      ctx,
      x: bounds.x + 6,
      listTop,
      w: bounds.w - 12,
      footY,
      rows: this.entries.map((e) => ({ name: e.label })),
      cursor: this.listCursor,
      pageSize: this.entries.length,
      paging: "none",
      emptyText: "(无入口)",
      onRow: (absIndex) => {
        this.listCursor = absIndex;
        this.redraw();
        this.confirm();
      },
    });
    this.addLabelCentered(
      "部分站点可能拒绝内嵌",
      bounds.x + bounds.w / 2,
      footY + 12,
      CBE.inkLabel,
    );
  }
}
