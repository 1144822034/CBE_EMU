/**
 * DOM text overlay on #phone-wrap — reference-site Songti path.
 *
 * Two tiers above the pixelated canvas:
 * - #lcd-dom-world — map nameplates, clipped to the map band (under HUD chrome)
 * - #lcd-dom-text  — HUD / panel / popup labels (full LCD)
 *
 * Allowlisted popup branches still hide world/HUD DOM that is not inside the
 * cover, so nameplates cannot float above bag/menu art.
 */

import type { Container } from "pixi.js";
import { LCD_HEIGHT, LCD_WIDTH, MAP_VIEW_H, MAP_STATUS_STRIP_H, WORLD_BOTTOM_H, WORLD_TOP_H } from "./constants";

let worldLayer: HTMLDivElement | null = null;
let uiLayer: HTMLDivElement | null = null;
let rafId = 0;
const syncers = new Set<() => void>();

/** Direct children of this root participate in popup stacking (shell.content). */
let stackRoot: Container | null = null;
/** Fullscreen / modal branches that cover the map when visible. */
const coverBranches = new Set<Container>();
/**
 * Soft covers (NPC approach tip / world menu / NPC dialog / trade): hide map
 * nameplates, keep WorldHud Songti (挂机 / 多开芯片). Hard covers still hide
 * all non-cover DOM (bag / system / full-screen panels).
 * Status-strip Songti (修/拍) stays for short dialog; hide it via
 * {@link softCoverOccludeChrome} (tall warehouse) or
 * {@link setLcdDomSoftCoverChromeOccludeBranches} (trade) without a DOM plate.
 * Trade also hides WorldHud location-title Songti via
 * {@link setLcdDomSoftCoverChromeOccludeHosts}.
 */
const softCoverBranches = new Set<Container>();
/** Tall soft-cover dialog: hide 修/拍 chrome Songti; keep 挂机/多开. */
let softCoverOccludeChrome = false;
/**
 * Soft covers that always hide status-strip Songti while they are the top
 * cover (trade). Independent of {@link softCoverOccludeChrome}.
 */
const softCoverChromeOccludeBranches = new Set<Container>();
/**
 * Extra label ancestors hidden with trade chrome-occlude (WorldHud location
 * band). Not the whole HUD — hang / 多开 live on `hud.bottom`.
 */
const softCoverChromeOccludeHosts = new Set<Container>();
/**
 * Map-corner chrome (修拍擂… + 比武计时): same DOM z band, under popups.
 * Direct shell.content children (view.root / arenaHud).
 */
const chromeBranches = new Set<Container>();
/**
 * Visible center toast hosts ({@link registerLcdDomPanelTip}).
 * Tip Songti uses a top CSS z band; overlapping labels stay drawn and are
 * covered by a solid DOM plate ({@link setLcdDomTipOccludeRect}) — not hidden.
 * Only set the plate when preferSystemFont() — UC3 tip glyphs live on the
 * canvas under #lcd-dom-text and would be covered by an opaque DOM plate.
 */
const panelTipHosts = new Set<Container>();
type DomOccludeRect = {
  host: Container;
  x: number;
  y: number;
  w: number;
  h: number;
  /** RGB fill matching canvas panel (default CBE.bgPanelDark). */
  fill: number;
  /** When true (bag discard/extract confirm), also hide HUD Songti (挂机/多开). */
  occludeHud: boolean;
};

/** Tip frame in host-local LCD px — solid DOM cover under tip Songti. */
let tipOcclude: DomOccludeRect | null = null;
/** Opaque plate under tip Songti so behind text is covered, not display:none. */
let tipCoverEl: HTMLDivElement | null = null;
/**
 * Soft-cover dialog over another popup (bag discard/extract confirm): DOM plate
 * over the dialog chrome so lower popup Songti cannot bleed through the plate,
 * while labels outside the plate stay visible.
 */
let hostOcclude: DomOccludeRect | null = null;
let hostCoverEl: HTMLDivElement | null = null;
/** Map nameplate host — labels under this go to #lcd-dom-world. */
let worldTextRoot: Container | null = null;

/** Matches {@link CBE.bgPanelDark} — avoid importing constants (cycle via BitmapLabel). */
const TIP_COVER_FILL_DEFAULT = 0x2a4a42;
/** Matches {@link CBE.bgPanel}. */
const HOST_COVER_FILL_DEFAULT = 0x102520;
/** Above normal popup Songti (~150k); below host-elevated labels + tip band. */
const HOST_COVER_Z = 1_700_000;
/** Labels under {@link hostOcclude}.host sit above the host plate. */
const HOST_LABEL_Z = 1_750_000;
/** Between popup labels (~150k) and tip Songti (1_900_000). */
const TIP_COVER_Z = 1_800_000;

export type LcdDomTier = "world" | "ui";

function ensureLayer(
  id: string,
  existing: HTMLDivElement | null,
): HTMLDivElement {
  if (existing?.isConnected) return existing;
  const wrap = document.getElementById("phone-wrap");
  if (!wrap) {
    throw new Error("missing #phone-wrap for LCD DOM text");
  }
  let el = document.getElementById(id) as HTMLDivElement | null;
  if (!el) {
    el = document.createElement("div");
    el.id = id;
    wrap.appendChild(el);
  }
  return el;
}

export function ensureLcdDomTextLayer(tier: LcdDomTier = "ui"): HTMLDivElement {
  if (tier === "world") {
    worldLayer = ensureLayer("lcd-dom-world", worldLayer);
    syncWorldClipPath();
    return worldLayer;
  }
  uiLayer = ensureLayer("lcd-dom-text", uiLayer);
  return uiLayer;
}

/** Keep world DOM clip aligned if LCD chrome constants ever change. */
function syncWorldClipPath(): void {
  if (!worldLayer) return;
  // Start below the status strip so nameplates cannot cover 修/拍/… chrome.
  const top = ((WORLD_TOP_H + MAP_STATUS_STRIP_H) / LCD_HEIGHT) * 100;
  const bottom = (WORLD_BOTTOM_H / LCD_HEIGHT) * 100;
  worldLayer.style.clipPath = `inset(${top}% 0 ${bottom}% 0)`;
}

/** CSS px per LCD px (#phone-wrap width / 240). */
export function lcdDomCssScale(): number {
  const wrap = document.getElementById("phone-wrap");
  const w =
    wrap?.clientWidth ||
    Number.parseFloat(wrap?.style.width || "") ||
    LCD_WIDTH;
  return Math.max(0.5, w / LCD_WIDTH);
}

export function setLcdDomStackRoot(root: Container): void {
  stackRoot = root;
}

/** Register modal/fullscreen shells (system/menu/dialog/panels/…). */
export function setLcdDomCoverBranches(nodes: readonly Container[]): void {
  coverBranches.clear();
  for (const n of nodes) coverBranches.add(n);
}

/**
 * NPC approach tip / world menu / NPC dialog / trade — occlude world nameplates
 * only, not HUD Songti. Nodes must also be in {@link setLcdDomCoverBranches}.
 */
export function setLcdDomSoftCoverBranches(nodes: readonly Container[]): void {
  softCoverBranches.clear();
  for (const n of nodes) softCoverBranches.add(n);
}

/**
 * Soft covers that hide 修/拍 chrome Songti while open (trade panel over the
 * status strip). Does not hide HUD (挂机/多开). Warehouse tall lists still use
 * {@link setLcdDomSoftCoverOccludeChrome}.
 */
export function setLcdDomSoftCoverChromeOccludeBranches(
  nodes: readonly Container[],
): void {
  softCoverChromeOccludeBranches.clear();
  for (const n of nodes) softCoverChromeOccludeBranches.add(n);
  kickLcdDomSyncLoop();
}

/**
 * Labels under these hosts hide with trade chrome-occlude (scene title bar).
 * Pass `hud.top` so 挂机/多开 on `hud.bottom` stay visible.
 */
export function setLcdDomSoftCoverChromeOccludeHosts(
  nodes: readonly Container[],
): void {
  softCoverChromeOccludeHosts.clear();
  for (const n of nodes) softCoverChromeOccludeHosts.add(n);
  kickLcdDomSyncLoop();
}

/**
 * Tall soft-cover lists (仓库存入/取回): hide status-strip Songti while open.
 * Prefer this over {@link setLcdDomHostOccludeRect} so canvas select bars stay
 * visible. Does not hide HUD (挂机/多开).
 */
export function setLcdDomSoftCoverOccludeChrome(on: boolean): void {
  softCoverOccludeChrome = on;
  kickLcdDomSyncLoop();
}

/**
 * Status-strip / arena timer branches — share one DOM z band under popups.
 */
export function setLcdDomChromeBranches(nodes: readonly Container[]): void {
  chromeBranches.clear();
  for (const n of nodes) chromeBranches.add(n);
}

/** Center toast host — Songti above everything while visible. */
export function registerLcdDomPanelTip(host: Container): void {
  panelTipHosts.add(host);
}

export function unregisterLcdDomPanelTip(host: Container): void {
  panelTipHosts.delete(host);
}

export function lcdDomIsPanelTip(label: Container): boolean {
  let n: Container | null = label;
  while (n) {
    if (panelTipHosts.has(n)) return true;
    n = n.parent;
  }
  return false;
}

/**
 * Tip toast frame (host-local LCD). Places a solid DOM plate under tip Songti
 * so overlapping panel/world text is covered in place (not whole-line hidden).
 * Callers must only use this for the Songti (cn) path — see panelTip.ts.
 */
export function setLcdDomTipOccludeRect(
  host: Container,
  x: number,
  y: number,
  w: number,
  h: number,
  fill: number = TIP_COVER_FILL_DEFAULT,
): void {
  tipOcclude = { host, x, y, w, h, fill, occludeHud: false };
  syncLcdDomTipCover();
  kickLcdDomSyncLoop();
}

export function clearLcdDomTipOccludeRect(): void {
  tipOcclude = null;
  syncLcdDomTipCover();
}

/**
 * Soft-cover popup chrome (host-local LCD). Opaque DOM plate above lower
 * popup Songti; labels under `host` are elevated above the plate.
 * Songti (cn) only — UC3 glyphs are on canvas under the dialog fill.
 * @param opts.occludeHud hide 挂机/多开 Songti (default true for bag confirm).
 */
export function setLcdDomHostOccludeRect(
  host: Container,
  x: number,
  y: number,
  w: number,
  h: number,
  fill: number = HOST_COVER_FILL_DEFAULT,
  opts?: { occludeHud?: boolean },
): void {
  hostOcclude = {
    host,
    x,
    y,
    w,
    h,
    fill,
    occludeHud: opts?.occludeHud !== false,
  };
  syncLcdDomHostCover();
  kickLcdDomSyncLoop();
}

export function clearLcdDomHostOccludeRect(): void {
  hostOcclude = null;
  syncLcdDomHostCover();
}

function labelUnderHost(label: Container, host: Container): boolean {
  let n: Container | null = label;
  while (n) {
    if (n === host) return true;
    n = n.parent;
  }
  return false;
}

function labelUnderAnyHost(label: Container, hosts: Set<Container>): boolean {
  if (!hosts.size) return false;
  for (const host of hosts) {
    if (labelUnderHost(label, host)) return true;
  }
  return false;
}

function ensureCoverEl(
  existing: HTMLDivElement | null,
  className: string,
  z: number,
): HTMLDivElement {
  const layer = ensureLcdDomTextLayer("ui");
  if (existing?.isConnected) return existing;
  const el = document.createElement("div");
  el.className = className;
  el.style.position = "absolute";
  el.style.left = "0";
  el.style.top = "0";
  el.style.margin = "0";
  el.style.padding = "0";
  el.style.pointerEvents = "none";
  el.style.display = "none";
  el.style.zIndex = String(z);
  el.style.borderRadius = "2px";
  layer.appendChild(el);
  return el;
}

function syncCoverEl(
  t: DomOccludeRect | null,
  el: HTMLDivElement | null,
  ensure: () => HTMLDivElement,
  z: number,
): void {
  if (!t || !t.host.visible || !t.host.parent || t.w <= 0 || t.h <= 0) {
    if (el) el.style.display = "none";
    return;
  }
  const node = ensure();
  const gp = t.host.getGlobalPosition();
  const s = lcdDomCssScale();
  node.style.display = "block";
  node.style.left = `${(gp.x + t.x) * s}px`;
  node.style.top = `${(gp.y + t.y) * s}px`;
  node.style.width = `${t.w * s}px`;
  node.style.height = `${t.h * s}px`;
  node.style.backgroundColor = cssHexColor(t.fill);
  node.style.zIndex = String(z);
}

function ensureTipCoverEl(): HTMLDivElement {
  tipCoverEl = ensureCoverEl(tipCoverEl, "lcd-dom-tip-cover", TIP_COVER_Z);
  return tipCoverEl;
}

function ensureHostCoverEl(): HTMLDivElement {
  hostCoverEl = ensureCoverEl(hostCoverEl, "lcd-dom-host-cover", HOST_COVER_Z);
  return hostCoverEl;
}

/** Keep tip cover aligned with the canvas toast frame (CSS px). */
function syncLcdDomTipCover(): void {
  syncCoverEl(tipOcclude, tipCoverEl, ensureTipCoverEl, TIP_COVER_Z);
}

function syncLcdDomHostCover(): void {
  syncCoverEl(hostOcclude, hostCoverEl, ensureHostCoverEl, HOST_COVER_Z);
}

export function lcdDomIsChrome(label: Container): boolean {
  const branch = lcdDomBranchRoot(label);
  return !!branch && chromeBranches.has(branch);
}

/** SceneView nameplate layer — Songti labels here use the clipped world tier. */
export function setLcdDomWorldTextRoot(node: Container | null): void {
  worldTextRoot = node;
}

export function lcdDomTierFor(label: Container): LcdDomTier {
  if (!worldTextRoot) return "ui";
  let n: Container | null = label;
  while (n) {
    if (n === worldTextRoot) return "world";
    n = n.parent;
  }
  return "ui";
}

export function registerLcdDomSync(fn: () => void): void {
  syncers.add(fn);
  kickLcdDomSyncLoop();
}

export function unregisterLcdDomSync(fn: () => void): void {
  syncers.delete(fn);
  if (!syncers.size && rafId) {
    cancelAnimationFrame(rafId);
    rafId = 0;
  }
}

function kickLcdDomSyncLoop(): void {
  if (rafId) return;
  if (!syncers.size && !tipOcclude && !hostOcclude) return;
  const tick = (): void => {
    rafId = 0;
    if (!syncers.size && !tipOcclude && !hostOcclude) return;
    for (const fn of [...syncers]) {
      try {
        fn();
      } catch {
        /* ignore per-label sync errors */
      }
    }
    try {
      syncLcdDomTipCover();
    } catch {
      /* ignore tip cover sync errors */
    }
    try {
      syncLcdDomHostCover();
    } catch {
      /* ignore host cover sync errors */
    }
    if (syncers.size || tipOcclude || hostOcclude) {
      rafId = requestAnimationFrame(tick);
    }
  };
  rafId = requestAnimationFrame(tick);
}

export function cssHexColor(rgb: number): string {
  return `#${(rgb >>> 0).toString(16).padStart(6, "0")}`;
}

function stackKey(content: Container, child: Container): number {
  return (child.zIndex || 0) * 1_000_000 + content.getChildIndex(child);
}

/** Direct child of stackRoot that owns `node`. */
export function lcdDomBranchRoot(node: Container): Container | null {
  if (!stackRoot) return null;
  let n: Container | null = node;
  while (n) {
    if (n.parent === stackRoot) return n;
    n = n.parent;
  }
  return null;
}

/** Topmost visible allowlisted popup branch, or null in open world. */
export function lcdDomTopCover(): Container | null {
  if (!stackRoot) return null;
  let best: Container | null = null;
  let bestKey = Number.NEGATIVE_INFINITY;
  for (const c of coverBranches) {
    if (!c.visible || c.parent !== stackRoot) continue;
    const key = stackKey(stackRoot, c);
    if (key >= bestKey) {
      bestKey = key;
      best = c;
    }
  }
  return best;
}

/**
 * Hide DOM text that belongs to a lower branch while a popup cover is open.
 * Labels inside the cover itself stay visible (and get a high CSS z-index).
 * Soft covers (approach / menu / dialog / trade) only hide world-tier nameplates.
 * Soft-cover + warehouse tall / trade chrome-occlude: hide status Songti
 * without a DOM plate. Bag discard confirm uses host plate and occludeHud so
 * 挂机/多开 cannot float over bag+confirm.
 * Center toasts do not hide overlapping Songti — {@link syncLcdDomTipCover}
 * paints an opaque plate so behind glyphs stay in place under the tip.
 */
export function lcdDomOccludedByPopup(label: Container): boolean {
  // Tip labels are never occluded (stack above covers via css z-index).
  if (lcdDomIsPanelTip(label)) return false;
  const branch = lcdDomBranchRoot(label);
  if (!branch) return false;
  const cover = lcdDomTopCover();
  if (!cover) return false;
  if (cover === branch) return false;
  // Bag overlay confirm plate: hide chrome (+ HUD when occludeHud).
  if (hostOcclude && hostOcclude.host === cover) {
    if (chromeBranches.has(branch)) return true;
    if (hostOcclude.occludeHud && !coverBranches.has(branch)) return true;
    return false;
  }
  if (softCoverBranches.has(cover)) {
    if (lcdDomTierFor(label) === "world") return true;
    if (
      chromeBranches.has(branch) &&
      (softCoverOccludeChrome || softCoverChromeOccludeBranches.has(cover))
    ) {
      return true;
    }
    if (
      softCoverChromeOccludeBranches.has(cover) &&
      labelUnderAnyHost(label, softCoverChromeOccludeHosts)
    ) {
      return true;
    }
    return false;
  }
  return true;
}

/**
 * CSS z-index bands (within each DOM tier layer):
 * world nameplates < map chrome (status+arena) < HUD/ui < popup cover labels
 * < center toast (panel tip).
 * Do not multiply by actor feet-Y (would float nameplates above HUD text).
 */
export function lcdDomCssZIndex(label: Container): number {
  if (lcdDomIsPanelTip(label)) {
    return 1_900_000;
  }
  // Soft-cover dialog plate: elevate this host's Songti above the plate.
  if (hostOcclude && labelUnderHost(label, hostOcclude.host)) {
    return HOST_LABEL_Z;
  }
  const tier = lcdDomTierFor(label);
  const branch = lcdDomBranchRoot(label);
  const popup = branch != null && coverBranches.has(branch);
  const chrome = branch != null && chromeBranches.has(branch);
  let z =
    tier === "world" ? 10 : popup ? 100_000 : chrome ? 800 : 1_000;
  // Local order only — clamp parent zIndex so map Y-sort / arenaHud.z=40
  // cannot push chrome above sibling status or under dialog dim.
  let n: Container | null = label;
  let depth = 0;
  while (n && n !== stackRoot && depth < 8) {
    const raw = n.zIndex || 0;
    const local = chrome
      ? Math.min(8, Math.max(0, raw))
      : Math.min(64, Math.max(0, raw));
    z += (local + 1) * (depth + 1);
    n = n.parent;
    depth++;
  }
  if (branch && stackRoot) {
    // Chrome shares one band — ignore high Pixi zIndex in stackKey.
    const key = chrome
      ? stackRoot.getChildIndex(branch)
      : stackKey(stackRoot, branch);
    z += (key % 5_000) + (popup ? 50_000 : 0);
  }
  return Math.max(1, Math.min(2_000_000, z));
}

export function lcdToDomCss(
  lcdX: number,
  lcdY: number,
): { x: number; y: number; scale: number } {
  const scale = lcdDomCssScale();
  return { x: lcdX * scale, y: lcdY * scale, scale };
}

export function lcdDomLayerSizeCss(): { w: number; h: number; scale: number } {
  const scale = lcdDomCssScale();
  return { w: LCD_WIDTH * scale, h: LCD_HEIGHT * scale, scale };
}

/** Map band usable for world nameplates (below status strip). */
export function lcdDomMapBand(): { top: number; height: number } {
  return {
    top: WORLD_TOP_H + MAP_STATUS_STRIP_H,
    height: MAP_VIEW_H - MAP_STATUS_STRIP_H,
  };
}
