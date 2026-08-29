/**
 * Game BGM: play jh.mp3 → ss.mp3 → bgad.mp3 in sequence, repeat while enabled.
 * Works in browser and Capacitor Android WebView after a user gesture.
 */

import { publicAssetUrl } from "../resources/publicUrl";

const LS_KEY = "pix_sound_enabled";

const TRACK_PATHS = [
  "JHOnlineData/jh.mp3",
  "JHOnlineData/ss.mp3",
  "JHOnlineData/bgad.mp3",
] as const;

let enabled = readEnabled();
let starting = false;
/** Playlist index into TRACK_PATHS — preserved across brief suspend. */
let trackIndex = 0;
let playGen = 0;

let players: HTMLAudioElement[] | null = null;

function readEnabled(): boolean {
  try {
    const v = localStorage.getItem(LS_KEY);
    // Default off when unset; only explicit "1" enables.
    if (v === null) return false;
    return v === "1";
  } catch {
    return false;
  }
}

function writeEnabled(on: boolean): void {
  try {
    localStorage.setItem(LS_KEY, on ? "1" : "0");
  } catch {
    /* ignore */
  }
}

function ensurePlayers(): HTMLAudioElement[] {
  if (players) return players;
  players = TRACK_PATHS.map((rel, i) => {
    const a = new Audio(publicAssetUrl(rel));
    a.loop = false;
    a.preload = "auto";
    a.setAttribute("playsinline", "true");
    a.volume = 0.55;
    a.addEventListener("ended", () => {
      if (!enabled) return;
      trackIndex = (i + 1) % TRACK_PATHS.length;
      void playCurrentTrack();
    });
    return a;
  });
  return players;
}

function stopCurrentSources(): void {
  if (!players) return;
  for (const a of players) {
    try {
      a.pause();
      a.currentTime = 0;
    } catch {
      /* ignore */
    }
  }
}

async function playCurrentTrack(): Promise<void> {
  if (!enabled || starting) return;
  const gen = ++playGen;
  starting = true;
  try {
    const list = ensurePlayers();
    stopCurrentSources();
    if (!enabled || gen !== playGen) return;
    const a = list[trackIndex] ?? list[0]!;
    try {
      a.currentTime = 0;
      await a.play();
    } catch (e) {
      console.warn("[bgm] play", TRACK_PATHS[trackIndex], e);
      if (!enabled || gen !== playGen) return;
      trackIndex = (trackIndex + 1) % TRACK_PATHS.length;
      queueMicrotask(() => {
        void playCurrentTrack();
      });
    }
  } finally {
    starting = false;
  }
}

function stopPlayback(): void {
  playGen++;
  stopCurrentSources();
}

function isActivelyPlaying(): boolean {
  if (!players) return false;
  return players.some((a) => !a.paused && !a.ended);
}

/** Current preference (persisted). Default off when unset. */
export function isSoundEnabled(): boolean {
  return enabled;
}

/**
 * Toggle / set BGM. Enabling starts playlist from jh.mp3.
 * Returns the new state.
 */
export async function setSoundEnabled(on: boolean): Promise<boolean> {
  enabled = on;
  writeEnabled(on);
  if (on) {
    trackIndex = 0;
    await playCurrentTrack();
  } else stopPlayback();
  return enabled;
}

export async function toggleSoundEnabled(): Promise<boolean> {
  return setSoundEnabled(!enabled);
}

/** Unlock / resume if preference is on and nothing is playing. */
export async function unlockAndResumeBgm(): Promise<void> {
  if (!enabled) return;
  try {
    if (!isActivelyPlaying()) await playCurrentTrack();
  } catch (e) {
    console.warn("[bgm] resume", e);
  }
}

/** After WebView background: rebuild current playlist track. */
export async function resumeBgmAfterForeground(): Promise<void> {
  if (!enabled) return;
  stopCurrentSources();
  await playCurrentTrack();
}

/** Pause when app backgrounds; keep trackIndex for resume. */
export function suspendBgm(): void {
  playGen++;
  stopCurrentSources();
}
