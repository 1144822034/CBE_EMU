/**
 * Left quest-tracker panel collapsed to a single expand button.
 * Independent from map-corner status strip (修/拍/…).
 */

const LS_KEY = "pix_quest_tracker_collapsed";

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
