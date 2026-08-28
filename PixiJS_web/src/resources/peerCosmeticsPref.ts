/**
 * Client-only: show/hide nearby players' wings / mounts / animated titles,
 * and pick e_deity sheet (on → e_deity.png, off → stock e_deity.gif).
 * Does not change server cosmetics or local self overlays.
 */

const LS_KEY = "pix_peer_cosmetics";

let visible = readVisible();

function readVisible(): boolean {
  try {
    if (typeof localStorage === "undefined") return false;
    const v = localStorage.getItem(LS_KEY);
    // Default off; only explicit "1" shows.
    if (v === null) return false;
    return v !== "0";
  } catch {
    return false;
  }
}

function writeVisible(on: boolean): void {
  try {
    if (typeof localStorage === "undefined") return;
    localStorage.setItem(LS_KEY, on ? "1" : "0");
  } catch {
    /* ignore */
  }
}

export function isPeerCosmeticsVisible(): boolean {
  return visible;
}

export function setPeerCosmeticsVisible(on: boolean): void {
  visible = !!on;
  writeVisible(visible);
}

/** Flip and persist; returns the new visible state. */
export function togglePeerCosmeticsVisible(): boolean {
  setPeerCosmeticsVisible(!visible);
  return visible;
}
