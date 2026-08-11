/*
 * Regression for the chest-admin editor response budget.
 *
 * It fills all three chest pools to their supported 120 rows, then renders
 * the golden-chest page through the normal admin renderer.  The renderer must
 * emit exactly the selected editor, not two hidden forms that consume the
 * response allocation.  No listener, MySQL connection or persistent state
 * is used: the test seeds only process-local catalog globals.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static u32 count_occurrences(const char *text, const char *needle)
{
    u32 count = 0;
    size_t needleLen = needle ? strlen(needle) : 0;

    if (text == NULL || needleLen == 0)
        return 0;
    while ((text = strstr(text, needle)) != NULL)
    {
        ++count;
        text += needleLen;
    }
    return count;
}

int main(void)
{
    char *page = NULL;

    vm_net_mock_chest_rows_reset_to_identities();
    memset(g_vm_net_mock_shop_catalog, 0, sizeof(g_vm_net_mock_shop_catalog));
    g_vm_net_mock_shop_catalog[0].itemId = 1001;
    snprintf(g_vm_net_mock_shop_catalog[0].name,
             sizeof(g_vm_net_mock_shop_catalog[0].name), "Test item");
    g_vm_net_mock_shop_catalog[0].isEquip = true;
    g_vm_net_mock_shop_catalog[0].quality = 3;
    g_vm_net_mock_shop_catalog_count = 1;
    g_vm_net_mock_shop_catalog_loaded = true;
    g_vm_net_mock_equipment_catalog[0].itemId = 1001;
    g_vm_net_mock_equipment_catalog[0].levelRequired = 12;
    g_vm_net_mock_equipment_catalog_count = 1;
    g_vm_net_mock_equipment_catalog_loaded = true;
    for (u32 chest = 0; chest < VM_NET_MOCK_CHEST_KIND_COUNT; ++chest)
    {
        g_vm_net_mock_chest_rows[chest].rewardCount =
            VM_NET_MOCK_CHEST_REWARD_MAX;
        for (u32 row = 0; row < VM_NET_MOCK_CHEST_REWARD_MAX; ++row)
        {
            vm_net_mock_chest_reward *reward =
                &g_vm_net_mock_chest_rows[chest].rewards[row];
            reward->itemId = 1001u + row;
            reward->count = 1;
            reward->weight = 1;
            reward->worldBroadcast = (row & 1u) ? 1 : 0;
        }
    }
    g_vm_net_mock_chest_db_loaded = true;
    g_vm_net_mock_chest_db_valid = true;
    page = (char *)calloc(1, VM_MOCK_ADMIN_RESPONSE_MAX);
    if (page == NULL)
    {
        fputs("unable to allocate admin response buffer\n", stderr);
        return 1;
    }
    vm_mock_admin_render_chest_page(page, VM_MOCK_ADMIN_RESPONSE_MAX,
                                    "tab=chests&chest=524");
    if (strstr(page, "宝箱管理页面超过大小限制") != NULL ||
        strstr(page, "id=\"chest-524\"") == NULL ||
        strstr(page, "id=\"chest-522\"") != NULL ||
        strstr(page, "id=\"chest-523\"") != NULL ||
        strstr(page, "item-picker-tools-equipment") == NULL ||
        strstr(page, "id=\"item-quality\"") == NULL ||
        strstr(page, "id=\"item-level\"") == NULL ||
        strstr(page, "data-quality=\"3\"") == NULL ||
        strstr(page, "data-level=\"12\"") == NULL ||
        count_occurrences(page, "data-chest-reward-row") !=
            VM_NET_MOCK_CHEST_REWARD_MAX ||
        strlen(page) >= VM_MOCK_ADMIN_RESPONSE_MAX)
    {
        fputs("selected chest editor exceeded the response budget or leaked hidden editors\n",
              stderr);
        free(page);
        return 1;
    }
    printf("chest admin page-size regression passed: selected rows=%u bytes=%u\n",
           VM_NET_MOCK_CHEST_REWARD_MAX, (u32)strlen(page));
    free(page);
    return 0;
}
