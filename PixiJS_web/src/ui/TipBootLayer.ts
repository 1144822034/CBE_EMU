import { Container, Graphics, Sprite } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  paintCbePanel,
  drawLocBanner,
  makePixelText,
} from "./constants";
import { BitmapLabel, layoutSameColorRuns, UI_WRAP_CHARS } from "./BitmapLabel";
import { tryLoadUiTexture } from "./UiAssets";
import {
  BOOT_TIPS,
  TIP_TITLE,
  pickBootTip,
  type BootTip,
} from "./bootTips";

/**
 * Pre-title 「江湖支招」 boot tip — CBE shows a teal ornate panel and
 * typewriter-reveals tip text (autotest frames 000001→000002), then the
 * version/comms panel, before ui_title.actor.
 *
 * Tip text rotates from the CBE-embedded pool (not a fixed string).
 * Not wordintro.gif (that strip is hang-up benefit glyphs).
 */

export type { BootTip };
export { BOOT_TIPS };

const CHAR_MS = 90;
const HOLD_MS = 700;
const VERSION_MS = 0;

export class TipBootLayer extends Container {
  private panel = new Graphics();
  private titleText: BitmapLabel;
  private bodyHost = new Container();
  private versionHost = new Container();
  private timer: number | null = null;
  private phase: "idle" | "tip" | "hold" | "version" | "done" = "idle";
  private tipIndex = -1;
  private charIndex = 0;
  private tip: BootTip = BOOT_TIPS[0]!;
  private loadingFill: Graphics | null = null;
  onFinished: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(this.panel);
    this.titleText = makePixelText("", 14, CBE.inkTip, { crisp: true });
    this.titleText.position.set(18, 72);
    this.addChild(this.titleText, this.bodyHost, this.versionHost);
    this.versionHost.visible = false;
  }

  async mountArt(): Promise<void> {
    this.redrawPanel();
    // Version panel chrome (hidden until tip ends).
    this.versionHost.removeChildren();
    const bannerGfx = new Graphics();
    this.versionHost.addChild(bannerGfx);
    drawLocBanner(bannerGfx, this.versionHost, "获取版本信息", 8);
    const status = makePixelText("正在和服务器通讯请稍候....", 13, CBE.ink, {
      crisp: true,
    });
    status.position.set(18, 56);
    this.versionHost.addChild(status);

    const barY = 190;
    const loading = await tryLoadUiTexture("loading.gif");
    if (loading) {
      const frame = new Sprite(loading);
      frame.anchor.set(0.5, 0);
      frame.position.set(LCD_WIDTH / 2, barY);
      this.versionHost.addChild(frame);
      this.loadingFill = new Graphics();
      this.loadingFill.position.set(LCD_WIDTH / 2 - 40, barY + 9);
      this.versionHost.addChild(this.loadingFill);
    } else {
      this.loadingFill = new Graphics();
      this.loadingFill.position.set(40, barY);
      this.versionHost.addChild(this.loadingFill);
    }
  }

  /** Start tip → version → callback. Picks a random tip each open. */
  play(): void {
    this.stop();
    this.visible = true;
    this.versionHost.visible = false;
    this.titleText.visible = true;
    this.bodyHost.visible = true;
    const picked = pickBootTip(this.tipIndex);
    this.tipIndex = picked.index;
    this.tip = picked.tip;
    this.charIndex = 0;
    this.phase = "tip";
    void this.titleText.setText(TIP_TITLE, CBE.inkTip, {
      fontSize: 14,
      crisp: true,
      outline: false,
    });
    this.redrawBody(0);
    this.timer = window.setInterval(() => this.tick(), CHAR_MS);
  }

  /** Skip remaining boot tip/version and finish. */
  skip(): void {
    if (this.phase === "done" || this.phase === "idle") return;
    this.finish();
  }

  stop(): void {
    if (this.timer != null) {
      window.clearTimeout(this.timer);
      window.clearInterval(this.timer);
      this.timer = null;
    }
  }

  destroy(options?: Parameters<Container["destroy"]>[0]): void {
    this.stop();
    super.destroy(options);
  }

  private tick(): void {
    if (this.phase === "tip") {
      this.charIndex += 1;
      this.redrawBody(this.charIndex);
      if (this.charIndex >= [...this.tip.plain].length) {
        this.phase = "hold";
        this.stop();
        this.timer = window.setTimeout(() => {
          this.timer = null;
          this.showVersion();
        }, HOLD_MS) as unknown as number;
      }
      return;
    }
    if (this.phase === "version") {
      // progress pulse while “comms”
      const t = (performance.now() / 400) % (Math.PI * 2);
      const p = 0.15 + 0.75 * (0.5 + 0.5 * Math.sin(t));
      this.setLoadingProgress(p);
    }
  }

  private showVersion(): void {
    this.phase = "version";
    this.titleText.visible = false;
    this.bodyHost.visible = false;
    this.versionHost.visible = true;
    this.setLoadingProgress(0.1);
    this.stop();
    this.timer = window.setInterval(() => this.tick(), 50);
    window.setTimeout(() => this.finish(), VERSION_MS);
  }

  private finish(): void {
    this.stop();
    this.phase = "done";
    this.visible = false;
    this.onFinished?.();
  }

  private redrawPanel(): void {
    this.panel.clear();
    this.panel.rect(0, 0, LCD_WIDTH, LCD_HEIGHT).fill({ color: CBE.bgPanel });
    // Inset ornate frame matching tip shots (full LCD).
    paintCbePanel(this, this.panel, 4, 4, LCD_WIDTH - 8, LCD_HEIGHT - 8, CBE.bgPanel);
  }

  private setLoadingProgress(t: number): void {
    if (!this.loadingFill) return;
    const w = Math.max(0, Math.min(1, t)) * 72;
    this.loadingFill.clear();
    if (w > 0) this.loadingFill.rect(0, 0, w, 6).fill({ color: CBE.cream });
  }

  /**
   * Typewriter: reveal first `visibleChars` via shared same-color run layout
   * (one Label per merged run, never per glyph).
   */
  private redrawBody(visibleChars: number): void {
    this.bodyHost.removeChildren().forEach((c) => c.destroy());
    const cellW = 14;
    const lineH = 20;
    const startX = 18;
    const startY = 98;
    const runs = layoutSameColorRuns(this.tip.segments, {
      maxCols: UI_WRAP_CHARS,
      visibleChars,
    });
    for (const run of runs) {
      const t = makePixelText(run.text, 13, run.color, { crisp: true });
      t.position.set(startX + run.col * cellW, startY + run.row * lineH);
      this.bodyHost.addChild(t);
    }
  }
}
