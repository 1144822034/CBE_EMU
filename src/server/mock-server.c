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

#include "mock_server_catalog.c"
#include "mock_server_role.c"

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
#include "mock_server_social.c"
#include "mock_server_battle.c"
#include "mock_server_interaction_login.c"
#include "mock_server_dispatch.c"
#include "mock_server_transport.c"
