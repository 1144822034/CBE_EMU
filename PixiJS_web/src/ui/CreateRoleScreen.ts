import { Container, Graphics, Sprite, Texture } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  drawCbePanel,
  drawLocBanner,
  makePixelText,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { ChatIme } from "./ChatIme";
import {
  actorResourceForJobSex,
  composeMapActor,
  loadActor,
} from "../resources/actor";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";

const JOBS = ["天机", "幻剑", "鬼道"];
const SEXES = ["男", "女"];

type Field = "sex" | "job" | "name";

const bodyCache = new Map<string, Texture>();

/** Leave a strip under the CBE panel so footer tip is not clipped by the frame. */
const PANEL_TOP = 40;
const TIP_STRIP_H = 24;
const PANEL_H = LCD_HEIGHT - SOFTKEY_H - PANEL_TOP - TIP_STRIP_H;
const FOOTER_TIP_Y = PANEL_TOP + PANEL_H + 5;

/**
 * Title create-role form: sex → job → name (mmTitle create rows).
 * Name uses ChatIme overlay for mobile soft keyboard.
 */
export class CreateRoleScreen extends Container {
  private field: Field = "sex";
  private sex = 0;
  private jobIndex = 0;
  private nameValue = "";
  private tipText: BitmapLabel;
  private rowsHost = new Container();
  private previewHost = new Container();
  private bannerGfx = new Graphics();
  private frame = new Graphics();
  private ime = new ChatIme();
  private toast: PanelTipState = createPanelTipState();
  /** Name field box in LCD coords (label sits to the left, not under the box). */
  private nameBox = { x: 72, y: 0, w: LCD_WIDTH - 96, h: 26 };
  /** Bumped on open/close so async preview redraw cannot remount IME after leave. */
  private openGen = 0;

  onSubmit: ((jobIndex: number, sex: number, name: string) => void) | null = null;
  onCancel: (() => void) | null = null;

  constructor() {
    super();
    this.addChild(
      new Graphics().rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H).fill({ color: CBE.bgDeep }),
    );
    this.addChild(this.bannerGfx);
    drawLocBanner(this.bannerGfx, this, "创建角色", 8);
    drawCbePanel(this.frame, 6, PANEL_TOP, LCD_WIDTH - 12, PANEL_H, CBE.bgDeep);
    this.addChild(this.frame);
    this.addChild(this.previewHost, this.rowsHost);

    this.tipText = makePixelText("左右改选项 · 点名字输入", 11, CBE.inkTip);
    this.tipText.position.set(14, FOOTER_TIP_Y);
    this.addChild(this.tipText);

    this.ime.onChange = (v) => {
      if (!this.visible) return;
      this.nameValue = v.slice(0, 14);
      this.redraw();
    };
    this.ime.onSubmit = () => this.confirm();
  }

  open(): void {
    this.openGen += 1;
    this.field = "sex";
    this.sex = 0;
    this.jobIndex = 0;
    this.nameValue = "";
    this.tipText.text = "左右改选项 · 点名字输入";
    clearPanelTip(this.toast);
    this.redraw();
    void this.refreshPreview();
    this.syncNameIme();
  }

  close(): void {
    this.openGen += 1;
    clearPanelTip(this.toast);
    this.ime.hide();
  }

  /** Drop name HTML overlay when leaving create-role (showPhase / cancel). */
  releaseIme(): void {
    this.close();
  }

  destroy(options?: Parameters<Container["destroy"]>[0]): void {
    this.openGen += 1;
    clearPanelTip(this.toast);
    this.ime.destroy();
    super.destroy(options);
  }

  setTip(text: string): void {
    this.tipText.text = text;
  }

  /** Centered opaque toast (e.g. 名已被注册). */
  showTip(text: string, ms = 1500): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (this.visible) this.redraw();
    }, ms);
  }

  move(delta: number): void {
    const order: Field[] = ["sex", "job", "name"];
    const i = order.indexOf(this.field);
    this.field = order[(i + delta + order.length) % order.length]!;
    this.redraw();
    if (this.field === "name") this.focusName();
    else this.ime.blur();
  }

  /** Left/right on sex/job; on name opens IME. */
  cycle(delta: number): void {
    if (this.field === "sex") {
      this.sex = this.sex ? 0 : 1;
      void this.refreshPreview();
    } else if (this.field === "job") {
      this.jobIndex = (this.jobIndex + delta + JOBS.length) % JOBS.length;
      void this.refreshPreview();
    } else {
      this.focusName();
      return;
    }
    this.redraw();
  }

  confirm(): void {
    if (this.field !== "name" && !this.nameValue.trim()) {
      this.field = "name";
      this.redraw();
      this.focusName();
      this.setTip("请输入角色名");
      return;
    }
    const name = this.nameValue.trim();
    if (!name) {
      this.setTip("请输入角色名");
      this.focusName();
      return;
    }
    this.onSubmit?.(this.jobIndex, this.sex, name);
  }

  cancel(): void {
    if (tryDismissPanelTipOnSoftRight(this.toast)) return;
    this.close();
    this.onCancel?.();
  }

  private focusName(): void {
    if (!this.visible) return;
    this.field = "name";
    this.redraw();
    this.syncNameIme();
    this.ime.focus(this.nameValue);
  }

  private syncNameIme(): void {
    if (this.visible) {
      this.ime.show(this.nameBox);
      this.ime.setValue(this.nameValue);
      if (this.field !== "name") this.ime.blur();
    } else {
      this.ime.hide();
    }
  }

  private async refreshPreview(): Promise<void> {
    const gen = this.openGen;
    const key = actorResourceForJobSex(this.jobIndex, this.sex);
    if (!bodyCache.has(key)) {
      try {
        const loaded = await loadActor(key);
        const frame = composeMapActor(loaded, 3, false, 0);
        if (frame && frame.width >= 4) {
          const canvas = document.createElement("canvas");
          canvas.width = frame.width;
          canvas.height = frame.height;
          canvas.getContext("2d")!.putImageData(
            new ImageData(new Uint8ClampedArray(frame.rgba), frame.width, frame.height),
            0,
            0,
          );
          const tex = Texture.from(canvas);
          tex.source.scaleMode = "nearest";
          bodyCache.set(key, tex);
        }
      } catch (e) {
        console.warn("[create-role] body", key, e);
      }
    }
    if (gen !== this.openGen || !this.visible) return;
    this.redraw();
  }

  private redraw(): void {
    this.rowsHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.previewHost.removeChildren().forEach((c) => c.destroy({ children: true }));

    const key = actorResourceForJobSex(this.jobIndex, this.sex);
    const body = bodyCache.get(key);
    if (body) {
      const spr = new Sprite(body);
      const maxH = 96;
      const maxW = 56;
      const sx = Math.min(maxW / body.width, maxH / body.height);
      spr.scale.set(sx);
      spr.anchor.set(0.5, 1);
      spr.position.set(LCD_WIDTH / 2, 148);
      this.previewHost.addChild(spr);
    }

    const rows: { field: Field; label: string; value: string }[] = [
      { field: "sex", label: "性别", value: SEXES[this.sex] ?? "?" },
      { field: "job", label: "职业", value: JOBS[this.jobIndex] ?? "?" },
      {
        field: "name",
        label: "名字",
        value: this.nameValue || "点此输入",
      },
    ];

    // Label left + value box right — full-width boxes used to cover the labels.
    const labelX = 20;
    const boxX = 72;
    const boxW = LCD_WIDTH - boxX - 20;
    const boxH = 26;
    const startY = 160;
    const rowPitch = 34;
    rows.forEach((row, i) => {
      const y = startY + i * rowPitch;
      const focused = this.field === row.field;
      if (row.field === "name") {
        this.nameBox = { x: boxX, y, w: boxW, h: boxH };
      }

      const lab = makePixelText(row.label, 12, focused ? CBE.inkTip : CBE.inkLabel);
      lab.position.set(labelX, y + 5);
      this.rowsHost.addChild(lab);

      const g = new Graphics();
      g.rect(boxX, y, boxW, boxH).fill({ color: focused ? CBE.select : CBE.bgPanelDark });
      g.rect(boxX, y, boxW, boxH).stroke({
        width: 1,
        color: focused ? CBE.borderBright : CBE.borderDim,
      });
      g.eventMode = "static";
      g.cursor = row.field === "name" ? "text" : "pointer";
      g.on("pointertap", () => {
        this.field = row.field;
        if (row.field === "name") this.focusName();
        else {
          this.ime.blur();
          this.cycle(1);
        }
        this.redraw();
      });
      this.rowsHost.addChild(g);

      const dim = row.field === "name" && !this.nameValue;
      const val = makePixelText(row.value, 13, dim ? CBE.borderDim : CBE.ink);
      val.position.set(boxX + 8, y + 5);
      val.eventMode = "none";
      this.rowsHost.addChild(val);
    });

    this.syncNameIme();
    drawPanelTipOverlay(this, this.toast);
  }
}
