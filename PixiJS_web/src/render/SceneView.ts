import { Application, Container, Graphics, Sprite, Texture } from "pixi.js";
import type { LoadedScene } from "../resources/loader";
import { formatCombatSpawnNameplate } from "../resources/monsterLevels";
import { ActorLayer } from "../game/ActorLayer";
import { LCD_WIDTH, MAP_VIEW_H, WORLD_TOP_H, ACTOR_DISPLAY_SCALE, NAMEPLATE_SCALE, actorSpriteScale, CBE, MAP_STATUS_STRIP_H } from "../ui/constants";
import { BitmapLabel } from "../ui/BitmapLabel";
import { sceneAssetCandidates } from "../net/protocol";
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
  clearPoseTextureCache,
  getOrCreatePoseTexture,
  poseTextureKey,
  rebindPoseTextureKey,
  releasePoseTextureKey,
} from "./poseTextureCache";
import { nudgePlistAtlasesAfterGpuResume } from "../resources/plistAtlas";
import {
  createSolidMaskSprite,
  remountSolidMaskSprite,
  replaceGraphics,
} from "./gpuResume";
import {
  WingOverlay,
  writeWingIdPreference,
} from "../resources/wingFx";
import {
  MountOverlay,
  MOUNT_BODY_LIFT,
  MOUNT_NAMEPLATE_EXTRA,
  writeMountIdPreference,
} from "../resources/mountFx";
import {
  ChenghaoOverlay,
  chenghaoLabel,
  parseChenghaoId,
} from "../resources/chenghaoFx";
import {
  isStatusStripCollapsed,
  toggleStatusStripCollapsed,
} from "../resources/statusStripPref";

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
/** Mount equipped: local walk multiplier (Pixi-only). */
const MOUNT_WALK_SPEED_MULT = 2;
/** Cap one tick so a long hitch cannot teleport past collision samples. */
const WALK_DT_CLAMP_MS = 50;
/** Map-corner status strip (修/拍/擂/斗/练/娱) — flush under exp (local y=0). */
const STATUS_BADGE_SIZE = 17;
/** Slot step — slightly tighter than size+3 so badges sit closer. */
const STATUS_BADGE_PITCH = 18;
const STATUS_BADGE_X0 = 3;
/** Thin vertical collapse rail — same idiom as QuestTrackerOverlay. */
const STATUS_TOGGLE_W = 8;
const STATUS_TOGGLE_GAP = 2;
/** Strip bar padding; badge Y = pad so bar top sits at y=0 (no seam under exp). */
const STATUS_STRIP_PAD_X = 3;
const STATUS_STRIP_PAD_Y = Math.max(
  0,
  Math.floor((MAP_STATUS_STRIP_H - STATUS_BADGE_SIZE) / 2),
);
const STATUS_BADGE_Y = STATUS_STRIP_PAD_Y;
const STATUS_STRIP_H = STATUS_BADGE_SIZE + STATUS_STRIP_PAD_Y * 2;
/** Above world nameplates (z=100_000) by one band. */
const STATUS_STRIP_Z = 100_001;

/**
 * World map camera inside the CBE LCD map band (below HUD, above softkeys).
 */
export class SceneView {
  readonly root = new Container();
  readonly world = new Container();
  /** Above map/actors inside world (shares camera); Songti DOM still map-band clipped. */
  readonly nameplates = new Container();
  readonly actors: ActorLayer;
  private mapSprite: Sprite | null = null;
  /**
   * CPU copy of the town map. After webglcontextrestored, canvas/Buffer
   * source.update() often leaves a black world; remount from this buffer.
   */
  private mapPixels: {
    width: number;
    height: number;
    rgba: Uint8ClampedArray;
  } | null = null;
  private decorSprites: Sprite[] = [];
  /** Shared stand textures keyed by actorResource (many trees → one GPU upload). */
  private decorTexByActor = new Map<string, Texture>();
  private decorPaintGen = 0;
  private player: Container;
  private playerPlate: Container;
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
  /** Top-left「翅」wing picker badge (after 娱). */
  onChiTap: (() => void) | null = null;
  /** Top-left「骑」mount picker badge (after 翅). */
  onQiTap: (() => void) | null = null;
  /** Top-left「称」title list badge (after 骑). */
  onChenTap: (() => void) | null = null;
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
  /**
   * Clip rect for the map band. Must be a Sprite (not Graphics): after
   * webglcontextrestored, Graphics masks stay empty on WebView even when
   * replaceGraphics runs — HUD outside clip still paints (正椁室黑屏).
   */
  private clipMask: Sprite;
  private clip: Container;
  private lastTick = 0;
  private movePixelAcc = 0;
  /** Overhead designation GIF or Pixi arena text title. */
  private titleBadge = new Container();
  private titleSpr: Sprite | null = null;
  private titleTextLabel: BitmapLabel | null = null;
  /** 「门派-xxx」above title; hidden when empty / 无帮派. */
  private guildLabel = new BitmapLabel();
  private lastGuildLine = "";
  private nameLabel = new BitmapLabel();
  private lastName = "";
  private lastRedName = false;
  /** Raw args for setPlayerNameplate remount after GL restore. */
  private lastNameplateTitle = "";
  private lastNameplateGif: string | null | undefined = undefined;
  private lastTitleGif: string | null = null;
  private lastTitleText = "";
  /** One rainbow gem per worn +16 piece (local player). */
  private plus16Sparkles = new Plus16Sparkles();
  /** Optional chibang wing atlas behind/on the body sprite. */
  private wingOverlay = new WingOverlay();
  /** Optional zuoqi mount atlas under the body sprite. */
  private mountOverlay = new MountOverlay();
  /** Optional chenghao animated title above the nameplate. */
  private chenghaoOverlay = new ChenghaoOverlay();
  /** True after battle enter dropped local map cosmetic GPU until resume. */
  private mapCosmeticGpuSuspended = false;
  private xiuBadge = new Container();
  private paiBadge = new Container();
  private leiBadge = new Container();
  /** Hangup / auto-fight 「斗」— vip_effect.gif, after 修/拍/擂. */
  private douBadge = new Container();
  /** Training buff 「练」— training.gif, after 斗. */
  private lianBadge = new Container();
  /** Entertainment 「娱」— after 斗/练 when shown, else right after 擂. */
  private yuBadge = new Container();
  /** Wing preview 「翅」— after 娱. */
  private chiBadge = new Container();
  /** Mount preview 「骑」— after 翅. */
  private qiBadge = new Container();
  /** Title list 「称」— after 骑. */
  private chenBadge = new Container();
  /** Collapsible host for 修…称 (toggle sits outside). */
  private statusStripHost = new Container();
  /** Shared bar behind [收/展] + expanded badges. */
  private statusStripBg = new Graphics();
  /** Left thin collapse rail for the map-corner status strip. */
  private statusToggleBadge = new Container();
  private statusStripCollapsed = isStatusStripCollapsed();
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
    this.clip.sortableChildren = true;
    this.clipMask = createSolidMaskSprite(this.viewW, this.viewH);
    this.clip.mask = this.clipMask;
    this.clip.addChild(this.clipMask);
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
    this.world.zIndex = 0;

    // Same transform as map/actors (camera on world). High zIndex = draw above
    // decor bodies but still under shell HUD (view.root < hud).
    this.nameplates.sortableChildren = true;
    this.nameplates.eventMode = "none";
    this.nameplates.zIndex = 100_000;
    this.world.addChild(this.nameplates);

    // Markers share world with player / decor for feet-Y depth sort.
    this.actors = new ActorLayer(this.world, this.nameplates);

    this.player = new Container();
    this.player.zIndex = 0;
    this.player.sortableChildren = true;
    this.player.eventMode = "none";
    this.playerPlate = new Container();
    this.playerPlate.eventMode = "none";
    this.playerPlate.sortableChildren = true;
    this.nameplates.addChild(this.playerPlate);
    this.playerFallback = new Graphics()
      .circle(0, -4, 5)
      .fill({ color: 0xffe08a })
      .stroke({ width: 1, color: 0x5a4010 });
    this.titleBadge.zIndex = 5;
    this.guildLabel.zIndex = 6;
    this.guildLabel.visible = false;
    this.nameLabel.zIndex = 4;
    this.nameLabel.pivot.set(0, 0);
    /* Depth: sparkles 0 → mount 0.5 → wing-behind 1 → body 2 → wing-back 3 */
    this.plus16Sparkles.root.zIndex = 0;
    this.mountOverlay.root.zIndex = 0.5;
    this.wingOverlay.root.zIndex = 1;
    this.playerFallback.zIndex = 0;
    this.player.addChild(
      this.plus16Sparkles.root,
      this.mountOverlay.root,
      this.wingOverlay.root,
      this.playerFallback,
    );
    this.playerPlate.addChild(this.titleBadge, this.nameLabel, this.guildLabel);
    // Portals join world feet-Y sort with player/decor (not a flat z=10000 layer).
    this.world.addChild(this.player);
    this.root.addChild(this.clip);
    this.clip.zIndex = 0;

    // Status strip on map top-left: [收/展] → 修 → 拍 → 擂 → (斗) → (练) → 娱 → 翅 → 骑 → 称.
    // Above nameplates (world z=100_000); bar top flush with WORLD_TOP_H / exp bottom.
    this.statusStripBg.eventMode = "none";
    this.statusStripBg.zIndex = STATUS_STRIP_Z;
    this.statusStripHost.eventMode = "passive";
    this.statusStripHost.zIndex = STATUS_STRIP_Z + 1;
    this.statusToggleBadge.zIndex = STATUS_STRIP_Z + 2;
    this.root.sortableChildren = true;
    this.root.addChild(this.statusStripBg);
    this.root.addChild(this.statusStripHost);

    this.mountStatusToggleSlot();
    this.statusToggleBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.statusStripCollapsed = toggleStatusStripCollapsed();
      this.layoutStatusBadges();
    });
    this.root.addChild(this.statusToggleBadge);
    this.mountStatusToggleArt();

    this.mountStatusBadgeSlot(this.xiuBadge, 0);
    this.xiuBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onXiuTap?.();
    });
    this.statusStripHost.addChild(this.xiuBadge);
    void this.mountXiuArt();

    this.mountStatusBadgeSlot(this.paiBadge, 1);
    this.paiBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onPaiTap?.();
    });
    this.statusStripHost.addChild(this.paiBadge);
    void this.mountPaiArt();

    this.mountStatusBadgeSlot(this.leiBadge, 2);
    this.leiBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onLeiTap?.();
    });
    this.statusStripHost.addChild(this.leiBadge);
    void this.mountLeiArt();

    this.mountStatusBadgeSlot(this.douBadge, 3);
    this.douBadge.visible = false;
    this.douBadge.eventMode = "none";
    this.statusStripHost.addChild(this.douBadge);
    void this.mountGifBadge(this.douBadge, "vip_effect.gif");

    this.mountStatusBadgeSlot(this.lianBadge, 4);
    this.lianBadge.visible = false;
    this.lianBadge.eventMode = "none";
    this.statusStripHost.addChild(this.lianBadge);
    void this.mountGifBadge(this.lianBadge, "training.gif");

    this.mountStatusBadgeSlot(this.yuBadge, 3);
    this.yuBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onYuTap?.();
    });
    this.statusStripHost.addChild(this.yuBadge);
    void this.mountYuArt();

    this.mountStatusBadgeSlot(this.chiBadge, 4);
    this.chiBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onChiTap?.();
    });
    this.statusStripHost.addChild(this.chiBadge);
    void this.mountChiArt();

    this.mountStatusBadgeSlot(this.qiBadge, 5);
    this.qiBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onQiTap?.();
    });
    this.statusStripHost.addChild(this.qiBadge);
    void this.mountQiArt();

    this.mountStatusBadgeSlot(this.chenBadge, 6);
    this.chenBadge.on("pointertap", (e) => {
      e.stopPropagation();
      this.onChenTap?.();
    });
    this.statusStripHost.addChild(this.chenBadge);
    void this.mountChenArt();
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
    this.mapPixels = null;
    this.mapIdentity = "";
    this.actors.clear();
    this.actors.setMapCollision(null);
    this.mapPreview = null;
    this.portals = [];
    this.decorations = [];
    this.clearDecorSprites();
  }

  /** Drop decor sprites without destroying shared textures, then free the cache. */
  private clearDecorSprites(): void {
    for (const spr of this.decorSprites) {
      this.world.removeChild(spr);
      spr.destroy({ texture: false });
    }
    this.decorSprites = [];
    for (const tex of this.decorTexByActor.values()) {
      try {
        tex.destroy(true);
      } catch {
        /* ignore */
      }
    }
    this.decorTexByActor.clear();
  }

  /** Upload map pixels to a fresh BufferImageSource sprite (load + GPU resume). */
  private mountMapSprite(
    width: number,
    height: number,
    rgba: Uint8ClampedArray,
  ): void {
    const prev = this.mapSprite;
    const texture = rgbaToTexture(width, height, rgba);
    const spr = new Sprite(texture);
    spr.zIndex = 0;
    spr.eventMode = "none";
    spr.visible = true;
    this.mapSprite = spr;
    if (prev) {
      const idx = Math.max(0, this.world.getChildIndex(prev));
      this.world.removeChild(prev);
      prev.destroy({ texture: true });
      this.world.addChildAt(spr, Math.min(idx, this.world.children.length));
    } else {
      this.world.addChildAt(spr, 0);
    }
  }

  /** True when the painted map shell is already this server scene key. */
  showsSceneFile(scene: string): boolean {
    if (!this.mapIdentity || !scene) return false;
    const leaf = this.mapIdentity.split("|")[0] || "";
    const norms = (s: string) =>
      sceneAssetCandidates(s).map((c) => c.replace(/\.sce$/i, "").toLowerCase());
    return norms(leaf).some((x) => norms(scene).includes(x));
  }

  async showScene(scene: LoadedScene, spawnX?: number, spawnY?: number): Promise<void> {
    // Town maps share 400×400 dims — must key on scene/map leaf, not size alone.
    const identity = `${scene.sceneFile}|${scene.mapFile}|${scene.width}x${scene.height}`;
    const sameMap = !!this.mapSprite && this.mapIdentity === identity;
    if (this.mapSprite && !sameMap) {
      this.world.removeChild(this.mapSprite);
      this.mapSprite.destroy({ texture: true });
      this.mapSprite = null;
      this.mapPixels = null;
    }
    if (!sameMap) this.actors.clear();

    if (!this.mapSprite) {
      const expected = scene.width * scene.height * 4;
      const rgba = new Uint8ClampedArray(expected);
      rgba.set(
        scene.rgba.length >= expected
          ? scene.rgba.subarray(0, expected)
          : scene.rgba,
      );
      this.mapPixels = {
        width: scene.width,
        height: scene.height,
        rgba,
      };
      this.mountMapSprite(scene.width, scene.height, rgba);
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
    this.syncPlayerPlate();
    this.applyCamera();
    if (!sameMap) {
      this.paintDecorations();
      this.paintPortals();
      this.actors.setSceneMonsters(
        (scene.combatSpawns ?? []).map((s, i) => {
          const plate = formatCombatSpawnNameplate({
            scene: scene.sceneFile,
            x: s.x,
            y: s.y,
            wire: s.sceActorId,
            sceName: s.name,
            battleActor: s.battleActorResource,
          });
          return {
            actorId: 0x70000000 + i,
            x: s.x,
            y: s.y,
            name: plate.name,
            actorResource: s.mapActorResource,
            battleActorResource: s.battleActorResource,
            scriptName: `sce_combat_${plate.realId}`,
            battleActorId: plate.realId,
          };
        }),
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
    }
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
    this.clearDecorSprites();
    void this.mountDecorations(gen);
  }

  /** Stamp SCE decor on world with feet-Y zIndex (interleave with player). */
  private async mountDecorations(gen: number): Promise<void> {
    const list = this.decorations;
    if (!list.length) return;
    const actorCache = new Map<string, LoadedActor>();
    for (const d of list) {
      if (gen !== this.decorPaintGen) return;
      let actor = actorCache.get(d.actorResource);
      if (!actor) {
        try {
          actor = await loadActor(d.actorResource);
          actorCache.set(d.actorResource, actor);
        } catch (e) {
          console.warn("[scene] decor actor", d.actorResource, e);
          continue;
        }
      }
      if (gen !== this.decorPaintGen) return;
      let tex = this.decorTexByActor.get(d.actorResource);
      if (!tex || tex.destroyed) {
        const composed = composeMapActor(actor, 3, false, 0);
        if (!composed) continue;
        tex = rgbaToTexture(composed.width, composed.height, composed.rgba);
        this.decorTexByActor.set(d.actorResource, tex);
      }
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
          this.lastNameplateTitle,
          this.lastNameplateGif,
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

  /**
   * Local map wing from `JHOnlineData/chibang/sfx_<id>_0`.
   * Pass 0 to clear. Persists to localStorage when `persist` is true.
   */
  async setPlayerWing(wingId: number, persist = true): Promise<void> {
    const id = wingId | 0;
    const hadWing = !!this.wingOverlay.currentWingId;
    await this.wingOverlay.setWingId(id);
    if (persist) writeWingIdPreference(id);
    if (this.playerSprite) {
      this.wingOverlay.setBodyDisplayHeight(this.playerSprite.height);
    }
    this.wingOverlay.setFacing(this.playerDir);
    this.syncWingDepth();
    /* Landing from flight: snap out of solid if wings were removed. */
    if (hadWing && !id) {
      this.ensurePlayerWalkable("unequip-wing");
      this.player.position.set(this.playerX, this.playerY);
      this.player.zIndex = this.playerY | 0;
      this.syncPlayerPlate();
      this.applyCamera();
    }
  }

  getPlayerWingId(): number {
    return this.wingOverlay.currentWingId;
  }

  /**
   * Local map mount from `JHOnlineData/zuoqi/sfx_<id>_0`.
   * Pass 0 to clear. Persists to localStorage when `persist` is true.
   */
  async setPlayerMount(mountId: number, persist = true): Promise<void> {
    const id = mountId | 0;
    await this.mountOverlay.setMountId(id);
    if (persist) writeMountIdPreference(id);
    if (this.playerSprite) {
      this.mountOverlay.setBodyDisplayHeight(this.playerSprite.height);
      this.applyMountBodyLift();
    }
    this.mountOverlay.setFacing(
      this.playerDir,
      this.playerDir === 2 || this.playerDir === 4,
    );
    if (this.playerActor) this.applyPlayerFrame(true);
    else this.layoutNameplate();
  }

  getPlayerMountId(): number {
    return this.mountOverlay.currentMountId;
  }

  /** Battle / VRAM: unload local wing/mount/chenghao atlases; keep equip ids. */
  releaseMapCosmeticGpu(): void {
    if (this.mapCosmeticGpuSuspended) return;
    this.wingOverlay.releaseGpu();
    this.mountOverlay.releaseGpu();
    this.chenghaoOverlay.releaseGpu();
    this.applyMountBodyLift();
    this.mapCosmeticGpuSuspended = true;
  }

  /** Reload map cosmetics after battle if they were suspended. */
  async resumeMapCosmeticGpu(): Promise<boolean> {
    if (!this.mapCosmeticGpuSuspended) return false;
    this.mapCosmeticGpuSuspended = false;
    const wing = this.wingOverlay.currentWingId;
    const mount = this.mountOverlay.currentMountId;
    if (wing) await this.setPlayerWing(wing, false);
    if (mount) await this.setPlayerMount(mount, false);
    if (this.lastName) {
      await this.setPlayerNameplate(
        this.lastName,
        this.lastNameplateTitle,
        this.lastNameplateGif,
        this.lastRedName,
      );
    }
    return true;
  }

  private applyMountBodyLift(): void {
    if (!this.playerSprite) return;
    /* Atlas off: keep feet on ground even when equip id is set. */
    this.playerSprite.y =
      this.mountOverlay.root.visible && this.mountOverlay.currentMountId
        ? -MOUNT_BODY_LIFT
        : 0;
  }

  setPlayerPos(x: number, y: number): void {
    this.playerX = x;
    this.playerY = y;
    this.clampPlayer();
    // Server/login can persist feet in solid or edge-fringe pockets.
    this.ensurePlayerWalkable("setPlayerPos", x, y);
    this.player.position.set(this.playerX, this.playerY);
    this.player.zIndex = this.playerY | 0;
    this.syncPlayerPlate();
    this.applyCamera();
  }

  /**
   * CBE nameplate: designation GIF / chenghao atlas / Pixi arena text title
   * above yellow role name.
   */
  async setPlayerNameplate(
    name: string,
    titleLabel = "",
    titleGif: string | null | undefined = undefined,
    redName = false,
  ): Promise<void> {
    this.lastName = name.slice(0, 8);
    this.lastRedName = redName;
    this.lastNameplateTitle = titleLabel;
    this.lastNameplateGif = titleGif;
    const chenghaoId =
      parseChenghaoId(titleGif) || parseChenghaoId(titleLabel) || 0;
    const gif = chenghaoId
      ? null
      : resolveTitleGif(titleGif) || resolveTitleGif(titleLabel) || null;
    this.lastTitleGif = gif;
    const textTitle =
      !chenghaoId &&
      !gif &&
      (isArenaTextTitle(titleLabel) || isArenaTextTitle(titleGif))
        ? (isArenaTextTitle(titleLabel) ? titleLabel : titleGif || "").trim()
        : "";
    this.lastTitleText = textTitle;

    await this.nameLabel.setText(this.lastName, redName ? NAME_RED : NAME_YELLOW, {
      smooth: false,
      outline: true,
      outlineThin: true,
    });
    this.nameLabel.scale.set(NAMEPLATE_SCALE);
    this.layoutNameplate();

    // Detach chenghao before wiping badge children.
    if (this.chenghaoOverlay.root.parent === this.titleBadge) {
      this.titleBadge.removeChild(this.chenghaoOverlay.root);
    }
    this.titleBadge.removeChildren().forEach((c) => c.destroy());
    this.titleSpr = null;
    this.titleTextLabel = null;

    if (chenghaoId) {
      await this.chenghaoOverlay.setTitleId(chenghaoId);
      if (this.chenghaoOverlay.root.visible) {
        this.titleBadge.addChild(this.chenghaoOverlay.root);
        this.titleBadge.visible = true;
        this.layoutNameplate();
        return;
      }
      /* Atlas disabled: fall back to text name so 称号仍可见. */
      this.chenghaoOverlay.clear();
      const lab = new BitmapLabel();
      await lab.setText(chenghaoLabel(chenghaoId).slice(0, 8), CBE.inkChat, {
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
      return;
    }
    this.chenghaoOverlay.clear();

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

  private syncPlayerPlate(): void {
    this.playerPlate.position.set(this.player.x, this.player.y);
    this.playerPlate.zIndex = this.player.zIndex;
    this.playerPlate.visible = this.player.visible;
  }

  private layoutNameplate(): void {
    this.syncPlayerPlate();
    const nameH = this.nameLabel.height || 16;
    const bodyH = this.playerSprite ? this.playerSprite.height + 2 : 20;
    const mountExtra = this.mountOverlay.currentMountId
      ? MOUNT_BODY_LIFT + MOUNT_NAMEPLATE_EXTRA
      : 0;
    const lift = bodyH + mountExtra;
    this.nameLabel.position.set(-Math.floor(this.nameLabel.width / 2), -lift - nameH);
    let topY = this.nameLabel.y;
    if (this.titleBadge.visible) {
      this.titleBadge.position.set(0, this.nameLabel.y - 2);
      if (this.titleTextLabel) {
        this.titleTextLabel.position.set(0, 0);
      }
      const titleH =
        this.chenghaoOverlay.root.parent === this.titleBadge
          ? this.chenghaoOverlay.root.height || 12
          : this.titleSpr?.height || this.titleTextLabel?.height || 12;
      topY = this.titleBadge.y - titleH;
    }
    if (this.guildLabel.visible) {
      const guildH = this.guildLabel.height || 12;
      this.guildLabel.position.set(
        -Math.floor(this.guildLabel.width / 2),
        topY - 2 - guildH,
      );
    }
  }

  /**
   * Show「门派-xxx」above the designation/title stack. Empty / 无帮派 hides it.
   */
  async setPlayerGuild(guildName: string): Promise<void> {
    const raw = (guildName || "").replace(/\0+$/g, "").trim();
    let line = "";
    if (raw.startsWith("门派-")) line = raw.slice(0, 12);
    else if (raw && raw !== "无" && raw !== "无帮派") line = `门派-${raw.slice(0, 8)}`;
    if (!line) {
      this.lastGuildLine = "";
      this.guildLabel.visible = false;
      this.layoutNameplate();
      return;
    }
    if (line === this.lastGuildLine && this.guildLabel.visible) {
      this.layoutNameplate();
      return;
    }
    this.lastGuildLine = line;
    await this.guildLabel.setText(line, 0x88eeff, {
      smooth: false,
      outline: true,
      outlineThin: true,
    });
    this.guildLabel.scale.set(NAMEPLATE_SCALE);
    this.guildLabel.visible = true;
    this.layoutNameplate();
  }

  private applyPlayerFrame(force = false): void {
    if (!this.playerActor) return;
    // Mounted: keep stand pose (no walk-cycle bob while the mount animates).
    const mounted = !!this.mountOverlay.currentMountId;
    const moving = mounted ? false : this.playerMoving;
    const phase = mounted ? 0 : this.playerPhase;
    const composed = composeMapActor(
      this.playerActor,
      this.playerDir,
      moving,
      phase,
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
      moving,
      phase,
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
      this.playerSprite.zIndex = 2;
      this.player.addChild(this.playerSprite);
    } else {
      this.playerSprite.texture = tex;
      this.playerSprite.zIndex = 2;
    }
    this.playerSprite.scale.set(
      actorSpriteScale(composed.artScale, !!composed.flipX).x,
      actorSpriteScale(composed.artScale, !!composed.flipX).y,
    );
    this.wingOverlay.setBodyDisplayHeight(this.playerSprite.height);
    this.wingOverlay.setFacing(this.playerDir);
    this.mountOverlay.setBodyDisplayHeight(this.playerSprite.height);
    // Do not commitSide here — playerDir jitters on click-paths; move loop owns flip.
    this.mountOverlay.setFacing(this.playerDir, false);
    this.applyMountBodyLift();
    this.syncWingDepth();
    this.playerFallback.visible = false;
    this.layoutNameplate();
  }

  /**
   * Back-facing (dir=1): wing must paint above the body so it sits on the
   * visible back. Front/side: wing stays under the body.
   */
  private syncWingDepth(): void {
    const facingBack = this.playerDir === 1;
    this.mountOverlay.root.zIndex = 0.5;
    this.wingOverlay.root.zIndex = facingBack ? 3 : 1;
    if (this.playerSprite) this.playerSprite.zIndex = 2;
    this.nameLabel.zIndex = 4;
    this.titleBadge.zIndex = 5;
    this.player.sortChildren();
    this.playerPlate.sortChildren();
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

  private async mountChiArt(): Promise<void> {
    await this.mountTextStatusBadge(this.chiBadge, "翅", 0x201028, 0xe0a8e8);
  }

  private async mountQiArt(): Promise<void> {
    await this.mountTextStatusBadge(this.qiBadge, "骑", 0x281810, 0xe8c8a0);
  }

  private async mountChenArt(): Promise<void> {
    await this.mountTextStatusBadge(this.chenBadge, "称", 0x102028, 0xa8c8e8);
  }

  /**
   * After font switch: remount text badges (anchor centering) and re-layout
   * nameplates now that BitmapLabel.width reflects the UC3 sprite again.
   */
  remountAfterFontChange(): void {
    void this.mountXiuArt();
    void this.mountPaiArt();
    void this.mountLeiArt();
    void this.mountYuArt();
    void this.mountChiArt();
    void this.mountQiArt();
    void this.mountChenArt();
    this.layoutStatusBadges();
    this.layoutNameplate();
    this.actors.relayoutAllNameplates();
  }

  /** Flash「拍」when auction claims / refunds are pending. */
  setPaiBlink(on: boolean): void {
    this.paiBlink = on;
    if (!on) {
      this.paiBadge.alpha = 1;
      this.paiBlinkPhase = 0;
    }
  }

  /**
   * 「拍」tile: hide when admin closes auction (42/1 enabled=0), unless
   * pending claims / blink still need an entry for claim/cancel.
   */
  setPaiVisible(on: boolean): void {
    this.paiBadge.visible = !!on;
    if (!on) {
      this.paiBlink = false;
      this.paiBadge.alpha = 1;
      this.paiBlinkPhase = 0;
    }
    this.layoutStatusBadges();
  }

  /** 「翅」tile: admin client-ui wing_status_enabled. */
  setChiVisible(on: boolean): void {
    this.chiBadge.visible = !!on;
    this.layoutStatusBadges();
  }

  /** 「骑」tile: admin client-ui mount_status_enabled. */
  setQiVisible(on: boolean): void {
    this.qiBadge.visible = !!on;
    this.layoutStatusBadges();
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
   * Compact strip: thin [收/展] rail fixed left; when expanded, 修/[拍]/擂 + optional 斗/练
   * then 娱 → 翅 → 骑 → 称. Collapsed: only the rail remains on the left.
   */
  private layoutStatusBadges(): void {
    this.mountStatusToggleSlot();
    this.mountStatusToggleArt();

    this.statusStripHost.visible = !this.statusStripCollapsed;
    if (this.statusStripCollapsed) {
      this.paintStatusStripBg(0);
      return;
    }

    // Badges start just right of the thin collapse rail.
    this.statusStripHost.position.set(STATUS_TOGGLE_W + STATUS_TOGGLE_GAP, 0);

    let next = 0;
    this.mountStatusBadgeSlot(this.xiuBadge, next++);
    if (this.paiBadge.visible) {
      this.mountStatusBadgeSlot(this.paiBadge, next++);
    }
    this.mountStatusBadgeSlot(this.leiBadge, next++);
    if (this.douBadge.visible) {
      this.mountStatusBadgeSlot(this.douBadge, next++);
    }
    if (this.lianBadge.visible) {
      this.mountStatusBadgeSlot(this.lianBadge, next++);
    }
    this.mountStatusBadgeSlot(this.yuBadge, next++);
    if (this.chiBadge.visible) {
      this.mountStatusBadgeSlot(this.chiBadge, next++);
    }
    if (this.qiBadge.visible) {
      this.mountStatusBadgeSlot(this.qiBadge, next++);
    }
    this.mountStatusBadgeSlot(this.chenBadge, next++);
    this.paintStatusStripBg(next);
  }

  /** Dark teal bar under the collapse rail (+ expanded badges). */
  private paintStatusStripBg(badgeSlots: number): void {
    const n = Math.max(0, badgeSlots | 0);
    const x = STATUS_BADGE_X0 - STATUS_STRIP_PAD_X;
    const y = STATUS_BADGE_Y - STATUS_STRIP_PAD_Y;
    const badgesW =
      n <= 0
        ? 0
        : STATUS_BADGE_SIZE + (n - 1) * STATUS_BADGE_PITCH;
    const innerW =
      n <= 0
        ? STATUS_TOGGLE_W
        : STATUS_TOGGLE_W + STATUS_TOGGLE_GAP + badgesW;
    const w = innerW + STATUS_STRIP_PAD_X * 2;
    const h = STATUS_STRIP_H;
    this.statusStripBg
      .clear()
      .roundRect(x, y, w, h, 2)
      .fill({ color: CBE.bgDeep, alpha: 0.82 })
      .stroke({ width: 1, color: CBE.borderDim, alpha: 0.95 });
  }

  private mountStatusToggleSlot(): void {
    // Full strip height, flush with bar top (local y=0 under exp).
    this.statusToggleBadge.position.set(
      STATUS_BADGE_X0,
      STATUS_BADGE_Y - STATUS_STRIP_PAD_Y,
    );
    this.statusToggleBadge.zIndex = STATUS_STRIP_Z + 2;
    this.statusToggleBadge.eventMode = "static";
    this.statusToggleBadge.cursor = "pointer";
    this.statusToggleBadge.hitArea = {
      contains: (px: number, py: number) =>
        px >= 0 && py >= 0 && px <= STATUS_TOGGLE_W && py <= STATUS_STRIP_H,
    };
  }

  private mountStatusToggleArt(): void {
    this.statusToggleBadge.removeChildren().forEach((c) => {
      c.destroy({ children: true });
    });
    const pointRight = this.statusStripCollapsed;
    const fill = pointRight ? 0x142414 : 0x241414;
    const border = pointRight ? 0xb0e0a8 : 0xe0c0a8;
    const ink = pointRight ? 0xc8f0c0 : 0xf0d8c0;
    const plate = new Graphics()
      .roundRect(0, 0, STATUS_TOGGLE_W, STATUS_STRIP_H, 2)
      .fill({ color: fill, alpha: 1 })
      .stroke({ width: 1, color: border, alpha: 0.95 });
    this.statusToggleBadge.addChild(plate);

    const g = new Graphics();
    const cx = STATUS_TOGGLE_W / 2;
    const cy = STATUS_STRIP_H / 2;
    if (pointRight) {
      g.moveTo(cx - 1.2, cy - 3.5)
        .lineTo(cx + 1.4, cy)
        .lineTo(cx - 1.2, cy + 3.5);
    } else {
      g.moveTo(cx + 1.2, cy - 3.5)
        .lineTo(cx - 1.4, cy)
        .lineTo(cx + 1.2, cy + 3.5);
    }
    g.stroke({ width: 1.25, color: ink, join: "miter", cap: "square" });
    this.statusToggleBadge.addChild(g);
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
    // Anchor at slot center so rebake / font switch keeps centering even if
    // Container.width briefly lags (sprite pivot tracks texture size).
    label.anchor.set(0.5, 0.5);
    label.position.set(
      Math.floor(STATUS_BADGE_SIZE / 2),
      Math.floor(STATUS_BADGE_SIZE / 2),
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
    /* Winged: ignore terrain; do not snap feet out of solid cells. */
    if (this.wingOverlay.currentWingId) return;
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
    const raw = this.lastTick ? now - this.lastTick : 16;
    this.lastTick = now;
    // One clamp for move + flaps + cosmetics (90/120Hz must not race cadence).
    const dt = Math.min(Math.max(raw, 0), WALK_DT_CLAMP_MS);
    this.actors.setViewer(this.playerX, this.playerY);
    this.actors.tick(dt);
    this.plus16Sparkles.tick(dt);
    this.wingOverlay.tick(dt);
    this.mountOverlay.tick(dt);
    this.chenghaoOverlay.tick(dt);
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
      let stepped = false;
      while (this.portalAnimAcc >= 120) {
        this.portalAnimAcc -= 120;
        this.portalPhase = (this.portalPhase + 1) % 256;
        stepped = true;
      }
      if (stepped) this.applyPortalFrames();
    }

    if (!this.inputEnabled) return;
    const dtMove = dt;
    const winged = !!this.wingOverlay.currentWingId;
    const mounted = !!this.mountOverlay.currentMountId;
    const speed =
      WALK_PX_PER_MS * dtMove * (mounted ? MOUNT_WALK_SPEED_MULT : 1);
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
      // Winged: ignore MAP solid / fringe. Still clamp to map bounds below.
      // Grounded: inside solid or fringe pocket (no open step) allow escape.
      // Edge fringe uses CBE exit|entry — mapMoveBlocked.
      if (!winged) {
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
      }
      this.playerX = tx;
      this.playerY = ty;
      this.clampPlayer();
      this.player.position.set(this.playerX, this.playerY);
      this.player.zIndex = this.playerY | 0;
      this.syncPlayerPlate();
      this.applyCamera();

      // Click-path stuck on collision: stop instead of spinning in place.
      if (this.walkTarget && !winged) {
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
      // Mount L/R flip: only when horizontal clearly dominates (see MountOverlay).
      this.mountOverlay.setMoveVector(dx, dy);
      this.mountOverlay.setMoving(true);

      if (mounted) {
        // Rider stays in stand pose; mount sheet provides motion.
        this.playerPhase = 0;
        this.playerAnimAcc = 0;
        if (dirChanged || !this.playerSprite) this.applyPlayerFrame();
      } else {
        this.playerAnimAcc += dt;
        let stepped = false;
        while (this.playerAnimAcc >= 140) {
          this.playerAnimAcc -= 140;
          this.playerPhase = (this.playerPhase + 1) % 256;
          stepped = true;
        }
        if (stepped || dirChanged || !this.playerSprite) this.applyPlayerFrame();
      }

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
        this.mountOverlay.setMoving(false);
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

  /** Observe-only: map / visibility fields for black-screen class tags. */
  forensicMapState(): {
    mapIdentity: string;
    mapW: number;
    mapH: number;
    hasMapSprite: boolean;
    rootVisible: boolean;
    worldVisible: boolean;
    mapSourceValid: boolean;
  } {
    let mapSourceValid = false;
    try {
      const src = this.mapSprite?.texture?.source;
      mapSourceValid = !!(src && (src as { width?: number }).width);
    } catch {
      mapSourceValid = false;
    }
    return {
      mapIdentity: this.mapIdentity,
      mapW: this.mapWidth,
      mapH: this.mapHeight,
      hasMapSprite: !!this.mapSprite,
      rootVisible: this.root.visible,
      worldVisible: this.world.visible,
      mapSourceValid,
    };
  }

  /**
   * Android WebView often drops the WebGL context after long screen-off.
   * Town map + actor poses use BufferImageSource — source.update() alone leaves
   * a black world while HUD/UC3 still paint. Recreate from CPU RGBA like battle.
   *
   * Critical: clip.mask must be a remounted Sprite. Graphics masks stay empty
   * after restore on WebView (replaceGraphics insufficient) → map band clips to
   * nothing while world HUD + 修/拍 UC3 labels outside `clip` still paint.
   */
  nudgeAfterGpuResume(): void {
    try {
      // Drop mask before destroy so StencilMask.reset does not race a live effect.
      this.clip.mask = null;
      this.clipMask = remountSolidMaskSprite(
        this.clipMask,
        this.viewW,
        this.viewH,
      );
      this.clip.mask = this.clipMask;
    } catch (e) {
      console.warn("[scene] clip mask rebuild", e);
      // Last resort: unclip so map can paint (may briefly spill into HUD bands).
      try {
        this.clip.mask = null;
      } catch {
        /* ignore */
      }
    }
    try {
      const prevPtr = this.mapPointer;
      this.mapPointer = replaceGraphics(this.mapPointer);
      this.mapPointer
        .rect(0, 0, this.viewW, this.viewH)
        .fill({ color: 0x000000, alpha: 0.001 });
      this.mapPointer.eventMode = "static";
      this.mapPointer.cursor = "crosshair";
      // replaceGraphics keeps index; rebind tap if the DisplayObject identity changed.
      if (this.mapPointer !== prevPtr) {
        this.mapPointer.on("pointertap", (e) => this.onMapPointerTap(e));
      }
    } catch (e) {
      console.warn("[scene] mapPointer rebuild", e);
    }
    try {
      this.playerFallback = replaceGraphics(this.playerFallback);
      this.playerFallback
        .circle(0, -4, 5)
        .fill({ color: 0xffe08a })
        .stroke({ width: 1, color: 0x5a4010 });
      this.playerFallback.zIndex = 0;
    } catch {
      /* ignore */
    }

    try {
      if (this.mapPixels) {
        this.mountMapSprite(
          this.mapPixels.width,
          this.mapPixels.height,
          this.mapPixels.rgba,
        );
      } else {
        this.mapSprite?.texture?.source?.update?.();
        if (this.mapSprite) this.mapSprite.visible = true;
      }
    } catch (e) {
      console.warn("[scene] map remount", e);
    }

    // Shared pose cache (player / peers / portals) keeps dead GPU textures
    // across restore; clear so applyFrame / applyPlayerFrame rebuild.
    clearPoseTextureCache();
    this.playerPoseKey = "";
    this.portalPoseKey = "";
    this.portalTex = null;

    // Remount body Sprite — texture swap on a post-restore Sprite stays black.
    if (this.playerSprite) {
      try {
        if (this.playerSprite.parent) {
          this.playerSprite.parent.removeChild(this.playerSprite);
        }
        this.playerSprite.destroy({ texture: false });
      } catch {
        /* ignore */
      }
      this.playerSprite = null;
    }

    try {
      this.paintDecorations();
    } catch (e) {
      console.warn("[scene] decor remount", e);
    }
    try {
      this.applyPortalFrames();
    } catch {
      /* ignore */
    }

    nudgePlistAtlasesAfterGpuResume();
    void this.wingOverlay.remountAfterGpuResume();
    void this.mountOverlay.remountAfterGpuResume();

    this.applyPlayerFrame(true);
    const body = this.playerSprite as Sprite | null;
    if (body) body.visible = true;
    if (this.lastName) {
      void this.setPlayerNameplate(
        this.lastName,
        this.lastNameplateTitle,
        this.lastNameplateGif,
        this.lastRedName,
      );
    }
    this.actors.nudgeAfterGpuResume();

    // Status badge plates are Graphics — UC3 glyphs return via refreshAllUc3Labels
    // but plates stay blank, leaving bare「修 拍 擂…」over the black band.
    try {
      this.statusStripBg = replaceGraphics(this.statusStripBg);
      this.mountStatusToggleArt();
      void this.mountXiuArt();
      void this.mountPaiArt();
      void this.mountLeiArt();
      void this.mountYuArt();
      void this.mountChiArt();
      void this.mountQiArt();
      void this.mountChenArt();
      void this.mountGifBadge(this.douBadge, "vip_effect.gif");
      void this.mountGifBadge(this.lianBadge, "training.gif");
      this.layoutStatusBadges();
    } catch (e) {
      console.warn("[scene] status strip remount", e);
    }

    // Do NOT force this.root.visible = true — showPhase owns that. GPU resume
    // on the title/login screen was un-hiding 修/拍/… over Background.gif.
    this.world.visible = true;
    this.clip.visible = true;
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
