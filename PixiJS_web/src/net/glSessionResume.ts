/**
 * Short-lived sessionStorage snapshot so GL soft-reload (location.reload)
 * can re-enter the same account/server/role instead of dumping to splash.
 *
 * Evidence: docs/re/2026-08-29-pixi-recover-soft-reload-to-splash.md
 * Industry: Pixi #11196 — real contextlost often needs hard restart.
 */

export const GL_SESSION_RESUME_KEY = "pix_gl_session_resume";
/** Soft-reload must resume within this window or fall back to splash. */
export const GL_SESSION_RESUME_TTL_MS = 90_000;

export type GlSessionResumeSnapshot = {
  v: 1;
  user: string;
  password: string;
  serverId: number;
  roleId: number;
  hangupDesired: boolean;
  autoBattle: boolean;
  why: string;
  ts: number;
};

const LS_USER = "pixi.jh.account";
const LS_PASS = "pixi.jh.password";

export function readStoredLoginCredentials(): {
  user: string;
  password: string;
} | null {
  if (typeof window === "undefined") return null;
  try {
    const user = String(window.localStorage.getItem(LS_USER) || "").trim();
    const password = String(window.localStorage.getItem(LS_PASS) || "");
    if (!user) return null;
    return { user, password };
  } catch {
    return null;
  }
}

export function writeGlSessionResume(
  snap: Omit<GlSessionResumeSnapshot, "v" | "ts"> & { ts?: number },
): void {
  if (typeof window === "undefined") return;
  try {
    const full: GlSessionResumeSnapshot = {
      v: 1,
      user: String(snap.user || "").trim(),
      password: String(snap.password || ""),
      serverId: (Number(snap.serverId) >>> 0) || 1,
      roleId: Number(snap.roleId) >>> 0,
      hangupDesired: !!snap.hangupDesired,
      autoBattle: !!snap.autoBattle,
      why: String(snap.why || ""),
      ts: snap.ts ?? Date.now(),
    };
    if (!full.user || !full.roleId) return;
    window.sessionStorage.setItem(GL_SESSION_RESUME_KEY, JSON.stringify(full));
  } catch {
    /* private mode / quota */
  }
}

export function peekGlSessionResume(): GlSessionResumeSnapshot | null {
  if (typeof window === "undefined") return null;
  try {
    const raw = window.sessionStorage.getItem(GL_SESSION_RESUME_KEY);
    if (!raw) return null;
    const o = JSON.parse(raw) as Partial<GlSessionResumeSnapshot>;
    if (!o || o.v !== 1) return null;
    const user = String(o.user || "").trim();
    const roleId = Number(o.roleId) >>> 0;
    const ts = Number(o.ts) || 0;
    if (!user || !roleId || !ts) return null;
    if (Date.now() - ts > GL_SESSION_RESUME_TTL_MS) return null;
    return {
      v: 1,
      user,
      password: String(o.password || ""),
      serverId: (Number(o.serverId) >>> 0) || 1,
      roleId,
      hangupDesired: !!o.hangupDesired,
      autoBattle: !!o.autoBattle,
      why: String(o.why || ""),
      ts,
    };
  } catch {
    return null;
  }
}

/** Read and clear. Expired / corrupt → clear and return null. */
export function takeGlSessionResume(): GlSessionResumeSnapshot | null {
  const snap = peekGlSessionResume();
  clearGlSessionResume();
  return snap;
}

export function clearGlSessionResume(): void {
  if (typeof window === "undefined") return;
  try {
    window.sessionStorage.removeItem(GL_SESSION_RESUME_KEY);
  } catch {
    /* ignore */
  }
}
