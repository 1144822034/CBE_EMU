import { Container, Graphics, Rectangle, Sprite, Texture } from "pixi.js";
import type { BattleSide } from "../net/protocol";
import {
  DigitStrip,
  loadUiCrop,
  tryLoadUiTexture,
  uiSprite,
} from "./UiAssets";
import { BitmapLabel, UC3_CELL } from "./BitmapLabel";
import {
  composeActorFrameIndex,
  loadActor,
  uiActorResourceForJobSex,
} from "../resources/actor";
import { nudgeTextureSource, replaceGraphics } from "../render/gpuResume";

const NAME_PINK = 0xff40c8;
const HP_FILL = 0xe03828;
const SP_FILL = 0x40e060;
/** ui_hpspnum.gif opaque ink — HP/SP digit glyph color. */
const HP_NUM = 0xd6f7a5;
/** xuetiao.gif inner track — fill draws ABOVE opaque black pixels. */
const BAR_FILL = { x: 5, y: 4, w: 34, h: 3 } as const;
const SLOT_W = 78;
/** Left edge shared by face + HP/SP bars. */
const FACE_X = 2;
/** Shrunk handkuang (was SLOT_FRAME_SIZE=25) so face sits above the bars. */
const FRAME = 16;
const PORTRAIT_PAD = 2;
const PORTRAIT = FRAME - PORTRAIT_PAD * 2;
const NAME_SIZE = 10;
/**
 * Layout:
 *   [face] name
 *   HP bar   (x = face)
 *   HP nums
 *   SP bar
 *   SP nums
 */
const Y_NAME = 3;
const Y_FACE = 1;
const Y_HP_BAR = 18;
const Y_HP_NUM = 30;
const Y_SP_BAR = 42;
const Y_SP_NUM = 54;
/** Total bottom party strip height (flush to LCD bottom). */
export const BATTLE_PARTY_HUD_H = 72;

type SlotArt = {
  root: Container;
  portrait: Container;
  name: BitmapLabel;
  hpFill: Graphics;
  spFill: Graphics;
  hpNum: DigitStrip | null;
  spNum: DigitStrip | null;
};

/**
 * Compact battle bottom HUD: up to 3 party slots using CBE `xuetiao` /
 * `ui_hpsp` / `ui_hpspnum`.
 */
export class BattlePartyHud extends Container {
  private slots: SlotArt[] = [];
  private panel = new Graphics();
  private ready = false;
  private xuetiao: Texture | null = null;
  private handkuang: Texture | null = null;
  private hpLabTex: Texture | null = null;
  private spLabTex: Texture | null = null;
  private jobIndex = 0;
  /** Normalized 0=male 1=female — only fallback for the local seat. */
  private sex = 0;
  private selfId = 0;

  constructor() {
    super();
    this.addChild(this.panel);
  }

  get heightPx(): number {
    return BATTLE_PARTY_HUD_H;
  }

  setIdentity(jobIndex: number, sex: number, selfId = 0): void {
    this.jobIndex = jobIndex;
    this.sex = sex ? 1 : 0;
    this.selfId = selfId >>> 0;
  }

  async mount(): Promise<void> {
    if (this.ready) return;
    this.xuetiao = await tryLoadUiTexture("xuetiao.gif");
    this.handkuang = await tryLoadUiTexture("handkuang.gif");
    const hpsp = await tryLoadUiTexture("ui_hpsp.gif");
    if (hpsp && hpsp.height >= 10) {
      this.hpLabTex = new Texture({
        source: hpsp.source,
        frame: new Rectangle(0, 0, hpsp.width, 5),
      });
      this.spLabTex = new Texture({
        source: hpsp.source,
        frame: new Rectangle(0, 5, hpsp.width, 5),
      });
      this.hpLabTex.source.scaleMode = "nearest";
      this.spLabTex.source.scaleMode = "nearest";
    } else {
      this.hpLabTex = await loadUiCrop("HP.gif", 0, 0, 9, 5);
      this.spLabTex = this.hpLabTex;
    }
    for (let i = 0; i < 3; i++) {
      const slot = await this.makeSlot();
      slot.root.x = 2 + i * SLOT_W;
      slot.root.y = 4;
      slot.root.visible = false;
      this.slots.push(slot);
      this.addChild(slot.root);
    }
    this.ready = true;
  }

  /** Show up to 3 right-side party members (leader-first wire order). */
  setParty(
    members: BattleSide[],
    opts?: { playerName?: string; playerActor?: string },
  ): void {
    if (!this.ready) {
      void this.mount().then(() => this.setParty(members, opts));
      return;
    }
    const n = Math.min(3, Math.max(1, members.length || 1));
    this.panel
      .clear()
      .rect(0, 0, 240, BATTLE_PARTY_HUD_H)
      .fill({ color: 0x1a332e });

    for (let i = 0; i < 3; i++) {
      const slot = this.slots[i]!;
      const row = members[i];
      if (!row || i >= n) {
        slot.root.visible = false;
        continue;
      }
      slot.root.visible = true;
      // right[0] is leader, not always self — never force local playerName onto seat 0.
      const isSelf = this.selfId !== 0 && (row.id >>> 0) === this.selfId;
      const name = (
        row.name ||
        (isSelf ? opts?.playerName : "") ||
        `队友${i + 1}`
      ).slice(0, 4);
      // right[0] = leader keeps pink; others match HP digit ink.
      const nameColor = i === 0 ? NAME_PINK : HP_NUM;
      // UC3 ignores fontSize (always ~14); scale → visual NAME_SIZE.
      // Songti DOM picks up the same container scale via worldTransform.
      slot.name.scale.set(NAME_SIZE / UC3_CELL);
      void slot.name.setText(name, nameColor, {
        smooth: false,
        outline: false,
      });
      this.paintVitals(slot, row);
      const job =
        row.jobIndex ?? (isSelf ? this.jobIndex : 0);
      const sex =
        row.sex != null ? (row.sex ? 1 : 0) : isSelf ? this.sex : 0;
      const face = uiActorResourceForJobSex(job, sex);
      void this.mountPortrait(slot, face);
      void opts?.playerActor;
    }
  }

  refresh(members: BattleSide[]): void {
    for (let i = 0; i < 3; i++) {
      const slot = this.slots[i]!;
      const row = members[i];
      if (!slot.root.visible || !row) continue;
      this.paintVitals(slot, row);
    }
  }

  /**
   * webglcontextrestored: xuetiao frames may return via texture nudge, but
   * HP/SP Graphics fills stay hollow until the nodes are replaced + redrawn.
   * Portraits (Texture.from canvas) also need remount.
   */
  nudgeAfterGpuResume(
    members: BattleSide[],
    opts?: { playerName?: string; playerActor?: string },
  ): void {
    if (!this.ready) return;
    try {
      nudgeTextureSource(this.xuetiao);
      nudgeTextureSource(this.handkuang);
      nudgeTextureSource(this.hpLabTex);
      nudgeTextureSource(this.spLabTex);
    } catch {
      /* ignore */
    }
    for (const slot of this.slots) {
      slot.hpFill = replaceGraphics(slot.hpFill);
      slot.spFill = replaceGraphics(slot.spFill);
    }
    this.panel = replaceGraphics(this.panel);
    if (this.children[0] !== this.panel) {
      this.addChildAt(this.panel, 0);
    }
    // setParty remounts faces + paintVitals on the fresh Graphics.
    this.setParty(members, opts);
  }

  private paintVitals(slot: SlotArt, row: BattleSide): void {
    const maxHp = Math.max(1, row.maxHp || row.hp || 1);
    const maxMp = Math.max(1, row.maxMp || row.mp || 1);
    const hp = Math.max(0, Math.min(maxHp, row.hp));
    const mp = Math.max(0, Math.min(maxMp, row.mp));
    const hpW = Math.max(0, Math.floor((BAR_FILL.w * hp) / maxHp));
    const spW = Math.max(0, Math.floor((BAR_FILL.w * mp) / maxMp));
    slot.hpFill
      .clear()
      .rect(BAR_FILL.x, BAR_FILL.y, hpW, BAR_FILL.h)
      .fill({ color: HP_FILL });
    slot.spFill
      .clear()
      .rect(BAR_FILL.x, BAR_FILL.y, spW, BAR_FILL.h)
      .fill({ color: SP_FILL });
    slot.hpNum?.setText(`${hp}/${maxHp}`);
    slot.spNum?.setText(`${mp}/${maxMp}`);
  }

  private async makeSlot(): Promise<SlotArt> {
    const root = new Container();
    const portrait = new Container();
    // HUD/battle face frame = handkuang.gif (same as world HUD / equip slots).
    if (this.handkuang) {
      portrait.addChild(uiSprite(this.handkuang, FRAME, FRAME));
    } else {
      portrait.addChild(
        new Graphics()
          .rect(0, 0, FRAME, FRAME)
          .stroke({ width: 1, color: 0xd4c489 }),
      );
    }
    portrait.position.set(FACE_X, Y_FACE);

    const name = new BitmapLabel();
    name.scale.set(NAME_SIZE / UC3_CELL);
    name.position.set(FACE_X + FRAME + 2, Y_NAME);

    const mkBar = () => {
      const host = new Container();
      const fill = new Graphics();
      if (this.xuetiao) {
        host.addChild(uiSprite(this.xuetiao));
      } else {
        host.addChild(
          new Graphics()
            .roundRect(0, 0, 44, 11, 2)
            .fill({ color: 0x101010 })
            .stroke({ width: 1, color: 0xd4c489 }),
        );
      }
      host.addChild(fill);
      return { host, fill };
    };

    const hp = mkBar();
    const sp = mkBar();
    // Bars share the face left edge.
    hp.host.position.set(FACE_X, Y_HP_BAR);
    sp.host.position.set(FACE_X, Y_SP_BAR);

    const hpLabel = this.hpLabTex
      ? uiSprite(this.hpLabTex)
      : new Sprite(Texture.EMPTY);
    const spLabel = this.spLabTex
      ? uiSprite(this.spLabTex)
      : new Sprite(Texture.EMPTY);
    hpLabel.position.set(FACE_X, Y_HP_NUM);
    spLabel.position.set(FACE_X, Y_SP_NUM);

    const hpNum =
      (await DigitStrip.battleHpspNum()) || (await DigitStrip.hpspNum());
    const spNum =
      (await DigitStrip.battleHpspNum()) || (await DigitStrip.hpspNum());
    const hpNumHost = new Container();
    const spNumHost = new Container();
    hpNumHost.position.set(FACE_X + 10, Y_HP_NUM);
    spNumHost.position.set(FACE_X + 10, Y_SP_NUM);
    if (hpNum) {
      hpNum.setText("0/0");
      hpNumHost.addChild(hpNum.root);
    }
    if (spNum) {
      spNum.setText("0/0");
      spNumHost.addChild(spNum.root);
    }

    root.addChild(
      portrait,
      name,
      hp.host,
      sp.host,
      hpLabel,
      spLabel,
      hpNumHost,
      spNumHost,
    );
    return {
      root,
      portrait,
      name,
      hpFill: hp.fill,
      spFill: sp.fill,
      hpNum,
      spNum,
    };
  }

  private async mountPortrait(slot: SlotArt, actorName: string): Promise<void> {
    // Keep gold frame (first child); replace any previous face / mask.
    while (slot.portrait.children.length > 1) {
      const c = slot.portrait.children[slot.portrait.children.length - 1]!;
      slot.portrait.removeChild(c);
      try {
        c.destroy({ children: true, texture: true, textureSource: true });
      } catch {
        /* ignore */
      }
    }
    try {
      const loaded = await loadActor(actorName);
      // Same contract as WorldHud.setPortraitActor.
      let frame = composeActorFrameIndex(loaded, 0);
      if (!frame || frame.width < 4 || frame.height < 4) {
        frame = composeActorFrameIndex(loaded, 1) ?? frame;
      }
      if (!frame || frame.width < 4 || frame.height < 4) return;

      const canvas = document.createElement("canvas");
      canvas.width = frame.width;
      canvas.height = frame.height;
      canvas.getContext("2d")!.putImageData(
        new ImageData(
          new Uint8ClampedArray(frame.rgba),
          frame.width,
          frame.height,
        ),
        0,
        0,
      );
      const tex = Texture.from(canvas);
      tex.source.scaleMode = "nearest";
      const spr = new Sprite(tex);
      spr.width = PORTRAIT;
      spr.height = PORTRAIT;
      spr.position.set(PORTRAIT_PAD, PORTRAIT_PAD);
      slot.portrait.addChild(spr);
    } catch (e) {
      console.warn("[battle-hud] portrait", actorName, e);
    }
  }
}
