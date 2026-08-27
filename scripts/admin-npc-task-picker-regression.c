/*
 * Contract regression for the SCE dynamic-NPC task picker.  It exercises the
 * rendered form contract only; no HTTP request, database, or game process is
 * started.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    vm_mock_admin_text page;
    char rendered[16384];
    char scriptTag[256];

    memset(g_vm_net_mock_task_catalog, 0, sizeof(g_vm_net_mock_task_catalog));
    g_vm_net_mock_task_catalog_attempted = true;
    g_vm_net_mock_task_catalog_count = 2;
    g_vm_net_mock_task_catalog[0].taskId = 2000;
    g_vm_net_mock_task_catalog[0].enabled = true;
    snprintf(g_vm_net_mock_task_catalog[0].name,
             sizeof(g_vm_net_mock_task_catalog[0].name), "Starter task");
    g_vm_net_mock_task_catalog[0].level = 1;
    g_vm_net_mock_task_catalog[1].taskId = 2001;
    g_vm_net_mock_task_catalog[1].enabled = true;
    snprintf(g_vm_net_mock_task_catalog[1].name,
             sizeof(g_vm_net_mock_task_catalog[1].name), "Follow-up task");
    g_vm_net_mock_task_catalog[1].level = 8;
    g_vm_net_mock_task_catalog[1].prerequisiteTaskId = 2000;

    memset(rendered, 0, sizeof(rendered));
    vm_mock_admin_text_init(&page, rendered, sizeof(rendered));
    vm_mock_admin_render_npc_task_select(&page, 2001);
    vm_mock_admin_render_npc_task_picker_modal(&page);
    if (page.truncated ||
        strstr(rendered,
               "<select class=\"npc-task-select\" name=\"task_id\" hidden>") == NULL ||
        strstr(rendered,
               "<option value=\"2001\" selected>2001 · Follow-up task") == NULL ||
        strstr(rendered, "data-npc-task-picker-open") == NULL ||
        strstr(rendered, "aria-controls=\"npc-task-picker-modal\"") == NULL ||
        strstr(rendered, "id=\"npc-task-picker-search\"") == NULL ||
        strstr(rendered, "id=\"npc-task-picker-list\"") == NULL)
    {
        fprintf(stderr, "NPC task picker lost its form or modal contract\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script,
               "const setupNpcTaskPicker=()=>{") == NULL ||
        strstr(g_vm_mock_admin_script, "select.npc-task-select") == NULL ||
        strstr(g_vm_mock_admin_script, "data-npc-task-picker-open") == NULL ||
        strstr(g_vm_mock_admin_script,
               "setupNpcServices=()=>{setupScenePicker();setupNpcTaskPicker();") == NULL ||
        strstr(g_vm_mock_admin_script, "找到 ${shown} 个任务") == NULL)
    {
        fprintf(stderr, "NPC task picker browser lifecycle is incomplete\n");
        return 1;
    }
    snprintf(scriptTag, sizeof(scriptTag),
             "<script src=\"/admin.js\" defer></script>");
    if (!vm_mock_admin_prefix_page_routes(scriptTag, sizeof(scriptTag)) ||
        strstr(scriptTag, "admin.js?v=20260827-1\"") == NULL)
    {
        fprintf(stderr, "NPC task picker script was not cache-busted\n");
        return 1;
    }

    puts("admin NPC task picker regression passed");
    return 0;
}
