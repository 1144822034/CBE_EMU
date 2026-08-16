/**
 * Arena kill-streak voice lines (梦三国式连杀播报).
 * Plays on world announce regardless of BGM / 「开关声音」 preference.
 *
 * Clips are Vite-bundled (`assets/arena_kill_sfx`) — not served via the
 * JHOnlineData middleware (that middleware rejects nested paths → 400 in dev).
 */
import kill1 from "../../assets/arena_kill_sfx/arena_kill_1.mp3?url";
import kill2 from "../../assets/arena_kill_sfx/arena_kill_2.mp3?url";
import kill3 from "../../assets/arena_kill_sfx/arena_kill_3.mp3?url";
import kill4 from "../../assets/arena_kill_sfx/arena_kill_4.mp3?url";
import kill5 from "../../assets/arena_kill_sfx/arena_kill_5.mp3?url";
import kill6 from "../../assets/arena_kill_sfx/arena_kill_6.mp3?url";
import kill7 from "../../assets/arena_kill_sfx/arena_kill_7.mp3?url";
import kill8 from "../../assets/arena_kill_sfx/arena_kill_8.mp3?url";
import kill9 from "../../assets/arena_kill_sfx/arena_kill_9.mp3?url";
import kill10 from "../../assets/arena_kill_sfx/arena_kill_10.mp3?url";
import kill11 from "../../assets/arena_kill_sfx/arena_kill_11.mp3?url";

/**
 * Title → bundled URL. 播报称号命中即可；对应段可能还带「三杀/四杀/五杀」前缀人声。
 * 1敌将讨伐 2双杀 3「三杀 大杀特杀」4「四杀 所向披靡」5「五杀 浑身是胆」
 * 6势如破竹 7勇冠三军 8一骑当千 9万夫莫敌 10举世无双 11天下无敌
 */
const TITLE_TO_URL: Record<string, string> = {
  敌将讨伐: kill1,
  双杀: kill2,
  三杀: kill3,
  大杀特杀: kill3,
  四杀: kill4,
  所向披靡: kill4,
  五杀: kill5,
  浑身是胆: kill5,
  势如破竹: kill6,
  勇冠三军: kill7,
  一骑当千: kill8,
  万夫莫敌: kill9,
  举世无双: kill10,
  天下无敌: kill11,
};

const buffers = new Map<string, AudioBuffer>();
const loading = new Map<string, Promise<AudioBuffer | null>>();
let ctx: AudioContext | null = null;
let currentSource: AudioBufferSourceNode | null = null;
let htmlPlayer: HTMLAudioElement | null = null;
let pendingTitle: string | null = null;
let unlocked = false;

function uniqueUrls(): string[] {
  return [...new Set(Object.values(TITLE_TO_URL))];
}

function ensureCtx(): AudioContext | null {
  if (typeof window === "undefined") return null;
  const AC =
    window.AudioContext ||
    (window as unknown as { webkitAudioContext?: typeof AudioContext })
      .webkitAudioContext;
  if (!AC) return null;
  if (!ctx) ctx = new AC();
  return ctx;
}

/** Must call resume() inside the user-gesture call stack (no prior await). */
function kickResume(): AudioContext | null {
  const c = ensureCtx();
  if (!c) return null;
  if (c.state === "suspended") {
    void c.resume().then(() => {
      unlocked = c.state === "running";
    });
  } else {
    unlocked = true;
  }
  return c;
}

async function resumeCtx(): Promise<AudioContext | null> {
  const c = kickResume();
  if (!c) return null;
  if (c.state === "suspended") {
    try {
      await c.resume();
    } catch (e) {
      console.warn("[kill-sfx] AudioContext.resume", e);
    }
  }
  unlocked = c.state === "running";
  return c;
}

async function loadBuffer(url: string): Promise<AudioBuffer | null> {
  const hit = buffers.get(url);
  if (hit) return hit;
  const inflight = loading.get(url);
  if (inflight) return inflight;

  const job = (async () => {
    try {
      const res = await fetch(url);
      if (!res.ok) {
        console.warn("[kill-sfx] fetch", res.status, url);
        return null;
      }
      const ab = await res.arrayBuffer();
      const c = await resumeCtx();
      if (!c) return null;
      const buf = await c.decodeAudioData(ab.slice(0));
      buffers.set(url, buf);
      return buf;
    } catch (e) {
      console.warn("[kill-sfx] decode", url, e);
      return null;
    } finally {
      loading.delete(url);
    }
  })();
  loading.set(url, job);
  return job;
}

function stopCurrent(): void {
  if (currentSource) {
    try {
      currentSource.stop(0);
    } catch {
      /* ignore */
    }
    try {
      currentSource.disconnect();
    } catch {
      /* ignore */
    }
    currentSource = null;
  }
  if (htmlPlayer) {
    try {
      htmlPlayer.pause();
      htmlPlayer.currentTime = 0;
    } catch {
      /* ignore */
    }
  }
}

function ensureHtmlPlayer(): HTMLAudioElement {
  if (htmlPlayer) return htmlPlayer;
  htmlPlayer = new Audio();
  htmlPlayer.preload = "auto";
  htmlPlayer.setAttribute("playsinline", "true");
  htmlPlayer.volume = 0.9;
  return htmlPlayer;
}

function playHtml(url: string, key: string): void {
  const a = ensureHtmlPlayer();
  stopCurrent();
  a.volume = 0.9;
  if (a.src !== url) a.src = url;
  try {
    a.currentTime = 0;
  } catch {
    /* ignore */
  }
  void a.play().then(
    () => {
      pendingTitle = null;
      console.info("[kill-sfx] html-play", key);
    },
    (e) => {
      console.warn("[kill-sfx] html-play", key, e);
      pendingTitle = key;
    },
  );
}

async function playWebAudio(url: string, key: string): Promise<boolean> {
  const c = await resumeCtx();
  if (!c || c.state !== "running") return false;
  const buf = await loadBuffer(url);
  if (!buf) return false;
  stopCurrent();
  const src = c.createBufferSource();
  src.buffer = buf;
  const gain = c.createGain();
  gain.gain.value = 0.9;
  src.connect(gain);
  gain.connect(c.destination);
  currentSource = src;
  src.onended = () => {
    if (currentSource === src) currentSource = null;
  };
  try {
    src.start(0);
    pendingTitle = null;
    console.info("[kill-sfx] webaudio", key);
    return true;
  } catch (e) {
    console.warn("[kill-sfx] start", key, e);
    return false;
  }
}

async function playUrl(url: string, key: string): Promise<void> {
  const ok = await playWebAudio(url, key);
  if (ok) return;
  playHtml(url, key);
}

/** Play voice for a known streak title; no-op if unknown / empty. */
export function playKillStreakTitle(title: string | undefined | null): void {
  const key = (title || "").trim();
  const url = TITLE_TO_URL[key];
  if (!url) {
    console.warn("[kill-sfx] unknown title", title);
    return;
  }
  void playUrl(url, key).catch((e) => console.warn("[kill-sfx]", key, e));
}

/** Warm decode so first announce is less delayed. */
export function preloadKillStreakSfx(): void {
  for (const url of uniqueUrls()) {
    void loadBuffer(url);
  }
  ensureHtmlPlayer();
}

/**
 * Satisfy autoplay policy. Must be invoked from a real user gesture.
 * Flushes a title that arrived before unlock.
 */
export function unlockKillStreakSfx(): void {
  kickResume();
  const a = ensureHtmlPlayer();
  /* Mute-unlock one element; Web Audio path uses AudioContext.resume above. */
  try {
    const first = uniqueUrls()[0];
    if (first && !a.src) a.src = first;
    const prev = a.volume;
    a.volume = 0.001;
    void a
      .play()
      .then(() => {
        a.pause();
        a.currentTime = 0;
        a.volume = prev;
      })
      .catch(() => {
        a.volume = prev;
      });
  } catch {
    /* ignore */
  }
  unlocked = true;
  if (pendingTitle) {
    const t = pendingTitle;
    pendingTitle = null;
    playKillStreakTitle(t);
  }
}

/** @internal test / diagnostics */
export function killStreakSfxUnlocked(): boolean {
  return unlocked && !!ctx && ctx.state === "running";
}
