/**
 * Left quest-tracker panel collapse state.
 * Login always starts collapsed; expand only after the player taps the strip.
 * Independent from map-corner status strip (修/拍/…).
 */

const LS_KEY = "pix_quest_tracker_collapsed";

/** Prefer collapsed when unset — matches「登录默认收缩」. */
let collapsed = readCollapsed();

function readCollapsed(): boolean {
  try {
    if (typeof localStorage === "undefined") return true;
    const v = localStorage.getItem(LS_KEY);
    if (v === null) return true;
    return v === "1";
  } catch {
    return true;
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

export function isQuestTrackerCollapsed(): boolean {
  return collapsed;
}

export function setQuestTrackerCollapsed(on: boolean): void {
  collapsed = !!on;
  writeCollapsed(collapsed);
}

export function toggleQuestTrackerCollapsed(): boolean {
  setQuestTrackerCollapsed(!collapsed);
  return collapsed;
}
