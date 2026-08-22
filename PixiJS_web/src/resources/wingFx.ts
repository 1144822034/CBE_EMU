/**
 * Map-player wing overlay from JHOnlineData/chibang TexturePacker atlases.
 * Not part of CBE .actor pipeline — decorative layer behind the body sprite.
 */
import { Container, Sprite } from "pixi.js";
import {
  bindPlistAtlasIfCurrent,
  loadPlistAtlas,
  rebindPlistAtlas,
  type LoadedPlistAtlas,
  type PlistFrame,
} from "./plistAtlas";

/** Wing sheet under `JHOnlineData/chibang/sfx_<id>_0`. Edit `name` for list text. */
export type WingSpec = {
  id: number;
  /** List display name (翅膀面板). */
  name?: string;
};

export const WING_SPECS: readonly WingSpec[] = [
  { id: 88010, name: "恶魔之翼" },
  { id: 88011, name: "鎏金紫翼" },
  { id: 88012, name: "萤火紫翼" },
  { id: 88014, name: "黑白羽翼" },
  { id: 88015, name: "桃粉双翼" },
  { id: 88016, name: "暗黑紫翼" },
  { id: 88017, name: "火龙双翼" },
  { id: 88018, name: "白银流光" },
  { id: 88019, name: "血色双翼" },
  { id: 88020, name: "紫晶双翼" },
  { id: 88021, name: "烈焰双翼" },
  { id: 88022, name: "青色流光" },
  { id: 88023, name: "幽蓝羽翼" },
  { id: 88024, name: "红白双翼" },
  { id: 88025, name: "赤电双翼" },
  { id: 88026, name: "翠绿羽翼" },
  { id: 88027, name: "火黑双翼" },
  { id: 88028, name: "粉青蝶翼" },
  { id: 88029, name: "橙色羽翼" },
  { id: 88030, name: "暗紫双翼" },
  { id: 88031, name: "深黑羽翼" },
  { id: 88032, name: "暗金双翼" },
  { id: 88033, name: "白青双翼" },
  { id: 88034, name: "白金双翼" },
  { id: 88035, name: "三色羽翼" },
  { id: 88036, name: "暗青羽翼" },
  { id: 90002, name: "黑蓝羽翼" },
  { id: 90003, name: "紫金蝶翼" },
  { id: 90006, name: "双色波浪" },
  { id: 90007, name: "粉紫羽翼" },
  { id: 90010, name: "流光冰雪" },
];

export const WING_IDS: readonly number[] = WING_SPECS.map((s) => s.id);

const SPEC_BY_ID = new Map(WING_SPECS.map((s) => [s.id, s]));

const WING_FRAME_MS = 120;
/** Target on-screen height vs map body sprite height. */
const WING_HEIGHT_RATIO = 1.85;
/** Soft cap so huge 900xx sheets do not swallow the LCD. */
const WING_MAX_DISPLAY_H = 48;

export function isWingId(id: number): boolean {
  return SPEC_BY_ID.has(id | 0);
}

export function getWingSpec(wingId: number): WingSpec | null {
  return SPEC_BY_ID.get(wingId | 0) ?? null;
}

export function wingLabel(wingId: number): string {
  const spec = getWingSpec(wingId);
  if (!spec) return `wing_${wingId}`;
  return spec.name || `sfx_${wingId}`;
}

export function wingAtlasPath(wingId: number): string {
  return `chibang/sfx_${wingId | 0}_0`;
}

export class WingOverlay {
  readonly root = new Container();
  private sprite: Sprite | null = null;
  private atlas: LoadedPlistAtlas | null = null;
  private wingId = 0;
  private phase = 0;
  private animAcc = 0;
  private loadGen = 0;
  /** True while setWingId await is in flight for current wingId. */
  private loadInFlight = false;
  /** Body height in display px (after ACTOR_DISPLAY_SCALE). */
  private bodyDisplayH = 24;
  /** CBE dir 1=up 2=right 3=down 4=left. */
  private faceDir = 3;

  constructor() {
    this.root.sortableChildren = false;
    this.root.visible = false;
    this.root.eventMode = "none";
    // Behind body by default; SceneView raises to 3 when facing back.
    this.root.zIndex = 1;
  }

  get currentWingId(): number {
    return this.wingId;
  }

  clear(): void {
    this.loadGen++;
    this.loadInFlight = false;
    this.wingId = 0;
    this.atlas = rebindPlistAtlas(this.atlas, null);
    this.phase = 0;
    this.animAcc = 0;
    this.faceDir = 3;
    this.root.visible = false;
    this.root.zIndex = 1;
    if (this.sprite) {
      this.root.removeChild(this.sprite);
      this.sprite.destroy({ texture: false });
      this.sprite = null;
    }
  }

  async setWingId(wingId: number): Promise<void> {
    const id = wingId | 0;
    if (!id) {
      this.clear();
      return;
    }
    if (!isWingId(id)) {
      /* Still try JHOnlineData atlas — catalog may lag behind server unlocks. */
      console.warn("[wing] unlisted id, trying atlas", id);
    }
    // Same id + ready → show. Same id + in flight → wait.
    // Same id + failed/cancelled (no atlas, not in flight) → retry below.
    if (id === this.wingId) {
      if (this.atlas) {
        this.root.visible = true;
        this.layout();
        return;
      }
      if (this.loadInFlight) return;
    }
    const gen = ++this.loadGen;
    this.wingId = id;
    this.loadInFlight = true;
    console.info("[wing] load start", id);
    try {
      const atlas = await loadPlistAtlas(wingAtlasPath(id));
      if (gen !== this.loadGen) {
        console.info("[wing] load stale", id, "gen", gen, "now", this.loadGen);
        return;
      }
      this.atlas = bindPlistAtlasIfCurrent(
        this.atlas,
        atlas,
        () => gen === this.loadGen,
      );
      if (!this.atlas || gen !== this.loadGen) return;
      this.phase = 0;
      this.animAcc = 0;
      if (!this.sprite) {
        this.sprite = new Sprite(this.atlas.textures[0]!);
        this.sprite.anchor.set(0.5, 0.5);
        this.root.addChild(this.sprite);
      } else {
        this.sprite.texture = this.atlas.textures[0]!;
      }
      if (gen !== this.loadGen) return;
      this.root.visible = true;
      this.layout();
      console.info("[wing] ready", id, "frames", this.atlas.textures.length);
    } catch (e) {
      if (gen !== this.loadGen) return;
      console.warn("[wing] load failed", id, e);
      this.clear();
    } finally {
      if (gen === this.loadGen) this.loadInFlight = false;
    }
  }

  /** Update scale/offset when the body sprite size changes. */
  setBodyDisplayHeight(h: number): void {
    const next = Math.max(8, h || 24);
    if (Math.abs(next - this.bodyDisplayH) < 0.5) return;
    this.bodyDisplayH = next;
    this.layout();
  }

  /** Follow local player facing (CBE 1..4). */
  setFacing(cbeDir: number): void {
    const d = ((cbeDir - 1) % 4 + 4) % 4 + 1;
    if (d === this.faceDir) return;
    this.faceDir = d;
    this.layout();
  }

  tick(dtMs: number): void {
    if (!this.root.visible || !this.atlas || !this.sprite) return;
    const n = this.atlas.textures.length;
    if (n <= 1) return;
    this.animAcc += dtMs;
    let stepped = false;
    while (this.animAcc >= WING_FRAME_MS) {
      this.animAcc -= WING_FRAME_MS;
      this.phase = (this.phase + 1) % n;
      stepped = true;
    }
    if (stepped) {
      this.sprite.texture = this.atlas.textures[this.phase]!;
      this.layout();
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
    // Prefer shared canvas height from centroid-normalized atlas.
    const refH = Math.max(
      1,
      this.atlas.commonSize?.h ||
        this.atlas.frames.reduce((m, f) => Math.max(m, f.sourceSize.h), 0) ||
        fr.sourceSize.h,
    );
    const refW = Math.max(
      1,
      this.atlas.commonSize?.w ||
        this.atlas.frames.reduce((m, f) => Math.max(m, f.sourceSize.w), 0) ||
        fr.sourceSize.w,
    );
    let scale = (this.bodyDisplayH * WING_HEIGHT_RATIO) / refH;
    const displayH = refH * scale;
    if (displayH > WING_MAX_DISPLAY_H) {
      scale *= WING_MAX_DISPLAY_H / displayH;
    }
    const facingLeft = this.faceDir === 4;
    const facingRight = this.faceDir === 2;
    // Lock displayed size to the shared canvas so a texture swap cannot resize.
    // (width/height drive scale from texture.orig = commonSize.)
    this.sprite.width = (facingLeft ? -1 : 1) * refW * scale;
    this.sprite.height = refH * scale;
    this.sprite.rotation = facingLeft ? 0.12 : facingRight ? -0.12 : 0;
    const wingDisplayH = refH * scale;
    // Tuned: nudge further toward feet (was 0.68).
    this.sprite.position.set(0, -this.bodyDisplayH * 0.58 - wingDisplayH * 0.02);
  }
}

/** Resolve wing id from URL `?wing=` or localStorage `pixi.wingId`. */
export function readWingIdPreference(): number {
  try {
    if (typeof location !== "undefined") {
      const q = new URLSearchParams(location.search).get("wing");
      if (q != null && q !== "") {
        const n = Number(q);
        if (Number.isFinite(n) && (n === 0 || isWingId(n))) return n | 0;
      }
    }
  } catch {
    /* ignore */
  }
  try {
    if (typeof localStorage !== "undefined") {
      const raw = localStorage.getItem("pixi.wingId");
      if (raw != null && raw !== "") {
        const n = Number(raw);
        if (Number.isFinite(n) && (n === 0 || isWingId(n))) return n | 0;
      }
    }
  } catch {
    /* ignore */
  }
  return 0;
}

export function writeWingIdPreference(wingId: number): void {
  try {
    if (typeof localStorage === "undefined") return;
    const id = wingId | 0;
    if (!id) localStorage.removeItem("pixi.wingId");
    else localStorage.setItem("pixi.wingId", String(id));
  } catch {
    /* ignore */
  }
}
