import {
  buildBattleStartRequest,
  buildGroupType1Request,
  buildLearnedSkillsRefreshRequest,
  buildLoginAlt12Request,
  loadClientCodeVersion,
  buildRoleCreateRequest,
  buildRoleDeleteRequest,
  buildRoleSelectRequest,
  buildSceneChangePostEnterFollowupRequest,
  buildSceneNpcCatalogFollowupRequest,
  buildSceneStartupFollowupRequest,
  buildServerSelectRequest,
  ingestPacket,
  parseBattleStart,
  parseNearbyPeers,
  parsePeerMoveUpdates,
  packetHasNpcCatalog,
  packetHasTitleRoleList,
  parseNpcCatalog,
  parseRemovedPeers,
  parseRoleCreateResult,
  parseRoleDeleteResult,
  parseRoleListFromSelect,
  parseRoleSelectEnter,
  parseMapActorVitals,
  parseSceneEnter,
  parseServerList,
  ensureSceneFile,
  sceneAssetCandidates,
  TITLE_ROLE_MAX,
  type BattleState,
  type NpcActor,
  type PeerActor,
  type PeerMoveUpdate,
  type RoleRow,
  type ServerRow,
} from "../net/protocol";
import { encodeWireString } from "../net/gbk";
import { BUNDLED_CLIENT_CODE_VERSION } from "../net/bundledCodeVersion";
import {
  buildBattleEscapeRequest,
  buildBattleItemUseRequest,
  buildBattleOperateRequest,
  buildBattleAutoFlagRequest,
  buildBagOpenRequest,
  buildChallengeConfirmRequest,
  buildChatRequest,
  buildPrivateChatRequest,
  buildWorldChatHornRequest,
  clampWorldChatText,
  SMALL_HORN_ITEM_ID,
  buildEnhanceRequest,
  buildEnhanceMaterialRequest,
  buildEnhanceTransferRequest,
  buildEnhanceTransferConfirmRequest,
  buildEnhanceWashOpenRequest,
  buildEnhanceWashMaterialRequest,
  buildEnhanceWashApplyRequest,
  buildCrystalCombineRequest,
  buildCrystalFragmentCombineRequest,
  buildDreamBadgeCombineRequest,
  buildDreamHudRemainQueryRequest,
  buildGemCombineRequest,
  buildGemEmbedRequest,
  buildGemFragmentCombineRequest,
  buildGemFromCrystal16Request,
  buildGemRemoveRequest,
  buildGemTypeExchangeRequest,
  buildStarSkyStoneWashResetRequest,
  buildRenameCardRequest,
  buildTianxiaSignatureRequest,
  buildCrystalExtractRequest,
  parseEnhanceOpenResult,
  parseEnhancePreviewResult,
  parseEnhanceConfirmResult,
  parseEnhanceTransferPreview,
  parseEnhanceTransferConfirm,
  parseEnhanceWashOpenResult,
  parseEnhanceWashPreviewResult,
  parseEnhanceWashConfirmResult,
  parseEnhanceWashApplyResult,
  parseCrystalCombineResult,
  parseCrystalFragmentCombineResult,
  parseDreamBadgeCombineResult,
  parseDreamHudRemainMs,
  parseDreamHudRemainMsFromBodies,
  parseGemOpResult,
  parseStarSkyStoneWashResetResult,
  parseRenameCardResult,
  parseTianxiaSignatureResult,
  parseCrystalExtractResult,
  buildSkillForgetRequest,
  parseSkillForgetResult,
  SKILL_FORGET_COST_COPPER,
  canCombineCrystalFragment,
  canCombineDreamBadge,
  canCombineEnhanceCrystal,
  canCombineGem,
  canCombineGemFragment,
  canExchangeGemType,
  isGemFragment,
  parseGemItem,
  CRYSTAL_FRAGMENT_COMBINE_COST,
  DREAM_BADGE_COMBINE_COST,
  ENHANCE_CRYSTAL_FIRST,
  isCrystalFragment,
  isDreamBadge,
  isDreamBadgeEquipId,
  STAR_SKY_STONE_ITEM_ID,
  RENAME_CARD_ITEM_ID,
  CRYSTAL_EXTRACT_STONE_ITEM_ID,
  isRenameCard,
  isEnhanceCrystal,
  canFitCrystalExtractInBag,
  estimateCrystalExtractBagSpace,
  type EnhanceOpenResult,
  type EnhancePreviewResult,
  type EnhanceConfirmResult,
  type EnhanceTransferPreviewResult,
  type EnhanceTransferConfirmResult,
  type EnhanceWashOpenResult,
  type EnhanceWashPreviewResult,
  type EnhanceWashConfirmResult,
  type EnhanceWashApplyResult,
  type CrystalCombineResult,
  type StarSkyStoneWashResetResult,
  type RenameCardResult,
  type TianxiaSignatureResult,
  type CrystalExtractResult,
  type SkillForgetResult,
  buildBattleInsightFollowupRequest,
  buildBattleInsightUseRequest,
  buildChestOpenRequest,
  buildChestOpenBatchRequest,
  parseChestOpenBatchResult,
  buildExpCardUseRequest,
  buildExpCardStatusRequest,
  buildCombatPillUseRequest,
  parseExpCardStatusInfo,
  parseExpCardStatusInfoFromPacket,
  parseBattleInsightRemainSecFromStatus,
  parseBattleInsightIteminfoFromPacket,
  isCombatPillItem,
  buildCultivationBookBindRequest,
  buildCultivationBookReadRequest,
  buildCultivationBookUseRequest,
  parseCultivationBookBindResult,
  parseCultivationBookReadResult,
  parseCultivationBookUseResult,
  parseCultivationBookMetaFromDes,
  formatCultivationBookName,
  isCultivationBookBlankItem,
  isCultivationBookItem,
  buildEquipSwapRequest,
  buildItemDiscardRequest,
  buildItemEquipRequest,
  buildItemUseRequest,
  buildMapHealSkillRequest,
  CHEST_KEY_BY_BOX,
  expCardSuccessMessage,
  goldScrollSuccessMessage,
  renameGoldScrollDisplayText,
  isBattleInsightItem,
  isChestBoxItem,
  isChestKeyItem,
  isExpCardItem,
  isDurableMallPassItemId,
  isGoldScrollItem,
  isOfficialExpCardItem,
  buildMallBalanceRequest,
  buildMallPageRequest,
  buildPanelStatsRequest,
  buildWornEquipRequest,
  buildShopBuy14Request,
  parseShopBuy14Response,
  shopBuy14ResultTip,
  buildMoveRequest,
  buildNpcDialogRequest,
  buildSettingsUnstuckRequest,
  buildTaskAcceptRequest,
  buildTaskAbandonRequest,
  buildTaskDetailRequest,
  buildTaskListQueryRequest,
  buildTaskSubmitRequest,
  buildTeamInviteRequest,
  buildTeamReplyRequest,
  buildTeamLeaveRequest,
  buildTeamRosterRequest,
  buildNearbyPageRequest,
  buildPlayerInfoRequest,
  buildOtherEquipRequest,
  buildFriendPageRequest,
  buildFriendAddRequest,
  buildFriendReplyRequest,
  buildFriendDeleteRequest,
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
  buildSceneChangePreludeRequest,
  buildSceneChangeRequest,
  buildTeleportStoneListRequest,
  buildMapTeleportPrepareRequest,
  buildMapTeleportConfirmedExitRequest,
  buildMapTeleportFreeExitRequest,
  TELEPORT_STONE_ITEM_ID,
  parseMapTeleportConfirm,
  npcServiceOpcode,
  EquipSellService,
  equipSellQualityLabel,
  matchEquipSellQuality,
  parseBagItems,
  parseBagItemUpdates,
  mergeBagItemUpdateDeltas,
  mergeBag17WithGridSeq,
  applyChestOpenToBag,
  parseFlaskReservoirUpdates,
  applyFlaskReservoirsToBag,
  preserveFlaskReservoirAcrossBagRefresh,
  preferEnhanceAttrs,
  bagVisibleStackCount,
  parseItemEquipResult,
  parseWornEquipmentSnapshot,
  parseMallState,
  parsePanelStats,
  parseBattleAction,
  buildBattleDeathPromptRequest,
  parseChallengePrompt,
  parseChatNotices,
  isAccountKickChatLine,
  parseHangupNotice,
  hangupActiveFromChatLine,
  parseItemUseFeedback,
  parseLearnedSkills,
  parseNpcDialog,
  parseSceneReload,
  parseServerSceneLanding,
  parseTaskAcceptResult,
  parseTaskAbandonResult,
  parseTaskDetail,
  parseTaskListQuery,
  parseTaskSubmitResult,
  computeNpcTaskPrompts,
  parseMoneyBalanceSync,
  buildDesignationListRequest,
  buildDesignationSelectRequest,
  parseDesignationPage,
  parseDesignationSelectResult,
  parseDesignationSceneUpdate,
  buildRankingBoardRequest,
  parseRankingBoard,
  buildQuickRepairQuoteRequest,
  buildQuickRepairConfirmRequest,
  parseQuickRepairQuote,
  parseQuickRepairConfirm,
  buildCosmeticsQueryRequest,
  buildCosmeticsEquipRequest,
  buildSpouseTeleportRequest,
  parseCosmeticsState,
  parseCosmeticsEquipResult,
  parseCosmeticsPeerUpdates,
  parseSpouseTeleportAck,
  COSMETIC_KIND_CHENGHAO,
  COSMETIC_KIND_WING,
  COSMETIC_KIND_MOUNT,
  type CosmeticsState,
  type CosmeticsPeer,
  NpcServiceOpcode,
} from "../net/protocolExtras";
import {
  CHENGHAO_SPECS,
  chenghaoLabel,
  chenghaoWireTag,
  isChenghaoId,
  parseChenghaoId,
  writeChenghaoPreference,
} from "../resources/chenghaoFx";
import { writeWingIdPreference } from "../resources/wingFx";
import { writeMountIdPreference } from "../resources/mountFx";
import {
  bracketForLevel,
  buildArenaClaimRequest,
  buildArenaLeaveRequest,
  buildArenaRankRequest,
  buildArenaStatusRequest,
  buildWagerCancelRequest,
  buildWagerChallengeRequest,
  buildWagerConfirmRequest,
  buildWagerCreateRequest,
  buildWagerListRequest,
  buildWagerMineRequest,
  parseArenaClaim,
  parseArenaFromBodies,
  parseArenaHud,
  parseArenaRank,
  parseArenaStatus,
  parseWagerEvent,
  parseWagerList,
  parseWagerMine,
  parseWagerOpResult,
  type ArenaHud,
  type ArenaRankPage,
  type ArenaStatus,
  type WagerEvent,
  type WagerListRow,
  type WagerMine,
  type WagerResult,
} from "../net/protocolArena";
import {
  buildAuctionBidRequest,
  buildAuctionBuyoutChallengeRequest,
  buildAuctionBuyoutRequest,
  buildAuctionCancelRequest,
  buildAuctionClaimRequest,
  buildAuctionClaimsListRequest,
  buildAuctionCreateRequest,
  buildAuctionListRequest,
  buildAuctionMineRequest,
  buildAuctionStatusRequest,
  parseAuctionBuyoutChallenge,
  parseAuctionClaim,
  parseAuctionClaimsList,
  parseAuctionEvent,
  parseAuctionList,
  parseAuctionOpResult,
  parseAuctionStatus,
  type AuctionBuyoutChallengeResult,
  type AuctionClaimResult,
  type AuctionClaimRow,
  type AuctionEvent,
  type AuctionListing,
  type AuctionOpResult,
  type AuctionStatus,
} from "../net/protocolAuction";
import {
  buildLadderStartRequest,
  buildLadderStatusRequest,
  parseLadderStartResult,
  parseLadderStatus,
  type LadderStatus,
} from "../net/protocolLadder";
import {
  buildGuessBetRequest,
  buildGuessClaimRequest,
  buildGuessHistoryRequest,
  buildGuessStatusRequest,
  parseGuessHistory,
  parseGuessOpResult,
  parseGuessStatus,
  type GuessHistoryRow,
  type GuessOpResult,
  type GuessStatus,
} from "../net/protocolGuess";
import {
  buildFruitCashOutRequest,
  buildFruitCollectRequest,
  buildFruitCompareRequest,
  buildFruitInsertRequest,
  buildFruitSpinRequest,
  buildFruitStatusRequest,
  parseFruitOpResult,
  parseFruitSpinResult,
  parseFruitStatus,
  type FruitOpResult,
  type FruitSpinResult,
  type FruitStatus,
} from "../net/protocolFruit";
import {
  activityMsgTip,
  buildActivitiesStatusRequest,
  buildFestivalExchangeRequest,
  buildRingAcceptRequest,
  buildRingClaimDailyRequest,
  buildRingRefreshRequest,
  buildRingStatusRequest,
  buildRingSubmitRequest,
  buildYanmenExchangeRequest,
  buildYanmenListRequest,
  parseActivitiesStatus,
  parseActivityOp,
  parseRingStatus,
  parseYanmenList,
  RING_REFRESH_WCOIN,
  type ActivitiesStatus,
  type ActivityOpResult,
  type RingStatus,
  type YanmenExchangeRow,
} from "../net/protocolActivities";
import { decodeWtPacket, type WtPacket } from "../net/wt";
import {
  type BagItem,
  type BagEnhanceAttr,
  type BattleActionUpdate,
  type CharacterStats,
  type DesignationPage,
  type ItemUseFeedback,
  type MallState,
  type NpcDialog,
  type RankingPage,
  type TaskCandidateRow,
  type TaskListRow,
  type NpcTaskPromptType,
  isReservoirFlaskItemId,
  overlayBagItemFromCatalog,
} from "../net/protocolExtras";
import {
  parseNearbyPage,
  parsePlayerInfo,
  parseOtherEquip,
  parseFriendPage,
  parseGroupPackets,
  parseFriendInvitePackets,
  parseGuildInvitePackets,
  parseTradePackets,
  parseSparPackets,
  parseGuildCreatePackets,
  FRIEND_PAGE_SIZE,
  type FriendRow,
  type OtherEquipView,
  type PlayerInfoView,
  type SocialInvite,
  type SocialNotice,
  type TeamMember,
  type TeamRoster,
  type TradeSessionUpdate,
} from "../net/protocolSocial";
import {
  buildEnmityPageRequest,
  buildHuntOrderUseRequest,
  parseEnmityPage,
  parseHuntOrderResult,
  HUNT_ORDER_ITEM_ID,
  ENMITY_PAGE_SIZE,
  type EnmityRow,
} from "../net/protocolEnmity";
import {
  parseGuildManagePackets,
  GUILD_PAGE_SIZE,
  guildPageRowIndex,
  type GuildManageUpdate,
} from "../net/protocolGuild";
import {
  CBMS_FLAG_SCENE_POLL,
  connectLiveTransport,
  LiveTransport,
  type GameTransport,
} from "../net/Transport";
import {
  clearHangupResumeStorage,
  syncNativeKeepAlive,
} from "../net/hangupResume";
import {
  resolveLearnedSkills,
  type BattleSkillRow,
} from "../resources/skill";
import { lookupItemSync, warmItemCatalogs, BACKPACK_EXPAND_ITEM_ID, BACKPACK_EXPAND_MAX_CAPACITY } from "../resources/itemCatalog";
import { monsterActorForEnemyId } from "../resources/monsterLevels";
import { setResourceExchange } from "../resources/loader";

export type SessionStatus = {
  mode: string;
  phase: string;
  detail: string;
};

/**
 * Town / city scene keys — mirrors `vm_net_mock_scene_is_city_hangup_forbidden`:
 * `c`+digit (e.g. c04临安府_*) or bare digit+GBK alias (e.g. 4临安…).
 * Cities have no SCE kind=3 combat spawns; do not reuse outdoor wild pools.
 */
export function isCitySceneKey(scene: string): boolean {
  if (!scene) return false;
  const leaf = scene.split(/[/\\]/).pop() || scene;
  const stem = leaf.replace(/\.sce$/i, "");
  if (/^c[0-9]/i.test(stem)) return true;
  if (/^[1-9]/.test(stem)) {
    const second = stem.charCodeAt(1);
    if (second >= 0x80) return true;
  }
  return false;
}

/** Match scene leaves including sMap `4临安…` ↔ disk `c04临安…` aliases. */
function sceneKeysMatch(a: string, b: string): boolean {
  if (!a || !b) return false;
  const norms = (s: string) =>
    sceneAssetCandidates(s).map((c) => c.replace(/\.sce$/i, "").toLowerCase());
  const A = norms(a);
  const B = norms(b);
  if (A.some((x) => B.includes(x))) return true;
  // Prefix+slot fingerprint: `07泰山_02` ↔ wire/decode drift in the Chinese
  // middle still shares `07` + `_02` (泰山-only false negatives).
  const finger = (s: string): string | null => {
    const stem = ensureSceneFile(s).replace(/\.sce$/i, "");
    const m = stem.match(/^c?(\d+)(.+)_(\d+)$/i);
    if (!m) return null;
    return `${m[1]!.replace(/^0+/, "") || "0"}_${m[3]}`;
  };
  const fa = finger(a);
  const fb = finger(b);
  return !!fa && !!fb && fa === fb;
}

export type SessionCallbacks = {
  onStatus: (s: SessionStatus) => void;
  onServers: (servers: ServerRow[]) => void;
  onRoles: (roles: RoleRow[]) => void;
  onEnterScene: (
    scene: string,
    x: number,
    y: number,
    opts?: { hasPos?: boolean },
  ) => void | Promise<void>;
  /**
   * Soft same-map shell ack (world-boss resync etc.): map already painted —
   * refresh overlays without remounting SCE / wiping hangup timers.
   */
  onSceneSoftResync?: (scene: string) => void | Promise<void>;
  /** Fired when switching roles — UI must drop previous map shell / party HUD. */
  onRoleSwitch?: () => void;
  /** Server landing after scene shell already shown (30/2+pos / 27/12). */
  onPlayerPos?: (x: number, y: number) => void;
  onNpcs: (npcs: NpcActor[]) => void;
  onPeers: (peers: PeerActor[], removed: number[]) => void;
  onPeerMoves?: (moves: PeerMoveUpdate[]) => void;
  onNearbyPage?: (page: { peers: PeerActor[]; allpgs: number }) => void;
  onTeamUpdate?: (roster: TeamRoster) => void;
  onFriends?: (friends: FriendRow[]) => void;
  onEnmity?: (page: { dir: number; rows: EnmityRow[] }) => void;
  onSocialInvite?: (invite: SocialInvite) => void;
  onSocialNotice?: (notice: SocialNotice) => void;
  onTradeSession?: (update: TradeSessionUpdate) => void;
  onGuildManage?: (update: GuildManageUpdate) => void;
  onPlayerInfo?: (info: PlayerInfoView) => void;
  onOtherEquip?: (equip: OtherEquipView) => void;
  /** Equipped designation changed (23/2) — refresh overhead badge. */
  onDesignationUpdate?: (info: { name: string; gif: string }) => void;
  /** Pixi cosmetics 1/31 state (unlock lists + equipped ids). */
  onCosmeticsUpdate?: (state: CosmeticsState) => void;
  /** Pixi 比武擂台 HUD push (41/5). */
  onArenaHud?: (hud: ArenaHud | null) => void;
  onWagerEvent?: (ev: WagerEvent) => void;
  onAuctionEvent?: (ev: AuctionEvent) => void;
  /** Peer designation update (23/2 for another actorId). */
  onPeerDesignation?: (info: {
    actorId: number;
    name: string;
    gif: string;
  }) => void;
  /** Peer cosmetics update (31/5). */
  onCosmeticsPeer?: (peer: CosmeticsPeer) => void;
  onBattle: (battle: BattleState) => void;
  /**
   * Hide map SCE spawn when 4/1 arms (hangup/challenge skip collide).
   * actorId may be 0 — then hide by home tile (x,y).
   */
  onEngageSceneMonster?: (actorId: number, x: number, y: number) => void;
  /** Undo engage when 4/5 never arrives (same id/pos as onEngageSceneMonster). */
  onRestoreSceneMonster?: (actorId: number, x: number, y: number) => void;
  onBattleUpdate?: (update: BattleActionUpdate) => void;
  onSkills?: (skills: BattleSkillRow[]) => void;
  onNpcDialog?: (dialog: NpcDialog) => void;
  /** Task detail after 6/10 — confirm accept/submit. */
  onTaskPrompt?: (prompt: {
    taskId: number;
    text: string;
    mode: "accept" | "submit";
  }) => void;
  /** NPC overhead quest marks from 6/1+6/14 (name → prompt type). */
  onNpcTaskPrompts?: (prompts: Map<string, NpcTaskPromptType>) => void;
  /** Instance challenge confirm after 26/0+30/9. */
  onChallengePrompt?: (text: string) => void;
  onChat?: (
    lines: {
      type: number;
      text: string;
      senderRoleId?: number;
      senderName?: string;
    }[],
  ) => void;
  /** Same-account login takeover — return to title. */
  onAccountKicked?: (reason: string) => void;
  /** Hangup soft/hard resume status tip for UI. */
  onHangupResumeStatus?: (message: string) => void;
  /** Item use / chest open success or failure (also pushed as chat). */
  onItemUseFeedback?: (fb: ItemUseFeedback) => void;
  onBag?: (items: BagItem[]) => void;
  onWorn?: (items: BagItem[]) => void;
  onMall?: (mall: MallState) => void;
  onCharacterStats?: (stats: CharacterStats) => void;
  /** Map top-left HP/MP bars (item use / map heal / 1/1/14). */
  onVitals?: (hp: number, hpMax: number, mp: number, mpMax: number) => void;
  onHangup?: (msg: string) => void;
};

/**
 * Pixi client hangup map-side gap after settle leave.
 * Align with reward gate (`CBE_BATTLE_REWARD_MIN_INTERVAL_MS` default 8000)
 * and hangup-loop-after-exit pacing so fights are evenly spaced — not paired.
 */
const HANGUP_ENCOUNTER_INTERVAL_MS = 8000;
/**
 * Soft-fail retry (no monster / tear-down-only). Keep short so a single miss
 * does not become interval+retry ≈ 13s (see 2026-09-12 paired-battles doc).
 */
const HANGUP_ENCOUNTER_RETRY_MS = 1500;

export class GameSession {
  transport: GameTransport | null = null;
  servers: ServerRow[] = [];
  roles: RoleRow[] = [];
  npcs: NpcActor[] = [];
  /** SCE kind=3 combat spawns for the current map (not in 27/11). */
  sceneMonsters: NpcActor[] = [];
  /**
   * Full SCE combat seeds for this map shell. Unlike `sceneMonsters` (live /
   * unsuppressed only), this stays populated while engaged spawns are hidden
   * for the post-battle respawn delay — hangup must still pick map species.
   */
  sceneMonsterCatalog: NpcActor[] = [];
  /** Last 26/1 talk target — challenge confirm binds guide NPC visuals. */
  lastNpcId = 0;
  /**
   * Guide NPC actorId from EC「挑战守关怪」(low 24 bits). Used only as a
   * last-resort outdoor e_* guide visual — never dream FB_* NPC portraits.
   */
  pendingChallengeGuideActorId = 0;
  /**
   * Catalog challenge_enemy_id from 30/9 `enemy`. Confirm assigns this into
   * hangup-style `startBattleNearPlayer` (4/1 preferNpcId).
   */
  pendingChallengeEnemyId = 0;
  /**
   * Spawn touched / nearest when issuing 4/1. Battle left seats must paint this
   * species (multi-clone fights share one template), not other map kinds.
   */
  pendingEncounterVisual: {
    battleActorId: number;
    x: number;
    y: number;
    actorResource: string;
    name?: string;
  } | null = null;
  /** Surrounding-player page cache (2/7). */
  nearbyList: PeerActor[] = [];
  /** Live team roster (5/3, 5/10, …). */
  teamMembers: TeamMember[] = [];
  teamLeaderId = 0;
  /** Friend list cache (10/1), one display page after online-first ordering. */
  friends: FriendRow[] = [];
  friendPage = 1;
  friendAllPages = 1;
  /** Ignore partial 10/1 pages while the full list is being assembled. */
  private suppressFriendPageApply = false;
  /** Enmity list cache (43/1). */
  enmityRows: EnmityRow[] = [];
  enmityDir = 0;
  enmityPage = 1;
  enmityAllPages = 1;
  /** Title page cache (23/1). */
  designationPage: DesignationPage | null = null;
  arenaHud: ArenaHud | null = null;
  arenaStatus: ArenaStatus | null = null;
  arenaRankPage: ArenaRankPage | null = null;
  /** Ranking board cache (23/7). */
  rankingPage: RankingPage | null = null;
  /** Cached accepted/candidate rows for NPC overhead marks. */
  acceptedTasks: TaskListRow[] = [];
  candidateTasks: TaskCandidateRow[] = [];
  /** NPC display-name → prompt type (1 submit / 2 accept / 3 incomplete). */
  npcTaskPrompts: Map<string, NpcTaskPromptType> = new Map();
  /**
   * Hangup-only operate: 0 = normal attack; skill = skillId+2.
   * Set only via 挂机战斗 skill dialog. Independent of [自动] LastOperate.
   */
  hangupSkillOperate = 0;
  /**
   * Pixi hangup armed: client loop simulates collide (`4/1` via
   * startBattleNearPlayer). Not driven by server hangup battle delivery.
   */
  hangupActive = false;
  /**
   * Item 828 wall-clock expiry (ms). Updated from 7/31 / 25/6 status copy;
   * same source as map-corner「修」buff list.
   */
  private battleInsightExpireAtMs = 0;
  /** True after at least one 7/31 / iteminfo parse this session. */
  private battleInsightStatusKnown = false;
  /**
   * Dream HUD remain (ms) from server 29/23 — same authority as
   * account_roles.dream_hud_remaining_ms after pause/resume.
   */
  dreamHudRemainMs: number | null = null;
  /** Fired when 29/23 remain arrives (enter query or push). */
  onDreamHudRemain: ((remainMs: number) => void) | null = null;
  /**
   * Player intent to keep hangup running across disconnect/relogin / between fights.
   */
  hangupDesired = false;
  /** Delay between client-driven hangup encounters (ms). */
  private hangupEncounterTimer: ReturnType<typeof setTimeout> | null = null;
  /**
   * Wall-clock (Date.now) when the next hangup 4/1 is due (0 = none).
   * Prefer Date over performance.now — WebView may freeze the latter while
   * FGS keepalive still advances wall time.
   */
  private hangupEncounterDueAt = 0;
  /** Earliest wall time for the next hangup fight (leave + interval). */
  private hangupEncounterEarliestAt = 0;
  /** Serialize hangup 4/1 so setTimeout + FGS keepalive cannot double-fire. */
  private hangupEncounterInFlight = false;
  /**
   * main.ts wires battle auto / settle confirm here — native FGS keepalive
   * must drive them while Chromium starves setTimeout (document.hidden).
   */
  onBackgroundGameplayTick: (() => void) | null = null;
  /** Live WS dropped — freeze dream HUD leftover (server also pauses). */
  onConnectionLost: (() => void) | null = null;
  /** Live WS re-opened — thaw dream HUD leftover. */
  onConnectionRestored: (() => void) | null = null;
  /**
   * In-battle [自动] switch. Gateway 4/11 prefer + poll synth (no client 4/2).
   * hangup=0 so it does not arm map HangupLoop / auto-encounter.
   */
  autoBattleActive = false;
  /**
   * Last manual / [自动] 4/2 operate. Used only by [自动] (server LastOperate).
   * Hangup never reads or writes this.
   */
  lastBattleOperate: number | null = null;
  /** Pending invite awaiting softkey confirm (5/2, 10/4, 21/2). */
  pendingSocialInvite: SocialInvite | null = null;
  /** True while 21/4 trade session is open (cancel via 21/7). */
  inTrade = false;
  tradePeerName = "";
  /** Name validated by 10/28 — must match 10/11 commit. */
  pendingGuildCreateName = "";
  /** Current guild id (0 = none). From list fid / detail / 10/36 / create. */
  guildId = 0;
  guildName = "";
  /** Own membership rank in current guild (1=帮主); 0 unknown. */
  guildSelfRank = 0;
  guildListPage = 1;
  guildMemberPage = 1;
  guildAppPage = 1;
  /** When false, 10/21 only updates guildId/fid (no list UI). */
  guildListUi = true;
  /** Which guild list is paginating: list | members | apps. */
  guildBrowseKind: "list" | "members" | "apps" = "list";
  selectedServerId = 1;
  selectedRoleId = 0;
  scene = "";
  x = 0;
  y = 0;
  /** True after wire posinfo / role-select enter; false on soft shell-only reload. */
  hasServerPos = false;
  /**
   * Last scene whose map shell finished loading (onEnterScene resolved).
   * Distinct from `scene` which may update on soft ack before showScene completes —
   * sameScene skip must use this or portal NPCs land on the previous map.
   */
  private loadedScene = "";
  /** While set, ignore poll/soft reloads for any other scene leaf. */
  private sceneTransferExpected: string | null = null;
  /** Guard nested poll while settling leader-follow catalog. */
  private teamFollowSettleInFlight = false;
  hp = 500;
  hpMax = 500;
  mp = 200;
  mpMax = 200;
  level = 1;
  exp = 0;
  gold = 0;
  roleTitle = "";
  /** Overhead GIF: riches_nameN.gif / level_nameN.gif / chenghao:ID */
  roleTitleGif = "";
  /** Equipped Pixi chenghao id from 1/31 (0 = none). */
  localChenghaoId = 0;
  /** Server unlock lists + equipped wing/mount/chenghao (1/31). */
  cosmetics: CosmeticsState = {
    unlockedChenghao: [],
    unlockedWing: [],
    unlockedMount: [],
    eqChenghao: 0,
    eqWing: 0,
    eqMount: 0,
  };
  /** Field PK score; red name when >= 60. */
  pkPoints = 0;
  roleName = "";
  /** actorinfo jobIndex (raw 0..2) for skill.dsh filter. */
  roleJobIndex = 0;
  /** Normalized 0=male 1=female (RoleRow / actorinfo). */
  roleSex = 0;
  /** Live 2/10 peers — battle right seats prefer these visuals to match the map. */
  private scenePeerById = new Map<number, PeerActor>();
  /**
   * Last 31/5 per map actorId. 2/10 may recreate markers without a fresh notice;
   * re-apply from this cache after every peer upsert.
   */
  private scenePeerCosmeticsById = new Map<number, CosmeticsPeer>();
  /** Learned skill IDs from WT 1/12/1. */
  learnedSkillIds: number[] = [];
  /** Resolved rows (id + skill.dsh name) for battle menu. */
  learnedSkills: BattleSkillRow[] = [];
  /** Last bag / worn / mall / actorinfo from server packets. */
  bagItems: BagItem[] = [];
  wornItems: BagItem[] = [];
  /** Bumps to cancel in-flight 7/38 list-name enrichment. */
  private cultivationBookEnrichGen = 0;
  /**
   * Unequip/swap: CBE moves the item object and keeps item+0x110. Pixi bag
   * open rebuilds from 30/21 (count=stack only), so stash current durability
   * by the destination bag seq until the next bag merge applies it.
   */
  private pendingBagEquipDurability = new Map<number, number>();
  /** Unequip/swap: preserve washed milestone attrs until bag refresh merges. */
  private pendingBagEquipEnhance = new Map<number, BagEnhanceAttr[]>();
  mallState: MallState | null = null;
  characterStats: CharacterStats | null = null;
  inBattle = false;
  private pollTimer: number | null = null;
  private phase = "idle";
  private moveBuf: number[] = [];
  private lastMoveFlush = 0;
  /** Guest credentials issued by result=3 on 1/12; reused on later list fetches. */
  private accountUser = "";
  private accountPass = "";
  /** Matches server server_code_version.txt via login WT codeVersion. */
  private codeVersion = BUNDLED_CLIENT_CODE_VERSION;
  private roleSelectInFlight = false;
  /**
   * While set (title role-select → first scene ready), only this scene leaf may
   * enter. Blocks deferred 30/1/30/2 from a prior role overwriting 1/6.actorinfo.
   */
  private roleEnterSceneLock: string | null = null;
  private pendingTaskId = 0;
  private pendingTaskMode: "accept" | "submit" = "accept";
  /** Last onEnterScene work (map decode); awaited before NPC catalog follow-up. */
  private enterScenePromise: Promise<void> | null = null;
  /** Bumped on each trackEnterScene so a superseded load cannot stamp loadedScene. */
  private enterSceneGen = 0;
  /** Portal/change: wait for wire posinfo (30/1, 30/2+pos, 27/12). */
  private landingWait: {
    scene: string;
    promise: Promise<void>;
    resolve: () => void;
    done: boolean;
  } | null = null;
  private titleKeepaliveTimer: number | null = null;
  private nativeKeepAliveInFlight = false;
  private pollFailStreak = 0;

  constructor(private readonly cb: SessionCallbacks) {
    // Legacy auto-resume snapshots must not re-arm hangup on launch.
    clearHangupResumeStorage();
  }

  setCredentials(user: string, password: string): void {
    this.accountUser = user;
    this.accountPass = password;
  }

  getCredentials(): { user: string; password: string } {
    return { user: this.accountUser, password: this.accountPass };
  }

  async start(
    credentials?: { user: string; password: string },
  ): Promise<void> {
    if (credentials) this.setCredentials(credentials.user, credentials.password);
    // Never resume a previous in-world scene across reconnect / re-login.
    this.clearWorldState();
    this.stopPoll();
    this.transport?.close();
    this.codeVersion = await loadClientCodeVersion();
    this.transport = await connectLiveTransport();
    this.bindTransportPush(this.transport);
    setResourceExchange((flags, wt) => this.transport!.exchange(flags, wt));
    this.setPhase("connected", `${this.transport.mode} · codeVersion=${this.codeVersion}`);
    await this.fetchServerList();
  }

  /** Gateway idle scene-poll pushes CBMR without a matching client request. */
  private bindTransportPush(transport: GameTransport): void {
    if (!(transport instanceof LiveTransport)) return;
    transport.onPushBodies = (bodies) => {
      try {
        this.handleBodies(bodies);
      } catch (e) {
        console.warn("[session] gateway push", e);
      }
    };
    transport.onConnectionLost = () => {
      try {
        this.onConnectionLost?.();
      } catch (e) {
        console.warn("[session] connection lost", e);
      }
    };
    transport.onConnectionRestored = () => {
      try {
        this.onConnectionRestored?.();
      } catch (e) {
        console.warn("[session] connection restored", e);
      }
    };
  }

  stop(): void {
    this.stopPoll();
    this.stopTitleKeepalive();
    this.transport?.close();
    this.transport = null;
    setResourceExchange(null);
    this.inBattle = false;
    this.hangupActive = false;
    this.autoBattleActive = false;
    this.lastBattleOperate = null;
    this.roleSelectInFlight = false;
    this.setPhase("idle", "stopped");
  }

  /**
   * Leave-game / 退出登录: disconnect and wipe the entire local session.
   * Next login must rebuild from the wire (no prior-role map/skills/bag/team).
   * Account credentials are kept only so the login form can resubmit.
   */
  logout(): void {
    this.clearHangupDesired();
    this.resetLocalSession(true);
    this.setPhase("idle", "logged-out");
  }

  /**
   * Hard-reset runtime state to a fresh client session (transport closed).
   * @param notify push empty bag/skills/NPC/team to UI listeners
   */
  private resetLocalSession(notify: boolean): void {
    this.stopPoll();
    this.stopTitleKeepalive();
    this.transport?.close();
    this.transport = null;
    setResourceExchange(null);

    this.servers = [];
    this.roles = [];
    this.selectedServerId = 1;
    this.selectedRoleId = 0;
    this.roleName = "";
    this.roleJobIndex = 0;
    this.roleSex = 0;
    this.scenePeerById.clear();
    this.scenePeerCosmeticsById.clear();
    this.scene = "";
    this.loadedScene = "";
    this.sceneTransferExpected = null;
    this.transferIgnoredScene = null;
    this.roleEnterSceneLock = null;
    this.enterSceneGen++;
    this.enterScenePromise = null;
    this.landingWait = null;
    this.moveBuf = [];
    this.lastMoveFlush = 0;
    this.roleSelectInFlight = false;
    this.pendingTaskId = 0;
    this.pendingTaskMode = "accept";
    this.guildListPage = 1;
    this.guildMemberPage = 1;
    this.guildAppPage = 1;
    this.guildListUi = true;
    this.guildBrowseKind = "list";

    this.clearRoleBoundState(notify);
  }

  /**
   * Drop all state owned by the previously entered role.
   * Multi-role accounts must not merge bag seq / worn / skills / team / NPC
   * catalogs from role A into role B (same starting town is a common case).
   */
  private clearRoleBoundState(notify = false): void {
    this.npcs = [];
    this.sceneMonsters = [];
    this.sceneMonsterCatalog = [];
    this.lastNpcId = 0;
    this.pendingChallengeGuideActorId = 0;
    this.pendingChallengeEnemyId = 0;
    this.pendingEncounterVisual = null;
    this.nearbyList = [];
    this.scenePeerById.clear();
    this.scenePeerCosmeticsById.clear();
    this.teamMembers = [];
    this.teamLeaderId = 0;
    this.friends = [];
    this.acceptedTasks = [];
    this.candidateTasks = [];
    this.npcTaskPrompts = new Map();
    this.pendingSocialInvite = null;
    this.inTrade = false;
    this.tradePeerName = "";
    this.hangupActive = false;
    this.autoBattleActive = false;
    this.lastBattleOperate = null;
    // Keep configured hangup skill across role-bound wipes when resume intent
    // is armed (hard reconnect / selectRole before re-arm).
    if (!this.hangupDesired) this.hangupSkillOperate = 0;
    this.hasServerPos = false;
    this.x = 0;
    this.y = 0;
    this.roleTitle = "";
    this.roleTitleGif = "";
    this.localChenghaoId = 0;
    this.dreamHudRemainMs = null;
    this.cosmetics = {
      unlockedChenghao: [],
      unlockedWing: [],
      unlockedMount: [],
      eqChenghao: 0,
      eqWing: 0,
      eqMount: 0,
    };
    this.hp = 500;
    this.hpMax = 500;
    this.mp = 200;
    this.mpMax = 200;
    this.level = 1;
    this.exp = 0;
    this.gold = 0;
    this.pkPoints = 0;
    this.bagItems = [];
    this.wornItems = [];
    this.pendingBagEquipDurability.clear();
    this.pendingBagEquipEnhance.clear();
    this.learnedSkillIds = [];
    this.learnedSkills = [];
    this.characterStats = null;
    this.mallState = null;
    this.designationPage = null;
    this.rankingPage = null;
    this.guildId = 0;
    this.guildName = "";
    this.guildSelfRank = 0;
    this.pendingGuildCreateName = "";
    this.inBattle = false;
    this.pendingTaskId = 0;
    if (notify) {
      this.cb.onNpcs([]);
      this.cb.onTeamUpdate?.({ members: [], leaderId: 0 });
      this.cb.onBag?.([]);
      this.cb.onWorn?.([]);
      this.cb.onSkills?.([]);
      this.cb.onNpcTaskPrompts?.(new Map());
    }
  }

  private clearWorldState(): void {
    this.scene = "";
    this.loadedScene = "";
    this.sceneTransferExpected = null;
    this.roleEnterSceneLock = null;
    this.enterSceneGen++;
    this.enterScenePromise = null;
    this.selectedRoleId = 0;
    this.roleName = "";
    this.roleJobIndex = 0;
    this.roleSex = 0;
    this.scenePeerById.clear();
    this.clearRoleBoundState(false);
  }

  /** Account takeover: stop live session and notify UI. */
  private forceAccountKick(reason: string): void {
    // Other-client login — do not fight them with auto re-login.
    this.clearHangupDesired();
    this.stopPoll();
    this.stopTitleKeepalive();
    this.transport?.close();
    this.transport = null;
    setResourceExchange(null);
    this.inBattle = false;
    this.roleSelectInFlight = false;
    this.setPhase("account-kicked", reason);
    this.cb.onAccountKicked?.(reason);
  }

  /** WT 1/1/12 → pause on server list (do not auto-select). */
  async fetchServerList(): Promise<void> {
    if (!this.transport) throw new Error("no transport");
    this.setPhase("login", "WT 1/1/12");
    const loginWt = () =>
      buildLoginAlt12Request(
        this.accountUser,
        this.accountPass,
        this.codeVersion,
      );
    let lastInfo = "";
    let lastResult = 0;
    const tryOnce = async () => {
      const listBodies = await this.transport!.exchange(0, loginWt());
      for (const body of listBodies) {
        if (!body || body.length < 5) continue;
        const packet = ingestPacket(body);
        if (!packet) continue;
        const list = parseServerList(packet);
        // Only guest issue (result=3) may replace typed credentials.
        // Echoed/empty passwords must not wipe the login password used by recharge API.
        if (list.result === 3 && list.username && list.password) {
          this.accountUser = list.username;
          this.accountPass = list.password;
        }
        if (list.information) lastInfo = list.information;
        if (list.result) lastResult = list.result;
        if (list.servers.length) {
          this.servers = list.servers;
          this.cb.onServers(this.servers);
        }
      }
      this.handleBodies(listBodies);
    };
    await tryOnce();
    if (this.servers.length === 0 && lastResult !== 2) await tryOnce();
    if (this.servers.length === 0) {
      // result=2 is a real 1/1/12 failure (codeVersion / auth), not a missing packet.
      if (lastResult === 2 || lastInfo) {
        const tip = lastInfo || `result=${lastResult}`;
        throw new Error(
          lastResult === 2
            ? `登录失败：${tip}`
            : `登录失败：${tip}（1/1/12）`,
        );
      }
      const detail = lastResult ? `result=${lastResult}` : "无 1/12 serverinfo";
      throw new Error(
        `未收到服务器列表（1/1/12：${detail}）。网关 :19092 / 服 :19090 需可达，且 MySQL 账号库可用（默认 CBE_MYSQL_* → jh_online）`,
      );
    }
    this.cb.onServers(this.servers);
    this.setPhase("select-server", `${this.servers.length} servers · ${this.transport.mode}`);
    this.startTitleKeepalive();
  }

  async confirmServer(serverId: number): Promise<void> {
    if (!this.transport) throw new Error("no transport");
    await this.ensureLiveSocket();
    this.selectedServerId = serverId;
    this.setPhase("select-server", `serverID=${serverId}`);
    const selBodies = await this.transport.exchange(
      0,
      buildServerSelectRequest(serverId, 0),
    );
    this.handleBodies(selBodies);
    let gotList = this.roles.length > 0;
    if (!gotList) {
      for (const body of selBodies) {
        const packet = ingestPacket(body);
        if (packet && packetHasTitleRoleList(packet)) {
          gotList = true;
          break;
        }
      }
    }
    if (!gotList) {
      throw new Error("未收到角色列表（1/1/4 actorinfo）");
    }
    // Empty actorinfo (count=0) is valid — logged-in accounts create roles here.
    this.cb.onRoles(this.roles);
    this.setPhase("select-role", `${this.roles.length} roles`);
    this.startTitleKeepalive();
  }

  /**
   * Title create 1/1/7. On success inserts a local RoleRow (CBE mmTitle:0x5324)
   * so the player can select without re-fetching the list.
   */
  async createRole(
    jobIndex: number,
    sex: number,
    name: string,
  ): Promise<RoleRow> {
    if (!this.transport) throw new Error("未连接（请重新登录）");
    const trimmed = name.trim();
    if (!trimmed) throw new Error("请输入角色名");
    if (this.roles.some((r) => r.name === trimmed)) {
      throw new Error("名已被注册");
    }
    if (this.roles.length >= TITLE_ROLE_MAX) {
      throw new Error(`角色已满（最多${TITLE_ROLE_MAX}个）`);
    }
    await this.ensureLiveSocket();
    this.setPhase("create-role", trimmed);
    const bodies = await this.transport.exchange(
      0,
      buildRoleCreateRequest(jobIndex, sex, trimmed),
    );
    let created: RoleRow | null = null;
    for (const body of bodies) {
      const packet = ingestPacket(body);
      if (!packet) continue;
      const ack = parseRoleCreateResult(packet);
      if (!ack) {
        this.handleBodies([body]);
        continue;
      }
      if (ack.result !== 0 || !ack.actorId) {
        const tip =
          ack.result === 2
            ? "名已被注册"
            : ack.result === 1 && this.roles.length >= TITLE_ROLE_MAX
              ? `角色已满（最多${TITLE_ROLE_MAX}个）`
              : `创建失败（result=${ack.result}）`;
        throw new Error(tip);
      }
      created = {
        roleId: ack.actorId,
        jobIndex: Math.max(0, Math.min(2, jobIndex | 0)),
        sex: sex ? 1 : 0,
        name: trimmed,
        level: 1,
      };
      if (!this.roles.some((r) => r.roleId === created!.roleId)) {
        this.roles = [...this.roles, created];
      }
      this.cb.onRoles(this.roles);
      this.setPhase("select-role", `created ${created.name}`);
      this.startTitleKeepalive();
      return created;
    }
    this.handleBodies(bodies);
    throw new Error("创建角色无有效应答（1/1/7）");
  }

  /** Title delete 1/1/8. On success removes the local RoleRow. */
  async deleteRole(roleId: number): Promise<void> {
    if (!this.transport) throw new Error("未连接（请重新登录）");
    if (!this.roles.some((r) => r.roleId === roleId)) {
      throw new Error("未选中角色");
    }
    await this.ensureLiveSocket();
    this.setPhase("delete-role", `actorID=${roleId}`);
    const bodies = await this.transport.exchange(0, buildRoleDeleteRequest(roleId));
    for (const body of bodies) {
      const packet = ingestPacket(body);
      if (!packet) continue;
      const result = parseRoleDeleteResult(packet);
      if (result == null) {
        this.handleBodies([body]);
        continue;
      }
      if (result !== 0) throw new Error(`删除失败（result=${result}）`);
      this.roles = this.roles.filter((r) => r.roleId !== roleId);
      if (this.selectedRoleId === roleId) this.selectedRoleId = 0;
      this.cb.onRoles(this.roles);
      this.setPhase("select-role", `${this.roles.length} roles`);
      this.startTitleKeepalive();
      return;
    }
    this.handleBodies(bodies);
    throw new Error("删除角色无有效应答（1/1/8）");
  }

  async selectRole(roleId: number): Promise<void> {
    if (!this.transport) throw new Error("未连接（请重新登录）");
    if (this.roleSelectInFlight) return;
    this.roleSelectInFlight = true;
    try {
      await this.ensureLiveSocket();
      this.stopPoll();
      this.stopTitleKeepalive();
      // Drop previous role caches before applying the new enter packet — bag
      // merge-by-itemId and same-town NPC catalogs otherwise leak across roles.
      this.scene = "";
      this.loadedScene = "";
      this.sceneTransferExpected = null;
      this.roleEnterSceneLock = null;
      this.enterSceneGen++;
      this.enterScenePromise = null;
      this.selectedRoleId = roleId;
      const picked = this.roles.find((r) => r.roleId === roleId);
      this.roleJobIndex = picked?.jobIndex ?? 0;
      this.roleSex = picked?.sex ?? 0;
      this.roleName = picked?.name ?? "";
      this.clearRoleBoundState(true);
      this.cb.onRoleSwitch?.();
      this.setPhase("enter-role", `actorID=${roleId}`);
      const bodies = await this.transport.exchange(0, buildRoleSelectRequest(roleId));
      this.handleBodies(bodies);
      void warmItemCatalogs().then(() => this.refreshCatalogLabelsOnOwnedItems());
      // Live title path: 1/6+1/15 with actorinfo scene — not immediate 30/1.
      if (!this.scene) {
        throw new Error("选角后未解析到场景（1/6 actorinfo.scene），检查角色数据");
      }
      if ((this.selectedRoleId >>> 0) !== (roleId >>> 0)) {
        throw new Error(
          `选角串号：请求角色 ${roleId}，应答角色 ${this.selectedRoleId}`,
        );
      }
      // Pin actorinfo scene for the rest of selectRole (follow-up soft 30/2 must match).
      this.roleEnterSceneLock = this.scene;
      // Scene shell must exist before 27/11 catalog (avoid showScene clear race).
      await this.awaitEnterScene();
      // Complete server first-scene lifecycle (NPC/task seed + 30/2 ack).
      try {
        const groupBodies = await this.transport.exchange(0, buildGroupType1Request());
        this.handleBodies(groupBodies);
        const followBodies = await this.transport.exchange(
          0,
          buildSceneStartupFollowupRequest(),
        );
        this.handleBodies(followBodies);
        await this.awaitEnterScene();
      } catch (e) {
        console.warn("[session] scene startup follow-up", e);
      }
      this.startPoll();
    } finally {
      this.roleEnterSceneLock = null;
      this.roleSelectInFlight = false;
    }
  }

  /** Queue one CBE direction frame (1..4 or 0); flush every 10 bytes. */
  noteMoveDir(dir: number): void {
    this.moveBuf.push(dir & 0xff);
    if (this.moveBuf.length >= 10) void this.flushMove();
  }

  async flushMove(): Promise<void> {
    if (!this.transport || this.inBattle) {
      this.moveBuf = [];
      return;
    }
    if (!this.moveBuf.length) return;
    while (this.moveBuf.length < 10) this.moveBuf.push(0);
    const batch = Uint8Array.from(this.moveBuf.splice(0, 10));
    this.lastMoveFlush = performance.now();
    try {
      const bodies = await this.transport.exchange(0, buildMoveRequest(batch));
      this.handleBodies(bodies);
    } catch (e) {
      this.setPhase("move-error", e instanceof Error ? e.message : String(e));
    }
  }

  /** True when in a multi-member party and this seat is not the leader. */
  isTeamMemberNotLeader(): boolean {
    const selfId = this.selectedRoleId >>> 0;
    if (this.teamMembers.length < 2 || !selfId) return false;
    const leader = this.teamLeaderId >>> 0;
    return leader !== 0 && leader !== selfId;
  }

  /**
   * Walk-into portal: optional 2/10 Type=1 then 2/3+27/11+7/42 (empty gate),
   * then post-enter 25/5+2/3+27/11+7/42 for nonempty NPC catalog.
   */
  async requestSceneChange(
    mapId: string,
    exitId: number,
    opts?: { prelude?: boolean; mapType?: number },
  ): Promise<void> {
    if (!this.transport || this.inBattle) return;
    if (this.isTeamMemberNotLeader()) {
      this.setPhase("inworld", "队员不能切图");
      return;
    }
    const mapType = opts?.mapType ?? 2;
    this.stopPoll();
    this.sceneTransferExpected = mapId;
    this.setPhase("scene-change", `${mapId} exit=${exitId}`);
    this.beginExpectLanding(mapId);
    try {
      if (opts?.prelude !== false) {
        const pre = await this.transport.exchange(
          0,
          buildSceneChangePreludeRequest(),
        );
        this.handleBodies(pre);
      }
      const bodies = await this.transport.exchange(
        0,
        buildSceneChangeRequest(mapId, exitId, mapType),
      );
      this.handleBodies(bodies);
      await this.awaitEnterScene();
      // New scene shell: post-enter combo owns nonempty 27/11 (not bare 6/1).
      const catalogBodies = await this.transport.exchange(
        0,
        buildSceneChangePostEnterFollowupRequest(mapId, exitId, mapType),
      );
      this.handleBodies(catalogBodies);
      await this.awaitEnterScene();
      // Footfall must come from server posinfo — not prior-map or SCE spawn guess.
      await this.awaitAuthoritativeLanding();
    } catch (e) {
      this.landingWait = null;
      this.setPhase(
        "scene-change-error",
        e instanceof Error ? e.message : String(e),
      );
    } finally {
      this.sceneTransferExpected = null;
      if (this.transport && this.scene) this.startPoll();
    }
  }

  private trackEnterScene(
    scene: string,
    x: number,
    y: number,
    hasPos = true,
  ): void {
    if (hasPos) this.hasServerPos = true;
    // Cross-scene: drop stale catalog until the new map's 27/11 arrives
    // (临安大使 stuck on 桃花岛 was re-stamping old session.npcs after showScene).
    if (
      this.loadedScene &&
      scene &&
      !sceneKeysMatch(this.loadedScene, scene)
    ) {
      this.npcs = [];
      this.cb.onNpcs([]);
      // Drop outdoor SCE combat rows immediately — city maps have none, and a
      // stale bee/ghostfire pool would still feed F-key / collide challenges.
      this.sceneMonsters = [];
      this.sceneMonsterCatalog = [];
      this.clearNpcTaskPrompts("scene-change");
    }
    const gen = ++this.enterSceneGen;
    this.enterScenePromise = Promise.resolve(
      this.cb.onEnterScene(scene, x, y, { hasPos }),
    )
      .then(() => {
        // A newer enter superseded this load (role-select / portal race).
        if (gen !== this.enterSceneGen) return;
        this.loadedScene = scene;
      })
      .catch((e) => {
        if (gen !== this.enterSceneGen) return;
        console.warn("[session] onEnterScene", e);
      });
    if (hasPos) this.noteAuthoritativeLanding(scene);
  }

  /** True when role-select has pinned actorinfo scene and `scene` disagrees. */
  private roleEnterIgnoresScene(scene: string): boolean {
    return (
      !!this.roleEnterSceneLock &&
      !!scene &&
      !sceneKeysMatch(scene, this.roleEnterSceneLock)
    );
  }

  private applyServerLanding(
    scene: string,
    x: number,
    y: number,
    source: string,
  ): void {
    // Skip only when the *displayed* map already matches — not when `this.scene`
    // was advanced by a soft ack before showScene finished (07 map + 06 NPCs).
    const mapShowing =
      this.loadedScene !== "" && sceneKeysMatch(this.loadedScene, scene);
    this.scene = scene;
    this.x = x;
    this.y = y;
    this.hasServerPos = true;
    console.info(`[session] landing ${source} ${scene} @${x},${y}`);
    this.noteAuthoritativeLanding(scene);
    this.syncArenaHudForScene(scene);
    // 16/3 unstuck / cross-scene stone: always reload shell so feet+map match.
    if (mapShowing && source !== "16/3") {
      this.cb.onPlayerPos?.(x, y);
    } else {
      this.trackEnterScene(scene, x, y, true);
    }
  }

  /** Last enter ignored while sceneTransferExpected is locked (forensics). */
  private transferIgnoredScene: string | null = null;

  private transferIgnoresScene(scene: string): boolean {
    const ignore =
      !!this.sceneTransferExpected &&
      !sceneKeysMatch(scene, this.sceneTransferExpected);
    if (ignore) this.transferIgnoredScene = scene;
    return ignore;
  }

  private beginExpectLanding(scene: string): void {
    let resolve!: () => void;
    const promise = new Promise<void>((r) => {
      resolve = r;
    });
    this.landingWait = { scene, promise, resolve, done: false };
  }

  private noteAuthoritativeLanding(scene: string): void {
    const w = this.landingWait;
    if (!w || w.done) return;
    if (w.scene && !sceneKeysMatch(w.scene, scene)) return;
    w.done = true;
    w.resolve();
  }

  private async awaitAuthoritativeLanding(timeoutMs = 8000): Promise<void> {
    const w = this.landingWait;
    if (!w || w.done) {
      this.landingWait = null;
      return;
    }
    await Promise.race([
      w.promise,
      new Promise<void>((r) => window.setTimeout(r, timeoutMs)),
    ]);
    if (!w.done) {
      console.warn(
        `[session] landing timeout ${w.scene}; kept @${this.x},${this.y}`,
      );
    }
    this.landingWait = null;
  }

  private async awaitEnterScene(): Promise<void> {
    if (!this.enterScenePromise) return;
    const p = this.enterScenePromise;
    this.enterScenePromise = null;
    await p;
  }

  /** Tap scene teleport stone → 16/1 destination list (mmGame UI path). */
  /** Menu「脱离卡死」→ settings 12/3; server returns 16/3 safe landing. */
  async requestUnstuck(): Promise<void> {
    if (!this.transport || this.inBattle) return;
    this.setPhase("unstuck", "12/3");
    try {
      await this.flushMove();
      const bodies = await this.transport.exchange(0, buildSettingsUnstuckRequest(2));
      this.handleBodies(bodies);
      if (this.hasServerPos) {
        this.setPhase("inworld", `unstuck ${this.scene} @${this.x},${this.y}`);
      } else {
        this.setPhase("unstuck-warn", "未解析到 16/3 落点");
      }
    } catch (e) {
      this.setPhase(
        "unstuck-error",
        e instanceof Error ? e.message : String(e),
      );
    }
  }

  async requestTeleportStoneList(): Promise<void> {
    if (!this.transport || this.inBattle) return;
    this.setPhase("telestone", "16/1");
    try {
      const bodies = await this.transport.exchange(
        0,
        buildTeleportStoneListRequest(),
      );
      this.handleBodies(bodies);
    } catch (e) {
      this.setPhase(
        "telestone-error",
        e instanceof Error ? e.message : String(e),
      );
    }
  }

  /**
   * Map UI teleport stone: 16/4 prepare → confirm cost → one WT
   * `16/2+16/3+7/1(800)` (consume then deferred scene enter on poll).
   * exitId = sMap.dsh row ID; targetScene = exact sMap scene key.
   */
  async requestMapTeleport(
    curTeleportId: number,
    objTeleportId: number,
    exitId: number,
    targetScene: string,
  ): Promise<{ ok: boolean; cost: number; detail: string }> {
    return this.requestMapTeleportInternal(
      curTeleportId,
      objTeleportId,
      exitId,
      targetScene,
      false,
    );
  }

  /**
   * Scene n_telestone free in-region teleport: same 16/4 arm + 16/2+16/3
   * deferred enter, but no item 800 consume.
   */
  async requestRegionFreeTeleport(
    curTeleportId: number,
    objTeleportId: number,
    exitId: number,
    targetScene: string,
  ): Promise<{ ok: boolean; cost: number; detail: string }> {
    return this.requestMapTeleportInternal(
      curTeleportId,
      objTeleportId,
      exitId,
      targetScene,
      true,
    );
  }

  private async requestMapTeleportInternal(
    curTeleportId: number,
    objTeleportId: number,
    exitId: number,
    targetScene: string,
    free: boolean,
  ): Promise<{ ok: boolean; cost: number; detail: string }> {
    if (!this.transport || this.inBattle) {
      return { ok: false, cost: 0, detail: "不可传送" };
    }
    if (this.isTeamMemberNotLeader()) {
      return { ok: false, cost: 0, detail: "队员不能传送" };
    }
    const expectedScene = ensureSceneFile(targetScene.trim());
    if (!expectedScene || !exitId) {
      return { ok: false, cost: 0, detail: "目标场景无效" };
    }

    let stone: { itemId: number; count: number; seq: number } | undefined;
    if (!free) {
      // Resolve stone BEFORE 16/4 — bag open after confirm can race server target.
      stone = this.bagItems.find(
        (i) => i.itemId === TELEPORT_STONE_ITEM_ID && i.count >= 1 && i.seq > 0,
      );
      if (!stone) {
        await this.openBag();
        stone = this.bagItems.find(
          (i) => i.itemId === TELEPORT_STONE_ITEM_ID && i.count >= 1 && i.seq > 0,
        );
      }
      if (!stone) {
        return { ok: false, cost: 1, detail: "传送石不够" };
      }
    }

    this.stopPoll();
    this.transferIgnoredScene = null;
    this.sceneTransferExpected = expectedScene;
    this.setPhase(
      free ? "region-free-teleport" : "map-teleport",
      `16/4 cur=${curTeleportId} obj=${objTeleportId} exit=${exitId} → ${expectedScene}${free ? " free" : ""}`,
    );
    this.beginExpectLanding(expectedScene);

    try {
      await this.flushMove();
      const prepBodies = await this.transport.exchange(
        0,
        buildMapTeleportPrepareRequest(curTeleportId, objTeleportId),
      );
      this.handleBodies(prepBodies);
      let cost = free ? 0 : 1;
      let sawConfirm = false;
      for (const body of prepBodies) {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const conf = parseMapTeleportConfirm(packet);
        if (conf) {
          sawConfirm = true;
          if (!free) cost = conf.value || 1;
          if (conf.result !== 0) {
            return { ok: false, cost, detail: `确认失败 result=${conf.result}` };
          }
        }
      }
      // Without 16/4 confirm the combo parser rejects; first-object 16/2 then
      // either empty-acks or arms a wrong provisional — never deferred 30/1.
      if (!sawConfirm) {
        return {
          ok: false,
          cost,
          detail: `16/4无确认 cur=${curTeleportId} obj=${objTeleportId}`,
        };
      }
      if (!free) {
        if (!stone || stone.count < cost) {
          return { ok: false, cost, detail: "传送石不够" };
        }
      }

      // Drop prior map NPCs only once exit is armed (avoid wiping on confirm fail).
      this.npcs = [];
      this.cb.onNpcs([]);
      this.clearNpcTaskPrompts("map-teleport");

      const comboBodies = await this.transport.exchange(
        0,
        free
          ? buildMapTeleportFreeExitRequest(exitId, 1)
          : buildMapTeleportConfirmedExitRequest(exitId, cost, stone!.seq, cost),
      );
      this.handleBodies(comboBodies);

      // Paid combo success = inventory ack (7/1 and/or 7/7). Free combo returns
      // empty WT while arming deferred 30/1 — treat that as success.
      let comboKinds = "";
      let sawItemAck = false;
      let sawAnyObject = false;
      for (const body of comboBodies) {
        if (!body || body.length < 5) continue;
        const packet = ingestPacket(body);
        if (!packet) continue;
        for (const o of packet.objects) {
          sawAnyObject = true;
          comboKinds += `${comboKinds ? "," : ""}${o.kind}/${o.subtype}`;
          if (o.kind === 7 && (o.subtype === 1 || o.subtype === 7 || o.subtype === 11)) {
            sawItemAck = true;
          }
        }
      }
      if (!free && !sawItemAck) {
        return {
          ok: false,
          cost,
          detail: `扣石组合包未生效(${comboKinds || `empty×${comboBodies.length}`})`,
        };
      }
      if (free && sawAnyObject && !comboKinds.includes("16/") && comboKinds !== "") {
        // Unexpected non-empty payload on free path — still try landing.
        console.warn("[session] region-free-teleport combo", comboKinds);
      }

      // Server refuses deferred enter on the same 100ms scheduler tick as the
      // combo; wait one frame before scene-poll, then poll calmly (avoid TCP storm).
      await new Promise<void>((r) => window.setTimeout(r, 150));
      let pollErrors = 0;
      for (let i = 0; i < 40 && this.landingWait && !this.landingWait.done; i++) {
        await this.pollOnce();
        if (this.phase === "poll-error") pollErrors++;
        if (this.landingWait?.done) break;
        await new Promise<void>((r) => window.setTimeout(r, 150));
      }
      const waitSnap = this.landingWait;
      await this.awaitAuthoritativeLanding(5000);
      const landed = !!(waitSnap && waitSnap.done);
      await this.awaitEnterScene();

      if (!sceneKeysMatch(this.scene || "", expectedScene)) {
        const ignored = this.transferIgnoredScene
          ? ` 忽略${this.transferIgnoredScene}`
          : "";
        const pollHint = pollErrors ? ` poll错误×${pollErrors}` : "";
        if (!landed) {
          return {
            ok: false,
            cost,
            detail: `进图超时 期望${expectedScene} 仍在${this.scene || "无"}${ignored}${pollHint}（若CBE同账号在线请先退出）`,
          };
        }
        return {
          ok: false,
          cost,
          detail: `落点不符 期望${expectedScene} 实际${this.scene || "无"}${ignored}`,
        };
      }

      /*
       * Catalog pull after deferred 30/1:
       * 1) Portal-shaped post-enter with exitID=0 (inherit armed landing) —
       *    server mark_pending re-delivers even if poll stole the one-shot.
       * 2) If still empty, CBE map-stone 2/3 + WT6/1.
       * Empty 27/11 gates during sceneTransferExpected do not wipe a nonempty
       * catalog (see handleBodies).
       */
      const catalogBodies = await this.transport.exchange(
        0,
        buildSceneChangePostEnterFollowupRequest(expectedScene, 0, 2),
      );
      this.handleBodies(catalogBodies);
      await this.awaitEnterScene();
      if (!this.npcs.length) {
        const completeBodies = await this.transport.exchange(
          0,
          buildSceneChangeRequest(expectedScene, 0, 2),
        );
        this.handleBodies(completeBodies);
        await this.awaitEnterScene();
        const wt6Bodies = await this.transport.exchange(
          0,
          buildSceneNpcCatalogFollowupRequest(),
        );
        this.handleBodies(wt6Bodies);
        await this.awaitEnterScene();
      }
      if (!this.npcs.length) {
        console.warn(
          "[session] map-teleport landed without 27/11 NPCs",
          expectedScene,
        );
      }

      this.setPhase(
        "inworld",
        `${free ? "region-free" : "map-teleport"} ${this.scene} @${this.x},${this.y}`,
      );
      return { ok: true, cost, detail: "ok" };
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      this.setPhase(free ? "region-free-teleport-error" : "map-teleport-error", msg);
      this.landingWait = null;
      return { ok: false, cost: 0, detail: msg };
    } finally {
      this.sceneTransferExpected = null;
      this.transferIgnoredScene = null;
      if (this.transport && this.scene) this.startPoll();
    }
  }

  /**
   * Replace both the live wild pool and the stable SCE seed catalog (map load /
   * combat-spawn refresh). Do not call when only syncing live/suppressed lists.
   */
  adoptSceneCombatSpawns(monsters: NpcActor[]): void {
    // Post-battle forceReload can briefly yield an empty combatSpawns list —
    // never wipe a good seed catalog or hangup will no-monster until retry.
    if (!monsters.length && this.sceneMonsterCatalog.length) {
      this.sceneMonsters = [];
      return;
    }
    const copy = monsters.map((m) => ({ ...m }));
    this.sceneMonsterCatalog = copy;
    this.sceneMonsters = monsters.map((m) => ({ ...m }));
  }

  /** Drop live + seed pools (map hop / role wipe). */
  clearSceneCombatSpawns(): void {
    this.sceneMonsters = [];
    this.sceneMonsterCatalog = [];
  }

  /**
   * Same species seed used by collide / F-key / hangup. Challenge assigns
   * catalog `challenge_enemy_id` as preferNpcId (hangup path + id).
   */
  armEncounterVisualLikeCollide(
    px = this.x | 0,
    py = this.y | 0,
    preferNpcId?: number,
  ): {
    id: number;
    index: number;
    battleX: number;
    battleY: number;
    encounter: NpcActor | null;
  } {
    let id = preferNpcId ?? 0;
    let index = 0;
    let battleX = px | 0;
    let battleY = py | 0;
    let encounter: NpcActor | null = null;
    const pickNearest = (pool: NpcActor[]): NpcActor | null => {
      if (!pool.length) return null;
      let best = pool[0]!;
      let bestD = Infinity;
      for (const n of pool) {
        const d = (n.x - px) * (n.x - px) + (n.y - py) * (n.y - py);
        if (d < bestD) {
          bestD = d;
          best = n;
        }
      }
      return best;
    };
    const catalogMonsters = this.npcs.filter(
      (n) =>
        /monster|怪|e_/i.test(n.scriptName) ||
        /^e_/i.test(n.actorResource) ||
        (/^e_/i.test(n.name) && n.actorId < 100000),
    );
    /** Pixi world-boss map overlay id — hangup must never pick this node.
     * Do NOT use `>= 0x70000000`: outdoor SCE kind=3 also uses 0x70000000+i. */
    const WORLD_BOSS_OVERLAY_ID = 0x70ffffff;
    const isWorldBossOverlay = (n: NpcActor) =>
      ((n.actorId >>> 0) === WORLD_BOSS_OVERLAY_ID) ||
      /世界BOSS|世界Boss/i.test(n.name || "");
    const hangupPool = (pool: NpcActor[]) =>
      pool.filter((n) => !isWorldBossOverlay(n));
    if (!id) {
      // Live SCE first; if every spawn is post-battle-suppressed, reuse map
      // seeds (same species/home). Never invent hard-coded 9001 (formula Lv45).
      // Hangup nearest: exclude world-boss overlay so AFK only farms trash.
      encounter =
        pickNearest(hangupPool(this.sceneMonsters)) ||
        pickNearest(hangupPool(this.sceneMonsterCatalog)) ||
        pickNearest(hangupPool(catalogMonsters));
      if (encounter) {
        id = encounter.battleActorId ?? encounter.actorId;
        index = id;
        battleX = (encounter.homeX ?? encounter.x) | 0;
        battleY = (encounter.homeY ?? encounter.y) | 0;
      }
    } else if (id >= 0x70000000) {
      const local =
        this.sceneMonsters.find((n) => n.actorId === id) ??
        this.sceneMonsterCatalog.find((n) => n.actorId === id);
      encounter = local ?? null;
      id = local?.battleActorId ?? id;
      index = id;
      if (local) {
        battleX = (local.homeX ?? local.x) | 0;
        battleY = (local.homeY ?? local.y) | 0;
      }
    } else {
      // Catalog / challenge_enemy_id: keep id + catalog actor. Do not borrow
      // nearest map spawn visual (wrong species) or its tile (pos remap stole
      // floor bind stats). Neutral 1,1 matches instance-challenge wire fallback.
      const local =
        this.sceneMonsters.find(
          (n) => n.battleActorId === id || n.actorId === id,
        ) ??
        this.sceneMonsterCatalog.find(
          (n) => n.battleActorId === id || n.actorId === id,
        );
      if (local) {
        encounter = local;
        battleX = (local.homeX ?? local.x) | 0;
        battleY = (local.homeY ?? local.y) | 0;
        index = local.battleActorId ?? id;
      } else {
        encounter = null;
        battleX = 1;
        battleY = 1;
        index = id;
      }
    }
    if (!id) {
      this.pendingEncounterVisual = null;
      return { id: 0, index: 0, battleX: px | 0, battleY: py | 0, encounter: null };
    }
    const catalogActor = monsterActorForEnemyId(id) || "";
    const explicitCatalog = (preferNpcId ?? 0) !== 0;
    const visualRes =
      (explicitCatalog
        ? catalogActor ||
          encounter?.battleActorResource ||
          encounter?.actorResource
        : encounter?.battleActorResource ||
          encounter?.actorResource ||
          catalogActor) || "";
    this.pendingEncounterVisual = {
      battleActorId: id >>> 0,
      x: battleX,
      y: battleY,
      actorResource: visualRes,
      name: explicitCatalog ? undefined : encounter?.name,
    };
    return { id, index, battleX, battleY, encounter };
  }

  /**
   * Start wild / challenge 4/1. Returns true only when a battle-start object
   * (4/5 or 4/10) arrived — callers use this to restore map SCE markers on miss.
   */
  async startBattleNearPlayer(
    px: number,
    py: number,
    preferNpcId?: number,
  ): Promise<boolean> {
    if (!this.transport || this.inBattle) return false;
    // Cities: no wild automonster. Challenge/hangup with an explicit catalog id
    // (e.g. 临安守关 / dream challenge_enemy) still uses the same 4/1 path.
    if (isCitySceneKey(this.scene || this.loadedScene || "") && !((preferNpcId ?? 0) >>> 0)) {
      // Cities: no automonster / kind=3; refuse so outdoor test bees cannot linger.
      this.cb.onChat?.([{ type: 5, text: "[系]城内不能遇怪" }]);
      this.cb.onStatus?.({
        mode: this.transport?.mode ?? "?",
        phase: "world",
        detail: "城内不能遇怪",
      });
      console.warn("[session] reject city wild battle", this.scene);
      return false;
    }
    const { id, index, battleX, battleY, encounter } =
      this.armEncounterVisualLikeCollide(px, py, preferNpcId);
    if (!(id >>> 0)) {
      console.warn("[session] battle wild refused (no scene monster)", this.scene);
      this.setPhase("world", "no-scene-monster");
      return false;
    }
    // Map actor id for hangup/challenge (collide already suppressed by caller).
    const mapActorId = (encounter?.actorId ?? 0) >>> 0;
    if (mapActorId >= 0x70000000) {
      this.cb.onEngageSceneMonster?.(mapActorId, battleX, battleY);
    } else if (battleX > 0 || battleY > 0) {
      this.cb.onEngageSceneMonster?.(0, battleX, battleY);
    }
    this.setPhase("battle-start", `id=${id} index=${index} pos=${battleX},${battleY}`);
    this.inBattle = true;
    const bodies = await this.transport.exchange(
      0,
      buildBattleStartRequest(id, index, battleX, battleY),
    );
    this.handleBodies(bodies);
    // Prefer parseBattleStart (same as handleBodies) over raw subtype — a
    // tear-down-prefixed WT can confuse a naive 4/5 scan and falsely fail,
    // which stacked hangup retry (8s+5s≈13s).
    const started = bodies.some((b) => {
      const p = ingestPacket(b);
      if (!p) return false;
      if (
        p.objects.some(
          (o) => o.kind === 4 && (o.subtype === 5 || o.subtype === 10),
        )
      ) {
        return true;
      }
      return !!parseBattleStart(p, {
        roleName: this.roleName,
        partyMembers: this.teamMembers.map((m) => ({
          id: m.id,
          name: m.name,
          jobIndex: m.jobIndex,
          sex: m.sexGroup >= 1 ? (m.sexGroup - 1) & 1 : 0,
        })),
      });
    });
    if (!started) {
      this.inBattle = false;
      this.pendingEncounterVisual = null;
      this.cb.onRestoreSceneMonster?.(mapActorId, battleX, battleY);
      return false;
    }
    return true;
  }

  async battleOperate(
    targetIndex: number,
    operate = 0,
    opts?: { hangup?: boolean },
  ): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(
      0,
      buildBattleOperateRequest(targetIndex, operate, opts),
    );
    this.handleBodies(bodies);
  }

  /**
   * UI: hangup or [自动] hides operate menu / shows banner.
   */
  shouldInBattleAuto(): boolean {
    return this.autoBattleActive || this.hangupActive;
  }

  /**
   * Client still drives 4/2 only for Pixi hangup-alone.
   * [自动] (with or without hangup) uses gateway prefer + poll synth.
   */
  shouldClientDriveInBattleAuto(): boolean {
    return this.hangupActive && !this.autoBattleActive;
  }

  /**
   * Hangup alone: only hangupSkillOperate (set when starting hangup).
   * [自动]: only lastBattleOperate / server LastOperate — never share.
   */
  inBattleAutoOperate(): number {
    if (this.hangupActive && !this.autoBattleActive) {
      return this.hangupSkillOperate | 0;
    }
    if (this.lastBattleOperate != null) return this.lastBattleOperate | 0;
    return 0;
  }

  /**
   * Record a battle operate for subsequent [自动] only.
   * Hangup casts never write this (source "hangup").
   * `source: "auto"` — [自动] 普攻 must not erase a remembered skill.
   */
  noteManualBattleOperate(
    operate: number,
    source: "manual" | "auto" | "hangup" = "manual",
  ): void {
    if (source === "hangup") return;
    const op = operate | 0;
    if (
      source === "auto" &&
      op === 0 &&
      this.lastBattleOperate != null &&
      (this.lastBattleOperate | 0) !== 0
    ) {
      return;
    }
    this.lastBattleOperate = op;
  }

  /**
   * Open/close gateway in-battle auto (1/4/11).
   * On: seed Operate from [自动] memory only + hangup=0; Off: type=0.
   * Never seed hangupSkillOperate into LastOperate (hangup / auto separated).
   * Omit Operate when unset — seeding Operate=0 wipes server LastOperate.
   */
  async setBattleAutoPrefer(on: boolean): Promise<void> {
    if (!this.transport) {
      this.autoBattleActive = on;
      return;
    }
    this.autoBattleActive = on;
    const seedOpts: { operate?: number; hangup: number } = { hangup: 0 };
    if (this.lastBattleOperate != null) {
      // Explicit memory including intentional 普攻 (0).
      seedOpts.operate = this.lastBattleOperate | 0;
    }
    const bodies = await this.transport.exchange(
      0,
      on
        ? buildBattleAutoFlagRequest(1, seedOpts)
        : buildBattleAutoFlagRequest(0),
    );
    this.handleBodies(bodies);
  }

  /** Battle medicine/flask: WT 1/4/3 { index=ally/self wire, seq }. */
  async battleItemUse(targetIndex: number, seq: number): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(
      0,
      buildBattleItemUseRequest(targetIndex, seq),
    );
    this.handleBodies(bodies);
  }

  /**
   * Battle 仙术 list: server learned IDs + skill.dsh names for this role's job.
   * If 12/1 was never parsed (e.g. old len-39 follow-up), refresh via 12/1+7/42.
   */
  async getBattleSkills(): Promise<BattleSkillRow[]> {
    if (this.learnedSkills.length) return this.learnedSkills;
    if (this.learnedSkillIds.length) {
      await this.refreshLearnedSkillRows();
      return this.learnedSkills;
    }
    await this.refreshLearnedSkillsFromServer();
    if (this.learnedSkillIds.length) {
      await this.refreshLearnedSkillRows();
    }
    return this.learnedSkills;
  }

  /** Request empty 12/1+7/42 so mock returns learnednum/learnedskill. */
  async refreshLearnedSkillsFromServer(): Promise<void> {
    if (!this.transport) return;
    try {
      await this.prepareLiveExchange("skills");
      this.learnedSkillIds = [];
      this.learnedSkills = [];
      this.cb.onSkills?.(this.learnedSkills);
      const bodies = await this.transport.exchange(
        0,
        buildLearnedSkillsRefreshRequest(),
      );
      this.handleBodies(bodies);
    } catch (e) {
      this.notifyPanelFetchFailed("技能", e);
    }
  }

  /**
   * PixiJS-only: forget a learned skill for 5金 (1/12/2).
   * Server keeps at least one skill; refreshes 12/1 + money via 10/26.
   */
  async forgetSkill(skillId: number): Promise<SkillForgetResult> {
    const fail = (message: string, result = 0): SkillForgetResult => ({
      result,
      ok: false,
      message,
      skillId: skillId >>> 0,
    });
    if (!this.transport || !skillId) return fail("未连接服务器");
    const money = this.characterStats?.money ?? this.gold;
    if ((money >>> 0) < SKILL_FORGET_COST_COPPER) {
      return fail("金钱不足（需5金）", 3);
    }
    if (this.learnedSkillIds.length <= 1 && this.learnedSkills.length <= 1) {
      return fail("至少保留一个技能", 4);
    }
    const bodies = await this.transport.exchange(
      0,
      buildSkillForgetRequest(skillId),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseSkillForgetResult(packet);
        if (parsed) {
          if (parsed.ok) await this.refreshLearnedSkillRows();
          return parsed;
        }
      } catch {
        /* skip */
      }
    }
    return fail("遗忘无响应");
  }

  private async refreshLearnedSkillRows(): Promise<void> {
    const ids = this.learnedSkillIds.length ? this.learnedSkillIds : [];
    try {
      this.learnedSkills = await resolveLearnedSkills(this.roleJobIndex, ids);
      this.cb.onSkills?.(this.learnedSkills);
    } catch (e) {
      console.warn("[session] skill.dsh resolve failed", e);
      // Keep IDs usable even if local catalog fetch fails.
      this.learnedSkills = ids.map((id) => ({
        id,
        name: `技能#${id}`,
        targetKind: 3 as const,
      }));
      this.cb.onSkills?.(this.learnedSkills);
    }
  }

  async battleEscape(): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildBattleEscapeRequest());
    this.handleBodies(bodies);
  }

  /**
   * After PvE death UI: 1/7/14 result=1 (复活石) or 2 (普通复活).
   * Server answers with 4/7+4/8… or 20/1+30/1 scene enter — both leave battle.
   */
  async battleDeathPrompt(result: 1 | 2): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(
      0,
      buildBattleDeathPromptRequest(result),
    );
    this.handleBodies(bodies);
  }

  /** Persist / clear hangup intent (player wants AFK hangup across disconnect). */
  clearHangupDesired(): void {
    this.hangupDesired = false;
    this.clearHangupEncounterState();
    clearHangupResumeStorage();
    void this.refreshNativeKeepAlive();
  }

  markHangupDesired(): void {
    this.hangupDesired = true;
    // Session-only intent — do not persist for cold-start / reconnect auto-hangup.
    void this.refreshNativeKeepAlive();
  }

  private clearHangupEncounterTimer(): void {
    if (this.hangupEncounterTimer != null) {
      clearTimeout(this.hangupEncounterTimer);
      this.hangupEncounterTimer = null;
    }
    this.hangupEncounterDueAt = 0;
  }

  private clearHangupEncounterState(): void {
    this.clearHangupEncounterTimer();
    this.hangupEncounterInFlight = false;
    this.hangupEncounterEarliestAt = 0;
  }

  /**
   * After leaving a hangup fight, wait then start the next collide-style 4/1.
   * Server does not deliver hangup battles for Pixi.
   * Interval is map-side only (armed from onLeftBattleForHangup after settle).
   */
  scheduleNextHangupEncounter(
    delayMs = HANGUP_ENCOUNTER_INTERVAL_MS,
  ): void {
    this.clearHangupEncounterTimer();
    if (!this.hangupDesired) return;
    const now = Date.now();
    const earliest = this.hangupEncounterEarliestAt;
    const want = Math.max(500, delayMs | 0);
    // Retries must not fire before the map-side interval from leave.
    const delay =
      earliest > now ? Math.max(want, earliest - now) : want;
    this.hangupEncounterDueAt = now + delay;
    this.hangupEncounterTimer = setTimeout(() => {
      this.hangupEncounterTimer = null;
      void this.tickHangupEncounter("schedule");
    }, delay);
  }

  /** Call when battle UI returns to world — continue hangup loop if armed. */
  onLeftBattleForHangup(): void {
    if (!this.hangupDesired) {
      this.hangupActive = false;
      return;
    }
    this.hangupActive = true;
    this.hangupEncounterEarliestAt =
      Date.now() + HANGUP_ENCOUNTER_INTERVAL_MS;
    this.scheduleNextHangupEncounter(HANGUP_ENCOUNTER_INTERVAL_MS);
  }

  private async tickHangupEncounter(reason: string): Promise<void> {
    if (!this.hangupDesired || this.inBattle || !this.transport) return;
    if (this.hangupEncounterInFlight) return;
    if (isCitySceneKey(this.scene || this.loadedScene || "")) {
      this.hangupActive = false;
      this.clearHangupDesired();
      this.cb.onChat?.([{ type: 5, text: "[挂]城内不能挂机" }]);
      return;
    }
    const now = Date.now();
    if (
      this.hangupEncounterEarliestAt > 0 &&
      now < this.hangupEncounterEarliestAt
    ) {
      this.scheduleNextHangupEncounter(
        this.hangupEncounterEarliestAt - now,
      );
      return;
    }
    this.hangupEncounterInFlight = true;
    // Claim the slot before await so FGS keepalive cannot start a twin 4/1.
    this.clearHangupEncounterTimer();
    this.hangupActive = true;
    // Live SCE may still be in the 8s post-battle suppress window — hangup
    // must keep using seed catalog (same species/home), not an empty live pool.
    if (!this.sceneMonsters.length && this.sceneMonsterCatalog.length) {
      this.sceneMonsters = this.sceneMonsterCatalog.map((m) => ({ ...m }));
    }
    console.info("[session] hangup encounter", reason);
    try {
      const ok = await this.startBattleNearPlayer(this.x | 0, this.y | 0);
      // Only retry when we did not enter battle — never arm a second timer
      // while a twin race already owns inBattle.
      if (!ok && this.hangupDesired && !this.inBattle) {
        this.scheduleNextHangupEncounter(HANGUP_ENCOUNTER_RETRY_MS);
      } else if (ok) {
        this.hangupEncounterEarliestAt = 0;
      }
    } finally {
      this.hangupEncounterInFlight = false;
    }
  }

  private persistHangupResume(): void {
    /* intentionally empty — auto resume after relaunch is disabled */
  }

  hasHangupResumePending(): boolean {
    return false;
  }

  /** Apply server hangup chat lines to local flags (CBE path). Pixi arms hangup itself. */
  private applyHangupChatFlag(text: string): void {
    if (text.includes("下一场完成后挂机停止")) {
      this.clearHangupDesired();
      this.hangupActive = false;
      return;
    }
    const flag = hangupActiveFromChatLine(text);
    if (flag == null) return;
    if (text.includes("不能挂机") || text.includes("挂机失败")) {
      this.clearHangupDesired();
      this.hangupActive = false;
      return;
    }
    this.hangupActive = flag;
    if (!flag) this.clearHangupDesired();
  }

  /** Disabled: no reconnect / cold-start auto hangup. */
  async tryRearmHangup(_reason: string): Promise<void> {
    return;
  }

  /** Disabled: no silent re-login + hangup. */
  async resumeFromHangupStorage(): Promise<boolean> {
    clearHangupResumeStorage();
    return false;
  }

  /**
   * Player-requested hangup stop (HUD / battle 关闭).
   * Pixi hangup is client-only — no 2/10 toggle / server loop.
   */
  async stopHangup(): Promise<void> {
    const was = this.hangupActive || this.hangupDesired;
    this.hangupActive = false;
    this.clearHangupDesired();
    this.setPhase("hangup", "client stop");
    if (was) this.cb.onChat?.([{ type: 5, text: "[挂]已停止挂机" }]);
  }

  /**
   * Start Pixi hangup: same battle path as collide (`4/1` / startBattleNearPlayer).
   * Monster id/pos come from the current scene pool; server does not push fights.
   * Caller must use stopHangup() to cancel (HUD already branches).
   */
  async startHangup(): Promise<void> {
    if (!this.transport) return;
    if (isCitySceneKey(this.scene || this.loadedScene || "")) {
      this.cb.onChat?.([{ type: 5, text: "[挂]城内不能挂机" }]);
      return;
    }
    this.markHangupDesired();
    this.hangupActive = true;
    this.setPhase("hangup", "client collide-loop");
    this.cb.onChat?.([{ type: 5, text: "[挂]已开始挂机" }]);
    /* Warm 战斗心得 remain so hangup settle can show 有效时间 without「修」. */
    void this.requestBuffStatus();
    if (this.inBattle) return;
    await this.tickHangupEncounter("start");
  }

  async openDesignations(): Promise<DesignationPage | null> {
    if (!this.transport) return null;
    await this.syncCosmetics();
    const bodies = await this.transport.exchange(0, buildDesignationListRequest());
    this.handleBodies(bodies);
    this.designationPage = this.withChenghaoDesignations(this.designationPage);
    return this.designationPage;
  }

  /** Append unlocked Pixi chenghao rows (server CSV) into the classic 23 list. */
  withChenghaoDesignations(page: DesignationPage | null): DesignationPage | null {
    const base = page ?? { equippedId: 0, rows: [] };
    const equippedChenghao =
      this.cosmetics.eqChenghao ||
      this.localChenghaoId ||
      parseChenghaoId(this.roleTitleGif) ||
      0;
    const serverRows = base.rows.filter((r) => !isChenghaoId(r.id));
    const unlocked = new Set(
      this.cosmetics.unlockedChenghao.filter((id) => isChenghaoId(id)),
    );
    const fxRows = CHENGHAO_SPECS.filter((s) => unlocked.has(s.id)).map((s) => ({
      id: s.id,
      name: chenghaoLabel(s.id),
      desc: "特效称号",
      gif: chenghaoWireTag(s.id),
    }));
    const equippedId = equippedChenghao
      ? equippedChenghao
      : isChenghaoId(base.equippedId)
        ? 0
        : base.equippedId;
    return {
      equippedId,
      rows: [...serverRows, ...fxRows],
    };
  }

  /** Query 1/31/1 and apply equipped overlays. */
  async syncCosmetics(): Promise<CosmeticsState | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildCosmeticsQueryRequest());
    this.handleBodies(bodies);
    return this.cosmetics;
  }

  /**
   * Equip / unequip Pixi cosmetic via 1/31/3.
   * kind: 1=chenghao 2=wing 3=mount; id 0 unequips.
   */
  async equipCosmetic(
    kind: number,
    id: number,
  ): Promise<{ ok: boolean; state: CosmeticsState | null }> {
    if (!this.transport) return { ok: false, state: null };
    const bodies = await this.transport.exchange(
      0,
      buildCosmeticsEquipRequest(kind, id >>> 0),
    );
    let ok = false;
    let state: CosmeticsState | null = null;
    for (const body of bodies) {
      const packet = ingestPacket(body);
      if (!packet) continue;
      const ack = parseCosmeticsEquipResult(packet);
      if (ack) {
        ok = ack.ok;
        if (ack.state) state = ack.state;
      }
    }
    this.handleBodies(bodies);
    return { ok, state: state || (ok ? this.cosmetics : null) };
  }

  async equipWing(wingId: number): Promise<boolean> {
    const id = wingId | 0;
    if (id && !this.cosmetics.unlockedWing.includes(id)) return false;
    const r = await this.equipCosmetic(COSMETIC_KIND_WING, id);
    return r.ok;
  }

  async equipMount(mountId: number): Promise<boolean> {
    const id = mountId | 0;
    if (id && !this.cosmetics.unlockedMount.includes(id)) return false;
    const r = await this.equipCosmetic(COSMETIC_KIND_MOUNT, id);
    return r.ok;
  }

  private applyCosmeticsState(state: CosmeticsState): void {
    this.cosmetics = {
      unlockedChenghao: state.unlockedChenghao.slice(),
      unlockedWing: state.unlockedWing.slice(),
      unlockedMount: state.unlockedMount.slice(),
      eqChenghao: state.eqChenghao >>> 0,
      eqWing: state.eqWing >>> 0,
      eqMount: state.eqMount >>> 0,
    };
    this.localChenghaoId = this.cosmetics.eqChenghao;
    writeChenghaoPreference(this.cosmetics.eqChenghao);
    writeWingIdPreference(this.cosmetics.eqWing);
    writeMountIdPreference(this.cosmetics.eqMount);
    if (this.cosmetics.eqChenghao && isChenghaoId(this.cosmetics.eqChenghao)) {
      this.applySelfDesignation(
        chenghaoLabel(this.cosmetics.eqChenghao),
        chenghaoWireTag(this.cosmetics.eqChenghao),
      );
    } else if (
      parseChenghaoId(this.roleTitleGif) ||
      parseChenghaoId(this.roleTitle)
    ) {
      /* Unequipped chenghao: clear FX tag; classic 23/2 may refill later. */
      this.applySelfDesignation("", "");
    }
    this.designationPage = this.withChenghaoDesignations(this.designationPage);
    this.cb.onCosmeticsUpdate?.(this.cosmetics);
  }

  async selectDesignation(designationId: number): Promise<{
    ok: boolean;
    name?: string;
    gif?: string;
  }> {
    if (!this.transport) return { ok: false };
    const id = designationId | 0;
    if (isChenghaoId(id)) {
      if (!this.cosmetics.unlockedChenghao.includes(id)) {
        return { ok: false };
      }
      const r = await this.equipCosmetic(COSMETIC_KIND_CHENGHAO, id);
      if (!r.ok) return { ok: false };
      const name = chenghaoLabel(id);
      const gif = chenghaoWireTag(id);
      this.designationPage = this.withChenghaoDesignations(this.designationPage);
      return { ok: true, name, gif };
    }
    /* Classic title: server clears equipped chenghao and broadcasts 31/5. */
    this.localChenghaoId = 0;
    writeChenghaoPreference(0);
    const bodies = await this.transport.exchange(
      0,
      buildDesignationSelectRequest(id),
    );
    /* 23/3 ack and 23/2 update may arrive in separate bodies. */
    let ok = false;
    let gif = "";
    let name = "";
    for (const body of bodies) {
      const packet = ingestPacket(body);
      if (!packet) continue;
      const ack = parseDesignationSelectResult(packet);
      if (ack) {
        ok = ack.ok;
        if (ack.gif) gif = ack.gif;
        if (ack.name) name = ack.name;
      }
      const upd = parseDesignationSceneUpdate(packet);
      if (upd) {
        /* Always take wire fields — arena titles send empty overhead gif. */
        name = upd.name;
        gif = upd.gif;
        this.applySelfDesignation(name, gif);
      }
    }
    this.handleBodies(bodies);
    if (ok) {
      await this.openDesignations();
      const page = this.designationPage;
      const row =
        page?.rows.find((r) => r.id === designationId) ||
        page?.rows.find((r) => r.id === page.equippedId);
      if (row && !isChenghaoId(row.id)) {
        /* Prefer refreshed list; empty row.gif must clear prior wealth/level badge. */
        name = row.name || name;
        gif = row.gif || "";
      }
      this.applySelfDesignation(name, gif);
    }
    return { ok, name: name || undefined, gif: gif || undefined };
  }

  /**
   * Apply equipped designation to self nameplate state.
   * Always overwrite gif (including "") — arena text titles have empty overhead;
   * keeping a prior riches/level gif hid the new title until re-login.
   */
  private applySelfDesignation(name: string, gif: string): void {
    this.roleTitle = name || "";
    this.roleTitleGif = gif || "";
    this.cb.onDesignationUpdate?.({
      name: this.roleTitle,
      gif: this.roleTitleGif,
    });
  }

  async openRanking(type = 0, pageIndex = 0): Promise<RankingPage | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildRankingBoardRequest(type, pageIndex),
    );
    let page: RankingPage | null = null;
    for (const body of bodies) {
      const packet = ingestPacket(body);
      if (!packet) continue;
      const parsed = parseRankingBoard(packet, type);
      if (parsed) page = parsed;
    }
    this.handleBodies(bodies);
    this.rankingPage = page;
    return page;
  }

  isArenaScene(scene = this.scene): boolean {
    return /比武擂台/.test(scene) || scene.includes("\xb1\xc8\xce\xe4\xc0\xde\xcc\xa8");
  }

  private clearArenaHud(_reason: string): void {
    this.arenaHud = null;
    this.cb.onArenaHud?.(null);
  }

  private syncArenaHudForScene(scene: string): void {
    if (!this.isArenaScene(scene)) {
      this.clearArenaHud("left-arena-scene");
      return;
    }
    /* Entering venue: if 41/5 is starved, status poll still drives the strip. */
    void this.ensureArenaHudFallback("enter-scene");
  }

  /** When scene-sync omits 41/5, synthesize strip from 41/1 status. */
  private arenaHudFallbackAt = 0;
  private async ensureArenaHudFallback(reason: string): Promise<void> {
    if (!this.isArenaScene() || !this.transport) return;
    const now = Date.now();
    if (
      this.arenaHud &&
      (this.arenaHud.status === 1 || this.arenaHud.status === 2) &&
      now - this.arenaHudFallbackAt < 8000
    ) {
      return;
    }
    if (now - this.arenaHudFallbackAt < 2500 && reason !== "enter-scene") return;
    this.arenaHudFallbackAt = now;
    try {
      const st = await this.fetchArenaStatus();
      if (!this.isArenaScene()) return;
      if (!st || (st.status !== 1 && st.status !== 2)) {
        if (this.arenaHud) this.clearArenaHud("status-idle");
        return;
      }
      /* Prefer live 41/5 if it arrived during the await. */
      if (
        this.arenaHud &&
        (this.arenaHud.status === 1 || this.arenaHud.status === 2) &&
        reason !== "enter-scene"
      ) {
        return;
      }
      this.arenaHud = {
        status: st.status,
        round: st.round || 0,
        roundRemain: 0,
        eventRemain: 0,
        score: this.arenaHud?.score ?? 0,
        job: this.arenaHud?.job ?? 0,
        bracket: this.arenaHud?.bracket ?? 0,
        playoff: this.arenaHud?.playoff ?? 0,
      };
      this.cb.onArenaHud?.(this.arenaHud);
      console.info(
        `[session] arena_hud_fallback reason=${reason} status=${st.status} round=${st.round}`,
      );
    } catch (e) {
      console.warn("[session] arena_hud_fallback failed", reason, e);
    }
  }

  async fetchArenaStatus(): Promise<ArenaStatus | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildArenaStatusRequest());
    const parsed = parseArenaFromBodies(bodies);
    this.handleBodies(bodies);
    if (parsed.status) this.arenaStatus = parsed.status;
    return this.arenaStatus;
  }

  async fetchArenaRank(
    job?: number,
    bracket?: number,
    pageIndex = 0,
  ): Promise<ArenaRankPage | null> {
    if (!this.transport) return null;
    const j = job && job > 0 ? job : this.roles.find((r) => r.roleId === this.selectedRoleId)?.jobIndex != null
      ? (this.roles.find((r) => r.roleId === this.selectedRoleId)!.jobIndex + 1)
      : 1;
    const b =
      bracket && bracket > 0 ? bracket : bracketForLevel(this.level || 1);
    const bodies = await this.transport.exchange(
      0,
      buildArenaRankRequest(j, b, pageIndex),
    );
    const parsed = parseArenaFromBodies(bodies);
    this.handleBodies(bodies);
    if (parsed.rank) this.arenaRankPage = parsed.rank;
    return this.arenaRankPage;
  }

  async claimArenaReward(): Promise<{
    result: number;
    rank: number;
    claimed: boolean;
    tip: string;
  } | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildArenaClaimRequest());
    const parsed = parseArenaFromBodies(bodies);
    this.handleBodies(bodies);
    return parsed.claim ?? null;
  }

  async leaveArena(): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildArenaLeaveRequest());
    this.handleBodies(bodies);
    this.clearArenaHud("leave-arena");
  }

  async fetchLadderStatus(): Promise<LadderStatus | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildLadderStatusRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const st = parseLadderStatus(decodeWtPacket(body));
        if (st) return st;
      } catch {
        /* skip */
      }
    }
    return null;
  }

  async startLadderChallenge(): Promise<{ ok: boolean; tip: string }> {
    if (!this.transport) return { ok: false, tip: "未连接" };
    const bodies = await this.transport.exchange(0, buildLadderStartRequest());
    this.handleBodies(bodies);
    if (this.inBattle) return { ok: true, tip: "" };
    for (const body of bodies) {
      try {
        const r = parseLadderStartResult(decodeWtPacket(body));
        if (r.tip) return { ok: false, tip: r.tip.replace(/\0+$/g, "") };
        if (!r.ok) return { ok: false, tip: "挑战失败" };
      } catch {
        /* skip */
      }
    }
    return { ok: false, tip: "挑战失败" };
  }

  async fetchGuessStatus(): Promise<GuessStatus | null> {
    if (!this.transport) return null;
    const statusBodies = await this.transport.exchange(
      0,
      buildGuessStatusRequest(),
    );
    this.handleBodies(statusBodies);
    for (const body of statusBodies) {
      try {
        const st = parseGuessStatus(decodeWtPacket(body));
        if (st) return st;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fetchGuessBoard(pageIndex = 0): Promise<{
    status: GuessStatus | null;
    rows: GuessHistoryRow[];
    pageIndex: number;
    hasMore: boolean;
  }> {
    if (!this.transport) {
      return { status: null, rows: [], pageIndex: 0, hasMore: false };
    }
    const statusBodies = await this.transport.exchange(
      0,
      buildGuessStatusRequest(),
    );
    this.handleBodies(statusBodies);
    const historyBodies = await this.transport.exchange(
      0,
      buildGuessHistoryRequest(pageIndex),
    );
    this.handleBodies(historyBodies);
    let status: GuessStatus | null = null;
    let rows: GuessHistoryRow[] = [];
    let hasMore = false;
    for (const body of statusBodies) {
      try {
        const st = parseGuessStatus(decodeWtPacket(body));
        if (st) status = st;
      } catch {
        /* ignore */
      }
    }
    for (const body of historyBodies) {
      try {
        const hist = parseGuessHistory(decodeWtPacket(body));
        if (hist) {
          rows = hist.rows;
          hasMore = hist.hasMore;
          pageIndex = hist.pageIndex;
          if (status) {
            status = { ...status, pendingClaims: hist.pendingClaims };
          }
        }
      } catch {
        /* ignore */
      }
    }
    return { status, rows, pageIndex, hasMore };
  }

  async guessBet(choice: number, stakeGold: number): Promise<GuessOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildGuessBetRequest(choice, stakeGold),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseGuessOpResult(decodeWtPacket(body), 2);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async guessClaim(betId = 0): Promise<GuessOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildGuessClaimRequest(betId),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseGuessOpResult(decodeWtPacket(body), 4);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fetchFruitStatus(): Promise<FruitStatus | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildFruitStatusRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const st = parseFruitStatus(decodeWtPacket(body));
        if (st) return st;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fruitInsert(gold: number): Promise<FruitOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildFruitInsertRequest(gold),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseFruitOpResult(decodeWtPacket(body), 2);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fruitSpin(
    multiple: number,
    bets: Record<number, number>,
  ): Promise<FruitSpinResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildFruitSpinRequest(multiple, bets),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseFruitSpinResult(decodeWtPacket(body));
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fruitCollect(): Promise<FruitOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildFruitCollectRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseFruitOpResult(decodeWtPacket(body), 4);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fruitCompare(guess: number): Promise<FruitOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildFruitCompareRequest(guess),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseFruitOpResult(decodeWtPacket(body), 5);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fruitCashOut(gold = 0): Promise<FruitOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildFruitCashOutRequest(gold),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseFruitOpResult(decodeWtPacket(body), 6);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fetchActivitiesStatus(): Promise<ActivitiesStatus | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildActivitiesStatusRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const st = parseActivitiesStatus(decodeWtPacket(body));
        if (st) return st;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async festivalExchange(exchangeId: number): Promise<ActivityOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildFestivalExchangeRequest(exchangeId),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseActivityOp(decodeWtPacket(body), 46, 2);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fetchYanmenExchangeList(): Promise<YanmenExchangeRow[]> {
    if (!this.transport) return [];
    const bodies = await this.transport.exchange(0, buildYanmenListRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const rows = parseYanmenList(decodeWtPacket(body));
        if (rows.length) return rows;
      } catch {
        /* ignore */
      }
    }
    return [];
  }

  async yanmenExchange(exchangeId: number): Promise<ActivityOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildYanmenExchangeRequest(exchangeId),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseActivityOp(decodeWtPacket(body), 46, 4);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fetchRingStatus(): Promise<RingStatus | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildRingStatusRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const st = parseRingStatus(decodeWtPacket(body));
        if (st) return st;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async ringAccept(): Promise<RingStatus | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildRingAcceptRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const st = parseRingStatus(decodeWtPacket(body));
        if (st) return st;
      } catch {
        /* ignore */
      }
    }
    return this.fetchRingStatus();
  }

  async ringSubmit(): Promise<ActivityOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildRingSubmitRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseActivityOp(decodeWtPacket(body), 47, 3);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async ringClaimDaily(): Promise<ActivityOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildRingClaimDailyRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseActivityOp(decodeWtPacket(body), 47, 4);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  /** Spend coolmoney to reroll the active ring quest (WT47/5). */
  async ringRefresh(): Promise<ActivityOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildRingRefreshRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseActivityOp(decodeWtPacket(body), 47, 5);
        if (r) {
          if (r.result === 1 && this.mallState) {
            this.mallState = {
              ...this.mallState,
              coolmoney: Math.max(
                0,
                (this.mallState.coolmoney ?? 0) - RING_REFRESH_WCOIN,
              ),
            };
          }
          return r;
        }
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  /**
   * After NPC arena enter (30/1): same post-enter catalog settle as instance
   * enter so sceneVisibleReady promotes and scene-sync can push 2/10 peers
   * without requiring the player to open「周围玩家」first.
   */
  async settleArenaSceneEnter(): Promise<void> {
    if (!this.transport) return;
    await this.awaitEnterScene();
    const landed = this.scene || "";
    if (!this.isArenaScene(landed)) return;
    const catalogBodies = await this.transport.exchange(
      0,
      buildSceneChangePostEnterFollowupRequest(landed, 0, 2),
    );
    this.handleBodies(catalogBodies);
    await this.awaitEnterScene();
    if (!this.inBattle) await this.pollOnce();
    void this.ensureArenaHudFallback("enter-scene");
  }

  /** Sync map peer set from a 2/7 page (and drop arena ghosts not on the page). */
  syncMapPeersFromNearbyPage(peers: PeerActor[]): {
    peers: PeerActor[];
    removed: number[];
  } {
    const keep = new Set(peers.map((p) => p.actorId >>> 0));
    const removed: number[] = [];
    if (this.isArenaScene()) {
      for (const id of this.scenePeerById.keys()) {
        if (!keep.has(id >>> 0)) removed.push(id >>> 0);
      }
    }
    for (const id of removed) this.scenePeerById.delete(id >>> 0);
    for (const p of peers) this.scenePeerById.set(p.actorId >>> 0, p);
    return { peers, removed };
  }

  /** Cached 31/5 notices for map peers (kept across brief 2/6 remove/re-add). */
  peerCosmeticsSnapshot(): CosmeticsPeer[] {
    return [...this.scenePeerCosmeticsById.values()];
  }

  async fetchWagerBoard(): Promise<{
    rows: WagerListRow[];
    mine: WagerMine | null;
    listResult: number;
    listMessage: string;
  }> {
    if (!this.transport) {
      return { rows: [], mine: null, listResult: 0, listMessage: "no-transport" };
    }
    const listBodies = await this.transport.exchange(0, buildWagerListRequest());
    this.handleBodies(listBodies);
    const mineBodies = await this.transport.exchange(0, buildWagerMineRequest());
    this.handleBodies(mineBodies);
    let rows: WagerListRow[] = [];
    let mine: WagerMine | null = null;
    let listResult = 0;
    let listMessage = "";
    for (const body of listBodies) {
      try {
        const list = parseWagerList(decodeWtPacket(body));
        if (list) {
          rows = list.rows;
          listResult = list.result;
          listMessage = list.message || "";
        }
      } catch {
        /* ignore */
      }
    }
    for (const body of mineBodies) {
      try {
        const m = parseWagerMine(decodeWtPacket(body));
        if (m) mine = m;
      } catch {
        /* ignore */
      }
    }
    return { rows, mine, listResult, listMessage };
  }

  async wagerCreate(stake: number): Promise<WagerResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildWagerCreateRequest(stake));
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseWagerOpResult(decodeWtPacket(body), 11);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async wagerCancel(challengeId: number): Promise<WagerResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildWagerCancelRequest(challengeId));
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseWagerOpResult(decodeWtPacket(body), 12);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async wagerChallenge(challengeId: number): Promise<WagerResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildWagerChallengeRequest(challengeId),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseWagerOpResult(decodeWtPacket(body), 13);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async wagerConfirm(
    challengeId: number,
    accept: boolean,
  ): Promise<WagerResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildWagerConfirmRequest(challengeId, accept),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseWagerOpResult(decodeWtPacket(body), 14);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async fetchAuctionBoard(opts?: {
    browsePage?: number;
    minePage?: number;
    claimPage?: number;
  }): Promise<{
    rows: AuctionListing[];
    mine: AuctionListing[];
    claims: AuctionClaimRow[];
    status: AuctionStatus | null;
    browsePage: number;
    minePage: number;
    claimPage: number;
    browseHasMore: boolean;
    mineHasMore: boolean;
    claimHasMore: boolean;
  }> {
    const browsePage = Math.max(0, opts?.browsePage ?? 0);
    const minePage = Math.max(0, opts?.minePage ?? 0);
    const claimPage = Math.max(0, opts?.claimPage ?? 0);
    if (!this.transport) {
      return {
        rows: [],
        mine: [],
        claims: [],
        status: null,
        browsePage,
        minePage,
        claimPage,
        browseHasMore: false,
        mineHasMore: false,
        claimHasMore: false,
      };
    }
    const statusBodies = await this.transport.exchange(
      0,
      buildAuctionStatusRequest(),
    );
    this.handleBodies(statusBodies);
    const listBodies = await this.transport.exchange(
      0,
      buildAuctionListRequest(browsePage),
    );
    this.handleBodies(listBodies);
    const mineBodies = await this.transport.exchange(
      0,
      buildAuctionMineRequest(minePage),
    );
    this.handleBodies(mineBodies);
    const claimBodies = await this.transport.exchange(
      0,
      buildAuctionClaimsListRequest(claimPage),
    );
    this.handleBodies(claimBodies);
    let rows: AuctionListing[] = [];
    let mine: AuctionListing[] = [];
    let claims: AuctionClaimRow[] = [];
    let status: AuctionStatus | null = null;
    let browseHasMore = false;
    let mineHasMore = false;
    let claimHasMore = false;
    let browsePageOut = browsePage;
    let minePageOut = minePage;
    let claimPageOut = claimPage;
    for (const body of statusBodies) {
      try {
        const st = parseAuctionStatus(decodeWtPacket(body));
        if (st) status = st;
      } catch {
        /* ignore */
      }
    }
    for (const body of listBodies) {
      try {
        const list = parseAuctionList(decodeWtPacket(body), 2);
        if (list) {
          rows = list.rows;
          browseHasMore = list.hasMore;
          browsePageOut = list.pageIndex;
        }
      } catch {
        /* ignore */
      }
    }
    for (const body of mineBodies) {
      try {
        const list = parseAuctionList(decodeWtPacket(body), 3);
        if (list) {
          mine = list.rows;
          mineHasMore = list.hasMore;
          minePageOut = list.pageIndex;
        }
      } catch {
        /* ignore */
      }
    }
    for (const body of claimBodies) {
      try {
        const list = parseAuctionClaimsList(decodeWtPacket(body));
        if (list) {
          claims = list.rows;
          claimHasMore = list.hasMore;
          claimPageOut = list.pageIndex;
          if (status) {
            status = { ...status, pendingClaims: list.pendingClaims };
          } else {
            status = {
              enabled: true,
              blink: list.pendingClaims > 0,
              pendingClaims: list.pendingClaims,
              feePercents: [2, 4, 6, 8],
              minBidStep: 1,
              currencyMask: 3,
            };
          }
        }
      } catch {
        /* ignore */
      }
    }
    return {
      rows,
      mine,
      claims,
      status,
      browsePage: browsePageOut,
      minePage: minePageOut,
      claimPage: claimPageOut,
      browseHasMore,
      mineHasMore,
      claimHasMore,
    };
  }

  async auctionCreate(opts: {
    bagSeq: number;
    count: number;
    startPrice: number;
    buyoutPrice: number;
    currency: number;
    durationTier: number;
  }): Promise<AuctionOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildAuctionCreateRequest(opts),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseAuctionOpResult(decodeWtPacket(body), 4);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async auctionBid(
    listingId: number,
    amount: number,
  ): Promise<AuctionOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildAuctionBidRequest(listingId, amount),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseAuctionOpResult(decodeWtPacket(body), 5);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async auctionBuyout(
    listingId: number,
    captcha: string,
  ): Promise<AuctionOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildAuctionBuyoutRequest(listingId, captcha),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseAuctionOpResult(decodeWtPacket(body), 9);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async auctionBuyoutChallenge(
    listingId: number,
  ): Promise<AuctionBuyoutChallengeResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildAuctionBuyoutChallengeRequest(listingId),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseAuctionBuyoutChallenge(decodeWtPacket(body));
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async auctionCancel(listingId: number): Promise<AuctionOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildAuctionCancelRequest(listingId),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseAuctionOpResult(decodeWtPacket(body), 6);
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  async auctionClaim(): Promise<AuctionClaimResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildAuctionClaimRequest());
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const r = parseAuctionClaim(decodeWtPacket(body));
        if (r) return r;
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  /** Quote then confirm quick repair (7/29 → 7/13). */
  async requestQuickRepairQuote(): Promise<{
    type: number;
    repairnum: number;
    coolmoney: number;
  } | null> {
    if (!this.transport) return null;
    const quoteBodies = await this.transport.exchange(
      0,
      buildQuickRepairQuoteRequest(1),
    );
    let quote = null as ReturnType<typeof parseQuickRepairQuote>;
    for (const body of quoteBodies) {
      const packet = ingestPacket(body);
      if (!packet) continue;
      const q = parseQuickRepairQuote(packet);
      if (q) quote = q;
    }
    this.handleBodies(quoteBodies);
    return quote;
  }

  async confirmQuickRepair(): Promise<{ ok: boolean; message: string }> {
    if (!this.transport) return { ok: false, message: "未连接" };
    const confirmBodies = await this.transport.exchange(
      0,
      buildQuickRepairConfirmRequest(),
    );
    let result = -1;
    for (const body of confirmBodies) {
      const packet = ingestPacket(body);
      if (!packet) continue;
      const c = parseQuickRepairConfirm(packet);
      if (c) result = c.result;
    }
    this.handleBodies(confirmBodies);
    return {
      ok: result === 1,
      message:
        result === 1
          ? "修理完成"
          : result === 2
            ? "酷宝不足"
            : `修理失败 result=${result}`,
    };
  }

  async openNpc(actorId: number, type = 1): Promise<void> {
    if (!this.transport) return;
    if (type === 1) this.lastNpcId = actorId >>> 0;
    this.setPhase("npc", `26/1 type=${type} id=${actorId >>> 0}`);
    const bodies = await this.transport.exchange(
      0,
      buildNpcDialogRequest(actorId, type),
    );
    this.handleBodies(bodies);
  }

  /**
   * subtype-10 PvE left visual bytes are crash-safe player-portrait placeholders
   * (mock default 0/1 → Pixi used to paint 男幻剑). Resolve a real .actor from
   * the pending collide spawn / scene SCE / monster catalog / challenge guide.
   * Dream challenge: enemy id is catalog real (204), SCE wire is 200 — never
   * fall through to the guide's FB_* NPC portrait.
   */
  resolvePveEnemyActor(
    enemyId: number,
    enemyName?: string,
    opts?: { x?: number; y?: number },
  ): { actorResource: string; name?: string } | null {
    const id = enemyId >>> 0;
    const name = (enemyName || "")
      .replace(/\0+$/g, "")
      .replace(/#\d+$/, "")
      .trim();
    const fromMon = (n: NpcActor) =>
      n.battleActorResource || n.actorResource || "";
    /** Combat body — reject map scenery / dialog NPCs (FB_lotus, trees, …). */
    const isCombatMonsterActor = (res: string) => {
      const a = (res || "").trim();
      if (!/\.actor$/i.test(a)) return false;
      if (/^FB_/i.test(a) || /^_/i.test(a)) return false;
      return /^e_/i.test(a);
    };
    const pending = this.pendingEncounterVisual;
    // Collide/tap/F set this for the current 4/1; hangup leaves it null.
    // Prefer it when it matches the battle enemy id. If server remapped
    // (world boss etc.), use catalog actor for the remapped id instead of
    // the outdoor trash sprite that armed the collide.
    if (pending?.actorResource) {
      const pendingId = (pending.battleActorId ?? 0) >>> 0;
      if (!id || !pendingId || pendingId === id) {
        return {
          actorResource: pending.actorResource,
          name: pending.name || name,
        };
      }
    }
    const matchPos = (n: NpcActor) =>
      opts?.x != null &&
      opts?.y != null &&
      (n.x | 0) === (opts.x | 0) &&
      (n.y | 0) === (opts.y | 0);
    const monByPos = this.sceneMonsters.find(matchPos);
    if (monByPos) {
      const res = fromMon(monByPos);
      if (res) return { actorResource: res, name: monByPos.name || name };
    }
    const monById = this.sceneMonsters.find(
      (n) =>
        (n.battleActorId != null && (n.battleActorId >>> 0) === id) ||
        (n.actorId >>> 0) === id,
    );
    if (monById) {
      const res = fromMon(monById);
      if (res) return { actorResource: res, name: monById.name || name };
    }
    // Admin catalog actor for remapped real id (dream 200→204 → e_fireG).
    const catalogActor = monsterActorForEnemyId(id);
    if (catalogActor) {
      return { actorResource: catalogActor, name: name || undefined };
    }
    // Challenge/hangup: SCE wire ≠ catalog id — use a scene combat .actor
    // (prefer e_fire* battle field over e_ghostfire* map blob).
    if (this.pendingChallengeGuideActorId || this.hangupDesired) {
      const preferBattle = this.sceneMonsters.find((n) => {
        const battle = (n.battleActorResource || "").trim();
        return (
          isCombatMonsterActor(battle) && !/^e_ghostfire/i.test(battle)
        );
      });
      const anyCombat =
        preferBattle ||
        this.sceneMonsters.find((n) => isCombatMonsterActor(fromMon(n)));
      if (anyCombat) {
        const res = fromMon(anyCombat);
        if (res)
          return { actorResource: res, name: name || anyCombat.name };
      }
    }
    const npcById = this.npcs.find((n) => (n.actorId >>> 0) === id);
    if (npcById?.actorResource && isCombatMonsterActor(npcById.actorResource)) {
      return {
        actorResource: npcById.actorResource,
        name: npcById.name || name,
      };
    }
    if (name) {
      const monByName = this.sceneMonsters.find(
        (n) => (n.name || "").replace(/\0+$/g, "").trim() === name,
      );
      if (monByName) {
        const res = fromMon(monByName);
        if (res) return { actorResource: res, name: monByName.name || name };
      }
      const npcByName = this.npcs.find(
        (n) => (n.name || "").replace(/\0+$/g, "").trim() === name,
      );
      if (
        npcByName?.actorResource &&
        isCombatMonsterActor(npcByName.actorResource)
      ) {
        return {
          actorResource: npcByName.actorResource,
          name: npcByName.name || name,
        };
      }
    }
    const guideId =
      this.pendingChallengeGuideActorId || this.lastNpcId || 0;
    if (guideId) {
      const guide = this.npcs.find((n) => (n.actorId >>> 0) === guideId);
      // Outdoor monkey challenge: guide is e_monkey — keep.
      // Dream instance guide: FB_lotus* — must not paint as the enemy.
      if (
        guide?.actorResource &&
        isCombatMonsterActor(guide.actorResource)
      ) {
        return {
          actorResource: guide.actorResource,
          name: name || guide.name,
        };
      }
    }
    return null;
  }

  /**
   * Dispatch a dialog option by CBE action byte.
   * action=4 → task detail then accept/submit; action=1 → service 26/1 type=2.
   * Server may inject leave as action=1 value=0 (empty gated menus) — close only.
   */
  async pickNpcOption(
    action: number,
    value: number,
    label = "",
  ): Promise<void> {
    if (!this.transport) return;
    if (action === 4) {
      const submit = /提交|完成|交还/.test(label);
      await this.openTaskDetail(value, submit ? "submit" : "accept");
      return;
    }
    if (action === 1) {
      /*
       * Leave / close stub from mock_npc_dialog_empty_options_leave:
       * action=1 value=0.  Must not send 26/1 type=2 id=0 — that hits the
       * unimplemented-handler tip (功能暂未实现(1/26/1)).
       */
      if (value === 0 || /离开|关闭/.test(label)) {
        this.setPhase("inworld", "npc-dialog-leave");
        return;
      }
      const opcode = npcServiceOpcode(value);
      this.setPhase(
        "npc-service",
        `26/1 type=2 op=0x${opcode.toString(16)} value=${value >>> 0}`,
      );
      const sceneBefore = this.scene || "";
      const bodies = await this.transport.exchange(
        0,
        buildNpcDialogRequest(value, 2),
      );
      this.handleBodies(bodies);
      // EC challenge answers 26/0+30/9 — onChallengePrompt fires from handleBodies.
      if (opcode === NpcServiceOpcode.CHALLENGE) {
        // value = 0xEC000000 | guideActorId (mock instance-guide dialog).
        this.pendingChallengeGuideActorId = value & 0xffffff;
        if (!this.pendingChallengeGuideActorId && this.lastNpcId) {
          this.pendingChallengeGuideActorId = this.lastNpcId;
        }
        return;
      }
      /*
       * EB instance enter/exit answers with 30/1.  Portals pull nonempty 27/11
       * via post-enter 25/5+2/3+27/11+7/42; without that, cross-scene clear of
       * session.npcs leaves the outdoor (or dream) map empty of catalog NPCs.
       * Also poll once so dream-exit outdoor map_stone_loading_clear can drip.
       */
      if (opcode === NpcServiceOpcode.ENTER_INSTANCE) {
        await this.awaitEnterScene();
        const landed = ensureSceneFile(this.scene || "");
        if (
          landed &&
          (!sceneBefore || !sceneKeysMatch(sceneBefore, landed))
        ) {
          const catalogBodies = await this.transport.exchange(
            0,
            buildSceneChangePostEnterFollowupRequest(landed, 0, 2),
          );
          this.handleBodies(catalogBodies);
          await this.awaitEnterScene();
        }
        if (!this.inBattle) {
          await this.pollOnce();
        }
        return;
      }
      return;
    }
    // Unknown action: treat value as service id when nonzero.
    if (value) await this.openNpc(value, 2);
  }

  /**
   * Client-side batch sell via existing equip-sell 26/1 SELL_BASE.
   * Sells backpack equips with enhance==0; optional quality filter.
   * Suppresses onNpcDialog while looping so the UI does not flicker.
   */
  equipSellBatchSilent = false;

  async sellUnenhancedBagEquips(opts?: {
    quality?: number;
  }): Promise<{
    sold: number;
    skipped: number;
    message: string;
  }> {
    if (!this.transport) {
      return { sold: 0, skipped: 0, message: "未连接服务器" };
    }
    const qualityFilter =
      opts?.quality !== undefined ? opts.quality | 0 : undefined;
    const qLabel =
      qualityFilter !== undefined
        ? equipSellQualityLabel(qualityFilter)
        : "装备";
    await this.openBag();
    const targets = this.bagItems.filter(
      (it) =>
        it.kind === "equip" &&
        (it.enhance | 0) === 0 &&
        it.seq > 0 &&
        !(
          it.socketGems &&
          (it.socketGems.reflect || it.socketGems.combo || it.socketGems.revive)
        ) &&
        (qualityFilter === undefined ||
          matchEquipSellQuality(it.quality, qualityFilter)),
    );
    if (!targets.length) {
      // Keep sell voucher UI open; do not drop to world.
      await this.pickNpcOption(1, EquipSellService.OPEN, "出售装备");
      return {
        sold: 0,
        skipped: 0,
        message:
          qualityFilter !== undefined
            ? `背包没有可出售的未强化${qLabel}`
            : "背包没有可出售的未强化装备",
      };
    }
    this.equipSellBatchSilent = true;
    let sold = 0;
    let skipped = 0;
    try {
      for (const it of targets) {
        const still = this.bagItems.some(
          (b) => b.seq === it.seq && b.kind === "equip",
        );
        if (!still) {
          skipped += 1;
          continue;
        }
        const value = (EquipSellService.SELL_BASE | (it.seq & 0xffff)) >>> 0;
        await this.pickNpcOption(1, value, "出售");
        // Optimistic local remove so later seq checks stay accurate if peel lags.
        if (this.bagItems.some((b) => b.seq === it.seq)) {
          this.bagItems = this.bagItems.filter((b) => b.seq !== it.seq);
          this.cb.onBag?.(this.bagItems);
        }
        sold += 1;
      }
    } finally {
      this.equipSellBatchSilent = false;
    }
    await this.openBag();
    // Re-open sell root while session is still armed.
    await this.pickNpcOption(1, EquipSellService.OPEN, "出售装备");
    const message =
      skipped > 0
        ? `已出售 ${sold} 件未强化${qLabel}（跳过 ${skipped}）`
        : `已出售 ${sold} 件未强化${qLabel}`;
    return { sold, skipped, message };
  }

  async sellAllUnenhancedBagEquips(): Promise<{
    sold: number;
    skipped: number;
    message: string;
  }> {
    return this.sellUnenhancedBagEquips();
  }

  async openTaskDetail(
    taskId: number,
    mode: "accept" | "submit" = "accept",
  ): Promise<void> {
    if (!this.transport) return;
    this.pendingTaskId = taskId;
    this.pendingTaskMode = mode;
    const bodies = await this.transport.exchange(
      0,
      buildTaskDetailRequest(taskId, mode === "submit" ? 2 : 0),
    );
    this.handleBodies(bodies);
    // If 6/10 missing (demo), still prompt with label.
    if (this.pendingTaskId === taskId && this.cb.onTaskPrompt) {
      /* handleBodies may already have shown prompt via parseTaskDetail */
    }
  }

  async confirmTaskPrompt(accept: boolean): Promise<void> {
    if (!this.transport) return;
    const taskId = this.pendingTaskId;
    const mode = this.pendingTaskMode;
    this.pendingTaskId = 0;
    if (!accept || !taskId) {
      this.setPhase("inworld", "task cancelled");
      return;
    }
    if (mode === "submit") {
      await this.submitTask(taskId);
    } else {
      await this.acceptTask(taskId);
    }
  }

  async confirmChallenge(agree: boolean): Promise<void> {
    if (!this.transport) return;
    const enemyId = this.pendingChallengeEnemyId >>> 0;
    if (!agree) {
      this.pendingChallengeGuideActorId = 0;
      this.pendingChallengeEnemyId = 0;
      this.pendingEncounterVisual = null;
      // Clear server 30/9 pending without arming subtype-10 followup.
      try {
        const bodies = await this.transport.exchange(
          0,
          buildChallengeConfirmRequest(false),
        );
        this.handleBodies(bodies);
      } catch {
        /* local cancel still wins */
      }
      this.setPhase("inworld", "challenge cancelled");
      return;
    }
    this.pendingChallengeEnemyId = 0;
    this.pendingChallengeGuideActorId = 0;
    // Hangup-style: clear 30/9 pending, then 4/1 with challenge_enemy_id.
    try {
      const clearBodies = await this.transport.exchange(
        0,
        buildChallengeConfirmRequest(false),
      );
      this.handleBodies(clearBodies);
    } catch {
      /* still start hangup-style battle */
    }
    this.setPhase("challenge", `4/1 enemy=${enemyId || "nearest"}`);
    await this.startBattleNearPlayer(
      this.x | 0,
      this.y | 0,
      enemyId || undefined,
    );
  }

  /**
   * Send chat. World (type=0) requires bag item 807 小喇叭 — tip and skip if missing.
   * Wire: 1/3/1 + 7/1 (seq/id/num). CBE also flushes 2/10 companions; Pixi omits
   * those scene refresh objects. Evidence: docs/re/2026-07-20-small-horn-world-chat.md.
   * Private (type=7) uses 1/3/2 { sendTo, data } via opts.sendTo.
   */
  async sendChat(
    text: string,
    type = 0,
    opts?: { sendTo?: number },
  ): Promise<{ ok: boolean; tip?: string }> {
    if (!this.transport || !text.trim()) return { ok: false };
    const body = text.trim();

    if (type === 7 || (opts?.sendTo ?? 0) > 0) {
      const sendTo = (opts?.sendTo ?? 0) >>> 0;
      if (!sendTo) return { ok: false, tip: "请选择私聊对象" };
      return this.sendPrivateChat(sendTo, body);
    }

    if (type === 0) {
      let horn = this.bagItems.find(
        (i) => i.itemId === SMALL_HORN_ITEM_ID && i.count >= 1,
      );
      if (!horn) {
        await this.openBag();
        horn = this.bagItems.find(
          (i) => i.itemId === SMALL_HORN_ITEM_ID && i.count >= 1,
        );
      }
      if (!horn) {
        return { ok: false, tip: "没有小喇叭，无法发送世界消息" };
      }
      const wire = clampWorldChatText(body);
      if (!wire) return { ok: false, tip: "消息内容无效" };
      const bodies = await this.transport.exchange(
        0,
        buildWorldChatHornRequest(wire, horn.seq > 0 ? horn.seq : 0),
      );
      this.handleBodies(bodies);
      for (const bodyBytes of bodies) {
        const packet = ingestPacket(bodyBytes);
        if (!packet) continue;
        for (const line of parseChatNotices(packet)) {
          if (line.text.includes("世界消息发送失败")) {
            return { ok: false, tip: "世界消息发送失败" };
          }
        }
      }
      return { ok: true };
    }

    const bodies = await this.transport.exchange(0, buildChatRequest(body, type));
    this.handleBodies(bodies);
    // Non-world: server empty ACK — client inserts the line.
    const prefix =
      type === 2
        ? "队"
        : type === 3
          ? "帮"
          : type === 4
            ? "本"
            : "?";
    const name = this.roleName || "我";
    this.cb.onChat?.([
      {
        type,
        text: `[${prefix}]${name}: ${body}`,
        senderRoleId: this.selectedRoleId >>> 0,
        senderName: name,
      },
    ]);
    return { ok: true };
  }

  /**
   * Private whisper: 1/3/2 { sendTo, data }.
   * Online → empty ACK + local type-7 echo; offline → type-5 tip in response.
   */
  async sendPrivateChat(
    sendTo: number,
    text: string,
  ): Promise<{ ok: boolean; tip?: string }> {
    if (!this.transport || !text.trim() || !(sendTo >>> 0)) {
      return { ok: false, tip: "请选择私聊对象" };
    }
    const wire = clampWorldChatText(text.trim());
    if (!wire) return { ok: false, tip: "消息内容无效" };
    const bodies = await this.transport.exchange(
      0,
      buildPrivateChatRequest(sendTo >>> 0, wire),
    );
    this.handleBodies(bodies);
    let systemTip: string | undefined;
    for (const bodyBytes of bodies) {
      const packet = ingestPacket(bodyBytes);
      if (!packet) continue;
      for (const line of parseChatNotices(packet)) {
        if (line.type === 5 || line.type === 6) {
          systemTip =
            line.text
              .replace(/^\[系\]/, "")
              .replace(/^系统:\s*/, "")
              .trim() || "对方不在线";
        }
      }
    }
    if (systemTip) return { ok: false, tip: systemTip };
    const name = this.roleName || "我";
    this.cb.onChat?.([
      {
        type: 7,
        text: `[私]${name}: ${wire}`,
        senderRoleId: this.selectedRoleId >>> 0,
        senderName: name,
      },
    ]);
    return { ok: true };
  }

  async openBag(): Promise<void> {
    if (!this.transport) return;
    try {
      await this.prepareLiveExchange("bag");
      await warmItemCatalogs();
      // Keep prior rows for seq/durability merge inside handleBodies (30/21 does
      // not carry equip current durability; CBE keeps item+0x110 across reopen).
      // Do not onBag([]) while awaiting — that clears battle「物品」mid-fight.
      const retained = this.bagItems;
      const bodies = await this.transport.exchange(0, buildBagOpenRequest());
      this.bagItems = retained;
      this.handleBodies(bodies);
      this.refreshCatalogLabelsOnOwnedItems();
    } catch (e) {
      this.notifyPanelFetchFailed("背包", e);
    }
  }

  /** After custom-catalog merge, rewrite `#id` bag/worn placeholders to real names. */
  refreshOwnedItemCatalogLabels(): void {
    this.refreshCatalogLabelsOnOwnedItems();
  }

  private refreshCatalogLabelsOnOwnedItems(): void {
    let bagChanged = false;
    let wornChanged = false;
    const nextBag = this.bagItems.map((it) => {
      const n = overlayBagItemFromCatalog(it);
      if (n !== it) bagChanged = true;
      return n;
    });
    const nextWorn = this.wornItems.map((it) => {
      const n = overlayBagItemFromCatalog(it);
      if (n !== it) wornChanged = true;
      return n;
    });
    if (bagChanged) {
      this.bagItems = nextBag;
      this.cb.onBag?.(this.bagItems);
    }
    if (wornChanged) {
      this.wornItems = nextWorn;
      this.cb.onWorn?.(this.wornItems);
    }
  }

  /** Merge bag-equip durability + washed attrs after unequip/swap/wire refresh. */
  private applyBagEquipDurability(it: BagItem, prev?: BagItem): BagItem {
    if (it.kind !== "equip") return it;
    let next = it;
    const pending = this.pendingBagEquipDurability.get(it.seq);
    if (pending != null) {
      this.pendingBagEquipDurability.delete(it.seq);
      next = { ...next, count: 1, durability: pending >>> 0 };
    } else if (prev?.kind === "equip" && prev.durability != null) {
      next = { ...next, count: 1, durability: prev.durability >>> 0 };
    } else if (next.durability != null) {
      next = { ...next, count: 1 };
    } else {
      next = { ...next, count: 1, durability: 1 };
    }
    // Wire enhance is authoritative (0 after 29/6 recast). Never `next || prev`
    // — that kept the pre-transfer (+L) until re-login.
    const wireEnhance = next.enhance >>> 0;
    const pendingAttrs = this.pendingBagEquipEnhance.get(it.seq);
    if (pendingAttrs?.length) {
      this.pendingBagEquipEnhance.delete(it.seq);
      if (wireEnhance > 0) {
        const attrs = preferEnhanceAttrs(next.enhanceAttrs, pendingAttrs);
        next = {
          ...next,
          enhance: wireEnhance,
          ...(attrs && attrs.length ? { enhanceAttrs: attrs } : {}),
        };
      } else {
        // +0: wire is authority (defaults or first-obtain seed). Pending only
        // fills when wire omitted attrs (unequip race). Never preferEnhanceAttrs
        // here — that would resurrect pre-recast wash onto enhance=0 wire.
        const attrs = next.enhanceAttrs?.length
          ? next.enhanceAttrs
          : pendingAttrs;
        next = {
          ...next,
          enhance: 0,
          ...(attrs.length ? { enhanceAttrs: attrs } : {}),
        };
      }
    } else if (prev?.kind === "equip" && wireEnhance > 0) {
      // Preserve wash attrs across reopen when wire truncates extras but
      // level still matches an enhanced equip. Recast (enhance=0) skips this.
      const attrs = preferEnhanceAttrs(next.enhanceAttrs, prev.enhanceAttrs);
      if (attrs && attrs.length) {
        next = {
          ...next,
          enhance: wireEnhance,
          enhanceAttrs: attrs,
        };
      } else {
        next = { ...next, enhance: wireEnhance };
      }
    } else if (wireEnhance === 0 && next.kind === "equip") {
      // Keep wire milestone rows at +0 (gray defaults / first-obtain seed).
      // Stripping them forced local yellow fallback until 强化+1 revealed seed.
      next = { ...next, enhance: 0 };
    }
    return next;
  }

  /** Pixi 装备信息：每次打开拉 1/7/22 → 7/7 type=2 当前穿戴. */
  async openWornEquipment(): Promise<void> {
    if (!this.transport) return;
    try {
      await this.prepareLiveExchange("worn");
      await warmItemCatalogs();
      /* Keep prior wornItems / onWorn until 7/7 type=2 arrives — clearing
       * here flashed empty equip UI and dropped +16 sparkles mid-refresh. */
      const bodies = await this.transport.exchange(0, buildWornEquipRequest());
      this.handleBodies(bodies);
    } catch (e) {
      this.notifyPanelFetchFailed("装备", e);
    }
  }

  /**
   * Use bag item via server.
   * - Chests → 1/7/15 (+ key)
   * - Official exp cards 809/810/811 → 1/7/30
   * - 战斗心得 828 → 1/25/6 (+ follow-up 1/25/7)
   * - 大力丸/神力丸 829/830 → 1/22/3
   * - Other consumables (incl. custom 845 / 打金心得 853) → 1/7/1
   * Always returns UI feedback (success text / reject tip).
   * On failure (`result!=1` / tip reject): do not apply inventory bodies —
   * CBE only client-deducts on result=1 (docs/re/2026-08-08-item-use-fail-no-deduct.md).
   */
  async useItem(itemId: number, seq: number): Promise<ItemUseFeedback> {
    if (!this.transport) {
      return { ok: false, message: "未连接服务器" };
    }

    if (isChestKeyItem(itemId)) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: "请对宝箱使用（需匹配钥匙）",
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }

    if (isChestBoxItem(itemId)) {
      return this.openChest(itemId, seq);
    }

    if (isOfficialExpCardItem(itemId)) {
      const bodies = await this.transport.exchange(
        0,
        buildExpCardUseRequest(seq),
      );
      let fb = this.feedbackFromBodies(bodies) ?? {
        ok: false,
        message: "经验卡无响应",
      };
      // CBE only client-deducts on result=1; never apply inventory on failure.
      if (fb.ok) {
        this.handleBodies(bodies);
        if (!fb.message || fb.message === "使用成功") {
          fb = { ...fb, message: expCardSuccessMessage(itemId) };
        }
      } else if (!fb.message) {
        fb = { ...fb, message: "使用失败" };
      }
      this.emitItemUseFeedback(fb);
      return fb;
    }

    if (isBattleInsightItem(itemId)) {
      return this.useBattleInsight(seq);
    }

    if (isCombatPillItem(itemId)) {
      return this.useCombatPill(itemId, seq);
    }

    if (isCultivationBookBlankItem(itemId)) {
      return this.useCultivationBookBlank(seq);
    }

    if (isCultivationBookItem(itemId)) {
      return this.useCultivationBook(seq);
    }

    const bodies = await this.transport.exchange(
      0,
      buildItemUseRequest(itemId, seq),
    );
    let fb = this.feedbackFromBodies(bodies) ?? {
      ok: false,
      message: "使用无响应",
    };
    // result!=1 / tip reject: keep local bag; do not merge 7/7·7/11 deltas.
    if (fb.ok) {
      this.handleBodies(bodies);
      this.applyItemVitalsLocally(itemId);
      // Custom 845 (and any 7/1 exp-card path): upgrade generic success copy.
      if (isExpCardItem(itemId) && fb.message === "使用成功") {
        fb = { ...fb, message: expCardSuccessMessage(itemId) };
      }
      if (isGoldScrollItem(itemId)) {
        let msg = renameGoldScrollDisplayText(fb.message || "");
        if (!msg || msg === "使用成功") msg = goldScrollSuccessMessage();
        fb = { ...fb, message: msg };
      }
    } else if (!fb.message) {
      fb = { ...fb, message: "使用失败" };
    }
    // 834/839 open a follow-up 26/1 (仓库 / 装备出售). Generic「使用成功」
    // lands on that panel and overlaps it.
    if (
      isDurableMallPassItemId(itemId) &&
      fb.ok &&
      (!fb.message || fb.message === "使用成功")
    ) {
      fb = { ...fb, message: "" };
    }
    this.emitItemUseFeedback(fb);
    return fb;
  }

  /**
   * 「修」status icon: WT 1/7/31 → 7/31 {expinfo} list of active timed buffs.
   * Evidence: JianghuOL.CBE SendSceneAction31 / HandleExpInfoResponse.
   */
  async requestBuffStatus(): Promise<string> {
    if (!this.transport) return "未连接服务器";
    const bodies = await this.transport.exchange(0, buildExpCardStatusRequest());
    this.handleBodies(bodies);
    this.battleInsightStatusKnown = true;
    const info = parseExpCardStatusInfo(bodies);
    if (!info.trim()) {
      this.battleInsightExpireAtMs = 0;
    }
    return info || "当前没有生效的增益物品";
  }

  /**
   * Dream HUD remain — same freeze/resume contract as exp card.
   * WT 1/29/23 → 29/23 { remaining } after server pause/resume.
   */
  async fetchDreamHudRemainingMs(): Promise<number | null> {
    if (!this.transport) return null;
    try {
      const bodies = await this.transport.exchange(
        0,
        buildDreamHudRemainQueryRequest(),
      );
      this.handleBodies(bodies);
      const ms = parseDreamHudRemainMsFromBodies(bodies);
      if (ms != null) {
        this.dreamHudRemainMs = ms;
        return ms;
      }
      return this.dreamHudRemainMs;
    } catch (e) {
      console.warn("[session] dream hud remain query", e);
      return this.dreamHudRemainMs;
    }
  }

  /**
   * Hangup settle: ensure 有效时间 is known without requiring map「修」tap.
   * Uses wall-clock cache after the first 7/31 fetch.
   */
  async ensureBattleInsightRemainSec(): Promise<number> {
    if (!this.battleInsightStatusKnown) {
      await this.requestBuffStatus();
    }
    return this.getBattleInsightRemainingSec();
  }

  /** Remaining seconds for 战斗心得 — matches「修」status tip. */
  getBattleInsightRemainingSec(): number {
    if (this.battleInsightExpireAtMs <= 0) return 0;
    return Math.max(
      0,
      Math.ceil((this.battleInsightExpireAtMs - Date.now()) / 1000),
    );
  }

  private noteBattleInsightFromStatus(info: string): void {
    const sec = parseBattleInsightRemainSecFromStatus(info);
    if (sec === null) return;
    this.battleInsightStatusKnown = true;
    if (sec <= 0) {
      this.battleInsightExpireAtMs = 0;
      return;
    }
    if (sec < 0) {
      if (this.battleInsightExpireAtMs <= Date.now()) {
        this.battleInsightExpireAtMs = Date.now() + 3600 * 1000;
      }
      return;
    }
    this.battleInsightExpireAtMs = Date.now() + sec * 1000;
  }

  private noteBattleInsightFromPacket(packet: WtPacket): void {
    const status = parseExpCardStatusInfoFromPacket(packet);
    if (status) {
      this.noteBattleInsightFromStatus(status);
      return;
    }
    const iteminfo = parseBattleInsightIteminfoFromPacket(packet);
    if (iteminfo) this.noteBattleInsightFromStatus(iteminfo);
  }

  private noteDreamHudRemainFromPacket(packet: WtPacket): void {
    const ms = parseDreamHudRemainMs(packet);
    if (ms == null) return;
    this.dreamHudRemainMs = ms;
    try {
      this.onDreamHudRemain?.(ms);
    } catch (e) {
      console.warn("[session] onDreamHudRemain", e);
    }
  }

  /**
   * Map heal (清风拂体): provisional 1/12/8 → 1/1/14 + tip.
   * skill.dsh 使用限制=1; only out of battle.
   */
  async useMapHealSkill(skillId: number): Promise<ItemUseFeedback> {
    if (!this.transport) {
      return { ok: false, message: "未连接服务器" };
    }
    if (this.inBattle) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: "请在战斗中用「回血施放」",
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }
    const hpBefore = this.hp;
    const bodies = await this.transport.exchange(
      0,
      buildMapHealSkillRequest(skillId),
    );
    this.handleBodies(bodies);
    let fb = this.feedbackFromBodies(bodies);
    if (!fb) {
      fb =
        this.hp > hpBefore
          ? { ok: true, message: "使用成功" }
          : { ok: false, message: "回血无响应" };
    } else if (this.hp > hpBefore) {
      fb = { ...fb, ok: true };
    }
    this.emitItemUseFeedback(fb);
    return fb;
  }

  private applyItemVitalsLocally(itemId: number): void {
    const cat = lookupItemSync(itemId);
    if (!cat) return;
    let changed = false;
    if (cat.hpChange > 0) {
      const next = Math.min(this.hpMax, this.hp + cat.hpChange);
      if (next !== this.hp) {
        this.hp = next;
        changed = true;
      }
    }
    if (cat.mpChange > 0) {
      const next = Math.min(this.mpMax, this.mp + cat.mpChange);
      if (next !== this.mp) {
        this.mp = next;
        changed = true;
      }
    }
    if (changed) this.emitVitals();
  }

  private emitVitals(): void {
    this.cb.onVitals?.(this.hp, this.hpMax, this.mp, this.mpMax);
  }

  /**
   * 829/830: WT 1/22/3 → 22/3 {result,info,ruffianflag}.
   * Server consumes the row; response has no 7/7 — mirror CBE local deduct.
   */
  private async useCombatPill(
    itemId: number,
    seq: number,
  ): Promise<ItemUseFeedback> {
    if (!this.transport) {
      return { ok: false, message: "未连接服务器" };
    }
    if (!(seq > 0)) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: "背包序号无效，请重新打开背包",
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }
    const bodies = await this.transport.exchange(
      0,
      buildCombatPillUseRequest(seq),
    );
    let fb = this.feedbackFromBodies(bodies) ?? {
      ok: false,
      message: "战力丹无响应",
    };
    if (fb.ok) {
      this.handleBodies(bodies);
      this.consumeBagSeqLocally(seq);
      if (!fb.message || fb.message === "使用成功") {
        fb = {
          ...fb,
          message:
            itemId === 830
              ? "神力丸效果已生效。"
              : "大力丸效果已生效。",
        };
      }
    } else if (!fb.message) {
      fb = { ...fb, message: "使用失败" };
    }
    this.emitItemUseFeedback(fb);
    return fb;
  }

  /**
   * Warehouse deposit: the success 26/1 is rebuilt from bagItems before the
   * later peel poll (equipment 17/1, stack 7/7). Drop the whole row now so
   * the deposit list does not keep showing an item already stored.
   */
  dropDepositedBagRow(seq: number): void {
    const s = seq & 0xffffff;
    if (!s) return;
    const idx = this.bagItems.findIndex((it) => (it.seq & 0xffffff) === s);
    if (idx < 0) return;
    this.pendingBagEquipDurability.delete(s);
    this.pendingBagEquipEnhance.delete(s);
    this.bagItems = this.bagItems.filter((_, i) => i !== idx);
    this.cb.onBag?.(this.bagItems);
  }

  /** CBE client-side row remove after dedicated use success (22/3 / similar). */
  private consumeBagSeqLocally(seq: number): void {
    const idx = this.bagItems.findIndex((it) => it.seq === seq);
    if (idx < 0) return;
    const cur = this.bagItems[idx]!;
    if (cur.count > 1) {
      this.bagItems = this.bagItems.map((it, i) =>
        i === idx ? { ...it, count: it.count - 1 } : it,
      );
    } else {
      this.bagItems = this.bagItems.filter((_, i) => i !== idx);
    }
    this.cb.onBag?.(this.bagItems);
  }

  /**
   * 920 空白修炼天书 → 1/7/35。result=0 本地扣空白；同包 7/7 加入 921。
   * Evidence: docs/re/2026-07-23-special-item-use-contract.md
   */
  async useCultivationBookBlank(seq: number): Promise<ItemUseFeedback> {
    if (!this.transport) {
      return { ok: false, message: "未连接服务器" };
    }
    const bodies = await this.transport.exchange(
      0,
      buildCultivationBookBindRequest(seq),
    );
    let fb: ItemUseFeedback | null = null;
    for (const body of bodies) {
      try {
        const packet = decodeWtPacket(body);
        fb = parseCultivationBookBindResult(packet) ?? fb;
      } catch {
        /* ignore */
      }
    }
    if (!fb) fb = { ok: false, message: "天书无响应" };
    if (fb.ok) {
      this.consumeBagSeqLocally(seq);
      this.handleBodies(bodies);
    }
    this.emitItemUseFeedback(fb);
    return fb;
  }

  /**
   * 921 读描述：1/7/38 → bookdes（背包详情明细）。
   */
  async readCultivationBookDes(seq: number): Promise<string> {
    if (!this.transport) return "";
    const bodies = await this.transport.exchange(
      0,
      buildCultivationBookReadRequest(seq),
    );
    let des = "";
    for (const body of bodies) {
      try {
        des = parseCultivationBookReadResult(decodeWtPacket(body)) || des;
      } catch {
        /* ignore */
      }
    }
    this.handleBodies(bodies);
    return des.trim();
  }

  /**
   * Apply 7/38 bookdes → bag row name「修炼天书 LvN·满/未满」.
   * Returns true when bag state changed.
   * Pass `notify: true` to push bag UI immediately (batch enrich leaves notify to caller).
   */
  applyCultivationBookMetaFromDes(
    seq: number,
    des: string,
    opts?: { notify?: boolean },
  ): boolean {
    const meta = parseCultivationBookMetaFromDes(des);
    if (!meta) return false;
    const idx = this.bagItems.findIndex((it) => it.seq === seq && seq > 0);
    if (idx < 0) return false;
    const cur = this.bagItems[idx]!;
    if (!isCultivationBookItem(cur.itemId)) return false;
    const base = lookupItemSync(cur.itemId)?.name ?? "修炼天书";
    const name = formatCultivationBookName(base, meta.level, meta.full);
    if (
      cur.bookLevel === meta.level &&
      cur.bookFull === meta.full &&
      cur.name === name &&
      (!des || cur.desc === des)
    ) {
      return false;
    }
    this.bagItems = this.bagItems.map((it, i) =>
      i === idx
        ? {
            ...it,
            bookLevel: meta.level,
            bookFull: meta.full,
            name,
            ...(des ? { desc: des } : {}),
          }
        : it,
    );
    if (opts?.notify) this.cb.onBag?.(this.bagItems);
    return true;
  }

  /** After bag refresh: async 7/38 for each 921 so list names show Lv/满未满. */
  private scheduleEnrichCultivationBookNames(opts?: {
    /** When true, re-query even if bookLevel already cached (full bag sync). */
    force?: boolean;
  }): void {
    if (!this.transport) return;
    const force = !!opts?.force;
    const targets = this.bagItems.filter(
      (it) =>
        isCultivationBookItem(it.itemId) &&
        it.seq > 0 &&
        (force || !(it.bookLevel && it.bookLevel > 0)),
    );
    if (!targets.length) return;
    const gen = ++this.cultivationBookEnrichGen;
    void (async () => {
      let changed = false;
      for (const it of targets) {
        if (gen !== this.cultivationBookEnrichGen) return;
        const des = await this.readCultivationBookDes(it.seq);
        if (gen !== this.cultivationBookEnrichGen) return;
        if (this.applyCultivationBookMetaFromDes(it.seq, des)) changed = true;
      }
      if (changed && gen === this.cultivationBookEnrichGen) {
        this.cb.onBag?.(this.bagItems);
      }
    })();
  }

  /**
   * 921：先 7/38 读描述，再 7/40 使用。result=0 本地删除并应用经验结算包。
   */
  async useCultivationBook(seq: number): Promise<ItemUseFeedback> {
    if (!this.transport) {
      return { ok: false, message: "未连接服务器" };
    }
    const des = await this.readCultivationBookDes(seq);

    const bodies = await this.transport.exchange(
      0,
      buildCultivationBookUseRequest(seq),
    );
    let fb: ItemUseFeedback | null = null;
    for (const body of bodies) {
      try {
        const packet = decodeWtPacket(body);
        fb = parseCultivationBookUseResult(packet) ?? fb;
      } catch {
        /* ignore */
      }
    }
    if (!fb) {
      fb = {
        ok: false,
        message: des ? `${des}（使用无响应）` : "天书使用无响应",
      };
    } else if (fb.ok) {
      this.consumeBagSeqLocally(seq);
      this.handleBodies(bodies);
      if (des && (!fb.message || fb.message === "修炼天书使用成功")) {
        fb = { ...fb, message: des };
      }
    } else if (des && !fb.message) {
      fb = { ...fb, message: des };
    }
    this.emitItemUseFeedback(fb);
    return fb;
  }

  /** 828: 25/6 apply + 25/7 wait-clear (docs/re/2026-07-23-special-item-use-contract.md). */
  /**
   * Hangup panel「补充时间」: use one 战斗心得 (828) from bag.
   * Tip to buy from mall when bag has none.
   */
  async useBattleInsightFromBag(): Promise<ItemUseFeedback> {
    const row = this.bagItems.find(
      (it) => isBattleInsightItem(it.itemId) && it.count > 0 && it.seq > 0,
    );
    if (!row) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: "背包没有战斗心得，请先商城购买",
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }
    return this.useItem(row.itemId, row.seq);
  }

  private async useBattleInsight(seq: number): Promise<ItemUseFeedback> {
    if (!this.transport) {
      return { ok: false, message: "未连接服务器" };
    }
    if (!(seq > 0)) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: "背包序号无效，请重新打开背包",
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }
    const bodies = await this.transport.exchange(
      0,
      buildBattleInsightUseRequest(seq),
    );
    let fb = this.feedbackFromBodies(bodies) ?? {
      ok: false,
      message: "战斗心得无响应",
    };
    if (fb.ok) {
      this.handleBodies(bodies);
      const follow = await this.transport.exchange(
        0,
        buildBattleInsightFollowupRequest(seq),
      );
      this.handleBodies(follow);
      if (!fb.message || fb.message === "使用成功") {
        fb = { ...fb, message: "战斗心得效果已生效，经验增加20%。" };
      }
    } else if (!fb.message) {
      fb = { ...fb, message: "使用失败" };
    }
    this.emitItemUseFeedback(fb);
    return fb;
  }

  /**
   * PixiJS-only ten-pull via server 1/7/24 (one request).
   * Pre-check empty slots / box / key; tip from backpack delta after openBag.
   */
  async openChestBatch(
    boxItemId: number,
    boxSeq: number,
    times = 10,
  ): Promise<ItemUseFeedback> {
    const need = Math.max(1, Math.min(10, times | 0));
    if (!isChestBoxItem(boxItemId)) {
      const fb: ItemUseFeedback = { ok: false, message: "无法开启该宝箱" };
      this.emitItemUseFeedback(fb);
      return fb;
    }
    if (!this.transport) {
      const fb: ItemUseFeedback = { ok: false, message: "未连接" };
      this.emitItemUseFeedback(fb);
      return fb;
    }
    const keyId = CHEST_KEY_BY_BOX[boxItemId];
    const cap =
      this.characterStats?.backpackCapacity ||
      Math.max(20, this.bagItems.length);
    const empty = Math.max(0, (cap | 0) - this.bagItems.length);
    if (empty < need) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: `背包空位不足（需≥${need}，当前${empty}）`,
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }
    const box =
      this.bagItems.find(
        (it) => it.itemId === boxItemId && it.count > 0 && it.seq > 0,
      ) ??
      (boxSeq > 0
        ? this.bagItems.find((it) => it.seq === boxSeq && it.count > 0)
        : undefined);
    if (!box || !(box.seq > 0)) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: "背包序号无效，请重新打开背包",
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }
    const boxCount = this.bagItems
      .filter((it) => it.itemId === boxItemId)
      .reduce((s, it) => s + Math.max(0, it.count | 0), 0);
    if (boxCount < need) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: `宝箱不足（需${need}，当前${boxCount}）`,
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }
    const key = keyId
      ? this.bagItems.find(
          (it) => it.itemId === keyId && it.count > 0 && it.seq > 0,
        )
      : undefined;
    if (!key) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: keyId
          ? "钥匙不存在（或背包未同步序号）"
          : "钥匙不存在",
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }
    const keyCount = this.bagItems
      .filter((it) => it.itemId === keyId)
      .reduce((s, it) => s + Math.max(0, it.count | 0), 0);
    if (keyCount < need) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: `钥匙不足（需${need}，当前${keyCount}）`,
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }

    const qtyBefore = new Map<number, number>();
    for (const it of this.bagItems) {
      const add = it.kind === "equip" ? 1 : Math.max(0, it.count | 0);
      qtyBefore.set(it.itemId, (qtyBefore.get(it.itemId) ?? 0) + add);
    }
    const nameById = new Map<number, string>();
    for (const it of this.bagItems) {
      if (!nameById.has(it.itemId)) nameById.set(it.itemId, it.name);
    }

    const bodies = await this.transport.exchange(
      0,
      buildChestOpenBatchRequest(box.seq, key.seq, need),
    );
    let batchFb: (ItemUseFeedback & { opened?: number }) | null = null;
    for (const body of bodies) {
      if (!body || body.length < 5) continue;
      const packet = ingestPacket(body);
      if (!packet) continue;
      const parsed = parseChestOpenBatchResult(packet);
      if (parsed) {
        batchFb = parsed;
        break;
      }
    }
    this.handleBodies(bodies);

    if (!batchFb?.ok) {
      const fb: ItemUseFeedback = batchFb ?? {
        ok: false,
        message: "开启失败",
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }

    const opened = Math.max(1, batchFb.opened ?? need);
    try {
      await this.openBag();
    } catch {
      /* tip falls back to wire rewards */
    }

    const qtyAfter = new Map<number, number>();
    for (const it of this.bagItems) {
      const add = it.kind === "equip" ? 1 : Math.max(0, it.count | 0);
      qtyAfter.set(it.itemId, (qtyAfter.get(it.itemId) ?? 0) + add);
      if (!nameById.has(it.itemId)) nameById.set(it.itemId, it.name);
    }
    for (const rw of batchFb.rewards ?? []) {
      nameById.set(rw.itemId, rw.name);
    }

    const tipRows: { itemId: number; name: string; count: number }[] = [];
    const ids = new Set<number>([
      ...qtyAfter.keys(),
      ...(batchFb.rewards ?? []).map((r) => r.itemId),
    ]);
    for (const id of ids) {
      if (id === boxItemId) continue;
      if (keyId && id === keyId) continue;
      const delta = (qtyAfter.get(id) ?? 0) - (qtyBefore.get(id) ?? 0);
      if (delta > 0) {
        tipRows.push({
          itemId: id,
          name: nameById.get(id) ?? `#${id}`,
          count: delta,
        });
      }
    }
    if (!tipRows.length) {
      for (const rw of batchFb.rewards ?? []) {
        tipRows.push({
          itemId: rw.itemId,
          name: rw.name,
          count: Math.max(1, rw.count | 0),
        });
      }
    }
    tipRows.sort((a, b) => a.name.localeCompare(b.name, "zh"));
    const parts = tipRows.map((g) =>
      g.count > 1 ? `${g.count}个${g.name}` : g.name,
    );
    const joined = parts.join("，");
    const detail =
      joined.length > 72 ? `${joined.slice(0, 71)}…` : joined;
    const summary: ItemUseFeedback = {
      ok: true,
      message:
        opened < need
          ? `连开${opened}/${need}次：${detail || "开启成功"}`
          : `连开${opened}次：${detail || "开启成功"}`,
      rewards: tipRows.map((g) => ({
        itemId: g.itemId,
        seq: 0,
        count: g.count,
        name: g.name,
      })),
    };
    this.emitItemUseFeedback(summary);
    return summary;
  }

  private async openChest(
    boxItemId: number,
    boxSeq: number,
    opts?: { emit?: boolean },
  ): Promise<ItemUseFeedback> {
    const emit = opts?.emit !== false;
    const keyId = CHEST_KEY_BY_BOX[boxItemId];
    if (!keyId || !this.transport) {
      const fb: ItemUseFeedback = { ok: false, message: "无法开启该宝箱" };
      if (emit) this.emitItemUseFeedback(fb);
      return fb;
    }
    if (!(boxSeq > 0)) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: "背包序号无效，请重新打开背包",
      };
      if (emit) this.emitItemUseFeedback(fb);
      return fb;
    }
    const key = this.bagItems.find(
      (it) => it.itemId === keyId && it.count > 0 && it.seq > 0,
    );
    if (!key) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: keyId
          ? "钥匙不存在（或背包未同步序号）"
          : "钥匙不存在",
      };
      if (emit) this.emitItemUseFeedback(fb);
      return fb;
    }
    const keySeq = key.seq;
    const bodies = await this.transport.exchange(
      0,
      buildChestOpenRequest(boxSeq, keySeq),
    );
    let fb = this.feedbackFromBodies(bodies) ?? {
      ok: false,
      message: "开启无响应",
    };
    if (fb.ok) {
      // Snapshot before handleBodies: 7/15 itself has no 7/7, but a mixed
      // exchange body must not let a type=1 peel race the CBE local grant.
      const bagBefore = this.bagItems;
      this.handleBodies(bodies);
      this.bagItems = applyChestOpenToBag(
        bagBefore,
        boxSeq,
        keySeq,
        fb.rewards ?? [],
      );
      this.cb.onBag?.(this.bagItems);
    } else if (!fb.message) {
      fb = { ...fb, message: "使用失败" };
    }
    if (emit) this.emitItemUseFeedback(fb);
    return fb;
  }

  private feedbackFromBodies(bodies: Uint8Array[]): ItemUseFeedback | null {
    for (const body of bodies) {
      if (!body || body.length < 5) continue;
      const packet = ingestPacket(body);
      if (!packet) continue;
      const fb = parseItemUseFeedback(packet);
      if (fb) return fb;
    }
    return null;
  }

  private emitItemUseFeedback(fb: ItemUseFeedback): void {
    if (!fb.message) return;
    this.cb.onItemUseFeedback?.(fb);
    this.cb.onChat?.([{ type: 5, text: `[系]系统: ${fb.message}` }]);
  }

  async discardItem(itemId: number, seq: number, count = 1): Promise<void> {
    if (!this.transport) return;
    // Equip / flask / mall-pass rows are atomic: never send reservoir/durability
    // as discard count (would look like ×4029249 in UI and confuse parsers).
    const row = this.bagItems.find((it) => it.seq === seq && it.itemId === itemId);
    const discardCount = row
      ? Math.max(1, bagVisibleStackCount(row))
      : Math.max(1, count | 0);
    const bodies = await this.transport.exchange(
      0,
      buildItemDiscardRequest(seq, itemId, discardCount),
    );
    this.handleBodies(bodies);
  }

  async equipItem(itemId: number, seq: number): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(
      0,
      buildItemEquipRequest(3, itemId, seq),
    );
    this.handleBodies(bodies);
  }

  async unequipItem(itemId: number, seq: number): Promise<void> {
    if (!this.transport) return;
    const worn =
      this.wornItems.find((w) => w.seq === seq) ||
      this.wornItems.find((w) => w.itemId === itemId);
    const bodies = await this.transport.exchange(
      0,
      buildItemEquipRequest(4, itemId, seq),
    );
    for (const body of bodies) {
      const packet = ingestPacket(body);
      if (!packet) continue;
      const eq = parseItemEquipResult(packet);
      if (eq && eq.result === 1 && eq.type === 4 && eq.seq > 0 && worn) {
        const dur =
          worn.durability != null
            ? worn.durability
            : (lookupItemSync(worn.itemId)?.durabilityMax ?? 0);
        this.pendingBagEquipDurability.set(eq.seq, dur >>> 0);
        // Prefer washed snapshot from worn; 7/8 may also carry same-packet
        // 7/7 type=1 with live wash — preferEnhanceAttrs keeps bit7 either way.
        if (worn.enhanceAttrs?.length) {
          this.pendingBagEquipEnhance.set(eq.seq, worn.enhanceAttrs.map((a) => ({ ...a })));
        }
      }
    }
    this.handleBodies(bodies);
  }

  /** Occupied slot replace: 1/7/9 { body: slot+1, bag: bagSeq }. */
  async swapEquip(bodySeq: number, bagSeq: number): Promise<void> {
    if (!this.transport) return;
    const worn = this.wornItems.find((w) => w.seq === bodySeq);
    if (worn) {
      // CBE: body item moves into bag using the bag row's sequence.
      const dur =
        worn.durability != null
          ? worn.durability
          : (lookupItemSync(worn.itemId)?.durabilityMax ?? 0);
      this.pendingBagEquipDurability.set(bagSeq, dur >>> 0);
      if (worn.enhanceAttrs?.length) {
        this.pendingBagEquipEnhance.set(
          bagSeq,
          worn.enhanceAttrs.map((a) => ({ ...a })),
        );
      }
    }
    const bodies = await this.transport.exchange(
      0,
      buildEquipSwapRequest(bodySeq, bagSeq),
    );
    this.handleBodies(bodies);
  }

  async openMall(): Promise<void> {
    if (!this.transport) return;
    try {
      await this.prepareLiveExchange("mall");
      await warmItemCatalogs();
      // Reset goods; keep last known coolmoney until 14/4 arrives.
      // coolmoney 字段实为 role.wcoin，UI 仍显示「酷宝」。
      this.mallState = {
        coolmoney: this.mallState?.coolmoney ?? 0,
        ticket: this.mallState?.ticket ?? 0,
        goods: [],
      };
      this.cb.onMall?.(this.mallState);

      // 余额：1/14/4（勿用 1/1/14，服务端要求 actorId）
      const balBodies = await this.transport.exchange(0, buildMallBalanceRequest());
      this.handleBodies(balBodies);

      // 商城停售装备：只拉 14/5 非装备酷宝/秘宝（不再拉 14/6..13 神兵页）。
      for (let page = 0; page < 256; page++) {
        const pageBodies = await this.transport.exchange(
          0,
          buildMallPageRequest(5, page),
        );
        let pageRows = 0;
        for (const body of pageBodies) {
          const packet = ingestPacket(body);
          if (!packet) continue;
          const mall = parseMallState(packet);
          if (mall) pageRows += mall.goods.length;
        }
        this.handleBodies(pageBodies);
        if (pageRows === 0) break;
      }

      if (this.mallState) this.cb.onMall?.(this.mallState);
      if (!this.mallState?.goods.length) {
        this.cb.onChat?.([
          {
            type: 5,
            text: "[系]商城无数据，若刚掉线请重试；仍空则重新选角进图",
          },
        ]);
      }
    } catch (e) {
      this.notifyPanelFetchFailed("商城", e);
    }
  }

  /** Pixi 人物信息：拉服务端 build_player_stats 全额汇总（1/1/21）. */
  async openCharacterStats(): Promise<void> {
    if (!this.transport) return;
    try {
      await this.prepareLiveExchange("stats");
      this.characterStats = null;
      const bodies = await this.transport.exchange(0, buildPanelStatsRequest());
      this.handleBodies(bodies);
    } catch (e) {
      this.notifyPanelFetchFailed("属性", e);
    }
  }

  async buyMallItem(
    itemId: number,
    num = 1,
  ): Promise<{ ok: boolean; result: number; message: string }> {
    if (!this.transport) {
      return { ok: false, result: 0, message: "未连接" };
    }
    const expandId = BACKPACK_EXPAND_ITEM_ID;
    const cap =
      this.characterStats?.backpackCapacity ||
      Math.max(20, this.bagItems.length);
    if ((itemId >>> 0) === expandId && (cap >>> 0) >= BACKPACK_EXPAND_MAX_CAPACITY) {
      return {
        ok: false,
        result: 3,
        message: "背包已达上限，不能再扩容",
      };
    }
    const bodies = await this.transport.exchange(
      0,
      buildShopBuy14Request(itemId, num),
    );
    let result = -1;
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseShopBuy14Response(packet);
        if (parsed) result = parsed.result;
      } catch {
        /* skip */
      }
    }
    this.handleBodies(bodies);
    // 购买后不重拉货架，保持当前详情/列表位置，只刷酷宝余额（1/14/4）。
    const balBodies = await this.transport.exchange(
      0,
      buildMallBalanceRequest(),
    );
    this.handleBodies(balBodies);
    if (result === 1 && (itemId >>> 0) === expandId) {
      // Direct expand updates role capacity — refresh panel stats for bag meta.
      try {
        await this.openCharacterStats();
      } catch {
        /* tip still reports purchase ok */
      }
    }
    if (result < 0) {
      return { ok: false, result: 0, message: "购买无响应" };
    }
    const tip = shopBuy14ResultTip(result, itemId);
    return {
      ok: result === 1,
      result,
      message: tip || (result === 1 ? "购买成功" : `购买失败(${result})`),
    };
  }

  async enhanceTransferPreview(
    destSeq: number,
    srcSeq: number,
  ): Promise<EnhanceTransferPreviewResult> {
    if (!this.transport) {
      return { result: 0, ok: false, name: "", moneyWire: 0, message: "未连接" };
    }
    const bodies = await this.transport.exchange(
      0,
      buildEnhanceTransferRequest(destSeq, srcSeq),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseEnhanceTransferPreview(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return { result: 0, ok: false, name: "", moneyWire: 0, message: "转移预览失败" };
  }

  async enhanceTransferConfirm(
    destSeq: number,
    srcSeq: number,
  ): Promise<EnhanceTransferConfirmResult> {
    if (!this.transport) {
      return {
        result: 0,
        ok: false,
        message: "未连接",
        sourceSeq: 0,
        sourceLevel: 0,
        destSeq: 0,
        destLevel: 0,
      };
    }
    const bodies = await this.transport.exchange(
      0,
      buildEnhanceTransferConfirmRequest(destSeq, srcSeq),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseEnhanceTransferConfirm(packet);
        if (parsed) {
          // CBE 0x010287C0: 29/6 result=1 writes seqs/curlevels + seqd/curleveld
          // into bag rows. Pixi must do the same — merge alone used to keep
          // source (+L) because enhance=0 was treated as "missing".
          if (parsed.ok) this.applyEnhanceTransferBagLevels(parsed);
          return parsed;
        }
      } catch {
        /* skip */
      }
    }
    return {
      result: 0,
      ok: false,
      message: "转移确认失败",
      sourceSeq: 0,
      sourceLevel: 0,
      destSeq: 0,
      destLevel: 0,
    };
  }

  /** Apply 29/6 curlevels/curleveld to local bag (CBE HandleItemUseAndEquip). */
  private applyEnhanceTransferBagLevels(
    parsed: EnhanceTransferConfirmResult,
  ): void {
    let changed = false;
    const src = this.bagItems.find(
      (it) => it.seq === parsed.sourceSeq && it.seq > 0,
    );
    const movedAttrs =
      src?.kind === "equip" && src.enhanceAttrs?.length
        ? src.enhanceAttrs.map((a) => ({ ...a }))
        : undefined;
    // Drop unequip-race pending for both seats — stale pending would resurrect
    // pre-transfer wash onto source (+0) or overwrite dest after move.
    if (parsed.sourceSeq > 0) this.pendingBagEquipEnhance.delete(parsed.sourceSeq);
    if (parsed.destSeq > 0) this.pendingBagEquipEnhance.delete(parsed.destSeq);
    this.bagItems = this.bagItems.map((it) => {
      if (it.seq === parsed.sourceSeq && it.seq > 0) {
        changed = true;
        const { enhanceAttrs: _drop, ...rest } = it;
        return { ...rest, enhance: parsed.sourceLevel >>> 0 };
      }
      if (it.seq === parsed.destSeq && it.seq > 0) {
        changed = true;
        // Server copies enhance_wash source→dest; mirror locally so dest does
        // not keep pre-transfer wash until the next 7/7/openBag.
        if (movedAttrs?.length) {
          return {
            ...it,
            enhance: parsed.destLevel >>> 0,
            enhanceAttrs: movedAttrs,
          };
        }
        const { enhanceAttrs: _dropDest, ...rest } = it;
        return { ...rest, enhance: parsed.destLevel >>> 0 };
      }
      return it;
    });
    if (changed) this.cb.onBag?.(this.bagItems);
  }

  /** @deprecated prefer enhanceTransferPreview / Confirm */
  async enhanceTransfer(destSeq: number, srcSeq: number): Promise<void> {
    await this.enhanceTransferPreview(destSeq, srcSeq);
  }

  async acceptTask(taskId: number): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildTaskAcceptRequest(taskId));
    this.handleBodies(bodies);
    // CBE scene_refresh_interact_prompt_types after accept — refresh overhead marks.
    await this.refreshNpcTaskPromptsAfterTaskChange();
  }

  async submitTask(taskId: number): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildTaskSubmitRequest(taskId));
    let ok = false;
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseTaskSubmitResult(packet);
        if (parsed?.ok) ok = true;
      } catch {
        /* skip */
      }
    }
    this.handleBodies(bodies);
    if (!ok) return;
    // Drop locally so yellow ? clears even if list query lags; then sync 6/1+6/14.
    const tid = taskId >>> 0;
    this.acceptedTasks = this.acceptedTasks.filter((t) => (t.taskId >>> 0) !== tid);
    this.npcTaskPrompts = computeNpcTaskPrompts(
      this.acceptedTasks,
      this.candidateTasks,
    );
    this.cb.onNpcTaskPrompts?.(this.npcTaskPrompts);
    await this.refreshNpcTaskPromptsAfterTaskChange();
  }

  /** Re-pull accepted + scene candidates so NPC fuhao marks match server. */
  private async refreshNpcTaskPromptsAfterTaskChange(): Promise<void> {
    try {
      await this.fetchTaskLists();
    } catch (e) {
      console.warn("[session] npc task prompt refresh", e);
    }
  }

  /** Empty 6/1 + 6/14 standalone list query (accepted + scene candidates). */
  async fetchTaskLists(): Promise<{
    accepted: TaskListRow[];
    candidates: TaskCandidateRow[];
  }> {
    const empty = { accepted: [] as TaskListRow[], candidates: [] as TaskCandidateRow[] };
    if (!this.transport) return empty;
    const bodies = await this.transport.exchange(0, buildTaskListQueryRequest());
    this.handleBodies(bodies);
    return {
      accepted: this.acceptedTasks.slice(),
      candidates: this.candidateTasks.slice(),
    };
  }

  private applyNpcTaskLists(
    accepted: TaskListRow[] | null,
    candidates: TaskCandidateRow[] | null,
  ): void {
    if (accepted) this.acceptedTasks = accepted;
    if (candidates) this.candidateTasks = candidates;
    this.npcTaskPrompts = computeNpcTaskPrompts(
      this.acceptedTasks,
      this.candidateTasks,
    );
    this.cb.onNpcTaskPrompts?.(this.npcTaskPrompts);
  }

  private clearNpcTaskPrompts(reason = "scene"): void {
    this.acceptedTasks = [];
    this.candidateTasks = [];
    this.npcTaskPrompts = new Map();
    this.cb.onNpcTaskPrompts?.(this.npcTaskPrompts);
    void reason;
  }

  /** Read-only 6/10 detail text (does not arm NPC confirm prompt). */
  async fetchTaskDetailText(taskId: number, state = 0): Promise<string> {
    if (!this.transport || !taskId) return "";
    const bodies = await this.transport.exchange(
      0,
      buildTaskDetailRequest(taskId, state),
    );
    let text = "";
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const detail = parseTaskDetail(packet);
        if (detail) text = detail;
      } catch {
        /* skip */
      }
    }
    return text;
  }

  /**
   * Abandon accepted task (6/7). Server removes given + type-1 collection
   * items; response may carry 7/7+7/11, then refresh bag via openBag.
   */
  async abandonTask(taskId: number): Promise<{ ok: boolean }> {
    if (!this.transport || !taskId) return { ok: false };
    const bodies = await this.transport.exchange(
      0,
      buildTaskAbandonRequest(taskId),
    );
    this.handleBodies(bodies);
    let ok = false;
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseTaskAbandonResult(packet);
        if (parsed) ok = parsed.ok;
      } catch {
        /* skip */
      }
    }
    if (ok) {
      const tid = taskId >>> 0;
      this.acceptedTasks = this.acceptedTasks.filter((t) => (t.taskId >>> 0) !== tid);
      this.npcTaskPrompts = computeNpcTaskPrompts(
        this.acceptedTasks,
        this.candidateTasks,
      );
      this.cb.onNpcTaskPrompts?.(this.npcTaskPrompts);
      await this.openBag();
      await this.refreshNpcTaskPromptsAfterTaskChange();
      this.cb.onChat?.([{ type: 5, text: "[任]已放弃任务" }]);
    } else {
      this.cb.onChat?.([{ type: 5, text: "[任]放弃失败" }]);
    }
    return { ok };
  }

  async openEnhance(seq: number): Promise<EnhanceOpenResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildEnhanceRequest(seq));
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseEnhanceOpenResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return null;
  }

  async previewEnhance(
    seq: number,
    materials: { itemId: number; count: number }[],
  ): Promise<EnhancePreviewResult | null> {
    if (!this.transport || !materials.length) return null;
    const bodies = await this.transport.exchange(
      0,
      buildEnhanceMaterialRequest(2, seq, materials),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseEnhancePreviewResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return null;
  }

  async confirmEnhance(
    seq: number,
    materials: { itemId: number; count: number }[],
  ): Promise<EnhanceConfirmResult> {
    if (!this.transport) {
      return { result: 0, ok: false, message: "未连接" };
    }
    if (!materials.length) {
      return { result: 0, ok: false, message: "请放入强化玄晶" };
    }
    const bodies = await this.transport.exchange(
      0,
      buildEnhanceMaterialRequest(3, seq, materials),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseEnhanceConfirmResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return { result: 0, ok: false, message: "强化无响应" };
  }

  /** @deprecated prefer openEnhance */
  async enhance(seq: number): Promise<void> {
    await this.openEnhance(seq);
  }

  async openEnhanceWash(seq: number): Promise<EnhanceWashOpenResult | null> {
    if (!this.transport) return null;
    if ((this.characterStats?.washButtonEnabled ?? 1) !== 1) {
      return {
        result: 7,
        seq: seq & 0xffff,
        curLevel: 0,
        tiers: [],
        pool: [],
      };
    }
    const bodies = await this.transport.exchange(
      0,
      buildEnhanceWashOpenRequest(seq),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseEnhanceWashOpenResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return null;
  }

  async previewEnhanceWash(
    seq: number,
    unlock: number,
    crystalItemId: number,
    crystalCount = 1,
  ): Promise<EnhanceWashPreviewResult | null> {
    if (!this.transport || !crystalItemId) return null;
    const bodies = await this.transport.exchange(
      0,
      buildEnhanceWashMaterialRequest(
        8,
        seq,
        unlock,
        crystalItemId,
        crystalCount,
      ),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseEnhanceWashPreviewResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return null;
  }

  async confirmEnhanceWash(
    seq: number,
    unlock: number,
    crystalItemId: number,
    crystalCount = 1,
  ): Promise<EnhanceWashConfirmResult> {
    if (!this.transport) {
      return {
        result: 0,
        ok: false,
        message: "未连接",
        unlock,
        rate: 0,
        money: 0,
      };
    }
    if (!crystalItemId) {
      return {
        result: 0,
        ok: false,
        message: "请放入玄晶",
        unlock,
        rate: 0,
        money: 0,
      };
    }
    const bodies = await this.transport.exchange(
      0,
      buildEnhanceWashMaterialRequest(
        9,
        seq,
        unlock,
        crystalItemId,
        crystalCount,
      ),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseEnhanceWashConfirmResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return {
      result: 0,
      ok: false,
      message: "洗练无响应",
      unlock,
      rate: 0,
      money: 0,
    };
  }

  /**
   * PixiJS-only: 29/14 accept/reject pending wash attrs.
   * Cost and wash_count already applied on 29/9 success.
   */
  async applyEnhanceWash(
    seq: number,
    unlock: number,
    apply: boolean,
  ): Promise<EnhanceWashApplyResult> {
    if (!this.transport) {
      return {
        result: 0,
        ok: false,
        message: "未连接",
        unlock,
        apply: apply ? 1 : 0,
      };
    }
    const bodies = await this.transport.exchange(
      0,
      buildEnhanceWashApplyRequest(seq, unlock, apply),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseEnhanceWashApplyResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return {
      result: 0,
      ok: false,
      message: "确认无响应",
      unlock,
      apply: apply ? 1 : 0,
    };
  }

  /**
   * PixiJS-only: 3×N级玄晶 → 1×(N+1)级.
   * Pre-checks count/space; server 29/10 is authoritative.
   */
  async combineCrystal(itemId: number, seq: number): Promise<CrystalCombineResult> {
    const fail = (message: string, result = 0): CrystalCombineResult => ({
      result,
      ok: false,
      message,
      srcItemId: itemId,
      destItemId: isEnhanceCrystal(itemId) ? itemId + 1 : 0,
      remaining: 0,
      destCount: 0,
    });
    if (!this.transport) return fail("未连接");
    if (!(seq > 0) || !isEnhanceCrystal(itemId)) {
      return fail("不是玄晶", 2);
    }
    if (itemId >= 916) {
      return fail("十六级玄晶无法合成", 5);
    }
    const row = this.bagItems.find((it) => it.seq === seq && it.itemId === itemId);
    const count = row?.count ?? 0;
    if (!canCombineEnhanceCrystal(itemId, count)) {
      return fail(count < 3 ? "玄晶不足3颗" : "十六级玄晶无法合成", count < 3 ? 4 : 5);
    }
    const destId = itemId + 1;
    const destExists = this.bagItems.some(
      (it) => it.itemId === destId && it.count > 0,
    );
    if (!destExists) {
      const cap =
        this.characterStats?.backpackCapacity ||
        Math.max(20, this.bagItems.length);
      const empty = Math.max(0, (cap | 0) - this.bagItems.length);
      const freesSlot = count === 3;
      if (empty < 1 && !freesSlot) {
        return fail("背包空间不足", 3);
      }
    }
    const bodies = await this.transport.exchange(
      0,
      buildCrystalCombineRequest(seq),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseCrystalCombineResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return fail("合成无响应");
  }

  /**
   * PixiJS-only: 10×玄晶碎片(900) → 1×一级玄晶(901).
   * Pre-checks count/space; server 29/11 is authoritative.
   */
  async combineCrystalFragment(
    itemId: number,
    seq: number,
  ): Promise<CrystalCombineResult> {
    const fail = (message: string, result = 0): CrystalCombineResult => ({
      result,
      ok: false,
      message,
      srcItemId: itemId,
      destItemId: ENHANCE_CRYSTAL_FIRST,
      remaining: 0,
      destCount: 0,
    });
    if (!this.transport) return fail("未连接");
    if (!(seq > 0) || !isCrystalFragment(itemId)) {
      return fail("不是玄晶碎片", 2);
    }
    const row = this.bagItems.find((it) => it.seq === seq && it.itemId === itemId);
    const count = row?.count ?? 0;
    if (!canCombineCrystalFragment(itemId, count)) {
      return fail("碎片不足10个", 4);
    }
    const destId = ENHANCE_CRYSTAL_FIRST;
    const destExists = this.bagItems.some(
      (it) => it.itemId === destId && it.count > 0,
    );
    if (!destExists) {
      const cap =
        this.characterStats?.backpackCapacity ||
        Math.max(20, this.bagItems.length);
      const empty = Math.max(0, (cap | 0) - this.bagItems.length);
      const freesSlot = count === CRYSTAL_FRAGMENT_COMBINE_COST;
      if (empty < 1 && !freesSlot) {
        return fail("背包空间不足", 3);
      }
    }
    const bodies = await this.transport.exchange(
      0,
      buildCrystalFragmentCombineRequest(seq),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseCrystalFragmentCombineResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return fail("合成无响应");
  }

  /**
   * PixiJS-only: 300×梦境徽章(840) → 1×自选梦境装备(40065..40088).
   * Pre-checks count/space; server 29/12 is authoritative.
   */
  async combineDreamBadge(
    itemId: number,
    seq: number,
    destItemId: number,
  ): Promise<CrystalCombineResult> {
    const fail = (message: string, result = 0): CrystalCombineResult => ({
      result,
      ok: false,
      message,
      srcItemId: itemId,
      destItemId: destItemId | 0,
      remaining: 0,
      destCount: 0,
    });
    if (!this.transport) return fail("未连接");
    if (!(seq > 0) || !isDreamBadge(itemId)) {
      return fail("不是梦境徽章", 2);
    }
    if (!isDreamBadgeEquipId(destItemId)) {
      return fail("请选择梦境装备", 6);
    }
    const row = this.bagItems.find((it) => it.seq === seq && it.itemId === itemId);
    const count = row?.count ?? 0;
    if (!canCombineDreamBadge(itemId, count)) {
      return fail("徽章不足300个", 4);
    }
    const cap =
      this.characterStats?.backpackCapacity ||
      Math.max(20, this.bagItems.length);
    const empty = Math.max(0, (cap | 0) - this.bagItems.length);
    const freesSlot = count === DREAM_BADGE_COMBINE_COST;
    if (empty < 1 && !freesSlot) {
      return fail("背包空间不足", 3);
    }
    const bodies = await this.transport.exchange(
      0,
      buildDreamBadgeCombineRequest(seq, destItemId),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseDreamBadgeCombineResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return fail("合成无响应");
  }

  private async exchangeGemOp(
    subtype: number,
    body: Uint8Array,
    failMessage: string,
  ): Promise<{ result: number; ok: boolean; message: string }> {
    if (!this.transport) return { result: 0, ok: false, message: "未连接" };
    const bodies = await this.transport.exchange(0, body);
    this.handleBodies(bodies);
    for (const raw of bodies) {
      try {
        const packet = ingestPacket(raw);
        if (!packet) continue;
        const parsed = parseGemOpResult(packet, subtype);
        if (parsed) {
          return {
            result: parsed.result,
            ok: parsed.result === 1,
            message:
              parsed.message || (parsed.result === 1 ? "成功" : failMessage),
          };
        }
      } catch {
        /* skip */
      }
    }
    return { result: 0, ok: false, message: failMessage };
  }

  /** Pixi-only 29/17. worn=true uses body slot seq (slot+1), not a bag seq. */
  async embedGem(
    equipSeq: number,
    gemSeq: number,
    worn: boolean,
  ): Promise<{ result: number; ok: boolean; message: string }> {
    if (!(equipSeq > 0) || !(gemSeq > 0)) {
      return { result: 2, ok: false, message: "参数无效" };
    }
    if ((this.characterStats?.gemSocketEnabled ?? 1) !== 1) {
      return { result: 7, ok: false, message: "宝石镶嵌未开放" };
    }
    return this.exchangeGemOp(
      17,
      buildGemEmbedRequest(equipSeq, gemSeq, worn),
      "镶嵌失败",
    );
  }

  /** Pixi-only 29/18. gemType 1反伤 2连击 3复活. */
  async removeGem(
    equipSeq: number,
    gemType: number,
    worn: boolean,
  ): Promise<{ result: number; ok: boolean; message: string }> {
    if (!(equipSeq > 0) || gemType < 1 || gemType > 3) {
      return { result: 2, ok: false, message: "参数无效" };
    }
    return this.exchangeGemOp(
      18,
      buildGemRemoveRequest(equipSeq, gemType, worn),
      "拆除失败",
    );
  }

  /** Pixi-only 29/19: 3× same gem → next level. times>1 is one-level batch. */
  async combineGem(
    itemId: number,
    seq: number,
    times = 1,
  ): Promise<{ result: number; ok: boolean; message: string }> {
    const n = Math.max(1, times | 0);
    if (!(seq > 0) || !parseGemItem(itemId)) {
      return { result: 2, ok: false, message: "不是宝石" };
    }
    const row = this.bagItems.find((it) => it.seq === seq && it.itemId === itemId);
    const have = row?.count ?? 0;
    const maxLevel = this.characterStats?.maxGemLevel || 3;
    if (!canCombineGem(itemId, have, maxLevel) || Math.floor(have / 3) < n) {
      return { result: 4, ok: false, message: "宝石不足3颗或已达最高等级" };
    }
    return this.exchangeGemOp(19, buildGemCombineRequest(seq, n), "合成失败");
  }

  /** Pixi-only 29/20: 2× Lv1 → 1× other-type Lv1. */
  async exchangeGemType(
    itemId: number,
    seq: number,
    destType: number,
  ): Promise<{ result: number; ok: boolean; message: string }> {
    if (!(seq > 0) || !canExchangeGemType(itemId, 2) || destType < 1 || destType > 3) {
      return { result: 2, ok: false, message: "不是1级宝石" };
    }
    const row = this.bagItems.find((it) => it.seq === seq && it.itemId === itemId);
    if (!canExchangeGemType(itemId, row?.count ?? 0)) {
      return { result: 4, ok: false, message: "1级宝石不足2颗" };
    }
    const src = parseGemItem(itemId);
    if (src && src.type === destType) {
      return { result: 2, ok: false, message: "请选择其它属性" };
    }
    return this.exchangeGemOp(
      20,
      buildGemTypeExchangeRequest(seq, destType),
      "兑换失败",
    );
  }

  /** Pixi-only 29/21: 1×916 → N random Lv1 gems (N from server gem config). */
  async gemFromCrystal16(
    itemId: number,
    seq: number,
  ): Promise<{ result: number; ok: boolean; message: string }> {
    if (!(seq > 0) || itemId !== 916) {
      return { result: 2, ok: false, message: "不是十六级玄晶" };
    }
    if (!(this.characterStats?.crystal16ExchangeEnabled ? 1 : 0)) {
      return { result: 7, ok: false, message: "十六级玄晶兑换未开放" };
    }
    return this.exchangeGemOp(21, buildGemFromCrystal16Request(seq), "兑换失败");
  }

  /** Pixi-only 29/22: 3×978 → 1 random Lv1 gem. times>1 rolls each independently. */
  async combineGemFragment(
    itemId: number,
    seq: number,
    times = 1,
  ): Promise<{ result: number; ok: boolean; message: string }> {
    const n = Math.max(1, times | 0);
    if (!(seq > 0) || !isGemFragment(itemId)) {
      return { result: 2, ok: false, message: "不是宝石碎片" };
    }
    const row = this.bagItems.find((it) => it.seq === seq && it.itemId === itemId);
    const have = row?.count ?? 0;
    if (!canCombineGemFragment(itemId, have) || Math.floor(have / 3) < n) {
      return { result: 4, ok: false, message: n > 1 ? "碎片不足" : "碎片不足3个" };
    }
    return this.exchangeGemOp(
      22,
      buildGemFragmentCombineRequest(seq, n),
      "合成失败",
    );
  }

  /**
   * PixiJS-only: 星空石(849) 重置指定装备洗练档位次数。
   * Server 29/13 is authoritative; attrs keep, only washCount → 0.
   */
  async resetEnhanceWashCount(
    stoneSeq: number,
    equipSeq: number,
    unlock: number,
  ): Promise<StarSkyStoneWashResetResult> {
    const fail = (
      message: string,
      result = 0,
    ): StarSkyStoneWashResetResult => ({
      result,
      ok: false,
      message,
      stoneSeq,
      equipSeq,
      unlock,
      remaining: 0,
      washCount: 0,
    });
    if (!this.transport) return fail("未连接");
    if (!(stoneSeq > 0) || !(equipSeq > 0)) return fail("参数无效");
    if (unlock !== 4 && unlock !== 8 && unlock !== 12 && unlock !== 16) {
      return fail("档位无效", 5);
    }
    const stone = this.bagItems.find(
      (it) => it.seq === stoneSeq && it.itemId === STAR_SKY_STONE_ITEM_ID,
    );
    if (!stone || (stone.count | 0) < 1) return fail("星空石不足", 2);
    const equip = this.bagItems.find((it) => it.seq === equipSeq);
    if (!equip || equip.kind !== "equip") return fail("装备不存在", 3);
    if ((equip.enhance | 0) < unlock) return fail("该档位未解锁", 4);
    const bodies = await this.transport.exchange(
      0,
      buildStarSkyStoneWashResetRequest(stoneSeq, equipSeq, unlock),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseStarSkyStoneWashResetResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return fail("重置无响应");
  }

  /**
   * PixiJS-only: 角色改名卡(852) 修改当前角色名。
   * Server 29/15 is authoritative; consumes 1 card on success.
   */
  async renameCharacter(
    cardSeq: number,
    name: string,
  ): Promise<RenameCardResult> {
    const fail = (message: string, result = 0): RenameCardResult => ({
      result,
      ok: false,
      message,
      cardSeq,
      remaining: 0,
      name: "",
    });
    if (!this.transport) return fail("未连接");
    if (!(cardSeq > 0)) return fail("参数无效");
    const trimmed = name.trim();
    if (!trimmed) return fail("请输入角色名", 3);
    if (trimmed.length > 14) return fail("角色名过长", 4);
    if (encodeWireString(trimmed).length >= 32) return fail("角色名过长", 4);
    if (trimmed === this.roleName) return fail("与当前名称相同", 5);
    if (this.roles.some((r) => r.roleId !== this.selectedRoleId && r.name === trimmed)) {
      return fail("名已被注册", 6);
    }
    const card = this.bagItems.find(
      (it) => it.seq === cardSeq && it.itemId === RENAME_CARD_ITEM_ID,
    );
    if (!card || (card.count | 0) < 1) return fail("改名卡不足", 2);
    const bodies = await this.transport.exchange(
      0,
      buildRenameCardRequest(cardSeq, trimmed),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseRenameCardResult(packet);
        if (parsed) {
          if (parsed.ok && parsed.name) {
            this.roleName = parsed.name;
            this.roles = this.roles.map((r) =>
              r.roleId === this.selectedRoleId ? { ...r, name: parsed.name } : r,
            );
            this.cb.onRoles(this.roles);
          }
          return parsed;
        }
      } catch {
        /* skip */
      }
    }
    return fail("改名无响应");
  }

  /** PixiJS-only: 天下第一冠军江湖签名。空串清空（对话显示默认高冷文案）. */
  async setTianxiaSignature(signature: string): Promise<TianxiaSignatureResult> {
    const fail = (message: string, result = 0): TianxiaSignatureResult => ({
      result,
      ok: false,
      message,
      signature: "",
    });
    if (!this.transport) return fail("未连接");
    const trimmed = signature.trim();
    if ([...trimmed].length > 32) return fail("签名过长", 4);
    const bodies = await this.transport.exchange(
      0,
      buildTianxiaSignatureRequest(trimmed),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseTianxiaSignatureResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return fail("签名无响应");
  }

  /**
   * PixiJS-only: 消耗1×玄晶提炼石(854)提炼已强化装备投入的玄晶，并销毁装备。
   * Server 29/16 is authoritative.
   */
  async extractEnhanceCrystals(equipSeq: number): Promise<CrystalExtractResult> {
    const fail = (message: string, result = 0): CrystalExtractResult => ({
      result,
      ok: false,
      message,
      equipSeq,
      stoneSeq: 0,
      remaining: 0,
      needSlots: 0,
      freeSlots: 0,
      spent: [],
    });
    if (!this.transport) return fail("未连接");
    if (!(equipSeq > 0)) return fail("参数无效");
    const equip = this.bagItems.find(
      (it) => it.seq === equipSeq && it.kind === "equip",
    );
    if (!equip) return fail("装备不存在", 3);
    if ((equip.enhance | 0) <= 0) return fail("未强化装备无法提炼", 4);
    const stone = this.bagItems.find(
      (it) => it.itemId === CRYSTAL_EXTRACT_STONE_ITEM_ID && (it.count | 0) >= 1,
    );
    if (!stone) return fail("玄晶提炼石不足", 2);
    const cap =
      this.characterStats?.backpackCapacity ||
      Math.max(20, this.bagItems.length);
    if (
      !canFitCrystalExtractInBag({
        enhance: equip.enhance,
        bagItems: this.bagItems,
        bagCapacity: cap,
        stoneCount: stone.count,
      })
    ) {
      const est = estimateCrystalExtractBagSpace({
        enhance: equip.enhance,
        bagItems: this.bagItems,
        stoneCount: stone.count,
      });
      const occupied = this.bagItems.filter((it) => (it.count | 0) > 0).length;
      const empty = Math.max(0, (cap | 0) - occupied);
      const short = Math.max(0, est.needSlots - empty - est.freeAfter);
      return fail(
        short > 0
          ? `背包空间不足，需空出${short}格存放玄晶`
          : "背包空间不足，无法存放提炼玄晶",
        5,
      );
    }
    const bodies = await this.transport.exchange(
      0,
      buildCrystalExtractRequest(equipSeq),
    );
    this.handleBodies(bodies);
    for (const body of bodies) {
      try {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const parsed = parseCrystalExtractResult(packet);
        if (parsed) return parsed;
      } catch {
        /* skip */
      }
    }
    return fail("提炼无响应");
  }

  async teamInvite(actorId: number): Promise<void> {
    if (!this.transport) return;
    /* Cap matches VM_MOCK_SERVICE_TEAM_MEMBER_MAX (1 leader + 2 members). */
    if (this.teamMembers.length >= 3) return;
    const bodies = await this.transport.exchange(0, buildTeamInviteRequest(actorId));
    this.handleBodies(bodies);
  }

  async teamReply(accept: boolean): Promise<void> {
    if (!this.transport) return;
    const inv = this.pendingSocialInvite;
    if (!inv || inv.kind !== "team") {
      this.setPhase("inworld", "no pending team invite");
      return;
    }
    const id = inv.id;
    this.pendingSocialInvite = null;
    const bodies = await this.transport.exchange(
      0,
      buildTeamReplyRequest(id, accept),
    );
    this.handleBodies(bodies);
  }

  async teamLeave(): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildTeamLeaveRequest());
    this.handleBodies(bodies);
  }

  async refreshTeamRoster(): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildTeamRosterRequest());
    this.handleBodies(bodies);
  }

  async openNearbyPlayers(pgIdx = 0): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildNearbyPageRequest(pgIdx));
    this.handleBodies(bodies);
  }

  async requestPlayerInfo(actorId: number): Promise<PlayerInfoView | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildPlayerInfoRequest(actorId));
    let info: PlayerInfoView | null = null;
    for (const body of bodies) {
      const packet = ingestPacket(body);
      const parsed = packet ? parsePlayerInfo(packet) : null;
      if (parsed) info = parsed;
    }
    this.handleBodies(bodies);
    return info;
  }

  async requestOtherEquip(actorId: number): Promise<OtherEquipView | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildOtherEquipRequest(actorId));
    let equip: OtherEquipView | null = null;
    for (const body of bodies) {
      const packet = ingestPacket(body);
      const parsed = packet ? parseOtherEquip(packet) : null;
      if (parsed) equip = parsed;
    }
    this.handleBodies(bodies);
    return equip;
  }

  async openFriends(page = 1): Promise<void> {
    if (!this.transport) return;
    const requested = Math.max(1, page | 0);
    this.friendPage = requested;
    /* Refresh spouseRoleId so friend list can mark 配偶. */
    if (!this.characterStats || !(this.characterStats.spouseRoleId > 0)) {
      try {
        await this.openCharacterStats();
      } catch {
        /* optional */
      }
    }
    const ordered = this.orderFriendsOnlineFirst(await this.loadFriendCatalog());
    const allPages = Math.max(
      1,
      Math.ceil(ordered.length / FRIEND_PAGE_SIZE) || 1,
    );
    const pageIndex = Math.min(requested, allPages);
    const start = (pageIndex - 1) * FRIEND_PAGE_SIZE;
    this.friendPage = pageIndex;
    this.friendAllPages = allPages;
    this.friends = ordered.slice(start, start + FRIEND_PAGE_SIZE);
    this.cb.onFriends?.(this.friends);
  }

  /**
   * Server pages friends by role id. Pull the whole list, then put online
   * rows first so later pages are not stuck behind offline role ids.
   */
  private async loadFriendCatalog(): Promise<FriendRow[]> {
    if (!this.transport) return [];
    const chunk = 255;
    const all: FriendRow[] = [];
    let index = 0;
    for (let n = 0; n < 8; n++) {
      this.suppressFriendPageApply = true;
      let bodies: Uint8Array[] = [];
      try {
        bodies = await this.transport.exchange(
          0,
          buildFriendPageRequest(index, chunk),
        );
        this.handleBodies(bodies);
      } finally {
        this.suppressFriendPageApply = false;
      }
      let pageFriends: FriendRow[] = [];
      let allpgs = 1;
      for (const body of bodies) {
        const packet = ingestPacket(body);
        const parsed = packet ? parseFriendPage(packet) : null;
        if (!parsed) continue;
        pageFriends = parsed.friends;
        allpgs = parsed.allpgs;
      }
      if (!pageFriends.length) break;
      all.push(...pageFriends);
      if (pageFriends.length < chunk || allpgs <= 1) break;
      index += pageFriends.length;
    }
    return all;
  }

  private orderFriendsOnlineFirst(rows: FriendRow[]): FriendRow[] {
    const spouseId = this.characterStats?.spouseRoleId ?? 0;
    return [...rows]
      .map((f) => ({
        ...f,
        isSpouse: spouseId > 0 && f.id === spouseId,
      }))
      .sort((a, b) => {
        if (a.online === b.online) return 0;
        return a.online ? -1 : 1;
      });
  }

  /** Warp to spouse (WT 1/32/1). Server returns scene-enter or 32/2 fail tip. */
  async spouseTeleport(): Promise<{ ok: boolean; tip: string }> {
    if (!this.transport) return { ok: false, tip: "未连接服务器" };
    if (this.isTeamMemberNotLeader()) {
      return { ok: false, tip: "队员不能传送" };
    }
    const bodies = await this.transport.exchange(0, buildSpouseTeleportRequest());
    let tip = "";
    let fail = false;
    for (const body of bodies) {
      const packet = ingestPacket(body);
      if (!packet) continue;
      const ack = parseSpouseTeleportAck(packet);
      if (ack) {
        tip = ack.tip || (ack.result === 1 ? "" : "传送失败");
        if (ack.result !== 1) fail = true;
      }
    }
    this.handleBodies(bodies);
    return { ok: !fail, tip };
  }

  async openEnmity(dir = 0, page = 1): Promise<void> {
    if (!this.transport) return;
    const p = Math.max(1, page | 0);
    this.enmityDir = dir === 1 ? 1 : 0;
    this.enmityPage = p;
    const index = (p - 1) * ENMITY_PAGE_SIZE;
    const bodies = await this.transport.exchange(
      0,
      buildEnmityPageRequest(this.enmityDir, index, ENMITY_PAGE_SIZE),
    );
    this.handleBodies(bodies);
  }

  async useHuntOrder(seq: number, targetId: number): Promise<ItemUseFeedback> {
    if (!this.transport) {
      return { ok: false, message: "未连接服务器" };
    }
    const bodies = await this.transport.exchange(
      0,
      buildHuntOrderUseRequest(seq, targetId, HUNT_ORDER_ITEM_ID),
    );
    let fb: ItemUseFeedback = { ok: false, message: "追杀令无响应" };
    for (const body of bodies) {
      try {
        const hunt = parseHuntOrderResult(decodeWtPacket(body));
        if (hunt) {
          fb = { ok: hunt.ok, message: hunt.tip };
          break;
        }
      } catch {
        /* try next */
      }
    }
    if (fb.ok) this.handleBodies(bodies);
    this.emitItemUseFeedback(fb);
    return fb;
  }

  async friendAdd(actorId: number): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildFriendAddRequest(actorId));
    this.handleBodies(bodies);
  }

  async friendDelete(actorId: number): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(
      0,
      buildFriendDeleteRequest(actorId),
    );
    this.handleBodies(bodies);
    /* Refresh current friend page so the removed row disappears. */
    await this.openFriends(this.friendPage);
  }

  async friendReply(accept: boolean): Promise<void> {
    if (!this.transport) return;
    const inv = this.pendingSocialInvite;
    if (!inv || inv.kind !== "friend") {
      this.setPhase("inworld", "no pending friend invite");
      return;
    }
    const id = inv.id;
    this.pendingSocialInvite = null;
    const bodies = await this.transport.exchange(
      0,
      buildFriendReplyRequest(id, accept),
    );
    this.handleBodies(bodies);
  }

  async sparChallenge(actorId: number, forcePk = false): Promise<void> {
    if (!this.transport || this.inBattle) return;
    if (this.isArenaScene()) {
      this.setPhase(
        "inworld",
        forcePk ? "比武擂台内不可强制PK" : "比武擂台内不可手动切磋",
      );
      return;
    }
    const bodies = await this.transport.exchange(
      0,
      buildSparRequest(actorId, forcePk),
    );
    this.handleBodies(bodies);
    // Force-PK may inline 4/10; invite-only 4/14 needs no poll for battle.
    if (forcePk && !this.inBattle) {
      await this.pollOnce();
    }
  }

  async sparReply(accept: boolean): Promise<void> {
    if (!this.transport) return;
    const inv = this.pendingSocialInvite;
    if (!inv || inv.kind !== "spar") {
      this.setPhase("inworld", "no pending spar invite");
      return;
    }
    const id = inv.id;
    this.pendingSocialInvite = null;
    const bodies = await this.transport.exchange(
      0,
      buildSparInviteReply(id, accept),
    );
    this.handleBodies(bodies);
    if (accept && !this.inBattle) {
      await this.pollOnce();
    }
  }

  async tradeInvite(actorId: number): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildTradeRequest(actorId));
    this.handleBodies(bodies);
  }

  async tradeReply(accept: boolean): Promise<void> {
    if (!this.transport) return;
    const inv = this.pendingSocialInvite;
    if (!inv || inv.kind !== "trade") {
      this.setPhase("inworld", "no pending trade invite");
      return;
    }
    const id = inv.id;
    this.pendingSocialInvite = null;
    const bodies = await this.transport.exchange(
      0,
      buildTradeReplyRequest(id, accept),
    );
    this.handleBodies(bodies);
  }

  async tradeCancel(): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildTradeCancelRequest());
    this.inTrade = false;
    this.tradePeerName = "";
    this.handleBodies(bodies);
  }

  async tradeOffer(
    money: number,
    items: { seq: number; count: number }[],
  ): Promise<void> {
    if (!this.transport || !this.inTrade) return;
    const bodies = await this.transport.exchange(
      0,
      buildTradeOfferRequest(money, items),
    );
    this.handleBodies(bodies);
  }

  async tradeConfirm(): Promise<void> {
    if (!this.transport || !this.inTrade) return;
    const bodies = await this.transport.exchange(0, buildTradeConfirmRequest());
    this.handleBodies(bodies);
  }

  async guildInvite(actorId: number): Promise<void> {
    if (!this.transport) return;
    const fid = (await this.ensureGuildId()) >>> 0;
    if (!fid) {
      this.setPhase("inworld", "你未在任何帮派中");
      return;
    }
    const bodies = await this.transport.exchange(
      0,
      buildGuildInviteRequest(actorId, fid),
    );
    this.handleBodies(bodies);
    this.cb.onSocialNotice?.({ kind: "guild_sent" });
  }

  async guildInviteReply(accept: boolean): Promise<void> {
    if (!this.transport) return;
    const inv = this.pendingSocialInvite;
    if (!inv || inv.kind !== "guild") {
      this.setPhase("inworld", "no pending guild invite");
      return;
    }
    const id = inv.id;
    this.pendingSocialInvite = null;
    const bodies = await this.transport.exchange(
      0,
      buildGuildInviteReplyRequest(id, accept),
    );
    this.handleBodies(bodies);
    /* Membership 10/36 / result 10/15 may arrive on the next scene-sync poll. */
    await this.pollOnce();
  }

  async guildCreateStart(): Promise<void> {
    if (!this.transport) return;
    this.pendingGuildCreateName = "";
    const bodies = await this.transport.exchange(
      0,
      buildGuildCreateStartRequest(),
    );
    this.handleBodies(bodies);
  }

  async guildCreateCheckName(name: string): Promise<void> {
    if (!this.transport) return;
    const trimmed = name.trim();
    if (!trimmed) {
      this.cb.onSocialNotice?.({
        kind: "guild_create",
        stage: "fail",
        tip: "名称不能为空",
      });
      return;
    }
    this.pendingGuildCreateName = trimmed;
    const bodies = await this.transport.exchange(
      0,
      buildGuildCreateNameRequest(trimmed),
    );
    this.handleBodies(bodies);
  }

  async guildCreateCommit(name?: string): Promise<void> {
    if (!this.transport) return;
    const commitName = (name ?? this.pendingGuildCreateName).trim();
    if (!commitName) {
      this.cb.onSocialNotice?.({
        kind: "guild_create",
        stage: "fail",
        tip: "无待确认帮派名",
      });
      return;
    }
    const bodies = await this.transport.exchange(
      0,
      buildGuildCreateCommitRequest(commitName),
    );
    this.handleBodies(bodies);
  }

  async openGuildList(page = 1): Promise<void> {
    if (!this.transport) return;
    this.guildListPage = Math.max(1, page);
    this.guildListUi = true;
    this.guildBrowseKind = "list";
    const bodies = await this.transport.exchange(
      0,
      buildGuildListRequest(
        guildPageRowIndex(this.guildListPage, GUILD_PAGE_SIZE),
        GUILD_PAGE_SIZE,
      ),
    );
    this.handleBodies(bodies);
  }

  /** Resolve guildId via 10/21.fid without opening list UI. */
  async ensureGuildId(): Promise<number> {
    if (this.guildId) return this.guildId;
    if (!this.transport) return 0;
    this.guildListUi = false;
    const bodies = await this.transport.exchange(
      0,
      buildGuildListRequest(1, GUILD_PAGE_SIZE),
    );
    this.handleBodies(bodies);
    return this.guildId;
  }

  async openGuildSelfDetail(): Promise<void> {
    if (!this.transport || !this.guildId) return;
    const bodies = await this.transport.exchange(
      0,
      buildGuildSelfDetailRequest(this.guildId),
    );
    this.handleBodies(bodies);
  }

  async openGuildDetail(guildId: number): Promise<void> {
    if (!this.transport || !guildId) return;
    const bodies = await this.transport.exchange(
      0,
      buildGuildDetailRequest(guildId),
    );
    this.handleBodies(bodies);
  }

  async openGuildMembers(page = 1): Promise<void> {
    if (!this.transport) return;
    this.guildMemberPage = Math.max(1, page);
    this.guildBrowseKind = "members";
    const bodies = await this.transport.exchange(
      0,
      buildGuildMemberPageRequest(
        guildPageRowIndex(this.guildMemberPage, GUILD_PAGE_SIZE),
        GUILD_PAGE_SIZE,
      ),
    );
    this.handleBodies(bodies);
  }

  async openGuildApplications(page = 1): Promise<void> {
    if (!this.transport) return;
    this.guildAppPage = Math.max(1, page);
    this.guildBrowseKind = "apps";
    const bodies = await this.transport.exchange(
      0,
      buildGuildApplicationPageRequest(
        guildPageRowIndex(this.guildAppPage, GUILD_PAGE_SIZE),
        GUILD_PAGE_SIZE,
      ),
    );
    this.handleBodies(bodies);
  }

  async guildApply(guildId: number): Promise<void> {
    if (!this.transport || !guildId) return;
    const bodies = await this.transport.exchange(
      0,
      buildGuildApplyRequest(guildId),
    );
    this.handleBodies(bodies);
  }

  async guildLeave(): Promise<void> {
    if (!this.transport || !this.selectedRoleId) return;
    const bodies = await this.transport.exchange(
      0,
      buildGuildLeaveRequest(this.selectedRoleId),
    );
    this.handleBodies(bodies);
  }

  async guildDisband(): Promise<void> {
    if (!this.transport || !this.selectedRoleId) return;
    const bodies = await this.transport.exchange(
      0,
      buildGuildDisbandRequest(this.selectedRoleId),
    );
    this.handleBodies(bodies);
  }

  async guildKick(roleId: number, memberRank: number): Promise<void> {
    if (!this.transport || !roleId || !memberRank) return;
    const bodies = await this.transport.exchange(
      0,
      buildGuildKickRequest(roleId, memberRank),
    );
    this.handleBodies(bodies);
    if (this.guildBrowseKind === "members") {
      await this.openGuildMembers(this.guildMemberPage);
    }
  }

  async guildReview(roleId: number, approve: boolean): Promise<void> {
    if (!this.transport || !roleId) return;
    const bodies = await this.transport.exchange(
      0,
      buildGuildApplicationActionRequest(roleId, approve),
    );
    this.handleBodies(bodies);
  }

  get inGuild(): boolean {
    return this.guildId > 0;
  }

  private applyTeamRoster(roster: TeamRoster): void {
    // Login/orphan 5/10 with memberCount<2 is not a party (CBE world HUD only
    // draws teammates for real multi-member teams). Keeping a solo self row
    // here re-seeds the top-left party vitals after role switch.
    if (roster.members.length < 2) {
      this.teamMembers = [];
      this.teamLeaderId = 0;
      this.cb.onTeamUpdate?.({ members: [], leaderId: 0 });
      return;
    }
    this.teamMembers = roster.members;
    this.teamLeaderId = roster.leaderId;
    const self = roster.members.find(
      (m) => (m.id >>> 0) === (this.selectedRoleId >>> 0),
    );
    if (self) {
      this.hp = self.hp;
      this.hpMax = Math.max(self.hpMax, self.hp, 1);
      this.mp = self.mp;
      this.mpMax = Math.max(self.mpMax, self.mp, 1);
      this.emitVitals();
    }
    this.cb.onTeamUpdate?.(roster);
  }

  /**
   * Merge 5/3 accept delta (other members only) into local roster.
   * Ensures self row exists so HUD / battle see a full party.
   */
  private mergeTeamRosterDelta(delta: TeamRoster): void {
    this.ensureSelfTeamRow();
    const byId = new Map(this.teamMembers.map((m) => [m.id >>> 0, m]));
    for (const row of delta.members) {
      byId.set(row.id >>> 0, row);
    }
    this.teamMembers = [...byId.values()];
    if (delta.leaderId) this.teamLeaderId = delta.leaderId;
    this.cb.onTeamUpdate?.({
      members: this.teamMembers,
      leaderId: this.teamLeaderId,
    });
  }

  private ensureSelfTeamRow(): void {
    const selfId = this.selectedRoleId >>> 0;
    if (!selfId) return;
    if (this.teamMembers.some((m) => m.id === selfId)) return;
    // Wire sexGroup 1..2 — must follow RoleRow.sex, not hardcode male.
    const sexGroup = this.roleSex ? 2 : 1;
    this.teamMembers = [
      {
        id: selfId,
        name: this.roleName || "我",
        sexGroup,
        jobIndex: this.roleJobIndex,
        online: true,
        hp: this.hp,
        mp: this.mp,
        hpMax: this.hpMax || 1,
        mpMax: this.mpMax,
      },
      ...this.teamMembers,
    ];
  }

  /** Map peer visual for a battle/team wire id (same onlineRoleId / synthetic id). */
  peerVisualFor(id: number): { jobIndex: number; sex: number; name: string } | null {
    const peer = this.scenePeerById.get(id >>> 0);
    if (!peer) return null;
    return {
      jobIndex: Math.max(0, Math.min(2, peer.visualVariant | 0)),
      sex: peer.visualGroup >= 1 ? (peer.visualGroup - 1) & 1 : 0,
      name: peer.name || "",
    };
  }

  private startPoll(): void {
    if (this.pollTimer != null) {
      window.clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
    this.pollTimer = window.setInterval(() => {
      void this.pollOnce();
      // Flush partial move buffer if stalled.
      if (this.moveBuf.length && performance.now() - this.lastMoveFlush > 400) {
        void this.flushMove();
      }
    }, 2500);
    void this.refreshNativeKeepAlive();
  }

  private stopPoll(): void {
    if (this.pollTimer != null) {
      window.clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
    void this.refreshNativeKeepAlive();
  }

  /** APK FGS: in-world online, or hangup-armed (even before scene). */
  private refreshNativeKeepAlive(): void {
    const inWorld = this.pollTimer != null || !!this.scene;
    void syncNativeKeepAlive({
      inWorld,
      hangup: this.hangupDesired,
    });
  }

  /** Keep gateway WS warm while parked on server/role list (no in-world poll yet). */
  private startTitleKeepalive(): void {
    this.stopTitleKeepalive();
    if (!(this.transport instanceof LiveTransport)) return;
    this.titleKeepaliveTimer = window.setInterval(() => {
      if (!(this.transport instanceof LiveTransport)) return;
      void this.transport.ping().catch(() => {
        /* next exchange will ensureConnected */
      });
    }, 8000);
  }

  private stopTitleKeepalive(): void {
    if (this.titleKeepaliveTimer != null) {
      window.clearInterval(this.titleKeepaliveTimer);
      this.titleKeepaliveTimer = null;
    }
  }

  private async ensureLiveSocket(): Promise<void> {
    if (this.transport instanceof LiveTransport) {
      await this.transport.ensureConnected();
    }
  }

  /**
   * Before panel fetches (mall/bag/…): re-open WS if dropped, then one
   * scene-poll so mock presence/account stay warm.  Heartbeat-timeout unbinds
   * the session — WS-only reconnect is not enough for 14/4 mall data.
   */
  private async prepareLiveExchange(reason: string): Promise<void> {
    if (!(this.transport instanceof LiveTransport)) return;
    await this.transport.ensureConnected();
    if (this.pollTimer == null || !this.selectedRoleId) return;
    try {
      const sceneBefore = this.scene || "";
      const bodies = await this.transport.exchange(
        CBMS_FLAG_SCENE_POLL,
        new Uint8Array(0),
      );
      this.handleBodies(bodies);
      await this.maybeSettleTeamFollowEnter(sceneBefore);
    } catch (e) {
      console.warn("[session] prepareLiveExchange", reason, e);
      throw e;
    }
  }

  private notifyPanelFetchFailed(panel: string, err: unknown): void {
    const msg = err instanceof Error ? err.message : String(err);
    console.warn(`[session] ${panel} fetch failed`, err);
    this.cb.onChat?.([
      { type: 5, text: `[系]${panel}拉取失败，请检查网络后重试` },
    ]);
    this.cb.onStatus?.({
      mode: this.transport?.mode ?? "?",
      phase: "panel-fetch-error",
      detail: `${panel}: ${msg.slice(0, 80)}`,
    });
  }

  /**
   * Native FGS Handler injects this every ~2s while in-world/hangup.
   * Bypasses Chromium hidden-document setInterval throttling that starves
   * scene-poll even after resumeTimers() + unrestricted battery.
   * Also drives client hangup / in-battle auto (those use setTimeout too).
   */
  async backgroundKeepAliveTick(): Promise<void> {
    if (this.nativeKeepAliveInFlight) return;
    if (!(this.transport instanceof LiveTransport)) return;
    this.nativeKeepAliveInFlight = true;
    try {
      // Unblock battle playback sleeps before attempting the next 4/2.
      this.onBackgroundGameplayTick?.();
      if (this.pollTimer != null) {
        await this.pollOnce();
      } else {
        await this.transport.ensureConnected();
        await this.transport.ping();
      }
      // Poll/operate responses may have started new playback — flush again.
      this.onBackgroundGameplayTick?.();
      await this.driveBackgroundHangup();
    } catch (e) {
      console.warn("[session] native keepalive", e);
    } finally {
      this.nativeKeepAliveInFlight = false;
    }
  }

  /**
   * When document.hidden, setTimeout for the next hangup 4/1 is unreliable.
   * FGS poke checks the deadline and starts the encounter.
   */
  private async driveBackgroundHangup(): Promise<void> {
    if (!this.hangupDesired || this.inBattle || !this.transport) return;
    if (this.hangupEncounterInFlight) return;
    if (
      typeof document !== "undefined" &&
      document.visibilityState === "visible"
    ) {
      return;
    }
    const dueAt = this.hangupEncounterDueAt;
    if (dueAt <= 0 || Date.now() < dueAt) return;
    await this.tickHangupEncounter("native-keepalive");
  }

  /** After screen-off / app resume: re-open gateway if OEM killed the WS. */
  async wakeFromBackground(): Promise<void> {
    try {
      if (this.transport instanceof LiveTransport) {
        await this.transport.ensureConnected();
        await this.transport.ping();
        if (this.pollTimer != null) {
          void this.pollOnce();
        }
      }
    } catch (e) {
      console.warn("[session] wake reconnect", e);
    }
  }

  private async pollOnce(): Promise<void> {
    if (!this.transport || this.teamFollowSettleInFlight) return;
    const runPoll = () =>
      this.transport!.exchange(CBMS_FLAG_SCENE_POLL, new Uint8Array(0));
    try {
      const sceneBefore = this.scene || "";
      const bodies = await runPoll();
      this.handleBodies(bodies);
      this.pollFailStreak = 0;
      if (!this.inBattle) {
        this.setPhase("inworld", `heartbeat ok · ${this.transport.mode}`);
      }
      await this.maybeSettleTeamFollowEnter(sceneBefore);
    } catch (e) {
      this.pollFailStreak++;
      try {
        await this.ensureLiveSocket();
        const sceneBefore = this.scene || "";
        const bodies = await runPoll();
        this.handleBodies(bodies);
        this.pollFailStreak = 0;
        if (!this.inBattle) {
          this.setPhase("inworld", `heartbeat ok · ${this.transport!.mode}`);
        }
        await this.maybeSettleTeamFollowEnter(sceneBefore);
        return;
      } catch {
        /* fall through */
      }
      this.setPhase("poll-error", e instanceof Error ? e.message : String(e));
    }
  }

  /**
   * Leader transfer fans out poll 30/1 to mates.  Same post-enter catalog
   * settle as map-stone / instance enter so NPC 27/11 is not left empty.
   */
  private async maybeSettleTeamFollowEnter(sceneBefore: string): Promise<void> {
    if (!this.transport || this.inBattle || this.sceneTransferExpected) return;
    if (this.teamFollowSettleInFlight) return;
    if (this.teamMembers.length < 2) return;
    const landed = ensureSceneFile(this.scene || "");
    if (!landed) return;
    if (sceneBefore && sceneKeysMatch(sceneBefore, landed)) return;
    this.teamFollowSettleInFlight = true;
    this.setPhase("team-follow", `跟随队长 → ${landed}`);
    try {
      await this.awaitEnterScene();
      const catalogBodies = await this.transport.exchange(
        0,
        buildSceneChangePostEnterFollowupRequest(landed, 0, 2),
      );
      this.handleBodies(catalogBodies);
      await this.awaitEnterScene();
      if (!this.npcs.length) {
        const completeBodies = await this.transport.exchange(
          0,
          buildSceneChangeRequest(landed, 0, 2),
        );
        this.handleBodies(completeBodies);
        await this.awaitEnterScene();
        const wt6Bodies = await this.transport.exchange(
          0,
          buildSceneNpcCatalogFollowupRequest(),
        );
        this.handleBodies(wt6Bodies);
        await this.awaitEnterScene();
      }
      this.setPhase("inworld", `team-follow ${this.scene} @${this.x},${this.y}`);
    } catch (e) {
      console.warn("[session] team-follow catalog settle", e);
    } finally {
      this.teamFollowSettleInFlight = false;
    }
  }

  private handleBodies(bodies: Uint8Array[]): void {
    for (const body of bodies) {
      if (!body || body.length < 5) continue;
      const packet = ingestPacket(body);
      if (!packet) continue;

      this.noteBattleInsightFromPacket(packet);
      this.noteDreamHudRemainFromPacket(packet);

      const list = parseServerList(packet);
      if (list.result === 3 && list.username && list.password) {
        this.accountUser = list.username;
        this.accountPass = list.password;
      }
      if (list.servers.length) {
        this.servers = list.servers;
        this.cb.onServers(this.servers);
      }

      const roles = parseRoleListFromSelect(packet);
      if (roles.length || packetHasTitleRoleList(packet)) {
        this.roles = roles;
        this.cb.onRoles(roles);
      }

      const mapVitals = parseMapActorVitals(packet);
      if (mapVitals) {
        this.hp = mapVitals.hp;
        this.hpMax = Math.max(mapVitals.hpMax, mapVitals.hp, 1);
        this.mp = mapVitals.mp;
        this.mpMax = Math.max(mapVitals.mpMax, mapVitals.mp, 1);
        // Settle baseline seed only (same WT as 4/7). Avoid mistaking live
        // 1/1/14 layout for fresh money/exp.
        const settleSeed = packet.objects.some(
          (o) => o.kind === 4 && o.subtype === 7,
        );
        if (settleSeed && mapVitals.hasEconomy) {
          this.exp = (mapVitals.exp ?? this.exp) >>> 0;
          this.gold = (mapVitals.money ?? this.gold) >>> 0;
        }
        this.emitVitals();
      }

      const roleEnter = parseRoleSelectEnter(packet);
      // Title 1/6.actorinfo is the role-select authority. Prefer it over a
      // concurrent deferred 30/1 — otherwise multi-role switch loads the prior map.
      const hardEnter = parseSceneEnter(packet);
      const roleEnterMismatch =
        !!roleEnter &&
        this.roleSelectInFlight &&
        !!this.selectedRoleId &&
        (roleEnter.roleId >>> 0) !== (this.selectedRoleId >>> 0);
      if (roleEnterMismatch) {
        console.error(
          `[session] ignore 1/6 role mismatch want=${this.selectedRoleId} got=${roleEnter!.roleId} name=${roleEnter!.name}`,
        );
      }
      const enter = roleEnterMismatch ? null : roleEnter ?? hardEnter;
      if (roleEnter && !roleEnterMismatch) {
        this.selectedRoleId = roleEnter.roleId >>> 0;
        this.hp = roleEnter.hp;
        // Title actorinfo hpMax is bare (pre wear-apply); current can exceed it.
        this.hpMax = Math.max(roleEnter.hpMax, roleEnter.hp, 1);
        this.mp = roleEnter.mp;
        this.mpMax = Math.max(roleEnter.mpMax, roleEnter.mp, 1);
        this.level = roleEnter.level || 1;
        // 4/7.exp/gold are absolute totals; panel 获得 = new − old (CBE HandleBattleSettleMsg).
        this.exp = roleEnter.exp >>> 0;
        this.gold = roleEnter.money >>> 0;
        this.roleTitle = roleEnter.title || "";
        this.roleTitleGif = roleEnter.titleGif || "";
        this.localChenghaoId = 0;
        this.roleName = roleEnter.name || this.roleName;
        if (this.roleSelectInFlight) {
          this.roleEnterSceneLock = roleEnter.scene;
        }
        if (hardEnter && !sceneKeysMatch(hardEnter.scene, roleEnter.scene)) {
          console.info(
            `[session] role-select prefer 1/6 ${roleEnter.scene} over 30/1 ${hardEnter.scene}`,
          );
        }
      }
      if (enter) {
        if (this.transferIgnoresScene(enter.scene)) {
          console.info(
            `[session] ignore stale enter ${enter.scene} during transfer→${this.sceneTransferExpected}`,
          );
        } else if (this.roleEnterIgnoresScene(enter.scene)) {
          console.info(
            `[session] ignore enter ${enter.scene} during role-select lock→${this.roleEnterSceneLock}`,
          );
        } else {
          this.scene = enter.scene;
          this.x = enter.x;
          this.y = enter.y;
          this.inBattle = false;
          this.syncArenaHudForScene(enter.scene);
          this.trackEnterScene(enter.scene, enter.x, enter.y, true);
          this.setPhase("inworld", `${enter.scene} @${enter.x},${enter.y}`);
        }
      } else {
        const landing = parseServerSceneLanding(packet);
        const reload = parseSceneReload(packet);
        if (landing) {
          if (this.transferIgnoresScene(landing.scene)) {
            console.info(
              `[session] ignore stale landing ${landing.scene} during transfer→${this.sceneTransferExpected}`,
            );
          } else if (this.roleEnterIgnoresScene(landing.scene)) {
            console.info(
              `[session] ignore landing ${landing.scene} during role-select lock→${this.roleEnterSceneLock}`,
            );
          } else {
            // Authoritative server footfall — never reuse prior-map coords.
            this.inBattle = false;
            this.applyServerLanding(
              landing.scene,
              landing.x,
              landing.y,
              landing.source,
            );
            this.setPhase(
              "inworld",
              `${landing.scene} @${landing.x},${landing.y}`,
            );
          }
        } else if (reload?.hasPos) {
          if (this.transferIgnoresScene(reload.scene)) {
            console.info(
              `[session] ignore stale reload+pos ${reload.scene} during transfer→${this.sceneTransferExpected}`,
            );
          } else if (this.roleEnterIgnoresScene(reload.scene)) {
            console.info(
              `[session] ignore reload+pos ${reload.scene} during role-select lock→${this.roleEnterSceneLock}`,
            );
          } else {
            this.inBattle = false;
            this.applyServerLanding(reload.scene, reload.x, reload.y, "30/reload");
            this.setPhase("inworld", `${reload.scene} @${reload.x},${reload.y}`);
          }
        } else if (reload) {
          if (this.transferIgnoresScene(reload.scene)) {
            console.info(
              `[session] ignore stale soft ${reload.scene} during transfer→${this.sceneTransferExpected}`,
            );
          } else if (this.roleEnterIgnoresScene(reload.scene)) {
            console.info(
              `[session] ignore soft ${reload.scene} during role-select lock→${this.roleEnterSceneLock}`,
            );
          } else {
            const mapShowing =
              this.loadedScene !== "" &&
              sceneKeysMatch(this.loadedScene, reload.scene);
            this.scene = reload.scene;
            this.inBattle = false;
            // Soft ack without posinfo: load shell only; wait for 30/2+pos / 27/12.
            // Same-map soft: do NOT remount SCE (keeps hangup timers), but still
            // notify UI — world-boss overlay used to require a full browser refresh.
            if (reload.soft && mapShowing) {
              void Promise.resolve(this.cb.onSceneSoftResync?.(reload.scene)).catch(
                (e) => console.warn("[session] onSceneSoftResync", e),
              );
            } else {
              this.hasServerPos = false;
              this.trackEnterScene(reload.scene, this.x, this.y, false);
            }
            this.setPhase(
              "inworld",
              reload.soft && mapShowing
                ? `ready ${reload.scene}`
                : `reload ${reload.scene} (await landing)`,
            );
          }
        }
      }

      // 27/11 present replaces catalog; missing object keeps prior.
      // Empty gate during an armed scene transfer must not wipe a nonempty
      // catalog (map-stone poll may seed before current-scene 2/3 empty-11).
      if (packetHasNpcCatalog(packet)) {
        const npcs = parseNpcCatalog(packet);
        if (
          npcs.length === 0 &&
          this.sceneTransferExpected &&
          this.npcs.length > 0
        ) {
          console.info(
            `[session] 27/11 empty gate ignored during transfer→${this.sceneTransferExpected} keep npc×${this.npcs.length}`,
          );
        } else {
          this.npcs = npcs;
          this.cb.onNpcs(npcs);
          if (npcs.length) {
            console.info(
              `[session] 27/11 npc×${npcs.length}`,
              npcs
                .slice(0, 4)
                .map((n) => `${n.name}@${n.x},${n.y}`)
                .join(" "),
            );
          }
        }
      }

      const peers = parseNearbyPeers(packet);
      const removed = parseRemovedPeers(packet);
      // Dream shells are solo — ignore peer paint / moves from any leftover path.
      const dreamSolo = (() => {
        const s = (this.scene || this.loadedScene || "").trim();
        if (!s) return false;
        const leaf = s
          .replace(/^.*[/\\]/, "")
          .replace(/\.sce$/i, "")
          .toLowerCase();
        return leaf.startsWith("29") || leaf.startsWith("b_29");
      })();
      if (dreamSolo) {
        if (peers.length || removed.length || this.scenePeerById.size) {
          const drop = [...this.scenePeerById.keys()];
          this.scenePeerById.clear();
          this.nearbyList = [];
          if (drop.length) this.cb.onPeers([], drop);
        }
      } else {
        for (const id of removed) this.scenePeerById.delete(id >>> 0);
        for (const p of peers) this.scenePeerById.set(p.actorId >>> 0, p);
        if (peers.length || removed.length) this.cb.onPeers(peers, removed);
        const peerMoves = parsePeerMoveUpdates(packet);
        if (peerMoves.length) this.cb.onPeerMoves?.(peerMoves);
      }

      const nearbyPage = parseNearbyPage(packet);
      if (nearbyPage) {
        this.nearbyList = dreamSolo ? [] : nearbyPage.peers;
        this.cb.onNearbyPage?.(
          dreamSolo ? { peers: [], allpgs: nearbyPage.allpgs } : nearbyPage,
        );
      }

      const playerInfo = parsePlayerInfo(packet);
      if (playerInfo) this.cb.onPlayerInfo?.(playerInfo);

      const otherEquip = parseOtherEquip(packet);
      if (otherEquip) this.cb.onOtherEquip?.(otherEquip);

      const friendPage = parseFriendPage(packet);
      if (friendPage && !this.suppressFriendPageApply) {
        const spouseId = this.characterStats?.spouseRoleId ?? 0;
        this.friends = friendPage.friends.map((f) => ({
          ...f,
          isSpouse: spouseId > 0 && f.id === spouseId,
        }));
        this.friendAllPages = Math.max(1, friendPage.allpgs | 0);
        if (this.friendPage > this.friendAllPages)
          this.friendPage = this.friendAllPages;
        this.cb.onFriends?.(this.friends);
      }

      const enmityPage = parseEnmityPage(packet);
      if (enmityPage) {
        this.enmityRows = enmityPage.rows;
        this.enmityDir = enmityPage.dir;
        this.enmityAllPages = Math.max(1, enmityPage.allPages | 0);
        if (this.enmityPage > this.enmityAllPages)
          this.enmityPage = this.enmityAllPages;
        this.cb.onEnmity?.({ dir: enmityPage.dir, rows: enmityPage.rows });
      }

      const friendInv = parseFriendInvitePackets(packet);
      if (friendInv.invite) {
        this.pendingSocialInvite = friendInv.invite;
        this.cb.onSocialInvite?.(friendInv.invite);
      }
      if (friendInv.notice) this.cb.onSocialNotice?.(friendInv.notice);

      const guildInv = parseGuildInvitePackets(packet);
      if (guildInv.invite) {
        this.pendingSocialInvite = guildInv.invite;
        this.cb.onSocialInvite?.(guildInv.invite);
      }
      if (guildInv.notice) this.cb.onSocialNotice?.(guildInv.notice);

      const tradePkt = parseTradePackets(packet);
      if (tradePkt.invite) {
        this.pendingSocialInvite = tradePkt.invite;
        this.cb.onSocialInvite?.(tradePkt.invite);
      }
      if (tradePkt.tradeOpen) {
        this.inTrade = true;
        this.tradePeerName = tradePkt.tradeOpen.name;
      }
      if (tradePkt.session) {
        if (tradePkt.session.kind === "open") {
          this.inTrade = true;
          this.tradePeerName = tradePkt.session.peerName;
        } else if (
          tradePkt.session.kind === "cancel" ||
          tradePkt.session.kind === "done"
        ) {
          this.inTrade = false;
          this.tradePeerName = "";
        }
        this.cb.onTradeSession?.(tradePkt.session);
      }
      if (tradePkt.notice) {
        if (tradePkt.notice.kind === "trade_result" && tradePkt.notice.result !== 1) {
          this.inTrade = false;
          this.tradePeerName = "";
        }
        this.cb.onSocialNotice?.(tradePkt.notice);
      }

      const guildCreate = parseGuildCreatePackets(packet);
      if (guildCreate.notice) {
        if (guildCreate.notice.kind === "guild_create") {
          if (guildCreate.notice.stage === "done") {
            const m = /帮派ID\s+(\d+)/.exec(guildCreate.notice.tip);
            if (m) this.guildId = Number(m[1]) >>> 0;
            this.guildSelfRank = 1;
            if (this.pendingGuildCreateName) {
              this.guildName = this.pendingGuildCreateName;
              if (this.characterStats) {
                this.characterStats = {
                  ...this.characterStats,
                  guild: this.guildName,
                };
                this.cb.onCharacterStats?.(this.characterStats);
              }
            }
            this.pendingGuildCreateName = "";
          } else if (guildCreate.notice.stage === "fail") {
            this.pendingGuildCreateName = "";
          }
        }
        this.cb.onSocialNotice?.(guildCreate.notice);
      }

      const guildManage = parseGuildManagePackets(packet);
      if (guildManage.list) {
        if (guildManage.list.fid) {
          this.guildId = guildManage.list.fid >>> 0;
        }
      }
      if (guildManage.detail) {
        this.guildId = guildManage.detail.guildId >>> 0;
        this.guildName = guildManage.detail.guildName;
      }
      if (guildManage.leaveResult?.result === 1) {
        this.guildId = 0;
        this.guildName = "";
        this.guildSelfRank = 0;
        if (this.characterStats) {
          this.characterStats = { ...this.characterStats, guild: "无帮派" };
          this.cb.onCharacterStats?.(this.characterStats);
        }
      }
      if (guildManage.members) {
        const self = guildManage.members.members.find(
          (m) => (m.roleId >>> 0) === (this.selectedRoleId >>> 0),
        );
        if (self) this.guildSelfRank = self.rank | 0;
      }
      if (guildManage.membershipNotice) {
        const n = guildManage.membershipNotice;
        if (n.type === 1 && n.guildId) {
          this.guildId = n.guildId >>> 0;
          this.guildName = n.guildName;
          if (this.characterStats) {
            this.characterStats = {
              ...this.characterStats,
              guild: n.guildName || this.characterStats.guild,
            };
            this.cb.onCharacterStats?.(this.characterStats);
          }
        }
      }
      if (
        guildManage.list ||
        guildManage.detail ||
        guildManage.members ||
        guildManage.applications ||
        guildManage.applyResult ||
        guildManage.leaveResult ||
        guildManage.kickResult ||
        guildManage.reviewResult ||
        guildManage.membershipNotice
      ) {
        if (guildManage.list && !this.guildListUi) {
          /* keep fid sync; strip list from UI payload */
          const { list: _omit, ...rest } = guildManage;
          if (
            rest.detail ||
            rest.members ||
            rest.applications ||
            rest.applyResult ||
            rest.leaveResult ||
            rest.kickResult ||
            rest.reviewResult ||
            rest.membershipNotice
          ) {
            this.cb.onGuildManage?.(rest);
          }
        } else {
          this.cb.onGuildManage?.(guildManage);
        }
        this.guildListUi = true;
      }

      const sparPkt = parseSparPackets(packet);
      if (sparPkt.invite) {
        this.pendingSocialInvite = sparPkt.invite;
        this.cb.onSocialInvite?.(sparPkt.invite);
      }
      if (sparPkt.notice) this.cb.onSocialNotice?.(sparPkt.notice);

      const group = parseGroupPackets(packet);
      if (group.invite) {
        this.pendingSocialInvite = group.invite;
        this.cb.onSocialInvite?.(group.invite);
      }
      if (group.roster) this.applyTeamRoster(group.roster);
      if (group.rosterDelta) this.mergeTeamRosterDelta(group.rosterDelta);
      if (group.join) {
        this.ensureSelfTeamRow();
        if (!this.teamLeaderId) this.teamLeaderId = this.selectedRoleId >>> 0;
        const exists = this.teamMembers.some((m) => m.id === group.join!.id);
        if (!exists) {
          this.teamMembers = [...this.teamMembers, group.join];
        }
        this.cb.onTeamUpdate?.({
          members: this.teamMembers,
          leaderId: this.teamLeaderId,
        });
      }
      if (group.hsp) {
        const h = group.hsp;
        this.teamMembers = this.teamMembers.map((m) =>
          m.id === h.id
            ? { ...m, hp: h.hp, hpMax: h.hpMax, mp: h.mp, mpMax: h.mpMax }
            : m,
        );
        // Self 5/11 is the post-battle map HUD authority (no ordinary 1/1/14).
        if ((h.id >>> 0) === (this.selectedRoleId >>> 0)) {
          this.hp = h.hp;
          this.hpMax = Math.max(h.hpMax, h.hp, 1);
          this.mp = h.mp;
          this.mpMax = Math.max(h.mpMax, h.mp, 1);
          this.emitVitals();
        }
        this.cb.onTeamUpdate?.({
          members: this.teamMembers,
          leaderId: this.teamLeaderId,
        });
      }
      if (group.leaveId != null) {
        const lid = group.leaveId;
        if (lid === this.teamLeaderId || this.teamMembers.length <= 1) {
          this.teamMembers = [];
          this.teamLeaderId = 0;
        } else {
          this.teamMembers = this.teamMembers.filter((m) => m.id !== lid);
        }
        this.cb.onTeamUpdate?.({
          members: this.teamMembers,
          leaderId: this.teamLeaderId,
        });
      }
      if (group.notice) this.cb.onSocialNotice?.(group.notice);

      const battle = parseBattleStart(packet, {
        roleName: this.roleName,
        partyMembers: this.teamMembers.map((m) => ({
          id: m.id,
          name: m.name,
          jobIndex: m.jobIndex,
          sex: m.sexGroup >= 1 ? (m.sexGroup - 1) & 1 : 0,
        })),
        monsterNameFor: (actorId) => {
          const hit =
            this.sceneMonsters.find(
              (n) =>
                n.actorId === actorId ||
                n.battleActorId === actorId,
            ) ||
            this.npcs.find(
              (n) =>
                n.actorId === actorId ||
                n.battleActorId === actorId,
            );
          return hit?.name;
        },
      });
      if (battle) {
        this.inBattle = true;
        this.cb.onBattle(battle);
      }

      const battleUp = parseBattleAction(packet);
      if (battleUp) {
        if (battleUp.settlement) {
          const s = battleUp.settlement;
          // Wire exp/gold are post-reward totals (docs/re/2026-06-25-battle-server-flow.md).
          const prevExp = this.exp >>> 0;
          const prevGold = this.gold >>> 0;
          const rawExp = (s.expTotal >>> 0) - prevExp;
          const rawGold = (s.goldTotal >>> 0) - prevGold;
          // Signed penalties only for known lose copy — not baseline desync.
          const f = s.fdata || "";
          const penalized =
            f.includes("挑战失败") ||
            f.includes("比武失败") ||
            /金钱\s*-/.test(f) ||
            /经验\s*-/.test(f);
          s.expGain = penalized ? rawExp : Math.max(0, rawExp);
          s.goldGain = penalized ? rawGold : Math.max(0, rawGold);
          this.exp = s.expTotal >>> 0;
          this.gold = s.goldTotal >>> 0;
          if (s.level > 0) this.level = s.level;
          if (this.characterStats) {
            this.characterStats = {
              ...this.characterStats,
              money: this.gold,
              exp: this.exp,
              level: this.level,
            };
            this.cb.onCharacterStats?.(this.characterStats);
          }
        }
        if (battleUp.finished || (battleUp.escaped && battleUp.escapeOk)) {
          this.inBattle = false;
        }
        this.cb.onBattleUpdate?.(battleUp);
      }

      const dialog = parseNpcDialog(packet);
      if (dialog) this.cb.onNpcDialog?.(dialog);

      const taskDetail = parseTaskDetail(packet);
      if (taskDetail && this.pendingTaskId) {
        this.cb.onTaskPrompt?.({
          taskId: this.pendingTaskId,
          text: taskDetail,
          mode: this.pendingTaskMode,
        });
      }

      const taskAccept = parseTaskAcceptResult(packet);
      if (taskAccept) {
        this.cb.onChat?.([
          {
            type: 5,
            text: taskAccept.ok ? "[任]接取成功" : "[任]接取失败",
          },
        ]);
        this.setPhase("inworld", taskAccept.ok ? "task accepted" : "task accept fail");
      }

      const taskSubmit = parseTaskSubmitResult(packet);
      if (taskSubmit) {
        this.cb.onChat?.([
          {
            type: 5,
            text: taskSubmit.ok
              ? `[任]提交成功${taskSubmit.text ? " " + taskSubmit.text : ""}`
              : "[任]提交失败",
          },
        ]);
        this.setPhase("inworld", taskSubmit.ok ? "task submitted" : "task submit fail");
      }

      const taskLists = parseTaskListQuery(packet);
      const hasAcceptedObj = packet.objects.some(
        (o) => o.kind === 6 && o.subtype === 1,
      );
      const hasCandidateObj = packet.objects.some(
        (o) => o.kind === 6 && o.subtype === 14,
      );
      if (hasAcceptedObj || hasCandidateObj) {
        this.applyNpcTaskLists(
          hasAcceptedObj ? taskLists.accepted : null,
          hasCandidateObj ? taskLists.candidates : null,
        );
      }

      const challenge = parseChallengePrompt(packet);
      if (challenge) {
        if (challenge.enemyId) {
          this.pendingChallengeEnemyId = challenge.enemyId >>> 0;
        }
        this.cb.onChallengePrompt?.(challenge.text);
      }

      const chats = parseChatNotices(packet);
      if (chats.length) {
        // Apply hangup flags before UI so onChat sees the updated hangupActive.
        for (const c of chats) {
          this.applyHangupChatFlag(c.text);
        }
        this.cb.onChat?.(
          chats.map((c) => ({
            type: c.type,
            text: c.text,
            senderRoleId: c.senderRoleId,
            senderName: c.senderName,
          })),
        );
        if (chats.some((c) => isAccountKickChatLine(c.text))) {
          this.forceAccountKick("账号已在其他地方登录，你被强制下线");
          return;
        }
      }

      const bagFull = parseBagItems(packet);
      if (bagFull.length) {
        // Prefer 30/21 seqs already merged inside parseBagItems.
        // Discard historically returned 17/1-only (all seq=0). Reattach via
        // prior-bag multiset — never Map-by-itemId (collapses duplicate equips).
        const hasWireSeq = bagFull.some((it) => it.seq > 0);
        const priorHasSeq = this.bagItems.some((it) => it.seq > 0);
        const reconciled =
          !hasWireSeq && priorHasSeq
            ? mergeBag17WithGridSeq(bagFull, this.bagItems)
            : bagFull;
        this.bagItems = reconciled.map((it) => {
          const prev = this.bagItems.find((b) => b.seq === it.seq && it.seq > 0);
          const withFlask = preserveFlaskReservoirAcrossBagRefresh(it, prev);
          return this.applyBagEquipDurability(withFlask, prev);
        });
        this.cb.onBag?.(this.bagItems);
        this.scheduleEnrichCultivationBookNames({ force: true });
      } else {
        const updates = parseBagItemUpdates(packet);
        if (updates.length) {
          for (const u of updates) {
            if (u.count <= 0) {
              this.pendingBagEquipDurability.delete(u.seq);
              this.pendingBagEquipEnhance.delete(u.seq);
            }
          }
          const merged = mergeBagItemUpdateDeltas(this.bagItems, updates);
          this.bagItems = merged.map((u) => {
            const prev = this.bagItems.find((i) => i.seq === u.seq);
            const withFlask = preserveFlaskReservoirAcrossBagRefresh(u, prev);
            return this.applyBagEquipDurability(withFlask, prev);
          });
          this.cb.onBag?.(this.bagItems);
          this.scheduleEnrichCultivationBookNames();
        }
      }
      const flaskRes = parseFlaskReservoirUpdates(packet);
      if (flaskRes.size) {
        let next = applyFlaskReservoirsToBag(this.bagItems, flaskRes);
        next = next.filter(
          (it) => !(isReservoirFlaskItemId(it.itemId) && it.count <= 0),
        );
        this.bagItems = next;
        this.cb.onBag?.(this.bagItems);
      }
      const wornSnap = parseWornEquipmentSnapshot(packet);
      if (wornSnap) {
        this.wornItems = wornSnap;
        this.cb.onWorn?.(wornSnap);
      }
      const mall = parseMallState(packet);
      if (mall) {
        const prev = this.mallState;
        const map = new Map<number, (typeof mall.goods)[0]>();
        for (const g of prev?.goods ?? []) map.set(g.itemId, g);
        for (const g of mall.goods) map.set(g.itemId, g);
        this.mallState = {
          coolmoney: mall.hasBalance
            ? mall.coolmoney
            : (prev?.coolmoney ?? 0),
          ticket: mall.hasBalance ? mall.ticket : (prev?.ticket ?? 0),
          goods: [...map.values()].sort((a, b) => a.itemId - b.itemId),
          hasBalance: mall.hasBalance || prev?.hasBalance,
        };
        if (this.characterStats && mall.hasBalance) {
          this.characterStats = {
            ...this.characterStats,
            coolmoney: this.mallState.coolmoney,
          };
          this.cb.onCharacterStats?.(this.characterStats);
        }
        this.cb.onMall?.(this.mallState);
      }
      const stats = parsePanelStats(packet);
      if (stats) {
        this.characterStats = stats;
        // Keep settle baseline aligned with 1/1/21 authority when available.
        this.exp = stats.exp >>> 0;
        this.gold = stats.money >>> 0;
        this.pkPoints = stats.pkPoints >>> 0;
        if (stats.level > 0) this.level = stats.level;
        // Panel 生命/法力 is map durable authority — keep WorldHud bars in sync.
        this.hp = stats.hp;
        this.hpMax = Math.max(stats.hpMax, stats.hp, 1);
        this.mp = stats.mp;
        this.mpMax = Math.max(stats.mpMax, stats.mp, 1);
        this.emitVitals();
        {
          const g = (stats.guild || "").replace(/\0+$/g, "").trim();
          if (g && g !== "无" && g !== "无帮派") this.guildName = g;
          else if (g === "无" || g === "无帮派") this.guildName = "";
        }
        this.cb.onCharacterStats?.(stats);
      }

      /* 10/26 after enhance / transfer / discard — bag copper label. */
      const moneySync = parseMoneyBalanceSync(packet);
      if (moneySync != null) {
        this.gold = moneySync >>> 0;
        if (this.characterStats) {
          this.characterStats = {
            ...this.characterStats,
            money: this.gold,
          };
          this.cb.onCharacterStats?.(this.characterStats);
        }
      }

      const hang = parseHangupNotice(packet);
      if (hang) {
        this.applyHangupChatFlag(hang);
        this.cb.onHangup?.(hang);
      }

      const arenaHud = parseArenaHud(packet);
      if (arenaHud) {
        this.arenaHud =
          arenaHud.status === 1 || arenaHud.status === 2 ? arenaHud : null;
        this.cb.onArenaHud?.(this.arenaHud);
        if (this.arenaHud) this.arenaHudFallbackAt = Date.now();
      } else if (this.isArenaScene() && !this.arenaHud) {
        void this.ensureArenaHudFallback("poll-miss");
      }
      const wagerEv = parseWagerEvent(packet);
      if (wagerEv) this.cb.onWagerEvent?.(wagerEv);
      const auctionEv = parseAuctionEvent(packet);
      if (auctionEv) this.cb.onAuctionEvent?.(auctionEv);
      const arenaSt = parseArenaStatus(packet);
      if (arenaSt) {
        this.arenaStatus = arenaSt;
        /* Status poll can outpace HUD push after settle — clear top strip. */
        if (arenaSt.status !== 1 && arenaSt.status !== 2 && this.arenaHud) {
          this.arenaHud = null;
          this.cb.onArenaHud?.(null);
        }
      }
      const arenaRk = parseArenaRank(packet);
      if (arenaRk) this.arenaRankPage = arenaRk;
      const arenaCl = parseArenaClaim(packet);
      if (arenaCl) {
        /* claim result consumed by claimArenaReward */
        void arenaCl;
      }

      const designation = parseDesignationPage(packet);
      if (designation) this.designationPage = designation;

      const designationUpd = parseDesignationSceneUpdate(packet);
      if (designationUpd) {
        const selfId = this.selectedRoleId >>> 0;
        if (!selfId || designationUpd.actorId === selfId) {
          this.applySelfDesignation(designationUpd.name, designationUpd.gif);
        } else {
          const peer = this.nearbyList.find(
            (p) => p.actorId === designationUpd.actorId,
          );
          if (peer) {
            peer.title = designationUpd.name;
            peer.titleGif = designationUpd.gif;
          }
          this.cb.onPeerDesignation?.({
            actorId: designationUpd.actorId,
            name: designationUpd.name,
            gif: designationUpd.gif,
          });
        }
      }

      const cosmeticsState = parseCosmeticsState(packet);
      if (cosmeticsState) this.applyCosmeticsState(cosmeticsState);
      for (const peerCos of parseCosmeticsPeerUpdates(packet)) {
        if (peerCos.actorId === (this.selectedRoleId >>> 0)) {
          /* Self echo in 31/5 — state already applied from 31/4 when present. */
          continue;
        }
        const id = peerCos.actorId >>> 0;
        const guildTrim = (peerCos.guild || "").replace(/\0+$/g, "").trim();
        const normalizedGuild =
          guildTrim === "无" || guildTrim === "无帮派" ? "" : guildTrim;
        const allZero =
          !(peerCos.eqChenghao || peerCos.eqWing || peerCos.eqMount ||
            normalizedGuild);
        const prev = this.scenePeerCosmeticsById.get(id);
        /*
         * Visibility enqueue can race before onlineEq* is mirrored → empty
         * 31/5. Caching that would wipe a later real notice on peer rebuild.
         * Keep empty only when we already had a notice (true unequip).
         * Guild is authoritative on every 31/5 (field always present).
         */
        if (allZero && !prev) {
          continue;
        }
        const merged: CosmeticsPeer = {
          ...peerCos,
          guild: normalizedGuild,
        };
        this.scenePeerCosmeticsById.set(id, merged);
        this.cb.onCosmeticsPeer?.(merged);
      }

      const ranking = parseRankingBoard(packet);
      if (ranking) this.rankingPage = ranking;

      const learned = parseLearnedSkills(packet);
      if (learned) {
        this.learnedSkillIds = learned.ids.slice();
        console.info(
          `[session] learned skills 12/1 count=${learned.count} ids=${learned.ids.join(",")} job=${this.roleJobIndex}`,
        );
        void this.refreshLearnedSkillRows();
      }
    }
  }

  private setPhase(phase: string, detail: string): void {
    this.phase = phase;
    this.cb.onStatus({
      mode: this.transport?.mode ?? "none",
      phase,
      detail,
    });
  }
}
