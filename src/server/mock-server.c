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

/* Chest rare-loot world announce lives with chat delivery in
 * mock_server_equipment_npc.c; the open handler in catalog.c calls it.
 * Same-player rapid opens (Pixi 十连) coalesce count into one [世] line.
 * Server-side batch 7/24 flushes one line per rare itemId with total count. */
static void vm_net_mock_gold_chest_maybe_announce_rare_reward(u32 chestItemId,
                                                             u32 rewardItemId);
static void vm_net_mock_gold_chest_announce_rare_reward_count(u32 chestItemId,
                                                             u32 rewardItemId,
                                                             u32 count);
/* Flush overdue 7/15 coalesce pending (also after non-rare opens). */
static void vm_net_mock_gold_chest_announce_flush_if_due(void);

/* Flask reservoir tip: enqueue 1/3/3 system chat from equipment_npc.c. */
static void vm_net_mock_notify_flask_reservoir_system_tip(u32 itemId,
                                                          u32 remaining);

/* After login equipment 7/7: same-packet tip + HP/MP sync (no host overlay file). */
static bool vm_mock_service_session_append_login_equip_wear_sync(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount, u8 equipmentRows);

/* Map HUD vitals: catalog item-use / map-heal update role HP then arm 1/1/14. */
static void vm_mock_service_session_note_map_vitals_changed(const char *reason);

/* Defined in mock_server_social.c; battle-insight 25/6|25/7 may ACK a same-WT
 * 1/2/10 companion from catalog handlers compiled earlier in this TU. */
static bool vm_net_mock_append_actor_other_empty10_object(u8 *out, u32 outCap,
                                                          u32 *pos);

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

/* Pixi arena helpers (defined in mock_server_arena.c); team/create + login
 * rebind call sites live earlier in equipment_npc.c. */
struct vm_mock_service_client_session;
static bool vm_mock_arena_team_forbidden_for_session(
    const struct vm_mock_service_client_session *session);
static void vm_mock_arena_rebind_session_if_needed(
    struct vm_mock_service_client_session *session,
    vm_net_mock_role_state *role);
static void vm_mock_arena_on_session_offline(
    struct vm_mock_service_client_session *session);
static void vm_mock_arena_restore_venue_scene_if_needed(
    vm_net_mock_role_state *role);

/* Implemented in mock_server_battle.c; team_remove_member runs earlier. */
static void vm_mock_service_team_battle_note_roster_disconnect(
    void *team,
    struct vm_mock_service_client_session *leaver,
    const char *reason);

#include "mock_server_equipment_npc.c"
#include "mock_server_cosmetics.c"
#include "mock_server_recovered_helpers.inc"
#include "mock_server_scene_task.c"

static void vm_mock_service_maintenance_tick(void);
bool vm_mock_service_maintenance_blocks_login(void);
const char *vm_mock_service_maintenance_login_message_gbk(void);
void vm_mock_maintenance_admin_get(u8 *activeOut, u32 *lastNoticeUnixOut,
                                   u8 *kickDoneOut, u32 *kickAtUnixOut,
                                   u32 *onlineCountOut, u32 *secondsLeftOut);
bool vm_mock_maintenance_admin_start(char *msgOut, size_t msgCap);
bool vm_mock_maintenance_admin_stop(char *msgOut, size_t msgCap);

#include "mock_server_maintenance.c"

/* World boss + ladder (after scene_task combat spawn helpers). */
static void vm_mock_world_boss_tick(void);
bool vm_mock_world_boss_try_claim_encounter(const char *scene, u32 wireId,
                                            u32 posx, u32 posy,
                                            u32 *enemyIdOut);
void vm_mock_world_boss_on_battle_end(bool victory);
static u32 vm_net_mock_build_ladder_protocol_response(const u8 *request,
                                                      u32 requestLen, u8 *out,
                                                      u32 outCap);
bool vm_mock_ladder_battle_is_active(void);
u32 vm_mock_ladder_damage_scale_percent(void);
void vm_mock_ladder_on_battle_end(bool victory);
bool vm_mock_ladder_npc_offers(const char *npcName);

#include "mock_server_world_boss.c"
#include "mock_server_ladder.c"

static void vm_mock_guess_tick(void);
static u32 vm_net_mock_build_guess_protocol_response(const u8 *request,
                                                     u32 requestLen, u8 *out,
                                                     u32 outCap);
static u32 vm_net_mock_build_fruit_protocol_response(const u8 *request,
                                                     u32 requestLen, u8 *out,
                                                     u32 outCap);

static bool vm_mock_festival_npc_offers(const char *npcName);
static bool vm_mock_yanmen_npc_offers(const char *npcName);
static bool vm_mock_ring_npc_offers(const char *npcName);
static void vm_mock_activities_arena_round_vitality(vm_mock_service_duel *duel);
static void vm_mock_activities_vitality_wire(vm_net_mock_role_state *role,
                                             u32 *energyOut, u32 *energyMaxOut);
static void vm_mock_activities_on_pve_victory(const char *scene, u32 enemyId,
                                              bool isBoss);
static void vm_mock_activities_on_pvp_kill(vm_net_mock_role_state *role,
                                           const char *accountId, u8 victimJob,
                                           u32 victimLevel);
static void vm_mock_activities_on_kill_progress(u32 enemyId, bool isBoss,
                                                const char *reason);
static bool vm_mock_ring_collect_material_remaining(u32 roleId, u32 itemId,
                                                    u32 *remainingOut);
static void vm_mock_activities_on_npc_talk(const char *npcName);
static void vm_mock_activities_on_world_chat(const char *accountId, u32 roleId,
                                             const char *messageGbk);
static const char *vm_mock_festival_welcome_text(void);
static u32 vm_net_mock_build_activities_protocol_response(const u8 *request,
                                                          u32 requestLen,
                                                          u8 *out, u32 outCap);
static u32 vm_net_mock_build_ring_protocol_response(const u8 *request,
                                                    u32 requestLen, u8 *out,
                                                    u32 outCap);

#include "mock_server_activities.c"

#include "mock_server_guess.c"
#include "mock_server_fruit.c"

/* Pixi arena (defined in mock_server_arena.c after guild). */
static bool vm_mock_arena_npc_offers_entry(const char *npcName);
static bool vm_mock_arena_wager_npc_offers(const char *npcName);
static bool vm_mock_arena_is_arena_scene(const char *scene);
static bool vm_mock_arena_peer_visible(
    const vm_mock_service_client_session *observer,
    const vm_mock_service_client_session *peer);
static bool vm_mock_arena_spar_forbidden_on_scene(const char *scene);
static bool vm_mock_arena_entry_window_open(void);
/* team_forbidden / rebind already declared before equipment_npc. */
static void vm_mock_arena_tick(void);
static void vm_mock_arena_catchup_phase_tip_session(
    vm_mock_service_client_session *session);
static void vm_mock_arena_on_duel_finished(vm_mock_service_duel *duel);
static void vm_mock_arena_on_wager_duel_finished(vm_mock_service_duel *duel);
static bool vm_net_mock_append_wager_event_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_arena_hud_object(u8 *out, u32 outCap, u32 *pos);
/* Solo venue polls still need 41/5 (and settle clear) — see idle fast-path. */
static bool vm_mock_arena_poll_needs_body(vm_mock_service_client_session *session);
static u32 vm_net_mock_build_arena_enter_response(u8 *out, u32 outCap);
static u32 vm_net_mock_build_arena_leave_response(u8 *out, u32 outCap);
static const char *vm_mock_arena_enter_refuse_tip(void);
static u32 vm_net_mock_build_arena_protocol_response(const u8 *request,
                                                      u32 requestLen, u8 *out,
                                                      u32 outCap);
static void vm_mock_auction_tick(void);
static bool vm_net_mock_append_auction_event_object(u8 *out, u32 outCap, u32 *pos);
static u32 vm_net_mock_build_auction_protocol_response(const u8 *request,
                                                       u32 requestLen, u8 *out,
                                                       u32 outCap);
/* Pixi enmity (kind=43); defined in mock_server_enmity.c after auction. */
static void vm_mock_enmity_record_duel_result(vm_mock_service_duel *duel);
static u32 vm_net_mock_build_enmity_protocol_response(const u8 *request,
                                                      u32 requestLen, u8 *out,
                                                      u32 outCap);

#include "mock_server_scene_sync.c"
#include "mock_server_guild.c"
#include "mock_server_arena.c"
#include "mock_server_auction.c"
#include "mock_server_enmity.c"
#include "mock_server_social.c"
#include "mock_server_marriage.c"
#include "mock_server_battle.c"
#include "mock_server_interaction_login.c"
#include "mock_server_dispatch.c"
#include "mock_server_transport.c"
