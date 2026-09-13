/**
 * Extra WT builders/parsers for live core loop (move, battle operate, hangup,
 * NPC, chat, bag/equip/quest/mall shells).
 */
import {
  SeqReader,
  SeqWriter,
  WtBuilder,
  decodeAsciiDigitResult,
  decodeFieldString,
  decodeTaggedNumber,
  decodeWtPacket,
  findField,
  type WtPacket,
} from "./wt";
import { decodeGbk, encodeWireString } from "./gbk";
import { ensureSceneFile } from "./protocol";
import { starterSkillIdForJob } from "../resources/skill";
import { lookupItemSync, BACKPACK_EXPAND_ITEM_ID, BACKPACK_EXPAND_MAX_CAPACITY } from "../resources/itemCatalog";
export {
  buildTeamInviteRequest,
  buildTeamReplyRequest,
  buildTeamLeaveRequest,
  buildNearbyPageRequest,
  buildPlayerInfoRequest,
  buildOtherEquipRequest,
  buildFriendPageRequest,
  buildFriendAddRequest,
  buildFriendReplyRequest,
  buildFriendDeleteRequest,
  buildTeamRosterRequest,
  buildSparRequest,
  buildSparInviteReply,
  buildTradeRequest,
  buildTradeReplyRequest,
  buildTradeCancelRequest,
  buildTradeConfirmRequest,
  buildTradeOfferRequest,
  buildGuildInviteRequest,
  buildGuildInviteReplyRequest,
  buildGuildCreateStartRequest,
  buildGuildCreateNameRequest,
  buildGuildCreateCommitRequest,
} from "./protocolSocial";
export {
  buildGuildListRequest,
  buildGuildSelfDetailRequest,
  buildGuildDetailRequest,
  buildGuildLeaveRequest,
  buildGuildDisbandRequest,
  buildGuildKickRequest,
  buildGuildMemberPageRequest,
  buildGuildApplyRequest,
  buildGuildApplicationPageRequest,
  buildGuildApplicationActionRequest,
} from "./protocolGuild";

/** Direction codes: 1=up 2=right 3=down 4=left; 0=idle (CBE timeline). */
export function buildMoveRequest(dirBytes: Uint8Array): Uint8Array {
  const b = new WtBuilder();
  // CBE put_object_blob: outer valueLen + inner BE16(dataLen) + dirs.
  // Sample WT len=32 (docs/re/2026-07-10-nearby-player-movement-sync.md).
  // Bare raw dirs (len=30) fail get_object_blob_field → no position persist.
  b.object(1, 2, 1).blob("moveinfo", dirBytes);
  return b.finish();
}

/**
 * Walk-into portal / scene change: WT 2/3 + empty 27/11 + 7/42.
 * Evidence: mock wait_wt6 — 2/3 alone returns soft 30/2 without catalog;
 * `{2/3,27/11,7/42}` arms empty gate; next task-subset `6/1` owns nonempty 27/11.
 */
export function buildSceneChangeRequest(
  mapId: string,
  exitId: number,
  mapType = 2,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 2, 3)
    .u8("maptype", mapType)
    .string("mapID", mapId)
    .u32("exitID", exitId >>> 0);
  b.object(1, 0x1b, 11);
  b.object(1, 7, 42);
  return b.finish();
}

/** Optional prelude before 2/3: 2/10 Type=1 (edge portal countdown path). */
export function buildSceneChangePreludeRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 2, 10).u8("Type", 1);
  return b.finish();
}

/** Scene teleport-stone destination list: empty 1/16/1. */
export function buildTeleportStoneListRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 16, 1);
  return b.finish();
}

/**
 * World/regional map teleport prepare: 1/16/4 { curid, objid }.
 * Server replies 16/4 result=0 value=cost (stone count).
 */
export function buildMapTeleportPrepareRequest(
  curTeleportId: number,
  objTeleportId: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 16, 4)
    .u32("curid", curTeleportId >>> 0)
    .u32("objid", objTeleportId >>> 0);
  return b.finish();
}

/**
 * After 16/4 confirm: one WT with 16/2 + 16/3 (+ optional later 7/1 in combo).
 * type must be nonzero (combo parser rejects type=0).
 */
export function buildMapTeleportExitRequest(
  subtype: 2 | 3,
  exitId: number,
  type = 1,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 16, subtype)
    .u32("exitID", exitId >>> 0)
    .u8("type", type & 0xff);
  return b.finish();
}

/**
 * CBE ConsumeInventoryItem batch: 16/2 + 16/3 + 7/1(item 800) in one WT.
 * Evidence: docs/re/2026-06-25-teleport-stone.md (confirmed-exit-combo);
 * server deducts via 7/1 then defers 30/1 on poll — without 7/1 the standalone
 * 16/3 path enters the scene for free.
 *
 * 7/1 fields match ordinary item-use (`type`/`itemId`/`seq`/`num`); also send
 * `id` so parsers that only read the short consumable shape still select 800.
 */
export function buildMapTeleportConfirmedExitRequest(
  exitId: number,
  type: number,
  stoneSeq: number,
  stoneCount = 1,
): Uint8Array {
  const t = Math.max(1, type & 0xff);
  const n = Math.max(1, stoneCount | 0);
  const b = new WtBuilder();
  b.object(1, 16, 2)
    .u32("exitID", exitId >>> 0)
    .u8("type", t);
  b.object(1, 16, 3)
    .u32("exitID", exitId >>> 0)
    .u8("type", t);
  b.object(1, 7, 1)
    .u8("type", 0)
    .u32("itemId", TELEPORT_STONE_ITEM_ID)
    .u32("id", TELEPORT_STONE_ITEM_ID)
    .u32("seq", stoneSeq >>> 0)
    .u32("num", n)
    .u32("count", n);
  return b.finish();
}

/**
 * Scene n_telestone free transfer after 16/4 arm: 16/2+16/3 without 7/1.
 * Server confirmed-exit-combo accepts itemObjectLen=0 and defers 30/1.
 */
export function buildMapTeleportFreeExitRequest(
  exitId: number,
  type = 1,
): Uint8Array {
  const t = Math.max(1, type & 0xff);
  const b = new WtBuilder();
  b.object(1, 16, 2)
    .u32("exitID", exitId >>> 0)
    .u8("type", t);
  b.object(1, 16, 3)
    .u32("exitID", exitId >>> 0)
    .u8("type", t);
  return b.finish();
}

/** Teleport stone backpack id (ConsumeInventoryItem / mall 秘宝). */
export const TELEPORT_STONE_ITEM_ID = 800;

/** Parse 16/4 confirm object { result, value }. */
export function parseMapTeleportConfirm(
  packet: WtPacket,
): { result: number; value: number } | null {
  for (const o of packet.objects) {
    if (o.kind !== 16 || o.subtype !== 4) continue;
    const resultRaw = findField(o, "result");
    const valueRaw = findField(o, "value");
    if (!resultRaw) continue;
    const result = decodeTaggedNumber(resultRaw);
    const value = valueRaw ? decodeTaggedNumber(valueRaw) : 0;
    if (result == null) continue;
    return { result, value: value ?? 0 };
  }
  return null;
}

export function buildBattleOperateRequest(index: number, operate: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 4, 2).u32("index", index).u32("Operate", operate);
  return b.finish();
}

/**
 * In-battle auto flag: WT 1/4/11 { type }.
 * type=1 prefer + poll synth; type=0 clear prefer.
 * Optional Operate seeds LastOperate (Pixi [自动] has no prior 4/2).
 * Omit Operate when unset — seeding 0 wipes server skill memory
 * (docs/re/2026-08-16-pixi-background-auto-skill-to-na.md).
 * Optional hangup=0: prefer without HangupLoop / auto-encounter (Pixi only;
 * classic CBE omits the field and may arm team hangup).
 */
export function buildBattleAutoFlagRequest(
  type: 0 | 1,
  opts?: { operate?: number; hangup?: number },
): Uint8Array {
  const b = new WtBuilder();
  const o = b.object(1, 4, 11).u32("type", type);
  if (opts?.operate != null) o.u32("Operate", opts.operate >>> 0);
  if (opts?.hangup != null) o.u32("hangup", opts.hangup >>> 0);
  return b.finish();
}

/**
 * Battle item use: WT 1/4/3 { index, seq }.
 * `index` = actioninfo wire (ally/self only); `seq` = backpack row.
 * Distinct from scene bag use `7/1`.
 */
export function buildBattleItemUseRequest(index: number, seq: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 4, 3)
    .u32("index", index >>> 0)
    .u16("seq", seq & 0xffff);
  return b.finish();
}

/** Escape: empty 1/4/4 object. */
export function buildBattleEscapeRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 4, 4);
  return b.finish();
}

/**
 * Battle death prompt choice (mmBattle → WT 1/7/14).
 * result=1 使用复活石原地复活；result=2 普通复活（惩罚+回城/传送石）。
 * Evidence: docs/re/2026-07-23-battle-death-revival-stone.md
 */
export function buildBattleDeathPromptRequest(result: 1 | 2): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 14).u32("result", result);
  return b.finish();
}

/**
 * Hangup start: 1/2/10 Type=2 + empty 1/25/3.
 * Optional id/index/posx/posy seed the same request-live-node subtype-5 path
 * as manual 4/1 (Pixi has no emulator live table). Operate seeds LastOperate
 * so server poll synth casts the hangup skill (CBE-style; no client 4/2).
 */
export function buildHangupStartRequest(opts?: {
  enemyId?: number;
  index?: number;
  posx?: number;
  posy?: number;
  operate?: number;
}): Uint8Array {
  const b = new WtBuilder();
  const o = b.object(1, 2, 10).u8("Type", 2);
  const posx = (opts?.posx ?? 0) >>> 0;
  const posy = (opts?.posy ?? 0) >>> 0;
  // Live-node needs pos; id may be 0 when only coords are known.
  if (posx !== 0 && posy !== 0) {
    o.u32("id", (opts?.enemyId ?? 0) >>> 0)
      .u32("index", (opts?.index ?? 0) >>> 0)
      .u32("posx", posx)
      .u32("posy", posy);
  } else if (opts?.enemyId != null && opts.enemyId !== 0) {
    o.u32("id", opts.enemyId >>> 0).u32("index", (opts.index ?? 0) >>> 0);
  }
  if (opts?.operate != null && opts.operate !== 0) {
    o.u32("Operate", opts.operate >>> 0);
  }
  b.object(1, 25, 3);
  return b.finish();
}

/** NPC root dialog open: 26/1 { type=u8(1), id }. Service follow-up uses type=2. */
export function buildNpcDialogRequest(actorId: number, type = 1): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 26, 1).u8("type", type & 0xff).u32("id", actorId >>> 0);
  return b.finish();
}

/** Task hall detail: 6/10 { taskid, state }. */
export function buildTaskDetailRequest(taskId: number, state = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 6, 10).u32("taskid", taskId >>> 0).u8("state", state & 0xff);
  return b.finish();
}

/** Accept: 6/11 { taskinfo=tagged-u32 } + empty 25/5. */
export function buildTaskAcceptRequest(taskId: number): Uint8Array {
  const tagged = new Uint8Array(6);
  tagged[0] = 0;
  tagged[1] = 4;
  const v = taskId >>> 0;
  tagged[2] = (v >>> 24) & 0xff;
  tagged[3] = (v >>> 16) & 0xff;
  tagged[4] = (v >>> 8) & 0xff;
  tagged[5] = v & 0xff;
  const b = new WtBuilder();
  b.object(1, 6, 11).raw("taskinfo", tagged);
  b.object(1, 25, 5);
  return b.finish();
}

/** Submit: empty 25/5 + 6/4 { taskid }. */
export function buildTaskSubmitRequest(taskId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 25, 5);
  b.object(1, 6, 4).u32("taskid", taskId >>> 0);
  return b.finish();
}

/** Abandon accepted task: 6/7 { taskid }. */
export function buildTaskAbandonRequest(taskId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 6, 7).u32("taskid", taskId >>> 0);
  return b.finish();
}

/**
 * Task list query: empty 6/1 (accepted) and/or empty 6/14 (scene candidates).
 * Standalone — not the scene task-subset combo.
 */
export function buildTaskListQueryRequest(
  opts: { accepted?: boolean; candidates?: boolean } = {},
): Uint8Array {
  const wantAccepted = opts.accepted !== false;
  const wantCandidates = opts.candidates !== false;
  const b = new WtBuilder();
  if (wantAccepted) b.object(1, 6, 1);
  if (wantCandidates) b.object(1, 6, 14);
  return b.finish();
}

/** Accepted-task row from 6/1 taskinfo (ParseItemDataFields 0x01046D24). */
export type TaskListRow = {
  taskId: number;
  difficulty: number;
  classification: number;
  name: string;
  giver: string;
  level: number;
  receiver: string;
  reqType1: number;
  reqId1: number;
  progress1: number;
  reqCount1: number;
  reqType2: number;
  reqId2: number;
  progress2: number;
  reqCount2: number;
  state: number;
};

/** Candidate row from 6/14 taskinfo (DeserializeRoleInfo 0x01046E00). */
export type TaskCandidateRow = {
  taskId: number;
  difficulty: number;
  classification: number;
  name: string;
  giver: string;
  level: number;
};

/** Instance challenge confirm: strict 20-byte 30/10 { agree:u8 }. */
export function buildChallengeConfirmRequest(agree = true): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 30, 10).u8("agree", agree ? 1 : 0);
  return b.finish();
}

/** NPC service opcodes (high byte). */
export const NpcServiceOpcode = {
  OPEN_WEAPON: 0xe1,
  BUY_WEAPON: 0xe2,
  REPAIR_ALL: 0xe3,
  OPEN_SKILLS: 0xe4,
  LEARN_SKILL: 0xe5,
  OPEN_ARMOR: 0xe6,
  OPEN_MEDICINE: 0xe7,
  OPEN_CATEGORY: 0xe8,
  BUY_ITEM: 0xe9,
  OPEN_INSTANCE: 0xea,
  ENTER_INSTANCE: 0xeb,
  CHALLENGE: 0xec,
} as const;

export function npcServiceOpcode(value: number): number {
  return (value >>> 24) & 0xff;
}

/** NPC medicine buy: 0xe9 | (count<<16) | itemId16. count 0 → server treats as 1. */
export function encodeNpcBuyItem(itemId: number, count = 1): number {
  const id = itemId & 0xffff;
  const c = Math.max(1, Math.min(255, count | 0));
  return ((NpcServiceOpcode.BUY_ITEM & 0xff) << 24) | ((c & 0xff) << 16) | id;
}

export function decodeNpcBuyItemId(value: number): number {
  return value & 0xffff;
}

/**
 * Equip-sell pass (839) 26/1 service values — mirror mock_server_core.c.
 * Client may batch-call SELL_BASE without a new server opcode.
 */
export const EquipSellService = {
  OPEN: 0xf1000001,
  MENU: 0xf1010000,
  SELL_BASE: 0xf2000000,
  VIEW_BASE: 0xf3000000,
} as const;

/** True when this 26/1 is the equip-sell voucher UI (root / list / detail). */
export function isEquipSellDialog(dialog: NpcDialog): boolean {
  if (/请选择要出售的装备|出售该装备/.test(dialog.body)) return true;
  return dialog.options.some((o) => {
    const v = o.value >>> 0;
    const hi = v & 0xffff0000;
    return (
      v === EquipSellService.OPEN ||
      hi === EquipSellService.MENU ||
      hi === EquipSellService.SELL_BASE ||
      hi === EquipSellService.VIEW_BASE ||
      o.label === "出售装备" ||
      o.label === "出售该装备"
    );
  });
}

/**
 * Client-only dialog actions for equip-sell batch.
 * 293..295: all-sell (guarded vs star-sky 293..295 in main.ts).
 * 296..298: quality sell (avoid star-sky / dream-badge ids).
 */
export const EquipSellBatchAction = {
  OPEN_CONFIRM: 293,
  CONFIRM: 294,
  CANCEL: 295,
  OPEN_QUALITY_MENU: 296,
  OPEN_QUALITY_CONFIRM: 297,
  CONFIRM_QUALITY: 298,
} as const;

/** 品质选项：0白/1绿/2青/3紫/4红。 */
export const EQUIP_SELL_QUALITY_OPTIONS = [
  { quality: 0, label: "白装" },
  { quality: 1, label: "绿装" },
  { quality: 2, label: "蓝装" },
  { quality: 3, label: "紫装" },
  { quality: 4, label: "粉装" },
] as const;

export function equipSellQualityLabel(quality: number): string {
  const q = quality | 0;
  return (
    EQUIP_SELL_QUALITY_OPTIONS.find((o) => o.quality === q)?.label ??
    `品质${q}`
  );
}

export function matchEquipSellQuality(
  itemQuality: number,
  selected: number,
): boolean {
  return (itemQuality | 0) === (selected | 0);
}

/** Inject client-only batch/quality sell options into equip-sell dialogs. */
export function augmentEquipSellDialog(dialog: NpcDialog): NpcDialog {
  if (!isEquipSellDialog(dialog)) return dialog;
  const options = [...dialog.options];
  let changed = false;
  if (
    !options.some(
      (o) =>
        o.action === EquipSellBatchAction.OPEN_CONFIRM ||
        o.label === "出售背包全部装备",
    )
  ) {
    options.push({
      label: "出售背包全部装备",
      action: EquipSellBatchAction.OPEN_CONFIRM,
      value: 0,
    });
    changed = true;
  }
  if (
    !options.some(
      (o) =>
        o.action === EquipSellBatchAction.OPEN_QUALITY_MENU ||
        o.label === "按品质出售",
    )
  ) {
    options.push({
      label: "按品质出售",
      action: EquipSellBatchAction.OPEN_QUALITY_MENU,
      value: 0,
    });
    changed = true;
  }
  return changed ? { ...dialog, options } : dialog;
}

/** World chat: 1/3/1 { type=0, data }. */
export function buildChatRequest(text: string, type = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 3, 1).u8("type", type).string("data", text);
  return b.finish();
}

/**
 * Private chat: 1/3/2 { sendTo:u32, data }.
 * Evidence: docs/re/2026-07-16-chat-message-system.md — downlink type 7.
 */
export function buildPrivateChatRequest(sendTo: number, text: string): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 3, 2)
    .u32("sendTo", sendTo >>> 0)
    .string("data", text);
  return b.finish();
}

/** item.dsh 807 小喇叭 — world channel consume (docs/re/2026-07-20-small-horn-world-chat.md). */
export const SMALL_HORN_ITEM_ID = 807;

/**
 * World send wire: chat object + horn consume (no 2/10 companions).
 * Full CBE flush is `3/1+2/10+7/1+2/10`; Pixi only needs the two stateful
 * objects. Evidence: docs/re/2026-07-20-small-horn-world-chat.md.
 */
export function buildWorldChatHornRequest(
  text: string,
  hornSeq: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 3, 1).u8("type", 0).string("data", text);
  b.object(1, 7, 1)
    .u8("type", 1)
    .u32("id", SMALL_HORN_ITEM_ID)
    .u32("itemId", SMALL_HORN_ITEM_ID)
    .u32("seq", hornSeq >>> 0)
    .u32("num", 1);
  return b.finish();
}

/** GBK wire budget for world chat `data` / MySQL VARBINARY(81). */
export function clampWorldChatText(text: string): string {
  const raw = text.trim();
  if (!raw) return "";
  const bytes = encodeWireString(raw);
  if (bytes.length <= 81) return raw;
  let out = "";
  let len = 0;
  for (const ch of raw) {
    const piece = encodeWireString(ch);
    if (len + piece.length > 81) break;
    out += ch;
    len += piece.length;
  }
  return out;
}

/**
 * Pixi backpack open / refresh: empty 1/7/23 → 17/1 + 30/21 (real item_seq).
 * CBE still uses 7/42→17/1 only (no seq); inventing row-index seq breaks 7/15.
 */
export function buildBagOpenRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 23);
  return b.finish();
}

/** Pixi-only worn equipment refresh: 1/7/22 → 7/7 type=2. */
export function buildWornEquipRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 22);
  return b.finish();
}

/**
 * Item use: WT 1/7/1 { type, id/itemId, seq?, num? }.
 * Evidence: docs/re/2026-06-28-item-use.md (was wrongly wired to 7/4 discard).
 */
export function buildItemUseRequest(
  itemId: number,
  seq: number,
  num = 1,
  type = 0,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 1)
    .u8("type", type)
    .u32("itemId", itemId)
    .u32("seq", seq)
    .u32("num", num);
  return b.finish();
}

/**
 * Map heal (鬼道 清风拂体): provisional Pixi `1/12/8 { skillid }`.
 * skill.dsh 使用限制=1; mmGame cast WT still unresolved.
 */
export function buildMapHealSkillRequest(skillId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 0x12, 8).u32("skillid", skillId >>> 0);
  return b.finish();
}

/** Chest ids → matching key (docs/re/2026-07-26-gold-chest-524-open.md). */
export const CHEST_KEY_BY_BOX: Readonly<Record<number, number>> = {
  522: 813,
  523: 814,
  524: 815,
  532: 533,
  534: 535,
};

export function isChestBoxItem(itemId: number): boolean {
  return itemId in CHEST_KEY_BY_BOX;
}

export function isChestKeyItem(itemId: number): boolean {
  return (
    itemId === 813 ||
    itemId === 814 ||
    itemId === 815 ||
    itemId === 533 ||
    itemId === 535
  );
}

/** Official CBE exp cards → 1/7/30; custom 845 stays on generic 7/1. */
export const EXP_CARD_MULT: Readonly<Record<number, number>> = {
  809: 2,
  810: 4,
  811: 10,
  845: 30,
};

export function isExpCardItem(itemId: number): boolean {
  return itemId in EXP_CARD_MULT;
}

/** 809/810/811 only — JianghuOL.CBE:0x010236CA hardcodes these onto 7/30. */
export function isOfficialExpCardItem(itemId: number): boolean {
  return itemId === 809 || itemId === 810 || itemId === 811;
}

/** Mock success copy (GBK on wire; UTF-8 here for Pixi tip). */
export function expCardSuccessMessage(itemId: number): string {
  const m = EXP_CARD_MULT[itemId];
  if (!m) return "经验卡使用成功";
  return `获得${m}倍经验值，持续时间1小时。`;
}

/** 打金心得 853 — Pixi 自定义；走通用 1/7/1 时效，非 25/6. */
export const GOLD_SCROLL_ITEM_ID = 853;

export function isGoldScrollItem(itemId: number): boolean {
  return itemId === GOLD_SCROLL_ITEM_ID;
}

export function goldScrollSuccessMessage(): string {
  return "打金心得效果已生效，钱币增加20%，经验为0。";
}

/** Rename leftover「打金卷轴」copy from older server builds. */
export function renameGoldScrollDisplayText(text: string): string {
  return text.replaceAll("打金卷轴", "打金心得");
}

/**
 * Exp card: WT 1/7/30 { itemseq:u16, num:u8=1 }.
 * Evidence: docs/re/2026-07-23-special-item-use-contract.md
 */
export function buildExpCardUseRequest(seq: number, num = 1): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 30).u16("itemseq", seq & 0xffff).u8("num", num & 0xff);
  return b.finish();
}

/**
 * Status icon / Pixi「修」click: WT 1/7/31 (SendSceneAction31).
 * Response 7/31 { expinfo } lists active timed buffs + remaining duration.
 */
export function buildExpCardStatusRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 31);
  return b.finish();
}

/** 829 大力丸 / 830 神力丸 — JianghuOL.CBE → 1/22/3 {seq:u16}. */
export const COMBAT_PILL_ITEM_IDS: ReadonlySet<number> = new Set([829, 830]);

export function isCombatPillItem(itemId: number): boolean {
  return COMBAT_PILL_ITEM_IDS.has(itemId);
}

/**
 * Combat pill: WT 1/22/3 { seq:u16 }.
 * Evidence: docs/re/2026-07-23-special-item-use-contract.md
 */
export function buildCombatPillUseRequest(seq: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 22, 3).u16("seq", seq & 0xffff);
  return b.finish();
}

/** Parse 7/31 expinfo from status / login push bodies. */
export function parseExpCardStatusInfo(bodies: Uint8Array[]): string {
  for (const body of bodies) {
    let packet: WtPacket;
    try {
      packet = decodeWtPacket(body);
    } catch {
      continue;
    }
    const text = parseExpCardStatusInfoFromPacket(packet);
    if (text) return text;
  }
  return "";
}

/** Single-packet 7/31 { expinfo } — status icon / buff list. */
export function parseExpCardStatusInfoFromPacket(packet: WtPacket): string {
  for (const obj of packet.objects) {
    if (obj.kind !== 7 || obj.subtype !== 31) continue;
    const raw = findField(obj, "expinfo");
    if (!raw) continue;
    const text = renameGoldScrollDisplayText(decodeFieldString(raw).trim());
    if (text) return text;
  }
  return "";
}

/**
 * Remaining wall-clock seconds for item 828 from 7/31 / 25/6 status copy.
 * null = no battle-insight line (caller should not clear cache).
 * -1 = active but no remaining suffix on the wire.
 */
export function parseBattleInsightRemainSecFromStatus(info: string): number | null {
  if (!info.trim()) return 0;
  let sawInsight = false;
  for (const line of info.split(/\n/)) {
    if (!line.includes("战斗心得")) continue;
    sawInsight = true;
    const hours = line.match(/剩余(\d+)小时(\d+)分/);
    if (hours) {
      return (+hours[1]!) * 3600 + (+hours[2]!) * 60;
    }
    const mins = line.match(/剩余(\d+)分钟/);
    if (mins) return (+mins[1]!) * 60;
    if (line.includes("生效中")) return -1;
    return 0;
  }
  return sawInsight ? 0 : null;
}

/** Hangup combatinfo panel — wall-clock MM:SS (60min → 60:00 … 59:59). */
export function formatBattleInsightRemainMmSs(totalSec: number): string {
  const s = Math.max(0, totalSec | 0);
  const mm = Math.floor(s / 60);
  const ss = s % 60;
  return `${String(mm).padStart(2, "0")}:${String(ss).padStart(2, "0")}`;
}

/** 25/6 iteminfo after using 828 — same remaining suffix as 7/31. */
export function parseBattleInsightIteminfoFromPacket(packet: WtPacket): string {
  for (const obj of packet.objects) {
    if (obj.kind !== 25 || obj.subtype !== 6) continue;
    const raw = findField(obj, "iteminfo");
    if (!raw) continue;
    const text = decodeFieldString(raw).trim();
    if (text.includes("战斗心得")) return text;
  }
  return "";
}

/** 战斗心得 828 — JianghuOL.CBE:0x0102DC40 → 1/25/6 {seq:u16}. */
export const BATTLE_INSIGHT_ITEM_ID = 828;

export function isBattleInsightItem(itemId: number): boolean {
  return itemId === BATTLE_INSIGHT_ITEM_ID;
}

export function buildBattleInsightUseRequest(seq: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 25, 6).u16("seq", seq & 0xffff);
  return b.finish();
}

/** Follow-up wait-clear only (not a second consume): 1/25/7 {num:u8=1,seq:u16}. */
export function buildBattleInsightFollowupRequest(seq: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 25, 7).u8("num", 1).u16("seq", seq & 0xffff);
  return b.finish();
}

/**
 * Open chest: WT 1/7/15 { x:i16 boxSeq, x:i16 keySeq }.
 * Live CBE field names are both `x` (ADR alignment); server also accepts box/key.
 */
export function buildChestOpenRequest(boxSeq: number, keySeq: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 15)
    .u16("x", boxSeq & 0xffff)
    .u16("x", keySeq & 0xffff);
  return b.finish();
}

/**
 * PixiJS-only ten-pull: 1/7/24 { box:u16, key:u16, times:u8 }.
 * Server rolls/consumes/grants in one request; world rare lines use total count.
 */
export function buildChestOpenBatchRequest(
  boxSeq: number,
  keySeq: number,
  times: number,
): Uint8Array {
  const n = Math.max(1, Math.min(10, times | 0));
  const b = new WtBuilder();
  b.object(1, 7, 24)
    .u16("box", boxSeq & 0xffff)
    .u16("key", keySeq & 0xffff)
    .u8("times", n);
  return b.finish();
}

export type ItemUseFeedback = {
  ok: boolean;
  /** Short UI line (success / fail / 获得N个…). */
  message: string;
  /** Chest reward rows when 7/15 result=1. */
  rewards?: { itemId: number; seq: number; count: number; name: string }[];
};

/** 16/2 tip used by item-use reject / unavailable paths. */
export function parseHintNotices(packet: WtPacket): string[] {
  const out: string[] = [];
  for (const obj of packet.objects) {
    if (obj.kind !== 16 || obj.subtype !== 2) continue;
    const hintRaw = findField(obj, "hint");
    if (!hintRaw) continue;
    const text = renameGoldScrollDisplayText(
      decodeFieldString(hintRaw).replace(/\0+$/g, "").trim(),
    );
    if (text) out.push(text);
  }
  return out;
}

/** Generic 7/1 result (+ optional same-packet 16/2 hint). */
export function parseItemUseResult(packet: WtPacket): ItemUseFeedback | null {
  const obj = packet.objects.find((o) => o.kind === 7 && o.subtype === 1);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const hints = parseHintNotices(packet);
  if (result === 1) {
    return { ok: true, message: hints[0] || "使用成功" };
  }
  return {
    ok: false,
    message: hints[0] || "使用失败",
  };
}

function chestFailMessage(result: number): string {
  switch (result) {
    case 2:
      return "宝箱不存在";
    case 3:
      return "钥匙不存在";
    case 4:
      return "背包空间不足";
    case 5:
      return "获得金币";
    case 6:
      return "开启失败";
    default:
      return "开启失败";
  }
}

/**
 * 7/15 chest open. Success: result=1 + total + iteminfo rewards → 「获得N个名」.
 * Evidence: docs/re/2026-07-26-gold-chest-524-open.md / 2026-07-29-gold-chest-reward-count-tip.md.
 */
export function parseChestOpenResult(packet: WtPacket): ItemUseFeedback | null {
  const obj = packet.objects.find((o) => o.kind === 7 && o.subtype === 15);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  if (result !== 1) {
    return { ok: false, message: chestFailMessage(result) };
  }
  const total = Math.max(1, fieldU32(obj, "total") || 1);
  const raw = findField(obj, "iteminfo");
  const rewards: NonNullable<ItemUseFeedback["rewards"]> = [];
  if (raw) {
    const r = new SeqReader(raw);
    for (let i = 0; i < total && r.remaining() > 4; i++) {
      try {
        const itemId = r.u32();
        const seq = r.i16();
        const count = r.u32();
        readItemCommonExtra(r);
        const cat = lookupItemSync(itemId);
        rewards.push({
          itemId,
          seq,
          count: Math.max(1, count),
          name: cat?.name ?? `#${itemId}`,
        });
      } catch {
        break;
      }
    }
  }
  if (!rewards.length) {
    return { ok: true, message: "开启成功", rewards };
  }
  const parts = rewards.map((rw) => `获得${rw.count}个${rw.name}`);
  return { ok: true, message: parts.join("，"), rewards };
}

/**
 * PixiJS-only 7/24 ten-pull. result=1 + opened + total + iteminfo (aggregated).
 */
export function parseChestOpenBatchResult(
  packet: WtPacket,
): (ItemUseFeedback & { opened?: number }) | null {
  const obj = packet.objects.find((o) => o.kind === 7 && o.subtype === 24);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const opened = fieldU32(obj, "opened") >>> 0;
  if (result !== 1 || opened <= 0) {
    return {
      ok: false,
      message: chestFailMessage(result || 6),
      opened,
    };
  }
  const total = Math.max(0, fieldU32(obj, "total") || 0);
  const raw = findField(obj, "iteminfo");
  const rewards: NonNullable<ItemUseFeedback["rewards"]> = [];
  if (raw) {
    const r = new SeqReader(raw);
    for (let i = 0; i < total && r.remaining() > 4; i++) {
      try {
        const itemId = r.u32();
        const seq = r.i16();
        const count = r.u32();
        readItemCommonExtra(r);
        const cat = lookupItemSync(itemId);
        rewards.push({
          itemId,
          seq,
          count: Math.max(1, count),
          name: cat?.name ?? `#${itemId}`,
        });
      } catch {
        break;
      }
    }
  }
  const parts = rewards.map((rw) =>
    rw.count > 1 ? `${rw.count}个${rw.name}` : rw.name,
  );
  const joined = parts.join("，");
  const detail =
    joined.length > 72 ? `${joined.slice(0, 71)}…` : joined;
  return {
    ok: true,
    opened,
    message:
      opened < 10 && !detail
        ? `连开${opened}次：开启成功`
        : `连开${opened}次：${detail || "开启成功"}`,
    rewards,
  };
}

/**
 * Special timed-item responses: 7/30 exp card / 22/3 combat pill / 25/6 insight.
 * Success/fail text lives in string field `iteminfo` (not a binary blob).
 */
export function parseSpecialTimedItemUseResult(
  packet: WtPacket,
): ItemUseFeedback | null {
  for (const obj of packet.objects) {
    const special =
      (obj.kind === 7 && obj.subtype === 30) ||
      (obj.kind === 22 && obj.subtype === 3) ||
      (obj.kind === 25 && obj.subtype === 6);
    if (!special) continue;
    const result = fieldU32(obj, "result");
    const infoRaw = findField(obj, "iteminfo") || findField(obj, "info");
    let text = infoRaw
      ? decodeFieldString(infoRaw).replace(/\0+$/g, "").trim()
      : "";
    if (text === "OK") text = "";
    if (result === 1) {
      return { ok: true, message: text || "使用成功" };
    }
    return { ok: false, message: text || "使用失败" };
  }
  return null;
}

/** Prefer chest batch → chest → special timed → item-use → lone 16/2 hint. */
export function parseItemUseFeedback(packet: WtPacket): ItemUseFeedback | null {
  return (
    parseChestOpenBatchResult(packet) ||
    parseChestOpenResult(packet) ||
    parseSpecialTimedItemUseResult(packet) ||
    parseItemUseResult(packet) ||
    (() => {
      const hints = parseHintNotices(packet);
      if (!hints.length) return null;
      const message = hints[0]!;
      /* Map heal success uses 16/2 tip without 7/1 result. */
      const ok = message === "使用成功";
      return { ok, message };
    })()
  );
}

/** Discard: WT 1/7/4 { seq, itemId?, count? }. */
export function buildItemDiscardRequest(
  seq: number,
  itemId: number,
  count = 1,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 4)
    .u32("seq", seq)
    .u32("itemId", itemId)
    .u32("count", count);
  return b.finish();
}

/** Equip type=3 / unequip type=4. */
export function buildItemEquipRequest(
  type: 3 | 4,
  itemId: number,
  seq: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 8).u8("type", type).u32("itemId", itemId).u32("seq", seq);
  return b.finish();
}

/** 7/8 result — bag seq after unequip (type=4) / equip (type=3). */
export function parseItemEquipResult(
  packet: WtPacket,
): { type: number; result: number; seq: number } | null {
  const obj = packet.objects.find((o) => o.kind === 7 && o.subtype === 8);
  if (!obj) return null;
  const typeRaw = findField(obj, "type");
  const resultRaw = findField(obj, "result");
  const seqRaw = findField(obj, "seq");
  if (!resultRaw) return null;
  return {
    type: typeRaw ? (decodeTaggedNumber(typeRaw) ?? 0) : 0,
    result: decodeTaggedNumber(resultRaw) ?? 0,
    seq: seqRaw ? (decodeTaggedNumber(seqRaw) ?? 0) : 0,
  };
}

/** Swap occupied body slot ↔ bag: body/bag as u16 seq. */
export function buildEquipSwapRequest(bodySeq: number, bagSeq: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 9).u16("body", bodySeq & 0xffff).u16("bag", bagSeq & 0xffff);
  return b.finish();
}

export function buildEnhanceRequest(seq: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 1).u32("seq", seq);
  return b.finish();
}

/** Crystal item ids 901..916 → 一级..十六级玄晶. */
export const ENHANCE_CRYSTAL_FIRST = 901;
export const ENHANCE_CRYSTAL_LAST = 916;
export const ENHANCE_MAX_SLOTS = 5;
export const ENHANCE_MAX_LEVEL = 16;

const CRYSTAL_TIER_NAMES = [
  "一",
  "二",
  "三",
  "四",
  "五",
  "六",
  "七",
  "八",
  "九",
  "十",
  "十一",
  "十二",
  "十三",
  "十四",
  "十五",
  "十六",
] as const;

export function enhanceCrystalName(itemId: number): string {
  const tier = itemId - ENHANCE_CRYSTAL_FIRST + 1;
  if (tier < 1 || tier > 16) return `玄晶${itemId}`;
  return `${CRYSTAL_TIER_NAMES[tier - 1]!}级玄晶`;
}

export function isEnhanceCrystal(itemId: number): boolean {
  return itemId >= ENHANCE_CRYSTAL_FIRST && itemId <= ENHANCE_CRYSTAL_LAST;
}

/** occultinfo: n×(00 04 <itemId:u32> 00 01 <count:u8>), 1<=n<=5. */
export function buildEnhanceOccultInfo(
  materials: { itemId: number; count: number }[],
): Uint8Array {
  const rows = materials.filter((m) => m.count > 0).slice(0, ENHANCE_MAX_SLOTS);
  const out = new Uint8Array(rows.length * 9);
  let p = 0;
  for (const m of rows) {
    out[p++] = 0;
    out[p++] = 4;
    const id = m.itemId >>> 0;
    out[p++] = (id >>> 24) & 0xff;
    out[p++] = (id >>> 16) & 0xff;
    out[p++] = (id >>> 8) & 0xff;
    out[p++] = id & 0xff;
    out[p++] = 0;
    out[p++] = 1;
    out[p++] = m.count & 0xff;
  }
  return out;
}

/**
 * Preview / confirm enhance: 29/2 or 29/3.
 * equipseq = tagged-u16 (outer len 4); occultinfo = raw material stream (no blob len).
 */
export function buildEnhanceMaterialRequest(
  subtype: 2 | 3,
  equipSeq: number,
  materials: { itemId: number; count: number }[],
): Uint8Array {
  const occult = buildEnhanceOccultInfo(materials);
  const b = new WtBuilder();
  b.object(1, 29, subtype)
    .u16("equipseq", equipSeq & 0xffff)
    .raw("occultinfo", occult);
  return b.finish();
}

/** Recast / transfer enhance preview: 29/5 { seqd, seqs } (tagged-u16). */
export function buildEnhanceTransferRequest(
  destSeq: number,
  srcSeq: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 5)
    .u16("seqd", destSeq & 0xffff)
    .u16("seqs", srcSeq & 0xffff);
  return b.finish();
}

/** Recast / transfer confirm: 29/6 { seqd, seqs }. */
export function buildEnhanceTransferConfirmRequest(
  destSeq: number,
  srcSeq: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 6)
    .u16("seqd", destSeq & 0xffff)
    .u16("seqs", srcSeq & 0xffff);
  return b.finish();
}

export type EnhanceOpenResult = {
  result: number;
  curLevel: number;
  maxLevel: number;
  /** Per-level required power (usually 100 each). */
  required: number[];
  /** Per crystal-tier unit rate for current L (100/33/11…). */
  crystalPower: number[];
};

export type EnhancePreviewResult = {
  result: number;
  rate: number;
  money: number;
};

export type EnhanceConfirmResult = {
  result: number;
  /** result 1=success 2=fail roll; 3..6 errors. */
  ok: boolean;
  message: string;
};

export type EnhanceTransferPreviewResult = {
  result: number;
  ok: boolean;
  name: string;
  moneyWire: number;
  message: string;
};

export type EnhanceTransferConfirmResult = {
  result: number;
  ok: boolean;
  message: string;
  sourceSeq: number;
  sourceLevel: number;
  destSeq: number;
  destLevel: number;
};

function parseTaggedU32Stream(raw: Uint8Array | null): number[] {
  if (!raw || raw.length < 6) return [];
  const out: number[] = [];
  let p = 0;
  while (p + 6 <= raw.length && raw[p] === 0 && raw[p + 1] === 4) {
    out.push(
      ((raw[p + 2]! << 24) |
        (raw[p + 3]! << 16) |
        (raw[p + 4]! << 8) |
        raw[p + 5]!) >>>
        0,
    );
    p += 6;
  }
  return out;
}

/** Local unit rate mirror of mock_server (tier≥L+1 → 100%; else floor(/3) steps). */
export function enhanceCrystalUnitRate(level: number, tier: number): number {
  if (tier <= 0) return 0;
  if (tier > level) return 100;
  let rate = 100;
  let steps = level - tier + 1;
  while (steps-- > 0) rate = Math.floor(rate / 3);
  return rate;
}

/** +16 洗练最多叠 3 颗十六级玄晶；其它档/玄晶仍为 1. */
export function enhanceWashMaxCrystalCount(
  unlock: number,
  crystalItemId: number,
): number {
  if (unlock === 16 && crystalItemId === ENHANCE_CRYSTAL_LAST) return 3;
  return 1;
}

/**
 * Wash success rate mirror.
 * +16 + 十六级: 1→33 / 2→66 / 3→100；其余 unit×count（封顶 100）.
 */
export function enhanceWashSuccessRate(
  unlock: number,
  crystalItemId: number,
  count: number,
): number {
  if (!isEnhanceCrystal(crystalItemId) || count <= 0) return 0;
  const tier = crystalItemId - ENHANCE_CRYSTAL_FIRST + 1;
  const n = Math.max(0, Math.floor(count));
  if (unlock === 16 && tier === 16) {
    if (n >= 3) return 100;
    return Math.floor((n * 100) / 3);
  }
  return Math.min(100, enhanceCrystalUnitRate(unlock, tier) * n);
}

export function enhanceSuccessRate(
  level: number,
  materials: { itemId: number; count: number }[],
): number {
  let total = 0;
  for (const m of materials) {
    if (!isEnhanceCrystal(m.itemId) || m.count <= 0) continue;
    const tier = m.itemId - ENHANCE_CRYSTAL_FIRST + 1;
    total += enhanceCrystalUnitRate(level, tier) * m.count;
  }
  return Math.min(100, total);
}

function enhanceConfirmFailMessage(result: number): string {
  switch (result) {
    case 2:
      return "强化失败";
    case 3:
      return "装备不存在";
    case 4:
      return "玄晶不足";
    case 5:
      return "已达强化上限";
    case 6:
      return "金钱不足";
    default:
      return "强化失败";
  }
}

export function parseEnhanceOpenResult(packet: WtPacket): EnhanceOpenResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 1);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const curLevel = fieldU32(obj, "curlevel");
  const maxLevel = fieldU32(obj, "maxlevel") || ENHANCE_MAX_LEVEL;
  const required = parseTaggedU32Stream(findField(obj, "data1"));
  const crystalPower = parseTaggedU32Stream(findField(obj, "data2"));
  return { result, curLevel, maxLevel, required, crystalPower };
}

export function parseEnhancePreviewResult(
  packet: WtPacket,
): EnhancePreviewResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 2);
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    rate: fieldU32(obj, "value"),
    money: fieldU32(obj, "money"),
  };
}

export function parseEnhanceConfirmResult(
  packet: WtPacket,
): EnhanceConfirmResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 3);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  if (result === 1) {
    return { result, ok: true, message: "强化成功" };
  }
  if (result === 2) {
    return { result, ok: false, message: "强化失败" };
  }
  return { result, ok: false, message: enhanceConfirmFailMessage(result) };
}

export function parseEnhanceTransferPreview(
  packet: WtPacket,
): EnhanceTransferPreviewResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 5);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const nameRaw = findField(obj, "name");
  const name = nameRaw ? decodeFieldString(nameRaw).replace(/\0+$/g, "") : "";
  if (result === 1) {
    return {
      result,
      ok: true,
      name,
      moneyWire: fieldU32(obj, "money"),
      message: "确认转移",
    };
  }
  if (result === 2) {
    return {
      result,
      ok: false,
      name,
      moneyWire: 0,
      message: "装备不存在！",
    };
  }
  return {
    result,
    ok: false,
    name,
    moneyWire: 0,
    message: result === 5 ? "不可转移" : "转移失败",
  };
}

export function parseEnhanceTransferConfirm(
  packet: WtPacket,
): EnhanceTransferConfirmResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 6);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  if (result === 1) {
    return {
      result,
      ok: true,
      message: "转移成功",
      sourceSeq: fieldU32(obj, "seqs"),
      sourceLevel: fieldU32(obj, "curlevels"),
      destSeq: fieldU32(obj, "seqd"),
      destLevel: fieldU32(obj, "curleveld"),
    };
  }
  let message = "转移失败";
  if (result === 3) message = "没有对应玄晶";
  else if (result === 4) message = "金钱不足";
  else if (result === 5) message = "不可转移";
  return {
    result,
    ok: false,
    message,
    sourceSeq: 0,
    sourceLevel: 0,
    destSeq: 0,
    destLevel: 0,
  };
}

/** Transfer fee display (product): 48金65银. */

/** PixiJS-only enhance wash (29/7..9 + 29/14 replace confirm). */
export type EnhanceWashTier = {
  unlock: number;
  washed: number;
  type: number;
  flag: number;
  value: number;
  washCount: number;
  nextGold: number;
};

export type EnhanceWashOpenResult = {
  result: number;
  seq: number;
  curLevel: number;
  tiers: EnhanceWashTier[];
  pool: { type: number; flag: number; min: number; max: number }[];
};

export type EnhanceWashPreviewResult = {
  result: number;
  unlock: number;
  rate: number;
  money: number;
};

export type EnhanceWashConfirmResult = {
  result: number;
  ok: boolean;
  message: string;
  unlock: number;
  rate: number;
  money: number;
  /** New rolled attrs (pending until 29/14 apply). */
  attrType?: number;
  attrFlag?: number;
  attrValue?: number;
  /** Current attrs before replace decision. */
  oldWashed?: number;
  oldType?: number;
  oldFlag?: number;
  oldValue?: number;
  tiers?: EnhanceWashTier[];
};

export type EnhanceWashApplyResult = {
  result: number;
  ok: boolean;
  message: string;
  unlock: number;
  apply: number;
  tiers?: EnhanceWashTier[];
};

export function buildEnhanceWashOpenRequest(seq: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 7).u32("seq", seq);
  return b.finish();
}

export function buildEnhanceWashMaterialRequest(
  subtype: 8 | 9,
  equipSeq: number,
  unlock: number,
  crystalItemId: number,
  crystalCount = 1,
): Uint8Array {
  const max = enhanceWashMaxCrystalCount(unlock, crystalItemId);
  const count = Math.max(1, Math.min(max, Math.floor(crystalCount) || 1));
  const b = new WtBuilder();
  b.object(1, 29, subtype)
    .u16("equipseq", equipSeq & 0xffff)
    .u8("unlock", unlock & 0xff)
    .u32("crystal", crystalItemId >>> 0)
    .u8("count", count & 0xff);
  return b.finish();
}

/** 29/14: apply=1 replace with rolled attrs; apply=0 keep current. */
export function buildEnhanceWashApplyRequest(
  equipSeq: number,
  unlock: number,
  apply: boolean,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 14)
    .u16("equipseq", equipSeq & 0xffff)
    .u8("unlock", unlock & 0xff)
    .u8("apply", apply ? 1 : 0);
  return b.finish();
}

function parseEnhanceWashTiers(raw: Uint8Array | null | undefined): EnhanceWashTier[] {
  if (!raw || raw.length < 12) return [];
  const out: EnhanceWashTier[] = [];
  for (let i = 0; i + 12 <= raw.length && out.length < 4; i += 12) {
    out.push({
      unlock: raw[i]!,
      washed: raw[i + 1]!,
      type: raw[i + 2]!,
      flag: raw[i + 3]!,
      value: (raw[i + 4]! << 8) | raw[i + 5]!,
      washCount: (raw[i + 6]! << 8) | raw[i + 7]!,
      nextGold:
        (raw[i + 8]! << 24) |
        (raw[i + 9]! << 16) |
        (raw[i + 10]! << 8) |
        raw[i + 11]!,
    });
  }
  return out;
}

function parseEnhanceWashPool(
  raw: Uint8Array | null | undefined,
): { type: number; flag: number; min: number; max: number }[] {
  if (!raw || raw.length < 6) return [];
  const out: { type: number; flag: number; min: number; max: number }[] = [];
  for (let i = 0; i + 6 <= raw.length; i += 6) {
    out.push({
      type: raw[i]!,
      flag: raw[i + 1]!,
      min: (raw[i + 2]! << 8) | raw[i + 3]!,
      max: (raw[i + 4]! << 8) | raw[i + 5]!,
    });
  }
  return out;
}

export function parseEnhanceWashOpenResult(
  packet: WtPacket,
): EnhanceWashOpenResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 7);
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    seq: fieldU32(obj, "seq"),
    curLevel: fieldU32(obj, "curlevel"),
    tiers: parseEnhanceWashTiers(findField(obj, "tiers")),
    pool: parseEnhanceWashPool(findField(obj, "pool")),
  };
}

export function parseEnhanceWashPreviewResult(
  packet: WtPacket,
): EnhanceWashPreviewResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 8);
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    unlock: fieldU32(obj, "unlock"),
    rate: fieldU32(obj, "rate"),
    money: fieldU32(obj, "money"),
  };
}

function enhanceWashFailMessage(result: number): string {
  switch (result) {
    case 3:
      return "装备不存在";
    case 4:
      return "玄晶不足";
    case 5:
      return "档次未解锁";
    case 6:
      return "金钱不足";
    default:
      return "洗练失败";
  }
}

export function parseEnhanceWashConfirmResult(
  packet: WtPacket,
): EnhanceWashConfirmResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 9);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const unlock = fieldU32(obj, "unlock");
  const rate = fieldU32(obj, "rate");
  const money = fieldU32(obj, "money");
  const tiers = parseEnhanceWashTiers(findField(obj, "tiers"));
  if (result === 1) {
    return {
      result,
      ok: true,
      message: "洗练成功，请确认是否替换",
      unlock,
      rate,
      money,
      attrType: fieldU32(obj, "attrtype"),
      attrFlag: fieldU32(obj, "attrflag"),
      attrValue: fieldU32(obj, "attrvalue"),
      oldWashed: fieldU32(obj, "oldwashed"),
      oldType: fieldU32(obj, "oldtype"),
      oldFlag: fieldU32(obj, "oldflag"),
      oldValue: fieldU32(obj, "oldvalue"),
      tiers,
    };
  }
  if (result === 2) {
    return {
      result,
      ok: false,
      message: "洗练失败",
      unlock,
      rate,
      money,
      tiers,
    };
  }
  return {
    result,
    ok: false,
    message: enhanceWashFailMessage(result),
    unlock,
    rate,
    money,
    tiers,
  };
}

export function parseEnhanceWashApplyResult(
  packet: WtPacket,
): EnhanceWashApplyResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 14);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const unlock = fieldU32(obj, "unlock");
  const apply = fieldU32(obj, "apply");
  const tiers = parseEnhanceWashTiers(findField(obj, "tiers"));
  if (result === 1) {
    return {
      result,
      ok: true,
      message: apply ? "已替换为新属性" : "已保留当前属性",
      unlock,
      apply,
      tiers,
    };
  }
  let message = "确认失败";
  if (result === 2) message = "没有待确认的洗练";
  else if (result === 3) message = "装备不存在";
  else if (result === 5) message = "确认信息不匹配";
  return { result, ok: false, message, unlock, apply, tiers };
}

/** PixiJS-only 玄晶合成: 3×N级 → 1×(N+1)级；16级不可合成. */
export type CrystalCombineResult = {
  result: number;
  ok: boolean;
  message: string;
  srcItemId: number;
  destItemId: number;
  remaining: number;
  destCount: number;
};

export function enhanceCrystalTier(itemId: number): number {
  if (!isEnhanceCrystal(itemId)) return 0;
  return itemId - ENHANCE_CRYSTAL_FIRST + 1;
}

/** True when bag detail should show 合成 instead of 使用. */
export function canCombineEnhanceCrystal(itemId: number, count: number): boolean {
  const tier = enhanceCrystalTier(itemId);
  return tier >= 1 && tier < 16 && (count | 0) >= 3;
}

export function buildCrystalCombineRequest(seq: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 10).u32("seq", seq);
  return b.finish();
}

function crystalCombineFailMessage(result: number): string {
  switch (result) {
    case 2:
      return "不是玄晶";
    case 3:
      return "背包空间不足";
    case 4:
      return "玄晶不足3颗";
    case 5:
      return "十六级玄晶无法合成";
    default:
      return "合成失败";
  }
}

export function parseCrystalCombineResult(
  packet: WtPacket,
): CrystalCombineResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 10);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const srcItemId = fieldU32(obj, "itemid");
  const destItemId = fieldU32(obj, "destid");
  const remaining = fieldU32(obj, "remaining");
  const destCount = fieldU32(obj, "destcount");
  if (result === 1) {
    const name = enhanceCrystalName(destItemId || srcItemId + 1);
    return {
      result,
      ok: true,
      message: `合成成功：获得${name}`,
      srcItemId,
      destItemId,
      remaining,
      destCount,
    };
  }
  return {
    result,
    ok: false,
    message: crystalCombineFailMessage(result),
    srcItemId,
    destItemId,
    remaining,
    destCount,
  };
}

/** item.dsh 900 玄晶碎片 → 10 合成 1×一级玄晶(901). PixiJS-only 29/11. */
export const CRYSTAL_FRAGMENT_ITEM_ID = 900;
export const CRYSTAL_FRAGMENT_COMBINE_COST = 10;

export function isCrystalFragment(itemId: number): boolean {
  return (itemId | 0) === CRYSTAL_FRAGMENT_ITEM_ID;
}

/** True when bag detail should show 合成 for 玄晶碎片. */
export function canCombineCrystalFragment(
  itemId: number,
  count: number,
): boolean {
  return isCrystalFragment(itemId) && (count | 0) >= CRYSTAL_FRAGMENT_COMBINE_COST;
}

export function buildCrystalFragmentCombineRequest(seq: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 11).u32("seq", seq);
  return b.finish();
}

function crystalFragmentCombineFailMessage(result: number): string {
  switch (result) {
    case 2:
      return "不是玄晶碎片";
    case 3:
      return "背包空间不足";
    case 4:
      return "碎片不足10个";
    default:
      return "合成失败";
  }
}

export function parseCrystalFragmentCombineResult(
  packet: WtPacket,
): CrystalCombineResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 11);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const srcItemId = fieldU32(obj, "itemid");
  const destItemId = fieldU32(obj, "destid");
  const remaining = fieldU32(obj, "remaining");
  const destCount = fieldU32(obj, "destcount");
  if (result === 1) {
    const name = enhanceCrystalName(
      destItemId || ENHANCE_CRYSTAL_FIRST,
    );
    return {
      result,
      ok: true,
      message: `合成成功：获得${name}`,
      srcItemId,
      destItemId,
      remaining,
      destCount,
    };
  }
  return {
    result,
    ok: false,
    message: crystalFragmentCombineFailMessage(result),
    srcItemId,
    destItemId,
    remaining,
    destCount,
  };
}

/** item.dsh 840 梦境徽章 → 300 合成 1×自选梦境装备(40065..40088). PixiJS-only 29/12. */
export const DREAM_BADGE_ITEM_ID = 840;
export const DREAM_BADGE_COMBINE_COST = 300;

/** Dream equip pool for badge combine (equip.dsh). */
export const DREAM_BADGE_EQUIP_IDS = [
  40065, 40066, 40067, 40068, 40069, 40070, 40071, 40072, 40073, 40074,
  40075, 40076, 40077, 40078, 40079, 40080, 40081, 40082, 40083, 40084,
  40085, 40086, 40087, 40088,
] as const;

export function isDreamBadge(itemId: number): boolean {
  return (itemId | 0) === DREAM_BADGE_ITEM_ID;
}

export function isDreamBadgeEquipId(itemId: number): boolean {
  const id = itemId | 0;
  return (DREAM_BADGE_EQUIP_IDS as readonly number[]).includes(id);
}

/** True when bag detail should show 合成 for 梦境徽章. */
export function canCombineDreamBadge(itemId: number, count: number): boolean {
  return isDreamBadge(itemId) && (count | 0) >= DREAM_BADGE_COMBINE_COST;
}

export function buildDreamBadgeCombineRequest(
  seq: number,
  destItemId: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 12)
    .u32("seq", seq)
    .u32("destid", destItemId >>> 0);
  return b.finish();
}

function dreamBadgeCombineFailMessage(result: number): string {
  switch (result) {
    case 2:
      return "不是梦境徽章";
    case 3:
      return "背包空间不足";
    case 4:
      return "徽章不足300个";
    case 5:
      return "装备目录缺失";
    case 6:
      return "请选择梦境装备";
    default:
      return "合成失败";
  }
}

export function parseDreamBadgeCombineResult(
  packet: WtPacket,
): CrystalCombineResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 12);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const srcItemId = fieldU32(obj, "itemid");
  const destItemId = fieldU32(obj, "destid");
  const remaining = fieldU32(obj, "remaining");
  const destCount = fieldU32(obj, "destcount");
  if (result === 1) {
    const name =
      lookupItemSync(destItemId)?.name ||
      (destItemId ? `装备#${destItemId}` : "梦境装备");
    return {
      result,
      ok: true,
      message: `合成成功：获得${name}`,
      srcItemId,
      destItemId,
      remaining,
      destCount,
    };
  }
  return {
    result,
    ok: false,
    message: dreamBadgeCombineFailMessage(result),
    srcItemId,
    destItemId,
    remaining,
    destCount,
  };
}

/** Pixi 自定义 849 星空石 — 重置指定装备洗练档位次数。29/13. */
export const STAR_SKY_STONE_ITEM_ID = 849;

export function isStarSkyStone(itemId: number): boolean {
  return (itemId | 0) === STAR_SKY_STONE_ITEM_ID;
}

export type StarSkyStoneWashResetResult = {
  result: number;
  ok: boolean;
  message: string;
  stoneSeq: number;
  equipSeq: number;
  unlock: number;
  remaining: number;
  washCount: number;
  tiers?: EnhanceWashTier[];
};

export function buildStarSkyStoneWashResetRequest(
  stoneSeq: number,
  equipSeq: number,
  unlock: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 13)
    .u16("stoneseq", stoneSeq & 0xffff)
    .u16("equipseq", equipSeq & 0xffff)
    .u8("unlock", unlock & 0xff);
  return b.finish();
}

function starSkyStoneWashResetFailMessage(result: number): string {
  switch (result) {
    case 2:
      return "星空石不足";
    case 3:
      return "装备不存在";
    case 4:
      return "该档位未解锁";
    case 5:
      return "档位无效";
    case 6:
      return "扣除星空石失败";
    case 7:
      return "该档洗练次数已为0";
    default:
      return "重置失败";
  }
}

export function parseStarSkyStoneWashResetResult(
  packet: WtPacket,
): StarSkyStoneWashResetResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 13);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const stoneSeq = fieldU32(obj, "stoneseq");
  const equipSeq = fieldU32(obj, "equipseq");
  const unlock = fieldU32(obj, "unlock");
  const remaining = fieldU32(obj, "remaining");
  const washCount = fieldU32(obj, "washcount");
  const tiers = parseEnhanceWashTiers(findField(obj, "tiers"));
  if (result === 1) {
    return {
      result,
      ok: true,
      message: `+${unlock}档洗练次数已重置`,
      stoneSeq,
      equipSeq,
      unlock,
      remaining,
      washCount,
      tiers: tiers.length ? tiers : undefined,
    };
  }
  return {
    result,
    ok: false,
    message: starSkyStoneWashResetFailMessage(result),
    stoneSeq,
    equipSeq,
    unlock,
    remaining,
    washCount,
    tiers: tiers.length ? tiers : undefined,
  };
}

/** Pixi 自定义 852 角色改名卡 — 使用后输入新名。29/15. */
export const RENAME_CARD_ITEM_ID = 852;

export function isRenameCard(itemId: number): boolean {
  return (itemId | 0) === RENAME_CARD_ITEM_ID;
}

export type RenameCardResult = {
  result: number;
  ok: boolean;
  message: string;
  cardSeq: number;
  remaining: number;
  name: string;
};

export function buildRenameCardRequest(
  cardSeq: number,
  name: string,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 29, 15)
    .u16("cardseq", cardSeq & 0xffff)
    .string("name", name);
  return b.finish();
}

function renameCardFailMessage(result: number): string {
  switch (result) {
    case 2:
      return "改名卡不足";
    case 3:
      return "请输入角色名";
    case 4:
      return "角色名过长";
    case 5:
      return "与当前名称相同";
    case 6:
      return "名已被注册";
    case 7:
      return "扣除改名卡失败";
    case 8:
      return "改名保存失败";
    default:
      return "改名失败";
  }
}

export function parseRenameCardResult(
  packet: WtPacket,
): RenameCardResult | null {
  const obj = packet.objects.find((o) => o.kind === 29 && o.subtype === 15);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const cardSeq = fieldU32(obj, "cardseq");
  const remaining = fieldU32(obj, "remaining");
  const nameRaw = findField(obj, "name");
  const name = nameRaw ? decodeFieldString(nameRaw).replace(/\0+$/g, "") : "";
  if (result === 1) {
    return {
      result,
      ok: true,
      message: name ? `角色名已改为${name}` : "改名成功",
      cardSeq,
      remaining,
      name,
    };
  }
  return {
    result,
    ok: false,
    message: renameCardFailMessage(result),
    cardSeq,
    remaining,
    name,
  };
}


export const ENHANCE_TRANSFER_FEE_TEXT = "48金65银";

/** Mall balance: 1/14/4 → coolmoney(=role.wcoin) + ticket. */
export function buildMallBalanceRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 14, 4);
  return b.finish();
}

/** @deprecated use buildMallBalanceRequest — 1/1/14 需 actorId，不能当余额查询. */
export function buildMallOpenRequest(): Uint8Array {
  return buildMallBalanceRequest();
}

/** Mall page: 1/14/{5..13} { index }.
 * subtype 5 = 秘宝/酷宝货架（server_shop_items + 类别14）；6..13 = 原 W 币神兵分栏。
 * 余额字段名 coolmoney，权威值为 role.wcoin。
 */
export function buildMallPageRequest(subtype: number, index = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 14, subtype & 0xff).u32("index", index >>> 0);
  return b.finish();
}

/** W币/酷宝 mall buy: 1/14/3 { type, id, num }. type=2 = 酷宝(W币)购买. */
export function buildShopBuy14Request(
  itemId: number,
  num = 1,
  type = 2,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 14, 3)
    .u8("type", type)
    .u32("id", itemId >>> 0)
    .u32("num", num >>> 0);
  return b.finish();
}

/**
 * Mall buy ack: 1/14/3 { seq, result }.
 * result: 1=ok, 2=酷宝不足(type=2), 3=背包已满/无法入库, 4=礼包空位不足.
 * Evidence: mmShop:0x9DE / docs/re/2026-07-26-shop-buy14-bag-full-as-wcoin.md
 * For item 806 (direct expand), result=3 means capacity already at the hard cap.
 */
export function parseShopBuy14Response(
  packet: WtPacket,
): { seq: number; result: number } | null {
  const obj = packet.objects.find((o) => o.kind === 14 && o.subtype === 3);
  if (!obj) return null;
  if (!findField(obj, "result") && !findField(obj, "seq")) return null;
  return {
    seq: fieldU32(obj, "seq"),
    result: fieldU32(obj, "result"),
  };
}

/** Map shop 14/3 result to panel tip; empty string = unknown / keep generic. */
export function shopBuy14ResultTip(result: number, itemId = 0): string {
  switch (result >>> 0) {
    case 1:
      return "";
    case 2:
      return "酷宝不够请右下角赞助";
    case 3:
      if ((itemId >>> 0) === BACKPACK_EXPAND_ITEM_ID)
        return "背包已达上限，不能再扩容";
      return "背包已满";
    case 4:
      return "背包已满";
    default:
      return result ? `购买失败(${result})` : "购买失败";
  }
}

/**
 * WT `1/12/1` (major=1 kind=0x0c subtype=1): learned skill IDs for the active role.
 * Names/MP/effects come from local skill.dsh filtered by actorinfo job.
 * Evidence: JianghuOL.CBE:0x01010594 / docs/re/2026-06-28-role-skills.md
 */
export function parseLearnedSkills(packet: WtPacket): {
  count: number;
  ids: number[];
} | null {
  const obj = packet.objects.find(
    (o) =>
      o.kind === 0x0c &&
      o.subtype === 1 &&
      (findField(o, "learnednum") || findField(o, "learnedskill")),
  );
  if (!obj) return null;
  const numRaw = findField(obj, "learnednum");
  const count = numRaw ? decodeTaggedNumber(numRaw) ?? 0 : 0;
  const blob = findField(obj, "learnedskill");
  const ids: number[] = [];
  if (blob && blob.length) {
    const r = new SeqReader(blob);
    try {
      while (r.remaining() >= 6 && ids.length < 64) {
        ids.push(r.u32());
      }
    } catch {
      /* truncated ok */
    }
  }
  return { count: count || ids.length, ids };
}

/** Pixi skill forget fee: 5金 = 50000铜. */
export const SKILL_FORGET_COST_GOLD = 5;
export const SKILL_FORGET_COST_COPPER = SKILL_FORGET_COST_GOLD * 10000;

/**
 * PixiJS-only: forget a learned skill for 5金.
 * Wire: `1/12/2 { skillid }` → `12/2 { result, skillid }` (+ `12/1` + `10/26` on ok).
 */
export function buildSkillForgetRequest(skillId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 0x0c, 2).u32("skillid", skillId >>> 0);
  return b.finish();
}

export type SkillForgetResult = {
  result: number;
  ok: boolean;
  message: string;
  skillId: number;
};

function skillForgetFailMessage(result: number): string {
  switch (result) {
    case 2:
      return "未学会该技能";
    case 3:
      return "金钱不足（需5金）";
    case 4:
      return "至少保留一个技能";
    default:
      return "遗忘失败";
  }
}

export function parseSkillForgetResult(
  packet: WtPacket,
): SkillForgetResult | null {
  const obj = packet.objects.find((o) => o.kind === 0x0c && o.subtype === 2);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  const skillId =
    fieldU32(obj, "skillid") ||
    fieldU32(obj, "skillId") ||
    fieldU32(obj, "id");
  if (result === 1) {
    return {
      result,
      ok: true,
      message: "遗忘成功，消耗5金",
      skillId,
    };
  }
  return {
    result,
    ok: false,
    message: skillForgetFailMessage(result),
    skillId,
  };
}

/** Demo / offline: append role-bound starter skill object (kind 12 subtype 1). */
export function appendDemoLearnedSkills(
  b: WtBuilder,
  jobIndex: number,
  skillIds?: number[],
): void {
  const ids =
    skillIds && skillIds.length
      ? skillIds
      : [starterSkillIdForJob(jobIndex)];
  const blob = new SeqWriter();
  for (const id of ids) blob.u32(id);
  b.object(1, 0x0c, 1)
    .u16("learnednum", ids.length)
    .raw("learnedskill", blob.finish());
}

export type SceneReload = {
  scene: string;
  x: number;
  y: number;
  /** true when 30/2 (may still carry posinfo on position-bearing closes). */
  soft: boolean;
  /** true when wire included posinfo (authoritative landing). */
  hasPos: boolean;
};

/**
 * 30/1 hard enter or 30/2 soft close. Soft packets often omit posinfo (ack-only);
 * full-bootstrap / portal completions put scene+posinfo on 30/2 — must read it.
 */
export function parseSceneReload(packet: WtPacket): SceneReload | null {
  const hard = packet.objects.find((o) => o.kind === 0x1e && o.subtype === 1);
  if (hard) {
    const sceneRaw = findField(hard, "scene");
    const posRaw = findField(hard, "posinfo");
    if (!sceneRaw) return null;
    const scene = ensureSceneFile(decodeFieldString(sceneRaw));
    let x = 0;
    let y = 0;
    let hasPos = false;
    if (posRaw && posRaw.length >= 4) {
      const r = new SeqReader(posRaw);
      x = r.i16();
      y = r.i16();
      hasPos = true;
    }
    return { scene, x, y, soft: false, hasPos };
  }
  const soft = packet.objects.find((o) => o.kind === 0x1e && o.subtype === 2);
  if (soft) {
    const sceneRaw = findField(soft, "scene");
    if (!sceneRaw) return null;
    const scene = ensureSceneFile(decodeFieldString(sceneRaw));
    const posRaw = findField(soft, "posinfo");
    let x = 0;
    let y = 0;
    let hasPos = false;
    if (posRaw && posRaw.length >= 4) {
      const r = new SeqReader(posRaw);
      x = r.i16();
      y = r.i16();
      hasPos = true;
    }
    return { scene, x, y, soft: true, hasPos };
  }
  return null;
}

/**
 * 27/12 name+posinfo landing (ui_apply_named_posinfo_target).
 * Evidence: mock append_fb_target_result12_for_scene; portal post-enter.
 */
export function parseNamedSceneLanding(
  packet: WtPacket,
): { scene: string; x: number; y: number } | null {
  const obj = packet.objects.find((o) => o.kind === 0x1b && o.subtype === 12);
  if (!obj) return null;
  const nameRaw = findField(obj, "name");
  const posRaw = findField(obj, "posinfo");
  if (!nameRaw || !posRaw || posRaw.length < 4) return null;
  const scene = ensureSceneFile(decodeFieldString(nameRaw));
  if (!scene) return null;
  const r = new SeqReader(posRaw);
  const x = r.i16();
  const y = r.i16();
  return { scene, x, y };
}

/** Prefer 30/1 → 30/2+posinfo → 16/3 unstuck → 27/12+posinfo for server landing. */
export function parseServerSceneLanding(
  packet: WtPacket,
): { scene: string; x: number; y: number; source: string } | null {
  const hard = packet.objects.find((o) => o.kind === 0x1e && o.subtype === 1);
  if (hard) {
    const sceneRaw = findField(hard, "scene");
    const posRaw = findField(hard, "posinfo");
    if (sceneRaw && posRaw && posRaw.length >= 4) {
      const r = new SeqReader(posRaw);
      return {
        scene: ensureSceneFile(decodeFieldString(sceneRaw)),
        x: r.i16(),
        y: r.i16(),
        source: "30/1",
      };
    }
  }
  const soft = packet.objects.find((o) => o.kind === 0x1e && o.subtype === 2);
  if (soft) {
    const sceneRaw = findField(soft, "scene");
    const posRaw = findField(soft, "posinfo");
    if (sceneRaw && posRaw && posRaw.length >= 4) {
      const r = new SeqReader(posRaw);
      return {
        scene: ensureSceneFile(decodeFieldString(sceneRaw)),
        x: r.i16(),
        y: r.i16(),
        source: "30/2",
      };
    }
  }
  const unstuck = parseMmGameUnstuckLanding(packet);
  if (unstuck) return { ...unstuck, source: "16/3" };
  const named = parseNamedSceneLanding(packet);
  if (named) return { ...named, source: "27/12" };
  return null;
}

/**
 * Settings menu「脱离卡死」: WT 0|1 / 12/3 { id }.
 * Evidence: docs/re/2026-06-27-unstuck-current-scene-reload.md (mmGame:0x5BCA).
 */
export function buildSettingsUnstuckRequest(id = 2): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 0x0c, 3).u32("id", id >>> 0);
  return b.finish();
}

/**
 * mmGame scene-transfer landing from settings unstuck: 16/3 { result=2, scene, posinfo, exitid }.
 * Compact path may use 16/2 result=1 with the same scene fields.
 */
export function parseMmGameUnstuckLanding(
  packet: WtPacket,
): { scene: string; x: number; y: number } | null {
  const obj =
    packet.objects.find((o) => o.kind === 0x10 && o.subtype === 3) ||
    packet.objects.find((o) => o.kind === 0x10 && o.subtype === 2);
  if (!obj) return null;
  const sceneRaw = findField(obj, "scene");
  const posRaw = findField(obj, "posinfo");
  if (!sceneRaw || !posRaw || posRaw.length < 4) return null;
  const scene = ensureSceneFile(decodeFieldString(sceneRaw));
  if (!scene) return null;
  const r = new SeqReader(posRaw);
  const x = r.i16();
  const y = r.i16();
  if (x === 0 && y === 0) return null;
  return { scene, x, y };
}

/** mmBattle:sub_7794 — 4/7.combatinfo hangup session snapshot (8×u32 + u8). */
export type HangupCombatinfo = {
  completed: number;
  maximum: number;
  totalExp: number;
  totalGold: number;
  totalMpRecovered: number;
  totalHpRecovered: number;
};

export type BattleSettlement = {
  /** Absolute totals on the wire (HandleBattleSettleMsg). */
  expTotal: number;
  goldTotal: number;
  level: number;
  lastExp: number;
  curExp: number;
  percentExp: number;
  /** Display deltas when client baseline known. */
  expGain: number;
  goldGain: number;
  /**
   * 4/7.hp / 4/7.mp — settle recovery deltas (auto-flask / recover), not absolute
   * vitals. Applied onto battle seat HP/MP before leaving to the world HUD.
   */
  recoverHp: number;
  recoverMp: number;
  fdata: string;
  result: number;
  /** Present when server (or CBE shim) appends 4/7.combatinfo during hangup. */
  hangupCombatinfo?: HangupCombatinfo;
};

export type BattleActionChild = {
  wire: number;
  flag: number;
  /** Signed HP delta from server valueA (playback only; never invent client-side). */
  delta: number;
  valueB: number;
};

/** One record in the 4/6 actioninfo round script (server-authored). */
export type BattleActionRecord = {
  type: number;
  actorWire: number;
  children: BattleActionChild[];
  /** type 1/2 skill effect index from eidolon.dsh (optional). */
  effectIndex?: number;
  effectText?: string;
};

export type BattleActionUpdate = {
  /** True when 4/8 exit (or escape success) — leave battle UI. */
  finished: boolean;
  /** True when 4/7 settlement arrived (show panel; do not tear down yet). */
  settled: boolean;
  escaped: boolean;
  escapeOk: boolean;
  log: string;
  /** Absolute HP snapshots (legacy / when server sends post-hit values). */
  leftHp: { id: number; hp: number }[];
  rightHp: { id: number; hp: number; mp?: number }[];
  /**
   * Signed HP deltas flattened from actioninfo children (valueA).
   * Client only plays these — damage was computed on the server.
   */
  hpDeltas: { wire: number; delta: number; flag?: number }[];
  /** Full round script for skill/death playback ordering. */
  actions: BattleActionRecord[];
  settlement?: BattleSettlement;
};

function fieldU32(obj: { fields: { name: string; raw: Uint8Array }[] }, name: string): number {
  const raw = findField(obj as never, name);
  if (!raw) return 0;
  return decodeTaggedNumber(raw) ?? decodeAsciiDigitResult(raw) ?? 0;
}

function readU32BE(buf: Uint8Array, off: number): number {
  return (
    ((buf[off]! << 24) |
      (buf[off + 1]! << 16) |
      (buf[off + 2]! << 8) |
      buf[off + 3]!) >>>
    0
  );
}

/** Parse 4/7.combatinfo raw (00 04 + u32)×8 then optional 00 01 + u8 flag). */
export function parseHangupCombatinfo(raw: Uint8Array): HangupCombatinfo | null {
  try {
    const r = new SeqReader(raw);
    const completed = r.u32();
    const maximum = r.u32();
    const totalExp = r.u32();
    const totalGold = r.u32();
    const totalMpRecovered = r.u32();
    const totalHpRecovered = r.u32();
    r.u32();
    r.u32();
    if (r.remaining() >= 3) r.u8();
    return {
      completed: completed >>> 0,
      maximum: maximum >>> 0,
      totalExp: totalExp >>> 0,
      totalGold: totalGold >>> 0,
      totalMpRecovered: totalMpRecovered >>> 0,
      totalHpRecovered: totalHpRecovered >>> 0,
    };
  } catch {
    return null;
  }
}

/** Mock overlapped teaminfo + demo tagged triple-u32 rows. */
export function parseTeaminfoBlob(
  team: Uint8Array,
): { id: number; hp: number; mp: number }[] {
  const rows: { id: number; hp: number; mp: number }[] = [];
  const taggedTriple =
    team.length >= 18 &&
    team[0] === 0 &&
    team[1] === 4 &&
    team[6] === 0 &&
    team[7] === 4 &&
    team[12] === 0 &&
    team[13] === 4;
  if (!taggedTriple) {
    let pos = 0;
    while (pos + 14 <= team.length && team[pos] === 0 && team[pos + 1] === 4) {
      rows.push({
        id: readU32BE(team, pos + 2),
        hp: readU32BE(team, pos + 6),
        mp: readU32BE(team, pos + 10),
      });
      pos += 14;
    }
    if (rows.length) return rows;
  }
  try {
    const r = new SeqReader(team);
    while (r.remaining() >= 18) {
      rows.push({ id: r.u32(), hp: r.u32(), mp: r.u32() });
    }
  } catch {
    /* partial ok */
  }
  return rows;
}

/**
 * Parse 4/6 actioninfo as a server round script.
 * Mock encodes type/actor/childCount/target/flag via seq_put_u8 (tagged
 * `00 01 xx`); valueA/valueB/effect via seq_put_u32 (`00 04` + BE32).
 * Demo blobs may still use bare u8 — accept both (same as SeqReader.u8).
 * Client must not invent HP changes — only replay type/actor/children/valueA.
 */
export function parseActionInfoScript(
  actionInfo: Uint8Array,
  actionnum: number,
): BattleActionRecord[] {
  const actions: BattleActionRecord[] = [];
  let pos = 0;
  const flexU8 = (): number => {
    if (pos >= actionInfo.length) throw new Error("actioninfo eof");
    if (
      pos + 2 < actionInfo.length &&
      actionInfo[pos] === 0 &&
      actionInfo[pos + 1] === 1
    ) {
      const v = actionInfo[pos + 2]!;
      pos += 3;
      return v;
    }
    return actionInfo[pos++]!;
  };
  const tagU32 = (): number => {
    if (pos + 6 > actionInfo.length) throw new Error("actioninfo u32");
    if (actionInfo[pos] !== 0 || actionInfo[pos + 1] !== 4) {
      throw new Error("actioninfo expected tagged u32");
    }
    const v =
      ((actionInfo[pos + 2]! << 24) |
        (actionInfo[pos + 3]! << 16) |
        (actionInfo[pos + 4]! << 8) |
        (actionInfo[pos + 5]!)) >>>
      0;
    pos += 6;
    return v;
  };
  const tagString = (): string => {
    if (pos + 2 > actionInfo.length) return "";
    const len = (actionInfo[pos]! << 8) | actionInfo[pos + 1]!;
    pos += 2;
    const bytes = actionInfo.subarray(pos, pos + len);
    pos += len;
    // seq_put_string includes trailing NUL inside len
    let end = bytes.length;
    if (end > 0 && bytes[end - 1] === 0) end -= 1;
    return decodeGbk(bytes.subarray(0, end));
  };
  const signedDelta = (u: number): number => (u | 0);

  for (let a = 0; a < actionnum && pos < actionInfo.length; a++) {
    try {
      const type = flexU8();
      const actorWire = flexU8();
      if (type === 3 || type === 4) {
        actions.push({ type, actorWire, children: [] });
        continue;
      }
      const childCount = flexU8();
      const children: BattleActionChild[] = [];
      for (let c = 0; c < childCount; c++) {
        const wire = flexU8();
        const flag = flexU8();
        const valueA = tagU32();
        const valueB = tagU32();
        children.push({ wire, flag, delta: signedDelta(valueA), valueB });
      }
      const rec: BattleActionRecord = { type, actorWire, children };
      if (type === 1 || type === 2) {
        rec.effectIndex = tagU32();
        rec.effectText = tagString();
        // Three tail bytes — mock writes them as tagged u8 each.
        flexU8();
        flexU8();
        flexU8();
      }
      actions.push(rec);
    } catch (e) {
      console.warn(
        `[battle] actioninfo parse stop at record ${a}/${actionnum} pos=${pos}/${actionInfo.length}`,
        e,
      );
      break;
    }
  }
  return actions;
}

/** @deprecated Prefer parseActionInfoScript; kept for narrow delta tests. */
export function parseActionInfoDeltas(
  actionInfo: Uint8Array,
  actionnum: number,
): { wire: number; delta: number; flag: number }[] {
  return parseActionInfoScript(actionInfo, actionnum).flatMap((rec) =>
    rec.children.map((c) => ({ wire: c.wire, delta: c.delta, flag: c.flag })),
  );
}

function childFlagLabel(flag: number): string {
  if (flag === 2) return "暴击";
  if (flag === 3) return "闪躲";
  return "";
}

function formatPlaybackLog(actions: BattleActionRecord[]): string {
  if (!actions.length) return "actioninfo";
  const bits: string[] = [];
  for (const rec of actions) {
    if (rec.type === 3) {
      bits.push("倒下");
      continue;
    }
    if (rec.type === 4) {
      bits.push("退场");
      continue;
    }
    const kind = rec.type === 1 ? "技" : rec.type === 2 ? "物" : "攻";
    for (const c of rec.children) {
      const fl = childFlagLabel(c.flag);
      if (fl === "闪躲") {
        bits.push(`${kind}:闪躲`);
      } else if (c.delta !== 0) {
        const n = c.delta < 0 ? `${c.delta}` : `+${c.delta}`;
        bits.push(fl ? `${kind}${n}${fl}` : `${kind}${n}`);
      } else if (rec.effectText) {
        bits.push(rec.effectText.slice(0, 6));
      }
    }
  }
  return bits.length ? bits.join(" ") : `action×${actions.length}`;
}

export function parseBattleAction(packet: WtPacket): BattleActionUpdate | null {
  const esc = packet.objects.find((o) => o.kind === 4 && o.subtype === 4);
  let escapeOk: boolean | null = null;
  if (esc) {
    const resultRaw = findField(esc, "result");
    escapeOk = resultRaw
      ? (decodeAsciiDigitResult(resultRaw) ?? 0) === 1
      : true;
    /* Success: only 4/4 {result=1}; battle ends (mmBattle:0x7F18). */
    if (escapeOk) {
      return {
        finished: true,
        settled: false,
        escaped: true,
        escapeOk: true,
        log: "逃跑成功",
        leftHp: [],
        rightHp: [],
        hpDeltas: [],
        actions: [],
      };
    }
    /* Fail: 4/4 {result=0} + 4/6 enemy counters in the same WT — fall through
     * so the other side still plays (docs/re/2026-06-25-battle-server-flow.md). */
  }

  /*
   * Spar / duel: each hit is its own 4/6 (team_count=1 InitActionSlot_B),
   * merged into one WT in delivery order.  Native CBE walks every object;
   * only taking .find() first dropped the peer's hit (second 4/6).
   */
  const acts = packet.objects.filter((o) => o.kind === 4 && o.subtype === 6);
  const settleObj = packet.objects.find((o) => o.kind === 4 && o.subtype === 7);
  const exit = packet.objects.find((o) => o.kind === 4 && o.subtype === 8);
  if (!acts.length && !settleObj && !exit && escapeOk === null) return null;
  /* Escape-fail with no counters (no living enemies) still needs a UI tick. */
  if (!acts.length && !settleObj && !exit && escapeOk === false) {
    return {
      finished: false,
      settled: false,
      escaped: true,
      escapeOk: false,
      log: "逃跑失败",
      leftHp: [],
      rightHp: [],
      hpDeltas: [],
      actions: [],
    };
  }

  const leftHp: { id: number; hp: number }[] = [];
  const rightHp: { id: number; hp: number; mp?: number }[] = [];
  const hpDeltas: { wire: number; delta: number; flag?: number }[] = [];
  let actions: BattleActionRecord[] = [];
  let log = "回合结算";
  for (const act of acts) {
    const numRaw = findField(act, "actionnum");
    const n = numRaw ? decodeAsciiDigitResult(numRaw) ?? numRaw[2] ?? 0 : 0;
    const actionInfo = findField(act, "actioninfo");
    if (actionInfo && n > 0) {
      const chunk = parseActionInfoScript(actionInfo, n);
      actions = actions.concat(chunk);
      for (const rec of chunk) {
        for (const c of rec.children) {
          hpDeltas.push({ wire: c.wire, delta: c.delta, flag: c.flag });
        }
      }
    }
    const team = findField(act, "teaminfo");
    if (team) {
      for (const row of parseTeaminfoBlob(team)) {
        // CBE InitActionSlot_B: id + mp authoritative; hp field overlapped/ignored.
        // Later 4/6 teaminfo wins for the same id (fresher MP after skill).
        const prev = rightHp.findIndex((r) => r.id === row.id);
        const next = { id: row.id, hp: row.hp, mp: row.mp };
        if (prev >= 0) rightHp[prev] = next;
        else rightHp.push(next);
      }
    }
  }
  if (escapeOk === false) {
    log = actions.length
      ? `逃跑失败 · ${formatPlaybackLog(actions)}`
      : "逃跑失败";
  } else if (actions.length) {
    log = formatPlaybackLog(actions);
  } else if (acts.length) {
    log = `actioninfo ×${acts.length}`;
  }

  let settlement: BattleSettlement | undefined;
  if (settleObj) {
    const expTotal = fieldU32(settleObj, "exp");
    const goldTotal = fieldU32(settleObj, "gold");
    const level = fieldU32(settleObj, "level");
    const lastExp = fieldU32(settleObj, "lastexp");
    const curExp = fieldU32(settleObj, "curexp");
    const percentExp = fieldU32(settleObj, "persentexp");
    const result = fieldU32(settleObj, "result");
    const recoverHp = fieldU32(settleObj, "hp");
    const recoverMp = fieldU32(settleObj, "mp");
    const fRaw = findField(settleObj, "fdata");
    const fdata = fRaw ? decodeFieldString(fRaw) : "";
    const combatRaw = findField(settleObj, "combatinfo");
    const hangupCombatinfo = combatRaw
      ? parseHangupCombatinfo(combatRaw)
      : undefined;
    settlement = {
      expTotal,
      goldTotal,
      level,
      lastExp,
      curExp,
      percentExp,
      expGain: 0,
      goldGain: 0,
      recoverHp,
      recoverMp,
      fdata,
      result,
      hangupCombatinfo: hangupCombatinfo ?? undefined,
    };
    log += ` · 结算 EXP=${expTotal} 金=${goldTotal}`;
    if (recoverHp || recoverMp) log += ` 回血蓝=${recoverHp}/${recoverMp}`;
    if (fdata) log += ` ${fdata.slice(0, 12)}`;
  }
  if (exit) log += " · 退出战斗";
  return {
    // Exit (4/8) leaves battle; settle alone keeps the panel open.
    finished: !!exit,
    settled: !!settleObj,
    escaped: escapeOk !== null,
    // Escape success already returned above; only null | false reach here.
    escapeOk: false,
    log,
    leftHp,
    rightHp,
    hpDeltas,
    actions,
    settlement,
  };
}

export type NpcDialog = {
  title: string;
  body: string;
  options: {
    label: string;
    action: number;
    value: number;
    description?: string;
    displayType?: number;
  }[];
  /** Soft buttons from dialog blob (usually empty). */
  buttons?: { label: string; type: number }[];
  hidebtn?: number;
};

export function parseTaskDetail(packet: WtPacket): string | null {
  const obj = packet.objects.find((o) => o.kind === 6 && o.subtype === 10);
  if (!obj) return null;
  const info = findField(obj, "info");
  return info ? decodeFieldString(info) : null;
}

export function parseTaskAcceptResult(
  packet: WtPacket,
): { ok: boolean; taskId?: number } | null {
  const obj = packet.objects.find((o) => o.kind === 6 && o.subtype === 11);
  if (!obj) return null;
  const resultRaw = findField(obj, "result");
  const result = resultRaw
    ? decodeAsciiDigitResult(resultRaw) ?? resultRaw[2] ?? 1
    : 1;
  return { ok: result === 0 };
}

export function parseTaskSubmitResult(
  packet: WtPacket,
): { ok: boolean; text?: string } | null {
  const obj = packet.objects.find((o) => o.kind === 6 && o.subtype === 4);
  if (!obj) return null;
  const resultRaw = findField(obj, "result");
  const result = resultRaw
    ? decodeAsciiDigitResult(resultRaw) ?? resultRaw[2] ?? 0
    : 0;
  const textRaw = findField(obj, "taskdes") || findField(obj, "info");
  return {
    ok: result === 1,
    text: textRaw ? decodeFieldString(textRaw) : undefined,
  };
}

export function parseTaskAbandonResult(
  packet: WtPacket,
): { ok: boolean } | null {
  const obj = packet.objects.find((o) => o.kind === 6 && o.subtype === 7);
  if (!obj) return null;
  const resultRaw = findField(obj, "result");
  const result = resultRaw
    ? decodeAsciiDigitResult(resultRaw) ?? resultRaw[2] ?? 1
    : 1;
  return { ok: result === 0 };
}

function parseAcceptedTaskInfoBlob(raw: Uint8Array): TaskListRow[] {
  const rows: TaskListRow[] = [];
  const r = new SeqReader(raw);
  try {
    while (r.remaining() > 8) {
      const taskId = r.u32();
      const difficulty = r.u8();
      const classification = r.u8();
      const name = r.string().replace(/\0+$/g, "");
      const giver = r.string().replace(/\0+$/g, "");
      const level = r.u8();
      const receiver = r.string().replace(/\0+$/g, "");
      const reqType1 = r.u8();
      const reqId1 = r.u32();
      r.string(); // unused prompt
      const progress1 = r.u8();
      const reqCount1 = r.u8();
      const reqType2 = r.u8();
      const reqId2 = r.u32();
      r.string();
      const progress2 = r.u8();
      const reqCount2 = r.u8();
      r.i16(); // pad
      const state = r.u8();
      rows.push({
        taskId,
        difficulty,
        classification,
        name,
        giver,
        level,
        receiver,
        reqType1,
        reqId1,
        progress1,
        reqCount1,
        reqType2,
        reqId2,
        progress2,
        reqCount2,
        state,
      });
    }
  } catch {
    /* truncated / partial row */
  }
  return rows;
}

function parseCandidateTaskInfoBlob(raw: Uint8Array): TaskCandidateRow[] {
  const rows: TaskCandidateRow[] = [];
  const r = new SeqReader(raw);
  try {
    while (r.remaining() > 6) {
      const taskId = r.u32();
      const difficulty = r.u8();
      const classification = r.u8();
      const name = r.string().replace(/\0+$/g, "");
      const giver = r.string().replace(/\0+$/g, "");
      const level = r.u8();
      rows.push({ taskId, difficulty, classification, name, giver, level });
    }
  } catch {
    /* truncated */
  }
  return rows;
}

/** Parse accepted (6/1) and/or candidates (6/14) from a list-query response. */
export function parseTaskListQuery(packet: WtPacket): {
  accepted: TaskListRow[];
  candidates: TaskCandidateRow[];
} {
  const accepted: TaskListRow[] = [];
  const candidates: TaskCandidateRow[] = [];
  for (const obj of packet.objects) {
    if (obj.kind !== 6) continue;
    const info = findField(obj, "taskinfo");
    if (!info) continue;
    if (obj.subtype === 1) accepted.push(...parseAcceptedTaskInfoBlob(info));
    else if (obj.subtype === 14) candidates.push(...parseCandidateTaskInfoBlob(info));
  }
  return { accepted, candidates };
}

/**
 * CBE `scene_refresh_interact_prompt_types` (0x01017C6C) node+326:
 * 1 = yellow ? (可提交), 2 = yellow ! (可接), 3 = grey ? (未完成).
 * Priority when multiple match the same NPC name: 1 > 2 > 3.
 */
export type NpcTaskPromptType = 1 | 2 | 3;

export function computeNpcTaskPrompts(
  accepted: TaskListRow[],
  candidates: TaskCandidateRow[],
): Map<string, NpcTaskPromptType> {
  const map = new Map<string, NpcTaskPromptType>();
  const rank = (t: number) => (t === 1 ? 3 : t === 2 ? 2 : t === 3 ? 1 : 0);
  const set = (name: string, type: NpcTaskPromptType) => {
    const key = name.replace(/\0+$/g, "").trim();
    if (!key) return;
    const cur = map.get(key) ?? 0;
    if (rank(type) > rank(cur)) map.set(key, type);
  };
  for (const c of candidates) set(c.giver, 2);
  for (const a of accepted) {
    if (a.state === 2) set(a.receiver, 1);
    else if (a.state === 1) set(a.receiver, 3);
  }
  return map;
}

/** 30/9 challenge confirm prompt (after empty 26/0). */
export function parseChallengePrompt(
  packet: WtPacket,
): { isLeader: number; text: string; enemyId: number } | null {
  const obj = packet.objects.find((o) => o.kind === 30 && o.subtype === 9);
  if (!obj) return null;
  const leaderRaw = findField(obj, "isleader");
  const isLeader = leaderRaw
    ? decodeAsciiDigitResult(leaderRaw) ?? leaderRaw[2] ?? 0
    : 0;
  const challenge = findField(obj, "challenge");
  if (!challenge) return null;
  const enemyRaw = findField(obj, "enemy");
  const enemyId = enemyRaw
    ? decodeTaggedNumber(enemyRaw) ?? decodeAsciiDigitResult(enemyRaw) ?? 0
    : 0;
  return {
    isLeader,
    text: decodeFieldString(challenge) || "是否挑战守关怪？",
    enemyId: enemyId >>> 0,
  };
}

export function parseNpcDialog(packet: WtPacket): NpcDialog | null {
  const obj = packet.objects.find((o) => o.kind === 26 && o.subtype === 1);
  if (!obj) return null;

  const dialogRaw = findField(obj, "dialog");
  if (dialogRaw && dialogRaw.length >= 1) {
    try {
      const r = new SeqReader(dialogRaw);
      r.u8(); // dialog-kind
      const body = r.string().replace(/\0+$/g, "");
      const optionCount = r.u8();
      const options: NpcDialog["options"] = [];
      for (let i = 0; i < optionCount && r.remaining() > 0; i++) {
        const displayType = r.u8();
        const label = r.string().replace(/\0+$/g, "");
        const action = r.u8();
        const value = r.u32();
        const description = r.string().replace(/\0+$/g, "");
        options.push({ label, action, value, description, displayType });
      }
      const buttons: NonNullable<NpcDialog["buttons"]> = [];
      if (r.remaining() > 0) {
        const buttonCount = r.u8();
        for (let i = 0; i < buttonCount && r.remaining() > 0; i++) {
          const type = r.u8();
          const label = r.string().replace(/\0+$/g, "");
          if (r.remaining() > 0) r.u8(); // final-byte
          buttons.push({ label, type });
        }
      }
      const hideRaw = findField(obj, "hidebtn");
      const hidebtn = hideRaw
        ? decodeAsciiDigitResult(hideRaw) ?? hideRaw[2] ?? 0
        : 0;
      return {
        title: "对话",
        body: body || "(对话)",
        options,
        buttons,
        hidebtn,
      };
    } catch {
      /* fall through to legacy flat fields */
    }
  }

  // Legacy / demo flat fields (title + info + option blob).
  const title = findField(obj, "title")
    ? decodeFieldString(findField(obj, "title")!)
    : "对话";
  const body =
    (findField(obj, "info") && decodeFieldString(findField(obj, "info")!)) ||
    (findField(obj, "data") && decodeFieldString(findField(obj, "data")!)) ||
    (findField(obj, "text") && decodeFieldString(findField(obj, "text")!)) ||
    "";
  const options: NpcDialog["options"] = [];
  const optRaw = findField(obj, "option") || findField(obj, "options");
  if (optRaw) {
    try {
      const r = new SeqReader(optRaw);
      const count = r.u8();
      for (let i = 0; i < count && r.remaining() > 0; i++) {
        const label = r.string();
        const action = r.u32();
        const value = r.u32();
        options.push({ label, action, value });
      }
    } catch {
      /* keep title/body */
    }
  }
  if (!body && !options.length && !findField(obj, "result")) return null;
  return { title, body: body || "(对话)", options };
}

export type ChatLine = {
  type: number;
  text: string;
  /** Wire sender role / nearby actor id; 0 = system / unknown. */
  senderRoleId?: number;
  senderName?: string;
};

/** Strip put_object_blob inner BE16(dataLen) prefix. */
function unwrapChatinfoBlob(raw: Uint8Array): Uint8Array {
  if (raw.length >= 2) {
    const len = ((raw[0]! << 8) | raw[1]!) >>> 0;
    if (len > 0 && 2 + len <= raw.length) return raw.subarray(2, 2 + len);
  }
  return raw;
}

export function parseChatNotices(packet: WtPacket): ChatLine[] {
  const lines: ChatLine[] = [];
  for (const obj of packet.objects) {
    if (obj.kind !== 3 || obj.subtype !== 3) continue;
    const typeRaw = findField(obj, "type");
    const type = typeRaw ? decodeAsciiDigitResult(typeRaw) ?? typeRaw[2] ?? 0 : 0;
    const raw = findField(obj, "chatinfo");
    if (!raw || raw.length < 1) continue;
    // chatinfo value = BE16(len) + [u8 count][seq rows…]
    // (docs/re/2026-07-16-chat-message-system.md). Using raw[0] as count
    // treated the length high-byte (usually 0) as zero messages.
    const info = unwrapChatinfoBlob(raw);
    if (info.length < 1) continue;
    const count = info[0]!;
    const r = new SeqReader(info.subarray(1));
    for (let i = 0; i < count && r.remaining() > 0; i++) {
      try {
        const name = r.string().replace(/\0+$/g, "");
        const senderRoleId = r.u32(); // tagged; 0 = server system broadcast
        const msg = r.string().replace(/\0+$/g, "");
        // World [世]: drop player nick "系统" (nonzero roleId). Keep official
        // enhance/chest world announces (name=系统, roleId=0) and type-5 tips.
        if (type === 0 && name.trim() === "系统" && senderRoleId !== 0) {
          continue;
        }
        lines.push({
          type,
          text: `[${chatPrefix(type)}]${name}: ${msg}`,
          senderRoleId: senderRoleId >>> 0,
          senderName: name,
        });
      } catch {
        break;
      }
    }
  }
  return lines;
}

/** Server account-login takeover tip (GBK decoded). */
export function isAccountKickChatLine(text: string): boolean {
  return text.includes("其他地方登录") || text.includes("强制下线");
}

function chatPrefix(type: number): string {
  switch (type) {
    case 0:
      return "世";
    case 2:
      return "队";
    case 3:
      return "帮";
    case 4:
      return "本";
    case 5:
    case 6:
      return "系";
    case 7:
      return "私";
    default:
      return "?";
  }
}

export type BagEnhanceAttr = {
  unlock: number;
  type: number;
  flag: number;
  value: number;
  /** PixiJS: washed override from enhance_wash (wire flag bit7). */
  washed?: boolean;
};

export type BagItem = {
  itemId: number;
  seq: number;
  count: number;
  name: string;
  quality: number;
  enhance: number;
  kind: "item" | "equip";
  /** item.dsh / equip.dsh 类别 — UI 分类筛选用. */
  category: number;
  desc: string;
  /** True when category-10 consumable or HP/MP/EXP/consumable effect (mock catalog rule). */
  usable: boolean;
  /** Worn body slot when equipment (0..7), else -1. */
  equipSlot: number;
  /**
   * Current durability (CBE item+0x110).
   * - Worn `7/7 type=2`: wire u32 count.
   * - Bag equip: wire count (login/grant = 1); preserved across bag refresh /
   *   unequip move (30/21 does not rewrite +0x110 on CBE).
   */
  durability?: number;
  /** Common-extra milestone / enhance attrs (server-authoritative, may include wash). */
  enhanceAttrs?: BagEnhanceAttr[];
};

/**
 * 神仙壶/逍遥壶：背包 u32 count 是 HP/MP 储量，可见堆叠恒为 1
 *（mmGame:0xD04 / docs/re/2026-07-20-vitality-flask-reserve.md）。
 */
export function isReservoirFlaskItemId(itemId: number): boolean {
  return itemId === 802 || itemId === 803;
}

/** 商城仓库/出售凭证：bag.count = 剩余耐久（打开次数），非堆叠张数。 */
export function isDurableMallPassItemId(itemId: number): boolean {
  return itemId === 834 || itemId === 839;
}

/** Visible stack for UI qty pickers — flasks/equip are atomic (1). */
export function bagVisibleStackCount(it: BagItem): number {
  if (
    it.kind === "equip" ||
    isReservoirFlaskItemId(it.itemId) ||
    isDurableMallPassItemId(it.itemId)
  )
    return 1;
  return Math.max(0, it.count >>> 0);
}

/**
 * Battle「物品」menu: HP/MP medicines (item.dsh category 10) and flasks 802/803.
 * Excludes EXP-only cards and non-restore props; never equipment.
 * Prefer live item.dsh via lookupItemSync — bag.category may be 0 when the row
 * was enriched before catalogs warmed.
 */
export function isBattleMedicineOrFlask(it: BagItem): boolean {
  if (it.seq <= 0 || it.count <= 0) return false;
  if (isReservoirFlaskItemId(it.itemId)) return true;
  const cat = lookupItemSync(it.itemId);
  const kind = cat?.kind ?? it.kind;
  if (kind !== "item") return false;
  const category = cat?.category ?? it.category;
  if (category !== 10) return false;
  if (cat) return cat.hpChange !== 0 || cat.mpChange !== 0;
  return it.usable;
}

export type WornSlot = {
  slot: number;
  slotName: string;
  item: BagItem | null;
};

export type ShopGoods = {
  itemId: number;
  name: string;
  price: number;
  stock: number;
  visual: number;
  quality: number;
  kind: "item" | "equip";
  /** item.dsh / equip.dsh 类别 — UI 分类筛选用. */
  category: number;
  /** item.dsh 说明 — 商城详情暗色说明区. */
  desc: string;
};

export type MallState = {
  coolmoney: number;
  ticket: number;
  goods: ShopGoods[];
  /** True when this update carried 14/4 balance (do not clobber with 0). */
  hasBalance?: boolean;
};

export type CharacterStats = {
  name: string;
  level: number;
  title: string;
  job: number;
  spouse: string;
  /** Married partner role id; 0 = unmarried. */
  spouseRoleId: number;
  guild: string;
  hp: number;
  hpMax: number;
  mp: number;
  mpMax: number;
  charm: number;
  strength: number;
  agility: number;
  wisdom: number;
  endurance: number;
  /** Festival / arena vitality currency (account_role_vitality). */
  vitality: number;
  vitalityMax: number;
  /** Full build_player_stats attack (jobPrimary + equip). */
  attack: number;
  /** Full defense (bare + equip.armor). */
  armor: number;
  hit: number;
  dodge: number;
  crit: number;
  resist: number;
  baseStrength: number;
  baseAgility: number;
  baseWisdom: number;
  eqStrength: number;
  eqAgility: number;
  eqWisdom: number;
  eqAttack: number;
  eqArmor: number;
  exp: number;
  /** Next level start exp (may exceed i32; UI may show as signed). */
  expNext: number;
  money: number;
  coolmoney: number;
  /** Field PK score; red name when >= 60. */
  pkPoints: number;
  backpackCapacity: number;
};

/** ParseEquipAttributes common-extra; returns enhance + attr slots. */
export function readItemCommonExtra(r: SeqReader): {
  enhance: number;
  stackHint: number;
  attrs: BagEnhanceAttr[];
} {
  let enhance = 0;
  let stackHint = 1;
  const attrs: BagEnhanceAttr[] = [];
  try {
    const first = r.i16();
    const second = r.i16();
    const attrCount = r.u8();
    enhance = first & 0xff;
    stackHint = first & 0xff;
    void second;
    for (let a = 0; a < attrCount && a < 8; a++) {
      const unlock = r.u8();
      const type = r.u8();
      const rawFlag = r.u8();
      const value = r.i16();
      const washed = (rawFlag & 0x80) !== 0;
      const flag = rawFlag & 0x01;
      attrs.push({ unlock, type, flag, value, ...(washed ? { washed: true } : {}) });
    }
  } catch {
    /* truncated extra ok */
  }
  return { enhance, stackHint, attrs };
}

export function enrichBagRow(
  itemId: number,
  seq: number,
  count: number,
  enhance: number,
  enhanceAttrs?: BagEnhanceAttr[],
): BagItem {
  const cat = lookupItemSync(itemId);
  const kind = cat?.kind ?? (itemId >= 1000 ? "equip" : "item");
  const quality = cat?.quality ?? 0;
  // Mirror vm_net_mock_item_effect_is_usable: category 10 (道具/消耗) or any
  // HP/MP/EXP effect. Also honor item.dsh 是否消耗 when set. Equip never usable.
  const usable = !!(
    cat &&
    cat.kind === "item" &&
    (cat.category === 10 ||
      cat.hpChange ||
      cat.mpChange ||
      cat.expChange ||
      cat.consumable)
  );
  const wireCount = Math.max(0, count >>> 0);
  // Bag/grid equip wire count → item+0x110 (mmGame:sub_D04). Login/grant sends
  // instance 1, so new bag rows show current durability 1 until unequip-move
  // preserves a worn value or a later type=2 path rewrites it.
  const durability = kind === "equip" ? (wireCount > 0 ? wireCount : 1) : undefined;
  return {
    itemId,
    seq,
    count: kind === "equip" ? 1 : Math.max(1, wireCount),
    name: cat?.name ?? `#${itemId}`,
    quality,
    enhance: kind === "equip" ? enhance : 0,
    kind,
    category: cat?.category ?? 0,
    desc: cat?.desc ?? "",
    usable,
    equipSlot: cat?.equipSlot ?? -1,
    ...(durability != null ? { durability } : {}),
    ...(kind === "equip" && enhanceAttrs && enhanceAttrs.length
      ? { enhanceAttrs }
      : {}),
  };
}

/**
 * Parse a full backpack snapshot from a packet.
 * - 17/1: i16 n; repeat u32 itemId + common-extra (no seq on wire)
 * - 30/21: repeat u32 itemId, i16 seq, u32 count, common-extra
 *
 * When both 17/1 and 30/21 are present (Pixi 7/23), keep 17/1 row order but
 * overlay real seq/count from the grid (matched by itemId occurrence order).
 * Prefer 17/1 alone over a truncated lone 30/21; never invent seq from row index
 * when a grid seq is available — fake seq makes 7/15 report「宝箱不存在」。
 *
 * Do NOT treat `7/7 type=1` as a full snapshot. Those rows are incremental
 * (battle drops / warehouse / shop peel) and must merge via
 * `parseBagItemUpdates` — otherwise a lone drop packet replaces the whole bag
 * and battle「物品」loses medicines/flasks (docs/re/2026-08-07-pixijs-system-panels.md).
 */
export function parseBagItems(packet: WtPacket): BagItem[] {
  const from17 = parseBagItems17(packet);
  const fromGrid = parseBagGrid30(packet);
  if (from17.length && fromGrid.length) {
    return mergeBag17WithGridSeq(from17, fromGrid);
  }
  if (from17.length) return from17;
  if (fromGrid.length) return fromGrid;
  return [];
}

/**
 * 7/11 `info`: u8 n + repeat { i16 seq, u32 reservoir }.
 * 802/803 储量权威；30/21 可见堆叠恒为 1。
 */
export function parseFlaskReservoirUpdates(
  packet: WtPacket,
): Map<number, number> {
  const map = new Map<number, number>();
  for (const obj of packet.objects) {
    if (obj.kind !== 7 || obj.subtype !== 11) continue;
    const raw = findField(obj, "info");
    if (!raw || raw.length < 1) continue;
    const r = new SeqReader(raw);
    try {
      const n = r.u8();
      for (let i = 0; i < n && r.remaining() >= 6; i++) {
        const seq = r.i16();
        const value = r.u32() >>> 0;
        if (seq > 0) map.set(seq, value);
      }
    } catch {
      /* truncated */
    }
  }
  return map;
}

/** Apply 7/11 reservoirs onto bag rows (by seq). count=0 marks empty flask. */
export function applyFlaskReservoirsToBag(
  bag: BagItem[],
  reservoirs: Map<number, number>,
): BagItem[] {
  if (!reservoirs.size) return bag;
  return bag.map((it) => {
    if (!isReservoirFlaskItemId(it.itemId)) return it;
    const v = reservoirs.get(it.seq);
    if (v == null) return it;
    return { ...it, count: v };
  });
}

/**
 * 30/21 / 17/1 壶行 count 常为可见堆叠 1；若本包无 7/11，保留先前储量。
 */
export function preserveFlaskReservoirAcrossBagRefresh(
  next: BagItem,
  prev: BagItem | undefined,
): BagItem {
  if (!isReservoirFlaskItemId(next.itemId)) return next;
  if (next.count > 1) return next;
  if (
    prev &&
    isReservoirFlaskItemId(prev.itemId) &&
    prev.seq === next.seq &&
    prev.count > 1
  ) {
    return { ...next, count: prev.count };
  }
  return next;
}

/**
 * Overlay 30/21 seq/count onto 17/1 rows (same itemId order / multiset).
 * Stackable quantity: take max(17/1 stackHint, 30/21 u32). When 30/21 is
 * truncated/desynced (login overflow docs), a bogus grid count=1 used to
 * clobber a correct 17/1 stack of N — auction claim / chest openBag then
 * looked like「领了N个玄晶背包只有1」(same class as warehouse SET overwrite).
 * Equip / flask keep grid wire (instance=1 / visible stack=1).
 *
 * Also used when discard returns 17/1-only: pass prior bag as the "grid" so
 * duplicate itemIds keep distinct seqs (Map-by-itemId collapsed them).
 */
export function mergeBag17WithGridSeq(from17: BagItem[], fromGrid: BagItem[]): BagItem[] {
  const pools = new Map<number, BagItem[]>();
  for (const g of fromGrid) {
    const arr = pools.get(g.itemId) ?? [];
    arr.push(g);
    pools.set(g.itemId, arr);
  }
  return from17.map((it) => {
    const arr = pools.get(it.itemId);
    const hit = arr?.shift();
    if (!hit) return it;
    const attrs = preferEnhanceAttrs(it.enhanceAttrs, hit.enhanceAttrs);
    const stackable =
      it.kind !== "equip" &&
      hit.kind !== "equip" &&
      !isReservoirFlaskItemId(it.itemId) &&
      !isReservoirFlaskItemId(hit.itemId);
    const count = stackable
      ? Math.max(it.count >>> 0, hit.count >>> 0)
      : hit.count > 0
        ? hit.count
        : it.count;
    // 17/1 enhance is wire authority (0 after recast). Never `hit.enhance ||`
    // — prior-bag-as-grid would keep the pre-transfer level forever.
    const enhance =
      it.kind === "equip" || hit.kind === "equip"
        ? it.enhance >>> 0
        : 0;
    const keepAttrs =
      enhance > 0 ? attrs : it.enhanceAttrs;
    return {
      ...it,
      seq: hit.seq > 0 ? hit.seq : it.seq,
      count,
      enhance,
      ...(keepAttrs && keepAttrs.length ? { enhanceAttrs: keepAttrs } : {}),
    };
  });
}

/** Prefer the attr list that carries wash markers / more milestone rows. */
export function preferEnhanceAttrs(
  a?: BagEnhanceAttr[],
  b?: BagEnhanceAttr[],
): BagEnhanceAttr[] | undefined {
  const aWash = a?.some((x) => x.washed) ? 1 : 0;
  const bWash = b?.some((x) => x.washed) ? 1 : 0;
  if (aWash !== bWash) return aWash > bWash ? a : b;
  const aLen = a?.length ?? 0;
  const bLen = b?.length ?? 0;
  if (aLen !== bLen) return aLen > bLen ? a : b;
  // Equal length, neither washed: keep `a` (wire) when present. Call sites
  // pass pending/prev as `b` after unequip so washed-bit wins above; when the
  // server finally wires bit7, wire and pending agree.
  return a ?? b;
}

/** Incremental 7/7 type=1 updates — merge by seq into existing bag. */
export function parseBagItemUpdates(packet: WtPacket): BagItem[] {
  return parseItemManager77(packet).bag;
}

/**
 * Merge 7/7 type=1 rows into a bag snapshot.
 * CBE `TimerControl_ProcessItem` treats wire count as a **delta** (shop buy /
 * warehouse retrieve / battle drop). Replacing the row with the wire absolute
 * made a second stackable grant look like count=1 (tip said +1 twice, bag 1).
 * Equipment / flask visible-stack rows still replace (instance or 7/11 path).
 */
export function mergeBagItemUpdateDeltas(
  bag: BagItem[],
  updates: BagItem[],
): BagItem[] {
  const map = new Map(bag.map((i) => [i.seq, i]));
  for (const u of updates) {
    if (!(u.seq > 0)) continue;
    if (u.count <= 0) {
      map.delete(u.seq);
      continue;
    }
    const prev = map.get(u.seq);
    const stackable =
      !!prev &&
      prev.kind !== "equip" &&
      u.kind !== "equip" &&
      !isReservoirFlaskItemId(prev.itemId) &&
      !isReservoirFlaskItemId(u.itemId) &&
      prev.itemId === u.itemId;
    if (stackable) {
      map.set(u.seq, {
        ...prev,
        ...u,
        count: (prev.count >>> 0) + (u.count >>> 0),
      });
    } else {
      // Wire enhance is authoritative — including 0 after 29/6 recast.
      // `u.enhance || prev.enhance` treated 0 as missing and kept the old (+L).
      const enhance = u.enhance >>> 0;
      const attrs =
        enhance > 0
          ? preferEnhanceAttrs(u.enhanceAttrs, prev?.enhanceAttrs)
          : u.enhanceAttrs;
      const row: BagItem = {
        ...prev,
        ...u,
        enhance,
      };
      if (attrs && attrs.length) row.enhanceAttrs = attrs;
      else delete row.enhanceAttrs;
      map.set(u.seq, row);
    }
  }
  return [...map.values()];
}

/**
 * CBE 7/15 result=1: client self-deducts box/key (0x01023850) and ADDs each
 * iteminfo row count into the bag (0x01019228). Wire count is grant delta.
 */
export function applyChestOpenToBag(
  bag: BagItem[],
  boxSeq: number,
  keySeq: number,
  rewards: { itemId: number; seq: number; count: number }[],
): BagItem[] {
  let next = bag.slice();
  const deductSeq = (seq: number) => {
    if (!(seq > 0)) return;
    const idx = next.findIndex((it) => it.seq === seq);
    if (idx < 0) return;
    const cur = next[idx]!;
    if (cur.count > 1) {
      next[idx] = { ...cur, count: cur.count - 1 };
    } else {
      next = next.filter((_, i) => i !== idx);
    }
  };
  deductSeq(boxSeq);
  deductSeq(keySeq);
  for (const rw of rewards) {
    if (!(rw.itemId > 0) || !(rw.seq > 0)) continue;
    const delta = Math.max(1, rw.count | 0);
    const bySeq = next.findIndex((it) => it.seq === rw.seq);
    if (bySeq >= 0) {
      const cur = next[bySeq]!;
      if (cur.kind === "equip" || isReservoirFlaskItemId(cur.itemId)) {
        next[bySeq] = enrichBagRow(rw.itemId, rw.seq, delta, cur.enhance, cur.enhanceAttrs);
      } else {
        next[bySeq] = { ...cur, count: (cur.count >>> 0) + delta };
      }
      continue;
    }
    const byId = next.findIndex(
      (it) =>
        it.itemId === rw.itemId &&
        it.kind !== "equip" &&
        !isReservoirFlaskItemId(it.itemId),
    );
    if (byId >= 0) {
      const cur = next[byId]!;
      next[byId] = {
        ...cur,
        seq: rw.seq > 0 ? rw.seq : cur.seq,
        count: (cur.count >>> 0) + delta,
      };
      continue;
    }
    next.push(enrichBagRow(rw.itemId, rw.seq, delta, 0));
  }
  return next;
}

export function parseWornEquipment(packet: WtPacket): BagItem[] {
  return parseItemManager77(packet).worn;
}

/**
 * Fresh worn snapshot from a packet that contains 7/7 type=2.
 * Returns null when no type=2 object (do not clear cache).
 * Returns [] when type=2 present but empty (clear all slots).
 */
export function parseWornEquipmentSnapshot(packet: WtPacket): BagItem[] | null {
  let sawType2 = false;
  const worn: BagItem[] = [];
  for (const obj of packet.objects) {
    if (obj.kind !== 7 || obj.subtype !== 7) continue;
    const typeRaw = findField(obj, "type");
    const type = typeRaw ? decodeTaggedNumber(typeRaw) ?? typeRaw[2] ?? 0 : 0;
    if (type !== 2) continue;
    sawType2 = true;
    const raw = findField(obj, "iteminfo");
    if (!raw) continue;
    const r = new SeqReader(raw);
    try {
      const n = r.u8();
      for (let i = 0; i < n && r.remaining() > 4; i++) {
        const seq = r.i16();
        const itemId = r.u32();
        const count = r.u32();
        const extra = readItemCommonExtra(r);
        // type=2 wire count = current durability (mmGame:sub_D04 / item+0x110).
        worn.push({
          ...enrichBagRow(itemId, seq, count, extra.enhance, extra.attrs),
          durability: count >>> 0,
        });
      }
    } catch {
      /* truncated */
    }
  }
  return sawType2 ? worn : null;
}

function parseBagItems17(packet: WtPacket): BagItem[] {
  const obj = packet.objects.find((o) => o.kind === 17 && o.subtype === 1);
  if (!obj) return [];
  const raw = findField(obj, "iteminfo") || findField(obj, "baginfo");
  if (!raw) return [];
  const items: BagItem[] = [];
  const r = new SeqReader(raw);
  let count = 0;
  try {
    count = r.i16();
  } catch {
    return [];
  }
  for (let i = 0; i < count && r.remaining() > 4; i++) {
    const mark = r.pos;
    try {
      const itemId = r.u32();
      const extra = readItemCommonExtra(r);
      const cat = lookupItemSync(itemId);
      const isEquip = cat?.kind === "equip" || itemId >= 1000;
      const qty = isEquip
        ? 1
        : Math.max(1, extra.stackHint || 1);
      // 17/1 has no wire seq — leave 0 until 30/21 overlay (do not invent i+1).
      items.push(
        enrichBagRow(itemId, 0, qty, isEquip ? extra.enhance : 0, isEquip ? extra.attrs : undefined),
      );
    } catch {
      // Resync attempt: skip one tagged field and continue next row.
      r.pos = mark;
      try {
        r.u32();
        readItemCommonExtra(r);
      } catch {
        break;
      }
    }
  }
  return items;
}

function parseBagGrid30(packet: WtPacket): BagItem[] {
  const obj = packet.objects.find((o) => o.kind === 30 && o.subtype === 21);
  if (!obj) return [];
  const raw = findField(obj, "iteminfo") || findField(obj, "info");
  if (!raw || raw.length < 8) return [];
  const items: BagItem[] = [];
  const r = new SeqReader(raw);
  while (r.remaining() >= 10) {
    const mark = r.pos;
    try {
      const itemId = r.u32();
      const seq = r.i16();
      const count = r.u32();
      const extra = readItemCommonExtra(r);
      items.push(enrichBagRow(itemId, seq, count, extra.enhance, extra.attrs));
    } catch {
      r.pos = mark + 1;
      if (r.pos >= raw.length) break;
    }
  }
  return items;
}

function parseItemManager77(packet: WtPacket): { bag: BagItem[]; worn: BagItem[] } {
  const bag: BagItem[] = [];
  const worn: BagItem[] = [];
  for (const obj of packet.objects) {
    if (obj.kind !== 7 || obj.subtype !== 7) continue;
    const typeRaw = findField(obj, "type");
    const type = typeRaw ? decodeTaggedNumber(typeRaw) ?? typeRaw[2] ?? 0 : 0;
    const raw = findField(obj, "iteminfo");
    if (!raw) continue;
    const r = new SeqReader(raw);
    try {
      const n = r.u8();
      for (let i = 0; i < n && r.remaining() > 4; i++) {
        const seq = r.i16();
        const itemId = r.u32();
        const count = r.u32();
        const extra = readItemCommonExtra(r);
        const row = enrichBagRow(itemId, seq, count, extra.enhance, extra.attrs);
        if (type === 2) {
          worn.push({ ...row, durability: count >>> 0 });
        } else {
          bag.push(row);
        }
      }
    } catch {
      /* truncated */
    }
  }
  return { bag, worn };
}

export function parseMallState(packet: WtPacket): MallState | null {
  let coolmoney = 0;
  let ticket = 0;
  let foundBalance = false;
  const bal = packet.objects.find((o) => o.kind === 14 && o.subtype === 4);
  if (bal) {
    foundBalance = true;
    const c = findField(bal, "coolmoney");
    const t = findField(bal, "ticket");
    if (c) coolmoney = decodeTaggedNumber(c) ?? 0;
    if (t) ticket = decodeTaggedNumber(t) ?? 0;
  }
  const goods: ShopGoods[] = [];
  for (const obj of packet.objects) {
    if (obj.kind !== 14 || obj.subtype < 5 || obj.subtype > 13) continue;
    const raw = findField(obj, "iteminfo");
    if (!raw) continue;
    const r = new SeqReader(raw);
    try {
      const n = r.u8();
      for (let i = 0; i < n && r.remaining() > 4; i++) {
        const itemId = r.u32();
        const name = r.string().replace(/\0+$/g, "") || `#${itemId}`;
        const visual = r.u8();
        r.u8(); // stack
        const price = r.u32();
        const stock = r.u32();
        r.u8(); // buy step
        try {
          readItemCommonExtra(r);
        } catch {
          /* page extra may be short */
        }
        const cat = lookupItemSync(itemId);
        goods.push({
          itemId,
          name: (cat?.name || name).replace(/\0+$/g, "") || `#${itemId}`,
          price,
          stock,
          visual,
          quality: cat?.quality ?? (visual <= 4 ? visual : 0),
          kind: cat?.kind ?? (itemId >= 1000 ? "equip" : "item"),
          category: cat?.category ?? 0,
          desc: cat?.desc ?? "",
        });
      }
    } catch {
      /* truncated page */
    }
  }
  if (!foundBalance && !goods.length) return null;
  return { coolmoney, ticket, goods, hasBalance: foundBalance };
}

/** Pixi-only panel summary: 1/1/21 → build_player_stats (base+equip). */
export function buildPanelStatsRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 1, 21);
  return b.finish();
}

function fieldStringNamed(
  obj: { fields: { name: string; raw: Uint8Array }[] },
  name: string,
): string {
  const raw = findField(obj as never, name);
  if (!raw) return "";
  return decodeFieldString(raw).replace(/\0+$/g, "");
}

/**
 * Pixi 人物信息权威源：`1/1/21` 全额汇总（基础+装备），非 CBE actorinfo 裸半字。
 */
export function parsePanelStats(packet: WtPacket): CharacterStats | null {
  const obj = packet.objects.find((o) => o.kind === 1 && o.subtype === 21);
  if (!obj) return null;
  const name = fieldStringNamed(obj, "name");
  const level = fieldU32(obj, "level");
  if (!name && !level) return null;
  return {
    name: name || "?",
    level,
    title: fieldStringNamed(obj, "title"),
    job: fieldU32(obj, "job"),
    spouse: fieldStringNamed(obj, "spouse") || "无",
    spouseRoleId: fieldU32(obj, "spouseRoleId"),
    guild: fieldStringNamed(obj, "guild") || "无帮派",
    hp: fieldU32(obj, "hp"),
    hpMax: fieldU32(obj, "hpMax"),
    mp: fieldU32(obj, "mp"),
    mpMax: fieldU32(obj, "mpMax"),
    charm: fieldU32(obj, "charm"),
    strength: fieldU32(obj, "strength"),
    agility: fieldU32(obj, "agility"),
    wisdom: fieldU32(obj, "wisdom"),
    endurance: fieldU32(obj, "endurance"),
    vitality: fieldU32(obj, "vitality"),
    vitalityMax: fieldU32(obj, "vitalityMax"),
    attack: fieldU32(obj, "attack"),
    armor: fieldU32(obj, "defense"),
    hit: fieldU32(obj, "hit"),
    dodge: fieldU32(obj, "dodge"),
    crit: fieldU32(obj, "crit"),
    resist: fieldU32(obj, "resist"),
    baseStrength: fieldU32(obj, "baseStrength"),
    baseAgility: fieldU32(obj, "baseAgility"),
    baseWisdom: fieldU32(obj, "baseWisdom"),
    eqStrength: fieldU32(obj, "eqStrength"),
    eqAgility: fieldU32(obj, "eqAgility"),
    eqWisdom: fieldU32(obj, "eqWisdom"),
    eqAttack: fieldU32(obj, "eqAttack"),
    eqArmor: fieldU32(obj, "eqArmor"),
    exp: fieldU32(obj, "exp"),
    expNext: fieldU32(obj, "expNext"),
    money: fieldU32(obj, "money"),
    coolmoney: fieldU32(obj, "coolmoney"),
    pkPoints: fieldU32(obj, "pkpoints"),
    backpackCapacity: fieldU32(obj, "backpack"),
  };
}

/**
 * 1/10/26 money refresh (enhance / transfer / discard / shop).
 * Authoritative copper balance for the bag 金/银/铜 label.
 */
export function parseMoneyBalanceSync(packet: WtPacket): number | null {
  const obj = packet.objects.find((o) => o.kind === 10 && o.subtype === 26);
  if (!obj) return null;
  const moneyRaw = findField(obj, "money");
  if (!moneyRaw) return null;
  return decodeTaggedNumber(moneyRaw) ?? fieldU32(obj, "money");
}

/** @deprecated use parsePanelStats — kept as alias. */
export function parseCharacterStats(packet: WtPacket): CharacterStats | null {
  return parsePanelStats(packet);
}

export function parseHangupNotice(packet: WtPacket): string | null {
  const o11 = packet.objects.find((o) => o.kind === 25 && o.subtype === 11);
  if (o11) {
    const info = findField(o11, "info");
    if (info) {
      const text = decodeFieldString(info).replace(/\0+$/g, "").trim();
      if (text) return text;
    }
  }
  // 25/12 is info-banner clear ACK (after 25/11 wait / map reject), not hangup
  // start. Mapping it to 「挂机已启动」 made stop/start both show the same tip.
  return null;
}

/** Sync local hangup flag from 1/3/3 system lines (server authority). */
export function hangupActiveFromChatLine(text: string): boolean | null {
  if (text.includes("已停止挂机")) return false;
  if (text.includes("不能挂机")) return false;
  if (text.includes("挂机失败")) return false;
  // Stop-after keeps server auto for this fight; hangupActive stays true.
  // Callers still clear hangupDesired so keepalive will not re-arm.
  if (text.includes("下一场完成后挂机停止")) return null;
  if (text.includes("已开始挂机")) return true;
  return null;
}

/* ---------- designation / ranking / quick-repair ---------- */

/** Open title page: 1/23/1. */
export function buildDesignationListRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 23, 1);
  return b.finish();
}

/** Equip title: 1/23/3 { type=designationId }. */
export function buildDesignationSelectRequest(designationId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 23, 3).u8("type", designationId & 0xff);
  return b.finish();
}

export type DesignationRow = {
  id: number;
  name: string;
  desc: string;
  gif: string;
};

export type DesignationPage = {
  equippedId: number;
  rows: DesignationRow[];
};

export function parseDesignationPage(packet: WtPacket): DesignationPage | null {
  const obj = packet.objects.find((o) => o.kind === 23 && o.subtype === 1);
  if (!obj) return null;
  const result = fieldU32(obj, "result");
  if (result !== 1 && result !== 0) {
    /* still try parse if equiptype present */
  }
  const equippedId = fieldU32(obj, "equiptype");
  const count = fieldU32(obj, "count");
  const raw = findField(obj, "designationinfo");
  if (!raw) return { equippedId, rows: [] };
  /* put_object_entry: no outer BE16 */
  const r = new SeqReader(raw);
  const rows: DesignationRow[] = [];
  for (let i = 0; i < count && r.remaining() > 2; i++) {
    try {
      const id = r.u8();
      r.u8(); /* fieldB */
      const name = r.string().replace(/\0+$/g, "");
      const desc = r.string().replace(/\0+$/g, "");
      const gif = r.string().replace(/\0+$/g, "");
      rows.push({ id, name, desc, gif });
    } catch {
      break;
    }
  }
  return { equippedId, rows };
}

export function parseDesignationSelectResult(
  packet: WtPacket,
): { ok: boolean; gif?: string; name?: string } | null {
  const ack = packet.objects.find((o) => o.kind === 23 && o.subtype === 3);
  if (!ack) return null;
  const result = fieldU32(ack, "result");
  const upd = parseDesignationSceneUpdate(packet);
  return {
    ok: result === 1,
    gif: upd?.gif,
    name: upd?.name,
  };
}

/**
 * Scene/self overhead title update: 23/2 designationinfo
 * tagged u32 actorId, i8 id, i8 fieldB, shortTitle, overheadResource.gif
 */
export function parseDesignationSceneUpdate(packet: WtPacket): {
  actorId: number;
  designationId: number;
  name: string;
  gif: string;
} | null {
  const upd = packet.objects.find((o) => o.kind === 23 && o.subtype === 2);
  if (!upd) return null;
  const raw = findField(upd, "designationinfo");
  if (!raw) return null;
  const r = new SeqReader(raw);
  try {
    const actorId = r.u32();
    const designationId = r.u8();
    r.u8(); /* fieldB */
    const name = r.string().replace(/\0+$/g, "");
    const gif = r.string().replace(/\0+$/g, "");
    return { actorId, designationId, name, gif };
  } catch {
    return null;
  }
}

/** Ranking board: 1/23/7 { type, pageIndex }. */
export function buildRankingBoardRequest(type = 0, pageIndex = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 23, 7)
    .u8("type", type & 0xff)
    .u8("pageIndex", pageIndex & 0xff);
  return b.finish();
}

export type RankingRow = {
  roleId: number;
  name: string;
  score: string;
};

export type RankingPage = {
  tabs: string[];
  type: number;
  myorder: number;
  colnames: string[];
  pagemax: number;
  rows: RankingRow[];
};

export function parseRankingBoard(
  packet: WtPacket,
  requestType?: number,
): RankingPage | null {
  const obj = packet.objects.find((o) => o.kind === 23 && o.subtype === 7);
  if (!obj) return null;
  /* Server echoes request type (u8). Prefer it; when caller passes the
   * openRanking() type, reject stale bodies (wealth under PK label). */
  const echoedType = fieldU32(obj, "type");
  const hasEcho = !!findField(obj, "type");
  if (
    hasEcho &&
    requestType != null &&
    echoedType !== (requestType & 0xff)
  ) {
    console.warn(
      `[rank] drop 23/7 type mismatch req=${requestType} echo=${echoedType}`,
    );
    return null;
  }
  const boardType =
    hasEcho ? echoedType : requestType != null ? requestType : 0;
  const ordernum = fieldU32(obj, "ordernum");
  const orderlist = findField(obj, "orderlist");
  const tabs: string[] = [];
  if (orderlist) {
    const r = new SeqReader(orderlist);
    for (let i = 0; i < ordernum && r.remaining() > 0; i++) {
      try {
        r.u8();
        tabs.push(r.string().replace(/\0+$/g, ""));
      } catch {
        break;
      }
    }
  }
  const colnum = fieldU32(obj, "colnum");
  const colnamesRaw = findField(obj, "colnames");
  const colnames: string[] = [];
  if (colnamesRaw) {
    const r = new SeqReader(colnamesRaw);
    for (let i = 0; i < colnum && r.remaining() > 0; i++) {
      try {
        colnames.push(r.string().replace(/\0+$/g, ""));
      } catch {
        break;
      }
    }
  }
  const count = fieldU32(obj, "count");
  const top = findField(obj, "topplayerinfo");
  const rows: RankingRow[] = [];
  if (top) {
    const r = new SeqReader(top);
    for (let i = 0; i < count && r.remaining() > 0; i++) {
      try {
        const roleId = r.u32();
        const name = r.string().replace(/\0+$/g, "");
        const score = r.string().replace(/\0+$/g, "");
        rows.push({ roleId, name, score });
      } catch {
        break;
      }
    }
  }
  if (boardType >= 2) {
    const odd = rows.filter((r) => r.score !== "0").slice(0, 5);
    if (odd.length) {
      console.warn(
        "[rank] PK board non-zero scores (expected pk_points)",
        odd.map((r) => ({ id: r.roleId, score: r.score })),
      );
    }
  }
  return {
    tabs: tabs.length ? tabs : ["等级榜", "财富榜", "PK榜"],
    type: boardType,
    myorder: fieldU32(obj, "myorder"),
    colnames,
    pagemax: fieldU32(obj, "pagemax"),
    rows,
  };
}

/** Quick repair quote: 1/7/29 { type=1 }. */
export function buildQuickRepairQuoteRequest(type = 1): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 29).u8("type", type & 0xff);
  return b.finish();
}

/** Confirm quick repair: 1/7/13. */
export function buildQuickRepairConfirmRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 7, 13).u8("type", 2);
  return b.finish();
}

export type QuickRepairQuote = {
  type: number;
  repairnum: number;
  coolmoney: number;
};

export function parseQuickRepairQuote(packet: WtPacket): QuickRepairQuote | null {
  const obj = packet.objects.find((o) => o.kind === 7 && o.subtype === 29);
  if (!obj) return null;
  return {
    type: fieldU32(obj, "type"),
    repairnum: fieldU32(obj, "repairnum"),
    coolmoney: fieldU32(obj, "coolmoney"),
  };
}

export function parseQuickRepairConfirm(
  packet: WtPacket,
): { result: number } | null {
  const obj = packet.objects.find((o) => o.kind === 7 && o.subtype === 13);
  if (!obj) return null;
  return { result: fieldU32(obj, "result") };
}

/* ---------- Pixi cosmetics 1/31 (chenghao / wing / mount) ---------- */

export const COSMETIC_KIND_CHENGHAO = 1;
export const COSMETIC_KIND_WING = 2;
export const COSMETIC_KIND_MOUNT = 3;

export type CosmeticsState = {
  unlockedChenghao: number[];
  unlockedWing: number[];
  unlockedMount: number[];
  eqChenghao: number;
  eqWing: number;
  eqMount: number;
};

export type CosmeticsPeer = {
  actorId: number;
  eqChenghao: number;
  eqWing: number;
  eqMount: number;
  /** Peer guild name for nameplate「门派-xxx」; empty = none. */
  guild: string;
};

function parseCsvIds(raw: string): number[] {
  const out: number[] = [];
  const seen = new Set<number>();
  for (const part of String(raw || "").split(",")) {
    const t = part.trim();
    if (!t) continue;
    const n = Number(t);
    if (!Number.isFinite(n) || n <= 0) continue;
    const id = n | 0;
    if (seen.has(id)) continue;
    seen.add(id);
    out.push(id);
  }
  return out;
}

export function buildCosmeticsQueryRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 31, 1);
  return b.finish();
}

export function buildCosmeticsEquipRequest(kind: number, id: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 31, 3)
    .u8("kind", kind & 0xff)
    .u32("id", id >>> 0);
  return b.finish();
}

function parseCosmeticsStateObject(obj: {
  fields: { name: string; raw: Uint8Array }[];
}): CosmeticsState {
  return {
    unlockedChenghao: parseCsvIds(fieldStringNamed(obj, "unlockedChenghao")),
    unlockedWing: parseCsvIds(fieldStringNamed(obj, "unlockedWing")),
    unlockedMount: parseCsvIds(fieldStringNamed(obj, "unlockedMount")),
    eqChenghao: fieldU32(obj as never, "eqChenghao"),
    eqWing: fieldU32(obj as never, "eqWing"),
    eqMount: fieldU32(obj as never, "eqMount"),
  };
}

export function parseCosmeticsState(packet: WtPacket): CosmeticsState | null {
  const obj =
    packet.objects.find((o) => o.kind === 31 && o.subtype === 2) ||
    packet.objects.find((o) => o.kind === 31 && o.subtype === 4);
  if (!obj) return null;
  if (obj.subtype === 4 && fieldU32(obj, "result") !== 1) return null;
  return parseCosmeticsStateObject(obj);
}

export function parseCosmeticsEquipResult(
  packet: WtPacket,
): { ok: boolean; state: CosmeticsState | null } | null {
  const ack = packet.objects.find((o) => o.kind === 31 && o.subtype === 4);
  if (!ack) return null;
  const ok = fieldU32(ack, "result") === 1;
  return {
    ok,
    state: ok ? parseCosmeticsStateObject(ack) : null,
  };
}

export function parseCosmeticsPeerUpdates(packet: WtPacket): CosmeticsPeer[] {
  const out: CosmeticsPeer[] = [];
  for (const obj of packet.objects) {
    if (obj.kind !== 31 || obj.subtype !== 5) continue;
    const actorId = fieldU32(obj, "actorId");
    if (!actorId) continue;
    out.push({
      actorId,
      eqChenghao: fieldU32(obj, "eqChenghao"),
      eqWing: fieldU32(obj, "eqWing"),
      eqMount: fieldU32(obj, "eqMount"),
      guild: fieldStringNamed(obj, "guild"),
    });
  }
  return out;
}

/* ---------- Pixi marriage 1/32 (spouse teleport) ---------- */

export function buildSpouseTeleportRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 32, 1);
  return b.finish();
}

export function parseSpouseTeleportAck(
  packet: WtPacket,
): { result: number; tip: string } | null {
  const obj = packet.objects.find((o) => o.kind === 32 && o.subtype === 2);
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    tip: fieldStringNamed(obj, "tip"),
  };
}
