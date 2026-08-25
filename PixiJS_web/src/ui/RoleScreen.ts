import { Container, Graphics, Sprite, Texture } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  paintCbePanel,
  preloadCbeFlowerCorners,
  drawLocBanner,
  makePixelText,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { ChatIme } from "./ChatIme";
import type { RoleRow } from "../net/protocol";
import { TITLE_ROLE_MAX } from "../net/protocol";
import {
  actorResourceForJobSex,
  composeMapActor,
  loadActor,
} from "../resources/actor";

const JOBS = ["天机", "幻剑", "鬼道"];
/** Full-body front stand (CBE dir=3), keyed by actor resource. */
const bodyCache = new Map<string, Texture>();

/** Leave a strip under the CBE panel so footer tip is not clipped by the frame. */
const PANEL_TOP = 40;
const TIP_STRIP_H = 24;
const PANEL_H = LCD_HEIGHT - SOFTKEY_H - PANEL_TOP - TIP_STRIP_H;
const FOOTER_TIP_Y = PANEL_TOP + PANEL_H + 5;

/**
 * Character select —「选择人物登录」slots with full-body stand preview.
 * OK opens delete confirm: type the role name, then 确定.
 */
export class RoleScreen extends Container {
  private roles: RoleRow[] = [];
  private cursorIndex = 0;
  private rowsHost = new Container();
  private confirmHost = new Container();
  private bannerGfx = new Graphics();
  private frame = new Graphics();
  private tipLabel: BitmapLabel;
  private deleting: RoleRow | null = null;
  private confirmValue = "";
  private ime = new ChatIme();
  private confirmBox = { x: 36, y: 210, w: LCD_WIDTH - 72, h: 28 };

  onConfirm: (() => void) | null = null;
  /** Fired when the create-role sentinel is confirmed. */
  onCreate: (() => void) | null = null;
  /** Fired after name-confirm when delete should proceed. */
  onDelete: (() => void) | null = null;
  /** Softkey labels switch when confirm overlay opens/closes. */
  onDeleteConfirmChange: ((open: boolean) => void) | null = null;

  constructor() {
    super();
    this.addChild(
      new Graphics().rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H).fill({ color: CBE.bgDeep }),
    );
    this.addChild(this.bannerGfx);
    drawLocBanner(this.bannerGfx, this, "选择人物登录", 8);
    this.paintFrame();
    preloadCbeFlowerCorners(() => {
      if (this.visible) this.paintFrame();
    });
    this.addChild(this.frame);
    this.addChild(this.rowsHost, this.confirmHost);

    this.tipLabel = makePixelText("OK键删除 · 需输入角色名", 11, CBE.inkTip);
    this.tipLabel.anchor.set(0.5, 0);
    this.tipLabel.position.set(LCD_WIDTH / 2, FOOTER_TIP_Y);
    this.addChild(this.tipLabel);

    this.ime.onChange = (v) => {
      this.confirmValue = v.slice(0, 14);
      if (this.deleting) this.paintConfirm();
    };
    this.ime.onSubmit = () => this.submitDeleteConfirm();
    this.confirmHost.visible = false;
  }

  async mountArt(): Promise<void> {
    // Bodies come from hW_*/h_* map actors (prefetchBodies).
  }

  private paintFrame(): void {
    this.frame.clear();
    paintCbePanel(
      this,
      this.frame,
      6,
      PANEL_TOP,
      LCD_WIDTH - 12,
      PANEL_H,
      CBE.bgDeep,
    );
  }

  setRoles(roles: RoleRow[]): void {
    this.cancelDeleteConfirm();
    this.roles = roles;
    if (this.cursorIndex > this.roles.length) this.cursorIndex = 0;
    this.redraw();
    void this.prefetchBodies();
  }

  /** Prefer selecting a just-created role id. */
  selectRoleId(roleId: number): void {
    const i = this.roles.findIndex((r) => r.roleId === roleId);
    this.cursorIndex = i >= 0 ? i : 0;
    this.redraw();
  }

  get createSelected(): boolean {
    return this.cursorIndex >= this.roles.length;
  }

  isDeleteConfirming(): boolean {
    return this.deleting != null;
  }

  private async prefetchBodies(): Promise<void> {
    await Promise.all(
      this.roles.map(async (role) => {
        const name = actorResourceForJobSex(role.jobIndex, role.sex);
        if (bodyCache.has(name)) return;
        try {
          const loaded = await loadActor(name);
          // CBE dir 3 = front stand (toward viewer).
          const frame = composeMapActor(loaded, 3, false, 0);
          if (!frame || frame.width < 4) return;
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
          bodyCache.set(name, tex);
        } catch (e) {
          console.warn("[role] body", name, e);
        }
      }),
    );
    this.redraw();
  }

  move(delta: number): void {
    if (this.deleting) return;
    const n = this.roles.length + (this.roles.length < TITLE_ROLE_MAX ? 1 : 0);
    if (n <= 0) return;
    this.cursorIndex = (this.cursorIndex + delta + n) % n;
    this.redraw();
  }

  confirm(): void {
    if (this.deleting) {
      this.submitDeleteConfirm();
      return;
    }
    if (this.cursorIndex < this.roles.length) this.onConfirm?.();
    else if (this.roles.length < TITLE_ROLE_MAX) this.onCreate?.();
  }

  /** OK / D-pad center — open delete name-confirm (create sentinel ignored). */
  deleteSelected(): void {
    if (this.deleting) {
      this.submitDeleteConfirm();
      return;
    }
    if (this.cursorIndex >= this.roles.length) {
      this.setTip("请先选中要删除的角色");
      return;
    }
    this.openDeleteConfirm(this.roles[this.cursorIndex]!);
  }

  cancelDeleteConfirm(): void {
    if (!this.deleting) return;
    this.deleting = null;
    this.confirmValue = "";
    this.ime.hide();
    this.confirmHost.visible = false;
    this.confirmHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.setTip("OK键删除 · 需输入角色名");
    this.onDeleteConfirmChange?.(false);
  }

  get selected(): RoleRow | null {
    return this.deleting ?? this.roles[this.cursorIndex] ?? null;
  }

  setTip(text: string): void {
    this.tipLabel.text = text;
  }

  destroy(options?: Parameters<Container["destroy"]>[0]): void {
    this.ime.destroy();
    super.destroy(options);
  }

  private openDeleteConfirm(role: RoleRow): void {
    this.deleting = role;
    this.confirmValue = "";
    this.confirmHost.visible = true;
    this.paintConfirm();
    this.ime.show(this.confirmBox);
    this.ime.focus("");
    this.setTip("输入角色名后按确定删除");
    this.onDeleteConfirmChange?.(true);
  }

  private submitDeleteConfirm(): void {
    if (!this.deleting) return;
    const typed = this.confirmValue.trim();
    if (!typed) {
      this.setTip("请输入角色名确认删除");
      this.ime.focus(this.confirmValue);
      return;
    }
    if (typed !== this.deleting.name) {
      this.setTip("角色名不一致，请重新输入");
      this.ime.focus(this.confirmValue);
      return;
    }
    this.cancelDeleteConfirm();
    this.onDelete?.();
  }

  private paintConfirm(): void {
    this.confirmHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    if (!this.deleting) return;

    const panel = new Graphics();
    const px = 16;
    const py = 120;
    const pw = LCD_WIDTH - 32;
    const ph = 150;
    panel.roundRect(px, py, pw, ph, 6).fill({ color: CBE.bgPanel });
    panel.roundRect(px, py, pw, ph, 6).stroke({ width: 2, color: CBE.borderBright });
    this.confirmHost.addChild(panel);

    const title = makePixelText("删除确认", 14, CBE.inkTip);
    title.anchor.set(0.5, 0);
    title.position.set(LCD_WIDTH / 2, py + 12);
    this.confirmHost.addChild(title);

    const warn = makePixelText(`将删除「${this.deleting.name}」`, 12, CBE.ink);
    warn.anchor.set(0.5, 0);
    warn.position.set(LCD_WIDTH / 2, py + 36);
    this.confirmHost.addChild(warn);

    const hint = makePixelText("请输入角色名确认", 11, CBE.inkLabel);
    hint.anchor.set(0.5, 0);
    hint.position.set(LCD_WIDTH / 2, py + 58);
    this.confirmHost.addChild(hint);

    const boxX = 36;
    const boxY = py + 82;
    const boxW = LCD_WIDTH - 72;
    const boxH = 28;
    this.confirmBox = { x: boxX, y: boxY, w: boxW, h: boxH };
    const box = new Graphics();
    box.rect(boxX, boxY, boxW, boxH).fill({ color: CBE.select });
    box.rect(boxX, boxY, boxW, boxH).stroke({ width: 1, color: CBE.borderBright });
    box.eventMode = "static";
    box.cursor = "text";
    box.on("pointertap", () => this.ime.focus(this.confirmValue));
    this.confirmHost.addChild(box);

    const shown = this.confirmValue || "点此输入角色名";
    const val = makePixelText(shown, 13, this.confirmValue ? CBE.ink : CBE.borderDim);
    val.position.set(boxX + 8, boxY + 6);
    val.eventMode = "none";
    this.confirmHost.addChild(val);

    if (this.visible) {
      this.ime.show(this.confirmBox);
      this.ime.setValue(this.confirmValue);
    }
  }

  private redraw(): void {
    this.rowsHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    // Taller slots so full-body stand fits beside name/job/level.
    const slotH = 72;
    const startY = 48;
    const showCreate = this.roles.length < TITLE_ROLE_MAX;
    const total = this.roles.length + (showCreate ? 1 : 0);
    const maxShow = Math.min(4, total);
    const bodyMaxH = slotH - 10;
    const bodyMaxW = 44;
    const scroll =
      this.cursorIndex >= maxShow ? this.cursorIndex - maxShow + 1 : 0;

    for (let view = 0; view < maxShow; view++) {
      const i = view + scroll;
      if (i >= total) break;
      const y = startY + view * (slotH + 4);
      const selected = i === this.cursorIndex;
      const slot = new Graphics();
      const fill = selected ? CBE.bgPanel : CBE.bgPanelDark;
      slot.roundRect(14, y, LCD_WIDTH - 28, slotH, 6).fill({ color: fill });
      slot.roundRect(14, y, LCD_WIDTH - 28, slotH, 6).stroke({
        width: selected ? 2 : 1,
        color: selected ? CBE.borderBright : CBE.borderDim,
      });
      slot.eventMode = this.deleting ? "none" : "static";
      slot.cursor = "pointer";
      const idx = i;
      slot.on("pointertap", () => {
        if (this.deleting) return;
        this.cursorIndex = idx;
        this.redraw();
        if (idx < this.roles.length) this.onConfirm?.();
        else this.onCreate?.();
      });
      this.rowsHost.addChild(slot);

      if (i < this.roles.length) {
        const role = this.roles[i]!;
        const key = actorResourceForJobSex(role.jobIndex, role.sex);
        const body = bodyCache.get(key);
        let textX = 70;
        if (body) {
          const spr = new Sprite(body);
          const sx = Math.min(bodyMaxW / body.width, bodyMaxH / body.height);
          spr.scale.set(sx);
          spr.anchor.set(0.5, 1);
          spr.position.set(22 + bodyMaxW / 2, y + slotH - 5);
          spr.eventMode = "none";
          this.rowsHost.addChild(spr);
          textX = 22 + bodyMaxW + 10;
        } else {
          const ph = new Graphics()
            .rect(22, y + 8, bodyMaxW, bodyMaxH)
            .fill({ color: 0x504030 })
            .stroke({ width: 1, color: CBE.gold });
          ph.eventMode = "none";
          this.rowsHost.addChild(ph);
          textX = 22 + bodyMaxW + 10;
        }
        const lines: BitmapLabel[] = [
          makePixelText(`名称: ${role.name}`, 12, CBE.ink),
          makePixelText(`职业: ${JOBS[role.jobIndex] ?? "未知"}`, 12, CBE.inkLabel),
          makePixelText(`等级: ${role.level}`, 12, CBE.ink),
        ];
        lines.forEach((t, li) => {
          t.eventMode = "none";
          t.position.set(textX, y + 14 + li * 16);
          this.rowsHost.addChild(t);
        });
      } else {
        const create = makePixelText("创建角色", 14, CBE.borderDim);
        create.eventMode = "none";
        create.anchor.set(0.5, 0.5);
        create.position.set(LCD_WIDTH / 2, y + slotH / 2);
        this.rowsHost.addChild(create);
      }
    }
  }
}
