/**
 * Client mirror of server role EXP curve
 * (`docs/re/2026-07-25-role-level-exp-curve.md` / mock_server_role.c).
 * Used for attribute-panel display: 本级当前经验 / 升级所需.
 */

export const ROLE_MAX_LEVEL = 70;

/** EXP to advance L→L+1 for L=1..49 (exact server table). */
const LEVEL_UP_COST_GEOM: readonly number[] = [
  200, 240, 292, 354, 429, 520, 630, 764, 926, 1123, 1361, 1649, 1998, 2421,
  2934, 3555, 4307, 5218, 6323, 7660, 9282, 11246, 13625, 16507, 19999, 24230,
  29355, 35565, 43088, 52202, 63245, 76623, 92831, 112467, 136257, 165080,
  199999, 242305, 293559, 355655, 430886, 522030, 632455, 766236, 928317,
  1124682, 1362583, 1650807, 2000000,
];

const STAGE3_A = 2_100_000;
const STAGE3_D = 350_242;
const STAGE3_FIRST = 50;
const GEOM_LAST = 49;

/** Cost to advance from `level` to `level + 1`. */
export function roleLevelUpCost(level: number): number {
  const lv = level >>> 0;
  if (lv < 1) return 0;
  if (lv <= GEOM_LAST) {
    return LEVEL_UP_COST_GEOM[lv - 1] ?? 0xffffffff;
  }
  const cost = STAGE3_A + (lv - STAGE3_FIRST) * STAGE3_D;
  return cost > 0xffffffff ? 0xffffffff : cost >>> 0;
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
