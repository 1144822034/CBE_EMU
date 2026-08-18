/**
 * Map-corner status strip (修/拍/擂/…) collapsed to a single expand button.
 */

const LS_KEY = "pix_status_strip_collapsed";

let collapsed = readCollapsed();

function readCollapsed(): boolean {
  try {
    if (typeof localStorage === "undefined") return false;
    return localStorage.getItem(LS_KEY) === "1";
  } catch {
    return false;
  }
}

function writeCollapsed(on: boolean): void {
  try {
    if (typeof localStorage === "undefined") return;
    localStorage.setItem(LS_KEY, on ? "1" : "0");
  } catch {
    /* ignore */
  }
}

export function isStatusStripCollapsed(): boolean {
  return collapsed;
}

export function setStatusStripCollapsed(on: boolean): void {
  collapsed = !!on;
  writeCollapsed(collapsed);
}

export function toggleStatusStripCollapsed(): boolean {
  setStatusStripCollapsed(!collapsed);
  return collapsed;
}
