/**
 * Pre-title 「江湖支招」 tip pool extracted from `bin/CBE/江湖OL.cbe`
 * (GBK strings near 0x502EB, ~256-byte slots + one leading poem).
 *
 * Color tags follow CBE markup: c0 cream, c2/c9 cyan highlight.
 */

export type TipSegment = {
  text: string;
  /** Pixi hex color */
  color: number;
};

export type BootTip = {
  /** Raw body without leading title line; may still contain cN tags. */
  raw: string;
  segments: TipSegment[];
  plain: string;
};

const CREAM = 0xfdf8c2;
const CYAN = 0x00ffff;

const COLOR_BY_TAG: Record<string, number> = {
  "0": CREAM,
  "1": 0xffcc66,
  "2": CYAN,
  "6": CREAM,
  "7": 0xffcc66,
  "9": CYAN,
};

/** Raw tip strings as stored in CBE (title may be embedded). */
const RAW_TIPS: string[] = [
  "c0仗剑c2江湖c0载酒行，赤胆雄心照前程，我辈岂是池中物，一遇风云化做龙。",
  "c2【江湖支招】\nc0玫兰杜康寄友情，笑看江湖谁最风流!",
  "c2【江湖支招】\nc0一段故事一个江湖，一场姻缘一段佳话。",
  "c9【江湖支招】蜀山有情聚一帮，一帮兄弟一个家!",
  "c9【江湖支招】桃缘再劫，临安月老渡你心愿达成。",
  "c9擂台宗主广邀天下英雄共赴蜀山南，踢台夺金。",
  "c9江湖儿女的友好度达到3000，即可成就一段良缘!",
];

function parseSegments(raw: string, initialColor = CREAM): TipSegment[] {
  const parts = raw.split(/(c[0-9])/);
  const out: TipSegment[] = [];
  let color = initialColor;
  for (const part of parts) {
    if (/^c[0-9]$/.test(part)) {
      color = COLOR_BY_TAG[part[1]!] ?? CREAM;
      continue;
    }
    if (!part) continue;
    // Drop embedded title — UI draws it separately.
    const cleaned = part
      .replace(/^【江湖支招】\s*/u, "")
      .replace(/\r?\n/g, "");
    if (!cleaned) continue;
    const last = out[out.length - 1];
    if (last && last.color === color) last.text += cleaned;
    else out.push({ text: cleaned, color });
  }
  return out.filter((s) => s.text.length > 0);
}

/** Autotest shots often cyan-tint bare 「江湖」 even without a c2 tag. */
function highlightJianghu(segments: TipSegment[]): TipSegment[] {
  const out: TipSegment[] = [];
  for (const seg of segments) {
    if (seg.color !== CREAM || !seg.text.includes("江湖")) {
      out.push(seg);
      continue;
    }
    const parts = seg.text.split(/(江湖)/);
    for (const p of parts) {
      if (!p) continue;
      out.push({ text: p, color: p === "江湖" ? CYAN : CREAM });
    }
  }
  return out;
}

function buildTip(raw: string): BootTip {
  // Body color after a leading c9 title tag should still read as cream in shots.
  let bodyRaw = raw;
  if (/^c9【江湖支招】/.test(bodyRaw)) {
    bodyRaw = bodyRaw.replace(/^c9【江湖支招】/, "c0");
  } else if (/^c9/.test(bodyRaw) && !bodyRaw.includes("【江湖支招】")) {
    // Tips that omit the title in the string still open with c9 — body is cream.
    bodyRaw = "c0" + bodyRaw.slice(2);
  }
  const segments = highlightJianghu(parseSegments(bodyRaw));
  const plain = segments.map((s) => s.text).join("");
  return { raw, segments, plain };
}

export const BOOT_TIPS: BootTip[] = RAW_TIPS.map(buildTip);

export const TIP_TITLE = "【江湖支招】";

/** Random tip each open; avoids immediate repeat when pool has >1 entry. */
export function pickBootTip(prevIndex = -1): { tip: BootTip; index: number } {
  const n = BOOT_TIPS.length;
  if (n <= 1) return { tip: BOOT_TIPS[0]!, index: 0 };
  let index = Math.floor(Math.random() * n);
  if (index === prevIndex) index = (index + 1 + Math.floor(Math.random() * (n - 1))) % n;
  return { tip: BOOT_TIPS[index]!, index };
}
