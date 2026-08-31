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
  /** Today's open bracket: 0=rest, 10/50/70. */
  activeBracket: number;
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
  /** 1 = past matchEnd, same job×bracket score ties still fighting. */
  playoff: number;
};

export type ArenaRankRow = {
  roleId: number;
  name: string;
  score: number;
  rank: number;
  claimed: boolean;
  rewardName: string;
  rewardCount: number;
};

export type ArenaRankPage = {
  job: number;
  bracket: number;
  myRank: number;
  myScore: number;
  settled: boolean;
  myClaimed: boolean;
  myRewardName: string;
  myRewardCount: number;
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
    activeBracket: fieldU32(obj, "activeBracket"),
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
    playoff: fieldU32(obj, "playoff"),
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
        let rewardName = "";
        let rewardCount = 0;
        /* New servers append rewardName/rewardCount; old packets stop here. */
        if (r.remaining() > 4) {
          try {
            rewardName = r.string().replace(/\0+$/g, "");
            rewardCount = r.u32();
          } catch {
            rewardName = "";
            rewardCount = 0;
          }
        }
        rows.push({
          roleId,
          name,
          score,
          rank,
          claimed,
          rewardName,
          rewardCount,
        });
      } catch {
        break;
      }
    }
  }
  let myRewardName = "";
  const myRewardField = findField(obj, "myRewardName");
  if (myRewardField) {
    try {
      myRewardName = new SeqReader(myRewardField).string().replace(/\0+$/g, "");
    } catch {
      myRewardName = "";
    }
  }
  return {
    job: fieldU32(obj, "job"),
    bracket: fieldU32(obj, "bracket"),
    myRank: fieldU32(obj, "myRank"),
    myScore: fieldU32(obj, "myScore"),
    settled: fieldU32(obj, "settled") !== 0,
    myClaimed: fieldU32(obj, "myClaimed") !== 0,
    myRewardName,
    myRewardCount: fieldU32(obj, "myRewardCount"),
    rows,
  };
}

export function parseArenaClaim(packet: WtPacket): {
  result: number;
  rank: number;
  claimed: boolean;
  tip: string;
} | null {
  const obj = packet.objects.find(
    (o) => o.kind === ARENA_WT_KIND && o.subtype === 3,
  );
  if (!obj) return null;
  let tip = "";
  const tipField = findField(obj, "tip");
  if (tipField) {
    try {
      tip = new SeqReader(tipField).string().replace(/\0+$/g, "");
    } catch {
      tip = "";
    }
  }
  return {
    result: fieldU32(obj, "result"),
    rank: fieldU32(obj, "rank"),
    claimed: fieldU32(obj, "claimed") !== 0,
    tip,
  };
}

/** Human tip for claim result when wire tip missing (old server). */
export function arenaClaimFailTip(result: number | undefined | null): string {
  switch (result) {
    case 2:
      return "比赛尚未结束或未参赛";
    case 3:
      return "奖励已领取";
    case 4:
      return "未进入前十名";
    case 5:
      return "该名次未配置奖励";
    case 6:
      return "背包已满，请清理后再领奖";
    default:
      return `领奖失败(${result ?? "?"})`;
  }
}

export function parseArenaFromBodies(bodies: Uint8Array[]): {
  status?: ArenaStatus;
  hud?: ArenaHud;
  rank?: ArenaRankPage;
  claim?: { result: number; rank: number; claimed: boolean; tip: string };
} {
  const out: {
    status?: ArenaStatus;
    hud?: ArenaHud;
    rank?: ArenaRankPage;
    claim?: { result: number; rank: number; claimed: boolean; tip: string };
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
export type WagerMemberBrief = {
  name: string;
  /** 1..3 天机/幻剑/鬼道 */
  job: number;
  level: number;
};

export type WagerListRow = {
  challengeId: number;
  stake: number;
  status: number;
  hostCount: number;
  challCount: number;
  feePercent: number;
  hostName: string;
  hostLeaderRoleId: number;
  scene: string;
  hostNames: string[];
  hosts: WagerMemberBrief[];
  challs: WagerMemberBrief[];
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
  message: string;
} | null {
  const obj = packet.objects.find(
    (o) => o.kind === ARENA_WT_KIND && o.subtype === 10,
  );
  if (!obj) return null;
  const count = fieldU32(obj, "count");
  const raw = findField(obj, "rows");
  const rows: WagerListRow[] = [];
  const msgField = findField(obj, "message");
  let message = "";
  if (msgField) {
    try {
      message = new SeqReader(msgField).string().replace(/\0+$/g, "");
    } catch {
      message = "";
    }
  }
  if (raw) {
    /* Tolerate put_object_blob (BE16 len + seq) or put_object_entry (naked seq). */
    let seq = raw;
    if (raw.length >= 2) {
      const declared = (raw[0]! << 8) | raw[1]!;
      if (declared + 2 === raw.length && raw.length >= 8 && raw[2] === 0x00) {
        seq = raw.subarray(2);
      }
    }
    const r = new SeqReader(seq);
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
        const scene = r.string().replace(/\0+$/g, "");
        const hosts: WagerMemberBrief[] = [];
        const challs: WagerMemberBrief[] = [];
        for (let m = 0; m < hostCount; m++) {
          const name = r.string().replace(/\0+$/g, "");
          const job = r.u32();
          const level = r.u32();
          hosts.push({ name, job, level });
        }
        for (let m = 0; m < challCount; m++) {
          const name = r.string().replace(/\0+$/g, "");
          const job = r.u32();
          const level = r.u32();
          challs.push({ name, job, level });
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
          scene,
          hostNames: hosts.map((h) => h.name),
          hosts,
          challs,
        });
      } catch {
        break;
      }
    }
  }
  return { result: fieldU32(obj, "result"), rows, message };
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
  if (level >= 10) return 10;
  return 0;
}

export function bracketLabel(bracket: number): string {
  if (bracket === 70) return "70级";
  if (bracket === 50) return "50-69";
  if (bracket === 10) return "10-49";
  return String(bracket);
}
