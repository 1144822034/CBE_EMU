/**
 * Map nameplate title FX from JHOnlineData/chenghao TexturePacker atlases.
 * Temporary client catalog (86001–86048); later gate by server unlock.
 */
import { Container, Sprite } from "pixi.js";
import {
  loadPlistAtlas,
  rebindPlistAtlas,
  type LoadedPlistAtlas,
  type PlistFrame,
} from "./plistAtlas";

export type ChenghaoSpec = {
  id: number;
  /** List / overhead display name. */
  name?: string;
};

/** All chenghao sheets under `JHOnlineData/chenghao/sfx_<id>_0`. Edit `name` for list. */
export const CHENGHAO_SPECS: readonly ChenghaoSpec[] = [
  { id: 86001, name: "开天辟地" },
  { id: 86002, name: "千古一帝" },
  { id: 86003, name: "神挡杀神" },
  { id: 86004, name: "捉鬼大师" },
  { id: 86005, name: "仙人之上一换一" },
  { id: 86006, name: "仙人之上一换十" },
  { id: 86007, name: "仙人之上一换百" },
  { id: 86008, name: "仙人之上我最浪" },
  { id: 86009, name: "九千九百九十九个界" },
  { id: 86010, name: "战无不胜" },
  { id: 86011, name: "天下第一" },
  { id: 86012, name: "绝世无双" },
  { id: 86013, name: "技冠群雄" },
  { id: 86014, name: "大哥带你飞" },
  { id: 86015, name: "单挑小王子" },
  { id: 86016, name: "痞帅街溜子" },
  { id: 86017, name: "专治各种不服" },
  { id: 86018, name: "泡8喝9玩10尚" },
  { id: 86019, name: "实力不允许低调" },
  { id: 86020, name: "我养你啊" },
  { id: 86021, name: "是真男人来砍我" },
  { id: 86022, name: "一天三顿小烧烤" },
  { id: 86024, name: "夜闯寡妇村" },
  { id: 86025, name: "我自横刀向天笑" },
  { id: 86026, name: "独孤求败" },
  { id: 86027, name: "万事皆与钱有缘" },
  { id: 86028, name: "一刀倒一片" },
  { id: 86029, name: "小楼昨夜又听雨" },
  { id: 86031, name: "唯我独尊" },
  { id: 86032, name: "凤翔九天" },
  { id: 86033, name: "飞龙在天" },
  { id: 86034, name: "与天同寿" },
  { id: 86036, name: "烽火戏诸侯" },
  { id: 86037, name: "怒斩百万雄" },
  { id: 86038, name: "无敌最寂寞" },
  { id: 86039, name: "无敌幸运星" },
  { id: 86040, name: "唯我独尊2" },
  { id: 86041, name: "雄才大略" },
  { id: 86042, name: "舍我其谁" },
  { id: 86043, name: "天下无双" },
  { id: 86044, name: "纵横四海" },
  { id: 86045, name: "义薄云天" },
  { id: 86046, name: "威震天下" },
  { id: 86047, name: "拔地倚天" },
  { id: 86048, name: "十步杀一人" },
];

export const CHENGHAO_IDS: readonly number[] = CHENGHAO_SPECS.map((s) => s.id);

const SPEC_BY_ID = new Map(CHENGHAO_SPECS.map((s) => [s.id, s]));

const FRAME_MS = 100;
const MAX_DISPLAY_H = 48;
/** Positive = closer to the nameplate (Pixi Y down). */
const DISPLAY_Y = 10;

/** Wire / preference tag so nameplate can resolve atlas (not a real .gif). */
export function chenghaoWireTag(id: number): string {
  return `chenghao:${id | 0}`;
}

export function isChenghaoId(id: number): boolean {
  return SPEC_BY_ID.has(id | 0);
}

export function getChenghaoSpec(id: number): ChenghaoSpec | null {
  return SPEC_BY_ID.get(id | 0) ?? null;
}

export function chenghaoLabel(id: number): string {
  const spec = getChenghaoSpec(id);
  if (!spec) return `称号${id}`;
  return spec.name || String(spec.id);
}

/** Parse `chenghao:86001`, `sfx_86001`, or bare id in known range. */
export function parseChenghaoId(raw: string | null | undefined): number {
  const s = String(raw || "").trim();
  if (!s) return 0;
  let m = /^chenghao:(\d+)$/i.exec(s);
  if (m) {
    const id = Number(m[1]);
    return isChenghaoId(id) ? id : 0;
  }
  m = /^sfx_(\d+)(?:_0)?(?:\.plist)?$/i.exec(s);
  if (m) {
    const id = Number(m[1]);
    return isChenghaoId(id) ? id : 0;
  }
  if (/^\d+$/.test(s)) {
    const id = Number(s);
    return isChenghaoId(id) ? id : 0;
  }
  return 0;
}

export function chenghaoAtlasPath(id: number): string {
  return `chenghao/sfx_${id | 0}_0`;
}

export function readChenghaoPreference(): number {
  try {
    if (typeof localStorage === "undefined") return 0;
    const raw = localStorage.getItem("pixi.chenghaoId");
    if (raw == null || raw === "") return 0;
    const n = Number(raw);
    return Number.isFinite(n) && isChenghaoId(n) ? n | 0 : 0;
  } catch {
    return 0;
  }
}

export function writeChenghaoPreference(id: number): void {
  try {
    if (typeof localStorage === "undefined") return;
    const n = id | 0;
    if (!n) localStorage.removeItem("pixi.chenghaoId");
    else localStorage.setItem("pixi.chenghaoId", String(n));
  } catch {
    /* ignore */
  }
}

/** Animated overhead badge for chenghao atlases. */
export class ChenghaoOverlay {
  readonly root = new Container();
  private sprite: Sprite | null = null;
  private atlas: LoadedPlistAtlas | null = null;
  private titleId = 0;
  private phase = 0;
  private animAcc = 0;
  private loadGen = 0;

  constructor() {
    this.root.visible = false;
    this.root.eventMode = "none";
    this.root.zIndex = 0;
  }

  get currentId(): number {
    return this.titleId;
  }

  clear(): void {
    this.loadGen++;
    this.titleId = 0;
    this.atlas = rebindPlistAtlas(this.atlas, null);
    this.phase = 0;
    this.animAcc = 0;
    this.root.visible = false;
    if (this.sprite) {
      this.root.removeChild(this.sprite);
      this.sprite.destroy({ texture: false });
      this.sprite = null;
    }
  }

  async setTitleId(titleId: number): Promise<void> {
    const id = titleId | 0;
    if (!id) {
      this.clear();
      return;
    }
    if (!isChenghaoId(id)) {
      console.warn("[chenghao] unlisted id, trying atlas", id);
    }
    if (id === this.titleId && this.atlas) {
      this.root.visible = true;
      this.layout();
      return;
    }
    const gen = ++this.loadGen;
    this.titleId = id;
    try {
      // stableFace: drop Y-spin / near-empty cells (e.g. 86043 tumble loop).
      const atlas = await loadPlistAtlas(chenghaoAtlasPath(id), {
        stableFace: true,
      });
      if (gen !== this.loadGen) return;
      this.atlas = rebindPlistAtlas(this.atlas, atlas);
      this.phase = 0;
      this.animAcc = 0;
      if (!this.sprite) {
        this.sprite = new Sprite(atlas.textures[0]!);
        this.sprite.anchor.set(0.5, 1);
        this.root.addChild(this.sprite);
      } else {
        this.sprite.texture = atlas.textures[0]!;
      }
      this.root.visible = true;
      this.layout();
    } catch (e) {
      if (gen !== this.loadGen) return;
      console.warn("[chenghao] load failed", id, e);
      this.clear();
    }
  }

  tick(dtMs: number): void {
    if (!this.root.visible || !this.atlas || !this.sprite) return;
    const n = this.atlas.textures.length;
    if (n <= 1) return;
    this.animAcc += dtMs;
    let stepped = false;
    while (this.animAcc >= FRAME_MS) {
      this.animAcc -= FRAME_MS;
      this.phase = (this.phase + 1) % n;
      stepped = true;
    }
    if (stepped) {
      this.sprite.texture = this.atlas.textures[this.phase]!;
    }
  }

  private currentFrame(): PlistFrame | null {
    if (!this.atlas) return null;
    return this.atlas.frames[this.phase] ?? this.atlas.frames[0] ?? null;
  }

  private layout(): void {
    if (!this.sprite || !this.atlas) return;
    const fr = this.currentFrame();
    if (!fr) return;
    const refH = Math.max(
      1,
      this.atlas.commonSize?.h ||
        this.atlas.frames.reduce((m, f) => Math.max(m, f.sourceSize.h), 0) ||
        fr.sourceSize.h,
    );
    const scale = Math.min(1, MAX_DISPLAY_H / refH);
    this.sprite.scale.set(scale, scale);
    this.sprite.position.set(0, DISPLAY_Y);
  }
}
