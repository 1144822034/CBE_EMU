/** luckyFruit machine1_bg (480×800) → LCD，底对齐露出押注区。 */
import { FRUIT_BET } from "../../net/protocolFruit";
import { LCD_HEIGHT, LCD_WIDTH, SOFTKEY_H } from "../constants";

export const FRUIT_BG_W = 480;
export const FRUIT_BG_H = 800;
export const FRUIT_DESIGN_W = 720;
export const FRUIT_DESIGN_H = 1280;

const GAME_H = LCD_HEIGHT - SOFTKEY_H;

/** 铺满宽度 480→240。 */
export const FRUIT_BG_SCALE = LCD_WIDTH / FRUIT_BG_W;

/**
 * 背景高 400 > GAME_H 368，上移 32px，让底部押注槽露在 softkey 之上。
 * （否则押注键落在 y≈368 被 softkey 挡住。）
 */
export const FRUIT_UI_OX = 0;
export const FRUIT_UI_OY = GAME_H - FRUIT_BG_H * FRUIT_BG_SCALE;

/** Cocos 机台节点里背景被非等比拉伸。 */
const STRETCH_X = FRUIT_DESIGN_W / FRUIT_BG_W; // 1.5
const STRETCH_Y = FRUIT_DESIGN_H / FRUIT_BG_H; // 1.6

export const FRUIT_UI_SCALE = FRUIT_BG_SCALE / STRETCH_X;

export function fruitDesignToScreen(
  x: number,
  y: number,
): { x: number; y: number } {
  return {
    x: (x / STRETCH_X) * FRUIT_BG_SCALE + FRUIT_UI_OX,
    y: ((FRUIT_DESIGN_H - y) / STRETCH_Y) * FRUIT_BG_SCALE + FRUIT_UI_OY,
  };
}

/** 图素坐标（bg 自上而下）→ 屏。 */
export function fruitBgPixelToScreen(
  px: number,
  py: number,
): { x: number; y: number } {
  return {
    x: px * FRUIT_BG_SCALE + FRUIT_UI_OX,
    y: py * FRUIT_BG_SCALE + FRUIT_UI_OY,
  };
}

/**
 * 24 格中心：直接取 machine1_bg 暗格实测中心（米色格 blob），
 * 避免 design 拉伸换算后上下/左右误差方向不一致。
 * LUCK 两格非米色，由相邻格插值。
 */
export const FRUIT_WHEEL_SLOTS: readonly {
  node: string;
  sprite: string;
  x: number;
  y: number;
}[] = (
  [
    ["luck_zuo", "luck_zuo.png", 70.2, 345.2],
    ["pingguo", "pingguo.png", 70.5, 287.5],
    ["tongling_X3", "tongling_X3.png", 70.0, 230.0],
    ["juzi", "juzi.png", 70.5, 176.0],
    ["tongling", "tongling.png", 124.0, 175.5],
    ["bar_X50", "bar_X50.png", 182.0, 175.0],
    ["bar_x100", "bar_x100.png", 239.5, 175.0],
    ["pingguo", "pingguo.png", 297.0, 175.0],
    ["pingguo_X3", "pingguo_X3.png", 355.0, 175.0],
    ["mangguo", "mangguo.png", 409.5, 175.5],
    ["xigua", "xigua.png", 410.0, 230.0],
    ["xigua_X3", "xigua_X3.png", 409.5, 287.5],
    ["luck_you", "luck_you.png", 409.8, 345.2],
    ["pingguo", "pingguo.png", 410.0, 403.0],
    ["juzi_X3", "juzi_X3.png", 409.5, 461.0],
    ["juzi", "juzi.png", 409.0, 515.0],
    ["tongling", "tongling.png", 355.0, 516.0],
    ["77_X3", "77_X3.png", 297.0, 515.5],
    ["77", "77.png", 239.5, 515.0],
    ["pingguo", "pingguo.png", 182.0, 515.5],
    ["mangguo_X3", "mangguo_X3.png", 124.0, 516.0],
    ["mangguo", "mangguo.png", 70.5, 515.5],
    ["xingxing", "xingxing.png", 70.0, 461.0],
    ["xingxing_X3", "xingxing_X3.png", 70.0, 403.0],
  ] as const
).map(([node, sprite, bpx, bpy]) => {
  const p = fruitBgPixelToScreen(bpx, bpy);
  return { node, sprite, x: p.x, y: p.y };
});

/**
 * 底部 8 押注。
 * 背景只有空槽，圆形图标必须用图集 anniu_* 叠上去。
 */
export const FRUIT_BET_UI: readonly {
  betId: number;
  atlasKey: string;
  payout: number;
  x: number;
  y: number;
  countX: number;
  countY: number;
}[] = (
  [
    [FRUIT_BET.BAR, "anniu_bar_01.png", 120, 49, 103, 64, 188],
    [FRUIT_BET.SEVEN, "anniu_77_01.png", 40, 140, 103, 150, 188],
    [FRUIT_BET.STAR, "anniu_xingxing_01.png", 30, 228, 103, 236, 188],
    [FRUIT_BET.WATERMELON, "anniu_xigua_01.png", 20, 314, 103, 318, 188],
    [FRUIT_BET.BELL, "anniu_tongzhong_01.png", 20, 403, 103, 400, 188],
    [FRUIT_BET.MANGO, "anniu_ningmeng_01.png", 15, 491, 103, 480, 188],
    [FRUIT_BET.ORANGE, "anniu_juzi_01.png", 10, 578, 103, 568, 188],
    [FRUIT_BET.APPLE, "anniu_pingguo_01.png", 5, 673, 105, 648, 188],
  ] as const
).map(([betId, atlasKey, payout, bx, by, cx, cy]) => {
  const btn = fruitDesignToScreen(bx, by);
  const count = fruitDesignToScreen(cx, cy);
  return {
    betId,
    atlasKey,
    payout,
    x: btn.x,
    y: btn.y,
    countX: count.x,
    countY: count.y,
  };
});

export const FRUIT_HUD = {
  bonusWin: fruitDesignToScreen(360 - 172, 640 + 454),
  credit: fruitDesignToScreen(360 + 156, 640 + 455),
  go: fruitDesignToScreen(358 + 242, 327 + 5),
  compareSmall: fruitDesignToScreen(358 - 15, 327 + 5),
  compareBig: fruitDesignToScreen(358 + 94, 327 + 5),
  pointLeft: fruitDesignToScreen(358 - 262, 327 + 5),
  pointRight: fruitDesignToScreen(358 - 140, 327 + 5),
  /** 中央 point 数码管（原版 point/pointCount，现兼显示下注倍数）。 */
  point: fruitDesignToScreen(360, 610),
};

/**
 * 中间赔率（背景只烤了两端 120 / 5）。
 * 用图集 `*_liang.png`，y≈224 对齐赔率条，切勿再用 BitmapLabel 以免叠到轮盘。
 */
export const FRUIT_ODDS_UI: readonly {
  atlasKey: string;
  x: number;
  y: number;
}[] = (
  [
    ["40_liang.png", 140],
    ["30_liang.png", 228],
    ["20_liang.png", 314],
    ["20_liang.png", 403],
    ["15_liang.png", 491],
    ["10_liang.png", 578],
  ] as const
).map(([atlasKey, dx]) => {
  const p = fruitDesignToScreen(dx, 224);
  return { atlasKey, x: p.x, y: p.y };
});

/** Cocos fruitButtons 节点 scale≈1.3。 */
export const FRUIT_BET_BTN_SCALE = FRUIT_UI_SCALE * 1.3;
