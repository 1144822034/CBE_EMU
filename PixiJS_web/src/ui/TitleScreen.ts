import { Container, Graphics, Rectangle, Sprite, Texture } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  paintCbePanel,
  drawLocBanner,
  makePixelText,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { tryLoadUiTexture } from "./UiAssets";
import type { ServerRow } from "../net/protocol";
import {
  actorPartKeyframeCount,
  composeActorPartKeyframe,
  loadActor,
  type LoadedActor,
} from "../resources/actor";
import { TipBootLayer } from "./TipBootLayer";
import { LoginIme } from "./LoginIme";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

type Mode = "splash" | "login" | "register" | "servers";
type LoginField = "user" | "pass" | "pass2";

const LS_USER = "pixi.jh.account";
const LS_PASS = "pixi.jh.password";
const AUTH_FIELDS_LOGIN: LoginField[] = ["user", "pass"];
const AUTH_FIELDS_REGISTER: LoginField[] = ["user", "pass", "pass2"];

/** Leave a strip under the server panel so footer tip clears the CBE frame. */
const TIP_STRIP_H = 24;

/** title2.gif: 146×76 → 2 cols (normal|hi) × 4 rows. */
const MENU_CELL_W = 73;
const MENU_CELL_H = 19;

/**
 * Title flow: 江湖支招 tip → ui_title.actor → menu → login/register → servers.
 */
export class TitleScreen extends Container {
  private mode: Mode = "splash";
  private splash = new Container();
  private tipBoot = new TipBootLayer();
  private introLayer = new Container();
  private menuLayer = new Container();
  private loginLayer = new Container();
  private serverLayer = new Container();
  private menuCursor = 0;
  private serverCursor = 0;
  private menuLabels: BitmapLabel[] = [];
  private menuSprites: Sprite[] = [];
  private serverRows: BitmapLabel[] = [];
  private serverHits: Container[] = [];
  private loginLabels: BitmapLabel[] = [];
  private loginHits: Container[] = [];
  private servers: ServerRow[] = [];
  private status: BitmapLabel;
  private tipText: BitmapLabel;
  private loginTip: BitmapLabel;
  private serverGfx = new Graphics();
  private loginGfx = new Graphics();
  private loginBannerLabel: BitmapLabel | null = null;
  private loginField: LoginField = "user";
  private userValue = "";
  private passValue = "";
  private pass2Value = "";
  private caretVisible = true;
  private caretTimer: number | null = null;
  private ime = new LoginIme();

  private introSprite: Sprite | null = null;
  private introTextures: Texture[] = [];
  private introTimer: number | null = null;
  private introPlaying = false;
  private introDone = false;
  private menuSheet: Texture | null = null;

  onStartGame: (() => void) | null = null;
  onOpenLogin: (() => void) | null = null;
  onOpenRegister: (() => void) | null = null;
  onSubmitLogin: ((user: string, password: string) => void) | null = null;
  onSubmitRegister: ((user: string, password: string) => void) | null = null;
  onCancelLogin: (() => void) | null = null;
  onConfirmServer: (() => void) | null = null;

  private readonly menuItems = ["开始游戏", "登陆游戏", "江湖赞助", "游戏退出"];

  constructor() {
    super();
    this.addChild(this.splash, this.loginLayer, this.serverLayer);
    this.splash.addChild(this.introLayer, this.menuLayer, this.tipBoot);
    this.loginLayer.visible = false;
    this.serverLayer.visible = false;
    this.menuLayer.visible = false;

    this.status = makePixelText("", 11, CBE.inkBody);
    this.status.position.set(8, LCD_HEIGHT - 48);
    this.tipText = makePixelText("", 11, CBE.ink);
    this.loginTip = makePixelText("", 11, CBE.inkTip);
    this.menuLayer.addChild(this.status);

    this.tipBoot.onFinished = () => this.playLogoIntro();

    this.ime.onChange = (field, value) => {
      if (field === "user") this.userValue = value;
      else if (field === "pass2") this.pass2Value = value;
      else this.passValue = value;
      this.redrawLogin();
    };
    this.ime.onSubmit = () => this.confirm();
    this.ime.onFocusField = (field) => {
      if (this.mode !== "register" && field === "pass2") return;
      if (this.loginField === field) return;
      this.loginField = field;
      this.redrawLogin();
    };

    try {
      this.userValue = localStorage.getItem(LS_USER) ?? "";
      this.passValue = localStorage.getItem(LS_PASS) ?? "";
    } catch {
      /* ignore */
    }
  }

  async mountArt(): Promise<void> {
    // Fallback static bg while actor builds / if actor missing.
    const bg = await tryLoadUiTexture("Background.gif");
    if (bg) {
      const spr = new Sprite(bg);
      spr.width = LCD_WIDTH;
      spr.height = LCD_HEIGHT;
      this.introLayer.addChild(spr);
      this.introSprite = spr;
    } else {
      this.introLayer.addChild(
        new Graphics().rect(0, 0, LCD_WIDTH, LCD_HEIGHT).fill({ color: CBE.bgPanel }),
      );
    }

    try {
      const actor = await loadActor("ui_title.actor");
      this.introTextures = buildTitleKeyframeTextures(actor);
      if (this.introTextures[0]) {
        if (this.introSprite) {
          this.introSprite.texture = this.introTextures[0];
          this.introSprite.width = LCD_WIDTH;
          this.introSprite.height = LCD_HEIGHT;
        } else {
          this.introSprite = new Sprite(this.introTextures[0]);
          this.introSprite.width = LCD_WIDTH;
          this.introSprite.height = LCD_HEIGHT;
          this.introLayer.addChild(this.introSprite);
        }
      }
    } catch (e) {
      console.warn("[title] ui_title.actor intro failed", e);
    }

    const seal = await tryLoadUiTexture("title001.gif");
    if (seal) {
      const spr = new Sprite(seal);
      spr.position.set(LCD_WIDTH - spr.width - 6, 8);
      this.menuLayer.addChild(spr);
    }

    // loading.gif belongs only to TipBootLayer「获取版本信息」, not the menu.
    this.menuSheet = await tryLoadUiTexture("title2.gif");
    await this.tipBoot.mountArt();
    this.redrawMenu();
    this.buildLoginChrome();
    this.buildServerChrome();
    this.playBootSequence();
  }

  setMode(mode: Mode): void {
    this.mode = mode;
    this.splash.visible = mode === "splash";
    this.loginLayer.visible = mode === "login" || mode === "register";
    this.serverLayer.visible = mode === "servers";
    if (mode === "login" || mode === "register") {
      if (mode === "login" && this.loginField === "pass2") this.loginField = "user";
      if (mode === "register") this.pass2Value = "";
      this.refreshLoginBanner();
      this.startCaret();
      this.redrawLogin();
    } else {
      this.releaseIme();
    }
    if (mode === "splash") {
      this.playBootSequence();
    } else {
      this.tipBoot.stop();
      this.stopIntro();
    }
  }

  /**
   * Leave title flow (role/world/…) without restarting splash boot.
   * Must stop the login caret — otherwise redrawLogin keeps calling
   * LoginIme.show() over the game after title.visible=false.
   */
  releaseIme(): void {
    this.stopCaret();
    this.ime.hide();
  }

  getMode(): Mode {
    return this.mode;
  }

  setServers(servers: ServerRow[]): void {
    this.servers = servers;
    this.serverCursor = 0;
    this.redrawServers();
  }

  setStatus(text: string): void {
    this.status.text = text;
    this.tipText.text = text;
    this.loginTip.text = text;
  }

  setCredentials(user: string, password: string): void {
    this.userValue = user;
    this.passValue = password;
    this.persistCredentials();
    if (this.mode === "login" || this.mode === "register") this.redrawLogin();
  }

  getCredentials(): { user: string; password: string } {
    return { user: this.userValue, password: this.passValue };
  }

  move(delta: number): void {
    if (this.mode === "splash") {
      if (!this.introDone) return;
      this.menuCursor =
        (this.menuCursor + delta + this.menuItems.length) % this.menuItems.length;
      this.redrawMenu();
    } else if (this.mode === "login" || this.mode === "register") {
      this.cycleAuthField(delta);
    } else if (this.servers.length) {
      this.serverCursor =
        (this.serverCursor + delta + this.servers.length) % this.servers.length;
      this.redrawServers();
    }
  }

  confirm(): void {
    if (this.mode === "splash") {
      if (this.tipBoot.visible) {
        this.tipBoot.skip();
        return;
      }
      if (!this.introDone) {
        this.finishIntro();
        return;
      }
      const item = this.menuItems[this.menuCursor];
      if (item === "开始游戏") {
        // 不再访客空凭据直连；与登陆一样先登录账号。
        (this.onStartGame ?? this.onOpenLogin)?.();
      } else if (item === "登陆游戏") {
        this.onOpenLogin?.();
      } else if (item === "游戏退出") {
        this.setStatus("请关闭页面退出");
      } else {
        this.setStatus("赞助请使用账号中心");
      }
      return;
    }
    if (this.mode === "login" || this.mode === "register") {
      const user = this.userValue.trim();
      const password = this.passValue;
      if (!user || !password) {
        this.setStatus(
          this.mode === "register" ? "请输入要注册的账号和密码" : "请输入账号和密码",
        );
        this.redrawLogin();
        return;
      }
      if (this.mode === "register") {
        if (!this.pass2Value) {
          this.setStatus("请再次输入确认密码");
          this.loginField = "pass2";
          this.redrawLogin();
          this.focusLoginField("pass2", true);
          return;
        }
        if (password !== this.pass2Value) {
          this.setStatus("两次输入的密码不一致");
          this.loginField = "pass2";
          this.redrawLogin();
          this.focusLoginField("pass2", true);
          return;
        }
        this.persistCredentials();
        this.onSubmitRegister?.(user, password);
        return;
      }
      this.persistCredentials();
      this.onSubmitLogin?.(user, password);
      return;
    }
    this.onConfirmServer?.();
  }

  cancelLogin(): void {
    if (this.mode === "register") {
      this.onOpenLogin?.();
      return;
    }
    this.onCancelLogin?.();
  }

  typeKey(key: string, ev?: KeyboardEvent): boolean {
    if (this.mode !== "login" && this.mode !== "register") return false;
    if (key === "tab") {
      this.cycleAuthField(ev?.shiftKey ? -1 : 1);
      return true;
    }
    // Overlay <input> owns typing while focused. Intercepting Backspace/chars
    // here preventDefaults the native edit but skips syncing the HTML value,
    // so the next soft-keyboard `input` event resurrected deleted text.
    if (this.ime.isFocused()) return false;
    if (key === "backspace") {
      if (this.loginField === "user") this.userValue = this.userValue.slice(0, -1);
      else if (this.loginField === "pass2") this.pass2Value = this.pass2Value.slice(0, -1);
      else this.passValue = this.passValue.slice(0, -1);
      this.syncImeValues(true);
      this.redrawLogin();
      return true;
    }
    if (ev?.ctrlKey && key === "v") return false;
    if (key.length === 1 && !ev?.ctrlKey && !ev?.altKey && !ev?.metaKey) {
      if (this.loginField === "user") {
        if (this.userValue.length < 32) this.userValue += key;
      } else if (this.loginField === "pass2") {
        if (this.pass2Value.length < 32) this.pass2Value += key;
      } else if (this.passValue.length < 32) {
        this.passValue += key;
      }
      this.syncImeValues(true);
      this.redrawLogin();
      return true;
    }
    return false;
  }

  async pasteClipboard(): Promise<void> {
    if (this.mode !== "login" && this.mode !== "register") return;
    try {
      const text = (await navigator.clipboard.readText()).replace(/[\r\n]/g, "");
      if (!text) return;
      if (this.loginField === "user") this.userValue = text.slice(0, 32);
      else if (this.loginField === "pass2") this.pass2Value = text.slice(0, 32);
      else this.passValue = text.slice(0, 32);
      this.syncImeValues(true);
      this.redrawLogin();
    } catch {
      /* clipboard denied */
    }
  }

  get selectedServer(): ServerRow | null {
    return this.servers[this.serverCursor] ?? null;
  }

  destroy(options?: Parameters<Container["destroy"]>[0]): void {
    this.stopCaret();
    this.tipBoot.stop();
    this.stopIntro();
    this.ime.destroy();
    super.destroy(options);
  }

  private focusLoginField(field: LoginField, openIme: boolean): void {
    this.loginField = field;
    this.syncImeValues(false);
    if (openIme) {
      const value =
        field === "user"
          ? this.userValue
          : field === "pass2"
            ? this.pass2Value
            : this.passValue;
      this.ime.focus(field, value);
    }
  }

  private authFields(): LoginField[] {
    return this.mode === "register" ? AUTH_FIELDS_REGISTER : AUTH_FIELDS_LOGIN;
  }

  private cycleAuthField(delta: number): void {
    const fields = this.authFields();
    let idx = fields.indexOf(this.loginField);
    if (idx < 0) idx = 0;
    const next = fields[(idx + delta + fields.length) % fields.length];
    this.redrawLogin();
    this.focusLoginField(next, true);
  }

  private syncImeValues(force: boolean): void {
    if (force) this.ime.forceValues(this.userValue, this.passValue, this.pass2Value);
    else this.ime.setValues(this.userValue, this.passValue, this.pass2Value);
  }

  private playBootSequence(): void {
    this.stopIntro();
    this.introDone = false;
    this.menuLayer.visible = false;
    this.introLayer.visible = false;
    this.tipBoot.play();
  }

  private playLogoIntro(): void {
    this.stopIntro();
    this.introDone = false;
    this.menuLayer.visible = false;
    this.introLayer.visible = true;
    if (!this.introTextures.length || !this.introSprite) {
      this.finishIntro();
      return;
    }
    this.introPlaying = true;
    let i = 0;
    this.introSprite.texture = this.introTextures[0]!;
    this.introTimer = window.setInterval(() => {
      i += 1;
      if (i >= this.introTextures.length) {
        this.finishIntro();
        return;
      }
      if (this.introSprite) this.introSprite.texture = this.introTextures[i]!;
    }, 100);
  }

  private finishIntro(): void {
    this.stopIntro();
    this.introDone = true;
    this.introLayer.visible = true;
    if (this.introTextures.length && this.introSprite) {
      this.introSprite.texture = this.introTextures[this.introTextures.length - 1]!;
    }
    this.menuLayer.visible = true;
    this.redrawMenu();
  }

  private stopIntro(): void {
    this.introPlaying = false;
    if (this.introTimer != null) {
      window.clearInterval(this.introTimer);
      this.introTimer = null;
    }
  }

  private persistCredentials(): void {
    try {
      localStorage.setItem(LS_USER, this.userValue);
      localStorage.setItem(LS_PASS, this.passValue);
    } catch {
      /* ignore */
    }
  }

  private startCaret(): void {
    this.stopCaret();
    this.caretTimer = window.setInterval(() => {
      this.caretVisible = !this.caretVisible;
      this.redrawLogin();
    }, 450);
  }

  private stopCaret(): void {
    if (this.caretTimer != null) {
      window.clearInterval(this.caretTimer);
      this.caretTimer = null;
    }
  }

  private redrawMenu(): void {
    for (const t of this.menuLabels) t.destroy();
    for (const s of this.menuSprites) s.destroy();
    this.menuLabels = [];
    this.menuSprites = [];
    const startY = 224;

    const useSheet =
      !!this.menuSheet &&
      this.menuSheet.width === MENU_CELL_W * 2 &&
      this.menuSheet.height === MENU_CELL_H * 4;

    this.menuItems.forEach((label, i) => {
      const hi = i === this.menuCursor;
      const y = startY + i * 26;
      if (useSheet && this.menuSheet) {
        const cell = new Texture({
          source: this.menuSheet.source,
          frame: new Rectangle(
            (hi ? 1 : 0) * MENU_CELL_W,
            i * MENU_CELL_H,
            MENU_CELL_W,
            MENU_CELL_H,
          ),
        });
        const spr = new Sprite(cell);
        spr.anchor.set(0.5, 0);
        spr.position.set(LCD_WIDTH / 2, y);
        spr.eventMode = "static";
        spr.cursor = "pointer";
        const idx = i;
        spr.on("pointertap", () => {
          this.menuCursor = idx;
          this.redrawMenu();
          this.confirm();
        });
        this.menuLayer.addChild(spr);
        this.menuSprites.push(spr);
      } else {
        const t = makePixelText(label, 14, hi ? CBE.menuHi : CBE.ink);
        t.anchor.set(0.5, 0);
        t.position.set(LCD_WIDTH / 2, y);
        t.eventMode = "static";
        t.cursor = "pointer";
        const idx = i;
        t.on("pointertap", () => {
          this.menuCursor = idx;
          this.redrawMenu();
          this.confirm();
        });
        this.menuLayer.addChild(t);
        this.menuLabels.push(t);
      }
    });
  }

  private buildLoginChrome(): void {
    this.loginLayer.removeChildren();
    this.loginLayer.addChild(
      new Graphics().rect(0, 0, LCD_WIDTH, LCD_HEIGHT).fill({ color: CBE.bgDeep }),
    );
    const bannerGfx = new Graphics();
    this.loginLayer.addChild(bannerGfx);
    this.loginBannerLabel = drawLocBanner(
      bannerGfx,
      this.loginLayer,
      "账号登录",
      10,
    );
    this.loginGfx = new Graphics();
    this.loginLayer.addChild(this.loginGfx);
    this.loginTip.position.set(12, LCD_HEIGHT - SOFTKEY_H - 36);
    this.loginLayer.addChild(this.loginTip);
  }

  private refreshLoginBanner(): void {
    const title = this.mode === "register" ? "江湖注册" : "账号登录";
    this.loginBannerLabel?.show(title, CBE.ink, { outline: false, smooth: false });
  }

  private redrawLogin(): void {
    for (const t of this.loginLabels) t.destroy();
    for (const h of this.loginHits) h.destroy();
    this.loginLabels = [];
    this.loginHits = [];
    this.loginGfx.clear();

    const registering = this.mode === "register";
    const boxX = 14;
    const boxY = 48;
    const boxW = LCD_WIDTH - 24;
    const boxH = registering ? 248 : 200;
    paintCbePanel(this, this.loginGfx, boxX, boxY, boxW, boxH, CBE.bgPanel);

    const hint = makePixelText(
      registering ? "点输入框 · 确定注册" : "点输入框 · 确定登录",
      11,
      CBE.inkMint,
    );
    hint.position.set(boxX + 12, boxY + 10);
    this.loginLayer.addChild(hint);
    this.loginLabels.push(hint);

    const fieldW = boxW - 24;
    const userY = boxY + 34;
    const passY = boxY + 90;
    const pass2Y = boxY + 146;
    const userBox = { x: boxX + 12, y: userY + 20, w: fieldW, h: 26 };
    const passBox = { x: boxX + 12, y: passY + 20, w: fieldW, h: 26 };
    const pass2Box = { x: boxX + 12, y: pass2Y + 20, w: fieldW, h: 26 };
    this.drawField("账号", this.userValue, "user", boxX + 12, userY, fieldW);
    this.drawField(
      "密码",
      "*".repeat(this.passValue.length),
      "pass",
      boxX + 12,
      passY,
      fieldW,
    );
    if (registering) {
      this.drawField(
        "确认密码",
        "*".repeat(this.pass2Value.length),
        "pass2",
        boxX + 12,
        pass2Y,
        fieldW,
      );
    }

    const noteY = registering ? boxY + 206 : boxY + 160;
    const linkY = registering ? boxY + 224 : boxY + 178;
    const note = makePixelText(
      registering
        ? "账号4-32位，密码6位"
        : this.userValue || this.passValue
          ? "将使用上方账号登录"
          : "输入已有账号密码登录",
      11,
      CBE.inkBody,
    );
    note.position.set(boxX + 12, noteY);
    this.loginLayer.addChild(note);
    this.loginLabels.push(note);

    if (!registering) {
      const regBtn = makePixelText("没有账号？点此注册", 12, CBE.inkTip);
      regBtn.position.set(boxX + 12, linkY);
      regBtn.eventMode = "static";
      regBtn.cursor = "pointer";
      regBtn.on("pointertap", () => this.onOpenRegister?.());
      this.loginLayer.addChild(regBtn);
      this.loginLabels.push(regBtn);
    } else {
      const backLogin = makePixelText("已有账号？返回登录", 12, CBE.inkTip);
      backLogin.position.set(boxX + 12, linkY);
      backLogin.eventMode = "static";
      backLogin.cursor = "pointer";
      backLogin.on("pointertap", () => this.onOpenLogin?.());
      this.loginLayer.addChild(backLogin);
      this.loginLabels.push(backLogin);
    }
    this.syncImeValues(false);
    // HTML overlays sit on the boxes so phone taps open the soft keyboard.
    // Guard: caret can fire after title was only hidden (visible=false) while
    // mode still "login"/"register" — never remount overlays onto the world scene.
    if ((this.mode === "login" || this.mode === "register") && this.visible) {
      this.ime.show(
        registering
          ? { user: userBox, pass: passBox, pass2: pass2Box }
          : { user: userBox, pass: passBox },
      );
    } else {
      this.ime.hide();
    }
  }

  private drawField(
    label: string,
    display: string,
    field: LoginField,
    x: number,
    y: number,
    w: number,
  ): void {
    const focused = this.loginField === field;
    const lab = makePixelText(label, 13, focused ? CBE.inkTip : CBE.ink);
    lab.position.set(x, y);
    this.loginLayer.addChild(lab);
    this.loginLabels.push(lab);

    const boxY = y + 20;
    this.loginGfx.rect(x, boxY, w, 26).fill({ color: focused ? CBE.select : CBE.bgPanelDark });
    this.loginGfx.rect(x, boxY, w, 26).stroke({
      width: 1,
      color: focused ? CBE.borderBright : CBE.borderDim,
    });

    const caret = focused && this.caretVisible ? "_" : "";
    const shown = (display || (focused ? "" : "")) + caret;
    const val = makePixelText(shown || " ", 13, CBE.ink);
    val.position.set(x + 6, boxY + 5);
    this.loginLayer.addChild(val);
    this.loginLabels.push(val);

    // Transparent hit target so mobile tap opens IME via HTML input focus.
    const hit = new Container();
    hit.position.set(x, boxY);
    hit.hitArea = new Rectangle(0, 0, w, 26);
    hit.eventMode = "static";
    hit.cursor = "text";
    hit.on("pointertap", () => {
      this.focusLoginField(field, true);
      this.redrawLogin();
    });
    this.loginLayer.addChild(hit);
    this.loginHits.push(hit);
  }

  private buildServerChrome(): void {
    this.serverLayer.removeChildren();
    this.serverLayer.addChild(
      new Graphics().rect(0, 0, LCD_WIDTH, LCD_HEIGHT).fill({ color: CBE.bgDeep }),
    );
    const bannerGfx = new Graphics();
    this.serverLayer.addChild(bannerGfx);
    drawLocBanner(bannerGfx, this.serverLayer, "选择要登入的服务器", 10);
    this.serverGfx = new Graphics();
    this.serverLayer.addChild(this.serverGfx);
    this.tipText.position.set(12, LCD_HEIGHT - SOFTKEY_H - TIP_STRIP_H + 5);
    this.serverLayer.addChild(this.tipText);
  }

  private redrawServers(): void {
    for (const r of this.serverRows) r.destroy();
    this.serverRows = [];
    clearPointerHits(this.serverHits);
    this.serverGfx.clear();

    const boxX = 8;
    const boxY = 44;
    const boxW = LCD_WIDTH - 16;
    const boxH = LCD_HEIGHT - SOFTKEY_H - boxY - TIP_STRIP_H;
    paintCbePanel(this, this.serverGfx, boxX, boxY, boxW, boxH, CBE.bgDeep);

    this.serverGfx.rect(boxX + 2, boxY + 2, boxW - 4, 24).fill({ color: CBE.bgPanel });
    this.serverGfx
      .moveTo(boxX + boxW - 56, boxY + 2)
      .lineTo(boxX + boxW - 56, boxY + 26)
      .stroke({ width: 1, color: CBE.borderDim });

    const h1 = makePixelText("服务器", 12, CBE.ink);
    h1.position.set(boxX + 12, boxY + 6);
    const h2 = makePixelText("状态", 12, CBE.ink);
    h2.position.set(boxX + boxW - 44, boxY + 6);
    this.serverLayer.addChild(h1, h2);
    this.serverRows.push(h1, h2);

    this.servers.forEach((s, i) => {
      const y = boxY + 32 + i * 22;
      if (i === this.serverCursor) {
        this.serverGfx.rect(boxX + 4, y - 2, boxW - 8, 20).fill({ color: CBE.select });
      }
      const name = makePixelText(s.name || `区服${s.serverId}`, 12, CBE.inkLabel);
      name.position.set(boxX + 12, y);
      name.eventMode = "none";
      const st = makePixelText(s.label || "推荐", 12, CBE.ink);
      st.position.set(boxX + boxW - 44, y);
      st.eventMode = "none";
      this.serverLayer.addChild(name, st);
      this.serverRows.push(name, st);
      const idx = i;
      this.serverHits.push(
        attachRowPointerHit(this.serverLayer, boxX + 4, y - 2, boxW - 8, 20, () => {
          this.serverCursor = idx;
          this.redrawServers();
          this.confirm();
        }),
      );
    });
  }
}

function buildTitleKeyframeTextures(actor: LoadedActor): Texture[] {
  const n = actorPartKeyframeCount(actor);
  const out: Texture[] = [];
  for (let i = 0; i < n; i++) {
    const frame = composeActorPartKeyframe(actor, i);
    if (!frame) continue;
    const canvas = document.createElement("canvas");
    canvas.width = frame.width;
    canvas.height = frame.height;
    canvas
      .getContext("2d")!
      .putImageData(
        new ImageData(new Uint8ClampedArray(frame.rgba), frame.width, frame.height),
        0,
        0,
      );
    const tex = Texture.from(canvas);
    tex.source.scaleMode = "nearest";
    out.push(tex);
  }
  return out;
}
