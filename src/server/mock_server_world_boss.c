/*
 * Hourly world boss: 30s map preview, random outdoor spawn, first leader
 * encounter claims fight; spawn removed for others. Included after scene_task.c.
 */

enum
{
    VM_MOCK_WORLD_BOSS_WIRE_MAGIC = 99200u,
    VM_MOCK_WORLD_BOSS_STATUS_IDLE = 0,
    VM_MOCK_WORLD_BOSS_STATUS_PREVIEW = 1,
    VM_MOCK_WORLD_BOSS_STATUS_ACTIVE = 2,
    VM_MOCK_WORLD_BOSS_STATUS_CLAIMED = 3,
    VM_MOCK_WORLD_BOSS_OUTDOOR_MAX = 128,
    VM_MOCK_WORLD_BOSS_RESYNC_MAX = 64
};

typedef struct
{
    u8 enabled;
    u32 monsterId;
    u16 announceSeconds;
    u32 rewardExp;
    u32 rewardMoney;
    u32 rewardItemId;
    u32 rewardItemCount;
} vm_mock_world_boss_config;

typedef struct
{
    u8 status;
    char scene[64];
    u16 x;
    u16 y;
    u32 wireId;
    u32 spawnIndex;
    u32 hourKey;
    u32 previewUnix;
    u32 spawnUnix;
    u32 claimRoleId;
} vm_mock_world_boss_runtime;

static vm_mock_world_boss_config g_vm_mock_world_boss_config;
static bool g_vm_mock_world_boss_config_loaded = false;
static vm_mock_world_boss_runtime g_vm_mock_world_boss_rt;
static u32 g_vm_mock_world_boss_last_tick_sec = 0;
static u32 g_vm_mock_world_boss_rng = 0x9e3779b9u;
static bool g_vm_mock_world_boss_reward_pending = false;
static u32 g_vm_mock_world_boss_resync_clients[VM_MOCK_WORLD_BOSS_RESYNC_MAX];
static u32 g_vm_mock_world_boss_resync_count = 0;
static char g_vm_mock_world_boss_outdoor_scenes[VM_MOCK_WORLD_BOSS_OUTDOOR_MAX][64];
static u32 g_vm_mock_world_boss_outdoor_count = 0;
static bool g_vm_mock_world_boss_outdoor_loaded = false;

/* scene_task.c (same TU) */
bool vm_net_mock_runtime_inject_combat_spawn(
    const char *scene, u16 x, u16 y, u32 monsterId,
    u32 *wireIdOut, u32 *indexOut, const char **errorOut);
bool vm_net_mock_runtime_remove_combat_spawn_at(
    const char *scene, u16 x, u16 y, const char **errorOut);
/* role.c */
u32 vm_net_mock_copy_outdoor_field_scenes(char scenes[][64], u32 cap);
bool vm_net_mock_pick_scene_spawn_xy(const char *scene, u16 *xOut, u16 *yOut);

static bool vm_mock_world_boss_scene_in_update_catalog(const char *scene)
{
    char sceName[72];

    if (scene == NULL || scene[0] == 0)
        return false;
    if (vm_net_mock_update_find_named_for_request(scene) != NULL)
        return true;
    if (strlen(scene) + 4u < sizeof(sceName))
    {
        snprintf(sceName, sizeof(sceName), "%s.sce", scene);
        if (vm_net_mock_update_find_named_for_request(sceName) != NULL)
            return true;
    }
    return false;
}

static u32 vm_mock_world_boss_rng_next(void)
{
    g_vm_mock_world_boss_rng ^= g_vm_mock_world_boss_rng << 13;
    g_vm_mock_world_boss_rng ^= g_vm_mock_world_boss_rng >> 17;
    g_vm_mock_world_boss_rng ^= g_vm_mock_world_boss_rng << 5;
    return g_vm_mock_world_boss_rng;
}

static bool vm_mock_world_boss_scene_is_city(const char *scene)
{
    const char *leaf = scene;
    unsigned char second = 0;

    if (scene == NULL || scene[0] == 0)
        return false;
    for (const char *p = scene; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            leaf = p + 1;
    }
    if (leaf[0] == 'c' && leaf[1] >= '0' && leaf[1] <= '9')
        return true;
    if (leaf[0] >= '1' && leaf[0] <= '9')
    {
        second = (unsigned char)leaf[1];
        if (second >= 0x80u)
            return true;
    }
    return false;
}

static void vm_mock_world_boss_load_outdoor_scenes(void)
{
    if (g_vm_mock_world_boss_outdoor_loaded)
        return;
    g_vm_mock_world_boss_outdoor_loaded = true;
    g_vm_mock_world_boss_outdoor_count =
        vm_net_mock_copy_outdoor_field_scenes(g_vm_mock_world_boss_outdoor_scenes,
                                              VM_MOCK_WORLD_BOSS_OUTDOOR_MAX);
}

static bool vm_mock_world_boss_ensure_tables(void)
{
    static bool ready = false;

    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_world_boss_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "monster_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "announce_seconds SMALLINT UNSIGNED NOT NULL DEFAULT 30,"
            "reward_exp INT UNSIGNED NOT NULL DEFAULT 0,"
            "reward_money INT UNSIGNED NOT NULL DEFAULT 0,"
            "reward_item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "reward_item_count INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY (id)) ENGINE=InnoDB"))
        return false;
    ready = true;
    return true;
}

static bool vm_mock_world_boss_config_row(void *contextValue,
                                          unsigned int columnCount,
                                          const char *const *values,
                                          const size_t *lengths)
{
    vm_mock_world_boss_config *cfg = (vm_mock_world_boss_config *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (cfg == NULL || values == NULL || lengths == NULL)
        return false;
    if (values[0] && lengths[0])
        cfg->enabled = (u8)(values[0][0] == '0' ? 0 : 1);
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        cfg->monsterId = tmp;
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        cfg->announceSeconds = (u16)tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        cfg->rewardExp = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        cfg->rewardMoney = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        cfg->rewardItemId = tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        cfg->rewardItemCount = tmp;
    return true;
}

static void vm_mock_world_boss_load_config(bool force)
{
    if (g_vm_mock_world_boss_config_loaded && !force)
        return;
    (void)vm_mock_world_boss_ensure_tables();
    memset(&g_vm_mock_world_boss_config, 0, sizeof(g_vm_mock_world_boss_config));
    g_vm_mock_world_boss_config.announceSeconds = 30;
    (void)vm_mysql_query(
        "SELECT enabled,monster_id,announce_seconds,reward_exp,reward_money,"
        "reward_item_id,reward_item_count "
        "FROM server_world_boss_config WHERE id=1 LIMIT 1",
        vm_mock_world_boss_config_row, &g_vm_mock_world_boss_config);
    g_vm_mock_world_boss_config_loaded = true;
}

const vm_mock_world_boss_config *vm_net_mock_world_boss_config_get(void)
{
    vm_mock_world_boss_load_config(false);
    return &g_vm_mock_world_boss_config;
}

static u32 vm_mock_world_boss_day_ymd(time_t now)
{
    struct tm *tmNow = localtime(&now);
    if (tmNow == NULL)
        return 0;
    return (u32)((tmNow->tm_year + 1900) * 10000 +
                 (tmNow->tm_mon + 1) * 100 + tmNow->tm_mday);
}

static u32 vm_mock_world_boss_hour_key(time_t now)
{
    struct tm *tmNow = localtime(&now);
    if (tmNow == NULL)
        return 0;
    return vm_mock_world_boss_day_ymd(now) * 100u + (u32)tmNow->tm_hour;
}

static bool vm_mock_world_boss_lookup_smap_alias_gbk(const char *scene,
                                                     char *out, size_t outCap)
{
    char path[256];
    u8 data[16384];
    u32 len = 0;
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 headerBytes = 0;
    u32 pos = 0;

    if (out == NULL || outCap == 0 || scene == NULL || scene[0] == 0)
        return false;
    out[0] = 0;
    if (!vm_net_mock_open_server_data_resource("sMap.dsh", ".dsh", NULL, path,
                                               sizeof(path)))
        return false;
    len = vm_net_mock_load_response_file(path, data, sizeof(data));
    if (len < 20 || vm_net_mock_read_le32_at(data, 0) != len - 4)
        return false;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    headerBytes = vm_net_mock_read_le32_at(data, 12);
    if (columnCount < 3 || columnCount > 64 || rowCount > 512 ||
        16u + headerBytes > len)
        return false;
    pos = 16u + headerBytes;
    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        char rowScene[64];
        char alias[32];

        memset(rowScene, 0, sizeof(rowScene));
        memset(alias, 0, sizeof(alias));
        if (rowLen == 0 || rowEnd > len || rowEnd < rowPos)
            break;
        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;

            if (rowPos + valueLen > rowEnd)
                break;
            if (col == 1)
                (void)vm_net_mock_copy_dsh_string_field(rowScene, sizeof(rowScene),
                                                         value, valueLen);
            else if (col == 2)
                (void)vm_net_mock_copy_dsh_string_field(alias, sizeof(alias),
                                                         value, valueLen);
            rowPos += valueLen;
        }
        pos = rowEnd;
        if (rowScene[0] == 0 || alias[0] == 0)
            continue;
        if (vm_net_mock_scene_names_equal_loose(scene, rowScene))
        {
            snprintf(out, outCap, "%s", alias);
            return out[0] != 0;
        }
    }
    return false;
}

static void vm_mock_world_boss_resolve_map_label_gbk(const char *scene,
                                                     char *out, size_t outCap)
{
    char fromDb[64];
    char fromSmap[64];
    char leaf[64];
    size_t leafLen = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (scene == NULL || scene[0] == 0)
        return;

    memset(fromDb, 0, sizeof(fromDb));
    if (vm_net_mock_scene_display_name_load(scene, fromDb, sizeof(fromDb)) &&
        fromDb[0] != 0)
    {
        snprintf(out, outCap, "%s", fromDb);
        return;
    }

    memset(fromSmap, 0, sizeof(fromSmap));
    if (vm_mock_world_boss_lookup_smap_alias_gbk(scene, fromSmap,
                                                 sizeof(fromSmap)) &&
        fromSmap[0] != 0)
    {
        snprintf(out, outCap, "%s", fromSmap);
        return;
    }

    /* Last resort: strip path / .sce / leading ASCII index so chat never
     * shows a raw resource leaf like 17无情谷_01.sce. */
    {
        const char *src = scene;
        const char *p = NULL;
        char *dst = leaf;

        for (p = scene; *p != '\0'; ++p)
        {
            if (*p == '/' || *p == '\\')
                src = p + 1;
        }
        while (*src == 'c' || *src == 'C' || (*src >= '0' && *src <= '9'))
            ++src;
        snprintf(leaf, sizeof(leaf), "%s", src);
        leafLen = strlen(leaf);
        if (leafLen > 4 && leaf[leafLen - 4] == '.' &&
            (leaf[leafLen - 3] == 's' || leaf[leafLen - 3] == 'S') &&
            (leaf[leafLen - 2] == 'c' || leaf[leafLen - 2] == 'C') &&
            (leaf[leafLen - 1] == 'e' || leaf[leafLen - 1] == 'E'))
        {
            leaf[leafLen - 4] = 0;
            leafLen -= 4;
        }
        /* Drop trailing _NN ordinal when present (ASCII only). */
        if (leafLen > 3 && leaf[leafLen - 3] == '_' &&
            leaf[leafLen - 2] >= '0' && leaf[leafLen - 2] <= '9' &&
            leaf[leafLen - 1] >= '0' && leaf[leafLen - 1] <= '9')
        {
            leaf[leafLen - 3] = 0;
        }
        (void)dst;
        snprintf(out, outCap, "%s", leaf[0] ? leaf : scene);
    }
}

static void vm_mock_world_boss_broadcast_preview(const char *sceneGbk)
{
    char message[160];
    /* GBK: 【世界BOSS】30秒后将出现在地图： */
    static const char prefixGbk[] =
        "\xa1\xbe\xca\xc0\xbd\xe7\x42\x4f\x53\x53\xa1\xbf\x33\x30\xc3\xeb\xba\xf3"
        "\xbd\xab\xb3\xf6\xcf\xd6\xd4\xda\xb5\xd8\xcd\xbc\xa3\xba";
    static const char systemNameGbk[] = "\xcf\xb5\xcd\xb3";

    if (sceneGbk == NULL || sceneGbk[0] == 0)
        return;
    snprintf(message, sizeof(message), "%s%s", prefixGbk, sceneGbk);
    (void)vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][mock-service] world_boss_preview scene=%s\n", sceneGbk);
}

static void vm_mock_world_boss_broadcast_spawn(const char *sceneGbk, u16 x, u16 y)
{
    char message[160];
    static const char prefixGbk[] =
        "\xa1\xbe\xca\xc0\xbd\xe7\x42\x4f\x53\x53\xa1\xbf\xd2\xd1\xb3\xf6\xcf\xd6"
        "\xd4\xda\xb5\xd8\xcd\xbc\xa1\xbe";
    /* GBK: 】 */
    static const char suffixGbk[] = "\xa1\xbf";
    static const char systemNameGbk[] = "\xcf\xb5\xcd\xb3";

    if (sceneGbk == NULL || sceneGbk[0] == 0)
        return;
    snprintf(message, sizeof(message), "%s%s%s pos=(%u,%u)", prefixGbk,
             sceneGbk, suffixGbk, (u32)x, (u32)y);
    (void)vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][mock-service] world_boss_spawn scene=%s pos=(%u,%u)\n",
           sceneGbk, (u32)x, (u32)y);
}

static void vm_mock_world_boss_queue_scene_resync_for(const char *scene)
{
    vm_mock_service_client_session *sess;

    if (scene == NULL || scene[0] == 0)
        return;
    g_vm_mock_world_boss_resync_count = 0;
    for (sess = g_vm_mock_service_client_sessions; sess != NULL;
         sess = sess->next)
    {
        const char *vis =
            sess->sceneVisibleScene[0] ? sess->sceneVisibleScene
                                       : sess->onlineScene;
        if (!sess->roleOnline || sess->onlineRoleId == 0)
            continue;
        if (vis == NULL || !vm_net_mock_scene_names_equal_loose(vis, scene))
            continue;
        if (g_vm_mock_world_boss_resync_count >= VM_MOCK_WORLD_BOSS_RESYNC_MAX)
            break;
        g_vm_mock_world_boss_resync_clients[g_vm_mock_world_boss_resync_count++] =
            sess->clientId;
        /* Pixi refreshes from [WBOSS] chat; CBE/WT18 cold path may also use this. */
        sess->dreamForceColdSceResync = true;
    }
}

static void vm_mock_world_boss_broadcast_despawn(const char *sceneGbk, u16 x,
                                                 u16 y)
{
    char message[192];
    /* GBK: 世界Boss 已刷新 （世=CA C0，勿写成 CA E0=枢） */
    static const char prefixGbk[] =
        "\xca\xc0\xbd\xe7\x42\x6f\x73\x73\xd2\xd1\xcb\xa2\xd0\xc2\x20";
    /* GBK:  坐标 */
    static const char coordGbk[] = " \xd7\xf9\xb1\xea";
    static const char systemNameGbk[] = "\xcf\xb5\xcd\xb3";

    if (sceneGbk == NULL || sceneGbk[0] == 0)
        return;
    snprintf(message, sizeof(message), "%s%s%s(%u,%u)", prefixGbk, sceneGbk,
             coordGbk, (u32)x, (u32)y);
    (void)vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][mock-service] world_boss_despawn_chat scene=%s pos=(%u,%u)\n",
           sceneGbk, (u32)x, (u32)y);
}

static void vm_mock_world_boss_broadcast_marker(const char *markerAscii,
                                                const char *sceneGbk, u16 x,
                                                u16 y)
{
    char message[192];
    static const char systemNameGbk[] = "\xcf\xb5\xcd\xb3";

    if (markerAscii == NULL || markerAscii[0] == 0)
        return;
    if (sceneGbk != NULL && sceneGbk[0] != 0)
        snprintf(message, sizeof(message), "%s %s pos=(%u,%u)", markerAscii,
                 sceneGbk, (u32)x, (u32)y);
    else
        snprintf(message, sizeof(message), "%s", markerAscii);
    (void)vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
}

/*
 * Victory live [世]: solo 「恭喜玩家{名}战胜世界BOSS！」 /
 * party 「恭喜{名}的队伍战胜世界BOSS！」.  ≤81 GBK; avoid 【世界BOSS】 /
 * [WBOSS:] / 「世界Boss 已刷新」 so Pixi map-sync chat filters stay quiet.
 */
static void vm_mock_world_boss_broadcast_victory(vm_net_mock_role_state *role)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    vm_mock_service_client_session *session = NULL;
    vm_mock_service_client_session *sess = NULL;
    vm_mock_service_team *team = NULL;
    const char *playerName = NULL;
    char message[82];
    int wrote = 0;
    u32 recipients = 0;
    u32 claimId = g_vm_mock_world_boss_rt.claimRoleId;
    bool isTeam = false;

    if (claimId != 0)
    {
        for (sess = g_vm_mock_service_client_sessions; sess != NULL;
             sess = sess->next)
        {
            if (sess->onlineRoleId != claimId)
                continue;
            session = sess;
            if (sess->onlineRoleName[0] != 0)
                playerName = sess->onlineRoleName;
            break;
        }
    }
    if (session == NULL)
        session = vm_mock_service_get_active_client_session();
    if (playerName == NULL && session != NULL && session->onlineRoleName[0] != 0)
        playerName = session->onlineRoleName;
    if (playerName == NULL && role != NULL && role->name[0] != 0)
        playerName = role->name;
    if (playerName == NULL || playerName[0] == 0)
        return;

    if (session != NULL)
        team = vm_mock_service_team_find_for_client(session->clientId);
    if (team != NULL && team->active)
    {
        if (team->battleActive && team->battleMemberCount >= 2)
            isTeam = true;
        else if (team->memberCount >= 2)
            isTeam = true;
    }

    memset(message, 0, sizeof(message));
    if (isTeam)
    {
        /* 恭喜{名}的队伍战胜世界BOSS！ */
        wrote = snprintf(
            message, sizeof(message),
            "\xB9\xA7\xCF\xB2%s\xB5\xC4\xB6\xD3\xCE\xE9"
            "\xD5\xBD\xCA\xA4\xCA\xC0\xBD\xE7\x42\x4F\x53\x53\xA3\xA1",
            playerName);
        if (wrote <= 0 || (size_t)wrote >= sizeof(message))
        {
            memset(message, 0, sizeof(message));
            wrote = snprintf(
                message, sizeof(message),
                "\xB9\xA7\xCF\xB2\xB6\xD3\xCE\xE9"
                "\xD5\xBD\xCA\xA4\xCA\xC0\xBD\xE7\x42\x4F\x53\x53\xA3\xA1");
        }
    }
    else
    {
        /* 恭喜玩家{名}战胜世界BOSS！ */
        wrote = snprintf(
            message, sizeof(message),
            "\xB9\xA7\xCF\xB2\xCD\xE6\xBC\xD2%s"
            "\xD5\xBD\xCA\xA4\xCA\xC0\xBD\xE7\x42\x4F\x53\x53\xA3\xA1",
            playerName);
        if (wrote <= 0 || (size_t)wrote >= sizeof(message))
        {
            memset(message, 0, sizeof(message));
            wrote = snprintf(
                message, sizeof(message),
                "\xB9\xA7\xCF\xB2\xCD\xE6\xBC\xD2"
                "\xD5\xBD\xCA\xA4\xCA\xC0\xBD\xE7\x42\x4F\x53\x53\xA3\xA1");
        }
    }
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
        return;
    recipients = vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][mock-service] world_boss_victory_broadcast team=%u "
           "player=%s claim=%u recipients=%u tip=%s\n",
           isTeam ? 1u : 0u, playerName, claimId, recipients, message);
}

static bool vm_mock_world_boss_pick_scene_and_pos_at(
    u32 outdoorIndex, char *sceneOut, size_t sceneCap, u16 *xOut, u16 *yOut)
{
    const char *scene = NULL;
    u16 x = 120;
    u16 y = 120;

    vm_mock_world_boss_load_outdoor_scenes();
    if (g_vm_mock_world_boss_outdoor_count == 0 ||
        outdoorIndex >= g_vm_mock_world_boss_outdoor_count)
        return false;
    scene = g_vm_mock_world_boss_outdoor_scenes[outdoorIndex];
    if (scene == NULL || scene[0] == 0)
        return false;
    if (sceneOut != NULL && sceneCap > 0)
        snprintf(sceneOut, sceneCap, "%s", scene);
    (void)vm_net_mock_pick_scene_spawn_xy(scene, &x, &y);
    if (x == 0)
        x = 120;
    if (y == 0)
        y = 120;
    if (xOut)
        *xOut = x;
    if (yOut)
        *yOut = y;
    return true;
}

static bool vm_mock_world_boss_pick_scene_and_pos(char *sceneOut, size_t sceneCap,
                                                  u16 *xOut, u16 *yOut)
{
    u32 pick = 0;

    vm_mock_world_boss_load_outdoor_scenes();
    if (g_vm_mock_world_boss_outdoor_count == 0)
        return false;
    pick = vm_mock_world_boss_rng_next() % g_vm_mock_world_boss_outdoor_count;
    return vm_mock_world_boss_pick_scene_and_pos_at(pick, sceneOut, sceneCap,
                                                     xOut, yOut);
}

static void vm_mock_world_boss_despawn_active(const char *reason)
{
    const char *despawnError = NULL;
    char sceneCopy[64];
    u16 x = 0;
    u16 y = 0;
    char sceneGbk[128];
    u8 prevStatus = 0;

    if (g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_IDLE &&
        g_vm_mock_world_boss_rt.scene[0] == 0)
        return;
    prevStatus = g_vm_mock_world_boss_rt.status;
    snprintf(sceneCopy, sizeof(sceneCopy), "%s", g_vm_mock_world_boss_rt.scene);
    x = g_vm_mock_world_boss_rt.x;
    y = g_vm_mock_world_boss_rt.y;
    /* Only ACTIVE rows live in SCE; CLAIMED already removed on encounter. */
    if (prevStatus == VM_MOCK_WORLD_BOSS_STATUS_ACTIVE && sceneCopy[0] != 0 &&
        x != 0 && y != 0)
    {
        (void)vm_net_mock_runtime_remove_combat_spawn_at(sceneCopy, x, y,
                                                         &despawnError);
        vm_mock_world_boss_queue_scene_resync_for(sceneCopy);
        memset(sceneGbk, 0, sizeof(sceneGbk));
        vm_mock_world_boss_resolve_map_label_gbk(sceneCopy, sceneGbk,
                                                 sizeof(sceneGbk));
        /* ASCII tag for Pixi same-map combat-spawn refresh. */
        vm_mock_world_boss_broadcast_despawn(sceneGbk, x, y);
    }
    else if (prevStatus == VM_MOCK_WORLD_BOSS_STATUS_CLAIMED &&
             sceneCopy[0] != 0)
    {
        /* Map already cleared on claim; still notify clients + resync. */
        vm_mock_world_boss_queue_scene_resync_for(sceneCopy);
        memset(sceneGbk, 0, sizeof(sceneGbk));
        vm_mock_world_boss_resolve_map_label_gbk(sceneCopy, sceneGbk,
                                                 sizeof(sceneGbk));
        vm_mock_world_boss_broadcast_despawn(sceneGbk, x, y);
    }
    printf("[info][mock-service] world_boss_despawn reason=%s status=%u "
           "scene=%s pos=(%u,%u) err=%s\n",
           reason ? reason : "-", (u32)prevStatus, sceneCopy, (u32)x, (u32)y,
           despawnError ? despawnError : "-");
    memset(&g_vm_mock_world_boss_rt, 0, sizeof(g_vm_mock_world_boss_rt));
    g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_IDLE;
    g_vm_mock_world_boss_reward_pending = false;
}

static bool vm_mock_world_boss_spawn_now(u32 hourKey, bool adminPureRandom,
                                         const char **errorOut)
{
    char scene[64];
    u16 x = 0;
    u16 y = 0;
    u32 wireId = 0;
    u32 spawnIndex = 0;
    const char *spawnError = NULL;
    const char *lastError = "刷BOSS失败";
    char sceneGbk[128];
    u32 start = 0;
    u32 tries = 0;
    u32 maxTries = 0;
    u32 order[VM_MOCK_WORLD_BOSS_OUTDOOR_MAX];
    u32 orderCount = 0;

    if (errorOut)
        *errorOut = lastError;
    if (!g_vm_mock_world_boss_config.enabled)
    {
        if (errorOut)
            *errorOut = "世界BOSS未启用";
        return false;
    }
    if (g_vm_mock_world_boss_config.monsterId == 0)
    {
        if (errorOut)
            *errorOut = "请先配置BOSS怪物ID";
        return false;
    }
    /* Clear ACTIVE / CLAIMED / PREVIEW so admin can re-force any time. */
    if (g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_IDLE ||
        g_vm_mock_world_boss_rt.scene[0] != 0)
        vm_mock_world_boss_despawn_active("respawn");

    vm_mock_world_boss_load_outdoor_scenes();
    if (g_vm_mock_world_boss_outdoor_count == 0)
    {
        if (errorOut)
            *errorOut = "无可用户外地图";
        return false;
    }
    memset(order, 0, sizeof(order));
    orderCount = g_vm_mock_world_boss_outdoor_count;
    if (adminPureRandom)
    {
        /* Admin force-spawn: uniform random over the outdoor pool (no catalog bias). */
        g_vm_mock_world_boss_rng ^=
            (u32)time(NULL) ^ (u32)(hourKey * 0x9e3779b9u);
        for (u32 i = 0; i < orderCount; ++i)
            order[i] = i;
    }
    else
    {
        orderCount = 0;
        for (u32 i = 0; i < g_vm_mock_world_boss_outdoor_count; ++i)
        {
            if (vm_mock_world_boss_scene_in_update_catalog(
                    g_vm_mock_world_boss_outdoor_scenes[i]))
            {
                if (orderCount < VM_MOCK_WORLD_BOSS_OUTDOOR_MAX)
                    order[orderCount++] = i;
            }
        }
        for (u32 i = 0; i < g_vm_mock_world_boss_outdoor_count; ++i)
        {
            if (vm_mock_world_boss_scene_in_update_catalog(
                    g_vm_mock_world_boss_outdoor_scenes[i]))
                continue;
            if (orderCount < VM_MOCK_WORLD_BOSS_OUTDOOR_MAX)
                order[orderCount++] = i;
        }
    }
    if (orderCount == 0)
    {
        if (errorOut)
            *errorOut = "无可用户外地图";
        return false;
    }
    start = vm_mock_world_boss_rng_next() % orderCount;
    maxTries = orderCount;
    if (maxTries > 12u)
        maxTries = 12u;
    for (tries = 0; tries < maxTries; ++tries)
    {
        u32 idx = order[(start + tries) % orderCount];

        memset(scene, 0, sizeof(scene));
        x = 0;
        y = 0;
        wireId = 0;
        spawnIndex = 0;
        spawnError = NULL;
        if (!vm_mock_world_boss_pick_scene_and_pos_at(idx, scene, sizeof(scene),
                                                      &x, &y))
        {
            lastError = "户外地图选点失败";
            continue;
        }
        if (!vm_net_mock_runtime_inject_combat_spawn(
                scene, x, y, g_vm_mock_world_boss_config.monsterId, &wireId,
                &spawnIndex, &spawnError))
        {
            lastError = spawnError ? spawnError : "刷怪点写入失败";
            printf("[warn][mock-service] world_boss_spawn_retry scene=%s "
                   "pos=(%u,%u) err=%s try=%u/%u\n",
                   scene, (u32)x, (u32)y, lastError, tries + 1u, maxTries);
            continue;
        }
        memset(&g_vm_mock_world_boss_rt, 0, sizeof(g_vm_mock_world_boss_rt));
        g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_ACTIVE;
        snprintf(g_vm_mock_world_boss_rt.scene,
                 sizeof(g_vm_mock_world_boss_rt.scene), "%s", scene);
        g_vm_mock_world_boss_rt.x = x;
        g_vm_mock_world_boss_rt.y = y;
        g_vm_mock_world_boss_rt.wireId = wireId;
        g_vm_mock_world_boss_rt.spawnIndex = spawnIndex;
        g_vm_mock_world_boss_rt.hourKey = hourKey;
        g_vm_mock_world_boss_rt.spawnUnix = (u32)time(NULL);
        memset(sceneGbk, 0, sizeof(sceneGbk));
        vm_mock_world_boss_resolve_map_label_gbk(scene, sceneGbk,
                                                  sizeof(sceneGbk));
        vm_mock_world_boss_broadcast_spawn(sceneGbk, x, y);
        vm_mock_world_boss_queue_scene_resync_for(scene);
        if (errorOut)
            *errorOut = "ok";
        return true;
    }
    printf("[error][mock-service] world_boss_spawn_fail tries=%u err=%s\n",
           maxTries, lastError ? lastError : "-");
    if (errorOut)
        *errorOut = lastError ? lastError : "刷BOSS失败";
    return false;
}

static void vm_mock_world_boss_tick(void)
{
    time_t now = time(NULL);
    u32 secInHour = 0;
    u32 hourKey = 0;
    u16 announceSec = 30;
    char scene[64];
    char sceneGbk[128];

    vm_mock_world_boss_load_config(false);
    if (!g_vm_mock_world_boss_config.enabled)
        return;
    if ((u32)now == g_vm_mock_world_boss_last_tick_sec)
        return;
    g_vm_mock_world_boss_last_tick_sec = (u32)now;
    secInHour = (u32)now % 3600u;
    hourKey = vm_mock_world_boss_hour_key(now);
    announceSec = g_vm_mock_world_boss_config.announceSeconds;
    if (announceSec == 0 || announceSec > 300)
        announceSec = 30;

    if (secInHour == 0u && g_vm_mock_world_boss_rt.hourKey != hourKey)
    {
        (void)vm_mock_world_boss_spawn_now(hourKey, false, NULL);
        return;
    }
    if (secInHour + announceSec == 3600u &&
        g_vm_mock_world_boss_rt.previewUnix != hourKey)
    {
        memset(scene, 0, sizeof(scene));
        if (!vm_mock_world_boss_pick_scene_and_pos(scene, sizeof(scene), NULL, NULL))
            return;
        g_vm_mock_world_boss_rt.previewUnix = hourKey;
        g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_PREVIEW;
        snprintf(g_vm_mock_world_boss_rt.scene, sizeof(g_vm_mock_world_boss_rt.scene),
                 "%s", scene);
        memset(sceneGbk, 0, sizeof(sceneGbk));
        vm_mock_world_boss_resolve_map_label_gbk(scene, sceneGbk, sizeof(sceneGbk));
        vm_mock_world_boss_broadcast_preview(sceneGbk);
    }
}

bool vm_mock_world_boss_try_claim_encounter(
    const char *scene, u32 wireId, u32 posx, u32 posy, u32 *enemyIdOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    bool posHit = false;
    bool idHit = false;
    u32 dx = 0;
    u32 dy = 0;

    if (enemyIdOut)
        *enemyIdOut = 0;
    if (g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_ACTIVE)
        return false;
    if (scene == NULL || !vm_net_mock_scene_names_equal_loose(
                             scene, g_vm_mock_world_boss_rt.scene))
        return false;
    /*
     * Pixi hangup/collide may upload patrol feet instead of SCE home tiles.
     * Accept exact home, nearby feet, or wire/monster id on the boss scene.
     */
    posHit = (posx == g_vm_mock_world_boss_rt.x &&
              posy == g_vm_mock_world_boss_rt.y);
    if (!posHit && posx != 0 && posy != 0)
    {
        dx = posx > g_vm_mock_world_boss_rt.x
                 ? posx - g_vm_mock_world_boss_rt.x
                 : g_vm_mock_world_boss_rt.x - posx;
        dy = posy > g_vm_mock_world_boss_rt.y
                 ? posy - g_vm_mock_world_boss_rt.y
                 : g_vm_mock_world_boss_rt.y - posy;
        if (dx <= 96u && dy <= 96u)
            posHit = true;
    }
    idHit = (wireId != 0 &&
             (wireId == g_vm_mock_world_boss_rt.wireId ||
              wireId == g_vm_mock_world_boss_config.monsterId));
    if (!posHit && !idHit)
        return false;
    g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_CLAIMED;
    g_vm_mock_world_boss_rt.claimRoleId = role ? role->roleId : 0;
    /* Only strip the boss home tile — never wipe every same-species wild row. */
    (void)vm_net_mock_runtime_remove_combat_spawn_at(
        g_vm_mock_world_boss_rt.scene, g_vm_mock_world_boss_rt.x,
        g_vm_mock_world_boss_rt.y, NULL);
    vm_mock_world_boss_queue_scene_resync_for(g_vm_mock_world_boss_rt.scene);
    {
        char sceneGbk[128];

        memset(sceneGbk, 0, sizeof(sceneGbk));
        vm_mock_world_boss_resolve_map_label_gbk(g_vm_mock_world_boss_rt.scene,
                                                  sceneGbk, sizeof(sceneGbk));
        vm_mock_world_boss_broadcast_marker(
            "[WBOSS:claim]", sceneGbk, g_vm_mock_world_boss_rt.x,
            g_vm_mock_world_boss_rt.y);
    }
    if (enemyIdOut)
        *enemyIdOut = g_vm_mock_world_boss_config.monsterId;
    g_vm_mock_world_boss_reward_pending = true;
    printf("[info][mock-service] world_boss_claim role=%u scene=%s "
           "pos=(%u,%u) wire=%u home=(%u,%u) enemy=%u posHit=%u idHit=%u\n",
           role ? role->roleId : 0, scene, posx, posy, wireId,
           (u32)g_vm_mock_world_boss_rt.x, (u32)g_vm_mock_world_boss_rt.y,
           g_vm_mock_world_boss_config.monsterId, posHit ? 1u : 0u,
           idHit ? 1u : 0u);
    return true;
}

void vm_mock_world_boss_on_battle_end(bool victory)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    bool wasClaimed =
        g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_CLAIMED ||
        g_vm_mock_world_boss_reward_pending;

    if (!g_vm_mock_world_boss_reward_pending && !wasClaimed)
        return;
    g_vm_mock_world_boss_reward_pending = false;
    if (!victory)
    {
        printf("[info][mock-service] world_boss_battle_end victory=0\n");
        /* Defeat still ends this hour's claim — map already empty; clear slot. */
        if (wasClaimed)
            vm_mock_world_boss_despawn_active("battle-defeat");
        return;
    }
    if (role != NULL)
    {
        u16 itemSeq = 0;
        if (g_vm_mock_world_boss_config.rewardExp != 0)
            (void)vm_net_mock_role_add_exp(role, g_vm_mock_world_boss_config.rewardExp);
        if (g_vm_mock_world_boss_config.rewardMoney != 0)
        {
            role->money = vm_net_mock_add_capped_u32(
                role->money, g_vm_mock_world_boss_config.rewardMoney);
            vm_net_mock_role_mark_inventory_dirty("world-boss-money");
        }
        if (g_vm_mock_world_boss_config.rewardItemId != 0 &&
            g_vm_mock_world_boss_config.rewardItemCount != 0)
        {
            (void)vm_net_mock_role_add_backpack_item_to_role(
                role, g_vm_mock_world_boss_config.rewardItemId,
                g_vm_mock_world_boss_config.rewardItemCount, 0, &itemSeq,
                "world-boss-item", NULL, 0);
        }
        vm_net_mock_role_db_save("world-boss-reward");
    }
    printf("[info][mock-service] world_boss_reward role=%u exp=%u money=%u "
           "item=%u x%u\n",
           role ? role->roleId : 0, g_vm_mock_world_boss_config.rewardExp,
           g_vm_mock_world_boss_config.rewardMoney,
           g_vm_mock_world_boss_config.rewardItemId,
           g_vm_mock_world_boss_config.rewardItemCount);
    vm_mock_world_boss_broadcast_victory(role);
    vm_mock_world_boss_despawn_active("battle-victory");
}

u32 vm_mock_world_boss_runtime_status(void)
{
    return (u32)g_vm_mock_world_boss_rt.status;
}

void vm_mock_world_boss_admin_runtime_get(u8 *statusOut, char *sceneOut,
                                          size_t sceneCap, u16 *xOut, u16 *yOut)
{
    if (statusOut)
        *statusOut = g_vm_mock_world_boss_rt.status;
    if (sceneOut != NULL && sceneCap > 0)
        snprintf(sceneOut, sceneCap, "%s", g_vm_mock_world_boss_rt.scene);
    if (xOut)
        *xOut = g_vm_mock_world_boss_rt.x;
    if (yOut)
        *yOut = g_vm_mock_world_boss_rt.y;
}

static bool vm_mock_world_boss_admin_pick_preview_scene(char *msgOut, size_t msgCap)
{
    char scene[64];

    if (!vm_mock_world_boss_pick_scene_and_pos(scene, sizeof(scene), NULL, NULL))
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "无可用户外地图");
        return false;
    }
    snprintf(g_vm_mock_world_boss_rt.scene, sizeof(g_vm_mock_world_boss_rt.scene),
             "%s", scene);
    return true;
}

bool vm_mock_world_boss_admin_force_preview(char *msgOut, size_t msgCap)
{
    char sceneGbk[128];
    u32 hourKey = 0;

    vm_mock_world_boss_load_config(false);
    if (!g_vm_mock_world_boss_config.enabled)
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "世界BOSS未启用");
        return false;
    }
    if (!vm_mock_world_boss_admin_pick_preview_scene(msgOut, msgCap))
        return false;
    hourKey = vm_mock_world_boss_hour_key(time(NULL));
    g_vm_mock_world_boss_rt.previewUnix = hourKey;
    g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_PREVIEW;
    memset(sceneGbk, 0, sizeof(sceneGbk));
    vm_mock_world_boss_resolve_map_label_gbk(g_vm_mock_world_boss_rt.scene,
                                              sceneGbk, sizeof(sceneGbk));
    vm_mock_world_boss_broadcast_preview(sceneGbk);
    if (msgOut && msgCap > 0)
        snprintf(msgOut, msgCap, "已发送全服预告");
    return true;
}

bool vm_mock_world_boss_admin_force_spawn(char *msgOut, size_t msgCap)
{
    const char *spawnError = NULL;

    vm_mock_world_boss_load_config(false);
    if (!g_vm_mock_world_boss_config.enabled ||
        g_vm_mock_world_boss_config.monsterId == 0)
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "请先启用并配置BOSS怪物ID");
        return false;
    }
    if (!vm_mock_world_boss_spawn_now(vm_mock_world_boss_hour_key(time(NULL)),
                                       true, &spawnError))
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "%s",
                     spawnError && spawnError[0] ? spawnError
                                                 : "刷BOSS失败");
        return false;
    }
    if (msgOut && msgCap > 0)
        snprintf(msgOut, msgCap, "已立即刷新世界BOSS");
    return true;
}

bool vm_mock_world_boss_admin_force_despawn(char *msgOut, size_t msgCap)
{
    if (g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_IDLE)
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "当前无活跃BOSS");
        return false;
    }
    vm_mock_world_boss_despawn_active("admin");
    if (msgOut && msgCap > 0)
        snprintf(msgOut, msgCap, "已清除当前BOSS");
    return true;
}

bool vm_mock_world_boss_admin_save(
    u8 enabled, u32 monsterId, u16 announceSeconds,
    u32 rewardExp, u32 rewardMoney, u32 rewardItemId, u32 rewardItemCount)
{
    char query[512];

    if (!vm_mock_world_boss_ensure_tables())
        return false;
    snprintf(
        query, sizeof(query),
        "INSERT INTO server_world_boss_config "
        "(id,enabled,monster_id,announce_seconds,reward_exp,reward_money,"
        "reward_item_id,reward_item_count) VALUES "
        "(1,%u,%u,%u,%u,%u,%u,%u) "
        "ON DUPLICATE KEY UPDATE "
        "enabled=VALUES(enabled),monster_id=VALUES(monster_id),"
        "announce_seconds=VALUES(announce_seconds),"
        "reward_exp=VALUES(reward_exp),reward_money=VALUES(reward_money),"
        "reward_item_id=VALUES(reward_item_id),"
        "reward_item_count=VALUES(reward_item_count)",
        (u32)enabled, monsterId, (u32)announceSeconds, rewardExp, rewardMoney,
        rewardItemId, rewardItemCount);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_world_boss_config_loaded = false;
    vm_mock_world_boss_load_config(true);
    return true;
}
