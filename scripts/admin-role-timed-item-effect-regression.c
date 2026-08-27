/*
 * Isolated contract regression for the role-operation timed-item page.  It
 * renders a durable effect snapshot without opening MySQL, then checks the
 * visible controls, audit label and SPA dialog restoration marker.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    vm_net_mock_role_state role;
    vm_mock_admin_role_timed_effect_list effects;
    vm_mock_admin_text page;
    char rendered[32768];
    char bonus[64];

    memset(&role, 0, sizeof(role));
    memset(&effects, 0, sizeof(effects));
    memset(rendered, 0, sizeof(rendered));
    memset(bonus, 0, sizeof(bonus));
    role.roleId = 37;
    effects.count = 1;
    effects.rows[0].kind = VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT;
    effects.rows[0].itemId = 828;
    effects.rows[0].multiplier = 20;
    effects.rows[0].expiresUnix = 4294967295u;
    vm_mock_admin_role_timed_effect_bonus_text(
        VM_NET_MOCK_ROLE_ITEM_EFFECT_COMBAT_PILL, 50, bonus, sizeof(bonus));
    if (strcmp(vm_mock_admin_role_timed_effect_label(
                   VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD),
               "修炼丹／经验卡") != 0 ||
        strcmp(vm_mock_admin_role_timed_effect_label(
                   VM_NET_MOCK_ROLE_ITEM_EFFECT_COMBAT_PILL),
               "大力丸／神力丸") != 0 ||
        strcmp(vm_mock_admin_role_timed_effect_label(
                   VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT),
               "战斗心得") != 0 ||
        strcmp(bonus, "攻击、防御 +50%") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label(
                   "remove-role-timed-item-effect"),
               "移除时效道具效果") != 0)
    {
        fprintf(stderr, "timed item effect labels regressed\n");
        return 1;
    }

    vm_mock_admin_text_init(&page, rendered, sizeof(rendered));
    vm_mock_admin_render_role_operation_modal(
        &page, "role.ops", &role, "操作测试角色", &effects, true, NULL, 0);
    if (page.truncated ||
        strstr(rendered,
               "data-role-operation-tab=\"timed-effects\"") == NULL ||
        strstr(rendered,
               "data-role-operation-pane=\"timed-effects\"") == NULL ||
        strstr(rendered, "data-role-timed-effect-list") == NULL ||
        strstr(rendered,
               "name=\"action\" value=\"remove-role-timed-item-effect\"") ==
            NULL ||
        strstr(rendered, "name=\"effect_kind\" value=\"3\"") == NULL ||
        strstr(rendered, "name=\"item\" value=\"828\"") == NULL ||
        strstr(rendered, "name=\"multiplier\" value=\"20\"") == NULL ||
        strstr(rendered, "name=\"expires_unix\" value=\"4294967295\"") ==
            NULL ||
        strstr(rendered, "战斗经验 +20%") == NULL ||
        strstr(g_vm_mock_admin_script, "state.pending") == NULL ||
        strstr(g_vm_mock_admin_script, "state.restore") == NULL ||
        strstr(g_vm_mock_admin_script,
               "form.dataset.adminSpaSubmitting!=='1'") == NULL)
    {
        fprintf(stderr, "timed item effect operation page regressed\n");
        return 1;
    }

    puts("admin role timed item effect regression passed");
    return 0;
}
