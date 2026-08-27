#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include "../config.h"
#include "../gifDecode.h"
#include "../mysql-client.h"

/* The service uses this historical helper in data normalization only.  Keep
 * it local to the server boundary instead of linking the SDL client runtime. */
#ifndef SDL_min
#define SDL_min(x, y) ((x) < (y) ? (x) : (y))
#endif

/* A few legacy client-only helpers remain textually present in shared source
 * fragments.  The standalone service neither defines nor resolves these
 * symbols: with CBE_SERVER_ONLY their sections are discarded at link time. */
#if defined(CBE_PLATFORM_ANDROID)
#include <unicorn/unicorn.h>
#elif defined(CBE_PLATFORM_HEADLESS)
#include "../../Lib/unicorn-2.1.4/unicorn/unicorn.h"
#else
#include "../../Lib/unicorn-2.1.4/unicorn/unicorn.h"
#endif
extern uc_engine *MTK;
extern u32 Global_R9;

/*
 * The standalone listener and the protocol implementation deliberately share
 * only this platform/runtime boundary.  Feature modules remain private to the
 * mock service; callers must not manipulate their protocol or role state.
 */
#define VM_SCHED_MAX_NET_TASKS 8
#define VM_SCHED_MAX_TIMERS 20
#define VM_SCHED_FRAME_MS 100u

/* Shared bounds and service values for independently linked service modules.
 * Keep these exact values with the former core-local declarations: they are
 * part of durable role layouts and parser-backed NPC dialog values. */
#define VM_NET_MOCK_BACKPACK_MAX_ITEMS 200u
#define VM_NET_MOCK_EQUIP_SLOT_COUNT 8u
#define VM_NET_MOCK_NPC_KIND_ARENA_MASTER 8u
#define VM_NET_MOCK_NPC_KIND_MAILBOX 9u
#define VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS 7u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_MAILBOX_BASE 0xf5000000u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_MAIL_BASE 0xf6000000u
#define VM_NET_MOCK_NPC_SERVICE_CLAIM_MAIL_BASE 0xf7000000u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_MAILBOX 0xf5000001u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_ARENA_CREATE 0xef000001u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_ARENA_CHALLENGE 0xef000002u
#define VM_NET_MOCK_NPC_SERVICE_VALUE_MASK 0x00ffffffu
#define VM_NET_MOCK_MAIL_REWARD_MAX 12u
#define VM_NET_MOCK_REWARD15_MAX_ROWS 12u
#define VM_NET_MOCK_REWARD15_ITEMINFO_MAX_BYTES 4096u
#define VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS 256u
/* Shared ParseEquipAttributes wire bounds. */
#define VM_NET_MOCK_ITEM_COMMON_EXTRA_MAX_BYTES 76u
#define VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL 16u
#define VM_NET_MOCK_NEARBY_EQUIPINFO_MAX_BYTES \
    (VM_NET_MOCK_EQUIP_SLOT_COUNT * (6u + 4u + VM_NET_MOCK_ITEM_COMMON_EXTRA_MAX_BYTES))

enum
{
    VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE = 0,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_INVITE = 1,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TRADE_INVITE = 2,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_RESULT = 3,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TRADE_RESULT = 4,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_INVITE = 5,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_RESULT = 6,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_MEMBER_JOIN = 7,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_LEAVE = 8,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_HSP = 9,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_APPROVED = 10,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_REJECTED = 11,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_INVITE = 12,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_RESULT = 13,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_ARENA_CHALLENGE = 14
};

typedef struct
{
    u8 hasSceneTarget;
    u8 sceneSubtype;
    u8 sceneCompleteAfterCallback;
    u8 updateComplete;
    u8 hasHangupBattleStart;
    u8 hangupBattleStartDirect;
    u8 hangupResponseObjectCount;
    u8 hangupResponseParsedCount;
    u8 reserved0;
    u16 sceneX;
    u16 sceneY;
    u32 hangupResponseSequence;
    u32 hangupResponseLength;
    char scene[64];
    char updateName[64];
} vm_net_remote_observation;

typedef struct
{
    u8 active;
    u8 fired;
    u8 downloadSnapshotValid;
    u8 downloadSnapshotState;
    u16 delayTicks;
    u32 eventType;
    u32 r0;
    u32 r1;
    u32 r2;
    u32 callback;
    u32 context;
    u8 downloadSnapshot[0x60];
    vm_net_remote_observation remoteObservation;
} vm_net_task;

typedef struct
{
    u8 active;
    u32 connectId;
    u32 callback;
    u32 context;
} vm_net_channel;

typedef struct
{
    u8 active;
    u16 handle;
    u32 remainingTicks;
    u32 callback;
    u32 context;
} vm_timer_task;

typedef struct
{
    u8 major;
    u8 kind;
    u8 subtype;
    const u8 *payload;
    u16 payloadLen;
} vm_net_mock_request_object;

/* A stage attribute is an owned equipment-instance roll, not a catalogue
 * property.  It is shared by role persistence and the incremental-reward
 * serializer, so it must have one stable layout across split objects. */
typedef struct
{
    u8 type[4];
    u16 value[4];
} vm_net_mock_equipment_enhance_affix_state;

typedef struct
{
    u32 itemId;
    u16 seq;
    u16 enhanceLevel;
    u16 durability;
    u16 durabilityMax;
    u32 count;
    vm_net_mock_equipment_enhance_affix_state enhanceAffixes;
} vm_net_mock_backpack_item_state;

typedef struct
{
    u32 itemId;
    u16 enhanceLevel;
    u16 durability;
    u16 durabilityMax;
    vm_net_mock_equipment_enhance_affix_state enhanceAffixes;
} vm_net_mock_equipped_item_state;

typedef struct
{
    u32 roleId;
    char name[32];
    u8 job;
    u8 sex;
    u8 backpackCapacity;
    u8 reserved0;
    u32 level;
    u32 exp;
    u32 hp;
    u32 hpMax;
    u32 mp;
    u32 mpMax;
    u32 money;
    u32 wcoin;
    char scene[64];
    u16 x;
    u16 y;
    u8 backpackItemCount;
    u8 designationId;
    u16 nextBackpackSeq;
    vm_net_mock_equipped_item_state equippedItems[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    vm_net_mock_backpack_item_state backpackItems[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
} vm_net_mock_role_state;

/* Durable social and guild rows are value objects shared by their database
 * owner and independently compiled protocol builders.  They deliberately do
 * not expose the live client-session list or its mutable state. */
typedef struct
{
    char ownerAccountId[64];
    u32 ownerRoleId;
    char targetAccountId[64];
    u32 targetRoleId;
    char targetRoleName[32];
    u32 friendDegree;
    u32 targetLevel;
    u8 targetJob;
    u8 targetSex;
    u16 reserved0;
} vm_mock_service_friend_record;

typedef struct
{
    char magic[4];
    u32 version;
    u32 recordCount;
    vm_mock_service_friend_record records[VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS];
} vm_mock_service_friend_db_file;

enum
{
    VM_NET_MOCK_GUILD_PAGE_MAX = 32,
    VM_NET_MOCK_GUILD_NAME_SIZE = 32,
    VM_NET_MOCK_GUILD_ROLE_NAME_SIZE = 32,
    VM_NET_MOCK_GUILD_TEXT_SIZE = 128,
    VM_NET_MOCK_GUILD_NOTICE_MAX_BYTES = 60,
    VM_NET_MOCK_GUILD_POSITION_COUNT = 2
};

typedef struct
{
    u32 guildId;
    u32 guildLevel;
    u32 minimumLevel;
    u32 memberCount;
    u32 memberLimit;
    u32 guildMoney;
    u32 prosperity;
    u32 actionPower;
    u32 researchPower;
    u32 construction;
    char guildName[VM_NET_MOCK_GUILD_NAME_SIZE];
    char leaderName[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE];
    char currentConstruction[VM_NET_MOCK_GUILD_TEXT_SIZE];
    char notice[VM_NET_MOCK_GUILD_TEXT_SIZE];
} vm_net_mock_guild_record;

typedef struct
{
    u32 roleId;
    u32 level;
    u8 memberRank;
    u8 online;
    char accountId[64];
    char roleName[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE];
    char memberTitle[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE];
} vm_net_mock_guild_member_record;

typedef struct
{
    u32 roleId;
    u32 level;
    u8 job;
    u8 sex;
    char accountId[64];
    char roleName[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE];
} vm_net_mock_guild_application_record;

/* The owning session type stays private to the session module.  Mailbox only
 * needs the opaque service context to validate an already-emitted dialog. */
typedef struct vm_mock_service_npc_context vm_mock_service_npc_context;
typedef struct vm_mock_service_client_session vm_mock_service_client_session;
typedef struct vm_mock_service_duel vm_mock_service_duel;

/* A narrow snapshot prevents independently linked activity modules from
 * reaching into the service's owning session list. */
typedef struct
{
    bool roleOnline;
    bool onlinePresenceValid;
    bool sceneVisibleReady;
    bool sceneVisiblePending;
    bool arenaChallengeInitiatorPromptPending;
    bool arenaChallengeReplyActive;
    u32 clientId;
    u32 onlineRoleId;
    u32 arenaChallengeSourceRoleId;
    u8 onlineJob;
    u8 onlineSex;
    u16 onlineLevel;
    u16 onlineX;
    u16 onlineY;
    u16 sceneVisibleX;
    u16 sceneVisibleY;
    u32 onlineHp;
    u32 onlineHpMax;
    u32 onlineMp;
    u32 onlineMpMax;
    u32 onlineEquippedItemIds[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    u16 onlineEquippedEnhanceLevels[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    vm_net_mock_equipment_enhance_affix_state
        onlineEquippedEnhanceAffixes[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    char onlineRoleName[32];
    char onlineRoleTitle[32];
    char onlineRoleTitleBadge[32];
} vm_mock_service_online_session_view;

/* Team membership remains owned by the session service.  Protocol fragments
 * only need the outcome needed to encode the native roster delta. */
typedef struct
{
    bool accepted;
    u8 memberCount;
    u8 existingMembersQueued;
} vm_mock_service_team_join_result;

typedef enum
{
    VM_MOCK_SERVICE_TEAM_INVITE_ALLOWED = 0,
    VM_MOCK_SERVICE_TEAM_INVITE_INVALID,
    VM_MOCK_SERVICE_TEAM_INVITE_NOT_LEADER,
    VM_MOCK_SERVICE_TEAM_INVITE_FULL,
    VM_MOCK_SERVICE_TEAM_INVITE_TARGET_IN_TEAM
} vm_mock_service_team_invite_status;

typedef struct
{
    bool active;
    u32 sourceClientId;
    u32 sourceWireId;
} vm_mock_service_team_invite_reply_context;

typedef struct
{
    u32 itemId;
    u16 seq;
    u32 count;
} vm_net_mock_mail_claimed_item;

typedef struct
{
    char dialog[512];
    char optionNames[VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS][64];
    char optionDescriptions[VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS][192];
    u32 optionValues[VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS];
    u8 optionCount;
    const char *action;
    u32 result;
    u32 page;
    vm_net_mock_mail_claimed_item claimedItems[VM_NET_MOCK_MAIL_REWARD_MAX];
    u8 claimedItemCount;
} vm_net_mock_mailbox_dialog;

typedef struct
{
    const vm_net_mock_backpack_item_state *item;
    u32 acquiredCount;
} vm_net_mock_reward15_item_row;

/* One-row unsigned result used by service-owned schema and allocation
 * queries.  The callback validates strict decimal input before exposing it to
 * the requesting feature module. */
typedef struct
{
    u32 value;
    bool found;
    bool invalid;
} vm_mock_mysql_u32_context;

extern u8 g_mockServiceOnly;
extern u8 g_mockServiceWarnedUnavailable;
extern char g_mockServiceHost[64];
extern char g_mockServiceBindHost[64];
extern char g_mockAdminBindHost[64];
extern u32 g_mockServiceClientId;
extern u16 g_mockServicePort;
extern u16 g_mockAdminPort;
extern u32 g_schedulerTick;
extern vm_net_task g_netTasks[VM_SCHED_MAX_NET_TASKS];
extern vm_net_channel g_netChannels[VM_SCHED_MAX_NET_TASKS];
extern int g_netTaskDispatchDepth;
extern int g_netTaskDispatchSlot;
extern vm_timer_task g_timerTasks[VM_SCHED_MAX_TIMERS];
extern u32 g_schedulerStartTicks;
extern u32 g_nextNetConnectId;
extern u8 g_netMockResponse[131072];
extern u32 g_netMockResponseLen;
extern u32 g_netMockResponseOffset;
extern u32 g_netMockResponseVmPtr;
extern bool g_netMockSplitProbe;
extern u8 g_netMockUpdateDelivered;
extern u32 g_netMockEnterGameOffset;
extern u32 g_netMockEnterGameChecksum;
extern u8 g_netBusinessSendReadyDeferred;
extern u8 g_netBusinessSendReadyRerun;
extern u8 g_netBusinessSendReadyPostVm;
extern u8 g_loginTail42AllocPending;
extern u8 g_loginTail42FlushPending;
extern u32 g_netUpLinkData;
extern u32 g_netDownLinkData;
extern u32 g_netCurrentObject;
extern u8 g_netLastHandledValid;
extern u32 g_netLastHandledResponseLen;
extern char g_netLastHandledSource[64];
extern char g_netLastHandledSummary[512];
extern bool g_vm_net_mock_pending_scene_save_valid;
extern char g_vm_net_mock_pending_scene_save_scene[64];
extern char g_vm_net_mock_pending_scene_save_reason[64];
extern u16 g_vm_net_mock_pending_scene_save_x;
extern u16 g_vm_net_mock_pending_scene_save_y;
extern u32 g_mockBattleOperateSessionSerial;
extern u32 g_mockBattleOperateTurnCounter;
extern u8 g_mockBattleOperateSessionArmed;
extern u8 g_vm_net_mock_battle_auto_enabled;
extern u8 g_vm_net_mock_battle_auto_last_operation_valid;
extern u32 g_vm_net_mock_battle_auto_last_operation_role_id;
extern u32 g_vm_net_mock_battle_auto_last_operation_index;
extern u32 g_vm_net_mock_battle_auto_last_operation_operate;
extern u8 g_vm_net_mock_battle_auto_replay_inflight;
extern u8 g_vm_net_mock_battle_action6_emitted_count;
extern u32 g_vm_net_mock_battle_terminal_close_not_before_tick;
extern u8 g_mockBattleOperateSessionFinished;
extern u8 g_mockBattlePendingEnemyTurn;
extern u8 g_mockBattleAwaitingSettlement;
extern u8 g_mockBattleSceneMonsterStartActive;
extern u32 g_mockBattleRoleHpCurrent;
extern u32 g_mockBattleRoleHpMax;
extern u32 g_mockBattleRoleMpCurrent;
extern u32 g_mockBattleRoleMpMax;
extern u8 g_mockBattleEnemyCountCurrent;
extern u32 g_mockBattleEnemyHpSlots[3];
extern u32 g_mockBattleEnemyHpMaxSlots[3];
extern u32 g_mockBattleEnemyHpCurrent;
extern u32 g_mockBattleEnemyHpMax;

void vm_server_crash_note_protocol(const char *stage, u32 clientId,
                                   u8 wtKind, u8 wtSubtype,
                                   u32 requestLen, u32 responseLen,
                                   const char *source);
void vm_server_crash_note_protocol_stage(const char *stage);
void vm_autotest_note(const char *fmt, ...);
u32 scheduler_get_tick_ms(void);

bool vm_net_mock_get_object_u8_field(const u8 *request, u32 requestLen,
                                     const char *field, u8 *value);
bool vm_net_mock_get_object_u32_field(const u8 *request, u32 requestLen,
                                      const char *field, u32 *value);
bool vm_net_mock_get_object_number_field(const u8 *payload, u32 payloadLen,
                                         const char *field, u32 *value);
bool vm_net_mock_next_request_object(const u8 *request, u32 requestLen,
                                     u32 *offset,
                                     vm_net_mock_request_object *object);
bool vm_net_mock_put_object_u8(u8 *out, u32 outCap, u32 *pos,
                               const char *name, u8 value);
bool vm_net_mock_put_object_u32(u8 *out, u32 outCap, u32 *pos,
                                const char *name, u32 value);
bool vm_net_mock_put_object_raw(u8 *out, u32 outCap, u32 *pos,
                                const char *name, const u8 *data,
                                u16 dataLen);
bool vm_net_mock_put_object_string(u8 *out, u32 outCap, u32 *pos,
                                   const char *name, const char *value);
bool vm_net_mock_begin_wt_object(u8 *out, u32 outCap, u32 *pos,
                                 u8 major, u8 kind, u8 subtype,
                                 u32 *objectStart);
void vm_net_mock_finish_wt_object(u8 *out, u32 objectStart, u32 pos);
void vm_net_mock_finish_wt_packet(u8 *out, u32 pos, u8 objectCount);
bool vm_net_mock_seq_put_u8(u8 *out, u32 outCap, u32 *pos, u8 value);
bool vm_net_mock_seq_put_u32(u8 *out, u32 outCap, u32 *pos, u32 value);
bool vm_net_mock_seq_put_string(u8 *out, u32 outCap, u32 *pos,
                                const char *value);
bool vm_net_mock_seq_put_string_list(u8 *out, u32 outCap, u32 *pos,
                                     const char *const *values, u32 count);
bool vm_net_mock_mysql_account_hex(char account_hex[129]);
bool vm_mock_service_friend_record_find(u32 ownerRoleId,
                                        const char *ownerAccountId,
                                        u32 targetRoleId,
                                        vm_mock_service_friend_record *recordOut);
bool vm_mock_mysql_single_u32_row(void *contextValue,
                                  unsigned int columnCount,
                                  const char *const *values,
                                  const size_t *lengths);
bool vm_net_mock_role_db_save(const char *reason);
bool vm_net_mock_vitality_snapshot(vm_net_mock_role_state *role,
                                   u32 *currentOut, u32 *maxOut);
bool vm_net_mock_vitality_consume(vm_net_mock_role_state *role,
                                  u32 amount, u32 *currentOut,
                                  u32 *maxOut);
bool vm_net_mock_role_add_backpack_item_to_role_in_memory(
    vm_net_mock_role_state *role, u32 itemId, u32 count, u16 *seqOut);
vm_net_mock_backpack_item_state *vm_net_mock_role_find_backpack_item(
    vm_net_mock_role_state *role, u32 itemId, u16 seq);
bool vm_net_mock_shop_catalog_item_exists(u32 itemId);
const char *vm_net_mock_shop_catalog_item_name(u32 itemId);
bool vm_net_mock_append_backpack_reward15_object(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
    const vm_net_mock_reward15_item_row *rows, u8 rowCount);
bool vm_net_mock_npc_service_context_has(
    const vm_mock_service_npc_context *context, u16 serviceKind);
const vm_mock_service_npc_context *vm_net_mock_npc_service_context_get(
    const vm_mock_service_client_session *session,
    const vm_net_mock_role_state *role);
bool vm_net_mock_is_npc_service_dialog_request(
    const u8 *request, u32 requestLen, u32 *serviceValueOut);
vm_mock_service_client_session *vm_mock_service_get_active_client_session(void);
const char *vm_mock_service_active_account_id(void);
u32 vm_mock_service_active_client_id(void);
vm_mock_service_client_session *vm_mock_service_find_online_session_by_role_id(
    u32 roleId);
vm_mock_service_client_session *vm_mock_service_find_online_session_by_role_account(
    u32 roleId, const char *accountId);
bool vm_mock_service_is_role_online_by_role_account(u32 roleId,
                                                    const char *accountId);
vm_mock_service_client_session *vm_mock_service_find_online_friend_session(
    const vm_mock_service_friend_record *record);
bool vm_mock_service_session_get_online_view(
    const vm_mock_service_client_session *session,
    vm_mock_service_online_session_view *viewOut);
const char *vm_mock_service_session_account_id(
    const vm_mock_service_client_session *session);
u32 vm_mock_service_collect_visible_session_views(
    const char *scene, u32 excludedClientId,
    vm_mock_service_online_session_view *viewsOut,
    vm_mock_service_client_session **sessionsOut, u32 viewCap);
void vm_mock_service_session_set_arena_challenge_state(
    vm_mock_service_client_session *session, bool initiatorPromptPending,
    bool replyActive, u32 sourceRoleId);
bool vm_mock_service_session_presence_is_recent(
    const vm_mock_service_client_session *session);
bool vm_mock_service_session_enqueue_arena_challenge_notice(
    vm_mock_service_client_session *target,
    const vm_mock_service_client_session *challenger,
    const vm_net_mock_role_state *challengerRole,
    const char *challengerAccountId);
bool vm_mock_service_team_accept_invitation(
    vm_mock_service_client_session *leader,
    vm_mock_service_client_session *joiner,
    vm_mock_service_team_join_result *resultOut);
vm_mock_service_team_invite_status vm_mock_service_team_validate_invitation(
    const vm_mock_service_client_session *source,
    const vm_mock_service_client_session *target);
bool vm_mock_service_session_get_team_invite_reply_context(
    const vm_mock_service_client_session *session,
    vm_mock_service_team_invite_reply_context *contextOut);
void vm_mock_service_session_clear_team_invite_reply_context(
    vm_mock_service_client_session *session);
bool vm_mock_service_enqueue_guild_application_notice(
    const vm_net_mock_guild_application_record *application,
    const vm_net_mock_guild_record *guild, u8 actionType,
    const vm_net_mock_role_state *requester);
vm_mock_service_duel *vm_mock_service_duel_begin(
    vm_mock_service_client_session *inviter,
    vm_mock_service_client_session *responder);
vm_mock_service_duel *vm_mock_service_arena_duel_begin(
    vm_mock_service_client_session *challenger,
    vm_mock_service_client_session *opponent, u32 roomId);
u32 vm_mock_service_duel_serial(const vm_mock_service_duel *duel);
void vm_net_mock_arena_remove_role(u32 roleId, const char *reason);
void vm_net_mock_arena_on_duel_released(u32 roomId, u32 duelSerial);
u32 vm_net_mock_build_pending_arena_initiator_confirm_response(
    u8 *out, u32 outCap, vm_mock_service_client_session *observer);
u32 vm_net_mock_build_arena_response(const u8 *request, u32 requestLen,
                                     u8 *out, u32 outCap);
bool vm_net_mock_mail_claim_commit_in_transaction(u32 scopedRoleId);
bool vm_net_mock_mailbox_prepare_schema(void);
bool vm_net_mock_mailbox_build_dialog(
    vm_net_mock_role_state *role,
    const vm_mock_service_npc_context *serviceContext,
    u32 operation, u32 value, vm_net_mock_mailbox_dialog *view);
u32 vm_net_mock_active_role_id(void);
vm_net_mock_role_state *vm_net_mock_active_role(void);
u32 vm_net_mock_build_ranking_page_response(const u8 *request,
                                             u32 requestLen, u8 *out,
                                             u32 outCap);

bool vm_mock_service_set_resource_dir(const char *resourceDir);
const char *vm_mock_service_resource_dir(void);
int vm_mock_service_run(const char *bindHost, u16 port);
