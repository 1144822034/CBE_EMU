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
import { BUNDLED_CLIENT_CODE_VERSION } from "../net/bundledCodeVersion";
import {
  buildBattleEscapeRequest,
  buildBattleItemUseRequest,
  buildBattleOperateRequest,
  buildBagOpenRequest,
  buildChallengeConfirmRequest,
  buildChatRequest,
  buildWorldChatHornRequest,
  clampWorldChatText,
  SMALL_HORN_ITEM_ID,
  buildEnhanceRequest,
  buildEnhanceMaterialRequest,
  buildEnhanceTransferRequest,
  buildEnhanceTransferConfirmRequest,
  buildEnhanceWashOpenRequest,
  buildEnhanceWashMaterialRequest,
  buildCrystalCombineRequest,
  parseEnhanceOpenResult,
  parseEnhancePreviewResult,
  parseEnhanceConfirmResult,
  parseEnhanceTransferPreview,
  parseEnhanceTransferConfirm,
  parseEnhanceWashOpenResult,
  parseEnhanceWashPreviewResult,
  parseEnhanceWashConfirmResult,
  parseCrystalCombineResult,
  canCombineEnhanceCrystal,
  isEnhanceCrystal,
  type EnhanceOpenResult,
  type EnhancePreviewResult,
  type EnhanceConfirmResult,
  type EnhanceTransferPreviewResult,
  type EnhanceTransferConfirmResult,
  type EnhanceWashOpenResult,
  type EnhanceWashPreviewResult,
  type EnhanceWashConfirmResult,
  type CrystalCombineResult,
  buildBattleInsightFollowupRequest,
  buildBattleInsightUseRequest,
  buildChestOpenRequest,
  buildExpCardUseRequest,
  buildExpCardStatusRequest,
  buildCombatPillUseRequest,
  parseExpCardStatusInfo,
  isCombatPillItem,
  buildEquipSwapRequest,
  buildItemDiscardRequest,
  buildItemEquipRequest,
  buildItemUseRequest,
  buildMapHealSkillRequest,
  CHEST_KEY_BY_BOX,
  expCardSuccessMessage,
  isBattleInsightItem,
  isChestBoxItem,
  isChestKeyItem,
  isExpCardItem,
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
  buildGuildListRequest,
  buildGuildSelfDetailRequest,
  buildGuildDetailRequest,
  buildGuildLeaveRequest,
  buildGuildMemberPageRequest,
  buildGuildApplyRequest,
  buildGuildApplicationPageRequest,
  buildGuildApplicationActionRequest,
  buildSceneChangePreludeRequest,
  buildSceneChangeRequest,
  buildTeleportStoneListRequest,
  buildMapTeleportPrepareRequest,
  buildMapTeleportConfirmedExitRequest,
  TELEPORT_STONE_ITEM_ID,
  parseMapTeleportConfirm,
  npcServiceOpcode,
  parseBagItems,
  parseBagItemUpdates,
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
  NpcServiceOpcode,
} from "../net/protocolExtras";
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
  buildAuctionBuyoutRequest,
  buildAuctionCancelRequest,
  buildAuctionClaimRequest,
  buildAuctionClaimsListRequest,
  buildAuctionCreateRequest,
  buildAuctionListRequest,
  buildAuctionMineRequest,
  buildAuctionStatusRequest,
  parseAuctionClaim,
  parseAuctionClaimsList,
  parseAuctionEvent,
  parseAuctionList,
  parseAuctionOpResult,
  parseAuctionStatus,
  type AuctionClaimResult,
  type AuctionClaimRow,
  type AuctionEvent,
  type AuctionListing,
  type AuctionOpResult,
  type AuctionStatus,
} from "../net/protocolAuction";
import { decodeWtPacket } from "../net/wt";
import {
  type BagItem,
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
} from "../net/protocolExtras";
import {
  parseNearbyPage,
  parsePlayerInfo,
  parseOtherEquip,
  parseFriendPage,
  parseGroupPackets,
  parseFriendInvitePackets,
  parseTradePackets,
  parseSparAck,
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
  onBattle: (battle: BattleState) => void;
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
  onChat?: (lines: { type: number; text: string }[]) => void;
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

export class GameSession {
  transport: GameTransport | null = null;
  servers: ServerRow[] = [];
  roles: RoleRow[] = [];
  npcs: NpcActor[] = [];
  /** SCE kind=3 combat spawns for the current map (not in 27/11). */
  sceneMonsters: NpcActor[] = [];
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
  /** Friend list cache (10/1). */
  friends: FriendRow[] = [];
  friendPage = 1;
  friendAllPages = 1;
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
   * Hangup / in-battle auto operate: 0 = normal attack; skill = skillId+2
   * (same as BattleScreen). Set via 挂机战斗 skill dialog; does not start hangup.
   */
  hangupSkillOperate = 0;
  /**
   * Pixi hangup armed: client loop simulates collide (`4/1` via
   * startBattleNearPlayer). Not driven by server hangup battle delivery.
   */
  hangupActive = false;
  /**
   * Player intent to keep hangup running across disconnect/relogin / between fights.
   */
  hangupDesired = false;
  /** Delay between client-driven hangup encounters (ms). */
  private hangupEncounterTimer: ReturnType<typeof setTimeout> | null = null;
  /**
   * In-battle [自动] switch (client-side 4/11). No auto-encounter — only
   * auto-operates after the player is already in battle.
   */
  autoBattleActive = false;
  /**
   * Last manual 4/2 operate this session. In-battle auto prefers this over
   * hangupSkillOperate when set.
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
  hp = 500;
  hpMax = 500;
  mp = 200;
  mpMax = 200;
  level = 1;
  exp = 0;
  gold = 0;
  roleTitle = "";
  /** Overhead GIF: riches_nameN.gif / level_nameN.gif */
  roleTitleGif = "";
  roleName = "";
  /** actorinfo jobIndex (raw 0..2) for skill.dsh filter. */
  roleJobIndex = 0;
  /** Normalized 0=male 1=female (RoleRow / actorinfo). */
  roleSex = 0;
  /** Live 2/10 peers — battle right seats prefer these visuals to match the map. */
  private scenePeerById = new Map<number, PeerActor>();
  /** Learned skill IDs from WT 1/12/1. */
  learnedSkillIds: number[] = [];
  /** Resolved rows (id + skill.dsh name) for battle menu. */
  learnedSkills: BattleSkillRow[] = [];
  /** Last bag / worn / mall / actorinfo from server packets. */
  bagItems: BagItem[] = [];
  wornItems: BagItem[] = [];
  /**
   * Unequip/swap: CBE moves the item object and keeps item+0x110. Pixi bag
   * open rebuilds from 30/21 (count=stack only), so stash current durability
   * by the destination bag seq until the next bag merge applies it.
   */
  private pendingBagEquipDurability = new Map<number, number>();
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
  }

  stop(): void {
    this.stopPoll();
    this.stopTitleKeepalive();
    this.transport?.close();
    this.transport = null;
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

    this.servers = [];
    this.roles = [];
    this.selectedServerId = 1;
    this.selectedRoleId = 0;
    this.roleName = "";
    this.roleJobIndex = 0;
    this.roleSex = 0;
    this.scenePeerById.clear();
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
    this.lastNpcId = 0;
    this.pendingChallengeGuideActorId = 0;
    this.pendingChallengeEnemyId = 0;
    this.pendingEncounterVisual = null;
    this.nearbyList = [];
    this.scenePeerById.clear();
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
    this.hp = 500;
    this.hpMax = 500;
    this.mp = 200;
    this.mpMax = 200;
    this.level = 1;
    this.exp = 0;
    this.gold = 0;
    this.bagItems = [];
    this.wornItems = [];
    this.pendingBagEquipDurability.clear();
    this.learnedSkillIds = [];
    this.learnedSkills = [];
    this.characterStats = null;
    this.mallState = null;
    this.designationPage = null;
    this.rankingPage = null;
    this.guildId = 0;
    this.guildName = "";
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
    if (this.servers.length === 0) await tryOnce();
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
      void warmItemCatalogs();
      // Live title path: 1/6+1/15 with actorinfo scene — not immediate 30/1.
      if (!this.scene) {
        throw new Error("选角后未解析到场景（1/6 actorinfo.scene），检查角色数据");
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
    if (!this.transport || this.inBattle) {
      return { ok: false, cost: 0, detail: "不可传送" };
    }
    const expectedScene = ensureSceneFile(targetScene.trim());
    if (!expectedScene || !exitId) {
      return { ok: false, cost: 0, detail: "目标场景无效" };
    }

    // Resolve stone BEFORE 16/4 — bag open after confirm can race server target.
    let stone = this.bagItems.find(
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

    this.stopPoll();
    this.transferIgnoredScene = null;
    this.sceneTransferExpected = expectedScene;
    this.setPhase(
      "map-teleport",
      `16/4 cur=${curTeleportId} obj=${objTeleportId} exit=${exitId} → ${expectedScene}`,
    );
    this.beginExpectLanding(expectedScene);

    try {
      await this.flushMove();
      const prepBodies = await this.transport.exchange(
        0,
        buildMapTeleportPrepareRequest(curTeleportId, objTeleportId),
      );
      this.handleBodies(prepBodies);
      let cost = 1;
      let sawConfirm = false;
      for (const body of prepBodies) {
        const packet = ingestPacket(body);
        if (!packet) continue;
        const conf = parseMapTeleportConfirm(packet);
        if (conf) {
          sawConfirm = true;
          cost = conf.value || 1;
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
      if (stone.count < cost) {
        return { ok: false, cost, detail: "传送石不够" };
      }

      // Drop prior map NPCs only once exit is armed (avoid wiping on confirm fail).
      this.npcs = [];
      this.cb.onNpcs([]);
      this.clearNpcTaskPrompts("map-teleport");

      const comboBodies = await this.transport.exchange(
        0,
        buildMapTeleportConfirmedExitRequest(exitId, cost, stone.seq, cost),
      );
      this.handleBodies(comboBodies);

      // Combo success = inventory ack (7/1 and/or 7/7). Empty WT or lone 16/2
      // means confirmed-exit-combo missed and deferred 30/1 was not armed.
      let comboKinds = "";
      let sawItemAck = false;
      for (const body of comboBodies) {
        if (!body || body.length < 5) continue;
        const packet = ingestPacket(body);
        if (!packet) continue;
        for (const o of packet.objects) {
          comboKinds += `${comboKinds ? "," : ""}${o.kind}/${o.subtype}`;
          if (o.kind === 7 && (o.subtype === 1 || o.subtype === 7 || o.subtype === 11)) {
            sawItemAck = true;
          }
        }
      }
      if (!sawItemAck) {
        return {
          ok: false,
          cost,
          detail: `扣石组合包未生效(${comboKinds || `empty×${comboBodies.length}`})`,
        };
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

      this.setPhase("inworld", `map-teleport ${this.scene} @${this.x},${this.y}`);
      return { ok: true, cost, detail: "ok" };
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      this.setPhase("map-teleport-error", msg);
      this.landingWait = null;
      return { ok: false, cost: 0, detail: msg };
    } finally {
      this.sceneTransferExpected = null;
      this.transferIgnoredScene = null;
      if (this.transport && this.scene) this.startPoll();
    }
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
    const pool = [...this.sceneMonsters, ...catalogMonsters];
    if (!id) {
      // Prefer SCE combat spawns, then catalog e_* rows; never town NPCs.
      encounter = pickNearest(pool);
      if (encounter) {
        id = encounter.battleActorId ?? encounter.actorId;
        index = id;
        battleX = encounter.x | 0;
        battleY = encounter.y | 0;
      }
    } else if (id >= 0x70000000) {
      const local = this.sceneMonsters.find((n) => n.actorId === id);
      encounter = local ?? null;
      id = local?.battleActorId ?? id;
      index = id;
      if (local) {
        battleX = local.x | 0;
        battleY = local.y | 0;
      }
    } else {
      // Catalog / challenge_enemy_id: keep id + catalog actor. Do not borrow
      // nearest map spawn visual (wrong species) or its tile (pos remap stole
      // floor bind stats). Neutral 1,1 matches instance-challenge wire fallback.
      const local = this.sceneMonsters.find(
        (n) => n.battleActorId === id || n.actorId === id,
      );
      if (local) {
        encounter = local;
        battleX = local.x | 0;
        battleY = local.y | 0;
        index = local.battleActorId ?? id;
      } else {
        encounter = null;
        battleX = 1;
        battleY = 1;
        index = id;
      }
    }
    if (!id) {
      id = 9001;
      index = 0;
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

  async startBattleNearPlayer(px: number, py: number, preferNpcId?: number): Promise<void> {
    if (!this.transport || this.inBattle) return;
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
      return;
    }
    const { id, index, battleX, battleY } = this.armEncounterVisualLikeCollide(
      px,
      py,
      preferNpcId,
    );
    if (id === 9001 && !preferNpcId) {
      console.warn("[session] battle wild id=9001 (no monster catalog)");
    }
    this.setPhase("battle-start", `id=${id} index=${index} pos=${battleX},${battleY}`);
    this.inBattle = true;
    const bodies = await this.transport.exchange(
      0,
      buildBattleStartRequest(id, index, battleX, battleY),
    );
    this.handleBodies(bodies);
    // Clear optimistic lock if no battle-start object arrived.
    if (
      !bodies.some((b) => {
        const p = ingestPacket(b);
        return p?.objects.some(
          (o) => o.kind === 4 && (o.subtype === 5 || o.subtype === 10),
        );
      })
    ) {
      this.inBattle = false;
      this.pendingEncounterVisual = null;
    }
  }

  async battleOperate(targetIndex: number, operate = 0): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(
      0,
      buildBattleOperateRequest(targetIndex, operate),
    );
    this.handleBodies(bodies);
  }

  /**
   * Client-side 4/2 auto for map [自动] and Pixi hangup (same as collide fight).
   */
  shouldInBattleAuto(): boolean {
    return this.autoBattleActive || this.hangupActive;
  }

  /**
   * Hangup always uses the configured hangup skill; [自动] prefers last manual.
   */
  inBattleAutoOperate(): number {
    if (this.hangupActive) return this.hangupSkillOperate | 0;
    if (this.lastBattleOperate != null) return this.lastBattleOperate | 0;
    return this.hangupSkillOperate | 0;
  }

  /** Record a player-chosen battle operate for subsequent [自动] rounds. */
  noteManualBattleOperate(operate: number): void {
    this.lastBattleOperate = operate | 0;
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
    this.clearHangupEncounterTimer();
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
  }

  /**
   * After leaving a hangup fight, wait then start the next collide-style 4/1.
   * Server does not deliver hangup battles for Pixi.
   */
  scheduleNextHangupEncounter(delayMs = 4000): void {
    this.clearHangupEncounterTimer();
    if (!this.hangupDesired) return;
    this.hangupEncounterTimer = setTimeout(() => {
      this.hangupEncounterTimer = null;
      void this.tickHangupEncounter("schedule");
    }, Math.max(500, delayMs | 0));
  }

  /** Call when battle UI returns to world — continue hangup loop if armed. */
  onLeftBattleForHangup(): void {
    if (!this.hangupDesired) {
      this.hangupActive = false;
      return;
    }
    this.hangupActive = true;
    this.scheduleNextHangupEncounter(4000);
  }

  private async tickHangupEncounter(reason: string): Promise<void> {
    if (!this.hangupDesired || this.inBattle || !this.transport) return;
    if (isCitySceneKey(this.scene || this.loadedScene || "")) {
      this.hangupActive = false;
      this.clearHangupDesired();
      this.cb.onChat?.([{ type: 5, text: "[挂]城内不能挂机" }]);
      return;
    }
    this.hangupActive = true;
    console.info("[session] hangup encounter", reason);
    await this.startBattleNearPlayer(this.x | 0, this.y | 0);
    if (!this.inBattle && this.hangupDesired) {
      this.scheduleNextHangupEncounter(5000);
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
    if (this.inBattle) return;
    await this.tickHangupEncounter("start");
  }

  async openDesignations(): Promise<DesignationPage | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(0, buildDesignationListRequest());
    this.handleBodies(bodies);
    return this.designationPage;
  }

  async selectDesignation(designationId: number): Promise<{
    ok: boolean;
    name?: string;
    gif?: string;
  }> {
    if (!this.transport) return { ok: false };
    const bodies = await this.transport.exchange(
      0,
      buildDesignationSelectRequest(designationId),
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
        if (upd.gif) gif = upd.gif;
        if (upd.name) name = upd.name;
        this.applySelfDesignation(upd.name, upd.gif);
      }
    }
    this.handleBodies(bodies);
    if (ok) {
      await this.openDesignations();
      const page = this.designationPage;
      const row = page?.rows.find(
        (r) => r.id === (page.equippedId || designationId),
      );
      if (row) {
        if (!gif && row.gif) gif = row.gif;
        if (!name && row.name) name = row.name;
        /* Prefer catalog overhead resource from refreshed list. */
        if (row.gif) gif = row.gif;
        if (row.name) name = row.name;
      }
      this.applySelfDesignation(name, gif);
    }
    return { ok, name: name || undefined, gif: gif || undefined };
  }

  private applySelfDesignation(name: string, gif: string): void {
    if (name) this.roleTitle = name;
    if (gif) this.roleTitleGif = gif;
    if (name || gif) {
      this.cb.onDesignationUpdate?.({
        name: this.roleTitle,
        gif: this.roleTitleGif,
      });
    }
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

  async auctionBuyout(listingId: number): Promise<AuctionOpResult | null> {
    if (!this.transport) return null;
    const bodies = await this.transport.exchange(
      0,
      buildAuctionBuyoutRequest(listingId),
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
    // Prefer it unconditionally so wire→catalog remap cannot pick another SCE kind.
    if (pending?.actorResource) {
      return {
        actorResource: pending.actorResource,
        name: pending.name || name,
      };
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
   */
  async sendChat(
    text: string,
    type = 0,
  ): Promise<{ ok: boolean; tip?: string }> {
    if (!this.transport || !text.trim()) return { ok: false };
    const body = text.trim();

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
            : type === 7
              ? "私"
              : "?";
    const name = this.roleName || "我";
    this.cb.onChat?.([{ type, text: `[${prefix}]${name}: ${body}` }]);
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
    } catch (e) {
      this.notifyPanelFetchFailed("背包", e);
    }
  }

  /** Merge bag-equip durability: pending unequip/swap → prior seq → wire (1). */
  private applyBagEquipDurability(it: BagItem, prev?: BagItem): BagItem {
    if (it.kind !== "equip") return it;
    const pending = this.pendingBagEquipDurability.get(it.seq);
    if (pending != null) {
      this.pendingBagEquipDurability.delete(it.seq);
      return { ...it, count: 1, durability: pending >>> 0 };
    }
    if (prev?.kind === "equip" && prev.durability != null) {
      return { ...it, count: 1, durability: prev.durability >>> 0 };
    }
    if (it.durability != null) return { ...it, count: 1 };
    // Login / fresh grant: server wire count = 1 → current durability 1.
    return { ...it, count: 1, durability: 1 };
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
   * - Other consumables (incl. custom 845) → 1/7/1
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
    } else if (!fb.message) {
      fb = { ...fb, message: "使用失败" };
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
    const info = parseExpCardStatusInfo(bodies);
    return info || "当前没有生效的增益物品";
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

  /** 828: 25/6 apply + 25/7 wait-clear (docs/re/2026-07-23-special-item-use-contract.md). */
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
   * Client-side batch open (default 10). Server still receives one 7/15 each.
   * Pre-check: bag empty slots >= times.
   */
  async openChestBatch(
    boxItemId: number,
    boxSeq: number,
    times = 10,
  ): Promise<ItemUseFeedback> {
    const need = Math.max(1, Math.min(99, times | 0));
    if (!isChestBoxItem(boxItemId)) {
      const fb: ItemUseFeedback = { ok: false, message: "无法开启该宝箱" };
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
    const keyCount = keyId
      ? this.bagItems
          .filter((it) => it.itemId === keyId)
          .reduce((s, it) => s + Math.max(0, it.count | 0), 0)
      : 0;
    if (keyCount < need) {
      const fb: ItemUseFeedback = {
        ok: false,
        message: `钥匙不足（需${need}，当前${keyCount}）`,
      };
      this.emitItemUseFeedback(fb);
      return fb;
    }

    const messages: string[] = [];
    let opened = 0;
    let lastFail: ItemUseFeedback | null = null;
    let seqHint = boxSeq;
    for (let i = 0; i < need; i++) {
      const box =
        this.bagItems.find(
          (it) => it.itemId === boxItemId && it.count > 0 && it.seq > 0,
        ) ??
        (seqHint > 0
          ? this.bagItems.find((it) => it.seq === seqHint && it.count > 0)
          : undefined);
      if (!box) {
        lastFail = { ok: false, message: "宝箱不存在" };
        break;
      }
      seqHint = box.seq;
      const fb = await this.openChest(box.itemId, box.seq, { emit: false });
      if (!fb.ok) {
        lastFail = fb;
        break;
      }
      opened += 1;
      if (fb.message) messages.push(fb.message);
    }

    let summary: ItemUseFeedback;
    if (opened <= 0) {
      summary = lastFail ?? { ok: false, message: "开启失败" };
    } else {
      const joined = messages.join("；");
      const detail =
        joined.length > 72 ? `${joined.slice(0, 71)}…` : joined;
      summary = {
        ok: true,
        message:
          opened < need
            ? `连开${opened}/${need}次：${detail || "开启成功"}`
            : `连开${opened}次：${detail || "开启成功"}`,
      };
    }
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
    const bodies = await this.transport.exchange(
      0,
      buildChestOpenRequest(boxSeq, key.seq),
    );
    let fb = this.feedbackFromBodies(bodies) ?? {
      ok: false,
      message: "开启无响应",
    };
    if (fb.ok) {
      this.handleBodies(bodies);
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
    this.cb.onItemUseFeedback?.(fb);
    this.cb.onChat?.([{ type: 5, text: `[系]系统: ${fb.message}` }]);
  }

  async discardItem(itemId: number, seq: number, count = 1): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(
      0,
      buildItemDiscardRequest(seq, itemId, count),
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
    await this.openMall();
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
        if (parsed) return parsed;
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
  ): Promise<EnhanceWashPreviewResult | null> {
    if (!this.transport || !crystalItemId) return null;
    const bodies = await this.transport.exchange(
      0,
      buildEnhanceWashMaterialRequest(8, seq, unlock, crystalItemId),
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
      buildEnhanceWashMaterialRequest(9, seq, unlock, crystalItemId),
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

  async teamInvite(actorId: number): Promise<void> {
    if (!this.transport) return;
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

  async requestPlayerInfo(actorId: number): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildPlayerInfoRequest(actorId));
    this.handleBodies(bodies);
  }

  async requestOtherEquip(actorId: number): Promise<void> {
    if (!this.transport) return;
    const bodies = await this.transport.exchange(0, buildOtherEquipRequest(actorId));
    this.handleBodies(bodies);
  }

  async openFriends(page = 1): Promise<void> {
    if (!this.transport) return;
    const p = Math.max(1, page | 0);
    this.friendPage = p;
    const index = (p - 1) * FRIEND_PAGE_SIZE;
    const bodies = await this.transport.exchange(
      0,
      buildFriendPageRequest(index, FRIEND_PAGE_SIZE),
    );
    this.handleBodies(bodies);
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

  async sparChallenge(actorId: number): Promise<void> {
    if (!this.transport || this.inBattle) return;
    if (this.isArenaScene()) {
      this.setPhase("inworld", "比武擂台内不可手动切磋");
      return;
    }
    const bodies = await this.transport.exchange(0, buildSparRequest(actorId));
    this.handleBodies(bodies);
    // Same as confirmChallenge: 4/10 may be deferred to scene-sync poll when
    // the spar ack response only carried 4/14 (or inline append failed).
    if (!this.inBattle) {
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
    const fid = 1;
    const bodies = await this.transport.exchange(
      0,
      buildGuildInviteRequest(actorId, fid),
    );
    this.handleBodies(bodies);
    this.cb.onSocialNotice?.({ kind: "guild_sent" });
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
      buildGuildListRequest(this.guildListPage, GUILD_PAGE_SIZE),
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
      buildGuildMemberPageRequest(this.guildMemberPage, GUILD_PAGE_SIZE),
    );
    this.handleBodies(bodies);
  }

  async openGuildApplications(page = 1): Promise<void> {
    if (!this.transport) return;
    this.guildAppPage = Math.max(1, page);
    this.guildBrowseKind = "apps";
    const bodies = await this.transport.exchange(
      0,
      buildGuildApplicationPageRequest(this.guildAppPage, GUILD_PAGE_SIZE),
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
      const bodies = await this.transport.exchange(
        CBMS_FLAG_SCENE_POLL,
        new Uint8Array(0),
      );
      this.handleBodies(bodies);
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
   */
  async backgroundKeepAliveTick(): Promise<void> {
    if (this.nativeKeepAliveInFlight) return;
    if (!(this.transport instanceof LiveTransport)) return;
    this.nativeKeepAliveInFlight = true;
    try {
      if (this.pollTimer != null) {
        await this.pollOnce();
      } else {
        await this.transport.ensureConnected();
        await this.transport.ping();
      }
    } catch (e) {
      console.warn("[session] native keepalive", e);
    } finally {
      this.nativeKeepAliveInFlight = false;
    }
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
    if (!this.transport) return;
    const runPoll = () =>
      this.transport!.exchange(CBMS_FLAG_SCENE_POLL, new Uint8Array(0));
    try {
      const bodies = await runPoll();
      this.handleBodies(bodies);
      this.pollFailStreak = 0;
      if (!this.inBattle) {
        this.setPhase("inworld", `heartbeat ok · ${this.transport.mode}`);
      }
    } catch (e) {
      this.pollFailStreak++;
      try {
        await this.ensureLiveSocket();
        const bodies = await runPoll();
        this.handleBodies(bodies);
        this.pollFailStreak = 0;
        if (!this.inBattle) {
          this.setPhase("inworld", `heartbeat ok · ${this.transport!.mode}`);
        }
        return;
      } catch {
        /* fall through */
      }
      this.setPhase("poll-error", e instanceof Error ? e.message : String(e));
    }
  }

  private handleBodies(bodies: Uint8Array[]): void {
    for (const body of bodies) {
      if (!body || body.length < 5) continue;
      const packet = ingestPacket(body);
      if (!packet) continue;

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
      const enter = roleEnter ?? hardEnter;
      if (roleEnter) {
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
            if (!(reload.soft && mapShowing)) {
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
      for (const id of removed) this.scenePeerById.delete(id >>> 0);
      for (const p of peers) this.scenePeerById.set(p.actorId >>> 0, p);
      if (peers.length || removed.length) this.cb.onPeers(peers, removed);
      const peerMoves = parsePeerMoveUpdates(packet);
      if (peerMoves.length) this.cb.onPeerMoves?.(peerMoves);

      const nearbyPage = parseNearbyPage(packet);
      if (nearbyPage) {
        this.nearbyList = nearbyPage.peers;
        this.cb.onNearbyPage?.(nearbyPage);
      }

      const playerInfo = parsePlayerInfo(packet);
      if (playerInfo) this.cb.onPlayerInfo?.(playerInfo);

      const otherEquip = parseOtherEquip(packet);
      if (otherEquip) this.cb.onOtherEquip?.(otherEquip);

      const friendPage = parseFriendPage(packet);
      if (friendPage) {
        this.friends = friendPage.friends;
        this.friendAllPages = Math.max(1, friendPage.allpgs | 0);
        if (this.friendPage > this.friendAllPages)
          this.friendPage = this.friendAllPages;
        this.cb.onFriends?.(friendPage.friends);
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
        if (this.characterStats) {
          this.characterStats = { ...this.characterStats, guild: "无帮派" };
          this.cb.onCharacterStats?.(this.characterStats);
        }
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

      const sparAck = parseSparAck(packet);
      if (sparAck) this.cb.onSocialNotice?.(sparAck);

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
        this.cb.onChat?.(chats.map((c) => ({ type: c.type, text: c.text })));
        if (chats.some((c) => isAccountKickChatLine(c.text))) {
          this.forceAccountKick("账号已在其他地方登录，你被强制下线");
          return;
        }
      }

      const bagFull = parseBagItems(packet);
      if (bagFull.length) {
        // Preserve known seq from prior 30/21 when 17/1 only has row order.
        // Equip durability: keep prior/pending (CBE +0x110); 30/21 is stack only.
        const byId = new Map(this.bagItems.map((i) => [i.itemId, i]));
        const bySeq = new Map(this.bagItems.map((i) => [i.seq, i]));
        this.bagItems = bagFull.map((it) => {
          const prevSeq = bySeq.get(it.seq);
          const prevId = byId.get(it.itemId);
          const prev = prevSeq ?? prevId;
          const merged =
            prev && prev.seq > 0 && it.seq <= 0
              ? { ...it, seq: prev.seq }
              : it;
          return this.applyBagEquipDurability(merged, prevSeq ?? prevId);
        });
        this.cb.onBag?.(this.bagItems);
      } else {
        const updates = parseBagItemUpdates(packet);
        if (updates.length) {
          const map = new Map(this.bagItems.map((i) => [i.seq, i]));
          for (const u of updates) {
            if (u.count <= 0) {
              map.delete(u.seq);
              this.pendingBagEquipDurability.delete(u.seq);
            } else {
              map.set(u.seq, this.applyBagEquipDurability(u, map.get(u.seq)));
            }
          }
          this.bagItems = [...map.values()];
          this.cb.onBag?.(this.bagItems);
        }
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
        if (stats.level > 0) this.level = stats.level;
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
        this.arenaHud = arenaHud;
        this.cb.onArenaHud?.(arenaHud);
      }
      const wagerEv = parseWagerEvent(packet);
      if (wagerEv) this.cb.onWagerEvent?.(wagerEv);
      const auctionEv = parseAuctionEvent(packet);
      if (auctionEv) this.cb.onAuctionEvent?.(auctionEv);
      const arenaSt = parseArenaStatus(packet);
      if (arenaSt) this.arenaStatus = arenaSt;
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
