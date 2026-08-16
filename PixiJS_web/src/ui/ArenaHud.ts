import { Container, Graphics } from "pixi.js";
import { CBE, LCD_WIDTH, WORLD_TOP_H, makePixelText } from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import type { ArenaHud as ArenaHudState } from "../net/protocolArena";

function fmtSec(sec: number): string {
  const s = Math.max(0, sec | 0);
  const m = Math.floor(s / 60);
  const r = s % 60;
  return `${m}:${r.toString().padStart(2, "0")}`;
}

/** Compact strip under WorldHud chrome while inside 比武擂台. */
export class ArenaHud extends Container {
  private bg = new Graphics();
  private line: BitmapLabel;
  private lastKey = "";

  constructor() {
    super();
    this.visible = false;
    /* Below olive status (y72) so it is not covered by location/status art. */
    this.y = WORLD_TOP_H;
    this.zIndex = 40;
    this.addChild(this.bg);
    this.line = makePixelText("", 12, CBE.inkTip);
    this.line.x = 4;
    this.line.y = 2;
    this.addChild(this.line);
  }

  apply(state: ArenaHudState | null): void {
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
    const key = `${state.status}|${state.round}|${state.roundRemain}|${state.eventRemain}|${state.score}`;
    if (key === this.lastKey && this.visible) return;
    this.lastKey = key;
    this.visible = true;
    const phase = state.status === 1 ? "进场" : "比赛";
    const text = `比武 ${phase} 第${state.round || 0}场 ${fmtSec(state.roundRemain)} 余${fmtSec(state.eventRemain)} 分${state.score}`;
    this.line.text = text;
    this.bg.clear();
    this.bg.rect(0, 0, LCD_WIDTH, 16).fill({ color: CBE.bgDeep, alpha: 0.82 });
  }
}
