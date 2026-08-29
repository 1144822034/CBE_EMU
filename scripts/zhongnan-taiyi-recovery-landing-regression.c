/*
 * Resource-only regression for the Zhongnan Mountain / Taiyi Peak recovery
 * landing.  It imports the server aggregation unit without starting a
 * listener or opening MySQL: the role fixture lives solely in static memory.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static void vm_test_write_le32(u8 *out, u32 value)
{
    out[0] = (u8)value;
    out[1] = (u8)(value >> 8);
    out[2] = (u8)(value >> 16);
    out[3] = (u8)(value >> 24);
}

static bool vm_test_append_dsh_ascii(u8 *raw, u32 rawCap, u32 *pos,
                                     const char *text)
{
    size_t textLen = text != NULL ? strlen(text) : 0;

    if (raw == NULL || pos == NULL || textLen > 255 ||
        *pos > rawCap || rawCap - *pos < textLen + 1)
    {
        return false;
    }
    raw[(*pos)++] = (u8)textLen;
    if (textLen != 0)
    {
        memcpy(raw + *pos, text, textLen);
        *pos += (u32)textLen;
    }
    return true;
}

/* The live resource has the conventional order, but DSH explicitly records
 * its column names.  This fixture deliberately reorders the route columns,
 * adds an unrelated field, and retains a blank terminal row. */
static bool vm_test_build_reordered_wmapline_dsh(u8 *raw, u32 rawCap,
                                                 u32 *rawLenOut)
{
    static const char *const columns[] = {"Y2", "ID", "X1", "Note", "Y1", "X2"};
    static const char *const rows[][6] = {
        {"48", "77", "12", "ignored", "34", "56"},
        {"", "", "", "", "", ""}
    };
    u32 pos = 16;
    u32 headerStart = pos;

    if (rawLenOut)
        *rawLenOut = 0;
    if (raw == NULL || rawLenOut == NULL || rawCap < 32)
        return false;
    memset(raw, 0, rawCap);
    for (u32 column = 0; column < sizeof(columns) / sizeof(columns[0]); ++column)
    {
        if (!vm_test_append_dsh_ascii(raw, rawCap, &pos, columns[column]))
            return false;
    }
    vm_test_write_le32(raw + 4, sizeof(columns) / sizeof(columns[0]));
    vm_test_write_le32(raw + 8, sizeof(rows) / sizeof(rows[0]));
    vm_test_write_le32(raw + 12, pos - headerStart);

    for (u32 row = 0; row < sizeof(rows) / sizeof(rows[0]); ++row)
    {
        u32 rowStart = pos;

        if (pos > rawCap || rawCap - pos < 4)
            return false;
        pos += 4;
        for (u32 column = 0; column < sizeof(columns) / sizeof(columns[0]); ++column)
        {
            if (!vm_test_append_dsh_ascii(raw, rawCap, &pos, rows[row][column]))
                return false;
        }
        vm_test_write_le32(raw + rowStart, pos - rowStart - 4);
    }
    vm_test_write_le32(raw, pos - 4);
    *rawLenOut = pos;
    return true;
}

static bool vm_test_reordered_wmapline_schema_reader(void)
{
    u8 raw[128];
    vm_net_mock_death_respawn_wmap_line lines[2];
    u32 rawLen = 0;
    u32 lineCount = 0;

    if (!vm_test_build_reordered_wmapline_dsh(raw, sizeof(raw), &rawLen) ||
        !vm_net_mock_death_respawn_read_wmap_lines_dsh(
            raw, rawLen, lines, sizeof(lines) / sizeof(lines[0]), &lineCount) ||
        lineCount != 1 || lines[0].lineId != 77 ||
        lines[0].x1 != 12 || lines[0].y1 != 34 ||
        lines[0].x2 != 56 || lines[0].y2 != 48)
    {
        return false;
    }
    return true;
}

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
    static const char ghostPalaceScene[] =
        "21\xD3\xC4\xDA\xA4\xB9\xED\xB8\xAE_01.sce"; /* 21幽冥鬼府_01.sce */
    static const char shushanRecoveryScene[] =
        "c14\xCA\xF1\xC9\xBD_02.sce"; /* c14蜀山_02.sce (蜀山长亭) */
    vm_net_mock_scene_recovery_map map;
    vm_net_mock_scene_change_target target;
    vm_net_mock_death_respawn_smap_node respawnSmap[
        VM_NET_MOCK_DEATH_RESPAWN_SMAP_MAX];
    vm_net_mock_death_respawn_wmap_node respawnWmap[
        VM_NET_MOCK_DEATH_RESPAWN_WMAP_MAX];
    vm_net_mock_death_respawn_wmap_line respawnWmapLines[
        VM_NET_MOCK_DEATH_RESPAWN_WMAP_LINE_MAX];
    bool respawnWmapLineEdges[VM_NET_MOCK_DEATH_RESPAWN_WMAP_MAX *
                              VM_NET_MOCK_DEATH_RESPAWN_WMAP_MAX];
    char respawnScene[64];
    u32 sourceRow = 0;
    u32 targetRow = 0;
    u32 distance = 0;
    u32 respawnSmapCount = 0;
    u32 respawnWmapCount = 0;
    u32 respawnWmapLineCount = 0;
    const char *route = NULL;
    u16 x = 427;
    u16 y = 340;

    if (!vm_test_reordered_wmapline_schema_reader())
    {
        fputs("wMapLine schema reader did not resolve reordered endpoint columns\n",
              stderr);
        return 1;
    }

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
        route == NULL || strcmp(route, "wmapline-nearest-city-link-distance") != 0 ||
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
        route == NULL || strcmp(route, "wmapline-nearest-city-link-distance") != 0)
    {
        fputs("Linan ordinary-death respawn did not retain its town centre\n",
              stderr);
        return 1;
    }

    /* Confirm the resource-only city classifier separately before asking for
     * a recovery target, so c00蓬莱仙岛 cannot re-enter the candidate set. */
    if (!vm_net_mock_death_respawn_load_smap_topology(
            respawnSmap, sizeof(respawnSmap) / sizeof(respawnSmap[0]),
            &respawnSmapCount) ||
        !vm_net_mock_death_respawn_load_wmap_topology(
            respawnWmap, sizeof(respawnWmap) / sizeof(respawnWmap[0]),
            &respawnWmapCount) ||
        !vm_net_mock_death_respawn_load_wmap_lines(
            respawnWmapLines,
            sizeof(respawnWmapLines) / sizeof(respawnWmapLines[0]),
            &respawnWmapLineCount) ||
        !vm_net_mock_death_respawn_build_wmap_line_topology(
            respawnWmap, respawnWmapCount, respawnWmapLines,
            respawnWmapLineCount, respawnWmapLineEdges,
            sizeof(respawnWmapLineEdges) / sizeof(respawnWmapLineEdges[0])))
    {
        fputs("Death-respawn topology could not be loaded\n", stderr);
        return 1;
    }
    {
        int penglaiWorldIndex = vm_net_mock_death_respawn_find_wmap_node(
            respawnWmap, respawnWmapCount, 1);
        int shushanWorldIndex = vm_net_mock_death_respawn_find_wmap_node(
            respawnWmap, respawnWmapCount, 14);
        int ghostWorldIndex = vm_net_mock_death_respawn_find_wmap_node(
            respawnWmap, respawnWmapCount, 21);

        if (penglaiWorldIndex < 0 || shushanWorldIndex < 0 ||
            ghostWorldIndex < 0 ||
            vm_net_mock_death_respawn_is_city_world(
                respawnSmap, respawnSmapCount,
                &respawnWmap[penglaiWorldIndex]) ||
            !vm_net_mock_death_respawn_is_city_world(
                respawnSmap, respawnSmapCount,
                &respawnWmap[shushanWorldIndex]) ||
            !respawnWmapLineEdges[
                ghostWorldIndex * respawnWmapCount + shushanWorldIndex] ||
            respawnWmapLineEdges[
                ghostWorldIndex * respawnWmapCount + penglaiWorldIndex])
        {
            fputs("World-map line topology does not separate Ghost Palace from Penglai\n",
                  stderr);
            return 1;
        }
    }
    if (!vm_net_mock_scene_resource_exists(shushanRecoveryScene) ||
        !vm_net_mock_get_scene_reasonable_spawn_from_sce(
            shushanRecoveryScene, &x, &y, NULL))
    {
        fputs("Selected Shushan recovery scene has no resource-backed spawn point\n",
              stderr);
        return 1;
    }
    {
        int ghostSmapIndex = -1;

        for (u32 i = 0; i < respawnSmapCount; ++i)
        {
            if (strcmp(respawnSmap[i].scene, ghostPalaceScene) == 0)
            {
                ghostSmapIndex = (int)i;
                break;
            }
        }
        if (!vm_net_mock_scene_name_is_safe(ghostPalaceScene) ||
            ghostSmapIndex < 0 || respawnSmap[ghostSmapIndex].rowId != 140)
        {
            fprintf(stderr,
                    "Ghost Palace scene key is not an exact safe sMap row: safe=%u index=%d\n",
                    vm_net_mock_scene_name_is_safe(ghostPalaceScene) ? 1u : 0u,
                    ghostSmapIndex);
            return 1;
        }
    }

    /* 幽冥鬼府 has no local safe child scene.  Its rendered wMapLine road
     * joins 蜀山 directly; 蓬莱 is only its directional UI-right entry and
     * has no connecting world-map line. */
    {
        bool ghostResolved = vm_net_mock_resolve_nearest_safe_respawn(
            ghostPalaceScene, respawnScene, sizeof(respawnScene), &x, &y,
            &sourceRow, &targetRow, &distance, &route);

        if (!ghostResolved ||
        strcmp(respawnScene, shushanRecoveryScene) != 0 ||
        sourceRow != 140 || targetRow != 105 || distance != 1 ||
        route == NULL || strcmp(route, "wmapline-nearest-city-link-distance") != 0 ||
        !vm_net_mock_scene_recovery_load_map(respawnScene, &map) ||
        !vm_net_mock_scene_recovery_candidate_is_clear(&map, x, y))
        {
            fprintf(stderr,
                    "Ghost Palace recovery mismatch: resolved=%u scene=%s source=%u target=%u hops=%u route=%s pos=(%u,%u)\n",
                    ghostResolved ? 1u : 0u, respawnScene, sourceRow, targetRow,
                    distance, route != NULL ? route : "-", x, y);
            return 1;
        }
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
