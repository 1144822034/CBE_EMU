import { asciiAt, decodeGbk, readU16LE } from "./binary";

export type SceHeader = {
  width: number;
  height: number;
  mapName: string;
  payload: Uint8Array;
  base: number;
};

/** SCE2 combat-spawn row (kind=3). Same layout as mock_server_scene_task.c. */
export type SceCombatSpawn = {
  /** SCE embedded enemy id (may repeat across rows). */
  sceActorId: number;
  x: number;
  y: number;
  name: string;
  /**
   * Field 0x11 primary — battle / catalog actor (蜜蜂、毒泥怪…).
   * Not drawn on the overworld when `mapActorResource` is set.
   */
  battleActorResource: string;
  /**
   * Field-3 dual companion — overworld map sprite (usually e_ghostfire*).
   * Outdoor census: nearly every kind=3 has this; CBE map shows the orb, battle
   * shows `battleActorResource`.
   */
  mapActorResource: string;
  visualHint: number;
};

/** SCE portal kinds — mirrors web_admin_server / mock edge+meta+named. */
export type ScePortalKind = "edge" | "meta" | "named";

export type ScePortal = {
  kind: ScePortalKind;
  entryId: number;
  targetEntryId: number;
  /** Landing spawn near trigger (edge only; 0 if absent). */
  spawnX: number;
  spawnY: number;
  /** Pixel trigger rect. */
  left: number;
  top: number;
  right: number;
  bottom: number;
  targetScene: string;
  /** Named portal display label (empty for edge/meta). */
  displayName: string;
  /** Optional b_*.sce / prompt string from field 0x12. */
  battleOrPrompt: string;
};

/** Interactive SCE NPC / prop (kind≤32), including n_telestone. */
export type SceInteractive = {
  x: number;
  y: number;
  name: string;
  actorResource: string;
  scriptName: string;
  orientation: number;
};

/** Find SCE2 and read embedded map leaf name (docs/re/2026-07-05…). */
export function parseSce2(payload: Uint8Array): SceHeader {
  let base = -1;
  const limit = Math.min(32, payload.length - 15);
  for (let i = 0; i <= limit; i++) {
    if (asciiAt(payload, i, 4) === "SCE2") {
      base = i;
      break;
    }
  }
  if (base < 0) throw new Error("sce: SCE2 magic not found");

  const width = readU16LE(payload, base + 4);
  const height = readU16LE(payload, base + 6);
  const nameLen = payload[base + 10]!;
  if (nameLen === 0 || base + 11 + nameLen > payload.length) {
    throw new Error("sce: bad map name length");
  }
  const nameBytes = payload.subarray(base + 11, base + 11 + nameLen);
  let mapName = decodeGbk(nameBytes);
  if (!mapName.toLowerCase().endsWith(".map")) {
    mapName = Array.from(nameBytes, (b) => String.fromCharCode(b)).join("");
  }
  if (!mapName.toLowerCase().endsWith(".map")) {
    throw new Error(`sce: map name not .map (${mapName})`);
  }

  return { width, height, mapName, payload, base };
}

/** Sibling map guess: stem.sce -> stem.map */
export function siblingMapName(sceneFile: string): string {
  const lower = sceneFile.toLowerCase();
  if (lower.endsWith(".sce")) return sceneFile.slice(0, -4) + ".map";
  return sceneFile + ".map";
}

function readSceLenString(
  data: Uint8Array,
  posRef: { pos: number },
): string | null {
  const pos = posRef.pos;
  if (pos >= data.length) return null;
  const n = data[pos]!;
  if (pos + 1 + n > data.length) return null;
  const s = decodeGbk(data.subarray(pos + 1, pos + 1 + n));
  posRef.pos = pos + 1 + n;
  return s;
}

function readSceStringField(
  data: Uint8Array,
  posRef: { pos: number },
  field: number,
): string | null {
  const pos = posRef.pos;
  if (pos + 5 > data.length) return null;
  if (readU16LE(data, pos) !== 3 || readU16LE(data, pos + 2) !== field) {
    return null;
  }
  posRef.pos = pos + 4;
  return readSceLenString(data, posRef);
}

function readSceScalarField(
  data: Uint8Array,
  posRef: { pos: number },
  expectedField: number,
): number | null {
  const pos = posRef.pos;
  if (pos + 6 > data.length) return null;
  if (readU16LE(data, pos) !== 1 || readU16LE(data, pos + 2) !== expectedField) {
    return null;
  }
  const v = readU16LE(data, pos + 4);
  posRef.pos = pos + 6;
  return v;
}

/** SCE map decoration placement (peach trees / FB lotus etc.). */
export type SceDecoration = {
  x: number;
  y: number;
  actorResource: string;
};

/**
 * Body after SCE2 header + zero pad.
 * Evidence: docs/re/2026-07-31-dream-sce-kind3-inject.md (FB decor table);
 * outdoor peach/Lin'an: `01 00 01 00 05 00 …` + `b_flowers01.actor` + 8-byte spots.
 */
function sceBodyStart(payload: Uint8Array): number {
  const limit = Math.min(32, payload.length - 15);
  for (let i = 0; i <= limit; i++) {
    if (asciiAt(payload, i, 4) !== "SCE2") continue;
    const nameLen = payload[i + 10]!;
    if (nameLen === 0 || i + 11 + nameLen > payload.length) return i + 4;
    let pos = i + 11 + nameLen;
    while (pos < payload.length && payload[pos] === 0) pos++;
    return pos;
  }
  return 0;
}

/**
 * Parse SCE decoration actor table + 8-byte (x,y,0,index) placements.
 * Ends when actorIndex ≥ name count (edge portal / kind=3 headers fail this).
 */
export function parseSceDecorations(payload: Uint8Array): SceDecoration[] {
  const start = sceBodyStart(payload);
  if (start + 12 > payload.length) return [];
  if (readU16LE(payload, start) !== 1 || readU16LE(payload, start + 2) !== 1) {
    return [];
  }
  // table id 0x05 (flowers) / 0x0d (FB) / others — require trailing 01 00 00 00
  if (readU16LE(payload, start + 6) !== 1 || readU16LE(payload, start + 8) !== 0) {
    return [];
  }
  const actorCount = readU16LE(payload, start + 10);
  if (actorCount === 0 || actorCount > 64) return [];
  let pos = start + 12;
  const actors: string[] = [];
  for (let i = 0; i < actorCount; i++) {
    if (pos >= payload.length) return [];
    const nameLen = payload[pos++]!;
    if (nameLen === 0 || nameLen > 64 || pos + nameLen > payload.length) return [];
    const bytes = payload.subarray(pos, pos + nameLen);
    pos += nameLen;
    let name = decodeGbk(bytes);
    if (!endsWithActor(name)) {
      name = Array.from(bytes, (b) => String.fromCharCode(b)).join("");
    }
    while (pos < payload.length && payload[pos] === 0) pos++;
    if (!endsWithActor(name)) return [];
    actors.push(name);
  }

  const out: SceDecoration[] = [];
  const seen = new Set<string>();
  while (pos + 8 <= payload.length) {
    const x = readU16LE(payload, pos);
    const y = readU16LE(payload, pos + 2);
    const idx = readU16LE(payload, pos + 6);
    pos += 8;
    if (idx >= actors.length) break;
    if (!x && !y) continue;
    const key = `${actors[idx]}@${x},${y}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({ x, y, actorResource: actors[idx]! });
  }
  return out;
}

function scePayloadStart(payload: Uint8Array): number {
  const limit = Math.min(32, payload.length - 4);
  for (let i = 0; i <= limit; i++) {
    if (asciiAt(payload, i, 4) === "SCE2") return i;
  }
  return 0;
}

function endsWithSce(name: string): boolean {
  return name.toLowerCase().endsWith(".sce");
}

function endsWithActor(name: string): boolean {
  return name.toLowerCase().endsWith(".actor");
}

function parseEdgePortalAt(
  data: Uint8Array,
  off: number,
): { portal: ScePortal; end: number } | null {
  if (off + 18 > data.length) return null;
  let pos = off;
  const kind0 = readU16LE(data, pos);
  pos += 2;
  let spawnX = 0;
  let spawnY = 0;
  if (kind0 === 2) {
    if (pos + 4 > data.length) return null;
    spawnX = readU16LE(data, pos);
    spawnY = readU16LE(data, pos + 2);
    pos += 4;
  } else {
    if (pos + 6 > data.length || readU16LE(data, pos) !== 2) return null;
    spawnX = readU16LE(data, pos + 2);
    spawnY = readU16LE(data, pos + 4);
    pos += 6;
  }
  if (pos + 8 > data.length || readU16LE(data, pos) !== 8) return null;
  pos += 8;
  const posRef = { pos };
  if (posRef.pos + 5 > data.length || readU16LE(data, posRef.pos) !== 3) {
    return null;
  }
  if (readU16LE(data, posRef.pos + 2) !== 6) return null;
  posRef.pos += 4;
  const targetScene = readSceLenString(data, posRef);
  if (!targetScene || !endsWithSce(targetScene)) return null;
  const entryId = readSceScalarField(data, posRef, 0x07);
  const left = readSceScalarField(data, posRef, 0x0a);
  const top = readSceScalarField(data, posRef, 0x0b);
  const right = readSceScalarField(data, posRef, 0x0c);
  const bottom = readSceScalarField(data, posRef, 0x0d);
  const targetEntryId = readSceScalarField(data, posRef, 0x13);
  if (
    entryId == null ||
    left == null ||
    top == null ||
    right == null ||
    bottom == null ||
    targetEntryId == null ||
    right < left ||
    bottom < top
  ) {
    return null;
  }
  return {
    portal: {
      kind: "edge",
      entryId,
      targetEntryId,
      spawnX,
      spawnY,
      left,
      top,
      right,
      bottom,
      targetScene,
      displayName: "",
      battleOrPrompt: "",
    },
    end: posRef.pos,
  };
}

function parseMetaPortalAt(
  data: Uint8Array,
  off: number,
): { portal: ScePortal; end: number } | null {
  if (off + 12 > data.length) return null;
  let pos = off;
  const kind0 = readU16LE(data, pos);
  pos += 2;
  if (kind0 === 8) {
    if (pos + 6 > data.length) return null;
    pos += 6;
  } else {
    if (pos + 8 > data.length || readU16LE(data, pos) !== 8) return null;
    pos += 8;
  }
  const posRef = { pos };
  const targetScene = readSceStringField(data, posRef, 6);
  if (!targetScene || !endsWithSce(targetScene)) return null;
  const entryId = readSceScalarField(data, posRef, 0x07);
  const left = readSceScalarField(data, posRef, 0x0a);
  const top = readSceScalarField(data, posRef, 0x0b);
  const right = readSceScalarField(data, posRef, 0x0c);
  const bottom = readSceScalarField(data, posRef, 0x0d);
  const targetEntryId = readSceScalarField(data, posRef, 0x13);
  if (
    entryId == null ||
    left == null ||
    top == null ||
    right == null ||
    bottom == null ||
    targetEntryId == null ||
    right < left ||
    bottom < top
  ) {
    return null;
  }
  return {
    portal: {
      kind: "meta",
      entryId,
      targetEntryId,
      spawnX: 0,
      spawnY: 0,
      left,
      top,
      right,
      bottom,
      targetScene,
      displayName: "",
      battleOrPrompt: "",
    },
    end: posRef.pos,
  };
}

function parseNamedPortalAt(
  data: Uint8Array,
  off: number,
): { portal: ScePortal; end: number } | null {
  if (off + 12 > data.length) return null;
  let pos = off;
  const kind0 = readU16LE(data, pos);
  pos += 2;
  let tileX = 0;
  let tileY = 0;
  let tileW = 0;
  let tileH = 0;
  let battleOrPrompt = "";

  if (
    kind0 === 4 &&
    off + 12 <= data.length &&
    readU16LE(data, off + 6) === 4 &&
    readU16LE(data, off + 8) === 3 &&
    readU16LE(data, off + 10) === 0x12
  ) {
    tileX = readU16LE(data, off + 2);
    tileY = readU16LE(data, off + 4);
    tileW = 4;
    tileH = 4;
    const posRef = { pos: off + 8 };
    const s = readSceStringField(data, posRef, 0x12);
    if (!s) return null;
    battleOrPrompt = s;
    pos = posRef.pos;
  } else if (
    kind0 === 1 &&
    off + 14 <= data.length &&
    readU16LE(data, off + 2) === 4 &&
    readU16LE(data, off + 8) === 4 &&
    readU16LE(data, off + 10) === 3 &&
    readU16LE(data, off + 12) === 0x12
  ) {
    tileX = readU16LE(data, off + 4);
    tileY = readU16LE(data, off + 6);
    tileW = 4;
    tileH = 4;
    const posRef = { pos: off + 10 };
    const s = readSceStringField(data, posRef, 0x12);
    if (!s) return null;
    battleOrPrompt = s;
    pos = posRef.pos;
  } else if (kind0 === 4) {
    if (pos + 8 > data.length) return null;
    tileX = readU16LE(data, pos);
    tileY = readU16LE(data, pos + 2);
    tileW = readU16LE(data, pos + 4);
    tileH = readU16LE(data, pos + 6);
    pos += 8;
    if (!tileW || !tileH) return null;
    const posRef = { pos };
    if (pos + 3 <= data.length && readU16LE(data, pos) === 0x12) {
      posRef.pos = pos + 2;
      const s = readSceLenString(data, posRef);
      if (s) battleOrPrompt = s;
      pos = posRef.pos;
    } else if (
      pos + 5 <= data.length &&
      readU16LE(data, pos) === 3 &&
      readU16LE(data, pos + 2) === 0x12
    ) {
      const s = readSceStringField(data, posRef, 0x12);
      if (s) battleOrPrompt = s;
      pos = posRef.pos;
    }
  } else {
    if (pos + 10 > data.length || readU16LE(data, pos) !== 4) return null;
    tileX = readU16LE(data, pos + 2);
    tileY = readU16LE(data, pos + 4);
    tileW = readU16LE(data, pos + 6);
    tileH = readU16LE(data, pos + 8);
    pos += 10;
    if (!tileW || !tileH) return null;
    if (
      pos + 5 <= data.length &&
      readU16LE(data, pos) === 3 &&
      readU16LE(data, pos + 2) === 0x12
    ) {
      const posRef = { pos };
      const s = readSceStringField(data, posRef, 0x12);
      if (s) battleOrPrompt = s;
      pos = posRef.pos;
    }
  }

  const posRef = { pos };
  const targetEntryId = readSceScalarField(data, posRef, 0x15);
  const displayName = readSceStringField(data, posRef, 0x16);
  const targetScene = readSceStringField(data, posRef, 0x17);
  if (
    targetEntryId == null ||
    displayName == null ||
    !targetScene ||
    !endsWithSce(targetScene)
  ) {
    return null;
  }
  const left = tileX * 16;
  const top = tileY * 16;
  return {
    portal: {
      kind: "named",
      entryId: 0xffff,
      targetEntryId,
      spawnX: 0,
      spawnY: 0,
      left,
      top,
      right: left + tileW * 16,
      bottom: top + tileH * 16,
      targetScene,
      displayName,
      battleOrPrompt,
    },
    end: posRef.pos,
  };
}

/** Collect edge / meta / named portals from SCE2 (same scan as admin collect). */
export function parseScePortals(payload: Uint8Array): ScePortal[] {
  const start = scePayloadStart(payload);
  const out: ScePortal[] = [];
  const seen = new Set<string>();
  for (let off = start; off + 12 <= payload.length; off++) {
    const edge = parseEdgePortalAt(payload, off);
    if (edge) {
      const key = `e:${edge.portal.entryId}:${edge.portal.targetScene}:${edge.portal.left},${edge.portal.top}`;
      if (!seen.has(key)) {
        seen.add(key);
        out.push(edge.portal);
      }
      off = Math.max(off, edge.end - 1);
      continue;
    }
    const meta = parseMetaPortalAt(payload, off);
    if (meta) {
      const key = `m:${meta.portal.entryId}:${meta.portal.targetScene}:${meta.portal.left},${meta.portal.top}`;
      if (!seen.has(key)) {
        seen.add(key);
        out.push(meta.portal);
      }
      off = Math.max(off, meta.end - 1);
      continue;
    }
    const named = parseNamedPortalAt(payload, off);
    if (named) {
      const key = `n:${named.portal.displayName}:${named.portal.targetScene}:${named.portal.left},${named.portal.top}`;
      if (!seen.has(key)) {
        seen.add(key);
        out.push(named.portal);
      }
      off = Math.max(off, named.end - 1);
    }
  }
  return out;
}

/**
 * Interactive SCE props/NPCs (mock parse_sce_interactive_npc_at).
 * Used for n_telestone when 27/11 omits them.
 */
export function parseSceInteractives(payload: Uint8Array): SceInteractive[] {
  const start = scePayloadStart(payload);
  const out: SceInteractive[] = [];
  const seen = new Set<string>();
  for (let off = start; off + 8 <= payload.length; off++) {
    const kind = readU16LE(payload, off);
    if (kind === 0 || kind > 32) continue;
    const posRef = { pos: off + 2 };
    const actorResource = readSceStringField(payload, posRef, 3);
    if (!actorResource || !endsWithActor(actorResource)) continue;
    const scriptName = readSceStringField(payload, posRef, 4);
    if (!scriptName || !scriptName.toLowerCase().endsWith(".xse")) continue;
    if (
      posRef.pos + 4 <= payload.length &&
      readU16LE(payload, posRef.pos) === 3 &&
      readU16LE(payload, posRef.pos + 2) === 2
    ) {
      if (!readSceStringField(payload, posRef, 2)) continue;
    }
    const displayName = readSceStringField(payload, posRef, 1);
    if (!displayName) continue;

    let orientation = 0;
    let x = 0;
    let y = 0;
    if (posRef.pos + 6 > payload.length) continue;

    if (readU16LE(payload, posRef.pos) === 1) {
      const field = readU16LE(payload, posRef.pos + 2);
      const value = readU16LE(payload, posRef.pos + 4);
      posRef.pos += 6;
      if (field === 0x18 && value <= 8) {
        orientation = value;
        if (posRef.pos + 6 > payload.length) continue;
        if (readU16LE(payload, posRef.pos) === 1) {
          // consume optional follow-up scalar then expect type-2 xy
          posRef.pos += 6;
        }
      } else {
        // Treat as x/y packed under unexpected type-1 (rare); reject.
        continue;
      }
    }

    if (posRef.pos + 6 > payload.length) continue;
    if (readU16LE(payload, posRef.pos) === 2) {
      x = readU16LE(payload, posRef.pos + 2);
      y = readU16LE(payload, posRef.pos + 4);
      posRef.pos += 6;
    } else {
      continue;
    }
    if (!x || !y) continue;
    const key = `${actorResource}@${x},${y}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({
      x,
      y,
      name: displayName,
      actorResource,
      scriptName,
      orientation,
    });
    off = Math.max(off, posRef.pos - 1);
  }
  return out;
}

export function isTeleportStoneActor(actorResource: string): boolean {
  // n_teleporter is the edge/named portal FX (stamped by client), not a stone.
  return actorResource.toLowerCase().includes("telestone");
}

/**
 * Locate n_telestone by record signature (not loose type-2 walk-back).
 *
 * Authored layout (29/29 SCE hits):
 *   kind(=5) | x | y | 04 00 | 01 00 | 19 00 | … | field 0x1a name | … | field 3 actor
 *
 * Loose "nearest type-2 / kind+x+y" falsely matched scalar `01 00 19 00 yy`
 * as coords (x=25,y=yy) → marker stuck near the map top-left.
 */
export function parseSceTeleportStones(payload: Uint8Array): SceInteractive[] {
  const out: SceInteractive[] = [];
  const seen = new Set<string>();
  const marker = "n_telestone.actor";
  const needle = new TextEncoder().encode(marker);
  for (let i = 0; i + needle.length <= payload.length; i++) {
    let ok = true;
    for (let k = 0; k < needle.length; k++) {
      if (payload[i + k] !== needle[k]) {
        ok = false;
        break;
      }
    }
    if (!ok) continue;

    let x = 0;
    let y = 0;
    let name = "传送石";
    for (let back = i - 12; back >= Math.max(0, i - 120); back -= 2) {
      if (back + 12 > payload.length) continue;
      const kind = readU16LE(payload, back);
      if (kind < 1 || kind > 32) continue;
      if (
        readU16LE(payload, back + 6) !== 4 ||
        readU16LE(payload, back + 8) !== 1 ||
        readU16LE(payload, back + 10) !== 0x19
      ) {
        continue;
      }
      const px = readU16LE(payload, back + 2);
      const py = readU16LE(payload, back + 4);
      if (px < 16 || py < 16 || px >= 4096 || py >= 4096) continue;
      x = px;
      y = py;
      break;
    }
    if (!x || !y) continue;

    for (let back = i - 5; back >= Math.max(0, i - 64); back--) {
      if (
        readU16LE(payload, back) === 3 &&
        readU16LE(payload, back + 2) === 0x1a &&
        back + 5 < i
      ) {
        const posRef = { pos: back };
        const s = readSceStringField(payload, posRef, 0x1a);
        if (s && s.length >= 2 && s.length < 30) {
          name = s;
          break;
        }
      }
    }

    const key = `${marker}@${x},${y}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({
      x,
      y,
      name,
      actorResource: marker,
      scriptName: "telestone.xse",
      orientation: 0,
    });
  }
  return out;
}

/**
 * Scan SCE2 payload for combat actors.
 * Dual-actor: field 0x11 = battle body; field 3 = map companion.
 */
export function parseSceCombatSpawns(payload: Uint8Array): SceCombatSpawn[] {
  const start = scePayloadStart(payload);
  const out: SceCombatSpawn[] = [];
  for (let off = start; off + 14 <= payload.length; off++) {
    if (readU16LE(payload, off) !== 3) continue;
    let pos = off + 2;
    const x = readU16LE(payload, pos);
    const y = readU16LE(payload, pos + 2);
    pos += 4;
    if (!x || !y || pos + 8 > payload.length) continue;
    const metaKind = readU16LE(payload, pos);
    if (
      (metaKind !== 5 && metaKind !== 6) ||
      readU16LE(payload, pos + 4) !== 0x0e
    ) {
      continue;
    }
    const sceActorId = readU16LE(payload, pos + 6);
    pos += 8;
    if (!sceActorId) continue;
    const posRef = { pos };
    const name = readSceStringField(payload, posRef, 0x0f);
    if (!name) continue;
    pos = posRef.pos;
    if (pos + 6 > payload.length || readU16LE(payload, pos) !== 1) continue;
    const field = readU16LE(payload, pos + 2);
    const visualHint = readU16LE(payload, pos + 4);
    pos += 6;
    if (field !== 0x10) continue;
    posRef.pos = pos;
    const battleActorResource = readSceStringField(payload, posRef, 0x11);
    if (!battleActorResource?.toLowerCase().endsWith(".actor")) continue;
    let mapActorResource = "";
    pos = posRef.pos;
    if (
      pos + 5 <= payload.length &&
      readU16LE(payload, pos) === 3 &&
      readU16LE(payload, pos + 2) === 3
    ) {
      const trail = { pos };
      const dual = readSceStringField(payload, trail, 3);
      if (dual?.toLowerCase().endsWith(".actor")) {
        mapActorResource = dual;
        posRef.pos = trail.pos;
      }
    }
    out.push({
      sceActorId,
      x,
      y,
      name,
      battleActorResource,
      mapActorResource: mapActorResource || battleActorResource,
      visualHint,
    });
    if (posRef.pos > off) off = posRef.pos - 1;
  }
  return out;
}

/** Point-in-rect with optional margin (mock find_sce_edge_portal_at_pos). */
export function portalContains(
  p: ScePortal,
  x: number,
  y: number,
  margin = 0,
): boolean {
  return (
    x >= p.left - margin &&
    x <= p.right + margin &&
    y >= p.top - margin &&
    y <= p.bottom + margin
  );
}
