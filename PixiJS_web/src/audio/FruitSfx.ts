/**
 * 水果机音效 — 资源来自 luckyFruit，Vite 打包 `assets/fruit_sfx`。
 * 与世界 BGM（「开关声音」）独立；水果机内常开，不读 GameBgm 偏好。
 */
import rotateStart from "../../assets/fruit_sfx/rotate_start.mp3?url";
import rotating from "../../assets/fruit_sfx/rotating.mp3?url";
import rotateEnd from "../../assets/fruit_sfx/rotate_end.mp3?url";
import ding from "../../assets/fruit_sfx/ding.mp3?url";
import zhongle from "../../assets/fruit_sfx/zhongle.mp3?url";
import fail from "../../assets/fruit_sfx/fail.mp3?url";
import hit from "../../assets/fruit_sfx/hit.mp3?url";
import hitLucky from "../../assets/fruit_sfx/hit_lucky.mp3?url";
import luckySuccess from "../../assets/fruit_sfx/lucky_success.mp3?url";
import luckyFail from "../../assets/fruit_sfx/lucky_fail.mp3?url";
import lightBoo from "../../assets/fruit_sfx/light_boo.mp3?url";
import sendLights from "../../assets/fruit_sfx/send_lights.mp3?url";
import fruit1 from "../../assets/fruit_sfx/fruit_1.mp3?url";
import fruit2 from "../../assets/fruit_sfx/fruit_2.mp3?url";
import fruit3 from "../../assets/fruit_sfx/fruit_3.mp3?url";
import fruit4 from "../../assets/fruit_sfx/fruit_4.mp3?url";
import fruit5 from "../../assets/fruit_sfx/fruit_5.mp3?url";
import fruit6 from "../../assets/fruit_sfx/fruit_6.mp3?url";
import fruit7 from "../../assets/fruit_sfx/fruit_7.mp3?url";
import fruit8 from "../../assets/fruit_sfx/fruit_8.mp3?url";
/** luckyFruit AudioCtrl audioResUrlMap：牌型固定提示音 Y114–Y131。 */
import effectDsy from "../../assets/fruit_sfx/Y114.mp3?url";
import effectXsy from "../../assets/fruit_sfx/Y115.mp3?url";
import effectDsx from "../../assets/fruit_sfx/Y117.mp3?url";
import effectZhsh from "../../assets/fruit_sfx/Y118.mp3?url";
import effectDmg from "../../assets/fruit_sfx/Y120.mp3?url";
import effectXnsh from "../../assets/fruit_sfx/Y122.mp3?url";
import effectKhc from "../../assets/fruit_sfx/Y124.mp3?url";
import effectSendLights from "../../assets/fruit_sfx/Y131.mp3?url";
import { FRUIT_WHEEL_FRUIT_IDS } from "../net/protocolFruit";

/** 特殊牌型 → 固定效果音（对齐 luckyFruit play*Effect）。 */
const SPECIAL_EFFECT_URL: Record<string, string> = {
  CONTINUITY: effectSendLights,
  DSX: effectDsx,
  DSY: effectDsy,
  XSY: effectXsy,
  ZHSH: effectZhsh,
  XNSH: effectXnsh,
  HC: effectKhc,
  DMG: effectDmg,
  /** 原版无独立资源时回落送灯提示音。 */
  JLBD: effectSendLights,
  TLBB: effectSendLights,
};

const FRUIT_BY_ID: Record<number, string> = {
  1: fruit1,
  2: fruit2,
  3: fruit3,
  4: fruit4,
  5: fruit5,
  6: fruit6,
  7: fruit7,
  8: fruit8,
};

/** C01–C15 中奖曲：Vite 分包，首次中奖时才拉取对应 chunk。 */
const winJingleLoaders = import.meta.glob<string>(
  "../../assets/fruit_sfx/win/C*.mp3",
  { query: "?url", import: "default" },
);

const winJingleUrlCache = new Map<string, string>();

const PRELOAD_URLS = [
  rotateStart,
  rotating,
  rotateEnd,
  ding,
  zhongle,
  fail,
  hit,
  hitLucky,
  luckySuccess,
  luckyFail,
  sendLights,
  lightBoo,
  effectDsy,
  effectXsy,
  effectDsx,
  effectZhsh,
  effectDmg,
  effectXnsh,
  effectKhc,
  effectSendLights,
  ...Object.values(FRUIT_BY_ID),
];

let rotatingEl: HTMLAudioElement | null = null;
let winJingleEl: HTMLAudioElement | null = null;
let sendLightsEl: HTMLAudioElement | null = null;
/** 本局是否已切入慢段（rotateEnd 只播一次）。 */
let spinSlowStarted = false;

/** 跑灯最快/切入慢段时的间隔（与 FruitPanel.spinStepDelayMs 对齐）。 */
const SPIN_DELAY_FAST_MS = 16;
const SPIN_SLOW_FROM_MS = 55;
/** 循环旋转音：快→中 的 playbackRate。 */
const SPIN_RATE_FAST = 1.75;
const SPIN_RATE_MID = 1.0;

function canPlay(): boolean {
  return typeof window !== "undefined";
}

function stopAudioEl(el: HTMLAudioElement | null): void {
  if (!el) return;
  try {
    el.pause();
    el.currentTime = 0;
  } catch {
    /* ignore */
  }
}

function playOne(url: string, volume = 0.5, loop = false): HTMLAudioElement | null {
  if (!canPlay()) return null;
  const a = new Audio(url);
  a.volume = volume;
  a.loop = loop;
  a.setAttribute("playsinline", "true");
  void a.play().catch(() => {});
  return a;
}

function playOneAsync(url: string, volume = 0.5): Promise<void> {
  if (!canPlay()) return Promise.resolve();
  return new Promise((resolve) => {
    const a = new Audio(url);
    a.volume = volume;
    a.setAttribute("playsinline", "true");
    const done = () => resolve();
    a.addEventListener("ended", done, { once: true });
    a.addEventListener("error", done, { once: true });
    void a.play().catch(done);
  });
}

function stopRotatingLoop(): void {
  stopAudioEl(rotatingEl);
  rotatingEl = null;
}

function stopWinJingle(): void {
  stopAudioEl(winJingleEl);
  winJingleEl = null;
}

function stopSendLightsLoop(): void {
  stopAudioEl(sendLightsEl);
  sendLightsEl = null;
}

async function loadWinJingleUrl(key: string): Promise<string | null> {
  const cached = winJingleUrlCache.get(key);
  if (cached) return cached;
  const loader = winJingleLoaders[key];
  if (!loader) return null;
  try {
    const url = await loader();
    winJingleUrlCache.set(key, url);
    return url;
  } catch (e) {
    console.warn("[fruit-sfx] win jingle load", key, e);
    return null;
  }
}

/** luckyFruit：中奖后随机播 C01–C15 之一（按需加载，不打进主包）。 */
async function playRandomWinJingle(volume = 0.4): Promise<void> {
  if (!canPlay()) return;
  const keys = Object.keys(winJingleLoaders);
  if (!keys.length) return;
  const key = keys[(Math.random() * keys.length) | 0]!;
  const url = await loadWinJingleUrl(key);
  if (!url) return;
  // 后触发的停掉前一条，避免中奖曲互相叠加
  stopWinJingle();
  if (!canPlay()) return;
  winJingleEl = playOne(url, volume);
  if (winJingleEl) {
    const el = winJingleEl;
    const clear = () => {
      if (winJingleEl === el) winJingleEl = null;
    };
    el.addEventListener("ended", clear, { once: true });
    el.addEventListener("error", clear, { once: true });
  }
}

/** 默认轮盘各格 fruitId（1=BAR…8=苹果）；与 mock_server_fruit 默认 wheel 一致。 */
export function fruitIdAtWheelPos(pos: number): number {
  const id = FRUIT_WHEEL_FRUIT_IDS[(pos >>> 0) % 24] ?? 0;
  return id >>> 0;
}

export function preloadFruitSfx(): void {
  if (typeof window === "undefined") return;
  for (const url of PRELOAD_URLS) {
    const a = new Audio(url);
    a.preload = "auto";
  }
}

export function fruitSfxSpinBegin(): void {
  spinSlowStarted = false;
  stopRotatingLoop();
  stopWinJingle();
  playOne(rotateStart, 0.5);
  rotatingEl = playOne(rotating, 0.35, true);
  if (rotatingEl) rotatingEl.playbackRate = SPIN_RATE_FAST;
}

/**
 * 与跑灯间隔同步：
 * - 快段：调 rotating 循环的 playbackRate
 * - 慢段：停循环，播一次 rotateEnd，之后每格叮一声
 */
export function fruitSfxSpinWheelStep(delayMs: number): void {
  if (delayMs >= SPIN_SLOW_FROM_MS) {
    if (!spinSlowStarted) {
      spinSlowStarted = true;
      stopRotatingLoop();
      // 对齐原版 playRotateEndAni：减速段主音效
      playOne(rotateEnd, 0.5);
    }
    playOne(ding, 0.18);
    return;
  }
  if (!rotatingEl) return;
  const span = Math.max(1, SPIN_SLOW_FROM_MS - SPIN_DELAY_FAST_MS);
  const t = Math.min(1, Math.max(0, (delayMs - SPIN_DELAY_FAST_MS) / span));
  rotatingEl.playbackRate = SPIN_RATE_FAST + (SPIN_RATE_MID - SPIN_RATE_FAST) * t;
}

export function fruitSfxSpinEnd(): void {
  stopRotatingLoop();
  // 极短路径未进慢段时补播；正常局 rotateEnd 已在慢段开头播过
  if (!spinSlowStarted) playOne(rotateEnd, 0.5);
  playOne(ding, 0.22);
  spinSlowStarted = false;
}

export function fruitSfxStopAll(): void {
  stopRotatingLoop();
  stopWinJingle();
  stopSendLightsLoop();
  spinSlowStarted = false;
}

export function fruitSfxLightBoo(): void {
  playOne(lightBoo, 0.35);
}

export function fruitSfxSendLights(): void {
  playOne(sendLights, 0.4);
}

export function fruitSfxSendLightsLoop(): void {
  stopSendLightsLoop();
  sendLightsEl = playOne(sendLights, 0.35, true);
}

export function fruitSfxStopSendLights(): void {
  stopSendLightsLoop();
}

/**
 * 特殊牌型固定提示音（送灯动画结束后播，对齐 luckyFruit playXnshEffect 等）。
 * 音量 0.7 与原版 playAudioByAudioMap 一致。
 */
export async function fruitSfxSpecialFanfare(
  cardGroupType: string,
): Promise<void> {
  if (!canPlay()) return;
  const url = SPECIAL_EFFECT_URL[cardGroupType];
  if (url) {
    await playOneAsync(url, 0.7);
    return;
  }
  await playOneAsync(zhongle, 0.4);
}

/** 落点水果 / 幸运格语音。 */
export async function fruitSfxLand(
  fruitId: number,
  cardGroupType: string,
): Promise<void> {
  if (!canPlay()) return;
  if (cardGroupType === "LUCK") {
    await playOneAsync(hitLucky, 0.35);
    return;
  }
  const url = FRUIT_BY_ID[fruitId >>> 0];
  if (url) await playOneAsync(url, 0.6);
}

/** 开奖结果：中奖 / 未中 / 幸运成功失败。 */
export function fruitSfxSpinResult(win: number, cardGroupType: string): void {
  if (!canPlay()) return;
  const special =
    cardGroupType !== "SINGLE" &&
    cardGroupType !== "BONUS" &&
    cardGroupType !== "LUCK";
  if (special) {
    /* 固定牌型音已在送灯结束后播过；中奖曲与原版 playWinAudio 一致 */
    if (win > 0) void playRandomWinJingle(0.4);
    return;
  }
  if (win > 0) {
    playOne(zhongle, 0.35);
    void playRandomWinJingle(0.4);
    if (cardGroupType === "LUCK") playOne(sendLights, 0.5);
    if (cardGroupType === "LUCK") playOne(luckySuccess, 0.25);
    return;
  }
  playOne(fail, 0.45);
  if (cardGroupType === "LUCK") playOne(luckyFail, 0.25);
}

export function fruitSfxInsert(): void {
  playOne(hit, 0.35);
}

export function fruitSfxCollect(): void {
  playOne(ding, 0.25);
}

export function fruitSfxCompare(compareType: string | undefined): void {
  if (!canPlay()) return;
  if (compareType === "WIN") playOne(zhongle, 0.35);
  else if (compareType === "TIED") playOne(ding, 0.2);
  else playOne(fail, 0.35);
}
