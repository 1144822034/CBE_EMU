/**
 * Arena kill-streak voice lines (梦三国式连杀播报).
 * Plays on world announce regardless of BGM / 「开关声音」 preference.
 *
 * Source clips: comma = 分段；空格连接的词在同一段里（如「三杀 大杀特杀」）。
 */
import { publicAssetUrl } from "../resources/publicUrl";

/**
 * Title → asset. 播报称号命中即可；对应段可能还带「三杀/四杀/五杀」前缀人声。
 * 1敌将讨伐 2双杀 3「三杀 大杀特杀」4「四杀 所向披靡」5「五杀 浑身是胆」
 * 6势如破竹 7勇冠三军 8一骑当千 9万夫莫敌 10举世无双 11天下无敌
 */
const TITLE_TO_PATH: Record<string, string> = {
  敌将讨伐: "JHOnlineData/arena_kill_sfx/arena_kill_1.mp3",
  双杀: "JHOnlineData/arena_kill_sfx/arena_kill_2.mp3",
  三杀: "JHOnlineData/arena_kill_sfx/arena_kill_3.mp3",
  大杀特杀: "JHOnlineData/arena_kill_sfx/arena_kill_3.mp3",
  四杀: "JHOnlineData/arena_kill_sfx/arena_kill_4.mp3",
  所向披靡: "JHOnlineData/arena_kill_sfx/arena_kill_4.mp3",
  五杀: "JHOnlineData/arena_kill_sfx/arena_kill_5.mp3",
  浑身是胆: "JHOnlineData/arena_kill_sfx/arena_kill_5.mp3",
  势如破竹: "JHOnlineData/arena_kill_sfx/arena_kill_6.mp3",
  勇冠三军: "JHOnlineData/arena_kill_sfx/arena_kill_7.mp3",
  一骑当千: "JHOnlineData/arena_kill_sfx/arena_kill_8.mp3",
  万夫莫敌: "JHOnlineData/arena_kill_sfx/arena_kill_9.mp3",
  举世无双: "JHOnlineData/arena_kill_sfx/arena_kill_10.mp3",
  天下无敌: "JHOnlineData/arena_kill_sfx/arena_kill_11.mp3",
};

const cache = new Map<string, HTMLAudioElement>();
let current: HTMLAudioElement | null = null;
let gestureUnlocked = false;

function getPlayer(rel: string): HTMLAudioElement {
  let a = cache.get(rel);
  if (a) return a;
  a = new Audio(publicAssetUrl(rel));
  a.preload = "auto";
  a.setAttribute("playsinline", "true");
  a.volume = 0.9;
  cache.set(rel, a);
  return a;
}

/** Play voice for a known streak title; no-op if unknown / empty. */
export function playKillStreakTitle(title: string | undefined | null): void {
  const key = (title || "").trim();
  const rel = TITLE_TO_PATH[key];
  if (!rel) return;
  try {
    if (current) {
      current.pause();
      current.currentTime = 0;
    }
    const a = getPlayer(rel);
    current = a;
    a.volume = 0.9;
    a.currentTime = 0;
    void a.play().catch((e) => console.warn("[kill-sfx] play", key, e));
  } catch (e) {
    console.warn("[kill-sfx]", key, e);
  }
}

/** Warm decode so first announce is less delayed. */
export function preloadKillStreakSfx(): void {
  for (const rel of Object.values(TITLE_TO_PATH)) {
    try {
      getPlayer(rel);
    } catch {
      /* ignore */
    }
  }
}

/**
 * Satisfy autoplay policy even when BGM preference is off.
 * Call from the same user-gesture path as BGM unlock.
 */
export function unlockKillStreakSfx(): void {
  preloadKillStreakSfx();
  if (gestureUnlocked) return;
  const first = Object.values(TITLE_TO_PATH)[0];
  if (!first) return;
  try {
    const a = getPlayer(first);
    const prev = a.volume;
    a.volume = 0.001;
    void a
      .play()
      .then(() => {
        a.pause();
        a.currentTime = 0;
        a.volume = prev;
        gestureUnlocked = true;
      })
      .catch(() => {
        a.volume = prev;
      });
  } catch {
    /* ignore */
  }
}
