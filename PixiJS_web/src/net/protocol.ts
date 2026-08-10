import { BUNDLED_CLIENT_CODE_VERSION } from "./bundledCodeVersion";
import {
  SeqReader,
  SeqWriter,
  WtBuilder,
  decodeAsciiDigitResult,
  decodeFieldString,
  decodeTaggedNumber,
  decodeWtPacket,
  findField,
  type WtObject,
  type WtPacket,
} from "./wt";

export type ServerRow = {
  name: string;
  label: string;
  serverId: number;
  color: number;
};

export type RoleRow = {
  roleId: number;
  jobIndex: number;
  /** 0=male 1=female (normalized). Title wire uses sexGroup 1..2. */
  sex: number;
  name: string;
  level: number;
};

export type NpcActor = {
  actorId: number;
  x: number;
  y: number;
  name: string;
  actorResource: string;
  scriptName: string;
  /** SCE combat enemy id for 4/1 when actorId is a local unique marker. */
  battleActorId?: number;
  /** SCE field 0x11 battle body (.actor); map uses actorResource / dual. */
  battleActorResource?: string;
};

export type PeerActor = {
  actorId: number;
  name: string;
  x: number;
  y: number;
  visualVariant: number;
  visualGroup: number;
  /** otherinfo shortLabel — designation display name. */
  title?: string;
  /** otherinfo longLabel — riches_nameN.gif / level_nameN.gif. */
  titleGif?: string;
  /**
   * Pixi companion field `plus16info` on 2/10 / 2/7:
   * worn slots with enhance >= 16 (0..8). Absent when field missing.
   */
  plus16Count?: number;
};

/** 1/2/2 peer timeline: outer x/y = start; dirs 1..4 advance 4px (0=idle). */
export type PeerMoveUpdate = {
  actorId: number;
  x: number;
  y: number;
  dirs: number[];
};

export type BattleSide = {
  id: number;
  hp: number;
  maxHp: number;
  mp: number;
  maxMp: number;
  name?: string;
  visual0?: number;
  visual1?: number;
  /** Battle-body .actor resource for mmBattle DrawBattleCharacter. */
  actorResource?: string;
  jobIndex?: number;
  sex?: number;
};

export type BattleState = {
  subtype: 5 | 10;
  side: number;
  left: BattleSide[];
  right: BattleSide[];
  sceneMonster?: { index: number; x: number; y: number };
};

/** Optional names when enriching subtype-5 from 1/2/2 + SCE. */
export type BattleStartContext = {
  roleName?: string;
  monsterNameFor?: (actorId: number) => string | undefined;
  /** Team roster for right-side party labels / visuals. */
  partyMembers?: {
    id: number;
    name: string;
    jobIndex?: number;
    sex?: number;
  }[];
};

/** Monster vitals seeded by companion 1/2/2 moveinfo (blob+44..+56 LE). */
export type SceneMoveSeed = {
  actorId: number;
  x: number;
  y: number;
  hp: number;
  mp: number;
  hpMax: number;
  mpMax: number;
};

function readU32LE(buf: Uint8Array, off: number): number {
  if (off + 4 > buf.length) return 0;
  return (
    (buf[off]! |
      (buf[off + 1]! << 8) |
      (buf[off + 2]! << 16) |
      (buf[off + 3]! << 24)) >>>
    0
  );
}

/**
 * Parse 1/2/2 moveinfo seeds from a battle-start (or poll) packet.
 * CBE copies hp/mp/max from raw_move_blob+44/+48/+52/+56 into the scene node
 * that subtype-5 then uses for the left fighter.
 */
export function parseSceneMoveSeeds(packet: WtPacket): SceneMoveSeed[] {
  const out: SceneMoveSeed[] = [];
  for (const obj of packet.objects) {
    if (obj.kind !== 2 || obj.subtype !== 2) continue;
    const raw = findField(obj, "moveinfo");
    if (!raw || raw.length < 12) continue;
    try {
      const r = new SeqReader(raw);
      const x = r.i16();
      const y = r.i16();
      const actorId = r.u32();
      if (r.remaining() < 2) continue;
      const blobLen = (raw[r.pos]! << 8) | raw[r.pos + 1]!;
      r.pos += 2;
      if (blobLen > r.remaining()) continue;
      const blob = raw.subarray(r.pos, r.pos + blobLen);
      out.push({
        actorId,
        x,
        y,
        hp: readU32LE(blob, 44),
        mp: readU32LE(blob, 48),
        hpMax: readU32LE(blob, 52),
        mpMax: readU32LE(blob, 56),
      });
    } catch {
      /* skip malformed seed */
    }
  }
  return out;
}

/**
 * Map actioninfo wire slot → left/right unit for side=1 layouts used by mock.
 *
 * subtype 10: wire 0 = right[0], wire 1+ = left[i-1].
 *
 * subtype 5 solo (party=1): monsters use visual-order wires
 *   N=1:[0] N=2:[2,0] N=3:[2,3,0]; player wire 1.
 *
 * subtype 5 team (party>=2): mmBattle sub_6BF0 + CalcTargetSideIndex
 *   display 0..P-1 = party (battleinfo order), P.. = monsters
 *   display_to_wire(d) = d >= M ? d - M : P + d
 *   wire_to_display(w) = w < P ? M + w : w - P
 * Evidence: docs/re/2026-06-25-battle-server-flow.md (M=1,P=2 →
 *   leader wire2, member wire0, monster wire1).
 */
export function battleUnitForWire(
  state: BattleState,
  wire: number,
): BattleSide | null {
  if (state.subtype === 10) {
    if (wire === 0) return state.right[0] ?? null;
    return state.left[wire - 1] ?? null;
  }
  const P = state.right.length;
  const M = state.left.length;
  if (P >= 2 && M >= 1) {
    const display = wire < P ? M + wire : wire - P;
    if (display < 0) return null;
    if (display < P) return state.right[display] ?? null;
    return state.left[display - P] ?? null;
  }
  // subtype 5 solo, side=1
  const leftWires =
    M === 1 ? [0] : M === 2 ? [2, 0] : M === 3 ? [2, 3, 0] : [0];
  const li = leftWires.indexOf(wire);
  if (li >= 0) return state.left[li] ?? null;
  if (wire === 1) return state.right[0] ?? null;
  return state.right.find((_, i) => i + 1 === wire) ?? null;
}

/** display index → actioninfo wire (team subtype-5). */
export function battleDisplayToWire(
  display: number,
  partyCount: number,
  monsterCount: number,
): number {
  return display >= monsterCount
    ? display - monsterCount
    : partyCount + display;
}

/** Request index for 4/2 — wire slot of first living left unit. */
export function battleTargetWire(state: BattleState): number {
  const P = state.right.length;
  const M = state.left.length;
  const idx = state.left.findIndex((e) => e.hp > 0);
  const mi = idx >= 0 ? idx : 0;
  if (state.subtype === 5 && P >= 2 && M >= 1) {
    return battleDisplayToWire(P + mi, P, M);
  }
  const leftWires =
    state.subtype === 5
      ? M === 1
        ? [0]
        : M === 2
          ? [2, 0]
          : M === 3
            ? [2, 3, 0]
            : [0]
      : state.left.map((_, i) => i + 1);
  const wire = leftWires[mi];
  // subtype-10: wire 0 is the player — never use it as a monster fallback.
  if (wire == null) {
    return state.subtype === 10 ? 1 : 0;
  }
  return wire;
}

/**
 * WT 1/1/12 — matches CBE net_build_login_request / PHP regression helpers.
 * Service gate `vm_net_mock_is_login_validation_request` requires
 * coreVer/appVer/userName|username/password/imsi field names present.
 * Guest first-run: empty userName+password blobs (BE16 len=0 inside value).
 * `codeVersion` must match server `server_code_version.txt` / CBE_CODE_VERSION.
 */
export function buildLoginAlt12Request(
  user = "",
  password = "",
  codeVersion = BUNDLED_CLIENT_CODE_VERSION,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 1, 12)
    .string("coreVer", "1")
    .string("appVer", "1")
    .string("imsi", "pixijs-web")
    .string("userName", user)
    .string("password", password)
    .u32("codeVersion", codeVersion >>> 0)
    .u8("newVer", 0);
  return b.finish();
}

function parseCodeVersionText(text: string): number | null {
  const token = text.trim().split(/\s+/)[0] ?? "";
  const n = Number.parseInt(token, 10);
  return Number.isFinite(n) && n >= 0 ? n >>> 0 : null;
}

/**
 * Load client codeVersion for WT 1/1/12.
 * Prefer relative URLs (`base: './'` / Capacitor); never fall back to a
 * stale hard-coded 2 when the live server gate expects a newer value.
 */
export async function loadClientCodeVersion(
  fallback = BUNDLED_CLIENT_CODE_VERSION,
): Promise<number> {
  const urls = [
    "./client_code_version.txt",
    "client_code_version.txt",
    "/client_code_version.txt",
  ];
  for (const url of urls) {
    try {
      const res = await fetch(url, { cache: "no-store" });
      if (!res.ok) continue;
      const parsed = parseCodeVersionText(await res.text());
      if (parsed != null) return parsed;
    } catch {
      /* try next candidate */
    }
  }
  return fallback;
}

export function buildServerSelectRequest(serverId: number, moneytype = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 1, 4).u32("serverID", serverId).u32("moneytype", moneytype);
  return b.finish();
}

export function buildRoleListRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 1, 16).u8("type", 0);
  return b.finish();
}

export function buildRoleSelectRequest(actorId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 1, 6).u32("actorID", actorId);
  return b.finish();
}

/** Title create-role cap (mock `VM_NET_MOCK_ROLE_DB_MAX_ROLES`). */
export const TITLE_ROLE_MAX = 5;

/**
 * WT 1/1/7 title role create (mmTitle:0x3E66).
 * Named fields: mock named-path expects career job 1..3 and sexGroup 1..2.
 */
export function buildRoleCreateRequest(
  jobIndex: number,
  sex: number,
  name: string,
): Uint8Array {
  const job = Math.max(0, Math.min(2, jobIndex | 0)) + 1;
  const sexGroup = sex ? 2 : 1;
  const trimmed = name.trim().slice(0, 14);
  const b = new WtBuilder();
  b.object(1, 1, 7).u8("job", job).u8("sex", sexGroup).string("name", trimmed);
  return b.finish();
}

export type RoleCreateResult = { actorId: number; result: number };

/** Response 1/1/7: result==0 success (mmTitle:0x5324). */
export function parseRoleCreateResult(packet: WtPacket): RoleCreateResult | null {
  const obj = packet.objects.find((o) => o.kind === 1 && o.subtype === 7);
  if (!obj) return null;
  const actorRaw = findField(obj, "actorid") || findField(obj, "actorID");
  const resultRaw = findField(obj, "result");
  if (!actorRaw && !resultRaw) return null;
  const actorId = actorRaw ? decodeTaggedNumber(actorRaw) ?? 0 : 0;
  const result = resultRaw
    ? decodeAsciiDigitResult(resultRaw) ?? decodeTaggedNumber(resultRaw) ?? 1
    : 1;
  return { actorId: actorId >>> 0, result: result & 0xff };
}

/** Synthetic 1/1/7 create ack for smoke / demos. */
export function demoBuildRoleCreate(actorId: number, result: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 1, 7).u32("actorid", actorId >>> 0).u8("result", result & 0xff);
  return b.finishResponse();
}

/** WT 1/1/8 title role delete (mmTitle:0x1F90). */
export function buildRoleDeleteRequest(actorId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 1, 8).u32("actorID", actorId >>> 0);
  return b.finish();
}

/** Response 1/1/8: result==0 success (mmTitle:0x53EC). */
export function parseRoleDeleteResult(packet: WtPacket): number | null {
  const obj = packet.objects.find((o) => o.kind === 1 && o.subtype === 8);
  if (!obj) return null;
  const resultRaw = findField(obj, "result");
  if (!resultRaw) return null;
  return decodeAsciiDigitResult(resultRaw) ?? decodeTaggedNumber(resultRaw) ?? 1;
}

export function demoBuildRoleDelete(result: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 1, 8).u8("result", result & 0xff);
  return b.finishResponse();
}

/** Login bootstrap: 5/10 + 7/7 type=1 (before scene resource follow-up). */
export function buildGroupType1Request(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 5, 10);
  b.object(1, 7, 7).u8("type", 1);
  return b.finish();
}

/**
 * First-scene resource/task follow-up (WT len=49).
 * Objects: 12/1, 7/42, 6/1, 6/13, 6/14, 2/10{Type=101}, 25/5.
 * Server only appends learned skills when the request contains empty 12/1+7/42
 * (`includeSkillBooks`). Len-39 task-only form deliberately omits them.
 */
export function buildSceneStartupFollowupRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 0x0c, 1);
  b.object(1, 7, 42);
  b.object(1, 6, 1);
  b.object(1, 6, 13);
  b.object(1, 6, 14);
  b.object(1, 2, 10).u8("Type", 101);
  b.object(1, 0x19, 5);
  return b.finish();
}

/**
 * Post-transfer NPC catalog owner: `25/5 + 2/3{map*} + 27/11 + 7/42`.
 * Evidence: mock `is_scene_change_post_enter_followup_request` /
 * docs/re/2026-07-24-scene-npc-return-investigation.md —
 * portal `2/3` answers empty 27/11; this follow-up runs against the new shell
 * and delivers nonempty `npcnum/npcinfo`.
 */
export function buildSceneChangePostEnterFollowupRequest(
  mapId: string,
  exitId: number,
  mapType = 2,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 0x19, 5);
  b.object(1, 2, 3)
    .u8("maptype", mapType)
    .string("mapID", mapId)
    .u32("exitID", exitId >>> 0);
  b.object(1, 0x1b, 11);
  b.object(1, 7, 42);
  return b.finish();
}

/**
 * @deprecated Prefer buildSceneChangePostEnterFollowupRequest for portal transfers.
 * Len≈39 task-subset after map-stone wait_wt6 (still used by some stone paths).
 */
export function buildSceneNpcCatalogFollowupRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 6, 1);
  b.object(1, 6, 13);
  b.object(1, 6, 14);
  b.object(1, 2, 10).u8("Type", 101);
  b.object(1, 0x19, 5);
  return b.finish();
}

/** Login-tail / skill refresh: empty 12/1 + 7/42 (WT len=14). */
export function buildLearnedSkillsRefreshRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 0x0c, 1);
  b.object(1, 7, 42);
  return b.finish();
}

export function buildBattleStartRequest(
  id: number,
  index: number,
  posx: number,
  posy: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 4, 1)
    .u32("id", id)
    .u32("index", index)
    .u32("posx", posx)
    .u32("posy", posy);
  return b.finish();
}

export function parseServerList(packet: WtPacket): {
  result: number;
  servers: ServerRow[];
  username?: string;
  password?: string;
  information?: string;
} {
  const obj = packet.objects.find((o) => o.kind === 1 && o.subtype === 12);
  if (!obj) return { result: 0, servers: [] };
  const resultRaw = findField(obj, "result");
  const result = resultRaw ? decodeAsciiDigitResult(resultRaw) ?? 0 : 0;
  const infoRaw = findField(obj, "serverinfo");
  const numRaw = findField(obj, "servernum");
  const servernum = numRaw ? decodeAsciiDigitResult(numRaw) ?? decodeTaggedU16(numRaw) : 0;
  const servers = infoRaw ? parseServerInfoBlob(infoRaw, servernum || 8) : [];
  const username =
    optionalString(obj, "username") || optionalString(obj, "userName");
  const password = optionalString(obj, "password");
  const information = optionalString(obj, "information");
  return { result, servers, username, password, information };
}

function decodeTaggedU16(raw: Uint8Array): number {
  if (raw.length >= 4 && raw[0] === 0 && raw[1] === 2) {
    return (raw[2]! << 8) | raw[3]!;
  }
  return 0;
}

function optionalString(obj: WtObject, name: string): string | undefined {
  const raw = findField(obj, name);
  return raw ? decodeFieldString(raw) : undefined;
}

function parseServerInfoBlob(raw: Uint8Array, maxRows: number): ServerRow[] {
  const rows: ServerRow[] = [];
  const r = new SeqReader(raw);
  try {
    while (r.remaining() > 4 && rows.length < maxRows) {
      const name = r.string();
      const label = r.string();
      const serverId = r.u32();
      // u24 color: 0x00 0x04 + 3 bytes
      let color = 0;
      if (r.buf[r.pos] === 0 && r.buf[r.pos + 1] === 4) {
        color =
          ((r.buf[r.pos + 2]! << 16) | (r.buf[r.pos + 3]! << 8) | r.buf[r.pos + 4]!) >>> 0;
        r.pos += 5;
      }
      rows.push({ name, label, serverId, color });
    }
  } catch {
    /* truncated row ok */
  }
  return rows;
}

export function parseRoleListFromSelect(packet: WtPacket): RoleRow[] {
  // 1/1/4 embeds actorinfo role list
  const obj =
    packet.objects.find((o) => o.kind === 1 && o.subtype === 4) ||
    packet.objects.find((o) => o.kind === 1 && o.subtype === 16);
  if (!obj) return [];
  const raw = findField(obj, "actorinfo");
  if (!raw) return [];
  return parseTitleRoleList(raw);
}

/** True when server-select carried compact actorinfo (including count=0). */
export function packetHasTitleRoleList(packet: WtPacket): boolean {
  const obj = packet.objects.find((o) => o.kind === 1 && o.subtype === 4);
  return !!(obj && findField(obj, "actorinfo"));
}

export function parseTitleRoleList(raw: Uint8Array): RoleRow[] {
  const r = new SeqReader(raw);
  const count = r.u8();
  const rows: RoleRow[] = [];
  for (let i = 0; i < count && r.remaining() > 0; i++) {
    const roleId = r.u32();
    const jobIndex = r.u8();
    // Title compact actorinfo (mmTitle:0x3544): sex is sexGroup 1..2, not DB 0..1.
    // actorResourceForJobSex expects 0=male 1=female — raw 1 would pick female assets.
    const sexGroup = r.u8();
    rows.push({
      roleId,
      jobIndex,
      sex: sexGroup >= 1 ? (sexGroup - 1) & 1 : 0,
      name: r.string(),
      level: r.i16(),
    });
  }
  return rows;
}

/** Mock may persist extensionless c-keys (`c04临安府_06`); assets need `.sce`. */
export function ensureSceneFile(scene: string): string {
  if (!scene) return scene;
  return /\.sce$/i.test(scene) ? scene : `${scene}.sce`;
}

/**
 * Disk / sMap aliases: sMap often stores `4临安府_10.sce` while JHOnlineData
 * has `c04临安府_10.sce`. Try both directions when fetching.
 */
export function sceneAssetCandidates(scene: string): string[] {
  const primary = ensureSceneFile(scene);
  const stem = primary.replace(/\.sce$/i, "");
  const out: string[] = [primary];
  if (/^\d/.test(stem)) {
    out.push(`c${stem}.sce`);
    const m = stem.match(/^(\d+)([\s\S]*)$/);
    if (m && m[1]!.length === 1) out.push(`c0${m[1]}${m[2]}.sce`);
    if (m && m[1]!.length === 2) out.push(`c${m[1]}${m[2]}.sce`);
  }
  if (/^c\d/i.test(stem)) {
    out.push(`${stem.slice(1)}.sce`);
  }
  return [...new Set(out)];
}

export function parseSceneEnter(packet: WtPacket): { scene: string; x: number; y: number } | null {
  const obj = packet.objects.find((o) => o.kind === 0x1e && o.subtype === 1);
  if (!obj) return null;
  const sceneRaw = findField(obj, "scene");
  const posRaw = findField(obj, "posinfo");
  if (!sceneRaw || !posRaw) return null;
  const scene = ensureSceneFile(decodeFieldString(sceneRaw));
  const r = new SeqReader(posRaw);
  const x = r.i16();
  const y = r.i16();
  return { scene, x, y };
}

/**
 * Title role-select `1/1/6` carries full scene/login actorinfo (not 30/1).
 * Layout: mock `vm_net_mock_build_actor_info_ex` fresh path → trailing
 * sceneKey + motion i16 grid (role x/y). Evidence: docs/re local-role-db +
 * mock_server_interaction_login.c title-role-select → 1/1/6+1/1/15.
 */
export type RoleSelectEnter = {
  scene: string;
  x: number;
  y: number;
  hp: number;
  hpMax: number;
  mp: number;
  mpMax: number;
  name: string;
  level: number;
  /** Durable total EXP from actorinfo (4/7 panel baseline). */
  exp: number;
  /** Durable money/copper from actorinfo. */
  money: number;
  /** Actor shortLabel display name (e.g. 一贫如洗). */
  title: string;
  /**
   * Overhead badge resource at actor+0x10A (`previewImage`):
   * `riches_nameN.gif` / `level_nameN.gif` — not human text.
   */
  titleGif: string;
};

/** Shared early actorinfo vitals (title 1/1/6 and map sync 1/1/14). */
function readActorInfoVitals(
  raw: Uint8Array,
): { hp: number; hpMax: number; mp: number; mpMax: number } | null {
  if (raw.length < 32) return null;
  try {
    const r = new SeqReader(raw);
    r.u32(); // roleId
    r.u8(); // visualVariant
    r.u8(); // visualGroup
    r.string(); // name
    r.u32(); // guildId
    r.string(); // displayName
    r.u32(); // summaryStatus
    const hp = r.u32();
    const hpMax = r.u32();
    const mp = r.u32();
    const mpMax = r.u32();
    return {
      hp,
      hpMax: hpMax || hp || 1,
      mp,
      mpMax: mpMax || mp || 1,
    };
  } catch {
    return null;
  }
}

/**
 * Map vitals sync `1/1/14` actorinfo (item use / map heal / revival).
 * Evidence: docs/re/2026-07-25-revival-map-vitals-1-1-14.md
 */
export function parseMapActorVitals(
  packet: WtPacket,
): { hp: number; hpMax: number; mp: number; mpMax: number } | null {
  const obj = packet.objects.find((o) => o.kind === 1 && o.subtype === 14);
  if (!obj) return null;
  const raw = findField(obj, "actorinfo");
  if (!raw) return null;
  return readActorInfoVitals(raw);
}

export function parseRoleSelectEnter(packet: WtPacket): RoleSelectEnter | null {
  const obj = packet.objects.find((o) => o.kind === 1 && o.subtype === 6);
  if (!obj) return null;
  const raw = findField(obj, "actorinfo");
  if (!raw || raw.length < 32) return null;
  try {
    const r = new SeqReader(raw);
    r.u32(); // roleId
    r.u8(); // visualVariant
    r.u8(); // visualGroup
    const name = r.string().replace(/\0+$/g, "");
    r.u32(); // guildId
    r.string(); // displayName
    r.u32(); // summaryStatus / level-ish
    const hp = r.u32();
    const hpMax = r.u32();
    const mp = r.u32();
    const mpMax = r.u32();
    r.u32(); // charm
    for (let i = 0; i < 6; i++) r.u32();
    const exp = r.u32();
    const money = r.u32(); // money / gap09C0
    r.u8(); // backpackCapacity
    r.u8(); // state1
    r.u32(); // hpDisplayMax
    r.u32(); // mpDisplayMax
    r.u8(); // target-ish
    r.u8();
    const title = r.string().replace(/\0+$/g, "");
    const titleGif = r.string().replace(/\0+$/g, "");
    r.u32();
    r.u32();
    r.u32();
    r.string(); // actorResource
    const level = r.i16();
    let scene = r.string().replace(/\0+$/g, "");
    const x = r.i16();
    const y = r.i16();
    // Fresh actorinfo may use extensionless c-prefixed keys (`c04临安府_06`)
    // after normalize_scene_name_for_enter; still a valid enter scene.
    if (!scene) return null;
    scene = ensureSceneFile(scene);
    return {
      scene,
      x,
      y,
      hp,
      hpMax: hpMax || hp || 1,
      mp,
      mpMax: mpMax || mp || 1,
      name,
      level,
      exp,
      money,
      title,
      titleGif,
    };
  } catch {
    return null;
  }
}

export function parseNpcCatalog(packet: WtPacket): NpcActor[] {
  const obj = packet.objects.find((o) => o.kind === 0x1b && o.subtype === 11);
  if (!obj) return [];
  const numRaw = findField(obj, "npcnum");
  const infoRaw = findField(obj, "npcinfo");
  if (!infoRaw) return [];
  const count = numRaw ? (decodeAsciiDigitResult(numRaw) ?? numRaw[2] ?? 0) : 0;
  const r = new SeqReader(infoRaw);
  const list: NpcActor[] = [];
  const n = count || 32;
  for (let i = 0; i < n && r.remaining() > 8; i++) {
    try {
      const actorId = r.u32();
      const x = r.u32();
      const y = r.u32();
      const name = r.string();
      const actorResource = r.string();
      const scriptName = r.string();
      r.string(); // dynamic resource key
      r.u32(); // repeated id
      list.push({ actorId, x, y, name, actorResource, scriptName });
    } catch {
      break;
    }
  }
  return list;
}

/** True when the packet carries a 27/11 object (including empty npcnum=0 gate). */
export function packetHasNpcCatalog(packet: WtPacket): boolean {
  return packet.objects.some((o) => o.kind === 0x1b && o.subtype === 11);
}

/** Pixi-only 2/10|2/7 `plus16info`: repeat { u32 actorId, u8 count }. */
export function parsePlus16Info(
  raw: Uint8Array | null | undefined,
): Map<number, number> {
  const map = new Map<number, number>();
  if (!raw || raw.length < 5) return map;
  const r = new SeqReader(raw);
  while (r.remaining() >= 5) {
    try {
      const actorId = r.u32() >>> 0;
      const count = r.u8() & 0xff;
      map.set(actorId, Math.max(0, Math.min(8, count)));
    } catch {
      break;
    }
  }
  return map;
}

export function applyPlus16Info(
  peers: PeerActor[],
  plus16Raw: Uint8Array | null | undefined,
): PeerActor[] {
  const counts = parsePlus16Info(plus16Raw);
  if (counts.size === 0) return peers;
  for (const p of peers) {
    const n = counts.get(p.actorId >>> 0);
    if (n !== undefined) p.plus16Count = n;
  }
  return peers;
}

export function parseNearbyPeers(packet: WtPacket): PeerActor[] {
  const obj = packet.objects.find((o) => o.kind === 2 && o.subtype === 10);
  if (!obj) return [];
  const numRaw = findField(obj, "othernum");
  const infoRaw = findField(obj, "otherinfo");
  if (!infoRaw) return [];
  const count = numRaw
    ? ((numRaw[0] === 0 && numRaw[1] === 4
        ? ((numRaw[2]! << 24) | (numRaw[3]! << 16) | (numRaw[4]! << 8) | numRaw[5]!) >>> 0
        : decodeAsciiDigitResult(numRaw)) ?? 0)
    : 0;
  const r = new SeqReader(infoRaw);
  const list: PeerActor[] = [];
  for (let i = 0; i < count && r.remaining() > 4; i++) {
    try {
      const actorId = r.u32();
      const visualVariant = r.u8();
      const visualGroup = r.u8();
      const labelText = r.string();
      r.u8(); // targetPosX
      r.u8(); // targetPosY
      const shortLabel = r.string().replace(/\0+$/g, "");
      const longLabel = r.string().replace(/\0+$/g, "");
      const gridPosX = r.i16();
      const gridPosY = r.i16();
      list.push({
        actorId,
        name: labelText.replace(/\0+$/g, ""),
        x: gridPosX,
        y: gridPosY,
        visualVariant,
        visualGroup,
        title: shortLabel,
        titleGif: longLabel,
      });
    } catch {
      break;
    }
  }
  return applyPlus16Info(list, findField(obj, "plus16info"));
}

/**
 * Peer movement deltas from scene-sync 1/2/2 moveinfo.
 * Timeline contract (docs/re/2026-07-10-nearby-player-movement-sync.md):
 *   i16 startX, i16 startY, u32 actorId, len16 raw (dirs 0..4, len<=10).
 * Monster vitals blobs (len>=44) and opaque non-timeline payloads are skipped.
 */
export function parsePeerMoveUpdates(packet: WtPacket): PeerMoveUpdate[] {
  const out: PeerMoveUpdate[] = [];
  for (const obj of packet.objects) {
    if (obj.kind !== 2 || obj.subtype !== 2) continue;
    const raw = findField(obj, "moveinfo");
    if (!raw || raw.length < 10) continue;
    try {
      const r = new SeqReader(raw);
      const x = r.i16();
      const y = r.i16();
      const actorId = r.u32();
      if (r.remaining() < 2) continue;
      const blobLen = (raw[r.pos]! << 8) | raw[r.pos + 1]!;
      r.pos += 2;
      if (blobLen > r.remaining()) continue;
      const blob = raw.subarray(r.pos, r.pos + blobLen);
      // Empty blob = position-only publish (baseline / idle).
      if (blobLen === 0) {
        out.push({ actorId, x, y, dirs: [] });
        continue;
      }
      // Monster seed blobs carry LE vitals at +44..; not a peer timeline.
      if (blobLen > 10) continue;
      let timeline = true;
      for (let i = 0; i < blob.length; i++) {
        if (blob[i]! > 4) {
          timeline = false;
          break;
        }
      }
      if (!timeline) continue;
      out.push({ actorId, x, y, dirs: Array.from(blob) });
    } catch {
      /* skip malformed */
    }
  }
  return out;
}

export function parseRemovedPeers(packet: WtPacket): number[] {
  const ids: number[] = [];
  for (const obj of packet.objects) {
    if (obj.kind !== 2 || obj.subtype !== 6) continue;
    const raw = findField(obj, "actorid") || findField(obj, "actorID");
    if (!raw) continue;
    if (raw[0] === 0 && raw[1] === 4 && raw.length >= 6) {
      ids.push(
        ((raw[2]! << 24) | (raw[3]! << 16) | (raw[4]! << 8) | raw[5]!) >>> 0,
      );
    }
  }
  return ids;
}

export function parseBattleStart(
  packet: WtPacket,
  ctx?: BattleStartContext,
): BattleState | null {
  const obj5 = packet.objects.find((o) => o.kind === 4 && o.subtype === 5);
  const obj10 = packet.objects.find((o) => o.kind === 4 && o.subtype === 10);
  const obj = obj5 || obj10;
  if (!obj) return null;
  const subtype = (obj5 ? 5 : 10) as 5 | 10;
  const sideRaw = findField(obj, "side");
  const side = sideRaw ? decodeAsciiDigitResult(sideRaw) ?? sideRaw[2] ?? 1 : 1;
  const info = findField(obj, "battleinfo");
  if (!info) return { subtype, side, left: [], right: [] };

  const seeds = parseSceneMoveSeeds(packet);
  if (subtype === 5) {
    return parseBattleInfoScene(info, side, seeds, ctx);
  }
  return parseBattleInfoPve(info, side, ctx);
}

function parseBattleInfoScene(
  info: Uint8Array,
  side: number,
  seeds: SceneMoveSeed[],
  ctx?: BattleStartContext,
): BattleState {
  const r = new SeqReader(info);
  const monsterCount = r.u8();
  const index = r.u32();
  const mx = r.u32();
  const my = r.u32();
  const partyCount = r.u8();
  const roleName = ctx?.roleName?.trim() || "我";
  const party = ctx?.partyMembers ?? [];
  const right: BattleSide[] = [];
  for (let i = 0; i < partyCount && r.remaining() > 0; i++) {
    const id = r.u32();
    const hp = r.u32();
    const maxHp = r.u32();
    const mp = r.u32();
    const maxMp = r.u32();
    const mate =
      party.find((p) => p.id === id) ||
      (i === 0 ? { id, name: roleName } : undefined) ||
      party[i];
    right.push({
      id,
      hp,
      maxHp,
      mp,
      maxMp,
      name: mate?.name || (i === 0 ? roleName : `队友${i + 1}`),
      jobIndex: mate?.jobIndex,
      sex: mate?.sex,
    });
  }

  // Prefer seed matching scene index / actor id; else first seed in packet.
  const seed =
    seeds.find((s) => s.actorId === index) ||
    seeds.find((s) => s.x === mx && s.y === my) ||
    seeds[0];
  const hp = seed?.hp ?? 0;
  const mp = seed?.mp ?? 0;
  const hpMax = seed?.hpMax || hp;
  const mpMax = seed?.mpMax || mp;
  const actorId = seed?.actorId ?? index;
  const name =
    ctx?.monsterNameFor?.(actorId) ||
    ctx?.monsterNameFor?.(index) ||
    `怪#${actorId}`;

  // Multi-enemy scene fights clone ONE template (same catalog/wire id + vitals).
  // Do not invent index+i species keys — that made left seats resolve to other
  // map SCE kinds while HP/ATK stayed identical.
  const left: BattleSide[] = [];
  for (let i = 0; i < monsterCount; i++) {
    left.push({
      id: actorId,
      hp,
      maxHp: hpMax,
      mp,
      maxMp: mpMax,
      name: monsterCount > 1 ? `${name}#${i + 1}` : name,
    });
  }
  return {
    subtype: 5,
    side,
    left,
    right,
    sceneMonster: { index, x: mx, y: my },
  };
}

function parseBattleInfoPve(
  info: Uint8Array,
  side: number,
  ctx?: BattleStartContext,
): BattleState {
  const r = new SeqReader(info);
  const leftCount = r.u8();
  const left: BattleSide[] = [];
  for (let i = 0; i < leftCount; i++) {
    const id = r.u32();
    const hp = r.u32();
    const maxHp = r.u32();
    const mp = r.u32();
    const maxMp = r.u32();
    const name = r.string();
    const visual0 = r.u8(); // sexGroup 1..2
    const visual1 = r.u8(); // jobIndex 0..2
    left.push({
      id,
      hp,
      maxHp,
      mp,
      maxMp,
      name,
      visual0,
      visual1,
      sex: visual0 >= 1 ? (visual0 - 1) & 1 : 0,
      jobIndex: Math.max(0, Math.min(2, visual1)),
    });
  }
  const rightCount = r.u8();
  const roleName = ctx?.roleName?.trim() || "我";
  const right: BattleSide[] = [];
  for (let i = 0; i < rightCount; i++) {
    right.push({
      id: r.u32(),
      hp: r.u32(),
      maxHp: r.u32(),
      mp: r.u32(),
      maxMp: r.u32(),
      name: i === 0 ? roleName : `队友${i + 1}`,
    });
  }
  return { subtype: 10, side, left, right };
}

export function ingestPacket(buf: Uint8Array): WtPacket | null {
  if (buf.length < 5) return null;
  if (buf[0] !== 0x57 || buf[1] !== 0x54) return null;
  try {
    return decodeWtPacket(buf);
  } catch {
    return null;
  }
}

/** Demo helpers to synthesize responses offline. */
export function demoBuildServerList(): Uint8Array {
  const serverInfo = new SeqWriter()
    .string("TestZone")
    .string("OK")
    .u32(1)
    .finish();
  // append fake u24 color manually
  const color = new Uint8Array(serverInfo.length + 5);
  color.set(serverInfo, 0);
  color[serverInfo.length] = 0;
  color[serverInfo.length + 1] = 4;
  color[serverInfo.length + 2] = 0xc4;
  color[serverInfo.length + 3] = 0xa3;
  color[serverInfo.length + 4] = 0x5a;

  const b = new WtBuilder();
  b.object(1, 1, 12)
    .asciiDigit("result", 1)
    .raw("serverinfo", color)
    .raw("color", new Uint8Array([0, 4, 0xc4, 0xa3, 0x5a]))
    .u16("servernum", 1)
    .u8("newVer", 0)
    .string("information", "demo select server");
  return b.finishResponse();
}

export function demoBuildServerSelect(roles: RoleRow[]): Uint8Array {
  const list = new SeqWriter().u8(roles.length);
  for (const role of roles) {
    // Wire sexGroup = internal sex + 1 (same as mock title actorinfo).
    const sexGroup = role.sex >= 1 ? 2 : 1;
    list.u32(role.roleId).u8(role.jobIndex).u8(sexGroup).string(role.name).i16(role.level);
  }
  const b = new WtBuilder();
  b.object(1, 1, 4)
    .asciiDigit("result", 1)
    .raw("servconf", new Uint8Array([0, 4, 0xc4, 0xa3, 0x5a]))
    .raw("actorinfo", list.finish());
  return b.finishResponse();
}

export function demoBuildRoleEnter(role: RoleRow, scene: string, x: number, y: number): Uint8Array {
  const pos = new SeqWriter().i16(x).i16(y).finish();
  const b = new WtBuilder();
  b.object(1, 1, 6).asciiDigit("result", 1).u32("type", 0);
  b.object(1, 1, 15).asciiDigit("result", 1);
  b.object(1, 0x1e, 1).string("scene", scene).raw("posinfo", pos);
  const npcInfo = new SeqWriter()
    .u32(90001)
    .u32(x + 40)
    .u32(y - 20)
    .string("Blacksmith")
    .string("n_blacksmith.actor")
    .string("blacksmith")
    .string("n_blacksmith.actor")
    .u32(90001)
    .u32(90002)
    .u32(x - 50)
    .u32(y + 30)
    .string("Shop")
    .string("n_shop.actor")
    .string("shop")
    .string("n_shop.actor")
    .u32(90002)
    .finish();
  b.object(1, 0x1b, 11).u8("npcnum", 2).raw("npcinfo", npcInfo);
  // Role-bound learned skills (1/12/1) — same object family as live login tail.
  const starter =
    role.jobIndex === 1 ? 101 : role.jobIndex === 2 ? 201 : 1;
  const skillBlob = new SeqWriter().u32(starter).finish();
  b.object(1, 0x0c, 1)
    .u16("learnednum", 1)
    .raw("learnedskill", skillBlob);
  return b.finishResponse();
}

export function demoBuildBattle(monsterId: number, roleId: number): Uint8Array {
  const info = new SeqWriter()
    .u8(1)
    .u32(monsterId)
    .u32(120)
    .u32(120)
    .u32(30)
    .u32(30)
    .string("野猪")
    .u8(0)
    .u8(1)
    .u8(1)
    .u32(roleId)
    .u32(500)
    .u32(500)
    .u32(200)
    .u32(200)
    .finish();
  const b = new WtBuilder();
  b.object(1, 4, 10).u8("side", 1).raw("battleinfo", info);
  return b.finishResponse();
}

/** Demo 1/2/2 moveinfo seed (LE vitals at blob+44..) + subtype-5 start. */
export function demoBuildBattleScene(opts: {
  monsterId: number;
  roleId: number;
  roleName?: string;
  hp?: number;
  hpMax?: number;
  mp?: number;
  mpMax?: number;
  x?: number;
  y?: number;
}): Uint8Array {
  const hp = opts.hp ?? 120;
  const hpMax = opts.hpMax ?? hp;
  const mp = opts.mp ?? 30;
  const mpMax = opts.mpMax ?? mp;
  const x = opts.x ?? 100;
  const y = opts.y ?? 200;
  const state = new Uint8Array(64);
  const storeLe = (off: number, v: number) => {
    state[off] = v & 0xff;
    state[off + 1] = (v >>> 8) & 0xff;
    state[off + 2] = (v >>> 16) & 0xff;
    state[off + 3] = (v >>> 24) & 0xff;
  };
  storeLe(44, hp);
  storeLe(48, mp);
  storeLe(52, hpMax);
  storeLe(56, mpMax);
  const move = new SeqWriter()
    .i16(x)
    .i16(y)
    .u32(opts.monsterId)
    .finish();
  const moveinfo = new Uint8Array(move.length + 2 + state.length);
  moveinfo.set(move, 0);
  moveinfo[move.length] = (state.length >>> 8) & 0xff;
  moveinfo[move.length + 1] = state.length & 0xff;
  moveinfo.set(state, move.length + 2);

  const info = new SeqWriter()
    .u8(1)
    .u32(opts.monsterId)
    .u32(x)
    .u32(y)
    .u8(1)
    .u32(opts.roleId)
    .u32(500)
    .u32(500)
    .u32(200)
    .u32(200)
    .finish();
  const b = new WtBuilder();
  b.object(1, 2, 2).raw("moveinfo", moveinfo);
  b.object(1, 4, 5).u8("side", 1).raw("battleinfo", info);
  return b.finishResponse();
}

/** Build actioninfo blob matching mock seq_put_u8/u32 (tagged stream). */
function demoActionInfoBlob(
  records: {
    type: number;
    actor: number;
    children: { target: number; flag: number; valueA: number; valueB: number }[];
    effectIndex?: number;
    effectText?: string;
  }[],
): Uint8Array {
  const chunks: number[] = [];
  const tagU8 = (v: number) => chunks.push(0, 1, v & 0xff);
  const tagU32 = (v: number) => {
    const x = v >>> 0;
    chunks.push(0, 4, (x >>> 24) & 0xff, (x >>> 16) & 0xff, (x >>> 8) & 0xff, x & 0xff);
  };
  const tagString = (s: string) => {
    const enc = new TextEncoder().encode(s); // ascii digits ok for demo effect text
    const len = enc.length + 1;
    chunks.push((len >>> 8) & 0xff, len & 0xff);
    for (let i = 0; i < enc.length; i++) chunks.push(enc[i]!);
    chunks.push(0);
  };
  for (const rec of records) {
    tagU8(rec.type);
    tagU8(rec.actor);
    if (rec.type === 3 || rec.type === 4) continue;
    tagU8(rec.children.length);
    for (const c of rec.children) {
      tagU8(c.target);
      tagU8(c.flag);
      tagU32(c.valueA);
      tagU32(c.valueB);
    }
    if (rec.type === 1 || rec.type === 2) {
      tagU32(rec.effectIndex ?? 14);
      tagString(rec.effectText ?? String(Math.abs(rec.children[0]?.valueA | 0)));
      tagU8(0);
      tagU8(0);
      tagU8(0);
    }
  }
  return Uint8Array.from(chunks);
}

/** Overlapped teaminfo row: 00 04 + id32 + hp32 + mp32 (mock contract). */
function demoTeaminfoBlob(roleId: number, roleHp: number, roleMp: number): Uint8Array {
  const out = new Uint8Array(14);
  out[0] = 0;
  out[1] = 4;
  const putBe = (off: number, v: number) => {
    out[off] = (v >>> 24) & 0xff;
    out[off + 1] = (v >>> 16) & 0xff;
    out[off + 2] = (v >>> 8) & 0xff;
    out[off + 3] = v & 0xff;
  };
  putBe(2, roleId);
  putBe(6, roleHp);
  putBe(10, roleMp);
  return out;
}

/** Demo 4/6 actioninfo + teaminfo (+ optional inline 4/7 settle). */
export function demoBuildBattleAction(opts: {
  roleId: number;
  roleHp: number;
  roleMp: number;
  monsterId: number;
  monsterHp: number;
  /** Signed HP delta applied to monster wire (negative = damage). */
  monsterHpDelta?: number;
  /** Signed HP delta applied to player wire. */
  roleHpDelta?: number;
  /** subtype 10: player wire 0 / monster 1; subtype 5: player 1 / monster 0 */
  playerWire?: number;
  monsterWire?: number;
  settle?: {
    expTotal: number;
    goldTotal: number;
    level: number;
    lastExp: number;
    curExp: number;
    percentExp: number;
    fdata?: string;
  };
}): Uint8Array {
  const playerWire = opts.playerWire ?? 0;
  const monsterWire = opts.monsterWire ?? 1;
  const mDelta = opts.monsterHpDelta ?? 0;
  const pDelta = opts.roleHpDelta ?? 0;
  const records: {
    type: number;
    actor: number;
    children: { target: number; flag: number; valueA: number; valueB: number }[];
  }[] = [];
  if (mDelta !== 0 || opts.monsterHpDelta !== undefined) {
    records.push({
      type: 0,
      actor: playerWire,
      children: [
        {
          target: monsterWire,
          flag: 0,
          valueA: mDelta >>> 0,
          valueB: 0,
        },
      ],
    });
  }
  if (pDelta !== 0) {
    records.push({
      type: 0,
      actor: monsterWire,
      children: [
        {
          target: playerWire,
          flag: 0,
          valueA: pDelta >>> 0,
          valueB: 0,
        },
      ],
    });
  }
  if (opts.monsterHp <= 0) {
    records.push({ type: 3, actor: monsterWire, children: [] });
  }
  const actionInfo = demoActionInfoBlob(
    records.length
      ? records
      : [
          {
            type: 0,
            actor: playerWire,
            children: [{ target: monsterWire, flag: 0, valueA: 0, valueB: 0 }],
          },
        ],
  );
  const team = demoTeaminfoBlob(opts.roleId, opts.roleHp, opts.roleMp);
  const b = new WtBuilder();
  b.object(1, 4, 6)
    .asciiDigit("actionnum", Math.max(1, records.length))
    .raw("teaminfo", team)
    .raw("actioninfo", actionInfo);
  if (opts.settle) {
    const s = opts.settle;
    const o = b.object(1, 4, 7);
    o.u32("exp", s.expTotal)
      .u32("lastexp", s.lastExp)
      .u32("curexp", s.curExp)
      .u32("persentexp", s.percentExp)
      .u32("energy", 100)
      .u32("energymax", 100)
      .u32("gold", s.goldTotal)
      .u32("level", s.level)
      .u8("result", 1)
      .u8("bagstatus", 0)
      .u32("hp", 0)
      .u32("mp", 0)
      .u8("itemnum", 0)
      .raw("iteminfo", new Uint8Array(0));
    if (s.fdata) o.string("fdata", s.fdata);
    o.u8("autorevive", 0);
  }
  void opts.monsterId;
  return b.finishResponse();
}

export function demoBuildBattleExit(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 4, 8);
  return b.finishResponse();
}

export function demoBuildPollPeers(peers: PeerActor[]): Uint8Array {
  if (peers.length === 0) return emptyWt();
  const info = new SeqWriter();
  const plus16 = new SeqWriter();
  for (const p of peers) {
    info
      .u32(p.actorId)
      .u8(p.visualVariant)
      .u8(p.visualGroup)
      .string(p.name)
      .u8((p.x / 16) & 0xff)
      .u8((p.y / 16) & 0xff)
      .string(p.title || "")
      .string(p.titleGif || "")
      .i16(p.x)
      .i16(p.y);
    plus16.u32(p.actorId).u8(Math.max(0, Math.min(8, p.plus16Count ?? 0)));
  }
  const b = new WtBuilder();
  b.object(1, 2, 10)
    .u32("othernum", peers.length)
    .raw("otherinfo", info.finish())
    .raw("plus16info", plus16.finish());
  return b.finishResponse();
}

export function emptyWt(): Uint8Array {
  const out = new Uint8Array(5);
  out[0] = 0x57;
  out[1] = 0x54;
  out[2] = 0;
  out[3] = 5;
  out[4] = 0;
  return out;
}
