/*
 * Jianghu OL mock service aggregation unit.
 *
 * The server retains one C translation unit because its protocol handlers share
 * deliberate `static` state and forward declarations.  Business code lives in
 * the smaller adjacent fragments below; their order is the former source order
 * and is therefore part of the server's initialization/dispatch contract.
 */

#include "mock_server_core.c"

/* The scene-runtime stream owns its 16/3 context and catalog objects, but an
 * explicitly independent companion can still use the generic one-object
 * dispatcher.  The implementation appears after the feature handlers in the
 * dispatch fragment; keep this narrow declaration at the aggregation
 * boundary so the scene fragment cannot accidentally invoke arbitrary packet
 * handling. */
static bool vm_net_mock_append_independent_single_object_response(
    const vm_net_mock_request_object *object, u8 *out, u32 outCap,
    u32 *pos, u8 *objectCount);
static bool vm_net_mock_object_is_independent_combo_candidate(
    const vm_net_mock_request_object *object);
static bool vm_net_mock_is_scene_runtime_position_ack_16_3_object(
    const vm_net_mock_request_object *object, u16 *positionXOut);

/* The service owns the progression rule for equipped state.  The catalog
 * serializes equipment before the role module is included, so keep this
 * narrow predicate declaration at the aggregation boundary. */
static bool vm_net_mock_role_equipment_slot_is_usable(
    const vm_net_mock_role_state *role, u32 slot);

/* A dynamic NPC instance is active only for its owning client session.  Role
 * movement is parsed before the session fragment is included, so declare the
 * narrow ownership bridge at the aggregation boundary. */
static bool vm_mock_service_active_transient_instance_update_position(
    const char *scene, u16 x, u16 y, const char *reason);
static void vm_mock_service_active_transient_instance_clear_if_departing(
    const char *scene, const char *reason);

/* Arena rooms are transient online activity state, like team membership.  The
 * session module calls this during offline teardown while the role identity
 * is still available; the implementation lives with the arena protocol. */
static void vm_net_mock_arena_remove_role(u32 roleId, const char *reason);

/* A duel is released only after both clients consumed its terminal packets.
 * Arena owns the room-level round count, so it receives that precise battle
 * lifecycle edge rather than guessing completion from a scene poll. */
static void vm_net_mock_arena_on_duel_released(u32 roomId, u32 duelSerial);

/* The canonical monster identity set is built from the shipped SCE2 combat
 * nodes after their parser is available.  Role persistence owns the default
 * stats/overrides, while the scene fragment owns the resource scan. */
static void vm_net_mock_monster_catalog_ensure_loaded(void);

/* The role layer owns the atomic equipment-drop replacement, while scene
 * discovery owns the authoritative display names and the assembled admin
 * catalog.  This narrow declaration keeps that operation data-driven without
 * moving persistence into the web layer. */
typedef struct vm_net_mock_monster_admin_row vm_net_mock_monster_admin_row;
static void vm_net_mock_monster_resource_labels_load(void);
static u32 vm_net_mock_monster_admin_list(
    vm_net_mock_monster_admin_row *rows, u32 rowCap);

/* Content deployment validates the Actor/GIF dependency set before its SCE2
 * kind-3 record can reach a clean client cache.  The web
 * administration implementation owns the existing resource inspector; this
 * declaration keeps scene-content assembly in the scene module. */
enum {
    VM_NET_MOCK_ACTOR_RESOURCE_IMAGE_MAX = 16
};
static bool vm_net_mock_ensure_actor_resource_available(
    const char *actorResource, const char **errorOut);
static bool vm_net_mock_actor_resource_collect_images(
    const char *actorResource,
    char imageNames[VM_NET_MOCK_ACTOR_RESOURCE_IMAGE_MAX][64],
    u32 *imageCountOut);
/* Runtime Actor motion descriptors are not serialized in the editor .actor
 * manifest.  Keep the capacity hook narrow until a runtime allocation trace
 * proves a stable file-to-child-node mapping. */
static bool vm_net_mock_actor_scene_node_reserve(
    const char *actorResource, u32 *reserveOut, const char **errorOut);

/* A fresh mmGame bootstrap can discard client-side NPC nodes without being a
 * scene enter.  The catalog arms this narrow session marker so the next
 * scene task/resource response replays 27/11 without emitting 30/2. */
static void vm_mock_service_mark_backpack_bootstrap_npc_reseed_pending(
    const char *source);

/* Chest configuration belongs to the catalog, while durable world-chat
 * storage and live delivery belong to the social service.  A successful
 * chest opening calls this narrow bridge only after the role mutation has
 * committed, so an uncommitted draw can never become a public announcement. */
static bool vm_mock_world_chat_publish_chest_reward(
    const char *openerName, u32 chestItemId, const char *chestNameGbk,
    u32 rewardItemId, const char *rewardNameGbk, u32 rewardCount);

/* The web-admin module owns the shared append-only operation-log table, but
 * successful in-game W-coin debits must be auditable in that same history.
 * Keep the declaration narrow at the aggregation boundary; callers invoke it
 * only after their own authoritative debit transaction has committed. */
static bool vm_mock_admin_operation_log_record(
    const char *actionCode, const char *accountId, u32 roleId, u32 itemId,
    u32 itemCount, u32 changeAmount, const char *detail,
    const char *operatorAccountId);

#include "mock_server_catalog.c"
#include "mock_server_role.c"
#include "mock_server_ranking.c"

/* Death recovery owns the role mutation in mock_server_equipment_npc.c, while
 * the nearest safe local scene or town fallback is derived from sMap/wMap topology and SCE
 * resources in mock_server_scene_task.c.  Keep this narrow declaration here
 * so both pieces remain in their proper business module despite the single
 * aggregation unit. */
static bool vm_net_mock_resolve_nearest_safe_respawn(
    const char *fromScene, char *sceneOut, size_t sceneOutCap,
    u16 *xOut, u16 *yOut, u32 *sourceSmapRowOut, u32 *targetSmapRowOut,
    u32 *distanceOut, const char **routeOut);
static bool vm_net_mock_adjust_recovery_landing_to_map_safe(
    const char *scene, u16 *x, u16 *y);
/* The scene-startup recovery must distinguish a standalone WT25/5 from the
 * same object embedded in a task/runtime composite. The detector is owned by
 * the later social fragment, so declare that narrow existing contract here. */
static bool vm_net_mock_is_short_wt_control_packet(
    const u8 *request, u32 requestLen, u8 kind, u8 subtype);

#include "mock_server_equipment_npc.c"
#include "mock_server_mailbox.c"
#include "mock_server_scene_task.c"
#include "mock_server_scene_sync.c"
#include "mock_server_guild.c"

static bool vm_net_mock_battle_pending_settlement_is_deliverable(
    const vm_mock_service_client_session *observer);
/* A completed scene-monster battle reaches the scene only after the native
 * 25/5 close acknowledgement.  The battle fragment owns the continuation
 * state transition; social owns that acknowledgement and the poll channel. */
static void vm_net_mock_battle_on_scene_default_event(void);
static void vm_net_mock_scene_hangup_on_scene_default_event(void);
static bool vm_net_mock_duel_on_scene_default_event(void);
static u32 vm_net_mock_build_pending_scene_hangup_battle_response(
    u8 *out, u32 outCap, vm_mock_service_client_session *observer);

/* The task-hall room confirmation itself cannot arm the in-game module's
 * native 30/9 -> 30/10 battle-entry edge.  Arena owns the room lease; social
 * owns the scene-sync poll that delivers this strictly scene-owned prompt. */
static u32 vm_net_mock_build_pending_arena_initiator_confirm_response(
    u8 *out, u32 outCap, vm_mock_service_client_session *observer);

#include "mock_server_social.c"
#include "mock_server_battle.c"
#include "mock_server_arena.c"
#include "mock_server_interaction_login.c"
#include "mock_server_dispatch.c"
#include "mock_server_transport.c"
