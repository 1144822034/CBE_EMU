/**
 * 水果机特殊牌型送灯动画（对齐 luckyFruit MainGame.runLightingEffect）。
 */
import type { FruitSpinResult } from "../../net/protocolFruit";
import {
  fruitSfxLightBoo,
  fruitSfxSendLights,
  fruitSfxSendLightsLoop,
  fruitSfxSpecialFanfare,
  fruitSfxStopSendLights,
} from "../../audio/FruitSfx";

const sleep = (ms: number) => new Promise<void>((r) => window.setTimeout(r, ms));

export type FruitLightApi = {
  turnOn: (pos: number) => void;
  turnOff: (pos: number) => void;
  turnOffAll: () => void;
  setCursor: (pos: number) => void;
};

/** 正反跑灯：收集 cardGroup 中每一格。 */
async function zigzagCollect(
  api: FruitLightApi,
  triggerPos: number,
  targets: number[],
): Promise<void> {
  if (!targets.length) return;
  let cur = triggerPos % 24;
  const lighted = new Set<number>([cur]);
  api.turnOffAll();
  api.turnOn(cur);
  api.setCursor(cur);
  let dir = 1;
  let lightIndex = 0;
  fruitSfxSendLightsLoop();
  const guardMax = 24 * targets.length + 48;
  let guard = 0;
  while (lightIndex < targets.length && guard++ < guardMax) {
    const prev = cur;
    if (!lighted.has(prev)) api.turnOff(prev);
    cur = (cur + dir + 24) % 24;
    api.turnOn(cur);
    api.setCursor(cur);
    if (cur === targets[lightIndex]) {
      lighted.add(cur);
      lightIndex++;
      dir *= -1;
      fruitSfxLightBoo();
      if (lightIndex < targets.length) {
        await sleep(400);
        fruitSfxSendLightsLoop();
      }
    } else {
      await sleep(45);
    }
  }
  fruitSfxStopSendLights();
  for (const p of targets) api.turnOn(p % 24);
}

async function alternateFlash(
  api: FruitLightApi,
  groups: number[][],
  rounds: number,
): Promise<void> {
  fruitSfxSendLights();
  let alt = 0;
  for (let left = rounds; left > 0; left--) {
    api.turnOffAll();
    const g = groups[alt % groups.length] ?? [];
    for (const p of g) api.turnOn(((p % 24) + 24) % 24);
    alt++;
    await sleep(180);
  }
  fruitSfxStopSendLights();
}

async function orderAllOn(api: FruitLightApi, start: number): Promise<void> {
  fruitSfxSendLights();
  api.turnOffAll();
  let idx = start % 24;
  for (let n = 0; n < 24; n++) {
    api.turnOn(idx);
    api.setCursor(idx);
    idx = (idx + 1) % 24;
    await sleep(80);
  }
  fruitSfxStopSendLights();
}

async function trainRun(
  api: FruitLightApi,
  triggerPos: number,
  trainLen: number,
): Promise<void> {
  fruitSfxSendLightsLoop();
  const len = Math.max(2, Math.min(8, trainLen));
  let head = triggerPos % 24;
  const steps = 24 + ((triggerPos - head + 24) % 24);
  for (let s = 0; s < steps + 8; s++) {
    api.turnOffAll();
    for (let i = 0; i < len; i++) api.turnOn((head + i) % 24);
    api.setCursor(head);
    head = (head + 1) % 24;
    await sleep(90);
  }
  fruitSfxStopSendLights();
  api.turnOffAll();
  for (let i = 0; i < len; i++) api.turnOn((triggerPos + i) % 24);
}

const ZHSH_SIDES: number[][] = [
  [21, 22, 23, 0, 1, 2, 3],
  [3, 4, 5, 6, 7, 8, 9],
  [9, 10, 11, 12, 13, 14, 15],
  [15, 16, 17, 18, 19, 20, 21],
];

const XNSH_ALT: number[][] = [
  [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22],
  [1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23],
];

export async function playFruitSpecialLighting(
  api: FruitLightApi,
  result: FruitSpinResult,
): Promise<void> {
  const type = result.cardGroupType || "SINGLE";
  const group =
    result.cardGroup.length > 0 ? result.cardGroup : [result.triggerPos];
  const trigger = result.triggerPos % 24;

  if (type === "SINGLE" || type === "BONUS" || type === "LUCK") {
    api.turnOffAll();
    api.turnOn(trigger);
    api.setCursor(trigger);
    return;
  }

  // 固定牌型音在动画结束后播（对齐 luckyFruit runLightingEffect）。
  if (type === "CONTINUITY" || type === "DSX" || type === "DSY" || type === "XSY") {
    await zigzagCollect(api, trigger, group);
    await fruitSfxSpecialFanfare(type);
    return;
  }
  if (type === "ZHSH") {
    let side = 0;
    for (let i = 0; i < ZHSH_SIDES.length; i++) {
      if (group.some((p) => ZHSH_SIDES[i]!.includes(p))) {
        side = i;
        break;
      }
    }
    await alternateFlash(api, ZHSH_SIDES, 4 * 3 + side + 1);
    api.turnOffAll();
    for (const p of group) api.turnOn(p % 24);
    await fruitSfxSpecialFanfare(type);
    return;
  }
  if (type === "XNSH") {
    const last = group[group.length - 1] ?? trigger;
    const idx = last % 2;
    await alternateFlash(api, XNSH_ALT, 2 * 8 + idx + 1);
    api.turnOffAll();
    for (const p of group) api.turnOn(p % 24);
    await fruitSfxSpecialFanfare(type);
    return;
  }
  if (type === "DMG" || type === "JLBD" || type === "TLBB") {
    await orderAllOn(api, trigger);
    api.turnOffAll();
    for (const p of group) api.turnOn(p % 24);
    await fruitSfxSpecialFanfare(type);
    return;
  }
  if (type === "HC") {
    await trainRun(api, trigger, group.length || 4);
    await fruitSfxSpecialFanfare(type);
    return;
  }

  api.turnOffAll();
  for (const p of group) api.turnOn(p % 24);
  await fruitSfxSpecialFanfare(type);
}
