/*
 * Contract regression for the task-target list editor.  It renders no HTTP
 * page and does not open a database connection; the script checks the two
 * protocol-backed slots and the browser-side list controller in isolation.
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

    memset(rendered, 0, sizeof(rendered));
    vm_mock_admin_text_init(&page, rendered, sizeof(rendered));
    vm_mock_admin_render_task_requirement_row(&page, 1, 0, 0, 0, "");
    vm_mock_admin_render_task_requirement_row(&page, 1, 1, 77, 2, "");
    vm_mock_admin_render_task_requirement_row(
        &page, 2, 2, 53, 3, "challenge.sce");
    if (page.truncated ||
        strstr(rendered,
               "class=\"task-target-row\" data-task-requirement-row=\"1\" hidden") == NULL ||
        strstr(rendered,
               "class=\"task-target-row\" data-task-requirement-row=\"2\"") == NULL ||
        strstr(rendered,
               "value=\"77\" data-item-picker-input data-task-requirement-numeric hidden>") == NULL ||
        strstr(rendered,
               "name=\"requirement_count2\" min=\"0\" max=\"255\" value=\"3\" data-task-requirement-count") == NULL ||
        strstr(rendered, "name=\"requirement_scene2\" maxlength=\"63\" value=\"challenge.sce\"") == NULL ||
        strstr(rendered, "data-task-requirement-remove") == NULL)
    {
        fprintf(stderr, "task target rows lost their form contract\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script,
               "const setupTaskRequirementTargets=()=>{const box=document.querySelector('#task-requirement-list')") == NULL ||
        strstr(g_vm_mock_admin_script, "data-task-requirement-add") == NULL ||
        strstr(g_vm_mock_admin_script, "const clear=row=>") == NULL ||
        strstr(g_vm_mock_admin_script, "field.numeric.hidden=true") == NULL ||
        strstr(g_vm_mock_admin_script,
               "const setupTaskRequirementMonsterPicker=()=>") == NULL ||
        strstr(g_vm_mock_admin_script,
               "[data-task-requirement-target] [data-monster-picker-open]") == NULL ||
        strstr(g_vm_mock_admin_script, "state.show(select)") == NULL ||
        strstr(g_vm_mock_admin_script, "＋ 新增任务目标（${active.length}/${rows.length}）") == NULL ||
        strstr(g_vm_mock_admin_script,
               "setupMonsterPicker();setupTaskRequirementTargets();setupNpcServices()") == NULL)
    {
        fprintf(stderr,
                "task target list lifecycle or partial-navigation rebind is incomplete\n");
        return 1;
    }

    puts("admin task target-list regression passed");
    return 0;
}
