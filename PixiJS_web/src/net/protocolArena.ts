/**
 * Pixi-only 比武擂台 protocol (WT kind=41).
 * subtypes: 1 status, 2 rank, 3 claim, 4 leave, 5 hud (push).
 */
import {
  SeqReader,
  WtBuilder,
  decodeTaggedNumber,
  decodeWtPacket,
  findField,
  type WtObject,
  type WtPacket,
} from "./wt";

function fieldU32(obj: WtObject, name: string): number {
  const raw = findField(obj, name);
  if (!raw) return 0;
  try {
    const n = decodeTaggedNumber(raw);
    return n == null ? 0 : n >>> 0;
  } catch {
    return 0;
  }
}

export const ARENA_WT_KIND = 41;

export type ArenaStatus = {
  enabled: boolean;
  status: number;
  canEnter: boolean;
  dayYmd: number;
  minute: number;
  sessionId: number;
  round: number;
  scene: string;
};

export type ArenaHud = {
  status: number;
  round: number;
  roundRemain: number;
  eventRemain: number;
  score: number;
  job: number;
  bracket: number;
};

export type ArenaRankRow = {
  roleId: number;
  name: string;
  score: number;
  rank: number;
  claimed: boolean;
};

export type ArenaRankPage = {
  job: number;
  bracket: number;
  myRank: number;
  myScore: number;
  settled: boolean;
  rows: ArenaRankRow[];
};

export function buildArenaStatusRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ARENA_WT_KIND, 1);
  return b.finish();
}

export function buildArenaRankRequest(
  job: number,
  bracket: number,
  pageIndex = 0,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ARENA_WT_KIND, 2)
    .u32("job", job >>> 0)
    .u32("bracket", bracket >>> 0)
    .u32("pageIndex", pageIndex >>> 0);
  return b.finish();
}

export function buildArenaClaimRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ARENA_WT_KIND, 3);
  return b.finish();
}

export function buildArenaLeaveRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ARENA_WT_KIND, 4);
  return b.finish();
}

function packetFromBody(body: Uint8Array): WtPacket | null {
  try {
    return decodeWtPacket(body);
  } catch {
    return null;
  }
}

export function parseArenaStatus(packet: WtPacket): ArenaStatus | null {
  const obj = packet.objects.find(
    (o) => o.kind === ARENA_WT_KIND && o.subtype === 1,
  );
  if (!obj) return null;
  const sceneField = findField(obj, "scene");
  let scene = "";
  if (sceneField) {
    try {
      scene = new SeqReader(sceneField).string().replace(/\0+$/g, "");
    } catch {
      scene = "";
    }
  }
  return {
    enabled: fieldU32(obj, "enabled") !== 0,
    status: fieldU32(obj, "status"),
    canEnter: fieldU32(obj, "canEnter") !== 0,
    dayYmd: fieldU32(obj, "dayYmd"),
    minute: fieldU32(obj, "minute"),
    sessionId: fieldU32(obj, "sessionId"),
    round: fieldU32(obj, "round"),
    scene,
  };
}

export function parseArenaHud(packet: WtPacket): ArenaHud | null {
  const obj = packet.objects.find(
    (o) => o.kind === ARENA_WT_KIND && o.subtype === 5,
  );
  if (!obj) return null;
  return {
    status: fieldU32(obj, "status"),
    round: fieldU32(obj, "round"),
    roundRemain: fieldU32(obj, "roundRemain"),
    eventRemain: fieldU32(obj, "eventRemain"),
    score: fieldU32(obj, "score"),
    job: fieldU32(obj, "job"),
    bracket: fieldU32(obj, "bracket"),
  };
}

export function parseArenaRank(packet: WtPacket): ArenaRankPage | null {
  const obj = packet.objects.find(
    (o) => o.kind === ARENA_WT_KIND && o.subtype === 2,
  );
  if (!obj) return null;
  const count = fieldU32(obj, "count");
  const raw = findField(obj, "rows");
  const rows: ArenaRankRow[] = [];
  if (raw) {
    const r = new SeqReader(raw);
    for (let i = 0; i < count && r.remaining() > 4; i++) {
      try {
        const roleId = r.u32();
        const name = r.string().replace(/\0+$/g, "");
        const score = r.u32();
        const rank = r.u32();
        const claimed = r.u32() !== 0;
        rows.push({ roleId, name, score, rank, claimed });
      } catch {
        break;
      }
    }
  }
  return {
    job: fieldU32(obj, "job"),
    bracket: fieldU32(obj, "bracket"),
    myRank: fieldU32(obj, "myRank"),
    myScore: fieldU32(obj, "myScore"),
    settled: fieldU32(obj, "settled") !== 0,
    rows,
  };
}

export function parseArenaClaim(packet: WtPacket): {
  result: number;
  rank: number;
  claimed: boolean;
} | null {
  const obj = packet.objects.find(
    (o) => o.kind === ARENA_WT_KIND && o.subtype === 3,
  );
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    rank: fieldU32(obj, "rank"),
    claimed: fieldU32(obj, "claimed") !== 0,
  };
}

export function parseArenaFromBodies(bodies: Uint8Array[]): {
  status?: ArenaStatus;
  hud?: ArenaHud;
  rank?: ArenaRankPage;
  claim?: { result: number; rank: number; claimed: boolean };
} {
  const out: {
    status?: ArenaStatus;
    hud?: ArenaHud;
    rank?: ArenaRankPage;
    claim?: { result: number; rank: number; claimed: boolean };
    wagerEvent?: WagerEvent;
  } = {};
  for (const body of bodies) {
    const pkt = packetFromBody(body);
    if (!pkt) continue;
    const st = parseArenaStatus(pkt);
    if (st) out.status = st;
    const hud = parseArenaHud(pkt);
    if (hud) out.hud = hud;
    const rank = parseArenaRank(pkt);
    if (rank) out.rank = rank;
    const claim = parseArenaClaim(pkt);
    if (claim) out.claim = claim;
    const we = parseWagerEvent(pkt);
    if (we) out.wagerEvent = we;
  }
  return out;
}


/** 赏金擂台 subtypes 10-16 */
export type WagerListRow = {
  challengeId: number;
  stake: number;
  status: number;
  hostCount: number;
  challCount: number;
  feePercent: number;
  hostName: string;
  hostLeaderRoleId: number;
  hostNames: string[];
};

export type WagerMine = {
  challengeId: number;
  status: number;
  stake: number;
  hostCount: number;
  challCount: number;
  needConfirm: boolean;
  acceptedMask: number;
};

export type WagerEvent = {
  challengeId: number;
  eventKind: number;
  stake: number;
  payout: number;
  result: number;
};

export type WagerResult = {
  result: number;
  challengeId: number;
  message: string;
};

export function buildWagerListRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ARENA_WT_KIND, 10);
  return b.finish();
}

export function buildWagerCreateRequest(stake: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ARENA_WT_KIND, 11).u32("stake", stake >>> 0);
  return b.finish();
}

export function buildWagerCancelRequest(challengeId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ARENA_WT_KIND, 12).u32("challengeId", challengeId >>> 0);
  return b.finish();
}

export function buildWagerChallengeRequest(challengeId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ARENA_WT_KIND, 13).u32("challengeId", challengeId >>> 0);
  return b.finish();
}

export function buildWagerConfirmRequest(
  challengeId: number,
  accept: boolean,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ARENA_WT_KIND, 14)
    .u32("challengeId", challengeId >>> 0)
    .u32("accept", accept ? 1 : 0);
  return b.finish();
}

export function buildWagerMineRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ARENA_WT_KIND, 16);
  return b.finish();
}

function parseWagerResultObj(obj: WtObject): WagerResult {
  const msgField = findField(obj, "message");
  let message = "";
  if (msgField) {
    try {
      message = new SeqReader(msgField).string().replace(/\0+$/g, "");
    } catch {
      message = "";
    }
  }
  return {
    result: fieldU32(obj, "result"),
    challengeId: fieldU32(obj, "challengeId"),
    message,
  };
}

export function parseWagerList(packet: WtPacket): {
  result: number;
  rows: WagerListRow[];
} | null {
  const obj = packet.objects.find(
    (o) => o.kind === ARENA_WT_KIND && o.subtype === 10,
  );
  if (!obj) return null;
  const count = fieldU32(obj, "count");
  const raw = findField(obj, "rows");
  const rows: WagerListRow[] = [];
  if (raw) {
    const r = new SeqReader(raw);
    for (let i = 0; i < count && r.remaining() > 8; i++) {
      try {
        const challengeId = r.u32();
        const stake = r.u32();
        const status = r.u32();
        const hostCount = r.u32();
        const challCount = r.u32();
        const feePercent = r.u32();
        const hostName = r.string().replace(/\0+$/g, "");
        const hostLeaderRoleId = r.u32();
        const hostNames: string[] = [];
        for (let m = 0; m < hostCount; m++) {
          hostNames.push(r.string().replace(/\0+$/g, ""));
        }
        rows.push({
          challengeId,
          stake,
          status,
          hostCount,
          challCount,
          feePercent,
          hostName,
          hostLeaderRoleId,
          hostNames,
        });
      } catch {
        break;
      }
    }
  }
  return { result: fieldU32(obj, "result"), rows };
}

export function parseWagerMine(packet: WtPacket): WagerMine | null {
  const obj = packet.objects.find(
    (o) => o.kind === ARENA_WT_KIND && o.subtype === 16,
  );
  if (!obj) return null;
  return {
    challengeId: fieldU32(obj, "challengeId"),
    status: fieldU32(obj, "status"),
    stake: fieldU32(obj, "stake"),
    hostCount: fieldU32(obj, "hostCount"),
    challCount: fieldU32(obj, "challCount"),
    needConfirm: fieldU32(obj, "needConfirm") !== 0,
    acceptedMask: fieldU32(obj, "acceptedMask"),
  };
}

export function parseWagerEvent(packet: WtPacket): WagerEvent | null {
  const obj = packet.objects.find(
    (o) => o.kind === ARENA_WT_KIND && o.subtype === 15,
  );
  if (!obj) return null;
  return {
    challengeId: fieldU32(obj, "challengeId"),
    eventKind: fieldU32(obj, "eventKind"),
    stake: fieldU32(obj, "stake"),
    payout: fieldU32(obj, "payout"),
    result: fieldU32(obj, "result"),
  };
}

export function parseWagerOpResult(
  packet: WtPacket,
  subtype: number,
): WagerResult | null {
  const obj = packet.objects.find(
    (o) => o.kind === ARENA_WT_KIND && o.subtype === subtype,
  );
  if (!obj) return null;
  return parseWagerResultObj(obj);
}


export const ARENA_NPC_ENTER = 0xf6000001;
export const ARENA_NPC_LEAVE = 0xf6000002;
export const ARENA_NPC_RANK = 0xf6000003;
export const ARENA_NPC_WAGER = 0xf6000004;

export function bracketForLevel(level: number): number {
  if (level >= 70) return 70;
  if (level >= 50) return 50;
  if (level >= 40) return 40;
  if (level >= 30) return 30;
  if (level >= 20) return 20;
  if (level >= 10) return 10;
  return 0;
}

export function bracketLabel(bracket: number): string {
  if (bracket === 70) return "70级";
  if (bracket >= 10 && bracket <= 50) return `${bracket}-${bracket + 9}`;
  return String(bracket);
}
