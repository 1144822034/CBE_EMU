/*
 * Contract regression for the account-role operation dialog and its shared
 * item and scene pickers.  It renders no HTTP page, opens no database
 * connection, and does not alter account or role state.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    vm_net_mock_role_state role;
    vm_mock_admin_text page;
    char rendered[16384];

    memset(&role, 0, sizeof(role));
    memset(rendered, 0, sizeof(rendered));
    role.roleId = 37;
    vm_mock_admin_text_init(&page, rendered, sizeof(rendered));
    vm_mock_admin_render_role_operation_modal(&page, "role.ops", &role,
                                              "操作测试角色", NULL, true, NULL, 0);
    vm_mock_admin_render_scene_picker_modal(&page);
    if (page.truncated ||
        strstr(rendered,
               "data-item-picker-open=\"role-grant-item-37\"") == NULL ||
        strstr(rendered, "aria-controls=\"item-picker-modal\"") == NULL ||
        strstr(rendered,
               "select class=\"scene-resource-select\" name=\"reset_scene\"") == NULL ||
        strstr(rendered, "data-scene-picker-open") == NULL ||
        strstr(rendered, "id=\"scene-picker-modal\"") == NULL)
    {
        fprintf(stderr, "role operation fields are not bound to shared pickers\n");
        return 1;
    }
    if (VM_MOCK_ADMIN_ITEM_PICKER_MODAL_Z_INDEX <=
        VM_MOCK_ADMIN_ROLE_OPERATION_MODAL_Z_INDEX)
    {
        fprintf(stderr, "item picker must be above the role operation modal\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script,
               "const roleOperationOpen=()=>!!document.querySelector") == NULL ||
        strstr(g_vm_mock_admin_script,
               "if(!roleOperationOpen())document.body.classList.remove('modal-open')") == NULL ||
        strstr(g_vm_mock_admin_script,
               "const itemPicker=document.querySelector('#item-picker-modal')") == NULL ||
        strstr(g_vm_mock_admin_script,
               "itemPicker&&!itemPicker.hidden") == NULL ||
        strstr(g_vm_mock_admin_script,
               "scenePicker=document.querySelector('#scene-picker-modal')") == NULL ||
        strstr(g_vm_mock_admin_script,
               "if(!roleOperationOpen())document.body.classList.remove('modal-open')") == NULL)
    {
        fprintf(stderr, "nested role picker modal lifecycle is incomplete\n");
        return 1;
    }

    puts("admin role item-picker layering regression passed");
    return 0;
}
