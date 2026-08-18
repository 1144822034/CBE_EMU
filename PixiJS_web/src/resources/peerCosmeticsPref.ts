/**
 * Client-only: show/hide nearby players' wings / mounts / animated titles.
 * Does not change server cosmetics or local self overlays.
 */

const LS_KEY = "pix_peer_cosmetics";

let visible = readVisible();

function readVisible(): boolean {
  try {
    if (typeof localStorage === "undefined") return true;
    const v = localStorage.getItem(LS_KEY);
    // Default on; only explicit "0" hides.
    if (v === null) return true;
    return v !== "0";
  } catch {
    return true;
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
