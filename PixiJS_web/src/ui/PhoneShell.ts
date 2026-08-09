import { Application, Container } from "pixi.js";
import { LCD_HEIGHT, LCD_WIDTH } from "./constants";
import { SoftkeyBar, type SoftkeyLabels } from "./SoftkeyBar";

/**
 * Fixed 240×400 CBE LCD surface, CSS-scaled to fill #phone-wrap (no side pad).
 * On-screen softkeys live in HTML TouchControls; canvas SoftkeyBar stays hidden.
 */
export class PhoneShell {
  readonly lcd = new Container();
  readonly content = new Container();
  readonly softkeys: SoftkeyBar;
  private scale = 1;
  private labels: SoftkeyLabels = {};
  onLabels: ((labels: SoftkeyLabels) => void) | null = null;

  constructor(private readonly app: Application) {
    this.softkeys = new SoftkeyBar();
    this.softkeys.visible = false;
    this.lcd.addChild(this.content);
    this.lcd.addChild(this.softkeys);
    this.app.stage.addChild(this.lcd);
    this.layout();
    window.addEventListener("resize", () => this.layout());
    if (typeof ResizeObserver !== "undefined") {
      const wrap = document.getElementById("phone-wrap");
      if (wrap) new ResizeObserver(() => this.layout()).observe(wrap);
    }
  }

  setSoftkeys(labels: SoftkeyLabels): void {
    this.labels = labels;
    this.softkeys.setLabels(labels);
    this.onLabels?.(labels);
  }

  onSoftkey(handler: (which: "left" | "center" | "right") => void): void {
    this.softkeys.onSoftkey = handler;
  }

  layout(): void {
    // Renderer is fixed 240×400; #phone-wrap CSS alone provides display scale.
    // Do NOT multiply lcd.scale from app.screen — that + resizeTo feedback made
    // battle skill FX frame swaps look like the whole stage zooming.
    this.scale = 1;
    this.lcd.scale.set(1);
    this.lcd.position.set(0, 0);
    const wrap = document.getElementById("phone-wrap");
    if (wrap) {
      wrap.dataset.lcdX = "0";
      wrap.dataset.lcdY = "0";
      wrap.dataset.lcdScale = "1";
      wrap.dataset.lcdScreenW = String(LCD_WIDTH);
      wrap.dataset.lcdScreenH = String(LCD_HEIGHT);
    }
  }

  /** Clear content layer but keep softkeys. */
  clearContent(): void {
    this.content.removeChildren().forEach((c) => c.destroy({ children: true }));
  }
}
