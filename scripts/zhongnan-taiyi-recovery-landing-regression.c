/*
 * Resource-only regression for the Zhongnan Mountain / Taiyi Peak recovery
 * landing.  It imports the server aggregation unit without starting a
 * listener or opening MySQL: the role fixture lives solely in static memory.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    static const char taiyiScene[] =
        "11\xD6\xD5\xC4\xCF\xC9\xBD_02.sce"; /* 11终南山_02.sce */
    static const char linanCenterScene[] =
        "c04\xC1\xD9\xB0\xB2\xB8\xAE_10.sce"; /* c04临安府_10.sce */
    static const char linanSouthScene[] =
        "c04\xC1\xD9\xB0\xB2\xB8\xAE_01.sce"; /* c04临安府_01.sce */
    static const char panlongNorthScene[] =
        "23\xF3\xB4\xC1\xFA\xD5\xAF_03.sce"; /* 23蟠龙寨_03.sce */
    static const char panlongSafeScene[] =
        "23\xF3\xB4\xC1\xFA\xD5\xAF_02.sce"; /* 23蟠龙寨_02.sce (止水堂) */
    vm_net_mock_scene_recovery_map map;
    vm_net_mock_scene_change_target target;
    char respawnScene[64];
    u32 sourceRow = 0;
    u32 targetRow = 0;
    u32 distance = 0;
    const char *route = NULL;
    u16 x = 427;
    u16 y = 340;

    /* The source SCE entry is close to the lower-right exit trigger.  The
     * existing portal-gap rule correctly prevents a retrigger, but moves it
     * left to the all-edge MAP cell that caused the player to be stuck. */
    vm_net_mock_adjust_safe_player_pos_for_scene(taiyiScene, &x, &y);
    if (x != 400 || y != 340 ||
        !vm_net_mock_scene_recovery_load_map(taiyiScene, &map) ||
        vm_net_mock_scene_recovery_candidate_is_clear(&map, x, y))
    {
        fputs("Taiyi baseline does not reproduce the post-portal collision cell\n", stderr);
        return 1;
    }

    if (!vm_net_mock_adjust_recovery_landing_to_map_safe(taiyiScene, &x, &y) ||
        x != 408 || y != 280 ||
        !vm_net_mock_scene_recovery_candidate_is_clear(&map, x, y))
    {
        fputs("Taiyi MAP recovery landing did not select the expected clear cell\n", stderr);
        return 1;
    }

    if (!vm_net_mock_resolve_nearest_safe_respawn(taiyiScene,
                                                   respawnScene,
                                                   sizeof(respawnScene),
                                                   &x,
                                                   &y,
                                                   &sourceRow,
                                                   &targetRow,
                                                   &distance,
                                                   &route) ||
        strcmp(respawnScene, linanCenterScene) != 0 ||
        sourceRow != 90 || targetRow != 56 || distance != 1 ||
        route == NULL || strcmp(route, "wmap-nearest-town-center") != 0 ||
        !vm_net_mock_scene_recovery_load_map(respawnScene, &map) ||
        !vm_net_mock_scene_recovery_candidate_is_clear(&map, x, y))
    {
        fputs("Taiyi ordinary-death respawn did not select the Linan town centre\n",
              stderr);
        return 1;
    }

    if (!vm_net_mock_resolve_nearest_safe_respawn(linanSouthScene,
                                                   respawnScene,
                                                   sizeof(respawnScene),
                                                   &x,
                                                   &y,
                                                   &sourceRow,
                                                   &targetRow,
                                                   &distance,
                                                   &route) ||
        strcmp(respawnScene, linanCenterScene) != 0 ||
        sourceRow != 47 || targetRow != 56 || distance != 0 ||
        route == NULL || strcmp(route, "wmap-nearest-town-center") != 0)
    {
        fputs("Linan ordinary-death respawn did not retain its town centre\n",
              stderr);
        return 1;
    }

    /* 23蟠龙寨_03 has no safety marker itself, but its local sMap graph
     * reaches the authored safe 23蟠龙寨_02 in two hops.  The old city-only
     * search instead observed the one-hop wMap edge to Penglai and made the
     * same wrong respawn choice from this whole map group. */
    if (!vm_net_mock_resolve_nearest_safe_respawn(panlongNorthScene,
                                                   respawnScene,
                                                   sizeof(respawnScene),
                                                   &x,
                                                   &y,
                                                   &sourceRow,
                                                   &targetRow,
                                                   &distance,
                                                   &route) ||
        strcmp(respawnScene, panlongSafeScene) != 0 ||
        sourceRow != 154 || targetRow != 153 || distance != 2 ||
        route == NULL || strcmp(route, "smap-nearest-safe-scene") != 0 ||
        !vm_net_mock_scene_recovery_load_map(respawnScene, &map) ||
        !vm_net_mock_scene_recovery_candidate_is_clear(&map, x, y))
    {
        fputs("Panlong local safe-scene respawn incorrectly fell back to Penglai\n",
              stderr);
        return 1;
    }

    /* Exercise the actual settings-unstuck target chooser against the same
     * in-memory role.  No role save occurs in this selector. */
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 0x54415949u;
    g_vm_net_mock_role_db.roles[0].roleId = g_vm_net_mock_role_db.activeRoleId;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", taiyiScene);
    g_vm_net_mock_role_db.roles[0].x = 427;
    g_vm_net_mock_role_db.roles[0].y = 340;

    memset(&target, 0, sizeof(target));
    vm_net_mock_get_current_scene_unstuck_target(&target);
    if (strcmp(target.scene, taiyiScene) != 0 || target.x != 408 || target.y != 280)
    {
        fputs("Taiyi settings-unstuck target did not retain the MAP-safe landing\n", stderr);
        return 1;
    }

    printf("Zhongnan Taiyi recovery landing regression passed: "
           "portal-gap=(400,340) map-safe=(408,280)\n");
    return 0;
}
