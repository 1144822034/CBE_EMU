import { decodeGbk, readU32LE } from "./binary";

export type MapPreview = {
  imageNames: string[];
  width: number;
  height: number;
  tileWidth: number;
  tileHeight: number;
  cols: number;
  rows: number;
  /** LE u32 cells: (imageIndex<<24) | tileIndex — see web_admin_server.c */
  cells: Uint32Array;
};

export function parseMapPayload(data: Uint8Array, mapName = ""): MapPreview {
  if (data.length < 24) throw new Error("map: truncated");
  let pos = 0;
  const imageCount = readU32LE(data, pos);
  pos += 4;
  if (imageCount === 0 || imageCount > 32) {
    throw new Error(`map: bad imageCount=${imageCount}`);
  }

  const imageNames: string[] = [];
  for (let i = 0; i < imageCount; i++) {
    if (pos >= data.length) throw new Error("map: eof in names");
    const nameLen = data[pos++]!;
    if (nameLen === 0 || pos + nameLen > data.length) {
      throw new Error("map: bad image name");
    }
    const bytes = data.subarray(pos, pos + nameLen);
    let name = decodeGbk(bytes);
    if (!name.toLowerCase().endsWith(".gif")) {
      name = Array.from(bytes, (b) => String.fromCharCode(b)).join("");
    }
    imageNames.push(name);
    pos += nameLen;
  }

  if (pos + 16 > data.length) throw new Error("map: missing dims");
  const width = readU32LE(data, pos);
  const height = readU32LE(data, pos + 4);
  const tileWidth = readU32LE(data, pos + 8);
  const tileHeight = readU32LE(data, pos + 12);
  pos += 16;

  if (
    width === 0 ||
    height === 0 ||
    tileWidth === 0 ||
    tileHeight === 0 ||
    width > 4096 ||
    height > 4096
  ) {
    throw new Error(`map: bad dims ${width}x${height} tile ${tileWidth}x${tileHeight}`);
  }

  const cols = Math.ceil(width / tileWidth);
  const rows = Math.ceil(height / tileHeight);
  const expected = cols * rows;
  const remaining = data.length - pos;
  if ((remaining & 3) !== 0 || remaining / 4 !== expected) {
    throw new Error(
      `map: cell mismatch expected=${expected} got=${remaining / 4} (${mapName})`,
    );
  }

  const cells = new Uint32Array(expected);
  for (let i = 0; i < expected; i++) {
    cells[i] = readU32LE(data, pos + i * 4);
  }

  return { imageNames, width, height, tileWidth, tileHeight, cols, rows, cells };
}

/**
 * MAP cell high nibble (bits 28..31): collision / edge bits.
 * Thumb evidence in 江湖OL.CBE CheckMapMoveCollision* (see
 * docs/re/2026-08-09-pixi-map-edge-collision.md):
 *   - 0x0 = open
 *   - 0x1 N / 0x2 W / 0x4 S / 0x8 E  (directional fringe on that cell edge)
 *   - 0xF = all four edges (= solid for cross-cell moves)
 * Crossing a shared edge is blocked if CURRENT exit bit OR DESTINATION entry
 * bit is set. Treating every non-zero as solid over-blocks town roads.
 */
export const MAP_EDGE_N = 0x1;
export const MAP_EDGE_W = 0x2;
export const MAP_EDGE_S = 0x4;
export const MAP_EDGE_E = 0x8;

export function mapCellFlags(packed: number): number {
  return (packed >>> 28) & 0xf;
}

/**
 * Full-cell solid — only the 0xF blob (all edges sealed).
 * Directional fringe alone is walkable; use mapMoveBlocked for edge tests.
 */
export function mapCellBlocked(packed: number): boolean {
  return mapCellFlags(packed) === 0xf;
}

function clampPixel(map: MapPreview, x: number, y: number): { px: number; py: number } {
  return {
    px: Math.max(0, Math.min(map.width - 1, x | 0)),
    py: Math.max(0, Math.min(map.height - 1, y | 0)),
  };
}

function mapCellGrid(
  map: MapPreview,
  x: number,
  y: number,
): { gx: number; gy: number } | null {
  const { tileWidth, tileHeight, cols, rows } = map;
  if (tileWidth <= 0 || tileHeight <= 0 || !map.cells.length) return null;
  const { px, py } = clampPixel(map, x, y);
  return {
    gx: Math.min(cols - 1, (px / tileWidth) | 0),
    gy: Math.min(rows - 1, (py / tileHeight) | 0),
  };
}

function mapPackedAtGrid(map: MapPreview, gx: number, gy: number): number | null {
  const { cols, rows, cells } = map;
  if (gx < 0 || gy < 0 || gx >= cols || gy >= rows) return null;
  return cells[gx * rows + gy] ?? null;
}

/** Footprint probe offsets (radius 0 = feet only). */
function footprintSamples(radius: number): Array<[number, number]> {
  if (radius <= 0) return [[0, 0]];
  return [
    [0, 0],
    [-radius, 0],
    [radius, 0],
    [0, -radius],
    [0, radius],
  ];
}

/**
 * Footprint sample: block if any probe lands on a solid (0xF) tile.
 * Default radius 1 — radius 4 over-blocked narrow roads between wall blobs.
 */
export function mapBlocksAt(
  map: MapPreview,
  x: number,
  y: number,
  radius = 1,
): boolean {
  if (!map.cells.length) return false;
  for (const [ox, oy] of footprintSamples(radius)) {
    const g = mapCellGrid(map, x + ox, y + oy);
    if (!g) continue;
    const packed = mapPackedAtGrid(map, g.gx, g.gy);
    if (packed != null && mapCellBlocked(packed)) return true;
  }
  return false;
}

/**
 * One shared edge between adjacent cells: blocked if exit|entry bit set.
 * Bit layout from CBE Thumb: N=1 W=2 S=4 E=8.
 */
function mapAdjacentEdgeBlocked(
  map: MapPreview,
  gx0: number,
  gy0: number,
  gx1: number,
  gy1: number,
): boolean {
  const a = mapPackedAtGrid(map, gx0, gy0);
  const b = mapPackedAtGrid(map, gx1, gy1);
  if (a == null || b == null) return true;
  const fa = mapCellFlags(a);
  const fb = mapCellFlags(b);
  if (gx1 === gx0 + 1 && gy1 === gy0) {
    return (fa & MAP_EDGE_E) !== 0 || (fb & MAP_EDGE_W) !== 0;
  }
  if (gx1 === gx0 - 1 && gy1 === gy0) {
    return (fa & MAP_EDGE_W) !== 0 || (fb & MAP_EDGE_E) !== 0;
  }
  if (gy1 === gy0 + 1 && gx1 === gx0) {
    return (fa & MAP_EDGE_S) !== 0 || (fb & MAP_EDGE_N) !== 0;
  }
  if (gy1 === gy0 - 1 && gx1 === gx0) {
    return (fa & MAP_EDGE_N) !== 0 || (fb & MAP_EDGE_S) !== 0;
  }
  return false;
}

/**
 * Axis-aligned (or multi-cell) segment: walk cell-by-cell and test each edge.
 * Diagonal segments are split into X then Y (same as SceneView slide order).
 */
export function mapEdgeBlocksSegment(
  map: MapPreview,
  fromX: number,
  fromY: number,
  toX: number,
  toY: number,
): boolean {
  const c0 = mapCellGrid(map, fromX, fromY);
  const c1 = mapCellGrid(map, toX, toY);
  if (!c0 || !c1) return true;
  if (c0.gx === c1.gx && c0.gy === c1.gy) return false;

  if (c0.gy === c1.gy) {
    const step = c1.gx > c0.gx ? 1 : -1;
    for (let gx = c0.gx; gx !== c1.gx; gx += step) {
      if (mapAdjacentEdgeBlocked(map, gx, c0.gy, gx + step, c0.gy)) return true;
    }
    return false;
  }
  if (c0.gx === c1.gx) {
    const step = c1.gy > c0.gy ? 1 : -1;
    for (let gy = c0.gy; gy !== c1.gy; gy += step) {
      if (mapAdjacentEdgeBlocked(map, c0.gx, gy, c0.gx, gy + step)) return true;
    }
    return false;
  }

  // Diagonal: X then Y (matches SceneView axis-separated collision).
  if (mapEdgeBlocksSegment(map, fromX, fromY, toX, fromY)) return true;
  return mapEdgeBlocksSegment(map, toX, fromY, toX, toY);
}

/**
 * Full move test: destination 0xF solid OR directional edge crossed.
 * Use for player steps; keep mapBlocksAt for “standing in solid” snaps.
 */
export function mapMoveBlocked(
  map: MapPreview,
  fromX: number,
  fromY: number,
  toX: number,
  toY: number,
  radius = 1,
): boolean {
  if (mapBlocksAt(map, toX, toY, radius)) return true;
  for (const [ox, oy] of footprintSamples(radius)) {
    if (mapEdgeBlocksSegment(map, fromX + ox, fromY + oy, toX + ox, toY + oy)) {
      return true;
    }
  }
  return false;
}

/** Spiral search for a walkable pixel near (ox,oy). Used when feet land in solid. */
export function findNearestWalkable(
  map: MapPreview,
  ox: number,
  oy: number,
  maxRadius = 64,
  footprint = 1,
): { x: number; y: number } | null {
  const x0 = ox | 0;
  const y0 = oy | 0;
  if (!mapBlocksAt(map, x0, y0, footprint)) return { x: x0, y: y0 };
  for (let r = 1; r <= maxRadius; r++) {
    for (let dy = -r; dy <= r; dy++) {
      for (let dx = -r; dx <= r; dx++) {
        if (Math.max(Math.abs(dx), Math.abs(dy)) !== r) continue;
        const x = x0 + dx;
        const y = y0 + dy;
        if (x < 0 || y < 0 || x >= map.width || y >= map.height) continue;
        if (!mapBlocksAt(map, x, y, footprint)) return { x, y };
      }
    }
  }
  return null;
}

/**
 * Compose RGB565 canvas then RGBA — matches admin map preview blit order
 * (column-major: gridX = index / rows, gridY = index % rows).
 */
export function composeMapRgba(
  map: MapPreview,
  sheets: Array<{ rgb565: Uint16Array; pitch: number; width: number; height: number } | null>,
): { width: number; height: number; rgba: Uint8ClampedArray } {
  const { width, height, tileWidth, tileHeight, rows, cells } = map;
  const canvas = new Uint16Array(width * height);

  for (let index = 0; index < cells.length; index++) {
    const packed = cells[index]!;
    const imageIndex = (packed >>> 24) & 0x0f;
    const tileIndex = packed & 0x00ffffff;
    const gridX = (index / rows) | 0;
    const gridY = index % rows;
    const dstX = gridX * tileWidth;
    const dstY = gridY * tileHeight;
    let copyW = tileWidth;
    let copyH = tileHeight;
    if (dstX + copyW > width) copyW = width - dstX;
    if (dstY + copyH > height) copyH = height - dstY;

    const sheet = sheets[imageIndex];
    let copied = false;
    if (sheet) {
      const tilesPerRow = (sheet.width / tileWidth) | 0;
      const tilesPerCol = (sheet.height / tileHeight) | 0;
      const tileCount = tilesPerRow * tilesPerCol;
      if (tilesPerRow > 0 && tilesPerCol > 0 && tileIndex < tileCount) {
        const sourceX = (tileIndex % tilesPerRow) * tileWidth;
        const sourceY = ((tileIndex / tilesPerRow) | 0) * tileHeight;
        for (let y = 0; y < copyH; y++) {
          const dstRow = (dstY + y) * width + dstX;
          const srcRow = (sourceY + y) * sheet.pitch + sourceX;
          for (let x = 0; x < copyW; x++) {
            canvas[dstRow + x] = sheet.rgb565[srcRow + x]!;
          }
        }
        copied = true;
      }
    }
    if (!copied) {
      // Magenta placeholder like admin fill
      for (let y = 0; y < copyH; y++) {
        for (let x = 0; x < copyW; x++) {
          canvas[(dstY + y) * width + dstX + x] = 0xf81f;
        }
      }
    }
  }

  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let i = 0; i < canvas.length; i++) {
    const pixel = canvas[i]!;
    const o = i * 4;
    rgba[o] = (((pixel >> 11) & 0x1f) * 255) / 31;
    rgba[o + 1] = (((pixel >> 5) & 0x3f) * 255) / 63;
    rgba[o + 2] = ((pixel & 0x1f) * 255) / 31;
    rgba[o + 3] = 255;
  }
  return { width, height, rgba };
}
