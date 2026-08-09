/** Designation overhead GIFs — same catalog as mock_server_role.c / docs/re/2026-07-02. */
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
};

/** Resolve actor overhead badge resource from wire shortLabel / previewImage. */
export function resolveTitleGif(labelOrGif: string | undefined | null): string | null {
  if (!labelOrGif) return null;
  const s = labelOrGif.trim();
  if (!s) return null;
  if (/\.gif$/i.test(s)) return s;
  return BY_NAME[s] ?? null;
}
