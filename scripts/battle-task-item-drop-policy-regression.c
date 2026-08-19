/*
 * Deterministic task-item classification and eligibility regression.
 *
 * This uses only in-memory resource/task snapshots.  It does not start a
 * client, open a socket, or mutate a role/database.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static void reset_fixture(void)
{
    memset(g_vm_net_mock_shop_catalog, 0, sizeof(g_vm_net_mock_shop_catalog));
    g_vm_net_mock_shop_catalog_loaded = true;
    g_vm_net_mock_shop_catalog_count = 2;
    g_vm_net_mock_shop_catalog[0].itemId = 42;
    g_vm_net_mock_shop_catalog[0].category = VM_NET_MOCK_ITEM_CATEGORY_TASK;
    g_vm_net_mock_shop_catalog[0].enabled = 1;
    g_vm_net_mock_shop_catalog[1].itemId = 304;
    g_vm_net_mock_shop_catalog[1].category = 1;
    g_vm_net_mock_shop_catalog[1].enabled = 1;

    memset(g_vm_net_mock_task_catalog, 0, sizeof(g_vm_net_mock_task_catalog));
    g_vm_net_mock_task_catalog_attempted = true;
    g_vm_net_mock_task_catalog_count = 1;
    g_vm_net_mock_task_catalog[0].taskId = 900001;
    g_vm_net_mock_task_catalog[0].enabled = true;
    g_vm_net_mock_task_catalog[0].requirementType1 = 1;
    g_vm_net_mock_task_catalog[0].requirementCount1 = 3;
    g_vm_net_mock_task_catalog[0].requirementId1 = 42;

    memset(&g_vm_net_mock_task_state_request_cache, 0,
           sizeof(g_vm_net_mock_task_state_request_cache));
    g_vm_net_mock_task_state_request_cache.active = true;
    g_vm_net_mock_task_state_request_cache.loaded = true;
    g_vm_net_mock_task_state_request_cache.loadOk = true;
    g_vm_net_mock_task_state_request_cache.roleId = 700001;
}

static bool expect_policy(u32 itemId, bool expectedTaskMaterial,
                          u32 expectedRemaining, const char *caseName)
{
    bool taskMaterial = false;
    u32 remaining = 0;

    if (!vm_net_mock_task_material_drop_policy(
            700001, itemId, &taskMaterial, &remaining) ||
        taskMaterial != expectedTaskMaterial || remaining != expectedRemaining)
    {
        fprintf(stderr,
                "%s failed: item=%u task_material=%u/%u remaining=%u/%u\n",
                caseName, itemId, taskMaterial ? 1u : 0u,
                expectedTaskMaterial ? 1u : 0u, remaining, expectedRemaining);
        return false;
    }
    return true;
}

int main(void)
{
    reset_fixture();
    if (!expect_policy(42, true, 0, "task-category-without-acceptance"))
        return 1;

    g_vm_net_mock_task_state_request_cache.rowCount = 1;
    g_vm_net_mock_task_state_request_cache.rows[0].taskId = 900001;
    g_vm_net_mock_task_state_request_cache.rows[0].state = 1;
    g_vm_net_mock_task_state_request_cache.rows[0].progress1 = 1;
    if (!expect_policy(42, true, 2, "accepted-task-remaining-cap"))
        return 1;

    g_vm_net_mock_task_state_request_cache.rows[0].state = 2;
    if (!expect_policy(42, true, 0, "ready-to-turn-in-is-ineligible"))
        return 1;

    g_vm_net_mock_task_state_request_cache.rowCount = 0;
    if (!expect_policy(304, false, 0, "ordinary-item-without-task"))
        return 1;

    g_vm_net_mock_task_catalog[0].requirementId1 = 304;
    g_vm_net_mock_task_catalog[0].requirementCount1 = 4;
    g_vm_net_mock_task_state_request_cache.rowCount = 1;
    g_vm_net_mock_task_state_request_cache.rows[0].taskId = 900001;
    g_vm_net_mock_task_state_request_cache.rows[0].state = 1;
    g_vm_net_mock_task_state_request_cache.rows[0].progress1 = 1;
    if (!expect_policy(304, true, 3, "custom-cross-category-task-item"))
        return 1;

    g_vm_net_mock_task_catalog[0].enabled = false;
    if (!expect_policy(304, true, 0, "disabled-task-cannot-enable-drop"))
        return 1;

    puts("battle task-item drop policy regression passed");
    return 0;
}
