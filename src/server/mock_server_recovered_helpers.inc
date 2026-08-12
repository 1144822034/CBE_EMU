/* Recovered helpers wiped by accidental git checkout during a1d304 cleanup.
 * Included at end of mock-server.c after all feature fragments. */

static u32 vm_net_mock_npc_service_mask_from_kind(u16 kind)
{
    if (kind == 0 || kind > VM_NET_MOCK_NPC_KIND_MAX)
        return 0;
    return 1u << kind;
}

static u16 vm_net_mock_npc_primary_kind_from_mask(u32 mask)
{
    u32 bits = mask & VM_NET_MOCK_NPC_SERVICE_MASK_ALL;

    for (u16 kind = 1; kind <= VM_NET_MOCK_NPC_KIND_MAX; ++kind)
    {
        if ((bits & (1u << kind)) != 0)
            return kind;
    }
    return 0;
}

static void vm_net_mock_npc_normalize_service_fields(
    vm_net_mock_scene_npcinfo_seed *seed)
{
    u32 mask = 0;

    if (seed == NULL)
        return;
    mask = seed->serviceMask & VM_NET_MOCK_NPC_SERVICE_MASK_ALL;
    if (mask == 0)
        mask = vm_net_mock_npc_service_mask_from_kind(seed->kind);
    seed->serviceMask = mask;
    seed->kind = vm_net_mock_npc_primary_kind_from_mask(mask);
}

static u32 vm_net_mock_npc_effective_service_mask(
    const vm_net_mock_scene_npcinfo_seed *seed)
{
    if (seed == NULL)
        return 0;
    if ((seed->serviceMask & VM_NET_MOCK_NPC_SERVICE_MASK_ALL) != 0)
        return seed->serviceMask & VM_NET_MOCK_NPC_SERVICE_MASK_ALL;
    return vm_net_mock_npc_service_mask_from_kind(seed->kind);
}

static bool vm_net_mock_npc_has_service(const vm_net_mock_scene_npcinfo_seed *seed,
                                         u16 kind)
{
    if (kind == 0 || kind > VM_NET_MOCK_NPC_KIND_MAX)
        return false;
    return (vm_net_mock_npc_effective_service_mask(seed) & (1u << kind)) != 0;
}

static bool vm_mock_service_session_trade_blocks_invite(u32 clientId)
{
    vm_mock_service_trade *trade = vm_mock_service_trade_find_for_client(clientId,
                                                                         NULL);
    return trade != NULL;
}

static bool vm_mock_service_session_duel_blocks_spar(u32 clientId)
{
    vm_mock_service_duel *duel = vm_mock_service_duel_find_for_client(clientId,
                                                                      NULL);
    return duel != NULL && duel->active;
}

static bool vm_mock_service_team_battle_in_progress(const vm_mock_service_team *team)
{
    return team != NULL && team->battleActive && !team->battleFinished;
}

/*
 * team_battle_release_if_settled lives in mock_server_battle.c (needs
 * absent_mask + undelivered event scan).  A prior stub here cleared
 * battleActive as soon as battleFinished flipped, which stranded shared
 * 4/6 before peer poll delivery
 * (docs/re/2026-08-05-team-battle-terminal-synth-exit-starve.md).
 */

static void vm_mock_service_duel_clear_finished_hold_for_client(
    u32 clientId,
    const char *reason)
{
    vm_mock_service_duel *duel = vm_mock_service_duel_find_for_client(clientId,
                                                                      NULL);
    (void)reason;
    if (duel != NULL && !duel->active)
        memset(duel, 0, sizeof(*duel));
}

static void vm_mock_service_clear_inflight_scene_transfer_globals(const char *reason)
{
    bool had =
        g_vm_net_mock_last_scene_change_target_valid ||
        g_vm_net_mock_teleport_stone_deferred_enter_valid ||
        g_vm_net_mock_teleport_stone_direct_enter_pending ||
        g_vm_net_mock_teleport_stone_map_enter_pending ||
        g_vm_net_mock_teleport_stone_confirm_target_valid ||
        g_vm_net_mock_task_transport_pending_valid ||
        g_vm_net_mock_scene_moveinfo_npc_pending ||
        g_vm_net_mock_scene_moveinfo_npc_wait_post_enter ||
        g_vm_net_mock_scene_moveinfo_npc_wait_wt6 ||
        g_vm_net_mock_scene_moveinfo_npc_seeded ||
        g_vm_net_mock_title_role_scene_followup_pending ||
        g_vm_net_mock_update_completed_reenter_pending;

    g_vm_net_mock_last_scene_change_target_valid = false;
    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
    g_vm_net_mock_teleport_stone_direct_enter_pending = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    g_vm_net_mock_teleport_stone_confirm_target_valid = false;
    if (g_vm_net_mock_teleport_stone_deferred_enter_client_id == 0 ||
        g_vm_mock_service_active_client_id == 0 ||
        g_vm_net_mock_teleport_stone_deferred_enter_client_id ==
            g_vm_mock_service_active_client_id)
    {
        g_vm_net_mock_teleport_stone_deferred_enter_valid = false;
        g_vm_net_mock_teleport_stone_deferred_enter_tick = 0;
        g_vm_net_mock_teleport_stone_deferred_enter_client_id = 0;
    }
    g_vm_net_mock_task_transport_pending_valid = false;
    g_vm_net_mock_task_transport_pending_task_id = 0;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    g_vm_net_mock_title_role_scene_followup_pending = false;
    g_vm_net_mock_update_completed_reenter_pending = false;
    g_vm_net_mock_update_completed_name[0] = 0;
    g_vm_net_mock_scene_moveinfo_npc_pending = false;
    g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
    g_vm_net_mock_scene_moveinfo_npc_wait_post_enter = false;
    g_vm_net_mock_scene_moveinfo_npc_wait_wt6 = false;
    g_vm_net_mock_scene_moveinfo_npc_seeded = false;
    g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] = 0;

    if (!had)
        return;
    printf("[info][mock-service] inflight_scene_transfer_clear reason=%s "
           "evidence=offline-capture-must-not-restore-mid-transfer\n",
           reason ? reason : "-");
}

#ifndef VM_NET_MOCK_BATTLE_DAMAGE_VARIANCE_PERCENT
#define VM_NET_MOCK_BATTLE_DAMAGE_VARIANCE_PERCENT 5u
#endif
#ifndef VM_NET_MOCK_BATTLE_SKILL_DAMAGE_VARIANCE_PERCENT
#define VM_NET_MOCK_BATTLE_SKILL_DAMAGE_VARIANCE_PERCENT 8u
#endif

static u32 vm_net_mock_battle_apply_damage_variance(u32 damage)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 variancePct = combatCfg ? combatCfg->naVariancePercent : 5u;
    u32 span;
    u32 roll;
    u32 pct;
    uint64_t scaled;

    if (damage == 0)
        return 0;
    if (vm_net_mock_battle_combat_rolls_disabled())
        return damage;
    if (variancePct > 50u)
        variancePct = 50u;
    span = variancePct * 2u + 1u;
    roll = vm_net_mock_battle_combat_rand() % span;
    pct = (100u - variancePct) + roll;
    scaled = ((uint64_t)damage * (uint64_t)pct + 50ull) / 100ull;
    if (scaled == 0)
        scaled = 1;
    return scaled > 0xffffffffull ? 0xffffffffu : (u32)scaled;
}

static u32 vm_net_mock_battle_apply_skill_damage_variance(u32 damage)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 variancePct = combatCfg ? combatCfg->skillVariancePercent : 8u;
    u32 span;
    u32 roll;
    u32 pct;
    uint64_t scaled;

    if (damage == 0)
        return 0;
    if (vm_net_mock_battle_combat_rolls_disabled())
        return damage;
    if (variancePct > 50u)
        variancePct = 50u;
    span = variancePct * 2u + 1u;
    roll = vm_net_mock_battle_combat_rand() % span;
    pct = (100u - variancePct) + roll;
    scaled = ((uint64_t)damage * (uint64_t)pct + 50ull) / 100ull;
    if (scaled == 0)
        scaled = 1;
    return scaled > 0xffffffffull ? 0xffffffffu : (u32)scaled;
}

static u32 vm_net_mock_battle_job_skill_primary_bonus_percent(u32 job)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();

    if (job == 3)
        return combatCfg ? combatCfg->duelJobBonusGuidaoPercent : 12u;
    if (job == 2)
        return combatCfg ? combatCfg->duelJobBonusHuanjianPercent : 8u;
    return combatCfg ? combatCfg->duelJobBonusTianjiPercent : 10u;
}

static bool vm_net_mock_battle_skill_is_single_target_offensive(
    const vm_net_mock_skill_catalog_item *skill)
{
    return skill != NULL && skill->targetDirection == 3;
}

static u32 vm_net_mock_battle_soft_boost_single_skill_vs_na(
    u32 damage,
    u32 naReference,
    const vm_net_mock_skill_catalog_item *skill)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 floorPct = combatCfg ? combatCfg->skillVsNaFloorPercent : 90u;
    u32 target;

    (void)skill;
    if (damage == 0)
        return 0;
    if (naReference == 0)
        return damage;
    target = (naReference * floorPct + 50u) / 100u;
    if (target < 1u)
        target = 1u;
    if (damage < target)
        damage = target;
    return damage;
}
