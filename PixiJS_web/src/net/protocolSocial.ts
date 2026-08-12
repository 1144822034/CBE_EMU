/**
 * Nearby players / team / friends / player-info wire.
 * Evidence: docs/re/2026-07-07-nearby-players.md, 2026-07-11-nearby-player-actions.md
 */
import {
  SeqReader,
  SeqWriter,
  WtBuilder,
  decodeAsciiDigitResult,
  decodeFieldString,
  decodeTaggedNumber,
  findField,
  type WtPacket,
} from "./wt";
import { applyPlus16Info, applyPkInfo, type PeerActor } from "./protocol";
import { lookupItemSync } from "../resources/itemCatalog";

/* ---------- builders ---------- */

/** Surrounding-player page: 1/2/7 { pgIdx:u8 }. */
export function buildNearbyPageRequest(pgIdx = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 2, 7).u8("pgIdx", pgIdx & 0xff);
  return b.finish();
}

/** Other player info: 1/10/2 { id, type=2 }. */
export function buildPlayerInfoRequest(actorId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 2).u32("id", actorId >>> 0).u8("type", 2);
  return b.finish();
}

/** Other player equip: 1/29/4 { id }. */
export function buildOtherEquipRequest(actorId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 4).u32("id", actorId >>> 0);
  return b.finish();
}

/** Friend page: 1/10/1 { index:u32, pageSize:u8 }. index = skip offset. */
export const FRIEND_PAGE_SIZE = 10;

export function buildFriendPageRequest(
  index = 0,
  pageSize = FRIEND_PAGE_SIZE,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 1)
    .u32("index", index >>> 0)
    .u8("pageSize", pageSize & 0xff);
  return b.finish();
}

/** Add friend: 1/10/3 { id }. */
export function buildFriendAddRequest(actorId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 3).u32("id", actorId >>> 0);
  return b.finish();
}

/** Friend invite reply: 1/10/5 { id, result 1|2 }. */
export function buildFriendReplyRequest(actorId: number, accept: boolean): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 5)
    .u32("id", actorId >>> 0)
    .u8("result", accept ? 1 : 2);
  return b.finish();
}

/** Team invite: 1/5/1 { id } — field must be `id`, not actorID. */
export function buildTeamInviteRequest(actorId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 5, 1).u32("id", actorId >>> 0);
  return b.finish();
}

/** Team invite reply: 1/5/3 { id, result 1|0 }. */
export function buildTeamReplyRequest(actorId: number, accept: boolean): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 5, 3)
    .u32("id", actorId >>> 0)
    .u8("result", accept ? 1 : 0);
  return b.finish();
}

/** Leave team: 1/5/6. */
export function buildTeamLeaveRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 5, 6);
  return b.finish();
}

/** Full roster refresh: 1/5/10. */
export function buildTeamRosterRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 5, 10);
  return b.finish();
}

/** Spar / 切磋: 1/4/14 { id }. Server starts duel immediately if peer idle. */
export function buildSparRequest(actorId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 4, 14).u32("id", actorId >>> 0);
  return b.finish();
}

/** Trade request: 1/21/1 { id }. */
export function buildTradeRequest(actorId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 21, 1).u32("id", actorId >>> 0);
  return b.finish();
}

/** Trade invite reply: 1/21/3 { id, result 1|2 }. */
export function buildTradeReplyRequest(actorId: number, accept: boolean): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 21, 3)
    .u32("id", actorId >>> 0)
    .u8("result", accept ? 1 : 2);
  return b.finish();
}

/** Cancel / abort trade: 1/21/7 { result=2 }. */
export function buildTradeCancelRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 21, 7).u8("result", 2);
  return b.finish();
}

/** Confirm trade lock: 1/21/7 { result=1 }. First lock waits; both → 21/8. */
export function buildTradeConfirmRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 21, 7).u8("result", 1);
  return b.finish();
}

/**
 * Submit local offer: 1/21/5 { num, trademoney, iteminfo }.
 * iteminfo rows: tagged seq + tagged count (SendTradeSellReq).
 */
export function buildTradeOfferRequest(
  money: number,
  items: { seq: number; count: number }[],
): Uint8Array {
  const seq = new SeqWriter();
  for (const it of items) {
    seq.i16(it.seq & 0xffff).i16(it.count & 0xffff);
  }
  const b = new WtBuilder();
  b.object(1, 21, 5)
    .u8("num", items.length & 0xff)
    .u32("trademoney", money >>> 0)
    .blob("iteminfo", seq.finish());
  return b.finish();
}

/**
 * Guild invite: 1/10/13 { id, fid }.
 * Sender shows local tip; server empty-wt ack (target inbound unresolved).
 */
export function buildGuildInviteRequest(actorId: number, guildId = 1): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 13)
    .u32("id", actorId >>> 0)
    .u32("fid", guildId >>> 0);
  return b.finish();
}

/** Create guild start: 1/10/30 {} → 10/27 { result }. */
export function buildGuildCreateStartRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 30);
  return b.finish();
}

/** Validate guild name: 1/10/28 { name } → 10/28 { result, info? }. */
export function buildGuildCreateNameRequest(name: string): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 28).string("name", name);
  return b.finish();
}

/** Commit guild create: 1/10/11 { name } → 10/11 { result, id?, … }. */
export function buildGuildCreateCommitRequest(name: string): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 11).string("name", name);
  return b.finish();
}

/* ---------- types ---------- */

export type NearbyPage = {
  pgIdx: number;
  allpgs: number;
  peers: PeerActor[];
};

export type PlayerInfoView = {
  lines: string[];
  fdegree: string;
};

export type OtherEquipItem = {
  itemId: number;
  seq: number;
  name: string;
  quality: number;
  enhance: number;
  equipSlot: number;
  desc: string;
};

export type OtherEquipView = {
  name: string;
  level: number;
  jobIndex: number;
  sex: number;
  items: OtherEquipItem[];
};

export type FriendRow = {
  id: number;
  name: string;
  online: boolean;
  degree: number;
  job: number;
};

export type FriendPage = {
  allpgs: number;
  friends: FriendRow[];
};

export type TeamMember = {
  id: number;
  name: string;
  sexGroup: number;
  jobIndex: number;
  online: boolean;
  hp: number;
  mp: number;
  hpMax: number;
  mpMax: number;
};

export type TeamRoster = {
  members: TeamMember[];
  leaderId: number;
};

export type SocialInvite =
  | { kind: "team"; id: number; name: string }
  | { kind: "friend"; id: number; name: string }
  | { kind: "trade"; id: number; name: string };

export type SocialNotice =
  | { kind: "team_result"; id: number; result: number; name: string }
  | { kind: "friend_result"; result: number; name: string }
  | { kind: "team_leave"; id: number }
  | { kind: "trade_result"; result: number; name: string }
  | { kind: "spar_result"; result: number; tip: string }
  | { kind: "guild_sent" }
  | { kind: "guild_create"; stage: "need_name" | "confirm" | "done" | "fail"; tip: string; name?: string };

export type TradeOfferItem = {
  seq: number;
  itemId: number;
  count: number;
  name: string;
  enhance: number;
};

export type TradeOfferView = {
  money: number;
  items: TradeOfferItem[];
};

export type TradeSessionUpdate =
  | { kind: "open"; peerName: string }
  | { kind: "peer_offer"; offer: TradeOfferView }
  | { kind: "offer_ack"; result: number; tip: string }
  | { kind: "waiting" }
  | { kind: "cancel"; tip: string }
  | { kind: "done"; result: number; tip: string; receipt?: TradeOfferView };

/* ---------- parsers ---------- */

function fieldNum(obj: { fields: { name: string; raw: Uint8Array }[] }, name: string): number {
  const raw = findField(obj as never, name);
  if (!raw) return 0;
  return decodeTaggedNumber(raw) ?? decodeAsciiDigitResult(raw) ?? raw[2] ?? 0;
}

function parseOtherinfoRows(infoRaw: Uint8Array, count: number): PeerActor[] {
  const r = new SeqReader(infoRaw);
  const list: PeerActor[] = [];
  for (let i = 0; i < count && r.remaining() > 4; i++) {
    try {
      const actorId = r.u32();
      const visualVariant = r.u8();
      const visualGroup = r.u8();
      const labelText = r.string();
      r.u8();
      r.u8();
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
  return list;
}

/** 2/7 surrounding-player page. */
export function parseNearbyPage(packet: WtPacket): NearbyPage | null {
  const obj = packet.objects.find((o) => o.kind === 2 && o.subtype === 7);
  if (!obj) return null;
  const pgIdx = fieldNum(obj, "pgIdx");
  const allpgs = fieldNum(obj, "allpgs") || 1;
  const numRaw = findField(obj, "othernum");
  const infoRaw = findField(obj, "otherinfo");
  const count = numRaw
    ? ((numRaw[0] === 0 && numRaw[1] === 4
        ? ((numRaw[2]! << 24) | (numRaw[3]! << 16) | (numRaw[4]! << 8) | numRaw[5]!) >>> 0
        : decodeAsciiDigitResult(numRaw) ?? decodeTaggedNumber(numRaw)) ?? 0)
    : 0;
  const peers = infoRaw ? parseOtherinfoRows(infoRaw, count) : [];
  applyPlus16Info(peers, findField(obj, "plus16info"));
  applyPkInfo(peers, findField(obj, "pkinfo"));
  return { pgIdx, allpgs, peers };
}

/** 10/2 player info — playerinfo strings + fdegree. */
export function parsePlayerInfo(packet: WtPacket): PlayerInfoView | null {
  const obj = packet.objects.find((o) => o.kind === 10 && o.subtype === 2);
  if (!obj) return null;
  const result = fieldNum(obj, "result");
  if (result !== 1) return null;
  const fdegree = findField(obj, "fdegree")
    ? decodeFieldString(findField(obj, "fdegree")!).replace(/\0+$/g, "")
    : "";
  const num = fieldNum(obj, "num") || 2;
  const raw = findField(obj, "playerinfo");
  const lines: string[] = [];
  if (raw) {
    const r = new SeqReader(raw);
    for (let i = 0; i < num && r.remaining() > 2; i++) {
      try {
        const s = r.string().replace(/\0+$/g, "");
        if (s) lines.push(s);
      } catch {
        break;
      }
    }
  }
  if (fdegree) lines.push(fdegree);
  return { lines, fdegree };
}

/** Skip ParseEquipAttributes common-extra. */
function skipItemCommonExtra(r: SeqReader): number {
  let enhance = 0;
  try {
    const first = r.i16();
    r.i16();
    const attrCount = r.u8();
    enhance = first & 0xff;
    for (let a = 0; a < attrCount && a < 8; a++) {
      r.u8();
      r.u8();
      r.u8();
      r.i16();
    }
  } catch {
    /* truncated */
  }
  return enhance;
}

/** 29/4 other-player equipment. */
export function parseOtherEquip(packet: WtPacket): OtherEquipView | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 4);
  if (!obj) return null;
  if (fieldNum(obj, "result") !== 1) return null;
  const num = fieldNum(obj, "num");
  const level = fieldNum(obj, "level");
  const name = findField(obj, "name")
    ? decodeFieldString(findField(obj, "name")!).replace(/\0+$/g, "")
    : "?";
  const jobIndex = fieldNum(obj, "work");
  const sexGroup = fieldNum(obj, "sex");
  const sex = sexGroup >= 1 ? (sexGroup - 1) & 1 : 0;
  const raw = findField(obj, "equipinfo");
  const items: OtherEquipItem[] = [];
  if (raw && num > 0) {
    const r = new SeqReader(raw);
    for (let i = 0; i < num && r.remaining() > 4; i++) {
      try {
        const itemId = r.u32();
        const seq = r.i16();
        const enhance = skipItemCommonExtra(r);
        const cat = lookupItemSync(itemId);
        items.push({
          itemId,
          seq,
          name: cat?.name ?? `#${itemId}`,
          quality: cat?.quality ?? 0,
          enhance,
          equipSlot: cat?.equipSlot ?? -1,
          desc: cat?.desc ?? "",
        });
      } catch {
        break;
      }
    }
  }
  return { name, level, jobIndex, sex, items };
}

/** 10/1 friend page. */
export function parseFriendPage(packet: WtPacket): FriendPage | null {
  const obj = packet.objects.find((o) => o.kind === 10 && o.subtype === 1);
  if (!obj) return null;
  const allpgs = fieldNum(obj, "allpgs") || 1;
  const raw = findField(obj, "friendinfo");
  if (!raw) return { allpgs, friends: [] };
  const r = new SeqReader(raw);
  let rowCount = 0;
  try {
    rowCount = r.i16();
  } catch {
    return { allpgs, friends: [] };
  }
  const friends: FriendRow[] = [];
  for (let i = 0; i < rowCount && r.remaining() > 4; i++) {
    try {
      const id = r.u32();
      const name = r.string().replace(/\0+$/g, "") || "Player";
      const state = r.u8();
      const degree = r.u32();
      const job = r.u8();
      friends.push({
        id,
        name,
        online: state === 1 || state === 2,
        degree,
        job: job >= 1 && job <= 3 ? job : 1,
      });
    } catch {
      break;
    }
  }
  return { allpgs, friends };
}

function readBe32(buf: Uint8Array, pos: number): number {
  return (
    ((buf[pos]! << 24) |
      (buf[pos + 1]! << 16) |
      (buf[pos + 2]! << 8) |
      buf[pos + 3]!) >>>
    0
  );
}

/**
 * groupinfo / hsp blob: put_object_blob prefixes BE16(dataLen); CBE tagged-i32
 * skip consumes that prefix for the first id, so first id is raw BE32.
 */
function parseGroupinfoMembers(
  fieldRaw: Uint8Array,
  count: number,
  opts: { online: boolean },
): TeamMember[] {
  if (!fieldRaw.length || count <= 0) return [];
  // Blob value = BE16(len) + payload. Skip prefix when present.
  let pos = 0;
  if (
    fieldRaw.length >= 2 &&
    ((fieldRaw[0]! << 8) | fieldRaw[1]!) === fieldRaw.length - 2
  ) {
    pos = 2;
  }
  const slice = fieldRaw.subarray(pos);
  const r = new SeqReader(slice);
  const members: TeamMember[] = [];
  for (let i = 0; i < count && r.remaining() >= 4; i++) {
    try {
      let id: number;
      if (i === 0) {
        // raw BE32 after blob prefix
        id = readBe32(slice, r.pos);
        r.pos += 4;
      } else {
        id = r.u32();
      }
      const name = r.string().replace(/\0+$/g, "") || "Player";
      const sexGroup = r.u8();
      const jobIndex = r.u8();
      let online = true;
      if (opts.online) online = r.u8() !== 0;
      const hp = r.u32();
      const mp = r.u32();
      const hpMax = r.u32();
      const mpMax = r.u32();
      members.push({
        id,
        name,
        sexGroup: sexGroup >= 1 && sexGroup <= 2 ? sexGroup : 1,
        jobIndex: Math.max(0, Math.min(2, jobIndex)),
        online,
        hp,
        mp,
        hpMax: hpMax || 1,
        mpMax,
      });
    } catch {
      break;
    }
  }
  return members;
}

function parseJoinRow(fieldRaw: Uint8Array): TeamMember | null {
  const rows = parseGroupinfoMembers(fieldRaw, 1, { online: false });
  if (!rows[0]) return null;
  return { ...rows[0], online: true };
}

function parseHsp(
  fieldRaw: Uint8Array,
): { id: number; hp: number; hpMax: number; mp: number; mpMax: number } | null {
  if (!fieldRaw.length) return null;
  let pos = 0;
  if (
    fieldRaw.length >= 2 &&
    ((fieldRaw[0]! << 8) | fieldRaw[1]!) === fieldRaw.length - 2
  ) {
    pos = 2;
  }
  const slice = fieldRaw.subarray(pos);
  if (slice.length < 4) return null;
  const r = new SeqReader(slice);
  try {
    const id = readBe32(slice, 0);
    r.pos = 4;
    const hp = r.u32();
    const hpMax = r.u32();
    const mp = r.u32();
    const mpMax = r.u32();
    return { id, hp, hpMax: hpMax || 1, mp, mpMax };
  } catch {
    return null;
  }
}

export type GroupPacketUpdate = {
  invite?: SocialInvite;
  notice?: SocialNotice;
  /** Full roster replace (5/10). */
  roster?: TeamRoster;
  /**
   * Successful accept 5/3: AddRoleToList delta — other members only, never
   * re-sends self (login 5/10 already owns the local row).
   */
  rosterDelta?: TeamRoster;
  /** Incremental member join (5/5). */
  join?: TeamMember;
  /** HSP vitals patch (5/11). */
  hsp?: { id: number; hp: number; hpMax: number; mp: number; mpMax: number };
  /** Leave / remove member id (5/7). */
  leaveId?: number;
};

/** Consume all kind=5 objects in a packet. */
export function parseGroupPackets(packet: WtPacket): GroupPacketUpdate {
  const out: GroupPacketUpdate = {};
  for (const obj of packet.objects) {
    if (obj.kind !== 5) continue;
    switch (obj.subtype) {
      case 2: {
        const id = fieldNum(obj, "id");
        const name = findField(obj, "name")
          ? decodeFieldString(findField(obj, "name")!).replace(/\0+$/g, "")
          : "?";
        out.invite = { kind: "team", id, name };
        break;
      }
      case 3: {
        const result = fieldNum(obj, "result");
        if (result === 1) {
          const num = fieldNum(obj, "num");
          const raw = findField(obj, "groupinfo");
          if (raw && num > 0) {
            const members = parseGroupinfoMembers(raw, num, { online: true });
            out.rosterDelta = {
              members,
              leaderId: members[0]?.id ?? 0,
            };
          }
        }
        break;
      }
      case 4: {
        out.notice = {
          kind: "team_result",
          id: fieldNum(obj, "id"),
          result: fieldNum(obj, "result"),
          name: findField(obj, "name")
            ? decodeFieldString(findField(obj, "name")!).replace(/\0+$/g, "")
            : "",
        };
        break;
      }
      case 5: {
        const raw = findField(obj, "groupinfo");
        if (raw) {
          const row = parseJoinRow(raw);
          if (row) out.join = row;
        }
        break;
      }
      case 7: {
        out.leaveId = fieldNum(obj, "id");
        out.notice = { kind: "team_leave", id: out.leaveId };
        break;
      }
      case 10: {
        const result = fieldNum(obj, "result");
        if (result === 1) {
          const num = fieldNum(obj, "num");
          const raw = findField(obj, "groupinfo");
          const leadid = fieldNum(obj, "leadid");
          if (raw && num > 0) {
            const members = parseGroupinfoMembers(raw, num, { online: true });
            out.roster = {
              members,
              leaderId: leadid || members[0]?.id || 0,
            };
          } else {
            out.roster = { members: [], leaderId: 0 };
          }
        }
        break;
      }
      case 11: {
        const raw = findField(obj, "hsp");
        if (raw) {
          const h = parseHsp(raw);
          if (h) out.hsp = h;
        }
        break;
      }
      default:
        break;
    }
  }
  return out;
}

export type FriendInviteUpdate = {
  invite?: SocialInvite;
  notice?: SocialNotice;
};

/** 10/4 invite + 10/6 result. */
export function parseFriendInvitePackets(packet: WtPacket): FriendInviteUpdate {
  const out: FriendInviteUpdate = {};
  for (const obj of packet.objects) {
    if (obj.kind !== 10) continue;
    if (obj.subtype === 4) {
      const id = fieldNum(obj, "id");
      const name = findField(obj, "name")
        ? decodeFieldString(findField(obj, "name")!).replace(/\0+$/g, "")
        : "?";
      out.invite = { kind: "friend", id, name };
    } else if (obj.subtype === 6) {
      out.notice = {
        kind: "friend_result",
        result: fieldNum(obj, "result"),
        name: findField(obj, "name")
          ? decodeFieldString(findField(obj, "name")!).replace(/\0+$/g, "")
          : "",
      };
    }
  }
  return out;
}

export type TradePacketUpdate = {
  invite?: SocialInvite;
  /** 21/4 result=1 opens trade; result=2 peer refused. */
  notice?: SocialNotice;
  tradeOpen?: { name: string };
  session?: TradeSessionUpdate;
};

function skipTradeCommonExtra(r: SeqReader): { enhance: number } {
  let enhance = 0;
  try {
    const first = r.i16();
    r.i16();
    const attrCount = r.u8();
    enhance = first & 0xff;
    for (let a = 0; a < attrCount && a < 8; a++) {
      r.u8();
      r.u8();
      r.u8();
      r.i16();
    }
  } catch {
    /* truncated */
  }
  return { enhance };
}

/** Parse 21/6 peer offer rows: i16 seq, u32 itemId, i16 count, [string if item], extra. */
export function parseTradeOfferIteminfo(
  raw: Uint8Array,
  itemCount: number,
): TradeOfferItem[] {
  const items: TradeOfferItem[] = [];
  const r = new SeqReader(raw);
  for (let i = 0; i < itemCount && r.remaining() > 4; i++) {
    try {
      const seq = r.i16();
      const itemId = r.u32();
      const count = r.i16();
      if (itemId < 1000) {
        try {
          r.string();
        } catch {
          /* optional empty name */
        }
      }
      const extra = skipTradeCommonExtra(r);
      const cat = lookupItemSync(itemId);
      items.push({
        seq,
        itemId,
        count: Math.max(1, count),
        name: cat?.name ?? `#${itemId}`,
        enhance: cat?.kind === "equip" || itemId >= 1000 ? extra.enhance : 0,
      });
    } catch {
      break;
    }
  }
  return items;
}

/** Parse 21/8 receipt rows: i16 seq, u32 itemId, u32 count, extra (7/7 shape). */
function parseTradeReceiptIteminfo(
  raw: Uint8Array,
  itemCount: number,
): TradeOfferItem[] {
  const items: TradeOfferItem[] = [];
  const r = new SeqReader(raw);
  for (let i = 0; i < itemCount && r.remaining() > 4; i++) {
    try {
      const seq = r.i16();
      const itemId = r.u32();
      const count = r.u32();
      const extra = skipTradeCommonExtra(r);
      const cat = lookupItemSync(itemId);
      items.push({
        seq,
        itemId,
        count: Math.max(1, count),
        name: cat?.name ?? `#${itemId}`,
        enhance: cat?.kind === "equip" || itemId >= 1000 ? extra.enhance : 0,
      });
    } catch {
      break;
    }
  }
  return items;
}

/** 21/2 invite + 21/4 enter/refuse + 21/5 ack + 21/6 peer + 21/7|8 terminal. */
export function parseTradePackets(packet: WtPacket): TradePacketUpdate {
  const out: TradePacketUpdate = {};
  for (const obj of packet.objects) {
    if (obj.kind !== 21) continue;
    if (obj.subtype === 2) {
      const id = fieldNum(obj, "id");
      const name = findField(obj, "name")
        ? decodeFieldString(findField(obj, "name")!).replace(/\0+$/g, "")
        : "?";
      out.invite = { kind: "trade", id, name };
    } else if (obj.subtype === 4) {
      const result = fieldNum(obj, "result");
      const name = findField(obj, "name")
        ? decodeFieldString(findField(obj, "name")!).replace(/\0+$/g, "")
        : "";
      out.notice = { kind: "trade_result", result, name };
      if (result === 1) {
        out.tradeOpen = { name };
        out.session = { kind: "open", peerName: name };
      } else {
        out.session = {
          kind: "cancel",
          tip: `${name || "对方"}拒绝交易`,
        };
      }
    } else if (obj.subtype === 5) {
      const result = fieldNum(obj, "result");
      let tip = "报价已提交";
      if (result === 2) tip = "不在交易中";
      else if (result === 3) tip = "报价无效（物品/金币不足）";
      else if (result !== 1) tip = `报价失败(${result})`;
      out.session = { kind: "offer_ack", result, tip };
    } else if (obj.subtype === 6) {
      const num = fieldNum(obj, "num");
      const money = fieldNum(obj, "trademoney");
      const raw = findField(obj, "iteminfo") ?? new Uint8Array();
      out.session = {
        kind: "peer_offer",
        offer: { money, items: parseTradeOfferIteminfo(raw, num) },
      };
    } else if (obj.subtype === 7) {
      const result = fieldNum(obj, "result");
      if (result === 1) {
        out.session = { kind: "waiting" };
      } else {
        out.session = { kind: "cancel", tip: "交易已取消" };
      }
    } else if (obj.subtype === 8) {
      const result = fieldNum(obj, "result");
      const money = fieldNum(obj, "money") || fieldNum(obj, "trademoney");
      const num = fieldNum(obj, "num");
      const raw = findField(obj, "iteminfo");
      let tip = "交易成功";
      if (result === 3) tip = "对方背包已满";
      else if (result === 4) tip = "交易存储失败";
      else if (result !== 1) tip = `交易失败(${result})`;
      const receipt =
        result === 1 && raw
          ? { money, items: parseTradeReceiptIteminfo(raw, num || 0) }
          : undefined;
      out.session = { kind: "done", result, tip, receipt };
    }
  }
  return out;
}

export type GuildCreateUpdate = {
  notice?: SocialNotice;
  /** Pending name after 10/28 ok — commit with same string. */
  pendingName?: string;
};

/** 10/27 start, 10/28 name check, 10/11 commit. */
export function parseGuildCreatePackets(packet: WtPacket): GuildCreateUpdate {
  const out: GuildCreateUpdate = {};
  for (const obj of packet.objects) {
    if (obj.kind !== 10) continue;
    if (obj.subtype === 27) {
      const result = fieldNum(obj, "result");
      if (result === 1) {
        out.notice = {
          kind: "guild_create",
          stage: "need_name",
          tip: "请输入帮派名称",
        };
      } else {
        out.notice = {
          kind: "guild_create",
          stage: "fail",
          tip: result === 0 ? "无法创建帮派" : `创建失败(${result})`,
        };
      }
    } else if (obj.subtype === 30) {
      /* already-joined path of 10/30 */
      const result = fieldNum(obj, "result");
      out.notice = {
        kind: "guild_create",
        stage: "fail",
        tip:
          result === 4
            ? "已有帮派（请从帮派管理进入）"
            : `无法开始创建(${result})`,
      };
    } else if (obj.subtype === 28) {
      const result = fieldNum(obj, "result");
      const info = findField(obj, "info")
        ? decodeFieldString(findField(obj, "info")!).replace(/\0+$/g, "")
        : "";
      if (result === 1) {
        out.notice = {
          kind: "guild_create",
          stage: "confirm",
          tip: info || "帮派名称确认无误，是否创建？",
        };
      } else if (result === 2) {
        out.notice = {
          kind: "guild_create",
          stage: "fail",
          tip: "名称非法或已存在",
        };
      } else {
        out.notice = {
          kind: "guild_create",
          stage: "fail",
          tip: `名称校验失败(${result})`,
        };
      }
    } else if (obj.subtype === 11) {
      const result = fieldNum(obj, "result");
      const id = fieldNum(obj, "id");
      const msg = findField(obj, "msg")
        ? decodeFieldString(findField(obj, "msg")!).replace(/\0+$/g, "")
        : "";
      if (result === 1) {
        out.notice = {
          kind: "guild_create",
          stage: "done",
          tip: `创建成功 帮派ID ${id >>> 0}`,
        };
      } else {
        out.notice = {
          kind: "guild_create",
          stage: "fail",
          tip: msg || `创建提交失败(${result})`,
        };
      }
    }
  }
  return out;
}

/** 4/14 spar ack result (1=ok, 3=busy, 4=not found, 6=level, 7=city, …). */
export function parseSparAck(packet: WtPacket): SocialNotice | null {
  const obj = packet.objects.find((o) => o.kind === 4 && o.subtype === 14);
  if (!obj) return null;
  const result = fieldNum(obj, "result");
  let tip = "切磋请求已发送";
  if (result === 1) tip = "切磋开始";
  else if (result === 2) tip = "战斗中无法切磋";
  else if (result === 3) tip = "对方忙碌或无法切磋";
  else if (result === 4) tip = "目标不存在";
  else if (result === 5) tip = "超出范围";
  else if (result === 6) tip = "等级不足25级无法切磋";
  else if (result === 7) tip = "城镇中无法切磋";
  else if (result === 8) tip = "对方替身人偶生效，无法切磋";
  else if (result === 9) tip = "切磋冷却中，请稍后再试";
  else if (result === 10) tip = "PK值已满，无法发起切磋";
  else if (result >= 11) tip = "当前状态无法切磋";
  return { kind: "spar_result", result, tip };
}

/** Demo helper for smoke: build a tagged groupinfo blob field value. */
export function demoGroupinfoBlob(members: TeamMember[]): Uint8Array {
  const inner: number[] = [];
  const push = (arr: Uint8Array) => {
    for (let i = 0; i < arr.length; i++) inner.push(arr[i]!);
  };
  for (let i = 0; i < members.length; i++) {
    const m = members[i]!;
    if (i === 0) {
      const id = m.id >>> 0;
      inner.push((id >>> 24) & 0xff, (id >>> 16) & 0xff, (id >>> 8) & 0xff, id & 0xff);
    } else {
      push(new SeqWriter().u32(m.id).finish());
    }
    push(new SeqWriter().string(m.name).finish());
    push(new SeqWriter().u8(m.sexGroup).u8(m.jobIndex).u8(m.online ? 1 : 0).finish());
    push(
      new SeqWriter()
        .u32(m.hp)
        .u32(m.mp)
        .u32(m.hpMax)
        .u32(m.mpMax)
        .finish(),
    );
  }
  const data = Uint8Array.from(inner);
  const out = new Uint8Array(2 + data.length);
  out[0] = (data.length >>> 8) & 0xff;
  out[1] = data.length & 0xff;
  out.set(data, 2);
  return out;
}
