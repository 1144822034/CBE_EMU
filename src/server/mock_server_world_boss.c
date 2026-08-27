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

static void vm_mock_world_boss_scene_utf8_to_gbk(const char *sceneUtf8,
                                                 char *gbkOut, size_t gbkCap)
{
    if (gbkOut == NULL || gbkCap == 0)
        return;
    gbkOut[0] = 0;
    if (sceneUtf8 == NULL || sceneUtf8[0] == 0)
        return;
    utf8_to_gbk((u8 *)sceneUtf8, (u8 *)gbkOut, gbkCap);
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
    static const char midGbk[] = "\xa1\xbf\xc0\xb4\xd7\xf7\xd3\xce\xcf\xb7\xa3\xa1";
    static const char systemNameGbk[] = "\xcf\xb5\xcd\xb3";

    if (sceneGbk == NULL || sceneGbk[0] == 0)
        return;
    snprintf(message, sizeof(message), "%s%s%s pos=(%u,%u)",
             prefixGbk, sceneGbk, midGbk, (u32)x, (u32)y);
    (void)vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][mock-service] world_boss_spawn scene=%s pos=(%u,%u)\n",
           sceneGbk, (u32)x, (u32)y);
}

static void vm_mock_world_boss_queue_scene_resync(void)
{
    vm_mock_service_client_session *sess;

    g_vm_mock_world_boss_resync_count = 0;
    for (sess = g_vm_mock_service_client_sessions; sess != NULL;
         sess = sess->next)
    {
        const char *vis =
            sess->sceneVisibleScene[0] ? sess->sceneVisibleScene
                                       : sess->onlineScene;
        if (!sess->roleOnline || sess->onlineRoleId == 0)
            continue;
        if (vis == NULL || !vm_net_mock_scene_names_equal_loose(
                               vis, g_vm_mock_world_boss_rt.scene))
            continue;
        if (g_vm_mock_world_boss_resync_count >= VM_MOCK_WORLD_BOSS_RESYNC_MAX)
            break;
        g_vm_mock_world_boss_resync_clients[g_vm_mock_world_boss_resync_count++] =
            sess->clientId;
        sess->dreamForceColdSceResync = true;
    }
}

static bool vm_mock_world_boss_pick_scene_and_pos(char *sceneOut, size_t sceneCap,
                                                  u16 *xOut, u16 *yOut)
{
    u32 pick = 0;
    const char *scene = NULL;
    u16 x = 120;
    u16 y = 120;

    vm_mock_world_boss_load_outdoor_scenes();
    if (g_vm_mock_world_boss_outdoor_count == 0)
        return false;
    pick = vm_mock_world_boss_rng_next() % g_vm_mock_world_boss_outdoor_count;
    scene = g_vm_mock_world_boss_outdoor_scenes[pick];
    if (sceneOut != NULL && sceneCap > 0)
        snprintf(sceneOut, sceneCap, "%s", scene);
    (void)vm_net_mock_pick_scene_spawn_xy(scene, &x, &y);
    if (xOut)
        *xOut = x;
    if (yOut)
        *yOut = y;
    return true;
}

static void vm_mock_world_boss_despawn_active(const char *reason)
{
    const char *despawnError = NULL;

    if (g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_IDLE)
        return;
    if (g_vm_mock_world_boss_rt.scene[0] != 0 &&
        g_vm_mock_world_boss_rt.x != 0 && g_vm_mock_world_boss_rt.y != 0)
    {
        (void)vm_net_mock_runtime_remove_combat_spawn_at(
            g_vm_mock_world_boss_rt.scene, g_vm_mock_world_boss_rt.x,
            g_vm_mock_world_boss_rt.y, &despawnError);
    }
    printf("[info][mock-service] world_boss_despawn reason=%s scene=%s "
           "pos=(%u,%u) err=%s\n",
           reason ? reason : "-", g_vm_mock_world_boss_rt.scene,
           (u32)g_vm_mock_world_boss_rt.x, (u32)g_vm_mock_world_boss_rt.y,
           despawnError ? despawnError : "-");
    memset(&g_vm_mock_world_boss_rt, 0, sizeof(g_vm_mock_world_boss_rt));
    g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_IDLE;
}

static void vm_mock_world_boss_spawn_now(u32 hourKey)
{
    char scene[64];
    u16 x = 0;
    u16 y = 0;
    u32 wireId = 0;
    u32 spawnIndex = 0;
    const char *spawnError = NULL;
    char sceneUtf8[128];
    char sceneGbk[128];

    if (!g_vm_mock_world_boss_config.enabled ||
        g_vm_mock_world_boss_config.monsterId == 0)
        return;
    if (g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_ACTIVE ||
        g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_CLAIMED)
        vm_mock_world_boss_despawn_active("hour-respawn");
    memset(scene, 0, sizeof(scene));
    if (!vm_mock_world_boss_pick_scene_and_pos(scene, sizeof(scene), &x, &y))
        return;
    if (!vm_net_mock_runtime_inject_combat_spawn(
            scene, x, y, g_vm_mock_world_boss_config.monsterId, &wireId,
            &spawnIndex, &spawnError))
    {
        printf("[error][mock-service] world_boss_spawn_fail scene=%s err=%s\n",
               scene, spawnError ? spawnError : "-");
        return;
    }
    memset(&g_vm_mock_world_boss_rt, 0, sizeof(g_vm_mock_world_boss_rt));
    g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_ACTIVE;
    snprintf(g_vm_mock_world_boss_rt.scene, sizeof(g_vm_mock_world_boss_rt.scene),
             "%s", scene);
    g_vm_mock_world_boss_rt.x = x;
    g_vm_mock_world_boss_rt.y = y;
    g_vm_mock_world_boss_rt.wireId = wireId;
    g_vm_mock_world_boss_rt.spawnIndex = spawnIndex;
    g_vm_mock_world_boss_rt.hourKey = hourKey;
    g_vm_mock_world_boss_rt.spawnUnix = (u32)time(NULL);
    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(sceneGbk, 0, sizeof(sceneGbk));
    vm_net_mock_gbk_label_to_utf8(scene, sceneUtf8, sizeof(sceneUtf8));
    vm_mock_world_boss_scene_utf8_to_gbk(sceneUtf8[0] ? sceneUtf8 : scene,
                                         sceneGbk, sizeof(sceneGbk));
    vm_mock_world_boss_broadcast_spawn(sceneGbk, x, y);
    vm_mock_world_boss_queue_scene_resync();
}

static void vm_mock_world_boss_tick(void)
{
    time_t now = time(NULL);
    u32 secInHour = 0;
    u32 hourKey = 0;
    u16 announceSec = 30;
    char scene[64];
    char sceneUtf8[128];
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
        vm_mock_world_boss_spawn_now(hourKey);
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
        memset(sceneUtf8, 0, sizeof(sceneUtf8));
        memset(sceneGbk, 0, sizeof(sceneGbk));
        vm_net_mock_gbk_label_to_utf8(scene, sceneUtf8, sizeof(sceneUtf8));
        vm_mock_world_boss_scene_utf8_to_gbk(sceneUtf8[0] ? sceneUtf8 : scene,
                                             sceneGbk, sizeof(sceneGbk));
        vm_mock_world_boss_broadcast_preview(sceneGbk);
    }
}

bool vm_mock_world_boss_try_claim_encounter(
    const char *scene, u32 wireId, u32 posx, u32 posy, u32 *enemyIdOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (enemyIdOut)
        *enemyIdOut = 0;
    if (g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_ACTIVE)
        return false;
    if (scene == NULL || !vm_net_mock_scene_names_equal_loose(
                             scene, g_vm_mock_world_boss_rt.scene))
        return false;
    if (posx != g_vm_mock_world_boss_rt.x || posy != g_vm_mock_world_boss_rt.y)
        return false;
    if (wireId != 0 && wireId != g_vm_mock_world_boss_rt.wireId &&
        wireId != g_vm_mock_world_boss_config.monsterId)
        return false;
    g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_CLAIMED;
    g_vm_mock_world_boss_rt.claimRoleId = role ? role->roleId : 0;
  (void)vm_net_mock_runtime_remove_combat_spawn_at(
        g_vm_mock_world_boss_rt.scene, g_vm_mock_world_boss_rt.x,
        g_vm_mock_world_boss_rt.y, NULL);
    if (enemyIdOut)
        *enemyIdOut = g_vm_mock_world_boss_config.monsterId;
    g_vm_mock_world_boss_reward_pending = true;
    printf("[info][mock-service] world_boss_claim role=%u scene=%s "
           "pos=(%u,%u) enemy=%u\n",
           role ? role->roleId : 0, scene, posx, posy,
           g_vm_mock_world_boss_config.monsterId);
    return true;
}

void vm_mock_world_boss_on_battle_end(bool victory)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (!g_vm_mock_world_boss_reward_pending)
        return;
    g_vm_mock_world_boss_reward_pending = false;
    if (!victory)
    {
        printf("[info][mock-service] world_boss_battle_end victory=0\n");
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
    char sceneUtf8[128];
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
    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(sceneGbk, 0, sizeof(sceneGbk));
    vm_net_mock_gbk_label_to_utf8(g_vm_mock_world_boss_rt.scene, sceneUtf8,
                                 sizeof(sceneUtf8));
    vm_mock_world_boss_scene_utf8_to_gbk(
        sceneUtf8[0] ? sceneUtf8 : g_vm_mock_world_boss_rt.scene, sceneGbk,
        sizeof(sceneGbk));
    vm_mock_world_boss_broadcast_preview(sceneGbk);
    if (msgOut && msgCap > 0)
        snprintf(msgOut, msgCap, "已发送全服预告");
    return true;
}

bool vm_mock_world_boss_admin_force_spawn(char *msgOut, size_t msgCap)
{
    vm_mock_world_boss_load_config(false);
    if (!g_vm_mock_world_boss_config.enabled ||
        g_vm_mock_world_boss_config.monsterId == 0)
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "请先启用并配置BOSS怪物ID");
        return false;
    }
    vm_mock_world_boss_spawn_now(vm_mock_world_boss_hour_key(time(NULL)));
    if (g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_ACTIVE)
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "刷BOSS失败，检查怪物ID与户外地图");
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
