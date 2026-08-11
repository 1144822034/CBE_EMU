/*
 * Pure server/resource regression for two battle-response contracts:
 *
 * - The empty WT 4/12 auto request replays an ordinary Operate.  A known
 *   spell whose current MP is below its skill.dsh cost must be normalized to
 *   Operate=0 before the 4/6 action is encoded.
 * - 神仙壶 (HP) and 逍遥壶 (MP) are both recovery items.  Their 4/6 effect
 *   must use the eidolon recovery actor f_renew1.actor (回复.gif), while
 *   803 keeps the client-native type-2 item action.
 *
 * This test starts neither a server nor a client and never opens MySQL.  It
 * includes the production translation unit so the exact helpers used by the
 * 4/2 and 4/3 response builders are exercised against an isolated in-memory
 * role and the checked-in JHOnlineData resources.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static int prepare_test_role(vm_net_mock_role_state **roleOut)
{
    vm_net_mock_role_state *role = NULL;

    if (roleOut != NULL)
        *roleOut = NULL;
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 601001;
    role = &g_vm_net_mock_role_db.roles[0];
    role->roleId = 601001;
    role->job = 2;
    role->level = 1;
    role->hp = 120;
    role->hpMax = 120;
    role->mp = 4;
    role->mpMax = 100;
    g_mockBattleRoleMpCurrent = 0;
    g_mockBattleRoleMpMax = 0;
    if (roleOut != NULL)
        *roleOut = role;
    return 0;
}

/* The battle item builder must not select a different Eidolon merely because
 * a restorative item affects MP instead of HP.  Exercise every ordinary
 * item.dsh recovery row rather than using only the two reservoir flasks. */
static int verify_all_battle_recovery_item_effects(u32 expectedEffect)
{
    u32 total = vm_net_mock_load_item_effect_catalog();
    u32 recoveryRows = 0;
    u32 reservoirRows = 0;

    for (u32 i = 0; i < total; ++i)
    {
        const vm_net_mock_item_effect_catalog_item *item =
            &g_vm_net_mock_item_effect_catalog[i];

        if (item->category != 10 || (item->hp == 0 && item->mp == 0))
            continue;
        if (!vm_net_mock_item_effect_is_usable(item) ||
            vm_net_mock_battle_item_effect_index(item->hp, item->mp) != expectedEffect)
        {
            fprintf(stderr,
                    "recovery item effect mismatch: item=%u hp=%u mp=%u effect=%u expected=%u\n",
                    item->itemId, item->hp, item->mp,
                    vm_net_mock_battle_item_effect_index(item->hp, item->mp),
                    expectedEffect);
            return 1;
        }
        if (item->consumeMode == 2)
            ++reservoirRows;
        ++recoveryRows;
    }

    /* Current checked-in item.dsh: 5 HP, 5 MP, 3 mixed and 2 reservoirs. */
    if (recoveryRows != 15 || reservoirRows != 2)
    {
        fprintf(stderr, "unexpected item.dsh recovery inventory: rows=%u reservoirs=%u\n",
                recoveryRows, reservoirRows);
        return 1;
    }
    return 0;
}

int main(void)
{
    const vm_net_mock_skill_catalog_item *skill = NULL;
    vm_net_mock_role_state *role = NULL;
    u32 operate = 203; /* skill.dsh row 201, whose action value is id + 2. */
    u32 before = 0;
    u32 after = 0;
    u32 cost = 0;
    u32 recoveryEffect = 0;

    if (!vm_net_mock_set_resource_dir("web/fs/JHOnlineData"))
    {
        fputs("unable to select isolated server resource root\n", stderr);
        return 1;
    }
    if (prepare_test_role(&role) != 0 || role == NULL)
        return 1;
    skill = vm_net_mock_battle_operate_skill(operate);
    if (skill == NULL || skill->skillId != 201 || skill->mpCost == 0)
    {
        fprintf(stderr, "expected skill 201 with a positive MP cost, got id=%u cost=%u\n",
                skill ? skill->skillId : 0, skill ? skill->mpCost : 0);
        return 1;
    }
    if (role->mp >= skill->mpCost ||
        vm_net_mock_battle_prepare_skill_mp(operate, &before, &after, &cost) ||
        before != role->mp || after != role->mp || cost != skill->mpCost)
    {
        fprintf(stderr,
                "low-MP eligibility contract mismatch: before=%u after=%u cost=%u role_mp=%u\n",
                before, after, cost, role->mp);
        return 1;
    }
    if (!vm_net_mock_battle_fallback_unaffordable_skill(&operate, "regression") ||
        operate != 0)
    {
        fprintf(stderr, "unaffordable skill was not normalized to Operate=0\n");
        return 1;
    }
    /* Exactly enough MP is valid: use the skill and spend the full cost. */
    role->mp = skill->mpCost;
    g_mockBattleRoleMpCurrent = 0;
    g_mockBattleRoleMpMax = 0;
    operate = skill->skillId + 2;
    if (!vm_net_mock_battle_prepare_skill_mp(operate, &before, &after, &cost) ||
        before != skill->mpCost || after != 0 || cost != skill->mpCost)
    {
        fprintf(stderr,
                "exact-MP eligibility contract mismatch: before=%u after=%u cost=%u\n",
                before, after, cost);
        return 1;
    }

    recoveryEffect = vm_net_mock_battle_item_effect_index(0, 50000);
    if (recoveryEffect == 0 ||
        recoveryEffect != vm_net_mock_battle_item_effect_index(50000, 0))
    {
        fprintf(stderr, "HP/MP recovery effect contract mismatch: effect=%u\n",
                recoveryEffect);
        return 1;
    }
    if (verify_all_battle_recovery_item_effects(recoveryEffect) != 0)
        return 1;
    printf("battle-auto-mp-flask-effect-v1 passed: skill=%u cost=%u "
           "low_mp=%u fallback=physical exact_mp=%u after=%u effect=%u recovery_rows=15\n",
           skill->skillId, skill->mpCost, 4u, skill->mpCost, after,
           recoveryEffect);
    return 0;
}
