/*
 * Contract regression for searchable monster selection fields.  It performs
 * no HTTP or database work: the catalog itself is rendered by the page-level
 * dialog, while each form keeps only its submitted monster value.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    vm_mock_admin_text page;
    char rendered[8192];

    memset(rendered, 0, sizeof(rendered));
    vm_mock_admin_text_init(&page, rendered, sizeof(rendered));
    vm_mock_admin_render_monster_picker_field(
        &page, "monster_id", 53, true, false, false);
    vm_mock_admin_render_monster_picker_field(
        &page, "instance_spawn_enemy_id", 0, false, true, false);
    vm_mock_admin_render_task_requirement_target_field(&page, 1, 2, 53);
    if (page.truncated ||
        strstr(rendered,
               "select class=\"monster-resource-select\" name=\"monster_id\" required hidden") == NULL ||
        strstr(rendered, "<option value=\"53\" selected>#53</option>") == NULL ||
        strstr(rendered, "data-monster-picker-open") == NULL ||
        strstr(rendered, "aria-controls=\"monster-picker-modal\"") == NULL ||
        strstr(rendered,
               "name=\"instance_spawn_enemy_id\" data-monster-picker-allow-none=\"1\" hidden") == NULL ||
        strstr(rendered, "<option value=\"0\" selected>不额外刷新</option>") == NULL ||
        strstr(rendered, "data-task-requirement-target=\"1\"") == NULL ||
        strstr(rendered,
               "name=\"requirement_id1\" min=\"0\" max=\"4294967295\" value=\"53\" data-item-picker-input data-task-requirement-numeric disabled hidden") == NULL)
    {
        fprintf(stderr, "monster picker fields lost their form contract\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script, "const setupMonsterPicker=()=>{") == NULL ||
        strstr(g_vm_mock_admin_script, "#monster-picker-options") == NULL ||
        strstr(g_vm_mock_admin_script, "data-monster-picker-open") == NULL ||
        strstr(g_vm_mock_admin_script,
               "select.monster-resource-select[required]") == NULL ||
        strstr(g_vm_mock_admin_script, "const setupTaskRequirementTargets=()=>{") == NULL ||
        strstr(g_vm_mock_admin_script, "找到 ${shown} 个怪物") == NULL)
    {
        fprintf(stderr, "monster picker dialog lifecycle is incomplete\n");
        return 1;
    }

    puts("admin monster picker regression passed");
    return 0;
}
