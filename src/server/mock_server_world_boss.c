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
    /* Preview announce map only — must never overwrite live SCE spawn anchors. */
    char previewScene[64];
} vm_mock_world_boss_runtime;

typedef struct
{
    char scene[64];
    u16 x;
    u16 y;
} vm_mock_world_boss_orphan;

enum
{
    VM_MOCK_WORLD_BOSS_ORPHAN_MAX = 64
};

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
static vm_mock_world_boss_orphan
    g_vm_mock_world_boss_orphans[VM_MOCK_WORLD_BOSS_ORPHAN_MAX];
static u32 g_vm_mock_world_boss_orphan_count = 0;

/* scene_task.c (same TU) — binding-only purge; never write disk .sce. */
bool vm_net_mock_sce_combat_spawn_bindings_delete_at(
    const char *scene, u16 x, u16 y, const char **errorOut);
bool vm_net_mock_sce_combat_spawn_bindings_purge_real_remaps(
    u32 monsterId, u32 *removedOut, const char **errorOut);
bool vm_net_mock_sce_decoded_append_combat_spawn(
    u8 *data, u32 *lenInOut, u32 dataCap, u16 x, u16 y, u32 realActorId,
    u32 wireActorId, const char **errorOut);
u32 vm_net_mock_sce_encode_named_update_blob(const u8 *decoded, u32 decodedLen,
                                             u8 *out, u32 outCap);
/* role.c */
u32 vm_net_mock_copy_outdoor_field_scenes(char scenes[][64], u32 cap);
bool vm_net_mock_pick_scene_spawn_xy(const char *scene, u16 *xOut, u16 *yOut);

static void vm_mock_world_boss_resolve_map_label_gbk(const char *scene,
                                                     char *out, size_t outCap);
static void vm_mock_world_boss_queue_scene_resync_for(const char *scene);
static void vm_mock_world_boss_broadcast_despawn(const char *sceneGbk, u16 x,
                                                 u16 y);
static void vm_mock_world_boss_load_config(bool force);
static void vm_mock_world_boss_despawn_active(const char *reason);

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

/*
 * Orphan tile list retained for in-process notify batching when clearing
 * leftover bindings. DB-overlay spawn no longer records inject tiles.
 */
static void vm_mock_world_boss_orphan_forget(const char *scene, u16 x, u16 y)
{
    u32 w = 0;

    if (scene == NULL || scene[0] == 0)
        return;
    for (u32 i = 0; i < g_vm_mock_world_boss_orphan_count; ++i)
    {
        if (g_vm_mock_world_boss_orphans[i].x == x &&
            g_vm_mock_world_boss_orphans[i].y == y &&
            vm_net_mock_scene_names_equal_loose(
                g_vm_mock_world_boss_orphans[i].scene, scene))
            continue;
        if (w != i)
            g_vm_mock_world_boss_orphans[w] = g_vm_mock_world_boss_orphans[i];
        ++w;
    }
    g_vm_mock_world_boss_orphan_count = w;
}

/* Collect remapped (wire≠real) binding rows for the configured boss monster. */
static u32 vm_mock_world_boss_collect_binding_orphans(
    vm_mock_world_boss_orphan *out, u32 cap)
{
    u32 n = 0;
    u32 monsterId = g_vm_mock_world_boss_config.monsterId;

    if (out == NULL || cap == 0 || monsterId == 0)
        return 0;
    /* Reload so process-restart still sees inject rows left in MySQL. */
    g_vm_net_mock_sce_combat_spawn_bindings_loaded = false;
    vm_net_mock_sce_combat_spawn_bindings_ensure_loaded();
    for (u32 i = 0; i < g_vm_net_mock_sce_combat_spawn_binding_count && n < cap;
         ++i)
    {
        const vm_net_mock_sce_combat_spawn_binding *bind =
            &g_vm_net_mock_sce_combat_spawn_bindings[i];
        bool dup = false;

        if (bind->realActorId != (u16)monsterId)
            continue;
        /* Natural wild keeps wire==real; world-boss / custom remap differs. */
        if (bind->wireActorId == bind->realActorId)
            continue;
        if (bind->scene[0] == 0 || bind->x == 0 || bind->y == 0)
            continue;
        for (u32 j = 0; j < n; ++j)
        {
            if (out[j].x == bind->x && out[j].y == bind->y &&
                vm_net_mock_scene_names_equal_loose(out[j].scene, bind->scene))
            {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;
        snprintf(out[n].scene, sizeof(out[n].scene), "%s", bind->scene);
        out[n].x = bind->x;
        out[n].y = bind->y;
        ++n;
    }
    return n;
}

static void vm_mock_world_boss_clear_orphan_spawns(const char *reason,
                                                   bool notifyClients)
{
    vm_mock_world_boss_orphan batch[VM_MOCK_WORLD_BOSS_ORPHAN_MAX];
    u32 batchCount = 0;
    u32 bindCleared = 0;
    u32 remapPurged = 0;
    const char *err = NULL;

    memset(batch, 0, sizeof(batch));
    /* Notify/resync targets: remembered tiles + live runtime home. */
    for (u32 i = 0; i < g_vm_mock_world_boss_orphan_count &&
                    batchCount < VM_MOCK_WORLD_BOSS_ORPHAN_MAX;
         ++i)
    {
        batch[batchCount++] = g_vm_mock_world_boss_orphans[i];
    }
    if (g_vm_mock_world_boss_rt.scene[0] != 0 &&
        g_vm_mock_world_boss_rt.x != 0 && g_vm_mock_world_boss_rt.y != 0 &&
        (g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_ACTIVE ||
         g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_CLAIMED))
    {
        bool dup = false;
        for (u32 j = 0; j < batchCount; ++j)
        {
            if (batch[j].x == g_vm_mock_world_boss_rt.x &&
                batch[j].y == g_vm_mock_world_boss_rt.y &&
                vm_net_mock_scene_names_equal_loose(
                    batch[j].scene, g_vm_mock_world_boss_rt.scene))
            {
                dup = true;
                break;
            }
        }
        if (!dup && batchCount < VM_MOCK_WORLD_BOSS_ORPHAN_MAX)
        {
            snprintf(batch[batchCount].scene, sizeof(batch[batchCount].scene),
                     "%s", g_vm_mock_world_boss_rt.scene);
            batch[batchCount].x = g_vm_mock_world_boss_rt.x;
            batch[batchCount].y = g_vm_mock_world_boss_rt.y;
            ++batchCount;
        }
    }
    {
        vm_mock_world_boss_orphan fromBind[VM_MOCK_WORLD_BOSS_ORPHAN_MAX];
        u32 bindN = vm_mock_world_boss_collect_binding_orphans(
            fromBind, VM_MOCK_WORLD_BOSS_ORPHAN_MAX);
        for (u32 i = 0; i < bindN && batchCount < VM_MOCK_WORLD_BOSS_ORPHAN_MAX;
             ++i)
        {
            bool dup = false;
            for (u32 j = 0; j < batchCount; ++j)
            {
                if (batch[j].x == fromBind[i].x &&
                    batch[j].y == fromBind[i].y &&
                    vm_net_mock_scene_names_equal_loose(batch[j].scene,
                                                        fromBind[i].scene))
                {
                    dup = true;
                    break;
                }
            }
            if (dup)
                continue;
            batch[batchCount++] = fromBind[i];
        }
    }

    /*
     * DB overlay contract: never rewrite disk .sce here.
     * Only strip leftover wire≠real bindings from the old inject path.
     */
    if (g_vm_mock_world_boss_config.monsterId != 0)
    {
        err = NULL;
        if (vm_net_mock_sce_combat_spawn_bindings_purge_real_remaps(
                g_vm_mock_world_boss_config.monsterId, &remapPurged, &err))
            bindCleared += remapPurged;
        else
            printf("[warn][mock-service] world_boss_binding_purge "
                   "reason=%s monster=%u err=%s\n",
                   reason ? reason : "-", g_vm_mock_world_boss_config.monsterId,
                   err ? err : "-");
    }
    for (u32 i = 0; i < batchCount; ++i)
    {
        char sceneGbk[128];

        err = NULL;
        if (vm_net_mock_sce_combat_spawn_bindings_delete_at(
                batch[i].scene, batch[i].x, batch[i].y, &err))
        {
            /* ok even when row already gone */
        }
        else
        {
            printf("[warn][mock-service] world_boss_binding_delete "
                   "reason=%s scene=%s pos=(%u,%u) err=%s\n",
                   reason ? reason : "-", batch[i].scene, (u32)batch[i].x,
                   (u32)batch[i].y, err ? err : "-");
        }
        vm_mock_world_boss_queue_scene_resync_for(batch[i].scene);
        if (notifyClients)
        {
            memset(sceneGbk, 0, sizeof(sceneGbk));
            vm_mock_world_boss_resolve_map_label_gbk(batch[i].scene, sceneGbk,
                                                     sizeof(sceneGbk));
            vm_mock_world_boss_broadcast_despawn(sceneGbk, batch[i].x,
                                                batch[i].y);
        }
    }
    g_vm_mock_world_boss_orphan_count = 0;
    memset(g_vm_mock_world_boss_orphans, 0,
           sizeof(g_vm_mock_world_boss_orphans));
    printf("[info][mock-service] world_boss_orphan_clear reason=%s "
           "targets=%u bindings=%u notify=%u sce_write=0\n",
           reason ? reason : "-", batchCount, bindCleared,
           notifyClients ? 1u : 0u);
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
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_world_boss_runtime ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "scene VARBINARY(63) NOT NULL DEFAULT '',"
            "x SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "y SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "wire_id INT UNSIGNED NOT NULL DEFAULT 250,"
            "hour_key INT UNSIGNED NOT NULL DEFAULT 0,"
            "spawn_unix INT UNSIGNED NOT NULL DEFAULT 0,"
            "claim_role_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "preview_scene VARBINARY(63) NOT NULL DEFAULT '',"
            "preview_unix INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY (id)) ENGINE=InnoDB"))
        return false;
    ready = true;
    return true;
}

static bool g_vm_mock_world_boss_runtime_loaded = false;
static bool g_vm_mock_world_boss_legacy_sce_cleared = false;

static bool vm_mock_world_boss_runtime_row(void *contextValue,
                                           unsigned int columnCount,
                                           const char *const *values,
                                           const size_t *lengths)
{
    vm_mock_world_boss_runtime *rt = (vm_mock_world_boss_runtime *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (rt == NULL || values == NULL || lengths == NULL)
        return false;
    if (values[0] && vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        rt->status = (u8)tmp;
    if (values[1] && lengths[1] > 0)
    {
        size_t n = lengths[1];
        if (n >= sizeof(rt->scene))
            n = sizeof(rt->scene) - 1u;
        memcpy(rt->scene, values[1], n);
        rt->scene[n] = 0;
    }
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        rt->x = (u16)tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        rt->y = (u16)tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        rt->wireId = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        rt->hourKey = tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        rt->spawnUnix = tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        rt->claimRoleId = tmp;
    if (values[8] && lengths[8] > 0)
    {
        size_t n = lengths[8];
        if (n >= sizeof(rt->previewScene))
            n = sizeof(rt->previewScene) - 1u;
        memcpy(rt->previewScene, values[8], n);
        rt->previewScene[n] = 0;
    }
    if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
        rt->previewUnix = tmp;
    return true;
}

static void vm_mock_world_boss_load_runtime(bool force)
{
    if (g_vm_mock_world_boss_runtime_loaded && !force)
        return;
    (void)vm_mock_world_boss_ensure_tables();
    memset(&g_vm_mock_world_boss_rt, 0, sizeof(g_vm_mock_world_boss_rt));
    g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_IDLE;
    g_vm_mock_world_boss_rt.wireId = VM_NET_MOCK_SCE_COMBAT_MAP_WIRE_FB;
    (void)vm_mysql_query(
        "SELECT status,scene,x,y,wire_id,hour_key,spawn_unix,claim_role_id,"
        "preview_scene,preview_unix FROM server_world_boss_runtime "
        "WHERE id=1 LIMIT 1",
        vm_mock_world_boss_runtime_row, &g_vm_mock_world_boss_rt);
    if (g_vm_mock_world_boss_rt.wireId == 0)
        g_vm_mock_world_boss_rt.wireId = VM_NET_MOCK_SCE_COMBAT_MAP_WIRE_FB;
    g_vm_mock_world_boss_runtime_loaded = true;
    printf("[info][mock-service] world_boss_runtime_load status=%u scene=%s "
           "pos=(%u,%u) wire=%u hour=%u\n",
           (u32)g_vm_mock_world_boss_rt.status, g_vm_mock_world_boss_rt.scene,
           (u32)g_vm_mock_world_boss_rt.x, (u32)g_vm_mock_world_boss_rt.y,
           g_vm_mock_world_boss_rt.wireId, g_vm_mock_world_boss_rt.hourKey);
}

/* One-shot: scrub historical wire≠real bindings after DB overlay (no .sce). */
static void vm_mock_world_boss_legacy_sce_cleanup_once(void)
{
    if (g_vm_mock_world_boss_legacy_sce_cleared)
        return;
    g_vm_mock_world_boss_legacy_sce_cleared = true;
    vm_mock_world_boss_load_config(false);
    if (g_vm_mock_world_boss_config.monsterId == 0)
        return;
    vm_mock_world_boss_clear_orphan_spawns("legacy-db-overlay", false);
}

static void vm_mock_world_boss_save_runtime(void)
{
    char sceneHex[160];
    char previewHex[160];
    char query[768];
    const char *scene = g_vm_mock_world_boss_rt.scene;
    const char *preview = g_vm_mock_world_boss_rt.previewScene;

    (void)vm_mock_world_boss_ensure_tables();
    if (scene == NULL)
        scene = "";
    if (preview == NULL)
        preview = "";
    if (vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) ==
            0 ||
        vm_mysql_hex_encode(preview, strlen(preview), previewHex,
                            sizeof(previewHex)) == 0)
    {
        printf("[warn][mock-service] world_boss_runtime_save hex_failed\n");
        return;
    }
    snprintf(
        query, sizeof(query),
        "INSERT INTO server_world_boss_runtime("
        "id,status,scene,x,y,wire_id,hour_key,spawn_unix,claim_role_id,"
        "preview_scene,preview_unix) VALUES(1,%u,UNHEX('%s'),%u,%u,%u,"
        "%u,%u,%u,UNHEX('%s'),%u) ON DUPLICATE KEY UPDATE "
        "status=VALUES(status),scene=VALUES(scene),x=VALUES(x),"
        "y=VALUES(y),wire_id=VALUES(wire_id),hour_key=VALUES(hour_key),"
        "spawn_unix=VALUES(spawn_unix),claim_role_id=VALUES(claim_role_id),"
        "preview_scene=VALUES(preview_scene),"
        "preview_unix=VALUES(preview_unix)",
        (u32)g_vm_mock_world_boss_rt.status, sceneHex,
        (u32)g_vm_mock_world_boss_rt.x, (u32)g_vm_mock_world_boss_rt.y,
        g_vm_mock_world_boss_rt.wireId
            ? g_vm_mock_world_boss_rt.wireId
            : (u32)VM_NET_MOCK_SCE_COMBAT_MAP_WIRE_FB,
        g_vm_mock_world_boss_rt.hourKey, g_vm_mock_world_boss_rt.spawnUnix,
        g_vm_mock_world_boss_rt.claimRoleId, previewHex,
        g_vm_mock_world_boss_rt.previewUnix);
    if (!vm_mysql_exec(query))
        printf("[warn][mock-service] world_boss_runtime_save failed err=%s\n",
               vm_mysql_last_error());
}

/*
 * True when wire/id is the world-boss identity (configured monster, runtime
 * overlay wire, or keep-id #250). Unrelated catalog wires must never remap
 * via proximity alone — that turned nearby mucus #41 into Boss #213.
 */
static bool vm_mock_world_boss_wire_is_boss_identity(u32 wireId)
{
    if (wireId == 0)
        return false;
    if (wireId == g_vm_mock_world_boss_config.monsterId)
        return true;
    if (wireId == (u32)VM_NET_MOCK_SCE_COMBAT_MAP_WIRE_FB)
        return true;
    if (g_vm_mock_world_boss_rt.wireId != 0 &&
        wireId == g_vm_mock_world_boss_rt.wireId)
        return true;
    return false;
}

static bool vm_mock_world_boss_pos_near_home(u32 posx, u32 posy)
{
    u32 dx = 0;
    u32 dy = 0;

    if (posx == g_vm_mock_world_boss_rt.x && posy == g_vm_mock_world_boss_rt.y)
        return true;
    if (posx == 0 || posy == 0)
        return false;
    dx = posx > g_vm_mock_world_boss_rt.x ? posx - g_vm_mock_world_boss_rt.x
                                          : g_vm_mock_world_boss_rt.x - posx;
    dy = posy > g_vm_mock_world_boss_rt.y ? posy - g_vm_mock_world_boss_rt.y
                                          : g_vm_mock_world_boss_rt.y - posy;
    return dx <= 96u && dy <= 96u;
}

/*
 * Battle remap: enabled + ACTIVE/CLAIMED + boss-identity wire on boss scene
 * (home/near feet optional when wire is boss id). Never hijack other catalog
 * wires just because feet are within 96px of a stale home.
 */
u32 vm_mock_world_boss_remap_battle_enemy(const char *scene, u32 wireId,
                                          u32 posx, u32 posy)
{
    bool posHit = false;
    bool idHit = false;

    vm_mock_world_boss_load_config(false);
    vm_mock_world_boss_load_runtime(false);
    if (!g_vm_mock_world_boss_config.enabled ||
        g_vm_mock_world_boss_config.monsterId == 0)
        return 0;
    if (g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_ACTIVE &&
        g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_CLAIMED)
        return 0;
    if (scene == NULL || g_vm_mock_world_boss_rt.scene[0] == 0 ||
        !vm_net_mock_scene_names_equal_loose(scene,
                                             g_vm_mock_world_boss_rt.scene))
        return 0;
    idHit = vm_mock_world_boss_wire_is_boss_identity(wireId);
    posHit = vm_mock_world_boss_pos_near_home(posx, posy);
    /* Require boss wire/id — proximity alone must not steal outdoor trash mobs. */
    if (!idHit)
        return 0;
    if (!posHit && posx != 0 && posy != 0)
        return 0;
    return g_vm_mock_world_boss_config.monsterId;
}

u32 vm_mock_world_boss_configured_monster_id(void)
{
    vm_mock_world_boss_load_config(false);
    if (!g_vm_mock_world_boss_config.enabled)
        return 0;
    return g_vm_mock_world_boss_config.monsterId;
}

bool vm_mock_world_boss_active_anchor(char *sceneOut, size_t sceneCap, u16 *xOut,
                                      u16 *yOut, u32 *wireOut, u32 *monsterOut)
{
    vm_mock_world_boss_load_config(false);
    vm_mock_world_boss_load_runtime(false);
    if (!g_vm_mock_world_boss_config.enabled)
        return false;
    if (g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_ACTIVE)
        return false;
    if (g_vm_mock_world_boss_rt.scene[0] == 0 ||
        g_vm_mock_world_boss_rt.x == 0 || g_vm_mock_world_boss_rt.y == 0)
        return false;
    if (sceneOut != NULL && sceneCap > 0)
        snprintf(sceneOut, sceneCap, "%s", g_vm_mock_world_boss_rt.scene);
    if (xOut)
        *xOut = g_vm_mock_world_boss_rt.x;
    if (yOut)
        *yOut = g_vm_mock_world_boss_rt.y;
    if (wireOut)
        *wireOut = g_vm_mock_world_boss_rt.wireId
                       ? g_vm_mock_world_boss_rt.wireId
                       : (u32)VM_NET_MOCK_SCE_COMBAT_MAP_WIRE_FB;
    if (monsterOut)
        *monsterOut = g_vm_mock_world_boss_config.monsterId;
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
    vm_mock_world_boss_load_runtime(force);
    if (!g_vm_mock_world_boss_config.enabled &&
        g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_IDLE)
    {
        printf("[info][mock-service] world_boss_disabled_clear status=%u "
               "scene=%s evidence=config-enabled-0\n",
               (u32)g_vm_mock_world_boss_rt.status,
               g_vm_mock_world_boss_rt.scene);
        vm_mock_world_boss_despawn_active("config-disabled-load");
    }
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
    char sceneCopy[64];
    u16 x = 0;
    u16 y = 0;
    u8 prevStatus = 0;

    if (g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_IDLE &&
        g_vm_mock_world_boss_rt.scene[0] == 0 &&
        g_vm_mock_world_boss_orphan_count == 0)
    {
        /* Still sweep binding-table remaps after restart / preview orphans. */
        vm_mock_world_boss_load_config(false);
        if (g_vm_mock_world_boss_config.monsterId == 0)
            return;
    }
    prevStatus = g_vm_mock_world_boss_rt.status;
    snprintf(sceneCopy, sizeof(sceneCopy), "%s", g_vm_mock_world_boss_rt.scene);
    x = g_vm_mock_world_boss_rt.x;
    y = g_vm_mock_world_boss_rt.y;
    /*
     * Always sweep remembered + binding-table remaps. Preview used to demote
     * ACTIVE→PREVIEW and overwrite scene, orphaning the previous SCE row;
     * status-only remove then skipped the real map.
     */
    vm_mock_world_boss_load_config(false);
    vm_mock_world_boss_clear_orphan_spawns(reason, true);
    /* CLAIMED already stripped the home tile; still notify if sweep found nothing. */
    if (prevStatus == VM_MOCK_WORLD_BOSS_STATUS_CLAIMED && sceneCopy[0] != 0)
    {
        char sceneGbk[128];
        vm_mock_world_boss_queue_scene_resync_for(sceneCopy);
        memset(sceneGbk, 0, sizeof(sceneGbk));
        vm_mock_world_boss_resolve_map_label_gbk(sceneCopy, sceneGbk,
                                                 sizeof(sceneGbk));
        vm_mock_world_boss_broadcast_despawn(sceneGbk, x, y);
    }
    printf("[info][mock-service] world_boss_despawn reason=%s status=%u "
           "scene=%s pos=(%u,%u)\n",
           reason ? reason : "-", (u32)prevStatus, sceneCopy, (u32)x, (u32)y);
    memset(&g_vm_mock_world_boss_rt, 0, sizeof(g_vm_mock_world_boss_rt));
    g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_IDLE;
    g_vm_mock_world_boss_rt.wireId = VM_NET_MOCK_SCE_COMBAT_MAP_WIRE_FB;
    g_vm_mock_world_boss_reward_pending = false;
    vm_mock_world_boss_save_runtime();
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
    /* Always strip prior injects (incl. preview-orphaned / restart leftovers). */
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
        /* DB/runtime only — do not mutate stock .sce / bindings. */
        wireId = VM_NET_MOCK_SCE_COMBAT_MAP_WIRE_FB;
        spawnIndex = 0;
        (void)spawnError;
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
        /* DB overlay only — do not orphan-track for SCE remove/write. */
        vm_mock_world_boss_save_runtime();
        memset(sceneGbk, 0, sizeof(sceneGbk));
        vm_mock_world_boss_resolve_map_label_gbk(scene, sceneGbk,
                                                  sizeof(sceneGbk));
        vm_mock_world_boss_broadcast_spawn(sceneGbk, x, y);
        /* CBE: force cold SCE pull so WT18 overlay can splice kind=3 in memory. */
        vm_mock_world_boss_queue_scene_resync_for(scene);
        if (errorOut)
            *errorOut = "ok";
        printf("[info][mock-service] world_boss_spawn_db scene=%s pos=(%u,%u) "
               "wire=%u monster=%u hour=%u\n",
               scene, (u32)x, (u32)y, wireId,
               g_vm_mock_world_boss_config.monsterId, hourKey);
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
    vm_mock_world_boss_load_runtime(false);
    if (!g_vm_mock_world_boss_config.enabled)
    {
        /* Disable must clear leftover ACTIVE/CLAIMED/PREVIEW (stale DB rows). */
        if (g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_IDLE)
            vm_mock_world_boss_despawn_active("config-disabled");
        return;
    }
    vm_mock_world_boss_legacy_sce_cleanup_once();
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
        snprintf(g_vm_mock_world_boss_rt.previewScene,
                 sizeof(g_vm_mock_world_boss_rt.previewScene), "%s", scene);
        /*
         * Preview is announce-only. Never demote ACTIVE/CLAIMED or overwrite
         * live scene/x/y — that orphaned the previous SCE inject and left
         * bosses on multiple maps after the next spawn.
         */
        if (g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_IDLE)
        {
            g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_PREVIEW;
            snprintf(g_vm_mock_world_boss_rt.scene,
                     sizeof(g_vm_mock_world_boss_rt.scene), "%s", scene);
            vm_mock_world_boss_save_runtime();
        }
        else
            vm_mock_world_boss_save_runtime();
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

    if (enemyIdOut)
        *enemyIdOut = 0;
    vm_mock_world_boss_load_config(false);
    vm_mock_world_boss_load_runtime(false);
    if (!g_vm_mock_world_boss_config.enabled ||
        g_vm_mock_world_boss_config.monsterId == 0)
        return false;
    if (g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_ACTIVE)
        return false;
    if (scene == NULL || !vm_net_mock_scene_names_equal_loose(
                             scene, g_vm_mock_world_boss_rt.scene))
        return false;
    /*
     * Manual claim: boss-identity wire + home/near feet. Do not claim when the
     * client collided a different outdoor trash mob near the boss tile.
     */
    idHit = vm_mock_world_boss_wire_is_boss_identity(wireId);
    posHit = vm_mock_world_boss_pos_near_home(posx, posy);
    if (!idHit || !posHit)
        return false;
    g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_CLAIMED;
    g_vm_mock_world_boss_rt.claimRoleId = role ? role->roleId : 0;
    /* Map marker is DB overlay — no SCE kind=3 delete. */
    vm_mock_world_boss_orphan_forget(g_vm_mock_world_boss_rt.scene,
                                     g_vm_mock_world_boss_rt.x,
                                     g_vm_mock_world_boss_rt.y);
    vm_mock_world_boss_save_runtime();
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
    vm_mock_world_boss_load_runtime(false);
    if (statusOut)
        *statusOut = g_vm_mock_world_boss_rt.status;
    if (sceneOut != NULL && sceneCap > 0)
        snprintf(sceneOut, sceneCap, "%s", g_vm_mock_world_boss_rt.scene);
    if (xOut)
        *xOut = g_vm_mock_world_boss_rt.x;
    if (yOut)
        *yOut = g_vm_mock_world_boss_rt.y;
}

static bool vm_mock_world_boss_admin_pick_preview_scene(char *sceneOut,
                                                        size_t sceneCap,
                                                        char *msgOut,
                                                        size_t msgCap)
{
    char scene[64];

    if (!vm_mock_world_boss_pick_scene_and_pos(scene, sizeof(scene), NULL, NULL))
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "无可用户外地图");
        return false;
    }
    if (sceneOut != NULL && sceneCap > 0)
        snprintf(sceneOut, sceneCap, "%s", scene);
    return true;
}

bool vm_mock_world_boss_admin_force_preview(char *msgOut, size_t msgCap)
{
    char scene[64];
    char sceneGbk[128];
    u32 hourKey = 0;

    vm_mock_world_boss_load_config(false);
    if (!g_vm_mock_world_boss_config.enabled)
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "世界BOSS未启用");
        return false;
    }
    memset(scene, 0, sizeof(scene));
    if (!vm_mock_world_boss_admin_pick_preview_scene(scene, sizeof(scene),
                                                      msgOut, msgCap))
        return false;
    hourKey = vm_mock_world_boss_hour_key(time(NULL));
    g_vm_mock_world_boss_rt.previewUnix = hourKey;
    snprintf(g_vm_mock_world_boss_rt.previewScene,
             sizeof(g_vm_mock_world_boss_rt.previewScene), "%s", scene);
    /* Do not clobber live ACTIVE/CLAIMED spawn anchors. */
    if (g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_IDLE)
    {
        g_vm_mock_world_boss_rt.status = VM_MOCK_WORLD_BOSS_STATUS_PREVIEW;
        snprintf(g_vm_mock_world_boss_rt.scene,
                 sizeof(g_vm_mock_world_boss_rt.scene), "%s", scene);
    }
    vm_mock_world_boss_save_runtime();
    memset(sceneGbk, 0, sizeof(sceneGbk));
    vm_mock_world_boss_resolve_map_label_gbk(scene, sceneGbk, sizeof(sceneGbk));
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
    vm_mock_world_boss_load_config(false);
    if (g_vm_mock_world_boss_rt.status == VM_MOCK_WORLD_BOSS_STATUS_IDLE &&
        g_vm_mock_world_boss_rt.scene[0] == 0 &&
        g_vm_mock_world_boss_orphan_count == 0 &&
        g_vm_mock_world_boss_config.monsterId == 0)
    {
        if (msgOut && msgCap > 0)
            snprintf(msgOut, msgCap, "当前无活跃BOSS");
        return false;
    }
    vm_mock_world_boss_despawn_active("admin");
    if (msgOut && msgCap > 0)
        snprintf(msgOut, msgCap, "已清除当前BOSS及残留刷怪点");
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
    /* Saving enabled=0 must immediately drop map overlay + battle remap. */
    if (!g_vm_mock_world_boss_config.enabled &&
        g_vm_mock_world_boss_rt.status != VM_MOCK_WORLD_BOSS_STATUS_IDLE)
        vm_mock_world_boss_despawn_active("admin-disable");
    return true;
}

/*
 * WT18 content-version stamp: when ACTIVE boss sits on this leaf, xor spawn
 * unix so clientmiss does not keep stock SCE (disk bytes unchanged).
 */
u16 vm_mock_world_boss_content_version_stamp(const char *name, u16 diskVersion)
{
    char scene[64];
    u16 x = 0;
    u16 y = 0;
    u32 wire = 0;
    u32 monster = 0;
    u16 stamped = diskVersion;

    if (name == NULL || name[0] == 0 || diskVersion == 0)
        return diskVersion;
    if (!vm_mock_world_boss_active_anchor(scene, sizeof(scene), &x, &y, &wire,
                                         &monster))
        return diskVersion;
    if (!vm_net_mock_scene_names_equal_loose(name, scene))
    {
        char withSce[72];
        if (strlen(scene) + 4u >= sizeof(withSce))
            return diskVersion;
        snprintf(withSce, sizeof(withSce), "%s.sce", scene);
        if (!vm_net_mock_scene_names_equal_loose(name, withSce))
            return diskVersion;
    }
    stamped = (u16)((((u32)diskVersion ^
                      (g_vm_mock_world_boss_rt.spawnUnix & 0xffffu) ^ 0xb055u) %
                     65535u) +
                    1u);
    if (stamped == diskVersion)
        stamped = (u16)((diskVersion % 65534u) + 1u);
    return stamped;
}

/*
 * After disk SCE bytes loaded for WT18: splice ACTIVE boss kind=3 in memory
 * and re-encode. Returns 0 to keep disk bytes.
 */
u32 vm_mock_world_boss_overlay_update_payload(const char *payloadName, u8 *out,
                                              u32 outCap, u32 diskLen)
{
    char scene[64];
    u16 x = 0;
    u16 y = 0;
    u32 wire = 0;
    u32 monster = 0;
    u8 *decoded = NULL;
    u32 decodedLen = 0;
    u32 encodedLen = 0;
    const char *err = NULL;

    if (payloadName == NULL || out == NULL || outCap == 0 || diskLen == 0)
        return 0;
    if (!vm_mock_world_boss_active_anchor(scene, sizeof(scene), &x, &y, &wire,
                                         &monster) ||
        monster == 0)
        return 0;
    if (!vm_net_mock_scene_names_equal_loose(payloadName, scene))
    {
        char withSce[72];
        if (strlen(scene) + 4u >= sizeof(withSce))
            return 0;
        snprintf(withSce, sizeof(withSce), "%s.sce", scene);
        if (!vm_net_mock_scene_names_equal_loose(payloadName, withSce))
            return 0;
    }
    decoded = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    if (decoded == NULL)
        return 0;
    decodedLen = vm_net_mock_load_scene_resource(scene, decoded,
                                                 VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    if (decodedLen == 0)
    {
        free(decoded);
        return 0;
    }
    if (!vm_net_mock_sce_decoded_append_combat_spawn(
            decoded, &decodedLen, VM_NET_MOCK_SCE_EDIT_DECODE_MAX, x, y,
            monster, wire ? wire : (u32)VM_NET_MOCK_SCE_COMBAT_MAP_WIRE_FB,
            &err))
    {
        free(decoded);
        return 0;
    }
    encodedLen =
        vm_net_mock_sce_encode_named_update_blob(decoded, decodedLen, out, outCap);
    free(decoded);
    if (encodedLen == 0 || encodedLen == diskLen)
        return 0;
    printf("[info][mock-service] world_boss_wt18_overlay scene=%s pos=(%u,%u) "
           "wire=%u monster=%u disk=%u overlay=%u\n",
           scene, (u32)x, (u32)y, wire, monster, diskLen, encodedLen);
    return encodedLen;
}
