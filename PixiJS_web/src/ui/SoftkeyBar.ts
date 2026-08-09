import { Container, Graphics } from "pixi.js";
import { CBE, LCD_HEIGHT, LCD_WIDTH, SOFTKEY_H } from "./constants";
import { BitmapLabel } from "./BitmapLabel";

export type SoftkeyLabels = {
  left?: string;
  center?: string;
  right?: string;
};

/**
 * Bottom softkeys as CBE corner buttons (继续 / 返回 / optional center).
 * Plain system text — outline only for map nameplates.
 */
export class SoftkeyBar extends Container {
  private leftBtn: Container;
  private rightBtn: Container;
  private centerBtn: Container;
  private leftLabel: BitmapLabel;
  private rightLabel: BitmapLabel;
  private centerLabel: BitmapLabel;
  private labels: SoftkeyLabels = {};
  onSoftkey: ((which: "left" | "center" | "right") => void) | null = null;

  constructor() {
    super();
    this.y = LCD_HEIGHT - SOFTKEY_H;
    this.addChild(
      new Graphics().rect(0, 0, LCD_WIDTH, SOFTKEY_H).fill({ color: CBE.bgDeep }),
    );

    this.leftBtn = makeBtn(52);
    this.rightBtn = makeBtn(52);
    this.centerBtn = makeBtn(56);
    this.leftLabel = makeLabel("");
    this.rightLabel = makeLabel("");
    this.centerLabel = makeLabel("");

    this.leftBtn.position.set(6, 4);
    this.rightBtn.position.set(LCD_WIDTH - 58, 4);
    this.centerBtn.position.set((LCD_WIDTH - 56) / 2, 4);

    this.leftLabel.anchor.set(0.5, 0.5);
    this.rightLabel.anchor.set(0.5, 0.5);
    this.centerLabel.anchor.set(0.5, 0.5);
    this.leftLabel.position.set(6 + 26, 16);
    this.rightLabel.position.set(LCD_WIDTH - 32, 16);
    this.centerLabel.position.set(LCD_WIDTH / 2, 16);

    this.addChild(
      this.leftBtn,
      this.rightBtn,
      this.centerBtn,
      this.leftLabel,
      this.rightLabel,
      this.centerLabel,
    );

    wire(this.leftBtn, () => this.labels.left && this.onSoftkey?.("left"));
    wire(this.rightBtn, () => this.labels.right && this.onSoftkey?.("right"));
    wire(this.centerBtn, () => this.labels.center && this.onSoftkey?.("center"));
  }

  setLabels(labels: SoftkeyLabels): void {
    this.labels = labels;
    void this.leftLabel.setText(labels.left ?? "", CBE.inkMint, {
      uc3: true,
      smooth: false,
      outline: false,
    });
    void this.rightLabel.setText(labels.right ?? "", CBE.inkMint, {
      uc3: true,
      smooth: false,
      outline: false,
    });
    void this.centerLabel.setText(labels.center ?? "", CBE.inkMint, {
      uc3: true,
      smooth: false,
      outline: false,
    });
    this.leftBtn.visible = !!labels.left;
    this.rightBtn.visible = !!labels.right;
    this.centerBtn.visible = !!labels.center;
  }
}

function makeBtn(w: number): Container {
  const c = new Container();
  c.addChild(
    new Graphics()
      .roundRect(0, 0, w, 24, 3)
      .fill({ color: CBE.bgDeep })
      .stroke({ width: 2, color: CBE.softkeyBorder }),
  );
  return c;
}

function makeLabel(text: string): BitmapLabel {
  const t = new BitmapLabel();
  t.show(text, CBE.inkMint, { uc3: true, smooth: false, outline: false });
  t.eventMode = "none";
  return t;
}

function wire(c: Container, fn: () => void): void {
  c.eventMode = "static";
  c.cursor = "pointer";
  c.on("pointertap", fn);
}
