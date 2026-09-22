/** Designation overhead — GIF catalog + Pixi text titles (arena kill-streak). */

const BY_NAME: Record<string, string> = {
  一贫如洗: "riches_name0.gif",
  衣食无忧: "riches_name1.gif",
  生财有道: "riches_name2.gif",
  理财有方: "riches_name3.gif",
  财运亨通: "riches_name4.gif",
  腰缠万贯: "riches_name5.gif",
  家财万贯: "riches_name6.gif",
  富商巨贾: "riches_name7.gif",
  富甲一方: "riches_name8.gif",
  富可敌国: "riches_name9.gif",
  不堪一击: "level_name0.gif",
  初学乍练: "level_name1.gif",
  小试牛刀: "level_name2.gif",
  初露锋芒: "level_name3.gif",
  出人头地: "level_name4.gif",
  名震江湖: "level_name5.gif",
  江湖枭雄: "level_name6.gif",
  了然于胸: "level_name7.gif",
  炉火纯青: "level_name8.gif",
  江湖侠隐: "level_name9.gif",
  登峰造极: "level_name10.gif",
  超越极限: "level_name11.gif",
  开山鼻祖: "level_name12.gif",
  圣诞骑士: "sdqs0.gif",
  武林传奇: "wlcq0.gif",
  资深老友勇者王: "zsly.gif",
  资深老友: "zsly0.gif",
  勇者王: "zsly1.gif",
};

/** Pixi-only：比武连杀文字称号（服务端 id 30–40，overhead 空）。
 * 颜色与世界频道连杀播报称号着色一致（ChatPanel 共用本表）。 */
export const ARENA_TEXT_TITLE_COLORS: { title: string; color: number }[] = [
  { title: "敌将讨伐", color: 0xfff0c0 },
  { title: "双杀", color: 0xffc060 },
  { title: "大杀特杀", color: 0xffa040 },
  { title: "所向披靡", color: 0xffe040 },
  { title: "浑身是胆", color: 0x80ff40 },
  { title: "势如破竹", color: 0x40ffb0 },
  { title: "勇冠三军", color: 0x40e8ff },
  { title: "一骑当千", color: 0x6080ff },
  { title: "万夫莫敌", color: 0xc080ff },
  { title: "举世无双", color: 0xff60c8 },
  { title: "天下无敌", color: 0xff2020 },
  { title: "天下第一", color: 0xffd040 },
];

const ARENA_TEXT_TITLE_SET = new Set(
  ARENA_TEXT_TITLE_COLORS.map((r) => r.title),
);

/** Resolve actor overhead badge resource from wire shortLabel / previewImage. */
export function resolveTitleGif(labelOrGif: string | undefined | null): string | null {
  if (!labelOrGif) return null;
  const s = labelOrGif.trim();
  if (!s) return null;
  if (ARENA_TEXT_TITLE_SET.has(s)) return null;
  if (/\.gif$/i.test(s)) return s;
  return BY_NAME[s] ?? null;
}

export function isArenaTextTitle(label: string | undefined | null): boolean {
  if (!label) return false;
  return ARENA_TEXT_TITLE_SET.has(label.trim());
}

export function arenaTextTitleColor(label: string | undefined | null): number {
  const s = (label || "").trim();
  for (let i = ARENA_TEXT_TITLE_COLORS.length - 1; i >= 0; i--) {
    const row = ARENA_TEXT_TITLE_COLORS[i]!;
    if (row.title === s) return row.color;
  }
  return 0xffe080;
}
