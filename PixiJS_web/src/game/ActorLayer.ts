import { Container, Graphics, Sprite, type Texture } from "pixi.js";
import type { NpcActor, PeerActor } from "../net/protocol";
import { BitmapLabel } from "../ui/BitmapLabel";
import { ACTOR_DISPLAY_SCALE, NAMEPLATE_SCALE } from "../ui/constants";
import { resolveTitleGif } from "../ui/designation";
import { tryLoadUiTexture } from "../ui/UiAssets";
import type { NpcTaskPromptType } from "../net/protocolExtras";
import {
  actorResourceForJobSex,
  composeAnimationFrame,
  composeMapActor,
  loadActor,
  type LoadedActor,
} from "../resources/actor";
import {
  findNearestWalkable,
  mapBlocksAt,
  mapMoveBlocked,
  type MapPreview,
} from "../resources/map";
import { formatMonsterMapName } from "../resources/monsterLevels";
import { Plus16Sparkles } from "../render/Plus16Sparkles";
import { rgbaToTexture } from "../render/rgbaTexture";

const NPC_CYAN = 0x80e8ff;
const PEER_GREEN = 0xa8e6a1;
/** Map monster nameplate — green (distinct from peer mint). */
const MONSTER_GREEN = 0x50e070;
/** Teleport stone nameplate. */
const STONE_GOLD = 0xffd070;

/**
 * CBE draws NPC quest marks via `fuhao.actor`, not equal 8×16 GIF tiles.
 * Evidence (fuhao.actor rects): 0..10 grey?, 10..20 yellow?, 20..26 yellow!,
 * 26..32 grey!. Anim0..2 map to prompt types 1/2/3.
 */
const PROMPT_ANIM: Record<NpcTaskPromptType, number> = {
  1: 0, // yellow ?
  2: 1, // yellow !
  3: 2, // grey ?
};

let fuhaoActorPromise: Promise<LoadedActor> | null = null;
const fuhaoTexCache = new Map<NpcTaskPromptType, Texture>();

async function loadFuhaoPromptTexture(
  prompt: NpcTaskPromptType,
): Promise<Texture | null> {
  const hit = fuhaoTexCache.get(prompt);
  if (hit) return hit;
  if (!fuhaoActorPromise) {
    fuhaoActorPromise = loadActor("fuhao.actor").catch((e) => {
      fuhaoActorPromise = null;
      throw e;
    });
  }
  try {
    const actor = await fuhaoActorPromise;
    const anim = PROMPT_ANIM[prompt];
    const frame = composeAnimationFrame(actor, anim, 0);
    if (!frame) return null;
    const tex = rgbaToTexture(frame.width, frame.height, frame.rgba);
    fuhaoTexCache.set(prompt, tex);
    return tex;
  } catch (e) {
    console.warn("[actor-layer] fuhao.actor", e);
    return null;
  }
}

/** Patrol radius around SCE spawn (pixels). */
const PATROL_RADIUS = 48;
/** Monster walk speed px/ms. */
const PATROL_SPEED = 0.028;
/** Min/max walk burst and idle pause (ms). */
const PATROL_WALK_MS = [900, 2200] as const;
const PATROL_IDLE_MS = [600, 1800] as const;
/** Map walk strip step — keep snappier than idle breath. */
const MAP_WALK_FRAME_MS = 140;
/** Map idle flap — match battle 鼠尾/呼吸节奏. */
const MAP_IDLE_FRAME_MS = 360;

type Marker = {
  id: number;
  root: Container;
  sprite: Sprite | null;
  label: BitmapLabel;
  /** Peer designation GIF badge above name (riches_name* / level_name*). */
  titleBadge: Container;
  titleSpr: Sprite | null;
  titleGifKey: string;
  /** NPC quest mark from fuhao.actor (submit/accept/incomplete). */
  questMark: Sprite | null;
  questPrompt: NpcTaskPromptType | 0;
  /** Bumped so overlapping async refreshQuestMark cannot leave a second sprite. */
  questMarkGen: number;
  kind: "npc" | "peer" | "monster";
  actorName: string;
  nameLabel: string;
  loaded: LoadedActor | null;
  /** CBE dir 1..4 */
  dir: number;
  phase: number;
  animate: boolean;
  source: "catalog" | "sce" | "peer";
  /** True while walking (map walk pose); flap-only idle uses false. */
  moving: boolean;
  homeX: number;
  homeY: number;
  x: number;
  y: number;
  /** Remaining ms in current walk or idle segment. */
  patrolLeft: number;
  /** Walking when true; paused when false. */
  patrolWalking: boolean;
  /** Peer 1/2/2 timeline: CBE dirs 1..4 (0=idle), one byte ≈ 100ms / 4px. */
  peerMoveQueue: number[];
  peerMoveAcc: number;
  /** Peer +16 gem sparkles (one color per piece). */
  plus16Sparkles: Plus16Sparkles | null;
};

/**
 * Aggressive wild catalog rows. Challenge dialogue NPCs (e.g. 铸剑谷小猴子
 * `e_monkey.actor`) must stay talkable — do NOT treat every `e_*` as a monster.
 * SCE combat spawns use setSceneMonsters / liveSceneMonsters separately.
 */
function isMonsterLike(scriptName: string, actorResource: string): boolean {
  const script = scriptName || "";
  const actor = actorResource || "";
  if (/monster|野怪|combat/i.test(script)) return true;
  if (/ghostfire|boss/i.test(actor)) return true;
  // Script literally names a 怪, but not NPC titles like 小猴子 / 欧冶子.
  if (/怪/.test(script) && !/猴子|使者|师傅|掌门/.test(script)) return true;
  return false;
}

function randRange(lo: number, hi: number): number {
  return lo + Math.random() * (hi - lo);
}

function dirDelta(dir: number): { dx: number; dy: number } {
  switch (dir) {
    case 1:
      return { dx: 0, dy: -1 };
    case 2:
      return { dx: 1, dy: 0 };
    case 3:
      return { dx: 0, dy: 1 };
    default:
      return { dx: -1, dy: 0 };
  }
}

/**
 * Map actors rendered from decoded .actor + LKV2 GIF sheets.
 * Catalog NPCs from 27/11; wild monsters from SCE kind=3 with local patrol.
 */
export class ActorLayer {
  /** Shared with SceneView.world so markers Y-sort with player / decor. */
  readonly root: Container;
  onNpcTap: ((actorId: number) => void) | null = null;
  private markers = new Map<number, Marker>();
  private animAcc = 0;
  private idleAnimAcc = 0;
  private catalogNpcs: NpcActor[] = [];
  private sceMonsters: NpcActor[] = [];
  private teleportStones: NpcActor[] = [];
  /** NPC display name → prompt type (from 6/1 + 6/14). */
  private taskPrompts = new Map<string, NpcTaskPromptType>();
  /** Current scene MAP cells — SCE patrol uses same collision as the player. */
  private mapPreview: MapPreview | null = null;
  /**
   * SCE combat markers hidden while in / after battle.
   * `respawnAt` null = waiting for battle end; number = performance.now() deadline.
   */
  private suppressedScene = new Map<
    number,
    { seed: NpcActor; respawnAt: number | null }
  >();

  constructor(parent: Container) {
    this.root = parent;
  }

  /** Wire MAP collision for wild-monster patrol (clear with null on scene drop). */
  setMapCollision(map: MapPreview | null): void {
    this.mapPreview = map;
  }

  clear(): void {
    for (const m of this.markers.values()) {
      this.root.removeChild(m.root);
      m.root.destroy({ children: true });
    }
    this.markers.clear();
    this.catalogNpcs = [];
    this.sceMonsters = [];
    this.teleportStones = [];
    this.taskPrompts = new Map();
    this.mapPreview = null;
    this.suppressedScene.clear();
  }

  /**
   * Remove a wild SCE monster from the map (enter battle). Returns false if
   * the id is not a live SCE combat marker.
   */
  suppressSceneMonster(actorId: number): boolean {
    if (actorId < 0x70000000) return false;
    const seed = this.sceMonsters.find((s) => s.actorId === actorId);
    const m = this.markers.get(actorId);
    if (!seed && !m) return false;
    const snap: NpcActor = seed
      ? { ...seed }
      : {
          actorId,
          x: m!.x | 0,
          y: m!.y | 0,
          name: m!.nameLabel,
          actorResource: m!.actorName,
          scriptName: "sce_combat",
        };
    // Keep spawn home (SCE seed), not patrol feet — 刷新回出生点.
    if (seed) {
      snap.x = seed.x | 0;
      snap.y = seed.y | 0;
    }
    this.suppressedScene.set(actorId, { seed: snap, respawnAt: null });
    if (m) {
      m.root.destroy({ children: true });
      this.markers.delete(actorId);
    }
    return true;
  }

  /** Battle start failed — put the marker back immediately. */
  restoreSceneMonster(actorId: number): void {
    const held = this.suppressedScene.get(actorId);
    if (!held) return;
    this.suppressedScene.delete(actorId);
    this.respawnSceneMonster(held.seed);
  }

  /**
   * After battle ends: arm 10s (default) refresh for every engaged SCE spawn.
   */
  armSuppressedRespawns(delayMs = 20_000): void {
    const at = performance.now() + Math.max(0, delayMs);
    for (const info of this.suppressedScene.values()) {
      if (info.respawnAt == null) info.respawnAt = at;
    }
  }

  /** Drop nearby-player markers only (role switch / same-map re-enter). */
  clearPeers(): void {
    for (const [id, m] of this.markers) {
      if (m.kind !== "peer") continue;
      m.root.destroy({ children: true });
      this.markers.delete(id);
    }
  }

  setNpcs(npcs: NpcActor[]): void {
    this.catalogNpcs = npcs;
    this.syncMarkers();
  }

  /** Apply quest overhead marks after 6/1 / 6/14 (CBE node+326). */
  setTaskPrompts(prompts: Map<string, NpcTaskPromptType>): void {
    this.taskPrompts = prompts;
    for (const m of this.markers.values()) {
      if (m.kind !== "npc" || m.source !== "catalog") continue;
      void this.refreshQuestMark(m);
    }
  }

  setSceneMonsters(monsters: NpcActor[]): void {
    this.sceMonsters = monsters;
    // Drop engage/respawn only for spawns gone from the new list (map hop).
    for (const id of [...this.suppressedScene.keys()]) {
      if (!monsters.some((m) => m.actorId === id)) {
        this.suppressedScene.delete(id);
      }
    }
    this.syncMarkers();
  }

  setTeleportStones(stones: NpcActor[]): void {
    this.teleportStones = stones;
    this.syncMarkers();
  }

  /** Live SCE monster coords after patrol (for F-key nearest). */
  liveSceneMonsters(): NpcActor[] {
    return this.sceMonsters
      .filter((seed) => !this.suppressedScene.has(seed.actorId))
      .map((seed) => {
        const m = this.markers.get(seed.actorId);
        if (!m) return seed;
        return { ...seed, x: m.x | 0, y: m.y | 0 };
      });
  }

  /**
   * Catalog NPCs that accept talk (26/1) — excludes teleport-stone id range
   * 0x60000000+. SCE combat spawns are NOT in catalogNpcs (collide path).
   * Challenge NPCs such as 小猴子 (`e_monkey`) must remain here.
   */
  liveInteractNpcs(): { actorId: number; x: number; y: number; name: string }[] {
    const out: { actorId: number; x: number; y: number; name: string }[] = [];
    for (const npc of this.catalogNpcs) {
      if (npc.actorId >= 0x60000000) continue;
      if (isMonsterLike(npc.scriptName || "", npc.actorResource || "")) continue;
      const m = this.markers.get(npc.actorId);
      const rawName = (npc.name || "NPC").replace(/\0+$/g, "").trim();
      out.push({
        actorId: npc.actorId,
        x: (m?.x ?? npc.x) | 0,
        y: (m?.y ?? npc.y) | 0,
        name: (/\.actor$/i.test(rawName) ? "NPC" : rawName).slice(0, 12),
      });
    }
    return out;
  }

  /** Live peer under map cursor (face-to-face tap → 周围玩家 actions). */
  peerAt(
    actorId: number,
  ): { actorId: number; name: string; x: number; y: number; title: string } | null {
    const m = this.markers.get(actorId);
    if (!m || m.kind !== "peer") return null;
    /* nameLabel = 玩家名；actorName = h_*.actor 资源，切勿当提示名. */
    const raw = (m.nameLabel || "").replace(/\0+$/g, "").trim();
    const name =
      raw && !/\.actor$/i.test(raw) ? raw.slice(0, 12) : "玩家";
    return {
      actorId,
      name,
      x: m.x | 0,
      y: m.y | 0,
      title: "",
    };
  }

  /** Dots for minimap (npc / peer / monster / teleport stone). */
  liveMinimapMarkers(): {
    x: number;
    y: number;
    kind: "npc" | "peer" | "monster" | "teleport";
  }[] {
    const out: {
      x: number;
      y: number;
      kind: "npc" | "peer" | "monster" | "teleport";
    }[] = [];
    for (const m of this.markers.values()) {
      if (m.kind === "npc" || m.kind === "peer" || m.kind === "monster") {
        out.push({ x: m.x, y: m.y, kind: m.kind });
      }
    }
    for (const stone of this.teleportStones) {
      const m = this.markers.get(stone.actorId);
      out.push({
        x: (m?.x ?? stone.x) | 0,
        y: (m?.y ?? stone.y) | 0,
        kind: "teleport",
      });
    }
    return out;
  }

  applyPeers(peers: PeerActor[], removed: number[]): void {
    for (const id of removed) {
      const m = this.markers.get(id);
      if (m?.kind === "peer") {
        m.plus16Sparkles = null;
        m.root.destroy({ children: true });
        this.markers.delete(id);
      }
    }
    for (const peer of peers) {
      // Wire (ParseSceneOtherNodeData / mock): visualVariant=job-1, visualGroup=sex+1.
      // Portrait index = visualGroup + 2*visualVariant - 1 ≡ job*2 + sex.
      const job = Math.max(0, Math.min(2, peer.visualVariant | 0));
      const sex = peer.visualGroup >= 1 ? ((peer.visualGroup - 1) & 1) : 0;
      const res = actorResourceForJobSex(job, sex);
      void this.upsert(
        peer.actorId,
        peer.x,
        peer.y,
        peer.name || "玩家",
        "peer",
        res,
        PEER_GREEN,
        true,
        "peer",
        peer.title || "",
        peer.titleGif || "",
        true,
        peer.plus16Count ?? 0,
      );
    }
  }

  /** Live 23/2 designation update for a visible peer. */
  applyPeerTitle(actorId: number, title: string, titleGif: string): void {
    const m = this.markers.get(actorId);
    if (!m || m.kind !== "peer") return;
    void this.refreshTitleBadge(m, title, titleGif);
  }

  /**
   * Apply 1/2/2 peer moveinfo: outer x/y are timeline start; dirs advance 4px/frame.
   * Empty dirs = snap to authoritative end position (baseline / idle publish).
   */
  applyPeerMoves(
    moves: { actorId: number; x: number; y: number; dirs: number[] }[],
  ): void {
    for (const mv of moves) {
      const m = this.markers.get(mv.actorId);
      if (!m || m.kind !== "peer") continue;
      m.peerMoveQueue = [];
      m.peerMoveAcc = 0;
      m.x = mv.x;
      m.y = mv.y;
      m.homeX = mv.x;
      m.homeY = mv.y;
      m.root.position.set(m.x, m.y);
      m.root.zIndex = m.y | 0;
      if (mv.dirs.length) {
        m.peerMoveQueue = mv.dirs.slice();
        m.moving = true;
        m.animate = true;
        const first = mv.dirs.find((d) => d >= 1 && d <= 4);
        if (first) m.dir = first;
      } else {
        m.moving = false;
      }
      if (m.loaded) this.applyFrame(m);
    }
  }

  tick(dtMs: number): void {
    const dt = Math.min(dtMs, 50);
    this.tickSuppressedRespawns();
    for (const m of this.markers.values()) {
      if (m.kind === "monster" && m.source === "sce") {
        this.tickPatrol(m, dt);
      } else if (m.kind === "peer" && m.peerMoveQueue.length) {
        this.tickPeerMove(m, dt);
      }
      m.plus16Sparkles?.tick(dt);
    }

    this.animAcc += dt;
    this.idleAnimAcc += dt;
    const stepWalk = this.animAcc >= MAP_WALK_FRAME_MS;
    const stepIdle = this.idleAnimAcc >= MAP_IDLE_FRAME_MS;
    if (!stepWalk && !stepIdle) return;
    if (stepWalk) this.animAcc = 0;
    if (stepIdle) this.idleAnimAcc = 0;
    for (const m of this.markers.values()) {
      if (!m.loaded || !m.animate) continue;
      if (m.kind === "peer" && !m.moving && !m.peerMoveQueue.length) continue;
      const moving = m.moving || m.peerMoveQueue.length > 0;
      if (moving ? !stepWalk : !stepIdle) continue;
      m.phase = (m.phase + 1) % 256;
      this.applyFrame(m);
    }
  }

  private tickPeerMove(m: Marker, dt: number): void {
    // CBE ProcessSceneAutoAction: one queue byte per ~100ms frame, 4px/step.
    m.peerMoveAcc += dt;
    let stepped = false;
    while (m.peerMoveAcc >= 100 && m.peerMoveQueue.length) {
      m.peerMoveAcc -= 100;
      const d = m.peerMoveQueue.shift()!;
      if (d >= 1 && d <= 4) {
        m.dir = d;
        const { dx, dy } = dirDelta(d);
        m.x += dx * 4;
        m.y += dy * 4;
        m.moving = true;
        stepped = true;
      }
    }
    if (stepped) {
      m.root.position.set(m.x, m.y);
      m.root.zIndex = m.y | 0;
    }
    if (!m.peerMoveQueue.length) {
      m.moving = false;
      m.peerMoveAcc = 0;
      if (m.loaded) this.applyFrame(m);
    }
  }

  private tickPatrol(m: Marker, dt: number): void {
    m.patrolLeft -= dt;
    if (m.patrolLeft <= 0) {
      if (m.patrolWalking) {
        // Finish walk → idle.
        m.patrolWalking = false;
        m.moving = false;
        m.patrolLeft = randRange(PATROL_IDLE_MS[0], PATROL_IDLE_MS[1]);
      } else {
        // Start walk in a random (or home-seeking) direction.
        m.patrolWalking = true;
        m.moving = true;
        m.dir = this.pickPatrolDir(m);
        m.patrolLeft = randRange(PATROL_WALK_MS[0], PATROL_WALK_MS[1]);
      }
    }

    if (!m.patrolWalking) return;

    const { dx, dy } = dirDelta(m.dir);
    let nx = m.x + dx * PATROL_SPEED * dt;
    let ny = m.y + dy * PATROL_SPEED * dt;
    const awayX = nx - m.homeX;
    const awayY = ny - m.homeY;
    if (awayX * awayX + awayY * awayY > PATROL_RADIUS * PATROL_RADIUS) {
      // Turn back toward home.
      if (Math.abs(awayX) >= Math.abs(awayY)) m.dir = awayX > 0 ? 4 : 2;
      else m.dir = awayY > 0 ? 1 : 3;
      const d2 = dirDelta(m.dir);
      nx = m.x + d2.dx * PATROL_SPEED * dt;
      ny = m.y + d2.dy * PATROL_SPEED * dt;
    }

    const fromX = m.x;
    const fromY = m.y;
    let tx = nx;
    let ty = ny;
    const map = this.mapPreview;
    if (map) {
      // Same axis-separated slide as SceneView player walk (MAP 0xF + edge bits).
      const stuck = mapBlocksAt(map, fromX, fromY, 0);
      if (!stuck) {
        if (mapMoveBlocked(map, fromX, fromY, tx, fromY, 1)) tx = fromX;
        if (mapMoveBlocked(map, tx, fromY, tx, ty, 1)) ty = fromY;
        if (mapMoveBlocked(map, fromX, fromY, tx, ty, 1)) {
          tx = fromX;
          ty = fromY;
        }
      }
      tx = Math.max(0, Math.min(map.width - 1, tx));
      ty = Math.max(0, Math.min(map.height - 1, ty));
    }

    if (tx === fromX && ty === fromY && (nx !== fromX || ny !== fromY)) {
      // Hit wall / edge — stop this burst and face another open dir next walk.
      m.patrolWalking = false;
      m.moving = false;
      m.dir = this.pickPatrolDir(m, m.dir);
      m.patrolLeft = randRange(PATROL_IDLE_MS[0], PATROL_IDLE_MS[1]);
      if (m.loaded) this.applyFrame(m);
      return;
    }

    m.x = tx;
    m.y = ty;
    m.root.position.set(m.x, m.y);
    m.root.zIndex = m.y | 0;
  }

  /** Prefer a dir that is not immediately wall-blocked from current feet. */
  private pickPatrolDir(m: Marker, avoidDir = 0): number {
    const map = this.mapPreview;
    const dirs = [1, 2, 3, 4].filter((d) => d !== avoidDir);
    for (let i = dirs.length - 1; i > 0; i--) {
      const j = (Math.random() * (i + 1)) | 0;
      const t = dirs[i]!;
      dirs[i] = dirs[j]!;
      dirs[j] = t;
    }
    if (!map) return dirs[0] ?? 1 + ((Math.random() * 4) | 0);
    const probe = 6;
    for (const d of dirs) {
      const { dx, dy } = dirDelta(d);
      const tx = m.x + dx * probe;
      const ty = m.y + dy * probe;
      if (!mapMoveBlocked(map, m.x, m.y, tx, ty, 1)) return d;
    }
    return dirs[0] ?? 1;
  }

  /** Snap SCE spawn out of solid so patrol does not start inside a wall. */
  private snapMonsterToWalkable(m: Marker): void {
    const map = this.mapPreview;
    if (!map || !mapBlocksAt(map, m.x, m.y, 0)) return;
    const safe = findNearestWalkable(map, m.x, m.y);
    if (!safe) return;
    m.x = safe.x;
    m.y = safe.y;
    m.homeX = safe.x;
    m.homeY = safe.y;
    m.root.position.set(m.x, m.y);
    m.root.zIndex = m.y | 0;
  }

  private tickSuppressedRespawns(): void {
    if (!this.suppressedScene.size) return;
    const now = performance.now();
    for (const [id, info] of [...this.suppressedScene]) {
      if (info.respawnAt == null || now < info.respawnAt) continue;
      this.suppressedScene.delete(id);
      this.respawnSceneMonster(info.seed);
    }
  }

  private respawnSceneMonster(seed: NpcActor): void {
    if (!this.sceMonsters.some((s) => s.actorId === seed.actorId)) {
      // Scene list dropped this spawn (map hop) — do not recreate.
      return;
    }
    void this.upsert(
      seed.actorId,
      seed.x,
      seed.y,
      seed.name || "怪",
      "monster",
      seed.actorResource || "",
      MONSTER_GREEN,
      true,
      "sce",
    );
  }

  private syncMarkers(): void {
    const keep = new Set<number>();
    for (const npc of this.catalogNpcs) {
      const monster = isMonsterLike(npc.scriptName || "", npc.actorResource || "");
      keep.add(npc.actorId);
      const rawName = npc.name || (monster ? "怪" : "NPC");
      void this.upsert(
        npc.actorId,
        npc.x,
        npc.y,
        monster
          ? formatMonsterMapName(rawName, npc.battleActorId ?? npc.actorId)
          : rawName,
        monster ? "monster" : "npc",
        npc.actorResource || "",
        monster ? MONSTER_GREEN : NPC_CYAN,
        monster,
        "catalog",
      );
    }
    for (const mon of this.sceMonsters) {
      if (this.suppressedScene.has(mon.actorId)) continue;
      keep.add(mon.actorId);
      void this.upsert(
        mon.actorId,
        mon.x,
        mon.y,
        mon.name || "怪",
        "monster",
        mon.actorResource || "",
        MONSTER_GREEN,
        true,
        "sce",
      );
    }
    for (const stone of this.teleportStones) {
      keep.add(stone.actorId);
      void this.upsert(
        stone.actorId,
        stone.x,
        stone.y,
        stone.name || "传送石",
        "npc",
        stone.actorResource || "n_telestone.actor",
        STONE_GOLD,
        true,
        "catalog",
        "",
        "",
        false, // 传送石名：无描边
      );
    }
    for (const [id, m] of this.markers) {
      if (m.kind === "peer") continue;
      if (!keep.has(id)) {
        m.root.destroy({ children: true });
        this.markers.delete(id);
      }
    }
  }

  private async upsert(
    id: number,
    x: number,
    y: number,
    label: string,
    kind: "npc" | "peer" | "monster",
    actorName: string,
    color: number,
    animate: boolean,
    source: "catalog" | "sce" | "peer",
    title = "",
    titleGif = "",
    /** Map player / monster / NPC nameplates only. */
    nameOutline = true,
    plus16Count = 0,
  ): Promise<void> {
    let m = this.markers.get(id);
    if (!m) {
      const root = new Container();
      root.sortableChildren = true;
      const body = new Graphics()
        .roundRect(-7, -14, 14, 14, 2)
        .fill({ color })
        .stroke({ width: 1, color: 0x1a1a1a });
      body.zIndex = 0;
      const text = new BitmapLabel();
      text.zIndex = 2;
      const titleBadge = new Container();
      titleBadge.zIndex = 3;
      titleBadge.visible = false;
      const plus16Sparkles = kind === "peer" ? new Plus16Sparkles() : null;
      if (plus16Sparkles) root.addChild(plus16Sparkles.root);
      root.addChild(body, titleBadge, text);
      root.zIndex = y | 0;
      if (kind === "npc" || kind === "monster" || kind === "peer") {
        root.eventMode = "static";
        root.cursor = "pointer";
        root.on("pointertap", () => this.onNpcTap?.(id));
      }
      this.root.addChild(root);
      m = {
        id,
        root,
        sprite: null,
        label: text,
        titleBadge,
        titleSpr: null,
        titleGifKey: "",
        questMark: null,
        questPrompt: 0,
        questMarkGen: 0,
        kind,
        actorName: "",
        nameLabel: label,
        loaded: null,
        dir: 1 + ((Math.random() * 4) | 0),
        phase: 0,
        animate,
        source,
        moving: false,
        homeX: x,
        homeY: y,
        x,
        y,
        patrolLeft: randRange(200, 1200),
        patrolWalking: false,
        peerMoveQueue: [],
        peerMoveAcc: 0,
        plus16Sparkles,
      };
      this.markers.set(id, m);
      root.position.set(x, y);
    }

    // Keep marker depth in sync with feet Y (same sort space as decor/player).
    m.root.zIndex = m.y | 0;
    m.kind = kind;
    m.animate = animate;
    m.source = source;
    m.nameLabel = label;
    // Catalog / peer: snap to server coords. SCE patrol keeps live x/y unless home moved.
    if (source !== "sce" || !m.loaded) {
      m.homeX = x;
      m.homeY = y;
      m.x = x;
      m.y = y;
      m.root.position.set(x, y);
      if (source === "peer") {
        m.peerMoveQueue = [];
        m.peerMoveAcc = 0;
        m.moving = false;
      }
    } else if (Math.hypot(m.homeX - x, m.homeY - y) > 4) {
      m.homeX = x;
      m.homeY = y;
    }
    if (source === "sce" && kind === "monster") {
      this.snapMonsterToWalkable(m);
    }

    // Map scene only: player / monster / NPC hard thin outline.
    // Monsters allow 8 glyphs so prefixes like "12级野猪" fit; peers/NPCs stay 6.
    const nameCap = kind === "monster" ? 8 : 6;
    await m.label.setText(label.slice(0, nameCap), color, {
      uc3: true,
      smooth: false,
      outline: nameOutline,
      outlineThin: true,
    });
    m.label.scale.set(NAMEPLATE_SCALE);
    this.layoutNameplate(m);

    if (kind === "peer") {
      void this.refreshTitleBadge(m, title, titleGif);
      if (!m.plus16Sparkles) {
        m.plus16Sparkles = new Plus16Sparkles();
        m.root.addChild(m.plus16Sparkles.root);
      }
      m.plus16Sparkles.setCount(plus16Count);
    } else if (kind === "npc" && source === "catalog") {
      void this.refreshQuestMark(m);
    }

    if (actorName && actorName !== m.actorName) {
      m.actorName = actorName;
      try {
        const loaded = await loadActor(actorName);
        if (this.markers.get(id) !== m) return;
        m.loaded = loaded;
        m.phase = 0;
        const parts = loaded.animations[0]?.parts.length ?? 0;
        const anims = loaded.animations.length;
        m.animate = animate && (parts > 1 || anims > 1);
        this.applyFrame(m);
      } catch (e) {
        console.warn("[actor-layer] load failed", actorName, e);
      }
    } else if (m.loaded && !m.sprite) {
      this.applyFrame(m);
    }
  }

  private clearQuestMarkSprite(m: Marker): void {
    if (!m.questMark) return;
    if (m.questMark.parent) m.root.removeChild(m.questMark);
    // Texture is shared via fuhaoTexCache — do not destroy it with the sprite.
    m.questMark.destroy({ texture: false });
    m.questMark = null;
  }

  /**
   * Drop untracked fuhao sprites left by overlapping async refreshes.
   * Those orphans sit at the pre-body nameplate Y and cover the NPC name after
   * the real mark is re-laid above the taller sprite.
   */
  private purgeOrphanQuestMarks(m: Marker): void {
    for (const child of [...m.root.children]) {
      if (child === m.sprite || child === m.label || child === m.titleBadge) {
        continue;
      }
      if (child === m.questMark) continue;
      if (!(child instanceof Sprite)) continue;
      m.root.removeChild(child);
      child.destroy({ texture: false });
    }
  }

  private async refreshQuestMark(m: Marker): Promise<void> {
    const name = (m.nameLabel || "").replace(/\0+$/g, "").trim();
    const prompt = (name && this.taskPrompts.get(name)) || 0;
    if (prompt === m.questPrompt && (prompt ? m.questMark : !m.questMark)) {
      this.purgeOrphanQuestMarks(m);
      this.layoutNameplate(m);
      return;
    }
    const gen = ++m.questMarkGen;
    m.questPrompt = prompt;
    this.clearQuestMarkSprite(m);
    this.purgeOrphanQuestMarks(m);
    if (!prompt) {
      this.layoutNameplate(m);
      return;
    }
    const tex = await loadFuhaoPromptTexture(prompt);
    // Catalog sync + 6/1/6/14 often overlap; only the latest gen may mount.
    if (this.markers.get(m.id) !== m || m.questMarkGen !== gen) return;
    if (m.questPrompt !== prompt) return;
    if (!tex) {
      this.layoutNameplate(m);
      return;
    }
    this.clearQuestMarkSprite(m);
    this.purgeOrphanQuestMarks(m);
    const spr = new Sprite(tex);
    spr.anchor.set(0.5, 1);
    spr.eventMode = "none";
    m.root.addChild(spr);
    m.questMark = spr;
    this.layoutNameplate(m);
  }

  private async refreshTitleBadge(
    m: Marker,
    titleLabel: string,
    titleGif: string,
  ): Promise<void> {
    const gif =
      resolveTitleGif(titleGif) || resolveTitleGif(titleLabel) || null;
    const key = gif || "";
    if (key === m.titleGifKey && (key ? m.titleBadge.visible : !m.titleBadge.visible)) {
      this.layoutNameplate(m);
      return;
    }
    m.titleGifKey = key;
    m.titleBadge.removeChildren().forEach((c) => c.destroy());
    m.titleSpr = null;
    if (!gif) {
      m.titleBadge.visible = false;
      this.layoutNameplate(m);
      return;
    }
    const tex = await tryLoadUiTexture(gif);
    if (this.markers.get(m.id) !== m) return;
    if (m.titleGifKey !== key) return;
    if (!tex) {
      m.titleBadge.visible = false;
      this.layoutNameplate(m);
      return;
    }
    m.titleSpr = new Sprite(tex);
    m.titleSpr.anchor.set(0.5, 1);
    m.titleBadge.addChild(m.titleSpr);
    m.titleBadge.visible = true;
    this.layoutNameplate(m);
  }

  private layoutNameplate(m: Marker): void {
    const bodyH = m.sprite
      ? Math.round(m.sprite.height)
      : 16;
    const nameH = m.label.height || 12;
    m.label.scale.set(NAMEPLATE_SCALE);
    m.label.position.set(
      -Math.floor(m.label.width / 2),
      -(bodyH + 2 + nameH),
    );
    let topY = m.label.y;
    if (m.titleBadge.visible) {
      m.titleBadge.position.set(0, m.label.y - 2);
      topY = m.titleBadge.y - (m.titleSpr?.height || 12);
    }
    if (m.questMark) {
      m.questMark.position.set(0, topY - 1);
    }
  }

  private applyFrame(m: Marker): void {
    if (!m.loaded) return;
    // SCE map sprites (ghostfire): partMode=frames flickers on phase even when idle.
    // Peers use walk pose while 1/2/2 timeline is playing.
    const moving =
      m.kind === "monster"
        ? m.source === "sce"
          ? m.moving
          : true
        : m.kind === "peer"
          ? m.moving
          : false;
    const composed = composeMapActor(m.loaded, m.dir, moving, m.phase);
    if (!composed) return;
    const tex = rgbaToTexture(composed.width, composed.height, composed.rgba);
    if (!m.sprite) {
      /* Drop placeholder Graphics only — keep plus16 sparkles / title / label. */
      for (const child of [...m.root.children]) {
        if (child instanceof Graphics) {
          m.root.removeChild(child);
          child.destroy();
          break;
        }
      }
      m.sprite = new Sprite(tex);
      m.sprite.anchor.set(0.5, 1);
      m.sprite.zIndex = 1;
      m.root.addChild(m.sprite);
    } else {
      const old = m.sprite.texture;
      m.sprite.texture = tex;
      old.destroy(true);
    }
    m.sprite.scale.set(
      composed.flipX ? -ACTOR_DISPLAY_SCALE : ACTOR_DISPLAY_SCALE,
      ACTOR_DISPLAY_SCALE,
    );
    this.layoutNameplate(m);
  }
}
