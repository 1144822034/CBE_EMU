#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    char *page = (char *)calloc(VM_MOCK_ADMIN_RESPONSE_MAX, 1);

    if (page == NULL || !vm_net_mock_set_resource_dir("web/fs/JHOnlineData"))
    {
        free(page);
        fprintf(stderr, "could not prepare scene battle monster layout fixture\n");
        return 1;
    }
    vm_mock_admin_render_scene_battle_monster_page(
        page, VM_MOCK_ADMIN_RESPONSE_MAX, "");
    if (strstr(page,
               ".battle-monster-fields{grid-template-columns:repeat(6,minmax(0,1fr))}") == NULL ||
        strstr(page,
               "@media(max-width:920px){.battle-monster-fields{grid-template-columns:repeat(2,minmax(0,1fr))}") == NULL ||
        strstr(page,
               "@media(max-width:560px){.battle-monster-fields{grid-template-columns:1fr}") == NULL ||
        strstr(page,
               "<div class=\"fields battle-monster-fields\"><label class=\"field battle-monster-wide\"><span>怪物</span>") == NULL ||
        strstr(page, "<span>显示名称（GBK ≤29字节）</span>") == NULL ||
        strstr(page, "<span>Actor 资源</span>") == NULL ||
        strstr(page,
               "@media(min-width:921px){.fields{grid-template-columns:") != NULL)
    {
        free(page);
        fprintf(stderr, "scene battle monster editor layout regressed\n");
        return 1;
    }
    free(page);
    puts("admin scene battle monster layout regression passed");
    return 0;
}
