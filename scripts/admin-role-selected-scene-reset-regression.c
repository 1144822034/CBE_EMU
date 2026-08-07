/*
 * Resource-only regression for the selected-scene role-position recovery
 * contract.  It does not start a listener, connect to MySQL, or mutate an
 * account: the test proves that the web form accepts only an exact server SCE
 * resource key and that the saved landing point is derived from that SCE.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    static const char selectedScene[] =
        "01\xCC\xD2\xBB\xA8\xB5\xBA_01.sce"; /* 01桃花岛_01.sce */
    char selectedSceneUtf8[192];
    char form[256];
    char formSceneUtf8[192];
    char runtimeScene[64];
    u16 x = 0;
    u16 y = 0;

    memset(selectedSceneUtf8, 0, sizeof(selectedSceneUtf8));
    memset(form, 0, sizeof(form));
    memset(formSceneUtf8, 0, sizeof(formSceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    vm_net_mock_gbk_label_to_utf8(selectedScene, selectedSceneUtf8,
                                  sizeof(selectedSceneUtf8));
    if (selectedSceneUtf8[0] == 0 ||
        snprintf(form, sizeof(form), "reset_scene=%s", selectedSceneUtf8) >=
            (int)sizeof(form) ||
        !vm_mock_admin_optional_scene_from_form(
            form, "reset_scene", formSceneUtf8, sizeof(formSceneUtf8),
            runtimeScene, sizeof(runtimeScene)) ||
        strcmp(runtimeScene, selectedScene) != 0 ||
        !vm_net_mock_scene_resource_exists(runtimeScene) ||
        !vm_net_mock_get_scene_reasonable_spawn_from_sce(runtimeScene, &x, &y,
                                                          NULL) ||
        x == 0 || y == 0)
    {
        fputs("admin selected-scene reset did not resolve an exact SCE landing\n",
              stderr);
        return 1;
    }

    memset(formSceneUtf8, 0, sizeof(formSceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    if (vm_mock_admin_optional_scene_from_form(
            "reset_scene=missing-admin-reset-scene.sce", "reset_scene",
            formSceneUtf8, sizeof(formSceneUtf8), runtimeScene,
            sizeof(runtimeScene)))
    {
        fputs("admin selected-scene reset accepted an unavailable SCE key\n",
              stderr);
        return 1;
    }

    printf("admin role selected-scene reset regression passed: scene=%s "
           "landing=(%u,%u)\n", runtimeScene[0] ? runtimeScene : selectedScene,
           x, y);
    return 0;
}
