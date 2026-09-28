/**
 * Same-page multi-account slots (APK / PC / browser).
 * Each slot owns a GameSession + LiveTransport; one Pixi viewport shows the active slot.
 * Limit: admin Client UI → multiOpenLimit on panel 1/1/21.
 */

const LS_USER = "pixi.jh.account";
const LS_PASS = "pixi.jh.password";

let activeCredSlotId = "main";

export function setActiveCredSlotId(slotId: string): void {
  activeCredSlotId = String(slotId || "main").trim() || "main";
}

export function getActiveCredSlotId(): string {
  return activeCredSlotId;
}

function slotCredKeys(slotId: string): { user: string; pass: string } {
  if (!slotId || slotId === "main") {
    return { user: LS_USER, pass: LS_PASS };
  }
  return {
    user: `pixi.jh.slot.${slotId}.account`,
    pass: `pixi.jh.slot.${slotId}.password`,
  };
}

export function allocMultiOpenSlotId(): string {
  const r =
    typeof crypto !== "undefined" && "randomUUID" in crypto
      ? crypto.randomUUID()
      : `s${Date.now().toString(36)}${Math.random().toString(36).slice(2, 8)}`;
  return r.replace(/[^a-zA-Z0-9_-]/g, "").slice(0, 32) || `s${Date.now()}`;
}

export function readSlotCredentials(slotId = getActiveCredSlotId()): {
  user: string;
  password: string;
} | null {
  if (typeof window === "undefined") return null;
  try {
    const keys = slotCredKeys(slotId);
    let user = String(window.localStorage.getItem(keys.user) || "").trim();
    let password = String(window.localStorage.getItem(keys.pass) || "");
    if (!user && slotId === "main") {
      user = String(window.localStorage.getItem(LS_USER) || "").trim();
      password = String(window.localStorage.getItem(LS_PASS) || "");
    }
    if (!user) return null;
    return { user, password };
  } catch {
    return null;
  }
}

export function writeSlotCredentials(
  user: string,
  password: string,
  slotId = getActiveCredSlotId(),
): void {
  if (typeof window === "undefined") return;
  try {
    const keys = slotCredKeys(slotId);
    window.localStorage.setItem(keys.user, user);
    window.localStorage.setItem(keys.pass, password);
    if (slotId === "main") {
      window.localStorage.setItem(LS_USER, user);
      window.localStorage.setItem(LS_PASS, password);
    }
  } catch {
    /* ignore */
  }
}

/** Clamp client-side parse of admin multiOpenLimit. */
export function clampMultiOpenLimit(raw: number): number {
  const n = Math.floor(Number(raw) || 1);
  if (n < 1) return 1;
  if (n > 8) return 8;
  return n;
}

/** MRU list of accounts that successfully filled the login form (device-local). */
const LS_ACCOUNT_HISTORY = "pixi.jh.accountHistory";
const ACCOUNT_HISTORY_MAX = 12;

export type CachedAccount = { user: string; password: string };

function normalizeHistoryUser(user: string): string {
  return String(user || "").trim().slice(0, 32);
}

export function readAccountHistory(): CachedAccount[] {
  if (typeof window === "undefined") return [];
  try {
    const raw = window.localStorage.getItem(LS_ACCOUNT_HISTORY);
    if (!raw) {
      // Seed from the active slot so returning players see at least one entry.
      const cur = readSlotCredentials();
      if (!cur?.user) return [];
      const seeded = [{ user: cur.user, password: cur.password || "" }];
      window.localStorage.setItem(LS_ACCOUNT_HISTORY, JSON.stringify(seeded));
      return seeded;
    }
    const parsed = JSON.parse(raw) as unknown;
    if (!Array.isArray(parsed)) return [];
    const out: CachedAccount[] = [];
    const seen = new Set<string>();
    for (const row of parsed) {
      if (!row || typeof row !== "object") continue;
      const user = normalizeHistoryUser(
        String((row as { user?: unknown }).user || ""),
      );
      if (!user || seen.has(user.toLowerCase())) continue;
      seen.add(user.toLowerCase());
      out.push({
        user,
        password: String((row as { password?: unknown }).password || ""),
      });
      if (out.length >= ACCOUNT_HISTORY_MAX) break;
    }
    return out;
  } catch {
    return [];
  }
}

/** Upsert MRU; call when the player commits credentials on login/register. */
export function rememberAccount(user: string, password: string): void {
  if (typeof window === "undefined") return;
  const name = normalizeHistoryUser(user);
  if (!name) return;
  try {
    const next: CachedAccount[] = [
      { user: name, password: String(password || "") },
    ];
    const key = name.toLowerCase();
    for (const row of readAccountHistory()) {
      if (row.user.toLowerCase() === key) continue;
      next.push(row);
      if (next.length >= ACCOUNT_HISTORY_MAX) break;
    }
    window.localStorage.setItem(LS_ACCOUNT_HISTORY, JSON.stringify(next));
  } catch {
    /* ignore */
  }
}

export function removeAccountFromHistory(user: string): void {
  if (typeof window === "undefined") return;
  const name = normalizeHistoryUser(user);
  if (!name) return;
  try {
    const key = name.toLowerCase();
    const next = readAccountHistory().filter(
      (row) => row.user.toLowerCase() !== key,
    );
    window.localStorage.setItem(LS_ACCOUNT_HISTORY, JSON.stringify(next));
  } catch {
    /* ignore */
  }
}
