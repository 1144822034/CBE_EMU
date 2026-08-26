#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    char *page = (char *)calloc(VM_MOCK_ADMIN_RESPONSE_MAX, 1);
    u32 firstId = 0;
    u32 nextId = 0;
    u32 exhaustedId = 0;

    /* The page may attempt to read existing NPC rows. Point it at a closed
     * local endpoint so this layout and allocator regression never reads or
     * migrates a user's database. */
    if (page == NULL || _putenv_s("CBE_MYSQL_HOST", "127.0.0.1") != 0 ||
        _putenv_s("CBE_MYSQL_PORT", "1") != 0 ||
        _putenv_s("CBE_MYSQL_DATABASE",
                  "cbe_dynamic_npc_id_layout_no_database") != 0 ||
        !vm_net_mock_set_resource_dir("web/fs/JHOnlineData") ||
        !vm_net_mock_dynamic_npc_admin_choose_actor_id(
            VM_NET_MOCK_DYNAMIC_NPC_AUTOMATIC_ID_MIN - 1u, &firstId, NULL) ||
        firstId != VM_NET_MOCK_DYNAMIC_NPC_AUTOMATIC_ID_MIN ||
        !vm_net_mock_dynamic_npc_admin_choose_actor_id(
            firstId, &nextId, NULL) || nextId != firstId + 1u ||
        vm_net_mock_dynamic_npc_admin_choose_actor_id(
            VM_NET_MOCK_DYNAMIC_NPC_AUTOMATIC_ID_MAX, &exhaustedId, NULL) ||
        exhaustedId != 0)
    {
        free(page);
        fputs("dynamic NPC ID allocation contract regressed\n", stderr);
        return 1;
    }
    vm_mock_admin_render_content_page(page, VM_MOCK_ADMIN_RESPONSE_MAX, "");
    if (strstr(page, "name=\"create_npc\" value=\"1\"") == NULL ||
        strstr(page, "<span>Actor ID</span><input value=\"保存后自动分配\" readonly>") == NULL ||
        strstr(page, "增加动态 NPC") == NULL)
    {
        free(page);
        fputs("dynamic NPC editor no longer uses automatic Actor IDs\n", stderr);
        return 1;
    }
    free(page);
    puts("admin dynamic NPC ID regression passed");
    return 0;
}
