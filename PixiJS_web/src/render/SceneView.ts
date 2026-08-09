import { Application, Container, Graphics, Sprite, Texture } from "pixi.js";
import type { LoadedScene } from "../resources/loader";
import { formatMonsterMapName } from "../resources/monsterLevels";
import { ActorLayer } from "../game/ActorLayer";
import { LCD_WIDTH, MAP_VIEW_H, WORLD_TOP_H, ACTOR_DISPLAY_SCALE, NAMEPLATE_SCALE } from "../ui/constants";
import { BitmapLabel } from "../ui/BitmapLabel";
import { resolveTitleGif } from "../ui/designation";
import { tryLoadUiTexture } from "../ui/UiAssets";
import {
  actorResourceForJobSex,
  composeMapActor,
  composePartStacked,
  loadActor,
  type LoadedActor,
} from "../resources/actor";
import {
  mapBlocksAt,
  mapMoveBlocked,
  findNearestWalkable,
  type MapPreview,
} from "../resources/map";
import { portalContains, type SceDecoration, type ScePortal } from "../resources/sce";
import { Plus16Sparkles } from "./Plus16Sparkles";
import { rgbaToTexture } from "./rgbaTexture";

/** CBE map nameplate colors (sampled from reference-shots). */
const NAME_YELLOW = 0xf8dc00;
/** Shared overworld portal FX (`n_teleporter.actor` + `n_teleporter.gif`). */
const PORTAL_ACTOR = "n_teleporter.actor";
/**
 * Local walk speed in map-px / ms.
 * Historical feel was 1 px per ~60Hz ticker frame (~60 px/s). Must be time-based:
 * per-frame steps race on 90/120Hz OEM displays after APK release.
 */
const WALK_PX_PER_MS = 1 / (1000 / 60);
/** Cap one tick so a long hitch cannot teleport past collision samples. */
const WALK_DT_CLAMP_MS = 50;

/**
 * World map camera inside the CBE LCD map band (below HUD, above softkeys).
 */
export class SceneView {
  readonly root = new Container();
  readonly world = new Container();
  readonly actors: ActorLayer;
  private mapSprite: Sprite | null = null;
  private decorSprites: Sprite[] = [];
  private decorPaintGen = 0;
  private player: Container;
  private playerSprite: Sprite | null = null;
  private playerFallback: Graphics;
  private playerActor: LoadedActor | null = null;
  /** CBE dir 1=up 2=right 3=down 4=left */
  private playerDir = 3;
  private playerMoving = false;
  private playerPhase = 0;
  private playerAnimAcc = 0;
  private mapWidth = 0;
  private mapHeight = 0;
  /** Last composed map identity — Lin'an scenes share 400×400; size≠same map. */
  private mapIdentity = "";
  private viewW = LCD_WIDTH;
  private viewH = MAP_VIEW_H;
  scale = 1;
  playerX = 0;
  playerY = 0;
  inputEnabled = true;
  /** CBE dir 1=up 2=right 3=down 4=left; called once per moved tick. */
  onMoveDir: ((dir: number) => void) | null = null;
  onNpcTap: ((actorId: number) => void) | null = null;
  /** Player entered talk range of a catalog NPC. */
  onNpcApproach: ((actorId: number, name: string) => void) | null = null;
  /** Player left talk range (no NPC nearby). */
  onNpcDepart: (() => void) | null = null;
  /** Player touched an SCE monster marker (local actorId). */
  onMonsterCollide: ((actorId: number) => void) | null = null;
  /** Walked into an SCE portal trigger rect. */
  onPortalEnter: ((portal: ScePortal) => void) | null = null;
  /** Top-right「修」status badge (SendSceneAction31 / 1/7/31). */
  onXiuTap: (() => void) | null = null;
  /** Collision radius in map pixels. */
  monsterCollideRadius = 22;
  /** Approach / collide radius for talk prompt. */
  npcNearRadius = 36;
  private collideCooldownMs = 0;
  private portalCooldownMs = 0;
  /** Currently nearest in-range NPC (0 = none). */
  private nearNpcId = 0;
  private keys = new Set<string>();
  private clip: Container;
  private lastTick = 0;
  private movePixelAcc = 0;
  /** Overhead designation GIF (riches_name* / level_name*), not DrawString. */
  private titleBadge = new Container();
  private titleSpr: Sprite | null = null;
  private nameLabel = new BitmapLabel();
  private lastName = "";
  private lastTitleGif: string | null = null;
  /** One rainbow gem per worn +16 piece (local player). */
  private plus16Sparkles = new Plus16Sparkles();
  private xiuBadge = new Container();
  private mapPreview: MapPreview | null = null;
  private portals: ScePortal[] = [];
  private decorations: SceDecoration[] = [];
  private teleporterActor: LoadedActor | null = null;
  private portalSprites: Sprite[] = [];
  private portalLabels: BitmapLabel[] = [];
  private portalTex: Texture | null = null;
  private portalPhase = 0;
  private portalAnimAcc = 0;
  private portalPaintGen = 0;

  constructor(private readonly app: Application) {
    this.root.y = WORLD_TOP_H;
    this.clip = new Container();
    const mask = new Graphics().rect(0, 0, this.viewW, this.viewH).fill({ color: 0xffffff });
    this.clip.mask = mask;
    this.clip.addChild(mask);
    this.clip.addChild(this.world);
    this.world.sortableChildren = true;

    // Markers share world with player / decor for feet-Y depth sort.
    this.actors = new ActorLayer(this.world);

    this.player = new Container();
    this.player.zIndex = 0;
    this.player.sortableChildren = true;
    this.playerFallback = new Graphics()
      .circle(0, -4, 5)
      .fill({ color: 0xffe08a })
      .stroke({ width: 1, color: 0x5a4010 });
    this.titleBadge.zIndex = 3;
    this.nameLabel.zIndex = 2;
    this.nameLabel.pivot.set(0, 0);
    /* Foot halo under body (sprite z=1). */
    this.plus16Sparkles.root.zIndex = 0;
    this.playerFallback.zIndex = 0;
    this.player.addChild(
      this.plus16Sparkles.root,
      this.playerFallback,
      this.titleBadge,
      this.nameLabel,
    );
    // Portals join world feet-Y sort with player/decor (not a flat z=10000 layer).
    this.world.addChild(this.player);
    this.root.addChild(this.clip);

    this.xiuBadge.position.set(4, 4);
    this.xiuBadge.zIndex = 20;
    this.xiuBadge.eventMode = "static";
    this.xiuBadge.cursor = "pointer";
    this.xiuBadge.hitArea = {
      contains: (px: number, py: number) =>
        px >= 0 && py >= 0 && px <= 18 && py <= 18,
    };
    this.xiuBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onXiuTap?.();
    });
    this.root.addChild(this.xiuBadge);
    void this.mountXiuArt();

    window.addEventListener("keydown", (e) => {
      this.keys.add(e.key.toLowerCase());
    });
    window.addEventListener("keyup", (e) => {
      this.keys.delete(e.key.toLowerCase());
    });

    this.actors.onNpcTap = (id) => this.onNpcTap?.(id);
    this.app.ticker.add(() => this.tick());
  }

  /** Virtual D-pad / HTML controls — same keys as WASD / arrows. */
  setHeldKey(key: string, down: boolean): void {
    const k = key.toLowerCase();
    if (down) this.keys.add(k);
    else this.keys.delete(k);
  }

  /** Drop sticky move keys (battle enter / touch release while not in world). */
  clearHeldKeys(): void {
    this.keys.clear();
  }

  /** Collision / tile map for minimap. */
  getMapPreview(): MapPreview | null {
    return this.mapPreview;
  }

  getMapPixelSize(): { w: number; h: number } {
    return { w: this.mapWidth, h: this.mapHeight };
  }

  /** NPC / peer / monster / teleport-stone / portal markers for minimap dots. */
  liveMinimapMarkers(): {
    x: number;
    y: number;
    kind: "npc" | "peer" | "monster" | "teleport";
  }[] {
    const out = this.actors.liveMinimapMarkers();
    for (const p of this.portals) {
      out.push({
        x: ((p.left + p.right) / 2) | 0,
        y: ((p.top + p.bottom) / 2) | 0,
        kind: "teleport",
      });
    }
    return out;
  }

  /**
   * Drop the painted map shell. Role-select must call this so the next enter
   * always rebuilds from the server scene key (never the previous role's map).
   */
  invalidateMap(): void {
    if (this.mapSprite) {
      this.world.removeChild(this.mapSprite);
      this.mapSprite.destroy({ texture: true });
      this.mapSprite = null;
    }
    this.mapIdentity = "";
    this.actors.clear();
    this.mapPreview = null;
    this.portals = [];
    this.decorations = [];
    for (const spr of this.decorSprites) {
      this.world.removeChild(spr);
      spr.destroy({ texture: true });
    }
    this.decorSprites = [];
  }

  async showScene(scene: LoadedScene, spawnX?: number, spawnY?: number): Promise<void> {
    // Town maps share 400×400 dims — must key on scene/map leaf, not size alone.
    const identity = `${scene.sceneFile}|${scene.mapFile}|${scene.width}x${scene.height}`;
    const sameMap = !!this.mapSprite && this.mapIdentity === identity;
    if (this.mapSprite && !sameMap) {
      this.world.removeChild(this.mapSprite);
      this.mapSprite.destroy({ texture: true });
      this.mapSprite = null;
    }
    if (!sameMap) this.actors.clear();

    if (!this.mapSprite) {
      const canvas = document.createElement("canvas");
      canvas.width = scene.width;
      canvas.height = scene.height;
      const ctx = canvas.getContext("2d")!;
      ctx.putImageData(
        new ImageData(new Uint8ClampedArray(scene.rgba), scene.width, scene.height),
        0,
        0,
      );
      const texture = Texture.from(canvas);
      texture.source.scaleMode = "nearest";
      this.mapSprite = new Sprite(texture);
      this.mapSprite.zIndex = 0;
      this.world.addChildAt(this.mapSprite, 0);
      this.mapIdentity = identity;
    }

    this.mapWidth = scene.width;
    this.mapHeight = scene.height;
    this.mapPreview = scene.map;
    this.portals = scene.portals ?? [];
    this.decorations = scene.decorations ?? [];
    this.playerX = spawnX ?? scene.width * 0.5;
    this.playerY = spawnY ?? scene.height * 0.55;
    this.portalCooldownMs = 1200;
    this.nearNpcId = 0;
    this.clampPlayer();
    this.player.position.set(this.playerX, this.playerY);
    this.player.zIndex = this.playerY | 0;
    this.applyCamera();
    this.paintDecorations();
    this.paintPortals();
    // Wild monsters: SCE kind=3 dual = map sprite (ghostfire); primary = battle.
    this.actors.setSceneMonsters(
      (scene.combatSpawns ?? []).map((s, i) => ({
        actorId: 0x70000000 + i,
        x: s.x,
        y: s.y,
        name: formatMonsterMapName(s.name, s.sceActorId),
        actorResource: s.mapActorResource,
        battleActorResource: s.battleActorResource,
        scriptName: `sce_combat_${s.sceActorId}`,
        battleActorId: s.sceActorId,
      })),
    );
    this.actors.setTeleportStones(
      (scene.teleportStones ?? []).map((s, i) => ({
        actorId: 0x60000000 + i,
        x: s.x,
        y: s.y,
        name: s.name || "传送石",
        actorResource: s.actorResource,
        scriptName: s.scriptName || "telestone.xse",
      })),
    );
    this.applyPlayerFrame(true);
    console.info(
      "[scene]",
      scene.sceneFile,
      `portals×${this.portals.length}`,
      `decor×${this.decorations.length}`,
      `stones×${scene.teleportStones?.length ?? 0}`,
    );
  }

  private paintDecorations(): void {
    const gen = ++this.decorPaintGen;
    for (const spr of this.decorSprites) {
      this.world.removeChild(spr);
      spr.destroy({ texture: true });
    }
    this.decorSprites = [];
    void this.mountDecorations(gen);
  }

  /** Stamp SCE decor on world with feet-Y zIndex (interleave with player). */
  private async mountDecorations(gen: number): Promise<void> {
    const list = this.decorations;
    if (!list.length) return;
    const cache = new Map<string, LoadedActor>();
    for (const d of list) {
      if (gen !== this.decorPaintGen) return;
      let actor = cache.get(d.actorResource);
      if (!actor) {
        try {
          actor = await loadActor(d.actorResource);
          cache.set(d.actorResource, actor);
        } catch (e) {
          console.warn("[scene] decor actor", d.actorResource, e);
          continue;
        }
      }
      if (gen !== this.decorPaintGen) return;
      const composed = composeMapActor(actor, 3, false, 0);
      if (!composed) continue;
      const tex = rgbaToTexture(composed.width, composed.height, composed.rgba);
      const spr = new Sprite(tex);
      spr.anchor.set(0.5, 1);
      spr.scale.set(ACTOR_DISPLAY_SCALE);
      spr.position.set(d.x, d.y);
      spr.zIndex = d.y;
      this.world.addChild(spr);
      this.decorSprites.push(spr);
    }
  }

  private paintPortals(): void {
    const gen = ++this.portalPaintGen;
    for (const spr of this.portalSprites) {
      this.world.removeChild(spr);
      spr.destroy({ texture: false });
    }
    this.portalSprites = [];
    for (const lab of this.portalLabels) {
      this.world.removeChild(lab);
      lab.destroy();
    }
    this.portalLabels = [];
    if (this.portalTex) {
      this.portalTex.destroy(true);
      this.portalTex = null;
    }
    void this.mountPortalFx(gen);
  }

  /**
   * Stamp shared `n_teleporter` glow at each portal AABB centre.
   * zIndex = trigger bottom (feet) so the player can walk in front of the glow.
   */
  private async mountPortalFx(gen: number): Promise<void> {
    if (!this.teleporterActor) {
      try {
        this.teleporterActor = await loadActor(PORTAL_ACTOR);
      } catch (e) {
        console.warn("[scene] portal actor", PORTAL_ACTOR, e);
        return;
      }
    }
    if (gen !== this.portalPaintGen) return;

    for (const p of this.portals) {
      /* Hide named/battle portal FX + labels; edge/meta keep the teleporter glow.
       * Collision / transfer still use this.portals unchanged. */
      if (p.kind === "named") continue;

      const cx = ((p.left + p.right) / 2) | 0;
      const cy = ((p.top + p.bottom) / 2) | 0;
      const feetY = p.bottom | 0;
      const spr = new Sprite(Texture.EMPTY);
      spr.anchor.set(0.5, 0.5);
      spr.position.set(cx, cy);
      spr.zIndex = feetY;
      this.world.addChild(spr);
      this.portalSprites.push(spr);
    }
    this.applyPortalFrames();
  }

  private applyPortalFrames(): void {
    if (!this.teleporterActor || !this.portalSprites.length) return;
    const parts = this.teleporterActor.animations[0]?.parts.length ?? 0;
    if (!parts) return;
    const composed = composePartStacked(
      this.teleporterActor,
      0,
      this.portalPhase % parts,
    );
    if (!composed || composed.width < 2 || composed.height < 2) return;
    const tex = rgbaToTexture(composed.width, composed.height, composed.rgba);
    const prev = this.portalTex;
    this.portalTex = tex;
    for (const spr of this.portalSprites) {
      spr.texture = tex;
      spr.scale.set(ACTOR_DISPLAY_SCALE);
    }
    if (prev) prev.destroy(true);
  }

  async setPlayerActor(jobIndex: number, sex: number): Promise<void> {
    const name = actorResourceForJobSex(jobIndex, sex);
    try {
      this.playerActor = await loadActor(name);
      this.playerPhase = 0;
      this.applyPlayerFrame(true);
      if (this.lastName) {
        void this.setPlayerNameplate(this.lastName, undefined, this.lastTitleGif ?? undefined);
      }
    } catch (e) {
      console.warn("[scene] player actor", name, e);
      this.playerActor = null;
      this.playerFallback.visible = true;
    }
  }

  /** Local player: one rainbow sparkle per worn enhance >= 16. */
  setPlus16Count(count: number): void {
    this.plus16Sparkles.setCount(count);
  }

  setPlayerPos(x: number, y: number): void {
    this.playerX = x;
    this.playerY = y;
    this.clampPlayer();
    // Server/login can persist feet inside solid (临安南墙). Snap to nearest walkable.
    if (this.mapPreview && mapBlocksAt(this.mapPreview, this.playerX, this.playerY, 0)) {
      const safe = findNearestWalkable(this.mapPreview, this.playerX, this.playerY);
      if (safe) {
        console.warn(
          `[scene] feet in solid @${x | 0},${y | 0} → walkable ${safe.x},${safe.y}`,
        );
        this.playerX = safe.x;
        this.playerY = safe.y;
      }
    }
    this.player.position.set(this.playerX, this.playerY);
    this.player.zIndex = this.playerY | 0;
    this.applyCamera();
  }

  /**
   * CBE nameplate: designation GIF (`riches_name*` / `level_name*`) above
   * yellow system-font role name — never DrawString for the title badge.
   */
  async setPlayerNameplate(
    name: string,
    titleLabel = "",
    titleGif: string | null | undefined = undefined,
  ): Promise<void> {
    this.lastName = name.slice(0, 8);
    const gif =
      resolveTitleGif(titleGif) ||
      resolveTitleGif(titleLabel) ||
      null;
    this.lastTitleGif = gif;

    // Map scene self nameplate — only place outside ActorLayer that keeps outline.
    await this.nameLabel.setText(this.lastName, NAME_YELLOW, {
      uc3: true,
      smooth: false,
      outline: true,
      outlineThin: true,
    });
    this.nameLabel.scale.set(NAMEPLATE_SCALE);
    this.layoutNameplate();

    if (gif) {
      const tex = await tryLoadUiTexture(gif);
      this.titleBadge.removeChildren().forEach((c) => c.destroy());
      this.titleSpr = null;
      if (tex) {
        this.titleSpr = new Sprite(tex);
        this.titleSpr.anchor.set(0.5, 1);
        this.titleBadge.addChild(this.titleSpr);
        this.titleBadge.visible = true;
        this.layoutNameplate();
      } else {
        this.titleBadge.visible = false;
      }
    } else {
      this.titleBadge.visible = false;
    }
  }

  private layoutNameplate(): void {
    const nameH = this.nameLabel.height || 16;
    const lift = this.playerSprite ? this.playerSprite.height + 2 : 20;
    this.nameLabel.position.set(-Math.floor(this.nameLabel.width / 2), -lift - nameH);
    if (this.titleBadge.visible) {
      this.titleBadge.position.set(0, this.nameLabel.y - 2);
    }
  }

  private applyPlayerFrame(force = false): void {
    if (!this.playerActor) return;
    const composed = composeMapActor(
      this.playerActor,
      this.playerDir,
      this.playerMoving,
      this.playerPhase,
    );
    if (!composed || composed.width < 4 || composed.height < 4) {
      if (force) {
        console.warn("[scene] empty map pose");
        this.playerFallback.visible = true;
      }
      return;
    }
    const tex = rgbaToTexture(composed.width, composed.height, composed.rgba);
    if (!this.playerSprite) {
      this.playerSprite = new Sprite(tex);
      this.playerSprite.anchor.set(0.5, 1);
      this.playerSprite.zIndex = 1;
      this.player.addChild(this.playerSprite);
    } else {
      const old = this.playerSprite.texture;
      this.playerSprite.texture = tex;
      old.destroy(true);
    }
    this.playerSprite.scale.set(
      composed.flipX ? -ACTOR_DISPLAY_SCALE : ACTOR_DISPLAY_SCALE,
      ACTOR_DISPLAY_SCALE,
    );
    this.playerFallback.visible = false;
    this.layoutNameplate();
  }

  private async mountXiuArt(): Promise<void> {
    const tex = await tryLoadUiTexture("UIRed.gif");
    this.xiuBadge.removeChildren().forEach((c) => c.destroy());
    if (tex) {
      const spr = new Sprite(tex);
      spr.width = 16;
      spr.height = 16;
      this.xiuBadge.addChild(spr);
    } else {
      this.xiuBadge.addChild(
        new Graphics()
          .rect(0, 0, 16, 16)
          .fill({ color: 0xc02828 })
          .stroke({ width: 1, color: 0xe8c080 }),
      );
    }
    const label = new BitmapLabel();
    await label.setText("修", 0xffe080, { outline: false });
    label.position.set(Math.floor((16 - label.width) / 2), Math.floor((16 - label.height) / 2));
    this.xiuBadge.addChild(label);
  }

  resetCamera(): void {
    this.scale = 1;
    this.applyCamera();
  }

  private clampPlayer(): void {
    this.playerX = Math.max(8, Math.min(Math.max(8, this.mapWidth - 8), this.playerX));
    this.playerY = Math.max(8, Math.min(Math.max(8, this.mapHeight - 8), this.playerY));
  }

  private tick(): void {
    const now = performance.now();
    const dt = this.lastTick ? now - this.lastTick : 16;
    this.lastTick = now;
    this.actors.tick(dt);
    this.plus16Sparkles.tick(dt);
    if (this.collideCooldownMs > 0) this.collideCooldownMs -= dt;
    if (this.portalCooldownMs > 0) this.portalCooldownMs -= dt;

    if (this.portalSprites.length) {
      this.portalAnimAcc += dt;
      if (this.portalAnimAcc >= 120) {
        this.portalAnimAcc = 0;
        this.portalPhase = (this.portalPhase + 1) % 256;
        this.applyPortalFrames();
      }
    }

    if (!this.inputEnabled) return;
    const dtMove = Math.min(Math.max(dt, 0), WALK_DT_CLAMP_MS);
    const speed = WALK_PX_PER_MS * dtMove;
    let dx = 0;
    let dy = 0;
    if (this.keys.has("arrowleft") || this.keys.has("a")) dx -= 1;
    if (this.keys.has("arrowright") || this.keys.has("d")) dx += 1;
    if (this.keys.has("arrowup") || this.keys.has("w")) dy -= 1;
    if (this.keys.has("arrowdown") || this.keys.has("s")) dy += 1;

    const moving = dx !== 0 || dy !== 0;
    if (moving) {
      const len = Math.hypot(dx, dy) || 1;
      const nx = this.playerX + (dx / len) * speed;
      const ny = this.playerY + (dy / len) * speed;
      // Axis-separated slide so one blocked axis does not freeze both.
      let tx = nx;
      let ty = ny;
      // Already inside solid (bad persist / portal): allow escape; only block free→solid.
      // Edge fringe (MAP high-nibble N/W/S/E) uses CBE exit|entry — mapMoveBlocked.
      const stuck =
        !!this.mapPreview && mapBlocksAt(this.mapPreview, this.playerX, this.playerY, 0);
      if (!stuck && this.mapPreview) {
        const ox = this.playerX;
        const oy = this.playerY;
        if (mapMoveBlocked(this.mapPreview, ox, oy, tx, oy, 1)) {
          tx = ox;
        }
        if (mapMoveBlocked(this.mapPreview, tx, oy, tx, ty, 1)) {
          ty = oy;
        }
        if (mapMoveBlocked(this.mapPreview, ox, oy, tx, ty, 1)) {
          tx = ox;
          ty = oy;
        }
      }
      this.playerX = tx;
      this.playerY = ty;
      this.clampPlayer();
      this.player.position.set(this.playerX, this.playerY);
      this.player.zIndex = this.playerY | 0;
      this.applyCamera();

      let dir = this.playerDir;
      if (Math.abs(dx) >= Math.abs(dy)) dir = dx > 0 ? 2 : 4;
      else dir = dy > 0 ? 3 : 1;
      const dirChanged = dir !== this.playerDir;
      this.playerDir = dir;
      this.playerMoving = true;

      this.playerAnimAcc += dt;
      let stepped = false;
      while (this.playerAnimAcc >= 140) {
        this.playerAnimAcc -= 140;
        this.playerPhase = (this.playerPhase + 1) % 256;
        stepped = true;
      }
      if (stepped || dirChanged || !this.playerSprite) this.applyPlayerFrame();

      this.movePixelAcc += speed;
      while (this.movePixelAcc >= 4) {
        this.movePixelAcc -= 4;
        this.onMoveDir?.(dir);
      }

      this.checkMonsterCollision();
      this.checkNpcProximity();
      this.checkPortalTrigger();
    } else {
      this.movePixelAcc = 0;
      this.playerAnimAcc = 0;
      if (this.playerMoving) {
        // Keep facing; side idle pose comes from composeMapActor (walk part6 收脚).
        this.playerMoving = false;
        this.playerPhase = 0;
        this.applyPlayerFrame(true);
      }
      this.checkMonsterCollision();
      this.checkNpcProximity();
      this.checkPortalTrigger();
    }
  }

  /** Nearest catalog NPC in talk range, or null. */
  nearestInteractNpc(): { actorId: number; name: string } | null {
    const r2 = this.npcNearRadius * this.npcNearRadius;
    let best: { actorId: number; name: string } | null = null;
    let bestD = r2;
    for (const n of this.actors.liveInteractNpcs()) {
      const dx = n.x - this.playerX;
      const dy = n.y - this.playerY;
      const d = dx * dx + dy * dy;
      if (d <= bestD) {
        bestD = d;
        best = { actorId: n.actorId, name: n.name };
      }
    }
    return best;
  }

  /** Clear approach latch so walking back to the same NPC can re-prompt. */
  clearNpcApproachLatch(): void {
    this.nearNpcId = 0;
  }

  private checkNpcProximity(): void {
    if (!this.inputEnabled || !this.onNpcApproach) return;
    const near = this.nearestInteractNpc();
    if (near) {
      if (near.actorId !== this.nearNpcId) {
        this.nearNpcId = near.actorId;
        this.onNpcApproach(near.actorId, near.name);
      }
      return;
    }
    if (this.nearNpcId) {
      this.nearNpcId = 0;
      this.onNpcDepart?.();
    }
  }

  /** Brief lockout after battle so the same spawn does not re-trigger instantly. */
  armCollideCooldown(ms = 1800): void {
    this.collideCooldownMs = ms;
  }

  private checkMonsterCollision(): void {
    if (this.collideCooldownMs > 0 || !this.onMonsterCollide) return;
    const r2 = this.monsterCollideRadius * this.monsterCollideRadius;
    for (const m of this.actors.liveSceneMonsters()) {
      const dx = m.x - this.playerX;
      const dy = m.y - this.playerY;
      if (dx * dx + dy * dy <= r2) {
        this.collideCooldownMs = 2500;
        this.onMonsterCollide(m.actorId);
        return;
      }
    }
  }

  private checkPortalTrigger(): void {
    if (this.portalCooldownMs > 0 || !this.onPortalEnter || !this.portals.length) {
      return;
    }
    for (const p of this.portals) {
      if (!portalContains(p, this.playerX, this.playerY, 2)) continue;
      this.portalCooldownMs = 2500;
      this.onPortalEnter(p);
      return;
    }
  }

  private applyCamera(): void {
    this.world.scale.set(this.scale);
    const mapW = this.mapWidth * this.scale;
    const mapH = this.mapHeight * this.scale;
    let x = this.viewW * 0.5 - this.playerX * this.scale;
    let y = this.viewH * 0.5 - this.playerY * this.scale;
    // Keep the viewport inside the map so edge walking does not show black gutters.
    if (mapW <= this.viewW) {
      x = (this.viewW - mapW) * 0.5;
    } else {
      const minX = this.viewW - mapW;
      x = Math.min(0, Math.max(minX, x));
    }
    if (mapH <= this.viewH) {
      y = (this.viewH - mapH) * 0.5;
    } else {
      const minY = this.viewH - mapH;
      y = Math.min(0, Math.max(minY, y));
    }
    this.world.position.set(x, y);
  }
}
