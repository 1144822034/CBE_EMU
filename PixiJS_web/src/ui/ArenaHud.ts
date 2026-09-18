import { Container, Graphics } from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import { CBE, LCD_WIDTH, MAP_STATUS_STRIP_H, WORLD_TOP_H, makePixelText } from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import type { ArenaHud as ArenaHudState } from "../net/protocolArena";

function fmtSec(sec: number): string {
  const s = Math.max(0, sec | 0);
  const m = Math.floor(s / 60);
  const r = s % 60;
  return `${m}:${r.toString().padStart(2, "0")}`;
}

/**
 * Map-corner status strip (修/拍/擂/… flush under exp).
 * Same clearance as WorldHud PARTY_Y0 / QuestTracker TRACKER_Y0.
 */
const STATUS_STRIP_BOTTOM = MAP_STATUS_STRIP_H;
/** Band height (bg rect + text padding). */
export const ARENA_HUD_BAND_H = 16;
/** Absolute LCD Y: under WorldHud chrome and clear of 修/拍 badges. */
const ARENA_HUD_Y = WORLD_TOP_H + STATUS_STRIP_BOTTOM + 2;

/** Compact strip under WorldHud chrome while inside 比武擂台. */
export class ArenaHud extends Container {
  private bg = new Graphics();
  private line: BitmapLabel;
  private lastKey = "";
  private lastState: ArenaHudState | null = null;

  constructor() {
    super();
    this.visible = false;
    /* Below olive chrome + map-corner status strip so 修/拍/擂… stay tappable. */
    this.y = ARENA_HUD_Y;
    this.zIndex = 40;
    this.addChild(this.bg);
    this.line = makePixelText("", 10, CBE.inkTip);
    this.line.x = 4;
    this.line.y = 2;
    this.addChild(this.line);
  }

  /** Extra map-local Y quest tracker / party should clear when this strip is up. */
  overlayOffsetY(): number {
    return this.visible ? ARENA_HUD_BAND_H + 2 : 0;
  }

  /** webglcontextrestored: remount strip fill then re-apply last state. */
  nudgeAfterGpuResume(): void {
    this.bg = replaceGraphics(this.bg);
    this.lastKey = "";
    if (this.visible && this.lastState) this.apply(this.lastState);
  }

  apply(state: ArenaHudState | null): void {
    this.lastState = state;
    if (!state) {
      this.visible = false;
      this.lastKey = "";
      return;
    }
    /* Only ENTRY(1)/LIVE(2). SETTLED(3)/IDLE hide so tip does not stick after 结束. */
    if (state.status !== 1 && state.status !== 2) {
      this.visible = false;
      this.lastKey = "";
      return;
    }
    const key = `${state.status}|${state.round}|${state.roundRemain}|${state.eventRemain}|${state.score}|${state.playoff || 0}`;
    if (key === this.lastKey && this.visible) return;
    this.lastKey = key;
    this.visible = true;
    const phase =
      state.status === 1 ? "进场" : state.playoff ? "加赛" : "比赛";
    const text = `比武 ${phase} 第${state.round || 0}场 ${fmtSec(state.roundRemain)} 余${fmtSec(state.eventRemain)} 分${state.score}`;
    this.line.text = text;
    this.bg.clear();
    this.bg
      .rect(0, 0, LCD_WIDTH, ARENA_HUD_BAND_H)
      .fill({ color: CBE.bgDeep, alpha: 0.82 });
  }
}
