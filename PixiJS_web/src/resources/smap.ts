import { decodeGbk, readU32LE } from "./binary";
import { fetchResourceBytes } from "./loader";
import { sceneAssetCandidates } from "../net/protocol";

/**
 * sMap.dsh column-2 display aliases (临安-庆阳宫 etc.).
 * Evidence: web_admin fill_scene_aliases; docs/re scene-key / 南宣门 notes.
 */
let cached: Map<string, string> | null = null;

function indexKey(scene: string): string {
  return scene.replace(/\.sce$/i, "").toLowerCase();
}

/** Parse plain sMap.dsh bytes → scene leaf → UTF-8 display name. */
export function parseSMapAliases(raw: Uint8Array): Map<string, string> {
  const out = new Map<string, string>();
  if (raw.length < 20) return out;
  const payloadLen = readU32LE(raw, 0);
  if (payloadLen !== raw.length - 4) return out;
  const cols = readU32LE(raw, 4);
  const rows = readU32LE(raw, 8);
  const headerBytes = readU32LE(raw, 12);
  if (cols < 3 || cols > 64 || rows > 512 || 16 + headerBytes > raw.length) {
    return out;
  }
  let pos = 16 + headerBytes;
  for (let row = 0; row < rows && pos + 4 <= raw.length; row++) {
    const rowLen = readU32LE(raw, pos);
    let rp = pos + 4;
    const end = rp + rowLen;
    if (rowLen === 0 || end > raw.length || end < rp) break;
    let scene = "";
    let alias = "";
    for (let c = 0; c < cols && rp < end; c++) {
      const vl = raw[rp++]!;
      if (rp + vl > end) break;
      const cell = decodeGbk(raw.subarray(rp, rp + vl)).replace(/\0+$/, "").trim();
      rp += vl;
      if (c === 1) scene = cell;
      else if (c === 2) alias = cell;
    }
    pos = end;
    if (!scene || !alias || !/\.sce$/i.test(scene)) continue;
    // Index every disk/sMap alias form so HUD lookup matches either key.
    for (const cand of sceneAssetCandidates(scene)) {
      out.set(indexKey(cand), alias);
    }
  }
  return out;
}

export async function loadSMapAliases(): Promise<Map<string, string>> {
  if (cached) return cached;
  try {
    const raw = await fetchResourceBytes("sMap.dsh");
    cached = parseSMapAliases(raw);
  } catch (e) {
    console.warn("[smap] load failed", e);
    cached = new Map();
  }
  return cached;
}

/** Sync lookup after loadSMapAliases(); null if unknown / not loaded. */
export function lookupSMapAlias(scene: string): string | null {
  if (!cached || !scene) return null;
  for (const cand of sceneAssetCandidates(scene)) {
    const hit = cached.get(indexKey(cand));
    if (hit) return hit;
  }
  return null;
}
