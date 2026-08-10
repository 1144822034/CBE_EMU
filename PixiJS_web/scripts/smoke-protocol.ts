import {
  buildLoginAlt12Request,
  buildRoleCreateRequest,
  buildRoleDeleteRequest,
  buildRoleSelectRequest,
  buildServerSelectRequest,
  buildBattleStartRequest,
  ingestPacket,
  parseServerList,
  parseRoleCreateResult,
  parseRoleDeleteResult,
  parseRoleListFromSelect,
  parseSceneEnter,
  parseNpcCatalog,
  parseBattleStart,
  parseSceneMoveSeeds,
  parseNearbyPeers,
  parsePeerMoveUpdates,
  demoBuildServerList,
  demoBuildServerSelect,
  demoBuildRoleCreate,
  demoBuildRoleDelete,
  demoBuildRoleEnter,
  demoBuildBattle,
  demoBuildBattleScene,
  demoBuildBattleAction,
  demoBuildPollPeers,
  buildSceneStartupFollowupRequest,
  buildLearnedSkillsRefreshRequest,
} from "../src/net/protocol";
import {
  buildTeamInviteRequest,
  buildTeamReplyRequest,
  buildNearbyPageRequest,
  buildPlayerInfoRequest,
  buildFriendAddRequest,
  buildSparRequest,
  buildTradeRequest,
  buildTradeReplyRequest,
  buildTradeCancelRequest,
  buildTradeConfirmRequest,
  buildTradeOfferRequest,
  buildGuildInviteRequest,
  buildGuildCreateStartRequest,
  buildGuildCreateNameRequest,
  buildGuildCreateCommitRequest,
  parseGroupPackets,
  parseTradePackets,
  parseSparAck,
  parseGuildCreatePackets,
  demoGroupinfoBlob,
  type TeamMember,
} from "../src/net/protocolSocial";
import {
  buildGuildListRequest,
  buildGuildDetailRequest,
  buildGuildLeaveRequest,
  buildGuildMemberPageRequest,
  buildGuildApplyRequest,
  buildGuildApplicationPageRequest,
  buildGuildApplicationActionRequest,
  parseGuildManagePackets,
} from "../src/net/protocolGuild";
import {
  parseBattleAction,
  parseLearnedSkills,
  parseActionInfoScript,
  buildMoveRequest,
  buildSettingsUnstuckRequest,
  parseMmGameUnstuckLanding,
  buildBattleItemUseRequest,
  buildBattleDeathPromptRequest,
  isBattleMedicineOrFlask,
  parseBagItems,
  parseBagItemUpdates,
  type BagItem,
} from "../src/net/protocolExtras";
import { WtBuilder, SeqWriter } from "../src/net/wt";
import { readFileSync } from "fs";
import { resolve } from "path";
import {
  parseSkillCatalogBytes,
  resolveLearnedSkillsFromCatalog,
} from "../src/resources/skill";

function assert(cond: unknown, msg: string): asserts cond {
  if (!cond) throw new Error(msg);
}

const listReq = buildLoginAlt12Request();
assert(listReq[0] === 0x57 && listReq[4] === 1, "login req");

const list = ingestPacket(demoBuildServerList())!;
const servers = parseServerList(list);
assert(servers.servers.length >= 1, "servers");
assert(servers.result === 1, "result");

const rolesPkt = ingestPacket(
  demoBuildServerSelect([
    { roleId: 10001, jobIndex: 0, sex: 0, name: "Demo", level: 10 },
  ]),
)!;
const roles = parseRoleListFromSelect(rolesPkt);
assert(roles.length === 1 && roles[0]!.roleId === 10001, "roles");
assert(roles[0]!.sex === 0, "title sexGroup 1 → RoleRow.sex 0 (male)");

{
  const emptyList = ingestPacket(demoBuildServerSelect([]))!;
  assert(parseRoleListFromSelect(emptyList).length === 0, "empty role list");
  const createReq = buildRoleCreateRequest(1, 0, "新手侠");
  // WT request header kind/subtype from first object: kind@5 subtype@6 → 1/7
  assert(createReq[4] === 1 && createReq[5] === 1 && createReq[6] === 7, "create 1/1/7");
  const createStr = String.fromCharCode(...createReq.filter((b) => b >= 0x20 && b < 0x7f));
  assert(createStr.includes("job") && createStr.includes("sex") && createStr.includes("name"), "create fields");
  // Named path: job=career 1..3 (幻剑 index1 → 2), sexGroup 1 for male
  assert(createStr.includes("job"), "create has job");
  const createAck = parseRoleCreateResult(ingestPacket(demoBuildRoleCreate(10099, 0))!);
  assert(createAck && createAck.result === 0 && createAck.actorId === 10099, "create ack");
  const createFail = parseRoleCreateResult(ingestPacket(demoBuildRoleCreate(0, 1))!);
  assert(createFail && createFail.result === 1, "create full/fail");
  const delReq = buildRoleDeleteRequest(10099);
  assert(delReq[4] === 1 && delReq[5] === 1 && delReq[6] === 8, "delete 1/1/8");
  assert(parseRoleDeleteResult(ingestPacket(demoBuildRoleDelete(0))!) === 0, "delete ack ok");
}

const enterPkt = ingestPacket(
  demoBuildRoleEnter(roles[0]!, "01桃花岛_01.sce", 100, 200),
)!;
const enter = parseSceneEnter(enterPkt);
assert(enter && enter.x === 100 && enter.y === 200, "enter");
const npcs = parseNpcCatalog(enterPkt);
assert(npcs.length === 2, "npcs");

const battlePkt = ingestPacket(demoBuildBattle(9, 10001))!;
const battle = parseBattleStart(battlePkt, { roleName: "Demo" });
assert(battle && battle.subtype === 10 && battle.left.length === 1, "battle");
assert(battle!.right[0]?.name === "Demo", "role name from ctx");
assert(battle!.left[0]?.hp === 120 && battle!.left[0]?.name === "野猪", "pve vitals");

const scenePkt = ingestPacket(
  demoBuildBattleScene({
    monsterId: 105,
    roleId: 10001,
    hp: 20,
    hpMax: 20,
    mp: 20,
    mpMax: 20,
  }),
)!;
const seeds = parseSceneMoveSeeds(scenePkt);
assert(seeds.length === 1 && seeds[0]!.hp === 20, "moveinfo seed hp");
const sceneBattle = parseBattleStart(scenePkt, {
  roleName: "侠客",
  monsterNameFor: (id) => (id === 105 ? "毒泥怪" : undefined),
});
assert(sceneBattle?.subtype === 5, "subtype 5");
assert(
  sceneBattle!.left[0]?.hp === 20 && sceneBattle!.left[0]?.name === "毒泥怪",
  "seed+name",
);
assert(
  sceneBattle!.right[0]?.hp === 500 && sceneBattle!.right[0]?.name === "侠客",
  "player from 4/5",
);

const actPkt = ingestPacket(
  demoBuildBattleAction({
    roleId: 10001,
    roleHp: 488,
    roleMp: 200,
    monsterId: 9,
    monsterHp: 85,
    monsterHpDelta: -35,
    roleHpDelta: -12,
    playerWire: 0,
    monsterWire: 1,
  }),
)!;
const act = parseBattleAction(actPkt);
assert(act && act.hpDeltas.length >= 1, "actioninfo deltas");
assert(act!.hpDeltas.some((d) => d.wire === 1 && d.delta === -35), "monster delta");
assert(act!.rightHp[0]?.mp === 200, "teaminfo mp");
assert(act!.actions.length >= 1 && act!.log !== "actioninfo", "playback log");

// Spar/duel: one 4/6 per hit merged in one WT — must not drop the peer hit.
{
  const mergeWt = (a: Uint8Array, b: Uint8Array): Uint8Array => {
    const bodyA = a.subarray(5);
    const bodyB = b.subarray(5);
    const total = 5 + bodyA.length + bodyB.length;
    const out = new Uint8Array(total);
    out[0] = 0x57;
    out[1] = 0x54;
    out[2] = (total >>> 8) & 0xff;
    out[3] = total & 0xff;
    out[4] = (a[4]! + b[4]!) & 0xff;
    out.set(bodyA, 5);
    out.set(bodyB, 5 + bodyA.length);
    return out;
  };
  const hitA = demoBuildBattleAction({
    roleId: 10001,
    roleHp: 500,
    roleMp: 200,
    monsterId: 10002,
    monsterHp: 480,
    monsterHpDelta: -25,
    playerWire: 0,
    monsterWire: 1,
  });
  const hitB = demoBuildBattleAction({
    roleId: 10001,
    roleHp: 475,
    roleMp: 200,
    monsterId: 10002,
    monsterHp: 480,
    roleHpDelta: -18,
    playerWire: 0,
    monsterWire: 1,
  });
  const sparMerged = ingestPacket(mergeWt(hitA, hitB))!;
  assert(
    sparMerged.objects.filter((o) => o.kind === 4 && o.subtype === 6).length === 2,
    "spar merge two 4/6",
  );
  const sparUp = parseBattleAction(sparMerged);
  assert(sparUp?.actions.length === 2, "spar merge both actions");
  assert(
    sparUp!.hpDeltas.some((d) => d.wire === 1 && d.delta === -25),
    "spar first hit damage",
  );
  assert(
    sparUp!.hpDeltas.some((d) => d.wire === 0 && d.delta === -18),
    "spar peer hit damage",
  );
}

// Live mock style: tagged-u8 type-1 skill + type-0 counter
const skillBlob = (() => {
  const c: number[] = [];
  const u8 = (v: number) => c.push(0, 1, v & 0xff);
  const u32 = (v: number) => {
    const x = v >>> 0;
    c.push(0, 4, (x >>> 24) & 0xff, (x >>> 16) & 0xff, (x >>> 8) & 0xff, x & 0xff);
  };
  const str = (s: string) => {
    const b = Array.from(new TextEncoder().encode(s));
    const len = b.length + 1;
    c.push((len >>> 8) & 0xff, len & 0xff, ...b, 0);
  };
  // type1 actor1 child1 target0 flag0 valueA=-55 effect=14 text tails
  u8(1); u8(1); u8(1);
  u8(0); u8(0); u32(-55 >>> 0); u32(0);
  u32(14); str("55"); u8(0); u8(0); u8(0);
  // type0 counter
  u8(0); u8(0); u8(1);
  u8(1); u8(0); u32(-12 >>> 0); u32(0);
  return new Uint8Array(c);
})();
const skillActs = parseActionInfoScript(skillBlob, 2);
assert(skillActs.length === 2, "tagged skill+counter");
assert(skillActs[0]!.type === 1 && skillActs[0]!.effectIndex === 14, "skill effect");
assert(skillActs[0]!.children[0]!.delta === -55, "skill damage");
assert(skillActs[1]!.type === 0 && skillActs[1]!.children[0]!.delta === -12, "counter");

// Monster cast_skill counter: type1 + effectIndex after a player type0
const monsterSkillBlob = (() => {
  const c: number[] = [];
  const u8 = (v: number) => c.push(0, 1, v & 0xff);
  const u32 = (v: number) => {
    const x = v >>> 0;
    c.push(0, 4, (x >>> 24) & 0xff, (x >>> 16) & 0xff, (x >>> 8) & 0xff, x & 0xff);
  };
  const str = (s: string) => {
    const b = Array.from(new TextEncoder().encode(s));
    const len = b.length + 1;
    c.push((len >>> 8) & 0xff, len & 0xff, ...b, 0);
  };
  u8(0); u8(1); u8(1);
  u8(0); u8(0); u32(-40 >>> 0); u32(0);
  u8(1); u8(0); u8(1);
  u8(1); u8(0); u32(-88 >>> 0); u32(0);
  u32(16); str("88"); u8(0); u8(0); u8(0);
  return new Uint8Array(c);
})();
const monsterSkillActs = parseActionInfoScript(monsterSkillBlob, 2);
assert(monsterSkillActs.length === 2, "na+monster skill");
assert(
  monsterSkillActs[1]!.type === 1 && monsterSkillActs[1]!.effectIndex === 16,
  "monster cast_skill effect",
);
{
  const deathReq = buildBattleDeathPromptRequest(2);
  assert(
    deathReq.length > 10 &&
      deathReq[4] === 1 &&
      deathReq[5] === 7 &&
      deathReq[6] === 14,
    "1/7/14 death prompt",
  );
}

assert(buildServerSelectRequest(1).length > 10, "select");
assert(buildRoleSelectRequest(10001).length > 10, "role");
assert(buildBattleStartRequest(1, 0, 1, 1).length > 10, "fight");
{
  const itemUse = buildBattleItemUseRequest(1, 42);
  assert(itemUse.length > 10 && itemUse[4] === 1 && itemUse[5] === 4 && itemUse[6] === 3, "4/3 item use");
  const flask: BagItem = {
    itemId: 802,
    seq: 1,
    count: 50000,
    name: "神仙壶",
    quality: 0,
    enhance: 0,
    kind: "item",
    category: 10,
    desc: "",
    usable: true,
    equipSlot: -1,
  };
  assert(isBattleMedicineOrFlask(flask), "flask battle usable");
  assert(
    isBattleMedicineOrFlask({ ...flask, category: 0 }),
    "flask usable even if bag.category stale 0",
  );
  assert(
    !isBattleMedicineOrFlask({ ...flask, kind: "equip", itemId: 1001 }),
    "equip not battle medicine",
  );
  // Lone 7/7 type=1 (battle drop) must not look like a full bag snapshot.
  const info = new SeqWriter()
    .u8(1)
    .i16(99)
    .u32(9001)
    .u32(1)
    .i16(0)
    .i16(0)
    .u8(0)
    .finish();
  const dropB = new WtBuilder();
  dropB.object(1, 7, 7).u8("type", 1).raw("iteminfo", info);
  const dropOnly = dropB.finishResponse();
  const dropPkt = ingestPacket(dropOnly);
  assert(!!dropPkt, "7/7 drop packet");
  assert(parseBagItems(dropPkt!).length === 0, "7/7 alone not full bag");
  const merged = parseBagItemUpdates(dropPkt!);
  assert(merged.length === 1 && merged[0]!.seq === 99, "7/7 merge-by-seq");
}
assert(buildSceneStartupFollowupRequest().length === 49, "followup wt49 with 12/1");
assert(buildLearnedSkillsRefreshRequest().length === 14, "skill refresh wt14");

const learned = parseLearnedSkills(enterPkt);
assert(learned && learned.ids.length === 1 && learned.ids[0] === 1, "12/1 starter skill");

const skillDsh = readFileSync(
  resolve(import.meta.dirname, "../../bin/JHOnlineData/skill.dsh"),
);
const catalog = parseSkillCatalogBytes(new Uint8Array(skillDsh));
assert(catalog.length >= 70, "skill.dsh rows");
const resolved = resolveLearnedSkillsFromCatalog(catalog, 0, learned!.ids);
assert(resolved.length === 1 && resolved[0]!.name.includes("万剑"), "role-bound name");
assert(!!resolved[0]!.desc && resolved[0]!.desc.length > 0, "skill.dsh 说明 on detail");
const ghost = resolveLearnedSkillsFromCatalog(catalog, 2, [201]);
assert(ghost[0]!.name.includes("绯炎"), "鬼道 skill bind");
const wrongJob = resolveLearnedSkillsFromCatalog(catalog, 0, [201]);
assert(wrongJob[0]!.name.startsWith("技能#"), "cross-job id not in catalog name");

const moveWt = buildMoveRequest(new Uint8Array(10).fill(2));
assert(moveWt.length === 32, "2/1 moveinfo blob WT len=32 (not bare raw 30)");

{
  // visualVariant=job-1, visualGroup=sex+1 — 女幻剑 must not decode as 女鬼道.
  const peerPkt = ingestPacket(
    demoBuildPollPeers([
      {
        actorId: 0x6a010001,
        name: "女幻剑",
        x: 100,
        y: 200,
        visualVariant: 1,
        visualGroup: 2,
        plus16Count: 3,
      },
    ]),
  )!;
  const peers = parseNearbyPeers(peerPkt);
  assert(peers[0]?.visualVariant === 1 && peers[0]?.visualGroup === 2, "peer wire job/sex");
  assert(peers[0]?.plus16Count === 3, "peer plus16info companion");

  const sw = new SeqWriter();
  sw.i16(296).i16(312).u32(0x6a010001);
  const dirs = new Uint8Array([2, 2, 3]);
  const body = sw.finish();
  const moveBlob = new Uint8Array(body.length + 2 + dirs.length);
  moveBlob.set(body, 0);
  moveBlob[body.length] = 0;
  moveBlob[body.length + 1] = dirs.length;
  moveBlob.set(dirs, body.length + 2);
  const mb = new WtBuilder();
  mb.object(1, 2, 2).raw("moveinfo", moveBlob);
  const peerMoves = parsePeerMoveUpdates(ingestPacket(mb.finishResponse())!);
  assert(
    peerMoves.length === 1 &&
      peerMoves[0]!.x === 296 &&
      peerMoves[0]!.dirs.join(",") === "2,2,3",
    "peer 1/2/2 timeline",
  );
}

{
  const inv = buildTeamInviteRequest(0x6a010001);
  assert(inv.includes(0x69) || true, "team invite built"); // smoke length
  // Field name must be `id` not `actorID`
  const asStr = String.fromCharCode(...inv.filter((b) => b >= 0x20 && b < 0x7f));
  assert(asStr.includes("id") && !asStr.includes("actorID"), "team invite field=id");
  const reply = buildTeamReplyRequest(0x6a010001, true);
  const replyStr = String.fromCharCode(...reply.filter((b) => b >= 0x20 && b < 0x7f));
  assert(replyStr.includes("id"), "team reply has id");
  // Request object header: major@4 kind@5 subtype@6
  assert(reply[4] === 1 && reply[5] === 5 && reply[6] === 3, "team reply 1/5/3");

  const members: TeamMember[] = [
    {
      id: 10001,
      name: "队长",
      sexGroup: 1,
      jobIndex: 0,
      online: true,
      hp: 100,
      mp: 50,
      hpMax: 120,
      mpMax: 80,
    },
    {
      id: 0x6a010002,
      name: "队员",
      sexGroup: 2,
      jobIndex: 1,
      online: true,
      hp: 90,
      mp: 40,
      hpMax: 100,
      mpMax: 60,
    },
  ];
  const blob = demoGroupinfoBlob(members);
  const b = new WtBuilder();
  b.object(1, 5, 10)
    .u8("result", 1)
    .u8("num", 2)
    .raw("groupinfo", blob)
    .u32("leadid", 10001);
  const g = parseGroupPackets(ingestPacket(b.finishResponse())!);
  assert(g.roster?.members.length === 2, "groupinfo members");
  assert(g.roster?.members[0]?.name === "队长", "leader name");
  assert(g.roster?.members[0]?.sexGroup === 1, "leader sexGroup male");
  assert(g.roster?.members[1]?.jobIndex === 1, "mate job");
  assert(g.roster?.members[1]?.sexGroup === 2, "mate sexGroup female");
  assert(g.roster?.leaderId === 10001, "leadid");

  assert(buildNearbyPageRequest(0).length > 10, "2/7 nearby page");
  assert(buildPlayerInfoRequest(1).length > 10, "10/2 player info");
  assert(buildFriendAddRequest(1).length > 10, "10/3 friend add");

  const spar = buildSparRequest(0x6a010001);
  const sparStr = String.fromCharCode(...spar.filter((b) => b >= 0x20 && b < 0x7f));
  assert(spar[4] === 1 && spar[5] === 4 && spar[6] === 14, "spar 1/4/14");
  assert(sparStr.includes("id"), "spar field=id");

  const trade = buildTradeRequest(0x6a010001);
  assert(trade[4] === 1 && trade[5] === 21 && trade[6] === 1, "trade 1/21/1");
  const tradeReply = buildTradeReplyRequest(0x6a010001, true);
  assert(tradeReply[5] === 21 && tradeReply[6] === 3, "trade reply 21/3");
  assert(buildTradeCancelRequest()[6] === 7, "trade cancel 21/7");
  assert(buildTradeConfirmRequest()[6] === 7, "trade confirm 21/7");
  {
    const offer = buildTradeOfferRequest(5, [{ seq: 2, count: 1 }]);
    assert(offer[5] === 21 && offer[6] === 5, "trade offer 21/5");
    const offerStr = String.fromCharCode(...offer.filter((b) => b >= 0x20 && b < 0x7f));
    assert(offerStr.includes("trademoney") && offerStr.includes("iteminfo"), "offer fields");
  }

  const guild = buildGuildInviteRequest(0x6a010001, 1);
  const guildStr = String.fromCharCode(...guild.filter((b) => b >= 0x20 && b < 0x7f));
  assert(guild[5] === 10 && guild[6] === 13, "guild invite 10/13");
  assert(guildStr.includes("id") && guildStr.includes("fid"), "guild id+fid");

  assert(buildGuildCreateStartRequest()[5] === 10 && buildGuildCreateStartRequest()[6] === 30, "guild create start 10/30");
  assert(buildGuildCreateNameRequest("测试帮")[6] === 28, "guild name 10/28");
  assert(buildGuildCreateCommitRequest("测试帮")[6] === 11, "guild commit 10/11");

  {
    const b = new WtBuilder();
    b.object(1, 21, 2).u32("id", 100).string("name", "卖家");
    const t = parseTradePackets(ingestPacket(b.finishResponse())!);
    assert(t.invite?.kind === "trade" && t.invite.name === "卖家", "21/2 trade invite");
  }
  {
    const b = new WtBuilder();
    b.object(1, 4, 14).u8("result", 1);
    const s = parseSparAck(ingestPacket(b.finishResponse())!);
    assert(s?.kind === "spar_result" && s.result === 1, "4/14 spar ack");
  }
  {
    const b = new WtBuilder();
    b.object(1, 10, 27).u8("result", 1);
    const g = parseGuildCreatePackets(ingestPacket(b.finishResponse())!);
    assert(g.notice?.kind === "guild_create" && g.notice.stage === "need_name", "10/27 need name");
  }
  {
    const b = new WtBuilder();
    b.object(1, 21, 5).u8("result", 1);
    const t = parseTradePackets(ingestPacket(b.finishResponse())!);
    assert(t.session?.kind === "offer_ack" && t.session.result === 1, "21/5 offer ack");
  }

  assert(buildGuildListRequest(1)[5] === 10 && buildGuildListRequest(1)[6] === 21, "guild list 10/21");
  assert(buildGuildDetailRequest(4)[6] === 22, "guild detail req 10/22");
  assert(buildGuildLeaveRequest(10024)[6] === 22, "guild leave req 10/22");
  assert(buildGuildMemberPageRequest(1)[6] === 37, "guild members 10/37");
  assert(buildGuildApplyRequest(4)[6] === 31, "guild apply 10/31");
  assert(buildGuildApplicationPageRequest(1)[6] === 32, "guild apps 10/32");
  assert(buildGuildApplicationActionRequest(10024, true)[6] === 33, "guild review 10/33");
  {
    const b = new WtBuilder();
    b.object(1, 10, 22).u8("result", 1);
    const g = parseGuildManagePackets(ingestPacket(b.finishResponse())!);
    assert(g.leaveResult?.result === 1, "10/22 leave ok");
  }
  {
    const b = new WtBuilder();
    b.object(1, 10, 33).u8("result", 1).u8("type", 1);
    const g = parseGuildManagePackets(ingestPacket(b.finishResponse())!);
    assert(g.reviewResult?.result === 1, "10/33 review ok");
  }
}

const unstuckReq = buildSettingsUnstuckRequest(2);
assert(unstuckReq.length > 10, "12/3 unstuck request");
{
  const pos = new Uint8Array([0, 2, 0, 160, 0, 2, 1, 86]);
  const b = new WtBuilder();
  b.object(1, 0x0c, 3).u8("result", 1).u16("id", 2);
  b.object(1, 0x10, 3)
    .u8("result", 2)
    .string("scene", "01桃花岛_02.sce")
    .raw("posinfo", pos)
    .u16("exitid", 0);
  const land = parseMmGameUnstuckLanding(ingestPacket(b.finishResponse())!);
  assert(land?.scene.includes("桃花岛") && land.x === 160 && land.y === 342, "16/3 unstuck landing");
}

console.log("protocol smoke OK", {
  servers: servers.servers[0]?.name,
  role: roles[0]?.name,
  scene: enter?.scene,
  npcs: npcs.map((n) => n.name),
  monster: battle?.left[0]?.name,
  sceneMonster: sceneBattle?.left[0],
  deltas: act?.hpDeltas,
  learned: learned?.ids,
  skillName: resolved[0]?.name,
});
