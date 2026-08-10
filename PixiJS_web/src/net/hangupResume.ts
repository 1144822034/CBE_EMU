import { Capacitor, registerPlugin } from "@capacitor/core";

/** localStorage key for hangup intent + credentials (device-local). */
export const HANGUP_RESUME_KEY = "pix_hangup_resume";

export type HangupResumeSnapshot = {
  desired: boolean;
  user: string;
  password: string;
  serverId: number;
  roleId: number;
  operate: number;
  savedAt: number;
};

/** Native FGS mode: in-world online vs AFK hangup (stronger copy). */
export type NativeKeepAliveMode = "online" | "hangup";

interface HangupKeepAlivePlugin {
  start(options?: { mode?: NativeKeepAliveMode }): Promise<void>;
  stop(): Promise<void>;
}

const HangupKeepAlive = registerPlugin<HangupKeepAlivePlugin>("HangupKeepAlive");

export function readHangupResume(): HangupResumeSnapshot | null {
  if (typeof window === "undefined") return null;
  try {
    const raw = window.localStorage.getItem(HANGUP_RESUME_KEY);
    if (!raw) return null;
    const o = JSON.parse(raw) as Partial<HangupResumeSnapshot>;
    if (!o || o.desired !== true) return null;
    const user = String(o.user || "").trim();
    const password = String(o.password || "");
    const roleId = Number(o.roleId) >>> 0;
    if (!user || !roleId) return null;
    return {
      desired: true,
      user,
      password,
      serverId: (Number(o.serverId) >>> 0) || 1,
      roleId,
      operate: (Number(o.operate) | 0) || 0,
      savedAt: Number(o.savedAt) || 0,
    };
  } catch {
    return null;
  }
}

export function writeHangupResume(snap: HangupResumeSnapshot): void {
  if (typeof window === "undefined") return;
  try {
    window.localStorage.setItem(HANGUP_RESUME_KEY, JSON.stringify(snap));
  } catch {
    /* private mode */
  }
}

export function clearHangupResumeStorage(): void {
  if (typeof window === "undefined") return;
  try {
    window.localStorage.removeItem(HANGUP_RESUME_KEY);
  } catch {
    /* ignore */
  }
}

/**
 * APK foreground service so OEM Doze / WebView pause does not kill gateway WS.
 * Hangup mode wins over plain online; both off → stop.
 */
export async function syncNativeKeepAlive(opts: {
  inWorld: boolean;
  hangup: boolean;
}): Promise<void> {
  if (!Capacitor.isNativePlatform()) return;
  try {
    if (opts.hangup) {
      await HangupKeepAlive.start({ mode: "hangup" });
    } else if (opts.inWorld) {
      await HangupKeepAlive.start({ mode: "online" });
    } else {
      await HangupKeepAlive.stop();
    }
  } catch (e) {
    console.warn("[keepalive] native", e);
  }
}

/** @deprecated prefer syncNativeKeepAlive — hangup-only toggle kept for call sites. */
export async function setHangupNativeKeepAlive(on: boolean): Promise<void> {
  if (!Capacitor.isNativePlatform()) return;
  try {
    if (on) await HangupKeepAlive.start({ mode: "hangup" });
    else await HangupKeepAlive.stop();
  } catch (e) {
    console.warn("[hangup] native keepalive", e);
  }
}
