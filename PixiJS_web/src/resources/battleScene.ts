/**
 * Resolve outdoor scene → dedicated battle map SCE (`b_*.sce` in JHOnlineData).
 *
 * Evidence: admin portal field 0x12 stores `b_*.sce`; outdoor `17无情谷_01.sce`
 * pairs with `b_17无情谷.sce` (user + file census).
 */
export function battleSceCandidatesFromWorld(worldScene: string): string[] {
  const file = worldScene.replace(/^.*[/\\]/, "").trim();
  let base = file.replace(/\.sce$/i, "");
  if (!base) return [];

  const out: string[] = [];
  const push = (s: string) => {
    const name = s.toLowerCase().endsWith(".sce") ? s : `${s}.sce`;
    if (!out.includes(name)) out.push(name);
  };

  // Already a battle map.
  if (/^b_/i.test(base)) {
    push(`${base}.sce`);
    return out;
  }

  // Strip trailing area suffix: 17无情谷_01 → 17无情谷
  const stripped = base.replace(/_\d+$/, "");
  push(`b_${stripped}`);

  // 17无情谷m variant (some maps ship both).
  push(`b_${stripped}m`);

  // 00_蓬莱仙岛01 / 00蓬莱仙岛_02 style → try digit+name core.
  const m = stripped.match(/^(\d+)[_]?(.+)$/);
  if (m) {
    const num = m[1]!;
    const name = m[2]!.replace(/^_/, "");
    push(`b_${num}${name}`);
    push(`b_${num}_${name}`);
    // name-only fallback used by some maps (b_黑龙潭.sce)
    if (name) push(`b_${name}`);
  }

  return out;
}

/** Pick first candidate that exists via probe (async). */
export async function resolveBattleSce(
  worldScene: string,
  exists: (name: string) => Promise<boolean>,
): Promise<string | null> {
  for (const cand of battleSceCandidatesFromWorld(worldScene)) {
    if (await exists(cand)) return cand;
  }
  return null;
}
