/**
 * Client-only: where battle enemy HP bars paint.
 * - corner: stacked under the title bar (top-left)
 * - follow: one bar above each left-side unit
 */

export type EnemyHpBarPlacement = "corner" | "follow";

export type EnemyHpBarOption = {
  id: EnemyHpBarPlacement;
  label: string;
};

const LS_KEY = "pix_enemy_hp_bar";

/** Default matches the current top-left HUD. */
export const DEFAULT_ENEMY_HP_BAR: EnemyHpBarPlacement = "corner";

export const ENEMY_HP_BAR_OPTIONS: readonly EnemyHpBarOption[] = [
  { id: "corner", label: "左上角" },
  { id: "follow", label: "跟随怪" },
];

let placement = readPlacement();

function readPlacement(): EnemyHpBarPlacement {
  try {
    if (typeof localStorage === "undefined") return DEFAULT_ENEMY_HP_BAR;
    const v = localStorage.getItem(LS_KEY);
    if (v === "follow" || v === "corner") return v;
  } catch {
    /* ignore */
  }
  return DEFAULT_ENEMY_HP_BAR;
}

function writePlacement(p: EnemyHpBarPlacement): void {
  try {
    if (typeof localStorage === "undefined") return;
    localStorage.setItem(LS_KEY, p);
  } catch {
    /* ignore */
  }
}

export function getEnemyHpBarPlacement(): EnemyHpBarPlacement {
  return placement;
}

export function setEnemyHpBarPlacement(p: EnemyHpBarPlacement): void {
  placement = p === "follow" ? "follow" : "corner";
  writePlacement(placement);
}

export function enemyHpBarPlacementLabel(
  p: EnemyHpBarPlacement = placement,
): string {
  return ENEMY_HP_BAR_OPTIONS.find((o) => o.id === p)?.label ?? "左上角";
}
