/**
 * wMap / sMap / line sheets for world & regional map UI.
 * Evidence: docs/re/2026-07-18-world-map-read-lag.md; mock_server_scene_task.c sMap/wMap cols.
 */
import { decodeGbk, readU32LE } from "./binary";
import { fetchResourceBytes } from "./loader";
import { sceneAssetCandidates } from "../net/protocol";

export type WMapNode = {
  id: number;
  teleportId: number;
  name: string;
  alias: string;
  x: number;
  y: number;
  up: number;
  right: number;
  down: number;
  left: number;
  /** First sMap row id for this region. */
  childBase: number;
  sceneCount: number;
  lineBase: number;
  lineCount: number;
  level: string;
};

export type SMapNode = {
  id: number;
  scene: string;
  alias: string;
  x: number;
  y: number;
  up: number;
  right: number;
  down: number;
  left: number;
  parentWMapId: number;
  level: string;
};

export type MapLine = {
  id: number;
  parentId: number;
  x1: number;
  y1: number;
  x2: number;
  y2: number;
};

export type WorldMapCatalog = {
  worlds: WMapNode[];
  scenes: SMapNode[];
  worldLines: MapLine[];
  sceneLines: MapLine[];
  byWorldId: Map<number, WMapNode>;
  bySceneId: Map<number, SMapNode>;
  byTeleportId: Map<number, WMapNode>;
};

let cached: WorldMapCatalog | null = null;

function parseRows(raw: Uint8Array): { rows: string[][] } {
  const out: { rows: string[][] } = { rows: [] };
  if (raw.length < 20) return out;
  const cols = readU32LE(raw, 4);
  const rowCount = readU32LE(raw, 8);
  const headerBytes = readU32LE(raw, 12);
  if (cols < 1 || cols > 64 || rowCount > 10000) return out;
  let pos = 16 + headerBytes;
  if (pos > raw.length) return out;
  for (let row = 0; row < rowCount && pos + 4 <= raw.length; row++) {
    const rowLen = readU32LE(raw, pos);
    let rp = pos + 4;
    const end = rp + rowLen;
    if (rowLen === 0 || end > raw.length || end < rp) break;
    const cells: string[] = [];
    for (let c = 0; c < cols && rp < end; c++) {
      const vl = raw[rp++]!;
      if (rp + vl > end) break;
      cells.push(decodeGbk(raw.subarray(rp, rp + vl)).replace(/\0+$/, "").trim());
      rp += vl;
    }
    while (cells.length < cols) cells.push("");
    out.rows.push(cells);
    pos = end;
  }
  return out;
}

function u32(s: string): number {
  const n = Number.parseInt(s, 10);
  return Number.isFinite(n) ? n >>> 0 : 0;
}

function parseWMap(raw: Uint8Array): WMapNode[] {
  const { rows } = parseRows(raw);
  return rows.map((c) => ({
    id: u32(c[0]!),
    teleportId: u32(c[1]!),
    name: c[2] || "",
    alias: c[3] || c[2] || "",
    x: u32(c[4]!),
    y: u32(c[5]!),
    up: u32(c[8]!),
    right: u32(c[9]!),
    down: u32(c[10]!),
    left: u32(c[11]!),
    childBase: u32(c[12]!),
    sceneCount: u32(c[13]!),
    lineBase: u32(c[14]!),
    lineCount: u32(c[15]!),
    level: c[16] || "无",
  }));
}

function parseSMap(raw: Uint8Array): SMapNode[] {
  const { rows } = parseRows(raw);
  return rows.map((c) => ({
    id: u32(c[0]!),
    scene: c[1] || "",
    alias: c[2] || "",
    x: u32(c[3]!),
    y: u32(c[4]!),
    up: u32(c[7]!),
    right: u32(c[8]!),
    down: u32(c[9]!),
    left: u32(c[10]!),
    parentWMapId: u32(c[11]!),
    level: c[12] || "无",
  }));
}

function parseWMapLines(raw: Uint8Array): MapLine[] {
  const { rows } = parseRows(raw);
  return rows.map((c) => ({
    id: u32(c[0]!),
    parentId: 0,
    x1: u32(c[1]!),
    y1: u32(c[2]!),
    x2: u32(c[3]!),
    y2: u32(c[4]!),
  }));
}

function parseSMapLines(raw: Uint8Array): MapLine[] {
  const { rows } = parseRows(raw);
  return rows.map((c) => ({
    id: u32(c[0]!),
    parentId: u32(c[1]!),
    x1: u32(c[2]!),
    y1: u32(c[3]!),
    x2: u32(c[4]!),
    y2: u32(c[5]!),
  }));
}

export async function loadWorldMapCatalog(): Promise<WorldMapCatalog> {
  if (cached) return cached;
  const [w, wl, s, sl] = await Promise.all([
    fetchResourceBytes("wMap.dsh"),
    fetchResourceBytes("wMapLine.dsh"),
    fetchResourceBytes("sMap.dsh"),
    fetchResourceBytes("sMapLine.dsh"),
  ]);
  const worlds = parseWMap(w);
  const scenes = parseSMap(s);
  const worldLines = parseWMapLines(wl);
  const sceneLines = parseSMapLines(sl);
  const byWorldId = new Map(worlds.map((n) => [n.id, n]));
  const bySceneId = new Map(scenes.map((n) => [n.id, n]));
  const byTeleportId = new Map(worlds.map((n) => [n.teleportId, n]));
  cached = { worlds, scenes, worldLines, sceneLines, byWorldId, bySceneId, byTeleportId };
  return cached;
}

function sceneKey(s: string): string {
  return s.replace(/\.sce$/i, "").toLowerCase();
}

/** Resolve sMap row for the current scene leaf. */
export function findSceneNode(
  catalog: WorldMapCatalog,
  scene: string,
): SMapNode | null {
  if (!scene) return null;
  const keys = new Set(sceneAssetCandidates(scene).map(sceneKey));
  for (const n of catalog.scenes) {
    if (keys.has(sceneKey(n.scene))) return n;
  }
  // Loose contains (临安府_06)
  const leaf = sceneKey(scene).replace(/^c0+/, "");
  for (const n of catalog.scenes) {
    if (sceneKey(n.scene).replace(/^c0+/, "").includes(leaf) || leaf.includes(sceneKey(n.scene).replace(/^c0+/, ""))) {
      return n;
    }
  }
  return null;
}

export function scenesForWorld(catalog: WorldMapCatalog, wMapId: number): SMapNode[] {
  return catalog.scenes.filter((n) => n.parentWMapId === wMapId);
}

export function linesForWorld(catalog: WorldMapCatalog, wMapId: number): MapLine[] {
  return catalog.sceneLines.filter((n) => n.parentId === wMapId);
}

/** Neighbor hop using ID上/右/下/左 (1=up 2=right 3=down 4=left). */
export function neighborId(
  node: { up: number; right: number; down: number; left: number },
  cbeDir: number,
): number {
  switch (cbeDir) {
    case 1:
      return node.up;
    case 2:
      return node.right;
    case 3:
      return node.down;
    case 4:
      return node.left;
    default:
      return 0;
  }
}

/**
 * Map UI may teleport to any selected node (item 800). DSH neighbor links can
 * be sparse — when the directed link is 0, pick the geometrically nearest
 * other node in that direction. Scene teleport-stone NPC (16/1) stays the
 * restricted destination list.
 */
export function nearestNodeInDir<T extends { id: number; x: number; y: number }>(
  nodes: T[],
  from: T,
  cbeDir: number,
): T | null {
  let best: T | null = null;
  let bestScore = Infinity;
  for (const n of nodes) {
    if (n.id === from.id) continue;
    const dx = n.x - from.x;
    const dy = n.y - from.y;
    let ok = false;
    let primary = 0;
    let lateral = 0;
    switch (cbeDir) {
      case 1:
        ok = dy < 0;
        primary = -dy;
        lateral = Math.abs(dx);
        break;
      case 2:
        ok = dx > 0;
        primary = dx;
        lateral = Math.abs(dy);
        break;
      case 3:
        ok = dy > 0;
        primary = dy;
        lateral = Math.abs(dx);
        break;
      case 4:
        ok = dx < 0;
        primary = -dx;
        lateral = Math.abs(dy);
        break;
      default:
        return null;
    }
    if (!ok) continue;
    const score = primary * 1000 + lateral;
    if (score < bestScore) {
      bestScore = score;
      best = n;
    }
  }
  return best;
}

/** Teleport stone item id (ConsumeInventoryItem / mall 秘宝). */
export const TELEPORT_STONE_ITEM_ID = 800;
