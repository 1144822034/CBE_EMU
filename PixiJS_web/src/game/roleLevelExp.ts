/**
 * Client mirror of server role EXP curve
 * (`docs/re/2026-07-25-role-level-exp-curve.md` / mock_server_role.c).
 * Used for attribute-panel display: 本级当前经验 / 升级所需.
 */

export const ROLE_MAX_LEVEL = 70;

/**
 * EXP to advance L→L+1 for L=1..69 (exact server table).
 * Piecewise geometric between anchors:
 * 1→2=200, 29→30=5368, 39→40=85800, 49→50=1040000,
 * 59→60=11704000, 69→70=100100000.
 */
const LEVEL_UP_COST_TABLE: readonly number[] = [
  200, 225, 253, 285, 320, 360, 405, 455, 512, 576, 648, 728, 819, 921,
  1036, 1165, 1311, 1474, 1658, 1864, 2097, 2358, 2652, 2983, 3355, 3773,
  4244, 4773, 5368, 7082, 9344, 12329, 16266, 21461, 28315, 37358, 49289,
  65031, 85800, 110114, 141318, 181364, 232759, 298717, 383367, 492005,
  631428, 810361, 1040000, 1324840, 1687693, 2149927, 2738758, 3488862,
  4444408, 5661663, 7212306, 9187645, 11704000, 14505887, 17978534,
  22282517, 27616855, 34228211, 42422296, 52578010, 65164959, 80765168,
  100100000,
];

/** Cost to advance from `level` to `level + 1`. */
export function roleLevelUpCost(level: number): number {
  const lv = level >>> 0;
  if (lv < 1) return 0;
  return LEVEL_UP_COST_TABLE[lv - 1] ?? 0xffffffff;
}

/** Cumulative EXP required to reach `level` (level 1 = 0). */
export function roleLevelStartExp(level: number): number {
  const lv = level >>> 0;
  if (lv <= 1) return 0;
  let start = 0;
  for (let from = 1; from < lv; from++) {
    const step = roleLevelUpCost(from);
    if (step === 0xffffffff || start > 0xffffffff - step) return 0xffffffff;
    start += step;
  }
  return start >>> 0;
}

/**
 * Attribute panel: 本级当前经验 / 本级升级所需（非累计总经验）.
 * At max level shows progress against 「满」.
 */
export function formatPanelLevelExp(totalExp: number, level: number): string {
  const lv = Math.max(1, level >>> 0);
  const total = totalExp >>> 0;
  const last = roleLevelStartExp(lv);
  const cur = total > last ? total - last : 0;
  if (lv >= ROLE_MAX_LEVEL) return `${cur}/满`;
  const need = roleLevelUpCost(lv);
  return `${cur}/${need}`;
}
