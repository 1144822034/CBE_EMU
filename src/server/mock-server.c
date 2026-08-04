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

/* Gold-chest rare-loot world announce lives with chat delivery in
 * mock_server_equipment_npc.c; the open handler in catalog.c calls it. */
static void vm_net_mock_gold_chest_maybe_announce_rare_reward(u32 itemId);

/* Flask reservoir tip: enqueue 1/3/3 system chat from equipment_npc.c. */
static void vm_net_mock_notify_flask_reservoir_system_tip(u32 itemId,
                                                          u32 remaining);

/* After login equipment 7/7: same-packet aggregated live wear attrs. */
static bool vm_mock_service_session_append_login_equip_wear_sync(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount, u8 equipmentRows);

/* Publish aggregated panel attrs for host realign (login has no FA wear). */
static void vm_net_mock_publish_panel_attr_overlay(
    const vm_net_mock_player_stats *stats);

#include "mock_server_catalog.c"

/* Sync MySQL writers in role.c (position / full role_db_save) must invalidate
 * in-flight deferred persist jobs owned by account_state in equipment_npc.c. */
static void vm_mock_service_account_note_durable_role_write(
    const char *accountId,
    const vm_net_mock_role_db_file *db,
    bool dbValid,
    const vm_net_mock_warehouse_state *warehouse,
    bool durableInventoryCommitted,
    bool durablePositionCommitted,
    const char *reason);

#include "mock_server_role.c"

/* Death recovery owns the role mutation in mock_server_equipment_npc.c, while
 * the destination is derived from the sMap/wMap topology and SCE resources in
 * mock_server_scene_task.c.  Keep this narrow declaration here so both pieces
 * remain in their proper business module despite the single aggregation unit. */
static bool vm_net_mock_resolve_nearest_teleport_stone_respawn(
    const char *fromScene, char *sceneOut, size_t sceneOutCap,
    u16 *xOut, u16 *yOut, u32 *sourceSmapRowOut, u32 *targetSmapRowOut,
    u32 *distanceOut, const char **routeOut);
static void vm_net_mock_hangup_loop_clear(const char *reason);
static void vm_mock_service_clear_request_local_scratch(const char *reason);

/* Dream kind=3 hot-reenter: stock SCE install lives in scene_task.c; session
 * offline restore is called from equipment_npc.c (included first). */
static void vm_net_mock_dream_sce_hot_reenter_restore_admin(void);
/* Heal web/fs↔bin 29* SCE twin drift before WT18/7 publish (equipment_npc). */
static void vm_net_mock_dream_sce_heal_authoritative_kind3(const char *scene);

#include "mock_server_equipment_npc.c"
#include "mock_server_recovered_helpers.inc"
#include "mock_server_scene_task.c"
#include "mock_server_scene_sync.c"
#include "mock_server_guild.c"
#include "mock_server_social.c"
#include "mock_server_battle.c"
#include "mock_server_interaction_login.c"
#include "mock_server_dispatch.c"
#include "mock_server_transport.c"
