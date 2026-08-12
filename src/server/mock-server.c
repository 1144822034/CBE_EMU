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

/* Content deployment validates the Actor/GIF dependency set before its SCE2
 * kind-3 record can reach a clean client cache.  The web
 * administration implementation owns the existing resource inspector; this
 * declaration keeps scene-content assembly in the scene module. */
static bool vm_net_mock_ensure_actor_resource_available(
    const char *actorResource, const char **errorOut);

/* Chest configuration belongs to the catalog, while durable world-chat
 * storage and live delivery belong to the social service.  A successful
 * chest opening calls this narrow bridge only after the role mutation has
 * committed, so an uncommitted draw can never become a public announcement. */
static bool vm_mock_world_chat_publish_chest_reward(
    const char *openerName, u32 chestItemId, const char *chestNameGbk,
    u32 rewardItemId, const char *rewardNameGbk, u32 rewardCount);

#include "mock_server_catalog.c"
#include "mock_server_role.c"
#include "mock_server_ranking.c"

/* Death recovery owns the role mutation in mock_server_equipment_npc.c, while
 * the destination is derived from the sMap/wMap topology and SCE resources in
 * mock_server_scene_task.c.  Keep this narrow declaration here so both pieces
 * remain in their proper business module despite the single aggregation unit. */
static bool vm_net_mock_resolve_nearest_teleport_stone_respawn(
    const char *fromScene, char *sceneOut, size_t sceneOutCap,
    u16 *xOut, u16 *yOut, u32 *sourceSmapRowOut, u32 *targetSmapRowOut,
    u32 *distanceOut, const char **routeOut);

#include "mock_server_equipment_npc.c"
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
static u32 vm_net_mock_build_pending_scene_hangup_battle_response(
    u8 *out, u32 outCap, vm_mock_service_client_session *observer);

#include "mock_server_social.c"
#include "mock_server_battle.c"
#include "mock_server_arena.c"
#include "mock_server_interaction_login.c"
#include "mock_server_dispatch.c"
#include "mock_server_transport.c"
