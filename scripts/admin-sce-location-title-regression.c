/*
 * Resource-only regression for the SCE content-list location title contract.
 *
 * It reads the checked-in sample data, starts neither a listener nor a
 * database connection, and does not modify any resource.  The user-facing
 * SCE list must use field 0x16 of a named portal as its title while retaining
 * the filename as a separately searchable subtitle.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    vm_mock_admin_scene_file files[VM_MOCK_ADMIN_SCENE_FILE_MAX];
    char savedResourceDir[sizeof(g_vm_net_mock_resource_dir)];
    char sceneFile[sizeof(files[0].name)];
    char titleGbk[64];
    char titleUtf8[128];
    char renderedItem[2048];
    vm_mock_admin_text renderedPage;
    u32 fileCount = 0;
    int result = 1;

    memset(files, 0, sizeof(files));
    memset(savedResourceDir, 0, sizeof(savedResourceDir));
    memset(sceneFile, 0, sizeof(sceneFile));
    memset(titleGbk, 0, sizeof(titleGbk));
    memset(titleUtf8, 0, sizeof(titleUtf8));
    memset(renderedItem, 0, sizeof(renderedItem));
    snprintf(savedResourceDir, sizeof(savedResourceDir), "%s",
             g_vm_net_mock_resource_dir);
    if (!vm_net_mock_set_resource_dir("web/fs/JHOnlineData"))
    {
        fputs("SCE title regression could not open the resource fixture\n",
              stderr);
        goto done;
    }
    fileCount = vm_mock_admin_collect_scene_files(
        files, VM_MOCK_ADMIN_SCENE_FILE_MAX);
    for (u32 i = 0; i < fileCount; ++i)
    {
        if (strncmp(files[i].name, "00_", 3) == 0)
        {
            snprintf(sceneFile, sizeof(sceneFile), "%s", files[i].name);
            break;
        }
    }
    if (sceneFile[0] == 0 ||
        !vm_mock_admin_scene_location_title(sceneFile, titleGbk,
                                            sizeof(titleGbk)))
    {
        fputs("SCE title regression could not resolve the sample location\n",
              stderr);
        goto done;
    }
    vm_net_mock_gbk_label_to_utf8(titleGbk, titleUtf8, sizeof(titleUtf8));
    if (strcmp(titleUtf8,
               "\xE8\x93\xAC\xE8\x8E\xB1\xE4\xBB\x99\xE5\xB2\x9B"
               "-\xE5\x8D\x81\xE4\xBA\x8C\xE5\x9F\x8E") != 0 ||
        strcmp(titleGbk, sceneFile) == 0)
    {
        fprintf(stderr, "unexpected SCE location title: %s\n", titleUtf8);
        goto done;
    }
    vm_mock_admin_text_init(&renderedPage, renderedItem, sizeof(renderedItem));
    for (u32 i = 0; i < fileCount; ++i)
    {
        if (strcmp(files[i].name, sceneFile) == 0)
        {
            vm_mock_admin_render_scene_catalog_item(
                &renderedPage, &files[i], titleGbk, true, false);
            break;
        }
    }
    if (renderedPage.truncated ||
        strstr(renderedItem, "data-content-resource-search-key=\"") == NULL ||
        strstr(renderedItem, "scene-title") == NULL ||
        strstr(renderedItem, "scene-file") == NULL ||
        strstr(renderedItem, titleUtf8) == NULL ||
        strstr(g_vm_mock_admin_script,
               "item.dataset.contentResourceSearchKey") == NULL)
    {
        fputs("SCE list title/subtitle search contract is missing\n", stderr);
        goto done;
    }
    printf("admin SCE location title regression passed: %s -> %s\n",
           sceneFile, titleUtf8);
    result = 0;

done:
    snprintf(g_vm_net_mock_resource_dir, sizeof(g_vm_net_mock_resource_dir),
             "%s", savedResourceDir);
    return result;
}
