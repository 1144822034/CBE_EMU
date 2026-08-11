import { Container, Graphics, Sprite, Texture } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  drawCbePanel,
  drawCbeTitlePill,
} from "./constants";
import { BitmapLabel, measureUiTextWidth } from "./BitmapLabel";
import { tryLoadUiTexture, uiSprite } from "./UiAssets";
import {
  findSceneNode,
  linesForWorld,
  loadWorldMapCatalog,
  nearestNodeInDir,
  neighborId,
  scenesForWorld,
  type MapLine,
  type WMapNode,
  type WorldMapCatalog,
} from "../resources/worldMap";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  setPanelTipSticky,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

type Mode = "world" | "region" | "confirm";

/** Sampled from CBE world/region map shots + map_kuang*.gif. */
const LINE_YELLOW = 0xf0c020;
const LABEL_GOLD = 0xe5a844;
const VALUE_GREEN = 0x00ff42;
const HERE_FILL = 0x40ff40;
const NODE = 17; // map_kuang.gif
const CURSOR = 18; // map_kuang2.gif

/**
 * CBE 世界地图 / 区域地图 / 传送石确认.
 * Layout: full LCD panel; nodes & lines at absolute wMap/sMap 位置X/Y
 * (docs/re/2026-06-25-teleport-stone.md — UI markers, not scene coords).
 * Node art: map_kuang.gif + map_kuang2.gif selection ring.
 *
 * Destination policy (product):
 * - Map UI (this panel): any selected world/region node is a valid teleport
 *   target (costs item 800). Cursor prefers DSH neighbor links, then falls
 *   back to nearest node in that direction so the full graph stays reachable.
 * - Scene `n_telestone` NPC (16/1 exitinfo): restricted destination list only.
 */
export class WorldMapPanel extends Container {
  private box = new Graphics();
  private graph = new Container();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private catalog: WorldMapCatalog | null = null;
  private mode: Mode = "world";
  private worldCursor = 0;
  private regionCursor = 0;
  private regionWorld: WMapNode | null = null;
  private currentScene = "";
  private hereWorldId = 0;
  private hereSceneId = 0;
  private kuang: Texture | null = null;
  private kuangSel: Texture | null = null;
  private artReady = false;
  private toast: PanelTipState = createPanelTipState();
  /** Sticky confirm copy restored after transient tips (e.g. 传送石不够). */
  private confirmTip = "";

  onClose: (() => void) | null = null;
  onSoftkeys: ((labels: { left: string; right: string }) => void) | null = null;
  /** Confirm spend stone → teleport to sMap row. */
  onTeleport: ((args: {
    curTeleportId: number;
    objTeleportId: number;
    exitId: number;
    scene: string;
    alias: string;
  }) => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.addChild(this.box, this.graph);
  }

  async mountArt(): Promise<void> {
    if (this.artReady) return;
    this.artReady = true;
    this.kuang = await tryLoadUiTexture("map_kuang.gif");
    this.kuangSel = await tryLoadUiTexture("map_kuang2.gif");
  }

  async open(scene: string): Promise<void> {
    await this.mountArt();
    this.currentScene = scene;
    if (!this.catalog) {
      try {
        this.catalog = await loadWorldMapCatalog();
      } catch (e) {
        console.warn("[worldmap] load", e);
        return;
      }
    }
    const sn = findSceneNode(this.catalog, scene);
    this.hereSceneId = sn?.id ?? 0;
    this.hereWorldId = sn?.parentWMapId ?? 0;
    const wi = this.catalog.worlds.findIndex((w) => w.id === this.hereWorldId);
    this.worldCursor = wi >= 0 ? wi : 0;
    this.mode = "world";
    this.regionWorld = null;
    this.clearConfirmTip();
    this.visible = true;
    this.redraw();
  }

  hide(): void {
    this.confirmTip = "";
    clearPanelTip(this.toast);
    this.visible = false;
  }

  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (!this.visible) return;
      // Restore sticky confirm only after transient tip expires/dismisses.
      // Must not run on the immediate show redraw — that wiped「传送石不够」.
      if (
        !this.toast.tipText &&
        this.mode === "confirm" &&
        this.confirmTip
      ) {
        setPanelTipSticky(this.toast, this.confirmTip);
      }
      this.redraw();
    }, ms);
  }

  private setConfirmTip(alias: string): void {
    this.confirmTip = `您需要花费1个传送石\n瞬移到${alias}`;
    setPanelTipSticky(this.toast, this.confirmTip, () => {
      if (this.visible) this.redraw();
    });
  }

  private clearConfirmTip(): void {
    this.confirmTip = "";
    clearPanelTip(this.toast);
  }

  move(cbeDir: number): void {
    if (!this.catalog || this.mode === "confirm") return;
    if (this.mode === "world") {
      const cur = this.catalog.worlds[this.worldCursor];
      if (!cur) return;
      const nid = neighborId(cur, cbeDir);
      let ix = nid
        ? this.catalog.worlds.findIndex((w) => w.id === nid)
        : -1;
      if (ix < 0) {
        const geo = nearestNodeInDir(this.catalog.worlds, cur, cbeDir);
        if (geo) ix = this.catalog.worlds.findIndex((w) => w.id === geo.id);
      }
      if (ix >= 0) {
        this.worldCursor = ix;
        this.redraw();
      }
      return;
    }
    if (this.mode === "region" && this.regionWorld) {
      const list = scenesForWorld(this.catalog, this.regionWorld.id);
      const cur = list[this.regionCursor];
      if (!cur) return;
      const nid = neighborId(cur, cbeDir);
      let ix = nid ? list.findIndex((s) => s.id === nid) : -1;
      if (ix < 0) {
        const geo = nearestNodeInDir(list, cur, cbeDir);
        if (geo) ix = list.findIndex((s) => s.id === geo.id);
      }
      if (ix >= 0) {
        this.regionCursor = ix;
        this.redraw();
      }
    }
  }

  confirm(): void {
    if (!this.catalog) return;
    if (this.mode === "world") {
      const w = this.catalog.worlds[this.worldCursor];
      if (!w) return;
      this.regionWorld = w;
      const list = scenesForWorld(this.catalog, w.id);
      const prefer = list.findIndex((s) => s.id === this.hereSceneId);
      this.regionCursor = prefer >= 0 ? prefer : 0;
      this.mode = "region";
      this.redraw();
      return;
    }
    if (this.mode === "region") {
      const w = this.regionWorld;
      const list = w ? scenesForWorld(this.catalog, w.id) : [];
      const scene = list[this.regionCursor];
      const alias = scene?.alias || scene?.scene || "目标";
      this.mode = "confirm";
      this.setConfirmTip(alias);
      this.redraw();
      return;
    }
    if (this.mode === "confirm") {
      const w = this.regionWorld;
      const list = w ? scenesForWorld(this.catalog, w.id) : [];
      const scene = list[this.regionCursor];
      if (!w || !scene) return;
      const curW =
        this.catalog.byWorldId.get(this.hereWorldId) ||
        this.catalog.worlds[this.worldCursor]!;
      this.onTeleport?.({
        curTeleportId: curW.teleportId,
        objTeleportId: w.teleportId,
        exitId: scene.id,
        scene: scene.scene,
        alias: scene.alias || scene.scene,
      });
    }
  }

  cancel(): void {
    if (tryDismissPanelTipOnSoftRight(this.toast)) return;
    if (this.mode === "confirm") {
      this.clearConfirmTip();
      this.mode = "region";
      this.redraw();
      return;
    }
    if (this.mode === "region") {
      this.mode = "world";
      this.regionWorld = null;
      this.redraw();
      return;
    }
    this.hide();
    this.onClose?.();
  }

  private clearDynamic(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    for (const c of this.graph.removeChildren()) c.destroy();
    this.box.clear();
  }

  private redraw(): void {
    this.clearDynamic();
    if (!this.catalog) return;

    // Full-screen panel like CBE bag / attr (covers HUD).
    const x = 2;
    const y = 2;
    const w = LCD_WIDTH - 4;
    const h = LCD_HEIGHT - 4;
    drawCbePanel(this.box, x, y, w, h, CBE.bgDeep);

    const title = this.mode === "world" ? "世界地图" : "区域地图";
    const pillW = 88;
    const pillH = 22;
    const pillY = y + 8;
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, pillY, pillW, pillH);
    this.addLabelCentered(title, LCD_WIDTH / 2, pillY + pillH / 2, CBE.ink);

    // Bottom L/R chrome removed — HTML softkeys carry 确定/返回/瞬移.
    const softY = y + h - 6;

    try {
      if (this.mode === "confirm") {
        this.drawConfirm(x, y, w, softY);
        return;
      }

      const focus =
        this.mode === "world"
          ? this.catalog.worlds[this.worldCursor]
          : scenesForWorld(this.catalog, this.regionWorld!.id)[this.regionCursor];
      const name = focus?.alias || "—";
      const level = focus?.level || "无";
      this.addKv(x + 10, pillY + pillH + 8, "名称:", name);
      this.addKv(x + 10, pillY + pillH + 24, "等级:", level);

      if (this.mode === "world") {
        this.drawGraph(
          this.catalog.worlds.map((n) => ({
            id: n.id,
            x: n.x,
            y: n.y,
            selected: n.id === this.catalog!.worlds[this.worldCursor]?.id,
            here: n.id === this.hereWorldId,
          })),
          this.catalog.worldLines,
        );
        this.onSoftkeys?.({ left: "确定", right: "返回" });
      } else {
        const list = scenesForWorld(this.catalog, this.regionWorld!.id);
        const lines = linesForWorld(this.catalog, this.regionWorld!.id);
        this.drawGraph(
          list.map((n) => ({
            id: n.id,
            x: n.x,
            y: n.y,
            selected: n.id === list[this.regionCursor]?.id,
            here: n.id === this.hereSceneId,
          })),
          lines,
        );
        this.onSoftkeys?.({ left: "瞬移", right: "返回" });
      }
    } finally {
      drawPanelTipOverlay(this, this.toast);
    }
  }

  private drawConfirm(x: number, y: number, _w: number, _softY: number): void {
    if (!this.catalog || !this.regionWorld) return;
    const list = scenesForWorld(this.catalog, this.regionWorld.id);
    const scene = list[this.regionCursor];
    const alias = scene?.alias || "目标";
    this.addKv(x + 10, y + 36, "名称:", alias);
    this.addKv(x + 10, y + 52, "等级:", scene?.level || "无");

    this.drawGraph(
      list.map((n) => ({
        id: n.id,
        x: n.x,
        y: n.y,
        selected: n.id === scene?.id,
        here: n.id === this.hereSceneId,
      })),
      linesForWorld(this.catalog, this.regionWorld.id),
    );

    // Confirm copy is drawn by unified panelTip (sticky) in redraw finally.
    if (!this.confirmTip) this.setConfirmTip(alias);
    this.onSoftkeys?.({ left: "确定", right: "取消" });
  }

  /**
   * Draw lines + nodes at absolute DSH UI pixels.
   * Evidence: 位置X/Y is the top-left of map_kuang; wMapLine/sMapLine
   * endpoints pass through the icon centre (x+8,y+8 for 17×17), so the
   * cursor/node must be placed with that centre — not anchored on (x,y).
   */
  private drawGraph(
    nodes: { id: number; x: number; y: number; selected: boolean; here: boolean }[],
    lines: MapLine[],
  ): void {
    if (!nodes.length) {
      this.addAttr("(无节点)", 16, 80, CBE.inkLabel);
      return;
    }
    const g = new Graphics();
    this.graph.addChild(g);
    const half = Math.floor(NODE / 2);

    for (const l of lines) {
      g.moveTo(l.x1, l.y1)
        .lineTo(l.x2, l.y2)
        .stroke({ width: 1, color: LINE_YELLOW });
    }

    for (const n of nodes) {
      // Icon top-left = DSH (x,y); visual / line centre = (x+half, y+half).
      const cx = n.x + half;
      const cy = n.y + half;
      if (n.here) {
        g.rect(n.x, n.y, NODE, NODE).fill({ color: HERE_FILL });
      } else if (this.kuang) {
        const spr = uiSprite(this.kuang);
        spr.eventMode = "none";
        spr.position.set(n.x, n.y);
        this.graph.addChild(spr);
      } else {
        g.rect(n.x, n.y, NODE, NODE)
          .fill({ color: 0x145040 })
          .stroke({ width: 1, color: 0x30c080 });
        g.moveTo(n.x + 3, n.y + 3)
          .lineTo(n.x + NODE - 3, n.y + NODE - 3)
          .moveTo(n.x + NODE - 3, n.y + 3)
          .lineTo(n.x + 3, n.y + NODE - 3)
          .stroke({ width: 1, color: 0x28a070 });
      }
      if (n.selected) {
        if (this.kuangSel) {
          const spr = uiSprite(this.kuangSel);
          spr.eventMode = "none";
          spr.anchor.set(0.5);
          spr.position.set(cx, cy);
          this.graph.addChild(spr);
        } else {
          g.rect(cx - CURSOR / 2, cy - CURSOR / 2, CURSOR, CURSOR).stroke({
            width: 2,
            color: 0x40c0ff,
          });
          g.poly([cx, cy - 4, cx + 4, cy, cx, cy + 4, cx - 4, cy]).fill({
            color: LINE_YELLOW,
          });
        }
      }
      const nodeId = n.id;
      this.rowHits.push(
        attachRowPointerHit(this, n.x - 4, n.y - 4, NODE + 8, NODE + 8, () => {
          if (this.mode === "confirm") {
            this.confirm();
            return;
          }
          if (this.mode === "world" && this.catalog) {
            const ix = this.catalog.worlds.findIndex((w) => w.id === nodeId);
            if (ix < 0) return;
            this.worldCursor = ix;
            this.redraw();
            this.confirm();
            return;
          }
          if (this.mode === "region" && this.regionWorld && this.catalog) {
            const list = scenesForWorld(this.catalog, this.regionWorld.id);
            const ix = list.findIndex((s) => s.id === nodeId);
            if (ix < 0) return;
            this.regionCursor = ix;
            this.redraw();
            this.confirm();
          }
        }),
      );
    }
  }

  /** 「名称:」gold + value green (CBE world/region header). */
  private addKv(x: number, y: number, key: string, value: string): void {
    const k = new BitmapLabel();
    k.position.set(x, y);
    void k.setText(key, LABEL_GOLD, { outline: false });
    this.addChild(k);
    this.labels.push(k);
    const approx = measureUiTextWidth(key);
    const v = new BitmapLabel();
    v.position.set(x + approx, y);
    void v.setText(value, VALUE_GREEN, {
      outline: false,
      maxWidth: LCD_WIDTH - x - approx - 12,
    });
    this.addChild(v);
    this.labels.push(v);
  }

  private addAttr(text: string, x: number, y: number, color: number): void {
    const t = new BitmapLabel();
    t.position.set(x, y);
    void t.setText(text, color, { outline: false, maxWidth: LCD_WIDTH - 28 });
    this.addChild(t);
    this.labels.push(t);
  }

  private addLabelCentered(text: string, cx: number, cy: number, color: number): void {
    const t = new BitmapLabel();
    t.anchor.set(0.5, 0.5);
    t.position.set(Math.round(cx), Math.round(cy));
    void t.setText(text, color, { outline: false, maxWidth: LCD_WIDTH - 24 });
    this.addChild(t);
    this.labels.push(t);
  }
}
