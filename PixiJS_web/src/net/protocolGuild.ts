/**
 * Guild list / detail / members / apply / approval wire.
 * Evidence: docs/re/2026-07-17-guild-list-and-role-display.md
 */
import {
  SeqReader,
  WtBuilder,
  decodeFieldString,
  decodeTaggedNumber,
  decodeAsciiDigitResult,
  findField,
  type WtPacket,
} from "./wt";

const PAGE_SIZE = 8;

/* ---------- builders ---------- */

/** Other-guild list (first enter): 1/10/21 { index, pagesize }. */
export function buildGuildListRequest(index = 1, pagesize = PAGE_SIZE): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 21)
    .u32("index", index >>> 0)
    .u8("pagesize", pagesize & 0xff);
  return b.finish();
}

/** Own-guild detail: 1/10/23 { id=guildId }. */
export function buildGuildSelfDetailRequest(guildId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 23).u32("id", guildId >>> 0);
  return b.finish();
}

/** Selected guild detail: 1/10/22 { id=guildId } → response 10/23. */
export function buildGuildDetailRequest(guildId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 22).u32("id", guildId >>> 0);
  return b.finish();
}

/** Leave guild: 1/10/22 { id=roleId } → same-subtype 10/22 { result }. */
export function buildGuildLeaveRequest(roleId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 22).u32("id", roleId >>> 0);
  return b.finish();
}

/** Member page: 1/10/37 { index, pagesize } → 10/20. */
export function buildGuildMemberPageRequest(index = 1, pagesize = PAGE_SIZE): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 37)
    .u32("index", index >>> 0)
    .u8("pagesize", pagesize & 0xff);
  return b.finish();
}

/** Apply to guild: 1/10/31 { id=guildId }. */
export function buildGuildApplyRequest(guildId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 31).u32("id", guildId >>> 0);
  return b.finish();
}

/** Pending applications: 1/10/32 { id=0, index, pagesize }. */
export function buildGuildApplicationPageRequest(
  index = 1,
  pagesize = PAGE_SIZE,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 32)
    .u32("id", 0)
    .u32("index", index >>> 0)
    .u8("pagesize", pagesize & 0xff);
  return b.finish();
}

/** Approve/reject: 1/10/33 { type 1|2, roleid }. */
export function buildGuildApplicationActionRequest(
  roleId: number,
  approve: boolean,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 10, 33)
    .u8("type", approve ? 1 : 2)
    .u32("roleid", roleId >>> 0);
  return b.finish();
}

/* ---------- types ---------- */

export type GuildListRow = {
  guildId: number;
  guildLevel: number;
  guildName: string;
  memberCount: number;
  leaderName: string;
};

export type GuildListPage = {
  fid: number;
  allpgs: number;
  rows: GuildListRow[];
};

export type GuildDetail = {
  guildId: number;
  guildName: string;
  leaderName: string;
  guildLevel: number;
  memberCountText: string;
  guildMoney: number;
  prosperity: number;
  actionPower: number;
  researchPower: number;
  construction: number;
  currentConstruction: string;
  notice: string;
};

export type GuildMemberRow = {
  roleId: number;
  online: boolean;
  roleName: string;
  title: string;
  rank: number;
  level: number;
};

export type GuildMemberPage = {
  cnum: number;
  mnum: number;
  allpgs: number;
  members: GuildMemberRow[];
};

export type GuildApplicantRow = {
  roleId: number;
  roleName: string;
  level: number;
};

export type GuildApplicationPage = {
  roles: number;
  maxroles: number;
  allpgs: number;
  applicants: GuildApplicantRow[];
  result: number;
};

export type GuildManageUpdate = {
  list?: GuildListPage;
  detail?: GuildDetail;
  members?: GuildMemberPage;
  applications?: GuildApplicationPage;
  applyResult?: { result: number; tip: string; name: string };
  leaveResult?: { result: number; tip: string };
  reviewResult?: { result: number; tip: string; type: number };
  membershipNotice?: {
    type: number;
    info: string;
    guildId: number;
    guildName: string;
    status: number;
  };
};

/* ---------- helpers ---------- */

function fieldNum(obj: { fields: { name: string; raw: Uint8Array }[] }, name: string): number {
  const raw = findField(obj as never, name);
  if (!raw) return 0;
  return decodeTaggedNumber(raw) ?? decodeAsciiDigitResult(raw) ?? raw[2] ?? 0;
}

function fieldStr(obj: { fields: { name: string; raw: Uint8Array }[] }, name: string): string {
  const raw = findField(obj as never, name);
  if (!raw) return "";
  return decodeFieldString(raw).replace(/\0+$/g, "");
}

function blobPayload(raw: Uint8Array): Uint8Array {
  if (raw.length >= 2) {
    const len = (raw[0]! << 8) | raw[1]!;
    if (len > 0 && 2 + len <= raw.length) return raw.subarray(2, 2 + len);
    if (len === 0) return new Uint8Array();
  }
  return raw;
}

function readRawBe32(r: SeqReader): number {
  if (r.remaining() < 4) throw new Error("raw be32");
  const v =
    ((r.buf[r.pos]! << 24) |
      (r.buf[r.pos + 1]! << 16) |
      (r.buf[r.pos + 2]! << 8) |
      r.buf[r.pos + 3]!) >>>
    0;
  r.pos += 4;
  return v;
}

function applyTip(result: number): string {
  switch (result) {
    case 1:
      return "申请已提交，等待批准";
    case 2:
      return "等级不足";
    case 3:
      return "已有帮派";
    case 4:
      return "帮派不存在";
    case 5:
      return "人数已满";
    case 6:
      return "已有待审申请";
    case 7:
      return "申请失败";
    default:
      return `申请结果(${result})`;
  }
}

function reviewTip(result: number): string {
  switch (result) {
    case 1:
      return "审批成功";
    case 2:
      return "权限不足";
    case 3:
      return "人数已满";
    case 4:
      return "对方已有帮派";
    case 5:
      return "申请已被处理";
    default:
      return `审批结果(${result})`;
  }
}

function parseFactionListBlob(raw: Uint8Array, num: number): GuildListRow[] {
  const data = blobPayload(raw);
  const rows: GuildListRow[] = [];
  const r = new SeqReader(data);
  for (let i = 0; i < num && r.remaining() >= 4; i++) {
    try {
      const guildId = i === 0 ? readRawBe32(r) : r.u32();
      const guildLevel = r.u32();
      const guildName = r.string().replace(/\0+$/g, "");
      const memberCount = r.u32();
      const leaderName = r.string().replace(/\0+$/g, "");
      rows.push({ guildId, guildLevel, guildName, memberCount, leaderName });
    } catch {
      break;
    }
  }
  return rows;
}

function parseFactionDetailBlob(raw: Uint8Array): GuildDetail | null {
  const data = blobPayload(raw);
  if (data.length < 4) return null;
  const r = new SeqReader(data);
  try {
    const guildId = readRawBe32(r);
    const guildName = r.string().replace(/\0+$/g, "");
    const leaderName = r.string().replace(/\0+$/g, "");
    const guildLevel = r.u32();
    const memberCountText = r.string().replace(/\0+$/g, "");
    const guildMoney = r.u32();
    const prosperity = r.u32();
    const actionPower = r.u32();
    const researchPower = r.u32();
    const construction = r.u32();
    const currentConstruction = r.string().replace(/\0+$/g, "");
    const notice = r.string().replace(/\0+$/g, "");
    return {
      guildId,
      guildName,
      leaderName,
      guildLevel,
      memberCountText,
      guildMoney,
      prosperity,
      actionPower,
      researchPower,
      construction,
      currentConstruction,
      notice,
    };
  } catch {
    return null;
  }
}

function parseMemberBlob(raw: Uint8Array, num: number): GuildMemberRow[] {
  const data = blobPayload(raw);
  const rows: GuildMemberRow[] = [];
  const r = new SeqReader(data);
  for (let i = 0; i < num && r.remaining() >= 4; i++) {
    try {
      const roleId = i === 0 ? readRawBe32(r) : r.u32();
      const online = r.u8() !== 0;
      const roleName = r.string().replace(/\0+$/g, "");
      const title = r.string().replace(/\0+$/g, "");
      const rank = r.u32();
      const level = r.u32();
      rows.push({ roleId, online, roleName, title, rank, level });
    } catch {
      break;
    }
  }
  return rows;
}

function parseApplicantBlob(raw: Uint8Array, num: number): GuildApplicantRow[] {
  const r = new SeqReader(raw);
  const rows: GuildApplicantRow[] = [];
  for (let i = 0; i < num && r.remaining() >= 4; i++) {
    try {
      const roleId = r.u32();
      const roleName = r.string().replace(/\0+$/g, "");
      const level = r.u32();
      rows.push({ roleId, roleName, level });
    } catch {
      break;
    }
  }
  return rows;
}

export function parseGuildManagePackets(packet: WtPacket): GuildManageUpdate {
  const out: GuildManageUpdate = {};
  for (const obj of packet.objects) {
    if (obj.kind !== 10) continue;
    if (obj.subtype === 21) {
      const result = fieldNum(obj, "result");
      if (result !== 1) continue;
      const num = fieldNum(obj, "num");
      const faction = findField(obj, "faction") ?? new Uint8Array();
      out.list = {
        fid: fieldNum(obj, "fid"),
        allpgs: Math.max(1, fieldNum(obj, "allpgs")),
        rows: parseFactionListBlob(faction, num),
      };
    } else if (obj.subtype === 23) {
      if (fieldNum(obj, "result") === 1) {
        const faction = findField(obj, "faction");
        if (faction) {
          const detail = parseFactionDetailBlob(faction);
          if (detail) out.detail = detail;
        }
      }
    } else if (obj.subtype === 20) {
      if (fieldNum(obj, "result") === 1) {
        const num = fieldNum(obj, "num");
        const playerinfo = findField(obj, "playerinfo") ?? new Uint8Array();
        out.members = {
          cnum: fieldNum(obj, "cnum"),
          mnum: fieldNum(obj, "mnum"),
          allpgs: Math.max(1, fieldNum(obj, "allpgs")),
          members: parseMemberBlob(playerinfo, num),
        };
      }
    } else if (obj.subtype === 22) {
      const result = fieldNum(obj, "result");
      out.leaveResult = {
        result,
        tip: result === 1 ? "成功脱离帮派" : "脱离帮派失败",
      };
    } else if (obj.subtype === 31) {
      const result = fieldNum(obj, "result");
      out.applyResult = {
        result,
        tip: applyTip(result),
        name: fieldStr(obj, "name"),
      };
    } else if (obj.subtype === 32) {
      const result = fieldNum(obj, "result");
      if (result === 1) {
        const num = fieldNum(obj, "num");
        const roleinfo = findField(obj, "roleinfo") ?? new Uint8Array();
        out.applications = {
          result,
          roles: fieldNum(obj, "roles"),
          maxroles: fieldNum(obj, "maxroles"),
          allpgs: Math.max(1, fieldNum(obj, "allpgs")),
          applicants: parseApplicantBlob(roleinfo, num),
        };
      } else {
        out.applications = {
          result,
          roles: 0,
          maxroles: 0,
          allpgs: 1,
          applicants: [],
        };
      }
    } else if (obj.subtype === 33) {
      const result = fieldNum(obj, "result");
      out.reviewResult = {
        result,
        tip: reviewTip(result),
        type: fieldNum(obj, "type"),
      };
    } else if (obj.subtype === 36) {
      const type = fieldNum(obj, "type");
      out.membershipNotice = {
        type,
        info: fieldStr(obj, "info") || (type === 1 ? "入帮申请已通过" : "入帮申请被拒绝"),
        guildId: fieldNum(obj, "id"),
        guildName: fieldStr(obj, "name"),
        status: fieldNum(obj, "status"),
      };
    }
  }
  return out;
}

export { PAGE_SIZE as GUILD_PAGE_SIZE };
