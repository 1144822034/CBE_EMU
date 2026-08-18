/**
 * Map-player mount overlay from JHOnlineData/zuoqi TexturePacker atlases.
 *
 * Layouts:
 * - `sfx_<id>_0` — single side-view sheet (mirror for left).
 * - `<id>/(monster|player)_<id>_0_<act>_<part>` — multi-facing packs.
 *   Frame names carry facing: `…_<act>_<face>_####.png` (usually 0–7).
 *   Plist `part` is only an atlas split; we pick the part that contains the face.
 */
import { Container, Sprite } from "pixi.js";
import {
  loadPlistAtlas,
  rebindPlistAtlas,
  type LoadedPlistAtlas,
  type PlistFrame,
} from "./plistAtlas";

export type MountKind = "sfx" | "dir";

export type MountSpec = {
  id: number;
  kind: MountKind;
  prefix?: "monster" | "player";
  /** List display name (坐骑面板). Fall back to id-based label if omitted. */
  name?: string;
  /** Animation ids present on disk (0=stand, 1=walk, …). */
  acts: readonly number[];
  /** Max plist part index to probe (inclusive). */
  maxPart: number;
};

/** Side-view sfx + multi-dir packs (16128 removed). Edit `name` for list text. */
export const MOUNT_SPECS: readonly MountSpec[] = [
  { id: 11370, kind: "dir", prefix: "monster", name: "藏青龙", acts: [0, 1], maxPart: 3 },
  { id: 11372, kind: "dir", prefix: "monster", name: "黑魔龙", acts: [0, 1], maxPart: 7 },
  { id: 11802, kind: "dir", prefix: "monster", name: "金圣龙", acts: [0, 1], maxPart: 3 },
  { id: 16171, kind: "dir", prefix: "monster", name: "鳄青龙", acts: [0, 1], maxPart: 3 },
  { id: 20148, kind: "dir", prefix: "monster", name: "黄金龙", acts: [0], maxPart: 1 },
  { id: 40183, kind: "dir", prefix: "player", name: "蓝云龙", acts: [0, 1], maxPart: 1 },
  { id: 75535, kind: "sfx", name: "云青龙", acts: [], maxPart: 0 },
  { id: 75537, kind: "sfx", name: "汗血马", acts: [], maxPart: 0 },
  { id: 75549, kind: "sfx", name: "腾云龙", acts: [], maxPart: 0 },
];

export const MOUNT_IDS: readonly number[] = MOUNT_SPECS.map((s) => s.id);

const SPEC_BY_ID = new Map(MOUNT_SPECS.map((s) => [s.id, s]));

const MOUNT_FRAME_MS = 120;
const MOUNT_HEIGHT_RATIO = 2.35;
const MOUNT_MAX_DISPLAY_H = 56;
export const MOUNT_BODY_LIFT = 10;
/** Extra nameplate/title lift while mounted (Pixi Y up = more negative). */
export const MOUNT_NAMEPLATE_EXTRA = 4;

/** Cache: `${id}:${act}:${face}` → sheet path (no prefix filter). */
const facePartCache = new Map<string, string>();

export function isMountId(id: number): boolean {
  return SPEC_BY_ID.has(id | 0);
}

export function getMountSpec(mountId: number): MountSpec | null {
  return SPEC_BY_ID.get(mountId | 0) ?? null;
}

/**
 * CBE dir 1=up 2=right 3=down 4=left → pack face 0–7.
 * Pack layout (verified in-client): 0=up, 2=right, 4=down, 6=left.
 */
export function cbeDirToMountFace(cbeDir: number): number {
  const d = ((cbeDir - 1) % 4 + 4) % 4 + 1;
  const map: Record<number, number> = { 1: 0, 2: 2, 3: 4, 4: 6 };
  return map[d] ?? 0;
}

export function mountLabel(mountId: number): string {
  const spec = getMountSpec(mountId);
  if (!spec) return `mount_${mountId}`;
  if (spec.name) return spec.name;
  if (spec.kind === "sfx") return `sfx_${mountId}`;
  return `${spec.prefix}_${mountId}`;
}

export function mountAtlasPath(mountId: number): string {
  return `zuoqi/sfx_${mountId | 0}_0`;
}

function dirFramePrefix(
  spec: MountSpec,
  act: number,
  face: number,
): string {
  const prefix = spec.prefix || "monster";
  return `${prefix}_${spec.id}_0_${act}_${face}_`;
}

function dirPartPath(spec: MountSpec, act: number, part: number): string {
  const prefix = spec.prefix || "monster";
  return `zuoqi/${spec.id}/${prefix}_${spec.id}_0_${act}_${part}`;
}

async function loadDirFacingAtlas(
  spec: MountSpec,
  act: number,
  face: number,
): Promise<LoadedPlistAtlas> {
  const cacheKey = `${spec.id}:${act}:${face}`;
  const framePrefix = dirFramePrefix(spec, act, face);
  const cachedPath = facePartCache.get(cacheKey);
  if (cachedPath) {
    return loadPlistAtlas(cachedPath, { frameNamePrefix: framePrefix });
  }
  let lastErr: unknown;
  for (let part = 0; part <= spec.maxPart; part++) {
    const path = dirPartPath(spec, act, part);
    try {
      const atlas = await loadPlistAtlas(path, { frameNamePrefix: framePrefix });
      facePartCache.set(cacheKey, path);
      return atlas;
    } catch (e) {
      lastErr = e;
    }
  }
  throw lastErr || new Error(`no sheet for ${cacheKey}`);
}

export class MountOverlay {
  readonly root = new Container();
  private sprite: Sprite | null = null;
  private atlas: LoadedPlistAtlas | null = null;
  private mountId = 0;
  private phase = 0;
  private animAcc = 0;
  private loadGen = 0;
  private bodyDisplayH = 24;
  private faceDir = 3;
  private sideDir = 2;
  private moving = false;
  private sheetKey = "";

  constructor() {
    this.root.sortableChildren = false;
    this.root.visible = false;
    this.root.eventMode = "none";
    this.root.zIndex = 0.5;
  }

  get currentMountId(): number {
    return this.mountId;
  }

  clear(): void {
    this.loadGen++;
    this.mountId = 0;
    this.atlas = rebindPlistAtlas(this.atlas, null);
    this.phase = 0;
    this.animAcc = 0;
    this.faceDir = 3;
    this.sideDir = 2;
    this.moving = false;
    this.sheetKey = "";
    this.root.visible = false;
    this.root.zIndex = 0.5;
    if (this.sprite) {
      this.root.removeChild(this.sprite);
      this.sprite.destroy({ texture: false });
      this.sprite = null;
    }
  }

  async setMountId(mountId: number): Promise<void> {
    const id = mountId | 0;
    if (!id) {
      this.clear();
      return;
    }
    if (!isMountId(id)) {
      console.warn("[mount] unlisted id, trying atlas", id);
    }
    if (id === this.mountId && this.atlas) {
      this.root.visible = true;
      this.layout();
      return;
    }
    this.mountId = id;
    this.phase = 0;
    this.animAcc = 0;
    this.sheetKey = "";
    await this.reloadSheet();
  }

  setBodyDisplayHeight(h: number): void {
    const next = Math.max(8, h || 24);
    if (Math.abs(next - this.bodyDisplayH) < 0.5) return;
    this.bodyDisplayH = next;
    this.layout();
  }

  setMoving(moving: boolean): void {
    if (this.moving === !!moving) return;
    this.moving = !!moving;
    const spec = getMountSpec(this.mountId);
    if (spec?.kind === "dir" && spec.acts.includes(1)) {
      void this.reloadSheet();
    }
  }

  setFacing(cbeDir: number, commitSide = false): void {
    const d = ((cbeDir - 1) % 4 + 4) % 4 + 1;
    const spec = getMountSpec(this.mountId);
    let changed = false;
    if (this.faceDir !== d) {
      this.faceDir = d;
      changed = true;
    }
    if (commitSide && (d === 2 || d === 4) && this.sideDir !== d) {
      this.sideDir = d;
      changed = true;
    }
    if (!changed && this.atlas) return;

    if (spec?.kind === "dir") {
      void this.reloadSheet();
      return;
    }
    this.layout();
  }

  setMoveVector(dx: number, dy: number): void {
    const ax = Math.abs(dx);
    const ay = Math.abs(dy);
    if (ax < 1e-8 && ay < 1e-8) return;
    const spec = getMountSpec(this.mountId);
    const multi = spec?.kind === "dir";
    if (ax > ay * 1.2) {
      this.setFacing(dx > 0 ? 2 : 4, true);
      return;
    }
    if (ay > ax * 1.2) {
      this.setFacing(dy > 0 ? 3 : 1, !!multi);
      return;
    }
  }

  tick(dtMs: number): void {
    if (!this.root.visible || !this.atlas || !this.sprite) return;
    const n = this.atlas.textures.length;
    if (n <= 1) return;
    this.animAcc += dtMs;
    let stepped = false;
    while (this.animAcc >= MOUNT_FRAME_MS) {
      this.animAcc -= MOUNT_FRAME_MS;
      this.phase = (this.phase + 1) % n;
      stepped = true;
    }
    if (stepped) {
      this.sprite.texture = this.atlas.textures[this.phase]!;
    }
  }

  private pickAct(spec: MountSpec): number {
    if (this.moving && spec.acts.includes(1)) return 1;
    if (spec.acts.includes(0)) return 0;
    return spec.acts[0] ?? 0;
  }

  private async reloadSheet(): Promise<void> {
    const id = this.mountId;
    if (!id) return;
    const spec = getMountSpec(id);
    if (!spec) return;

    const gen = ++this.loadGen;
    try {
      let atlas: LoadedPlistAtlas;
      let key: string;
      if (spec.kind === "sfx") {
        key = mountAtlasPath(id);
        if (key === this.sheetKey && this.atlas) {
          this.root.visible = true;
          this.layout();
          return;
        }
        atlas = await loadPlistAtlas(key);
      } else {
        const act = this.pickAct(spec);
        const face = cbeDirToMountFace(this.faceDir);
        key = `${spec.id}:${act}:${face}`;
        if (key === this.sheetKey && this.atlas) {
          this.root.visible = true;
          this.layout();
          return;
        }
        atlas = await loadDirFacingAtlas(spec, act, face);
      }
      if (gen !== this.loadGen) return;
      this.atlas = rebindPlistAtlas(this.atlas, atlas);
      this.sheetKey = key;
      this.phase = 0;
      this.animAcc = 0;
      if (!this.sprite) {
        this.sprite = new Sprite(atlas.textures[0]!);
        this.sprite.anchor.set(0.5, 0.5);
        this.root.addChild(this.sprite);
      } else {
        this.sprite.texture = atlas.textures[0]!;
      }
      this.root.visible = true;
      this.layout();
    } catch (e) {
      if (gen !== this.loadGen) return;
      console.warn("[mount] load failed", id, e);
      if (!this.atlas) this.clear();
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
    let scale = (this.bodyDisplayH * MOUNT_HEIGHT_RATIO) / refH;
    if (refH * scale > MOUNT_MAX_DISPLAY_H) {
      scale *= MOUNT_MAX_DISPLAY_H / (refH * scale);
    }
    const spec = getMountSpec(this.mountId);
    this.sprite.anchor.set(0.5, 0.5);
    const flip = spec?.kind === "sfx" && this.sideDir === 4;
    this.sprite.scale.set((flip ? -1 : 1) * scale, scale);
    this.sprite.rotation = 0;
    this.sprite.position.set(0, -this.bodyDisplayH * 0.22);
  }
}

export function readMountIdPreference(): number {
  try {
    if (typeof location !== "undefined") {
      const q = new URLSearchParams(location.search).get("mount");
      if (q != null && q !== "") {
        const n = Number(q);
        if (Number.isFinite(n) && (n === 0 || isMountId(n))) return n | 0;
      }
    }
  } catch {
    /* ignore */
  }
  try {
    if (typeof localStorage !== "undefined") {
      const raw = localStorage.getItem("pixi.mountId");
      if (raw != null && raw !== "") {
        const n = Number(raw);
        if (Number.isFinite(n) && (n === 0 || isMountId(n))) return n | 0;
      }
    }
  } catch {
    /* ignore */
  }
  return 0;
}

export function writeMountIdPreference(mountId: number): void {
  try {
    if (typeof localStorage === "undefined") return;
    const id = mountId | 0;
    if (!id) localStorage.removeItem("pixi.mountId");
    else localStorage.setItem("pixi.mountId", String(id));
  } catch {
    /* ignore */
  }
}
