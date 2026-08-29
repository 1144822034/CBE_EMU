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
    vm_net_mock_dynamic_npc_task_binding bindings[2];

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
    memset(bindings, 0, sizeof(bindings));
    bindings[0].taskId = 2000;
    bindings[1].taskId = 2001;

    memset(rendered, 0, sizeof(rendered));
    vm_mock_admin_text_init(&page, rendered, sizeof(rendered));
    vm_mock_admin_render_npc_task_select(&page, bindings, 2);
    vm_mock_admin_render_npc_task_picker_modal(&page);
    if (page.truncated ||
        strstr(rendered,
               "<div class=\"npc-task-picker-field\" data-npc-task-list-editor><select class=\"npc-task-select\" data-npc-task-max=\"8\" multiple hidden>") == NULL ||
        strstr(rendered,
               "<input type=\"hidden\" name=\"task_ids\" data-npc-task-values value=\"2000,2001\">") == NULL ||
        strstr(rendered,
               "<option value=\"2000\" selected>2000 · Starter task") == NULL ||
        strstr(rendered,
               "<option value=\"2001\" selected>2001 · Follow-up task") == NULL ||
        strstr(rendered, "data-npc-task-list") == NULL ||
        strstr(rendered, "＋ 添加任务") == NULL ||
        strstr(rendered, "id=\"npc-task-picker-search\"") == NULL)
    {
        fprintf(stderr, "NPC task picker lost its form or modal contract\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script,
               "const setupNpcTaskListEditor=()=>{") == NULL ||
        strstr(g_vm_mock_admin_script, "select.npc-task-select") == NULL ||
        strstr(g_vm_mock_admin_script, "data-npc-task-picker-open") == NULL ||
        strstr(g_vm_mock_admin_script,
               "setupNpcServices=()=>{setupScenePicker();setupNpcTaskListEditor();") == NULL ||
        strstr(g_vm_mock_admin_script, "data-npc-task-picker-clear") == NULL ||
        strstr(g_vm_mock_admin_script, "已选 ${chosen}/${limit}") == NULL)
    {
        fprintf(stderr, "NPC task picker browser lifecycle is incomplete\n");
        return 1;
    }
    snprintf(scriptTag, sizeof(scriptTag),
             "<script src=\"/admin.js\" defer></script>");
    if (!vm_mock_admin_prefix_page_routes(scriptTag, sizeof(scriptTag)) ||
        strstr(scriptTag, "admin.js?v=20260829-1\"") == NULL)
    {
        fprintf(stderr, "NPC task picker script was not cache-busted\n");
        return 1;
    }

    puts("admin NPC task picker regression passed");
    return 0;
}
