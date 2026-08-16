import { Application, Container, Graphics, Sprite, Texture } from "pixi.js";
import type { LoadedScene } from "../resources/loader";
import { formatMonsterMapName } from "../resources/monsterLevels";
import { ActorLayer } from "../game/ActorLayer";
import { LCD_WIDTH, MAP_VIEW_H, WORLD_TOP_H, ACTOR_DISPLAY_SCALE, NAMEPLATE_SCALE } from "../ui/constants";
import { BitmapLabel } from "../ui/BitmapLabel";
import { resolveTitleGif, isArenaTextTitle, arenaTextTitleColor } from "../ui/designation";
import { tryLoadUiTexture } from "../ui/UiAssets";
import {
  actorResourceForJobSex,
  composeMapActor,
  composePartStacked,
  loadActor,
  type LoadedActor,
} from "../resources/actor";
import {
  mapMoveBlocked,
  mapStandTrapped,
  findNearestEscapableStand,
  type MapPreview,
} from "../resources/map";
import { portalContains, type SceDecoration, type ScePortal } from "../resources/sce";
import { Plus16Sparkles } from "./Plus16Sparkles";
import { rgbaToTexture } from "./rgbaTexture";
import {
  getOrCreatePoseTexture,
  poseTextureKey,
  rebindPoseTextureKey,
  releasePoseTextureKey,
} from "./poseTextureCache";

/** CBE map nameplate colors (sampled from reference-shots). */
const NAME_YELLOW = 0xf8dc00;
const NAME_RED = 0xff4040;
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
/** Map-corner status strip (修/拍/擂/斗/练/娱). */
const STATUS_BADGE_SIZE = 17;
const STATUS_BADGE_PITCH = 20;
const STATUS_BADGE_X0 = 4;
const STATUS_BADGE_Y = 4;

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
  /** Resource leaf used for poseTextureCache keys. */
  private playerActorRes = "";
  /** CBE dir 1=up 2=right 3=down 4=left */
  private playerDir = 3;
  private playerMoving = false;
  private playerPhase = 0;
  private playerAnimAcc = 0;
  private playerPoseKey = "";
  /** Shared-cache key for portal glow frame (do not destroy(true) each tick). */
  private portalPoseKey = "";
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
  /** Top-left「修」status badge (SendSceneAction31 / 1/7/31). */
  onXiuTap: (() => void) | null = null;
  /** Top-left「拍」auction badge (right of 修). */
  onPaiTap: (() => void) | null = null;
  /** Top-left「擂」wager badge (right of 拍). */
  onLeiTap: (() => void) | null = null;
  /** Top-left「娱」entertainment badge (after 练). */
  onYuTap: (() => void) | null = null;
  /** Collision radius in map pixels. */
  monsterCollideRadius = 22;
  /** Approach / collide radius for talk prompt. */
  npcNearRadius = 36;
  private collideCooldownMs = 0;
  private portalCooldownMs = 0;
  /** Currently nearest in-range NPC (0 = none). */
  private nearNpcId = 0;
  private keys = new Set<string>();
  /** Click-to-walk destination in map pixels (null = none). */
  private walkTarget: { x: number; y: number } | null = null;
  private walkStuckMs = 0;
  private walkLastX = 0;
  private walkLastY = 0;
  private mapPointer: Graphics;
  private clip: Container;
  private lastTick = 0;
  private movePixelAcc = 0;
  /** Overhead designation GIF or Pixi arena text title. */
  private titleBadge = new Container();
  private titleSpr: Sprite | null = null;
  private titleTextLabel: BitmapLabel | null = null;
  private nameLabel = new BitmapLabel();
  private lastName = "";
  private lastTitleGif: string | null = null;
  private lastTitleText = "";
  private lastRedName = false;
  /** One rainbow gem per worn +16 piece (local player). */
  private plus16Sparkles = new Plus16Sparkles();
  private xiuBadge = new Container();
  private paiBadge = new Container();
  private leiBadge = new Container();
  /** Hangup / auto-fight 「斗」— vip_effect.gif, after 修/拍/擂. */
  private douBadge = new Container();
  /** Training buff 「练」— training.gif, after 斗. */
  private lianBadge = new Container();
  /** Entertainment 「娱」— after 斗/练 when shown, else right after 擂. */
  private yuBadge = new Container();
  private paiBlink = false;
  private paiBlinkPhase = 0;
  private leiBlink = false;
  private leiBlinkPhase = 0;
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
    // Behind world: empty-map taps walk here; NPC/peer markers sit above and win hit-test.
    this.mapPointer = new Graphics()
      .rect(0, 0, this.viewW, this.viewH)
      .fill({ color: 0x000000, alpha: 0.001 });
    this.mapPointer.eventMode = "static";
    this.mapPointer.cursor = "crosshair";
    this.mapPointer.on("pointertap", (e) => this.onMapPointerTap(e));
    this.clip.addChild(this.mapPointer);
    this.clip.addChild(this.world);
    this.world.sortableChildren = true;

    // Markers share world with player / decor for feet-Y depth sort.
    this.actors = new ActorLayer(this.world);

    this.player = new Container();
    this.player.zIndex = 0;
    this.player.sortableChildren = true;
    this.player.eventMode = "none";
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

    // Status strip on map top-left: 修 → 拍 → 擂 → (斗) → (练) → 娱.
    this.mountStatusBadgeSlot(this.xiuBadge, 0);
    this.xiuBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onXiuTap?.();
    });
    this.root.addChild(this.xiuBadge);
    void this.mountXiuArt();

    this.mountStatusBadgeSlot(this.paiBadge, 1);
    this.paiBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onPaiTap?.();
    });
    this.root.addChild(this.paiBadge);
    void this.mountPaiArt();

    this.mountStatusBadgeSlot(this.leiBadge, 2);
    this.leiBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onLeiTap?.();
    });
    this.root.addChild(this.leiBadge);
    void this.mountLeiArt();

    this.mountStatusBadgeSlot(this.douBadge, 3);
    this.douBadge.visible = false;
    this.douBadge.eventMode = "none";
    this.root.addChild(this.douBadge);
    void this.mountGifBadge(this.douBadge, "vip_effect.gif");

    this.mountStatusBadgeSlot(this.lianBadge, 4);
    this.lianBadge.visible = false;
    this.lianBadge.eventMode = "none";
    this.root.addChild(this.lianBadge);
    void this.mountGifBadge(this.lianBadge, "training.gif");

    this.mountStatusBadgeSlot(this.yuBadge, 3);
    this.yuBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onYuTap?.();
    });
    this.root.addChild(this.yuBadge);
    void this.mountYuArt();
    this.layoutStatusBadges();

    window.addEventListener("keydown", (e) => {
      if (!this.inputEnabled) return;
      const k = e.key.toLowerCase();
      if (
        k === "arrowleft" ||
        k === "arrowright" ||
        k === "arrowup" ||
        k === "arrowdown" ||
        k === "a" ||
        k === "d" ||
        k === "w" ||
        k === "s"
      ) {
        this.clearWalkTarget();
      }
      this.keys.add(k);
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
    if (down) {
      this.keys.add(k);
      this.clearWalkTarget();
    } else this.keys.delete(k);
  }

  /** Drop sticky move keys (battle enter / touch release while not in world). */
  clearHeldKeys(): void {
    this.keys.clear();
    this.clearWalkTarget();
  }

  clearWalkTarget(): void {
    this.walkTarget = null;
    this.walkStuckMs = 0;
  }

  /** Map-band tap → walk toward that map point (D-pad / keys cancel). */
  private onMapPointerTap(e: { getLocalPosition: (s: Container) => { x: number; y: number } }): void {
    if (!this.inputEnabled) return;
    const local = e.getLocalPosition(this.clip);
    const scale = this.scale || 1;
    const mx = (local.x - this.world.x) / scale;
    const my = (local.y - this.world.y) / scale;
    if (!Number.isFinite(mx) || !Number.isFinite(my)) return;
    this.walkTarget = {
      x: Math.max(8, Math.min(Math.max(8, this.mapWidth - 8), mx)),
      y: Math.max(8, Math.min(Math.max(8, this.mapHeight - 8), my)),
    };
    this.walkStuckMs = 0;
    this.walkLastX = this.playerX;
    this.walkLastY = this.playerY;
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
    this.actors.setMapCollision(null);
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
      this.mapSprite.eventMode = "none";
      this.world.addChildAt(this.mapSprite, 0);
      this.mapIdentity = identity;
    }

    this.mapWidth = scene.width;
    this.mapHeight = scene.height;
    this.mapPreview = scene.map;
    this.actors.setMapCollision(scene.map);
    this.portals = scene.portals ?? [];
    this.decorations = scene.decorations ?? [];
    this.playerX = spawnX ?? scene.width * 0.5;
    this.playerY = spawnY ?? scene.height * 0.55;
    this.portalCooldownMs = 1200;
    this.nearNpcId = 0;
    this.clearWalkTarget();
    this.clampPlayer();
    // Soft-shell / map-center / server persist can land in wall or fringe pocket.
    this.ensurePlayerWalkable("showScene");
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
      spr.eventMode = "none";
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
    if (this.portalPoseKey) {
      releasePoseTextureKey(this.portalPoseKey);
      this.portalPoseKey = "";
    }
    this.portalTex = null;
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
      spr.eventMode = "none";
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
    const key = poseTextureKey(
      "n_teleporter",
      0,
      false,
      this.portalPhase % parts,
      composed.width,
      composed.height,
    );
    rebindPoseTextureKey(this.portalPoseKey, key);
    this.portalPoseKey = key;
    const tex = getOrCreatePoseTexture(
      key,
      composed.width,
      composed.height,
      composed.rgba,
    );
    this.portalTex = tex;
    for (const spr of this.portalSprites) {
      spr.texture = tex;
      spr.scale.set(ACTOR_DISPLAY_SCALE);
    }
  }

  async setPlayerActor(jobIndex: number, sex: number): Promise<void> {
    const name = actorResourceForJobSex(jobIndex, sex);
    try {
      this.playerActor = await loadActor(name);
      this.playerActorRes = name;
      this.playerPhase = 0;
      rebindPoseTextureKey(this.playerPoseKey, "");
      this.playerPoseKey = "";
      this.applyPlayerFrame(true);
      if (this.lastName) {
        void this.setPlayerNameplate(
          this.lastName,
          this.lastTitleText || undefined,
          this.lastTitleGif ?? undefined,
          this.lastRedName,
        );
      }
    } catch (e) {
      console.warn("[scene] player actor", name, e);
      this.playerActor = null;
      this.playerActorRes = "";
      rebindPoseTextureKey(this.playerPoseKey, "");
      this.playerPoseKey = "";
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
    // Server/login can persist feet in solid or edge-fringe pockets.
    this.ensurePlayerWalkable("setPlayerPos", x, y);
    this.player.position.set(this.playerX, this.playerY);
    this.player.zIndex = this.playerY | 0;
    this.applyCamera();
  }

  /**
   * CBE nameplate: designation GIF above yellow role name.
   * Pixi arena text titles (no GIF): draw UC3 title string instead.
   */
  async setPlayerNameplate(
    name: string,
    titleLabel = "",
    titleGif: string | null | undefined = undefined,
    redName = false,
  ): Promise<void> {
    this.lastName = name.slice(0, 8);
    this.lastRedName = redName;
    const gif =
      resolveTitleGif(titleGif) ||
      resolveTitleGif(titleLabel) ||
      null;
    this.lastTitleGif = gif;
    const textTitle =
      !gif && (isArenaTextTitle(titleLabel) || isArenaTextTitle(titleGif))
        ? (isArenaTextTitle(titleLabel) ? titleLabel : titleGif || "").trim()
        : "";
    this.lastTitleText = textTitle;

    // Map scene self nameplate — only place outside ActorLayer that keeps outline.
    await this.nameLabel.setText(this.lastName, redName ? NAME_RED : NAME_YELLOW, {
      uc3: true,
      smooth: false,
      outline: true,
      outlineThin: true,
    });
    this.nameLabel.scale.set(NAMEPLATE_SCALE);
    this.layoutNameplate();

    this.titleBadge.removeChildren().forEach((c) => c.destroy());
    this.titleSpr = null;
    this.titleTextLabel = null;

    if (gif) {
      const tex = await tryLoadUiTexture(gif);
      if (tex) {
        this.titleSpr = new Sprite(tex);
        this.titleSpr.anchor.set(0.5, 1);
        this.titleBadge.addChild(this.titleSpr);
        this.titleBadge.visible = true;
        this.layoutNameplate();
      } else {
        this.titleBadge.visible = false;
      }
    } else if (textTitle) {
      const lab = new BitmapLabel();
      await lab.setText(textTitle.slice(0, 8), arenaTextTitleColor(textTitle), {
        uc3: true,
        smooth: false,
        outline: true,
        outlineThin: true,
      });
      lab.anchor.set(0.5, 1);
      lab.scale.set(NAMEPLATE_SCALE);
      this.titleTextLabel = lab;
      this.titleBadge.addChild(lab);
      this.titleBadge.visible = true;
      this.layoutNameplate();
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
      if (this.titleTextLabel) {
        this.titleTextLabel.position.set(0, 0);
      }
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
    const key = poseTextureKey(
      this.playerActorRes || "player",
      this.playerDir,
      this.playerMoving,
      this.playerPhase,
      composed.width,
      composed.height,
    );
    rebindPoseTextureKey(this.playerPoseKey, key);
    this.playerPoseKey = key;
    const tex = getOrCreatePoseTexture(
      key,
      composed.width,
      composed.height,
      composed.rgba,
    );
    if (!this.playerSprite) {
      this.playerSprite = new Sprite(tex);
      this.playerSprite.anchor.set(0.5, 1);
      this.playerSprite.zIndex = 1;
      this.player.addChild(this.playerSprite);
    } else {
      this.playerSprite.texture = tex;
    }
    this.playerSprite.scale.set(
      composed.flipX ? -ACTOR_DISPLAY_SCALE : ACTOR_DISPLAY_SCALE,
      ACTOR_DISPLAY_SCALE,
    );
    this.playerFallback.visible = false;
    this.layoutNameplate();
  }

  private async mountXiuArt(): Promise<void> {
    // Dark plate + cream edge (matches baked 斗/练 style). Do NOT use UIRed.gif
    // bright fill — it washes the glyph and looks like a solid red square.
    await this.mountTextStatusBadge(this.xiuBadge, "修", 0x2a1008, 0xe8d8a0);
  }

  private async mountPaiArt(): Promise<void> {
    await this.mountTextStatusBadge(this.paiBadge, "拍", 0x081828, 0xa8d8e8);
  }

  private async mountLeiArt(): Promise<void> {
    await this.mountTextStatusBadge(this.leiBadge, "擂", 0x281808, 0xe8c8a0);
  }

  private async mountYuArt(): Promise<void> {
    await this.mountTextStatusBadge(this.yuBadge, "娱", 0x102818, 0xa8e8c0);
  }

  /** Flash「拍」when auction claims / refunds are pending. */
  setPaiBlink(on: boolean): void {
    this.paiBlink = on;
    if (!on) {
      this.paiBadge.alpha = 1;
      this.paiBlinkPhase = 0;
    }
  }

  /** Flash「擂」when wager notices need attention (应战/拒绝/结算等). */
  setLeiBlink(on: boolean): void {
    this.leiBlink = on;
    if (!on) {
      this.leiBadge.alpha = 1;
      this.leiBlinkPhase = 0;
    }
  }

  /** Hangup/auto 「斗」tile after 修/拍/擂. */
  setDouVisible(on: boolean): void {
    this.douBadge.visible = !!on;
    this.layoutStatusBadges();
  }

  /** Training-buff 「练」tile after 斗. */
  setLianVisible(on: boolean): void {
    this.lianBadge.visible = !!on;
    this.layoutStatusBadges();
  }

  /**
   * Compact strip: 修/拍/擂 fixed; 斗/练 only occupy slots while visible;
   * 娱 always sits in the next free slot (so after 擂 when both are off).
   */
  private layoutStatusBadges(): void {
    let next = 3;
    if (this.douBadge.visible) {
      this.mountStatusBadgeSlot(this.douBadge, next++);
    }
    if (this.lianBadge.visible) {
      this.mountStatusBadgeSlot(this.lianBadge, next++);
    }
    this.mountStatusBadgeSlot(this.yuBadge, next);
  }

  private mountStatusBadgeSlot(host: Container, index: number): void {
    host.position.set(STATUS_BADGE_X0 + index * STATUS_BADGE_PITCH, STATUS_BADGE_Y);
    host.zIndex = 20;
    host.eventMode = "static";
    host.cursor = "pointer";
    host.hitArea = {
      contains: (px: number, py: number) =>
        px >= 0 && py >= 0 && px <= STATUS_BADGE_SIZE && py <= STATUS_BADGE_SIZE,
    };
  }

  private async mountTextStatusBadge(
    host: Container,
    text: string,
    fill: number,
    border: number,
  ): Promise<void> {
    host.removeChildren().forEach((c) => c.destroy());
    const plate = new Graphics()
      .rect(0, 0, STATUS_BADGE_SIZE, STATUS_BADGE_SIZE)
      .fill({ color: fill })
      .stroke({ width: 1, color: border });
    host.addChild(plate);
    const label = new BitmapLabel();
    await label.setText(text, 0xffe080, { outline: false });
    label.position.set(
      Math.floor((STATUS_BADGE_SIZE - label.width) / 2),
      Math.floor((STATUS_BADGE_SIZE - label.height) / 2),
    );
    host.addChild(label);
  }

  private async mountGifBadge(host: Container, name: string): Promise<void> {
    const tex = await tryLoadUiTexture(name);
    host.removeChildren().forEach((c) => c.destroy());
    if (!tex) return;
    const spr = new Sprite(tex);
    spr.width = STATUS_BADGE_SIZE;
    spr.height = STATUS_BADGE_SIZE;
    host.addChild(spr);
  }

  resetCamera(): void {
    this.scale = 1;
    this.applyCamera();
  }

  private clampPlayer(): void {
    this.playerX = Math.max(8, Math.min(Math.max(8, this.mapWidth - 8), this.playerX));
    this.playerY = Math.max(8, Math.min(Math.max(8, this.mapHeight - 8), this.playerY));
  }

  /**
   * Snap out of 0xF solid or fringe cells where every short step is blocked.
   * Called on showScene / setPlayerPos so login does not leave the player stuck.
   */
  private ensurePlayerWalkable(
    reason: string,
    fromX = this.playerX,
    fromY = this.playerY,
  ): void {
    const map = this.mapPreview;
    if (!map?.cells.length) return;
    if (!mapStandTrapped(map, this.playerX, this.playerY)) return;
    const safe = findNearestEscapableStand(map, this.playerX, this.playerY);
    if (!safe) return;
    if (safe.x === (this.playerX | 0) && safe.y === (this.playerY | 0)) return;
    console.warn(
      `[scene] ${reason} trapped @${fromX | 0},${fromY | 0} → ${safe.x},${safe.y}`,
    );
    this.playerX = safe.x;
    this.playerY = safe.y;
  }

  private tick(): void {
    const now = performance.now();
    const dt = this.lastTick ? now - this.lastTick : 16;
    this.lastTick = now;
    this.actors.tick(dt);
    this.plus16Sparkles.tick(dt);
    if (this.paiBlink) {
      this.paiBlinkPhase += dt * 0.01;
      this.paiBadge.alpha = 0.35 + 0.65 * (0.5 + 0.5 * Math.sin(this.paiBlinkPhase));
    }
    if (this.leiBlink) {
      this.leiBlinkPhase += dt * 0.01;
      this.leiBadge.alpha = 0.35 + 0.65 * (0.5 + 0.5 * Math.sin(this.leiBlinkPhase));
    }
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

    // Click-to-walk: steer toward target when no D-pad / keyboard hold.
    if (dx === 0 && dy === 0 && this.walkTarget) {
      const tx = this.walkTarget.x - this.playerX;
      const ty = this.walkTarget.y - this.playerY;
      const dist = Math.hypot(tx, ty);
      if (dist <= 6) {
        this.clearWalkTarget();
      } else {
        dx = tx / dist;
        dy = ty / dist;
      }
    }

    const moving = dx !== 0 || dy !== 0;
    if (moving) {
      const len = Math.hypot(dx, dy) || 1;
      const nx = this.playerX + (dx / len) * speed;
      const ny = this.playerY + (dy / len) * speed;
      // Axis-separated slide so one blocked axis does not freeze both.
      let tx = nx;
      let ty = ny;
      // Inside solid or fringe pocket (no open step): allow escape.
      // Edge fringe uses CBE exit|entry — mapMoveBlocked.
      const stuck =
        !!this.mapPreview &&
        mapStandTrapped(this.mapPreview, this.playerX, this.playerY);
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

      // Click-path stuck on collision: stop instead of spinning in place.
      if (this.walkTarget) {
        const step = Math.hypot(this.playerX - this.walkLastX, this.playerY - this.walkLastY);
        if (step < 0.15) this.walkStuckMs += dtMove;
        else this.walkStuckMs = 0;
        this.walkLastX = this.playerX;
        this.walkLastY = this.playerY;
        if (this.walkStuckMs > 450) this.clearWalkTarget();
      }

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

  /**
   * Android WebView often drops the WebGL context after long screen-off.
   * Pixi clears GPU texture slots on restore; nudge CPU sources / rebuild actors.
   */
  nudgeAfterGpuResume(): void {
    try {
      this.mapSprite?.texture?.source?.update?.();
    } catch {
      /* ignore */
    }
    for (const spr of this.decorSprites) {
      try {
        spr.texture?.source?.update?.();
      } catch {
        /* ignore */
      }
    }
    this.applyPlayerFrame(true);
    this.actors.nudgeAfterGpuResume();
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
